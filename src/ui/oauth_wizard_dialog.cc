#include "ui/oauth_wizard_dialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "oauth/google_oauth_client.h"

namespace passvault::ui {

OAuthWizardDialog::OAuthWizardDialog(oauth::GoogleOAuthClient* client,
                                     QWidget* parent)
    : QDialog(parent), client_(client) {
    setObjectName(QStringLiteral("OAuthWizardDialog"));
    setWindowTitle(QStringLiteral("连接 Google Drive"));
    setModal(true);
    setMinimumWidth(480);

    auto* root = new QVBoxLayout(this);
    root->setSizeConstraint(QLayout::SetFixedSize);
    root->setContentsMargins(8, 12, 8, 12);
    root->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("连接 Google Drive"), this);
    title->setObjectName(QStringLiteral("DialogTitle"));
    root->addWidget(title);

    description_ = new QLabel(
        QStringLiteral(
            "点击“开始授权”后，在浏览器中登录 Google 账号，完成后此窗口将自动关闭。"
            "PassVault 仅访问自己创建的云端备份。"),
        this);
    description_->setObjectName(QStringLiteral("DialogSubtitle"));
    description_->setFixedWidth(432);
    description_->setWordWrap(true);
    root->addWidget(description_);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("OAuthStatus"));
    status_->setWordWrap(true);
    status_->hide();
    root->addWidget(status_);

    auto* buttons = new QHBoxLayout;
    cancel_ = new QPushButton(QStringLiteral("取消"), this);
    connect(cancel_, &QPushButton::clicked, this,
            &OAuthWizardDialog::OnCancel);
    start_ = new QPushButton(QStringLiteral("开始授权"), this);
    start_->setProperty("accent", true);
    connect(start_, &QPushButton::clicked, this, &OAuthWizardDialog::OnStart);
    buttons->addStretch();
    buttons->addWidget(cancel_);
    buttons->addWidget(start_);
    root->addLayout(buttons);

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

void OAuthWizardDialog::OnCancel() {
    if (client_) client_->CancelAuthorization();
    reject();
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
