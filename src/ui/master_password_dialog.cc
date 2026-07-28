#include "ui/master_password_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace passvault::ui {

namespace {

QString TitleFor(MasterPasswordDialog::Mode mode) {
    switch (mode) {
        case MasterPasswordDialog::Mode::kSetup:
            return QStringLiteral("设置主密码");
        case MasterPasswordDialog::Mode::kUnlock:
            return QStringLiteral("解锁 PassVault");
        case MasterPasswordDialog::Mode::kChange:
            return QStringLiteral("修改主密码");
    }
    return {};
}

QString SubtitleFor(MasterPasswordDialog::Mode mode) {
    switch (mode) {
        case MasterPasswordDialog::Mode::kSetup:
            return QStringLiteral(
                "主密码用于本地和云端加密。请务必记住，"
                "PassVault 不会为您找回主密码。");
        case MasterPasswordDialog::Mode::kUnlock:
            return QStringLiteral("输入主密码继续使用您的密码库。");
        case MasterPasswordDialog::Mode::kChange:
            return QStringLiteral(
                "先输入当前主密码，再设置新的主密码。所有密码将使用新密码重新加密。");
    }
    return {};
}

}  // namespace

MasterPasswordDialog::MasterPasswordDialog(Mode mode, QWidget* parent)
    : QDialog(parent), mode_(mode) {
    setObjectName(QStringLiteral("MasterPasswordDialog"));
    setWindowTitle(TitleFor(mode));
    setModal(true);
    setMinimumWidth(420);
    BuildUi();
}

MasterPasswordDialog::~MasterPasswordDialog() {
    if (old_edit_) old_edit_->setText(QString());
    if (new_edit_) new_edit_->setText(QString());
    if (confirm_edit_) confirm_edit_->setText(QString());
}

QString MasterPasswordDialog::OldPassword() const {
    return old_edit_ ? old_edit_->text() : QString();
}

QString MasterPasswordDialog::NewPassword() const {
    return new_edit_ ? new_edit_->text() : QString();
}

void MasterPasswordDialog::SetHelloAvailable(bool available) {
    if (hello_button_) hello_button_->setVisible(available);
}

void MasterPasswordDialog::SetErrorText(const QString& text) {
    if (!error_) return;
    error_->setText(text);
    error_->setVisible(!text.isEmpty());
}

