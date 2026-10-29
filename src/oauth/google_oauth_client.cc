#include "oauth/google_oauth_client.h"

#include <QDesktopServices>
#include <QEventLoop>
#include <QScopedValueRollback>
#include <QTimer>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include "oauth/loopback_http_server.h"
#include "oauth/pkce.h"
#include "oauth/token_store.h"
#include "passvault/oauth/google_oauth_config.h"

namespace passvault::oauth {

namespace {

constexpr const char* kAuthEndpoint =
    "https://accounts.google.com/o/oauth2/v2/auth";
constexpr const char* kTokenEndpoint = "https://oauth2.googleapis.com/token";
constexpr const char* kRevokeEndpoint =
    "https://oauth2.googleapis.com/revoke";
constexpr const char* kDefaultScope =
    "https://www.googleapis.com/auth/drive.file";

}  // namespace

GoogleOAuthClient::GoogleOAuthClient(TokenStore* token_store, QObject* parent)
    : QObject(parent),
      token_store_(token_store),
      client_id_(QString::fromLatin1(kGoogleDesktopClientId)),
      client_secret_(QString::fromLatin1(kGoogleDesktopClientSecret)),
      scope_(QString::fromLatin1(kDefaultScope)) {
    connect(this, &GoogleOAuthClient::AccessTokenRefreshed, this, [this]() {
        account_refresh_reply_.clear();
        if (account_lookup_pending_) RequestAccountEmail();
    });
    connect(this, &GoogleOAuthClient::AccessTokenRefreshFailed, this,
            [this](const QString&) {
                account_refresh_reply_.clear();
                if (!account_lookup_pending_) return;
                account_lookup_pending_ = false;
                emit AccountLookupFinished(
                    refresh_requires_authorization_
                        ? AccountStatus::kNeedsAuthorization
                        : AccountStatus::kNetworkError, QString());
            });
}

void GoogleOAuthClient::CancelAccountLookup() {
    ++account_generation_;
    account_lookup_pending_ = false;
    if (account_refresh_reply_) {
        ++generation_;
        auto* reply = account_refresh_reply_.data();
        account_refresh_reply_.clear();
        replies_.remove(reply);
        reply->abort();
    }
    if (account_reply_) {
        auto* reply = account_reply_.data();
        account_reply_.clear();
        replies_.remove(reply);
        reply->abort();
    }
}

void GoogleOAuthClient::LookupAccount() {
    CancelAccountLookup();
    if (paused_ || is_busy()) {
        emit AccountLookupFinished(AccountStatus::kNetworkError, QString());
        return;
    }
    account_lookup_pending_ = true;
    if (!access_token_.isEmpty() &&
        access_token_expires_at_ > QDateTime::currentDateTimeUtc().addSecs(60)) {
        RequestAccountEmail();
    } else {
        RefreshAccessToken();
    }
}

void GoogleOAuthClient::SetPaused(bool paused) {
    if (paused) CancelAccountLookup();
    paused_ = paused;
}

void GoogleOAuthClient::RequestAccountEmail() {
    EnsureNetwork();
    QNetworkRequest request{QUrl(QStringLiteral(
        "https://www.googleapis.com/drive/v3/about?fields=user(emailAddress)"))};
    request.setTransferTimeout(15000);
    request.setRawHeader("Authorization", "Bearer " + access_token_.toUtf8());
    auto* reply = network_->get(request);
    account_reply_ = reply;
    replies_.insert(reply);
    const auto generation = account_generation_;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        replies_.remove(reply);
        reply->deleteLater();
        if (generation != account_generation_ || !account_lookup_pending_) return;
        account_reply_.clear();
        account_lookup_pending_ = false;
        const int http = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (http == 401) {
            access_token_.clear();
            access_token_expires_at_ = {};
            emit AccountLookupFinished(AccountStatus::kNeedsAuthorization, QString());
            return;
        }
        if (reply->error() != QNetworkReply::NoError || http != 200) {
            emit AccountLookupFinished(AccountStatus::kNetworkError, QString());
            return;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            emit AccountLookupFinished(AccountStatus::kNetworkError, QString());
            return;
        }
        const QString email = document.object().value(QStringLiteral("user"))
            .toObject().value(QStringLiteral("emailAddress")).toString().trimmed();
        emit AccountLookupFinished(email.isEmpty() ? AccountStatus::kMissingEmail
                                                  : AccountStatus::kConnected,
                                   email);
    });
}

GoogleOAuthClient::~GoogleOAuthClient() {
    if (owns_network_ && network_ != nullptr) {
        network_->deleteLater();
    }
    if (server_ != nullptr) {
        server_->Stop();
        server_->deleteLater();
    }
}

void GoogleOAuthClient::EnsureNetwork() {
    if (network_ != nullptr) return;
    network_ = new QNetworkAccessManager(this);
    owns_network_ = true;
}

