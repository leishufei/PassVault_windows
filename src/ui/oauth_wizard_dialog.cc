#include "ui/oauth_wizard_dialog.h"

#include <QDialogButtonBox>
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
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("连接 Google Drive"), this);
    title->setObjectName(QStringLiteral("DialogTitle"));
    root->addWidget(title);

    auto* card = new QWidget(this);
    card->setObjectName(QStringLiteral("OAuthDescriptionCard"));
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(0, 0, 0, 0);
    card_layout->setSpacing(0);
    description_ = new QLabel(
        QStringLiteral(
            "点击 \"开始授权\" 后，浏览器将打开 Google 登录页。"
            "授权完成后本对话框会自动关闭。"
            "PassVault 只申请 drive.file 范围——仅能访问自己创建的备份文件。"),
        card);
    description_->setWordWrap(true);
    card_layout->addWidget(description_);
    root->addWidget(card);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("OAuthStatus"));
    status_->setWordWrap(true);
    root->addWidget(status_);

    auto* buttons = new QHBoxLayout;
    cancel_ = new QPushButton(QStringLiteral("取消"), this);
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
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
    client_->Authorize();
}

void OAuthWizardDialog::OnSuccess() {
    status_->setText(QStringLiteral("授权成功。"));
    emit ConnectedSuccessfully();
    accept();
}

void OAuthWizardDialog::OnFailure(const QString& message) {
    status_->setText(QStringLiteral("授权失败：%1").arg(message));
    start_->setEnabled(true);
    start_->setText(QStringLiteral("重试"));
}

}  // namespace passvault::ui
