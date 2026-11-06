#pragma once

#include <QString>

#include "ui/app_dialog.h"

class QLabel;
class QPushButton;

namespace passvault::oauth {
class GoogleOAuthClient;
}  // namespace passvault::oauth

namespace passvault::ui {

// Simple wizard around GoogleOAuthClient::Authorize():
//   1. show intent + start button
//   2. wait for AuthorizationSucceeded / AuthorizationFailed signals
//   3. close on success, allow retry on failure
class OAuthWizardDialog : public AppDialog {
    Q_OBJECT

 public:
    OAuthWizardDialog(oauth::GoogleOAuthClient* client,
                      QWidget* parent = nullptr);

 signals:
    void ConnectedSuccessfully();

 private slots:
    void OnStart();
    void OnCancel();
    void OnSuccess();
    void OnFailure(const QString& message);

 public slots:
    // Esc and the header close button must also stop the browser flow.
    void reject() override;

 private:
    oauth::GoogleOAuthClient* client_;
    QLabel* description_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* start_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

}  // namespace passvault::ui