void GoogleOAuthClient::Authorize() {
    CancelAccountLookup();
    if (paused_ || is_busy()) return;
    if (client_id_.isEmpty() || client_secret_.isEmpty()) {
        emit AuthorizationFailed(
            QStringLiteral("OAuth client id/secret not configured"));
        return;
    }
    if (server_ != nullptr) {
        server_->Stop();
        server_->deleteLater();
        server_ = nullptr;
    }
    server_ = new LoopbackHttpServer(this);
    if (!server_->Start()) {
        emit AuthorizationFailed(
            QStringLiteral("Failed to bind loopback port"));
        server_->deleteLater();
        server_ = nullptr;
        return;
    }
    authorizing_ = true;
    const auto generation = generation_;
    const auto pkce = Pkce::Generate();
    expected_state_ = Pkce::RandomState();
    pending_verifier_ = pkce.verifier;
    const QString redirect = server_->redirect_uri();

    connect(server_, &LoopbackHttpServer::CallbackReceived, this,
            [this, redirect, generation](const QHash<QString, QString>& params) {
                if (paused_ || generation != generation_) return;
                authorizing_ = false;
                const QString state = params.value(QStringLiteral("state"));
                const QString code = params.value(QStringLiteral("code"));
                const QString err = params.value(QStringLiteral("error"));
                if (!err.isEmpty()) {
                    emit AuthorizationFailed(err);
                    server_->Stop();
                    return;
                }
                if (state != expected_state_) {
                    emit AuthorizationFailed(
                        QStringLiteral("state mismatch"));
                    server_->Stop();
                    return;
                }
                if (code.isEmpty()) {
                    emit AuthorizationFailed(QStringLiteral("missing code"));
                    server_->Stop();
                    return;
                }
                ExchangeCode(code, redirect, pending_verifier_);
                server_->Stop();
            });
    connect(server_, &LoopbackHttpServer::Error, this,
            [this, generation](const QString& m) {
                if (paused_ || generation != generation_) return;
                authorizing_ = false;
                emit AuthorizationFailed(m);
            });

    OpenAuthorizationUrl(redirect, pkce.challenge, expected_state_);
}

void GoogleOAuthClient::CancelAuthorization() {
    CancelAccountLookup();
    ++generation_;
    authorizing_ = false;
    expected_state_.clear();
    pending_verifier_.clear();
    if (server_ == nullptr) return;
    server_->Stop();
    server_->deleteLater();
    server_ = nullptr;
}

void GoogleOAuthClient::OpenAuthorizationUrl(const QString& redirect_uri,
                                             const QString& challenge,
                                             const QString& state) {
    QUrl url(QString::fromLatin1(kAuthEndpoint));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    q.addQueryItem(QStringLiteral("client_id"), client_id_);
    q.addQueryItem(QStringLiteral("redirect_uri"), redirect_uri);
    q.addQueryItem(QStringLiteral("scope"), scope_);
    q.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
    q.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
    q.addQueryItem(QStringLiteral("code_challenge"), challenge);
    q.addQueryItem(QStringLiteral("code_challenge_method"),
                   QStringLiteral("S256"));
    q.addQueryItem(QStringLiteral("state"), state);
    url.setQuery(q);
    QDesktopServices::openUrl(url);
}

void GoogleOAuthClient::ExchangeCode(const QString& code,
                                     const QString& redirect_uri,
                                     const QString& verifier) {
    if (paused_) return;
    EnsureNetwork();
    QNetworkRequest req{QUrl(QString::fromLatin1(kTokenEndpoint))};
    req.setTransferTimeout(25000);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("grant_type"),
                      QStringLiteral("authorization_code"));
    body.addQueryItem(QStringLiteral("code"), code);
    body.addQueryItem(QStringLiteral("redirect_uri"), redirect_uri);
    body.addQueryItem(QStringLiteral("client_id"), client_id_);
    body.addQueryItem(QStringLiteral("client_secret"), client_secret_);
    body.addQueryItem(QStringLiteral("code_verifier"), verifier);
    QNetworkReply* reply =
        network_->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    replies_.insert(reply);
    const auto generation = generation_;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        replies_.remove(reply);
        if (paused_ || generation != generation_) {
            reply->deleteLater();
            return;
        }
        ApplyTokenResponse(reply, false);
    });
}

QString GoogleOAuthClient::EnsureAccessToken() {
    if (paused_ || refreshing_) return {};
    if (!access_token_.isEmpty() &&
        access_token_expires_at_ > QDateTime::currentDateTimeUtc().addSecs(60)) {
        return access_token_;
    }
    QScopedValueRollback<bool> guard(refreshing_, true);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool done = false;
    bool success = false;
    const auto ok = connect(this, &GoogleOAuthClient::AccessTokenRefreshed,
                            &loop, [&]() { done = true; success = true; loop.quit(); });
    const auto failed = connect(this, &GoogleOAuthClient::AccessTokenRefreshFailed,
                                &loop, [&]() { done = true; loop.quit(); });
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(30000);
    if (!account_refresh_reply_) RefreshAccessToken();
    if (!done) loop.exec();
    if (!done) ++generation_;
    disconnect(ok);
    disconnect(failed);
    return success ? access_token_ : QString{};
}

