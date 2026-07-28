#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace passvault::oauth {

class LoopbackHttpServer;
class TokenStore;

struct TokenBundle {
    QString access_token;
    QString refresh_token;
    QDateTime expires_at;
};

class GoogleOAuthClient : public QObject {
    Q_OBJECT

 public:
    explicit GoogleOAuthClient(TokenStore* token_store,
                               QObject* parent = nullptr);
    ~GoogleOAuthClient() override;

    void set_scope(const QString& scope) { scope_ = scope; }
    void set_client_id(QString id) { client_id_ = std::move(id); }
    void set_client_secret(QString s) { client_secret_ = std::move(s); }
    void set_network_manager(QNetworkAccessManager* mgr) {
        network_ = mgr;
        owns_network_ = false;
    }

    void Authorize();

    void RefreshAccessToken();

    void RevokeTokens();

    bool HasAccessToken() const { return !access_token_.isEmpty(); }
    QString access_token() const { return access_token_; }
    QDateTime access_token_expires_at() const { return access_token_expires_at_; }

 signals:
    void AuthorizationSucceeded();
    void AuthorizationFailed(const QString& message);
    void AccessTokenRefreshed();
    void AccessTokenRefreshFailed(const QString& message);
    void Revoked();
    void RevokeFailed(const QString& message);

 private:
    void EnsureNetwork();
    void OpenAuthorizationUrl(const QString& redirect_uri,
                              const QString& challenge, const QString& state);
    void ExchangeCode(const QString& code, const QString& redirect_uri,
                      const QString& verifier);
    void ApplyTokenResponse(QNetworkReply* reply, bool from_refresh);

    TokenStore* token_store_ = nullptr;
    QNetworkAccessManager* network_ = nullptr;
    bool owns_network_ = false;

    QString client_id_;
    QString client_secret_;
    QString scope_;

    LoopbackHttpServer* server_ = nullptr;
    QString expected_state_;
    QString pending_verifier_;

    QString access_token_;
    QDateTime access_token_expires_at_;
};

}  // namespace passvault::oauth
