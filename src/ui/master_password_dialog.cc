#include "ui/master_password_dialog.h"

#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/icon_loader.h"
#include "ui/theme_manager.h"

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
                "修改后，本地保险库与已绑定的云端备份将使用新主密码。曾绑定云端时需要联网核对。");
    }
    return {};
}

}  // namespace

MasterPasswordDialog::MasterPasswordDialog(Mode mode, QWidget* parent)
    : QDialog(parent), mode_(mode) {
    setObjectName(QStringLiteral("MasterPasswordDialog"));
    setWindowTitle(TitleFor(mode));
    setModal(true);
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(560);
    BuildUi();
    RefreshThemeAssets();
    connect(ThemeManager::Instance(), &ThemeManager::ThemeChanged, this,
            [this](Theme) { RefreshThemeAssets(); });
}

MasterPasswordDialog::~MasterPasswordDialog() {
    if (backdrop_) {
        backdrop_->parentWidget()->removeEventFilter(this);
        delete backdrop_.data();
    }
    if (old_edit_) old_edit_->setText(QString());
    if (new_edit_) new_edit_->setText(QString());
    if (confirm_edit_) confirm_edit_->setText(QString());
}

bool MasterPasswordDialog::eventFilter(QObject* watched, QEvent* event) {
    if (backdrop_ && watched == backdrop_->parentWidget() &&
        event->type() == QEvent::Resize) {
        backdrop_->setGeometry(backdrop_->parentWidget()->rect());
    }
    return QDialog::eventFilter(watched, event);
}

void MasterPasswordDialog::showEvent(QShowEvent* event) {
    if (parentWidget()) {
        if (!backdrop_) {
            backdrop_ = new QWidget(parentWidget()->window());
            backdrop_->setObjectName(QStringLiteral("MasterPasswordBackdrop"));
            backdrop_->setAttribute(Qt::WA_StyledBackground);
        }
        backdrop_->parentWidget()->installEventFilter(this);
        backdrop_->setGeometry(backdrop_->parentWidget()->rect());
        backdrop_->show();
        backdrop_->raise();
    }
    QDialog::showEvent(event);
}

void MasterPasswordDialog::hideEvent(QHideEvent* event) {
    if (backdrop_) {
        backdrop_->hide();
        backdrop_->parentWidget()->removeEventFilter(this);
    }
    QDialog::hideEvent(event);
}

QString MasterPasswordDialog::OldPassword() const {
    return old_edit_ ? old_edit_->text() : QString();
}

QString MasterPasswordDialog::NewPassword() const {
    return new_edit_ ? new_edit_->text() : QString();
}

QString MasterPasswordDialog::ConfirmedPassword() const {
    return confirm_edit_ ? confirm_edit_->text() : QString();
}

void MasterPasswordDialog::SetErrorText(const QString& text) {
    if (!error_) return;
    error_->setText(text);
    error_->setVisible(!text.isEmpty());
}