void GoogleOAuthClient::RefreshAccessToken() {
    refresh_requires_authorization_ = false;
    if (paused_ || !replies_.isEmpty()) {
        emit AccessTokenRefreshFailed(QStringLiteral("OAuth paused or busy"));
        return;
    }
    if (token_store_ == nullptr) {
        emit AccessTokenRefreshFailed(QStringLiteral("no token store"));
        return;
    }
    const auto refresh = token_store_->LoadRefreshToken();
    if (!refresh.has_value()) {
        refresh_requires_authorization_ = true;
        emit AccessTokenRefreshFailed(
            QStringLiteral("no refresh token stored"));
        return;
    }
    if (paused_) return;
    EnsureNetwork();
    QNetworkRequest req{QUrl(QString::fromLatin1(kTokenEndpoint))};
    req.setTransferTimeout(25000);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("grant_type"),
                      QStringLiteral("refresh_token"));
    body.addQueryItem(QStringLiteral("refresh_token"), *refresh);
    body.addQueryItem(QStringLiteral("client_id"), client_id_);
    body.addQueryItem(QStringLiteral("client_secret"), client_secret_);
    QNetworkReply* reply =
        network_->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    if (account_lookup_pending_) account_refresh_reply_ = reply;
    replies_.insert(reply);
    const auto generation = generation_;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        replies_.remove(reply);
        if (paused_ || generation != generation_) {
            reply->deleteLater();
            return;
        }
        ApplyTokenResponse(reply, true);
    });
}

void GoogleOAuthClient::ApplyTokenResponse(QNetworkReply* reply,
                                           bool from_refresh) {
    reply->deleteLater();
    const QByteArray payload = reply->readAll();
    if (from_refresh) {
        const int http = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString error = QJsonDocument::fromJson(payload).object()
            .value(QStringLiteral("error")).toString();
        refresh_requires_authorization_ =
            http == 400 && error == QStringLiteral("invalid_grant");
    }
    if (reply->error() != QNetworkReply::NoError) {
        const QString msg = reply->errorString();
        if (from_refresh) {
            emit AccessTokenRefreshFailed(msg);
        } else {
            emit AuthorizationFailed(msg);
        }
        return;
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString msg = QStringLiteral("invalid token response");
        if (from_refresh) {
            emit AccessTokenRefreshFailed(msg);
        } else {
            emit AuthorizationFailed(msg);
        }
        return;
    }
    const auto obj = doc.object();
    const QString access = obj.value(QStringLiteral("access_token")).toString();
    const int expires_in = obj.value(QStringLiteral("expires_in")).toInt(0);
    const QString refresh =
        obj.value(QStringLiteral("refresh_token")).toString();
    if (access.isEmpty()) {
        const QString msg = QStringLiteral("no access_token in response");
        if (from_refresh) {
            emit AccessTokenRefreshFailed(msg);
        } else {
            emit AuthorizationFailed(msg);
        }
        return;
    }
    access_token_ = access;
    access_token_expires_at_ =
        QDateTime::currentDateTimeUtc().addSecs(expires_in > 0 ? expires_in
                                                               : 3600);
    if (!refresh.isEmpty() && token_store_ != nullptr) {
        token_store_->SaveRefreshToken(refresh);
    }
    if (from_refresh) {
        emit AccessTokenRefreshed();
    } else {
        emit AuthorizationSucceeded();
    }
}

bool GoogleOAuthClient::ForgetCredentials() {
    CancelAuthorization();
    access_token_.fill(QChar(0));
    access_token_.clear();
    access_token_expires_at_ = {};
    return token_store_ && token_store_->Clear();
}

void GoogleOAuthClient::RevokeTokens() {
    CancelAccountLookup();
    if (paused_ || is_busy()) return;
    if (token_store_ == nullptr) {
        emit RevokeFailed(QStringLiteral("no token store"));
        return;
    }
    const auto refresh = token_store_->LoadRefreshToken();
    QString token_to_revoke = refresh.value_or(QString());
    if (token_to_revoke.isEmpty()) token_to_revoke = access_token_;
    if (token_to_revoke.isEmpty()) {
        token_store_->Clear();
        access_token_.clear();
        access_token_expires_at_ = {};
        emit Revoked();
        return;
    }
    EnsureNetwork();
    QUrl url(QString::fromLatin1(kRevokeEndpoint));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("token"), token_to_revoke);
    url.setQuery(q);
    QNetworkRequest req{url};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* reply = network_->post(req, QByteArray());
    replies_.insert(reply);
    const auto generation = generation_;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        replies_.remove(reply);
        reply->deleteLater();
        if (paused_ || generation != generation_) return;
        token_store_->Clear();
        access_token_.clear();
        access_token_expires_at_ = {};
        if (reply->error() != QNetworkReply::NoError) {
            emit RevokeFailed(reply->errorString());
        } else {
            emit Revoked();
        }
    });
}

}  // namespace passvault::oauth
