#include "oauth/google_oauth_client.h"

#include <QDesktopServices>
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
      scope_(QString::fromLatin1(kDefaultScope)) {}

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
    const auto pkce = Pkce::Generate();
    expected_state_ = Pkce::RandomState();
    pending_verifier_ = pkce.verifier;
    const QString redirect = server_->redirect_uri();

    connect(server_, &LoopbackHttpServer::CallbackReceived, this,
            [this, redirect](const QHash<QString, QString>& params) {
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
            [this](const QString& m) { emit AuthorizationFailed(m); });

    OpenAuthorizationUrl(redirect, pkce.challenge, expected_state_);
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
    EnsureNetwork();
    QNetworkRequest req{QUrl(QString::fromLatin1(kTokenEndpoint))};
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
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { ApplyTokenResponse(reply, false); });
}

void GoogleOAuthClient::RefreshAccessToken() {
    if (token_store_ == nullptr) {
        emit AccessTokenRefreshFailed(QStringLiteral("no token store"));
        return;
    }
    const auto refresh = token_store_->LoadRefreshToken();
    if (!refresh.has_value()) {
        emit AccessTokenRefreshFailed(
            QStringLiteral("no refresh token stored"));
        return;
    }
    EnsureNetwork();
    QNetworkRequest req{QUrl(QString::fromLatin1(kTokenEndpoint))};
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
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { ApplyTokenResponse(reply, true); });
}

void GoogleOAuthClient::ApplyTokenResponse(QNetworkReply* reply,
                                           bool from_refresh) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        const QString msg = reply->errorString();
        if (from_refresh) {
            emit AccessTokenRefreshFailed(msg);
        } else {
            emit AuthorizationFailed(msg);
        }
        return;
    }
    const QByteArray payload = reply->readAll();
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

void GoogleOAuthClient::RevokeTokens() {
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
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
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