void MasterPasswordDialog::BuildUi() {
    auto* root = new QVBoxLayout(this);
    // Leave room around the 440px card for its downward shadow.
    root->setContentsMargins(60, 60, 60, 84);
    root->setSpacing(0);

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("MasterPasswordCard"));
    card->setFixedWidth(440);
    shadow_ = new QGraphicsDropShadowEffect(card);
    shadow_->setBlurRadius(60);
    shadow_->setOffset(0, 24);
    card->setGraphicsEffect(shadow_);
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(1, 1, 1, 1);
    card_layout->setSpacing(0);
    root->addWidget(card);

    auto* header = new QWidget(card);
    header->setObjectName(QStringLiteral("MasterPasswordHeader"));
    header->setFixedHeight(60);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(24, 0, 24, 0);
    header_layout->setSpacing(10);

    icon_ = new QLabel(header);
    icon_->setObjectName(QStringLiteral("MasterPasswordIcon"));
    icon_->setFixedSize(32, 32);
    icon_->setAlignment(Qt::AlignCenter);
    header_layout->addWidget(icon_);

    title_ = new QLabel(TitleFor(mode_), header);
    title_->setObjectName(QStringLiteral("DialogTitle"));
    header_layout->addWidget(title_, 1);

    close_button_ = new QToolButton(header);
    close_button_->setObjectName(QStringLiteral("MasterPasswordClose"));
    close_button_->setFixedSize(32, 32);
    close_button_->setIconSize(QSize(16, 16));
    close_button_->setToolTip(QStringLiteral("关闭"));
    close_button_->setAccessibleName(QStringLiteral("关闭"));
    close_button_->setCursor(Qt::PointingHandCursor);
    connect(close_button_, &QToolButton::clicked, this, &QDialog::reject);
    header_layout->addWidget(close_button_);
    card_layout->addWidget(header);

    auto* body = new QWidget(card);
    body->setObjectName(QStringLiteral("MasterPasswordBody"));
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(24, 20, 24, 20);
    body_layout->setSpacing(16);

    subtitle_ = new QLabel(SubtitleFor(mode_), body);
    subtitle_->setObjectName(QStringLiteral("DialogSubtitle"));
    subtitle_->setWordWrap(true);
    body_layout->addWidget(subtitle_);

    const auto add_field = [this, body, body_layout](
                               const QString& name, const QString& label_text,
                               const QString& placeholder) {
        auto* field = new QWidget(body);
        auto* layout = new QVBoxLayout(field);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        auto* label = new QLabel(label_text, field);
        label->setObjectName(QStringLiteral("MasterPasswordFieldLabel"));
        auto* edit = new QLineEdit(field);
        edit->setObjectName(name);
        edit->setEchoMode(QLineEdit::Password);
        edit->setFixedHeight(32);
        edit->setPlaceholderText(placeholder);
        edit->setAccessibleName(label_text);
        label->setBuddy(edit);
        layout->addWidget(label);
        layout->addWidget(edit);
        body_layout->addWidget(field);
        connect(edit, &QLineEdit::textChanged, this,
                &MasterPasswordDialog::ValidateInputs);
        return edit;
    };

    const bool needs_old = mode_ == Mode::kChange;
    const bool needs_confirm =
        mode_ == Mode::kSetup || mode_ == Mode::kChange;

    if (needs_old) {
        old_edit_ = add_field(QStringLiteral("OldPasswordEdit"),
                              QStringLiteral("当前主密码"),
                              QStringLiteral("输入当前主密码"));
    }

    new_edit_ = add_field(
        QStringLiteral("NewPasswordEdit"),
        mode_ == Mode::kChange ? QStringLiteral("新主密码")
                              : QStringLiteral("主密码"),
        mode_ == Mode::kUnlock
            ? QStringLiteral("输入主密码")
            : QStringLiteral("至少 %1 个字符").arg(kMinPasswordLength));
    if (needs_confirm) {
        confirm_edit_ = add_field(
            QStringLiteral("ConfirmPasswordEdit"),
            mode_ == Mode::kChange ? QStringLiteral("确认新主密码")
                                  : QStringLiteral("确认主密码"),
            mode_ == Mode::kChange ? QStringLiteral("再次输入新主密码")
                                  : QStringLiteral("再次输入主密码"));
    }

    preview_ = new QCheckBox(QStringLiteral("显示密码"), body);
    preview_->setObjectName(QStringLiteral("PreviewCheckbox"));
    connect(preview_, &QCheckBox::toggled, this,
            &MasterPasswordDialog::OnTogglePreview);
    body_layout->addWidget(preview_);

    error_ = new QLabel(body);
    error_->setObjectName(QStringLiteral("FormError"));
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    error_->setVisible(false);
    body_layout->addWidget(error_);

    card_layout->addWidget(body);

    auto* footer = new QWidget(card);
    footer->setObjectName(QStringLiteral("MasterPasswordFooter"));
    auto* buttons_row = new QHBoxLayout(footer);
    buttons_row->setContentsMargins(24, 16, 24, 16);
    buttons_row->setSpacing(8);
    buttons_row->addStretch();

    auto* cancel = new QPushButton(QStringLiteral("取消"), footer);
    cancel->setObjectName(QStringLiteral("CancelButton"));
    cancel->setFixedHeight(32);
    primary_ = new QPushButton(
        mode_ == Mode::kUnlock
            ? QStringLiteral("解锁")
            : (mode_ == Mode::kSetup ? QStringLiteral("创建保险库")
                                    : QStringLiteral("确认修改")),
        footer);
    primary_->setObjectName(QStringLiteral("PrimaryButton"));
    primary_->setProperty("accent", true);
    primary_->setFixedHeight(32);
    primary_->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(primary_, &QPushButton::clicked, this,
            &MasterPasswordDialog::OnAccept);
    buttons_row->addWidget(cancel);
    buttons_row->addWidget(primary_);
    card_layout->addWidget(footer);

    if (old_edit_) setTabOrder(old_edit_, new_edit_);
    if (confirm_edit_) setTabOrder(new_edit_, confirm_edit_);
    setTabOrder(confirm_edit_ ? confirm_edit_ : new_edit_, preview_);
    setTabOrder(preview_, cancel);
    setTabOrder(cancel, primary_);
    setTabOrder(primary_, close_button_);
    setTabOrder(close_button_, old_edit_ ? old_edit_ : new_edit_);
    (old_edit_ ? old_edit_ : new_edit_)->setFocus();
}