void MasterPasswordDialog::BuildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(14);

    title_ = new QLabel(TitleFor(mode_), this);
    title_->setObjectName(QStringLiteral("DialogTitle"));
    subtitle_ = new QLabel(SubtitleFor(mode_), this);
    subtitle_->setObjectName(QStringLiteral("DialogSubtitle"));
    subtitle_->setWordWrap(true);
    root->addWidget(title_);
    root->addWidget(subtitle_);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setSpacing(10);

    const bool needs_old = mode_ == Mode::kChange;
    const bool needs_confirm =
        mode_ == Mode::kSetup || mode_ == Mode::kChange;

    if (needs_old) {
        old_edit_ = new QLineEdit(this);
        old_edit_->setObjectName(QStringLiteral("OldPasswordEdit"));
        old_edit_->setEchoMode(QLineEdit::Password);
        old_edit_->setPlaceholderText(QStringLiteral("当前主密码"));
        form->addRow(QStringLiteral("当前主密码"), old_edit_);
    }

    new_edit_ = new QLineEdit(this);
    new_edit_->setObjectName(QStringLiteral("NewPasswordEdit"));
    new_edit_->setEchoMode(QLineEdit::Password);
    new_edit_->setPlaceholderText(mode_ == Mode::kUnlock
                                      ? QStringLiteral("主密码")
                                      : QStringLiteral("新主密码"));
    form->addRow(mode_ == Mode::kUnlock ? QStringLiteral("主密码")
                                        : QStringLiteral("新主密码"),
                 new_edit_);
    if (mode_ == Mode::kUnlock) {
        new_edit_->setText("12345678");
    }

    if (needs_confirm) {
        confirm_edit_ = new QLineEdit(this);
        confirm_edit_->setObjectName(QStringLiteral("ConfirmPasswordEdit"));
        confirm_edit_->setEchoMode(QLineEdit::Password);
        confirm_edit_->setPlaceholderText(QStringLiteral("再次输入以确认"));
        form->addRow(QStringLiteral("确认"), confirm_edit_);
    }

    root->addLayout(form);

    preview_ = new QCheckBox(QStringLiteral("显示密码"), this);
    preview_->setObjectName(QStringLiteral("PreviewCheckbox"));
    connect(preview_, &QCheckBox::toggled, this,
            &MasterPasswordDialog::OnTogglePreview);
    root->addWidget(preview_);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("FormError"));
    error_->setWordWrap(true);
    error_->setVisible(false);
    root->addWidget(error_);

    auto* buttons_row = new QHBoxLayout;
    hello_button_ =
        new QPushButton(QStringLiteral("使用 Windows Hello"), this);
    hello_button_->setObjectName(QStringLiteral("HelloButton"));
    hello_button_->setVisible(false);
    hello_button_->setProperty("flat", true);
    connect(hello_button_, &QPushButton::clicked, this,
            &MasterPasswordDialog::HelloRequested);
    buttons_row->addWidget(hello_button_);
    buttons_row->addStretch();

    auto* box = new QDialogButtonBox(this);
    auto* cancel = box->addButton(QDialogButtonBox::Cancel);
    cancel->setText(QStringLiteral("取消"));
    primary_ = box->addButton(mode_ == Mode::kUnlock
                                  ? QStringLiteral("解锁")
                                  : QStringLiteral("确定"),
                              QDialogButtonBox::AcceptRole);
    primary_->setObjectName(QStringLiteral("PrimaryButton"));
    primary_->setProperty("accent", true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(primary_, &QPushButton::clicked, this,
            &MasterPasswordDialog::OnAccept);
    buttons_row->addWidget(box);

    root->addLayout(buttons_row);

    new_edit_->setFocus();
    if (old_edit_) old_edit_->setFocus();
}

void MasterPasswordDialog::OnTogglePreview(bool visible) {
    const QLineEdit::EchoMode mode =
        visible ? QLineEdit::Normal : QLineEdit::Password;
    if (old_edit_) old_edit_->setEchoMode(mode);
    if (new_edit_) new_edit_->setEchoMode(mode);
    if (confirm_edit_) confirm_edit_->setEchoMode(mode);
}

void MasterPasswordDialog::ValidateInputs() {
    error_->clear();
    error_->setVisible(false);
}

void MasterPasswordDialog::OnAccept() {
    const QString new_pwd = new_edit_ ? new_edit_->text() : QString();
    const QString old_pwd = old_edit_ ? old_edit_->text() : QString();
    const QString confirm = confirm_edit_ ? confirm_edit_->text() : QString();

    if (new_pwd.isEmpty()) {
        SetErrorText(QStringLiteral("请输入主密码。"));
        return;
    }
    if (mode_ != Mode::kUnlock && new_pwd.size() < kMinPasswordLength) {
        SetErrorText(QStringLiteral("主密码至少 %1 位。").arg(kMinPasswordLength));
        return;
    }
    if (mode_ == Mode::kChange && old_pwd.isEmpty()) {
        SetErrorText(QStringLiteral("请输入当前主密码。"));
        return;
    }
    if ((mode_ == Mode::kSetup || mode_ == Mode::kChange) &&
        confirm != new_pwd) {
        SetErrorText(QStringLiteral("两次输入不一致。"));
        return;
    }
    if (mode_ == Mode::kChange && old_pwd == new_pwd) {
        SetErrorText(QStringLiteral("新主密码不能与当前主密码相同。"));
        return;
    }
    accept();
}

}  // namespace passvault::ui
