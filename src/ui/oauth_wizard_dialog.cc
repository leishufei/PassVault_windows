#include "ui/oauth_wizard_dialog.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "oauth/google_oauth_client.h"

namespace passvault::ui {

OAuthWizardDialog::OAuthWizardDialog(oauth::GoogleOAuthClient* client,
                                     QWidget* parent)
    : AppDialog(QStringLiteral("连接 Google Drive"), parent), client_(client) {
    setObjectName(QStringLiteral("OAuthWizardDialog"));
    SetIcon(QStringLiteral("cloud"), QStringLiteral("primary"));
    SetCardWidth(480);

    description_ = new QLabel(
        QStringLiteral(
            "点击“开始授权”后，在浏览器中登录 Google 账号，完成后此窗口将自动关闭。"
            "PassVault 仅访问自己创建的云端备份。"),
        body());
    description_->setObjectName(QStringLiteral("DialogSubtitle"));
    description_->setWordWrap(true);
    body_layout()->addWidget(description_);

    status_ = new QLabel(body());
    status_->setObjectName(QStringLiteral("OAuthStatus"));
    status_->setWordWrap(true);
    status_->hide();
    body_layout()->addWidget(status_);

    cancel_ = AddButton(QMessageBox::Cancel);
    start_ = AddButton(QStringLiteral("开始授权"), QMessageBox::ActionRole,
                       ButtonStyle::kPrimary);
    // Starting opens the browser; it only happens on an explicit click.
    disconnect(start_, &QPushButton::clicked, this, nullptr);
    connect(start_, &QPushButton::clicked, this, &OAuthWizardDialog::OnStart);
    SetDefaultButton(start_);

    if (client_) {
        connect(client_, &oauth::GoogleOAuthClient::AuthorizationSucceeded,
                this, &OAuthWizardDialog::OnSuccess);
        connect(client_, &oauth::GoogleOAuthClient::AuthorizationFailed, this,
                &OAuthWizardDialog::OnFailure);
    }
}

void OAuthWizardDialog::OnStart() {
    if (!client_) return;
    start_->setEnabled(false);
    status_->setText(QStringLiteral("已打开浏览器，正在等待授权..."));
    status_->show();
    client_->Authorize();
}

void OAuthWizardDialog::OnCancel() { reject(); }

void OAuthWizardDialog::reject() {
    if (client_ && result() != QDialog::Accepted) client_->CancelAuthorization();
    AppDialog::reject();
}

void OAuthWizardDialog::OnSuccess() {
    status_->setText(QStringLiteral("授权成功。"));
    emit ConnectedSuccessfully();
    accept();
}

void OAuthWizardDialog::OnFailure(const QString& message) {
    status_->setText(QStringLiteral("授权失败：%1").arg(message));
    status_->show();
    start_->setEnabled(true);
    start_->setText(QStringLiteral("重试"));
}

}  // namespace passvault::ui