void MasterPasswordDialog::RefreshThemeAssets() {
    auto* theme = ThemeManager::Instance();
    auto shadow_color = theme->Color(QStringLiteral("dialog-shadow"));
    shadow_color.setAlphaF(0.2);
    shadow_->setColor(shadow_color);
    icon_->setPixmap(
        IconLoader::Load(mode_ == Mode::kUnlock ? QStringLiteral("lock-keyhole")
                                               : QStringLiteral("key-round"),
                         theme->Color(QStringLiteral("primary")), 16)
            .pixmap(16, 16));
    close_button_->setIcon(IconLoader::Load(
        QStringLiteral("x"), theme->Color(QStringLiteral("muted-7")), 16));
}

void MasterPasswordDialog::OnTogglePreview(bool visible) {
    const QLineEdit::EchoMode mode =
        visible ? QLineEdit::Normal : QLineEdit::Password;
    if (old_edit_) old_edit_->setEchoMode(mode);
    if (new_edit_) new_edit_->setEchoMode(mode);
    if (confirm_edit_) confirm_edit_->setEchoMode(mode);
}

void MasterPasswordDialog::ValidateInputs() {
    SetErrorText(QString());
}

void MasterPasswordDialog::OnAccept() {
    const QString new_pwd = new_edit_ ? new_edit_->text() : QString();
    const QString old_pwd = old_edit_ ? old_edit_->text() : QString();
    const QString confirm = confirm_edit_ ? confirm_edit_->text() : QString();

    if (mode_ == Mode::kChange && old_pwd.isEmpty()) {
        SetErrorText(QStringLiteral("请输入当前主密码。"));
        old_edit_->setFocus();
        return;
    }
    if (new_pwd.isEmpty()) {
        SetErrorText(QStringLiteral("请输入主密码。"));
        new_edit_->setFocus();
        return;
    }
    if (mode_ != Mode::kUnlock && new_pwd.size() < kMinPasswordLength) {
        SetErrorText(QStringLiteral("主密码至少 %1 位。").arg(kMinPasswordLength));
        new_edit_->setFocus();
        return;
    }
    if ((mode_ == Mode::kSetup || mode_ == Mode::kChange) &&
        confirm != new_pwd) {
        SetErrorText(QStringLiteral("两次输入不一致。"));
        confirm_edit_->setFocus();
        return;
    }
    if (mode_ == Mode::kChange && old_pwd == new_pwd) {
        SetErrorText(QStringLiteral("新主密码不能与当前主密码相同。"));
        new_edit_->setFocus();
        return;
    }
    accept();
}

}  // namespace passvault::ui
