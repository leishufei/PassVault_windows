#include "ui/app_dialog.h"

#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/icon_loader.h"
#include "ui/theme_manager.h"

namespace passvault::ui {

namespace {

constexpr int kDefaultCardWidth = 420;
// Transparent room around the card for its downward shadow (same as the
// master password dialog).
constexpr int kShadowMarginSide = 60;
constexpr int kShadowMarginTop = 60;
constexpr int kShadowMarginBottom = 84;

QString StandardText(QMessageBox::StandardButton which) {
    switch (which) {
        case QMessageBox::Ok:
        case QMessageBox::Yes:
        case QMessageBox::YesToAll:
            return QStringLiteral("确认");
        case QMessageBox::No:
        case QMessageBox::NoToAll:
        case QMessageBox::Cancel:
        case QMessageBox::Abort:
            return QStringLiteral("取消");
        case QMessageBox::Retry:
            return QStringLiteral("重试");
        case QMessageBox::Close:
            return QStringLiteral("关闭");
        case QMessageBox::Save:
            return QStringLiteral("保存");
        case QMessageBox::Discard:
            return QStringLiteral("放弃");
        case QMessageBox::Ignore:
            return QStringLiteral("忽略");
        default:
            return QStringLiteral("确认");
    }
}

QMessageBox::ButtonRole StandardRole(QMessageBox::StandardButton which) {
    switch (which) {
        case QMessageBox::Ok:
        case QMessageBox::Save:
        case QMessageBox::Open:
        case QMessageBox::SaveAll:
        case QMessageBox::Retry:
        case QMessageBox::Ignore:
            return QMessageBox::AcceptRole;
        case QMessageBox::Yes:
        case QMessageBox::YesToAll:
            return QMessageBox::YesRole;
        case QMessageBox::No:
        case QMessageBox::NoToAll:
            return QMessageBox::NoRole;
        case QMessageBox::Discard:
            return QMessageBox::DestructiveRole;
        case QMessageBox::Apply:
            return QMessageBox::ApplyRole;
        case QMessageBox::Reset:
        case QMessageBox::RestoreDefaults:
            return QMessageBox::ResetRole;
        case QMessageBox::Help:
            return QMessageBox::HelpRole;
        case QMessageBox::Cancel:
        case QMessageBox::Close:
        case QMessageBox::Abort:
        default:
            return QMessageBox::RejectRole;
    }
}

bool IsAffirmative(QMessageBox::ButtonRole role) {
    return role == QMessageBox::AcceptRole || role == QMessageBox::YesRole ||
           role == QMessageBox::ApplyRole;
}

bool IsNegative(QMessageBox::ButtonRole role) {
    return role == QMessageBox::RejectRole || role == QMessageBox::NoRole;
}

void Repolish(QWidget* widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

}  // namespace

AppDialog::AppDialog(const QString& title, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("AppDialog"));
    // ThemeManager decorates plain QDialogs; this one draws its own surface.
    setProperty("appDialog", true);
    setWindowTitle(title);
    setModal(true);
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(kShadowMarginSide, kShadowMarginTop,
                             kShadowMarginSide, kShadowMarginBottom);
    root->setSpacing(0);
    root->setSizeConstraint(QLayout::SetFixedSize);

    card_ = new QFrame(this);
    card_->setObjectName(QStringLiteral("AppDialogCard"));
    card_->setFixedWidth(kDefaultCardWidth);
    shadow_ = new QGraphicsDropShadowEffect(card_);
    shadow_->setBlurRadius(60);
    shadow_->setOffset(0, 24);
    card_->setGraphicsEffect(shadow_);
    auto* card_layout = new QVBoxLayout(card_);
    card_layout->setContentsMargins(1, 1, 1, 1);
    card_layout->setSpacing(0);
    root->addWidget(card_);

    auto* header = new QWidget(card_);
    header->setObjectName(QStringLiteral("AppDialogHeader"));
    header->setFixedHeight(56);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(20, 0, 16, 0);
    header_layout->setSpacing(10);
    icon_ = new QLabel(header);
    icon_->setObjectName(QStringLiteral("AppDialogIcon"));
    icon_->setFixedSize(32, 32);
    icon_->setAlignment(Qt::AlignCenter);
    header_layout->addWidget(icon_);
    title_ = new QLabel(title, header);
    title_->setObjectName(QStringLiteral("AppDialogTitle"));
    title_->setTextFormat(Qt::PlainText);
    header_layout->addWidget(title_, 1);
    close_button_ = new QToolButton(header);
    close_button_->setObjectName(QStringLiteral("AppDialogClose"));
    close_button_->setFixedSize(32, 32);
    close_button_->setIconSize(QSize(16, 16));
    close_button_->setToolTip(QStringLiteral("关闭"));
    close_button_->setAccessibleName(QStringLiteral("关闭"));
    close_button_->setCursor(Qt::PointingHandCursor);
    close_button_->setFocusPolicy(Qt::TabFocus);
    connect(close_button_, &QToolButton::clicked, this, &AppDialog::reject);
    header_layout->addWidget(close_button_);
    card_layout->addWidget(header);

    body_ = new QWidget(card_);
    body_->setObjectName(QStringLiteral("AppDialogBody"));
    body_layout_ = new QVBoxLayout(body_);
    body_layout_->setContentsMargins(24, 18, 24, 20);
    body_layout_->setSpacing(14);
    message_ = new QLabel(body_);
    message_->setObjectName(QStringLiteral("AppDialogMessage"));
    message_->setTextFormat(Qt::PlainText);
    message_->setWordWrap(true);
    message_->setVisible(false);
    body_layout_->addWidget(message_);
    card_layout->addWidget(body_, 1);

    footer_ = new QWidget(card_);
    footer_->setObjectName(QStringLiteral("AppDialogFooter"));
    footer_layout_ = new QHBoxLayout(footer_);
    footer_layout_->setContentsMargins(24, 14, 24, 14);
    footer_layout_->setSpacing(8);
    footer_layout_->addStretch(1);
    footer_->setVisible(false);
    card_layout->addWidget(footer_);

    SetTone(Tone::kInfo);
    connect(ThemeManager::Instance(), &ThemeManager::ThemeChanged, this,
            [this](Theme) { RefreshThemeAssets(); });
}

AppDialog::~AppDialog() {
    if (text_field_ && text_field_->echoMode() != QLineEdit::Normal) {
        text_field_->setText(QString());
    }
    if (backdrop_) {
        if (backdrop_->parentWidget()) {
            backdrop_->parentWidget()->removeEventFilter(this);
        }
        delete backdrop_.data();
    }
}

void AppDialog::SetTitle(const QString& title) {
    title_->setText(title);
    setWindowTitle(title);
}

QString AppDialog::title() const { return title_->text(); }

void AppDialog::SetTone(Tone tone) {
    switch (tone) {
        case Tone::kWarning:
            SetIcon(QStringLiteral("shield-alert"), QStringLiteral("warn"));
            break;
        case Tone::kDanger:
            SetIcon(QStringLiteral("shield-alert"), QStringLiteral("danger"));
            break;
        case Tone::kQuestion:
        case Tone::kInfo:
        default:
            SetIcon(QStringLiteral("shield-check"), QStringLiteral("primary"));
            break;
    }
}

void AppDialog::SetIcon(const QString& icon_name, const QString& color_token) {
    icon_name_ = icon_name;
    icon_token_ = color_token;
    RefreshThemeAssets();
}

void AppDialog::SetMessage(const QString& text) {
    message_->setText(text);
    message_->setVisible(!text.isEmpty());
}

QString AppDialog::text() const { return message_->text(); }

void AppDialog::SetCardWidth(int width) { card_->setFixedWidth(width); }

QPushButton* AppDialog::AddButton(StandardButton which, ButtonStyle style) {
    if (auto* existing = button(which)) return existing;
    auto* added = AddButton(StandardText(which), StandardRole(which), style);
    button_info_[added].standard = which;
    return added;
}

QPushButton* AppDialog::AddButton(const QString& text,
                                  QMessageBox::ButtonRole role,
                                  ButtonStyle style) {
    auto* added = new QPushButton(text, footer_);
    added->setFixedHeight(32);
    added->setAutoDefault(false);
    added->setCursor(Qt::PointingHandCursor);
    button_info_.insert(added, ButtonInfo{role, QMessageBox::NoButton});
    buttons_.push_back(added);
    if (style == ButtonStyle::kAuto) {
        style = role == QMessageBox::DestructiveRole ? ButtonStyle::kDanger
                : IsAffirmative(role)               ? ButtonStyle::kPrimary
                                                    : ButtonStyle::kSecondary;
    }
    SetButtonStyle(added, style);
    footer_layout_->addWidget(added);
    footer_->setVisible(true);
    connect(added, &QPushButton::clicked, this,
            [this, added]() { OnButtonClicked(added); });
    if (!escape_button_ && IsNegative(role)) escape_button_ = added;
    return added;
}

QPushButton* AppDialog::button(StandardButton which) const {
    if (which == QMessageBox::NoButton) return nullptr;
    for (auto* candidate : buttons_) {
        if (button_info_.value(candidate).standard == which) return candidate;
    }
    return nullptr;
}

void AppDialog::SetButtonStyle(QPushButton* target, ButtonStyle style) {
    if (!target) return;
    target->setProperty("accent", style == ButtonStyle::kPrimary);
    target->setProperty("danger", style == ButtonStyle::kDanger);
    Repolish(target);
}

void AppDialog::SetDefaultButton(QPushButton* target) {
    if (default_button_) default_button_->setDefault(false);
    default_button_ = target;
    if (default_button_) default_button_->setDefault(true);
}

void AppDialog::SetDefaultButton(StandardButton which) {
    SetDefaultButton(button(which));
}

void AppDialog::SetEscapeButton(QPushButton* target) { escape_button_ = target; }

void AppDialog::SetEscapeButton(StandardButton which) {
    SetEscapeButton(button(which));
}

AppDialog::StandardButton AppDialog::standardButton(
    QAbstractButton* target) const {
    auto* push = qobject_cast<QPushButton*>(target);
    if (!push) return QMessageBox::NoButton;
    return button_info_.value(push).standard;
}

AppDialog::StandardButton AppDialog::ResultButton() const {
    if (clicked_button_) return standardButton(clicked_button_);
    if (result() == QDialog::Accepted) {
        for (auto* candidate : buttons_) {
            const auto info = button_info_.value(candidate);
            if (IsAffirmative(info.role) &&
                info.standard != QMessageBox::NoButton) {
                return info.standard;
            }
        }
    }
    return QMessageBox::NoButton;
}

QLineEdit* AppDialog::AddTextField(const QString& label, const QString& value,
                                   QLineEdit::EchoMode mode,
                                   const QString& placeholder) {
    auto* field = new QWidget(body_);
    field->setObjectName(QStringLiteral("AppDialogField"));
    auto* layout = new QVBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto* caption = new QLabel(label, field);
    caption->setObjectName(QStringLiteral("AppDialogFieldLabel"));
    caption->setTextFormat(Qt::PlainText);
    caption->setWordWrap(true);
    layout->addWidget(caption);
    text_field_ = new QLineEdit(field);
    text_field_->setObjectName(QStringLiteral("AppDialogInput"));
    text_field_->setEchoMode(mode);
    text_field_->setText(value);
    text_field_->setPlaceholderText(placeholder);
    text_field_->setAccessibleName(label);
    text_field_->setFixedHeight(32);
    caption->setBuddy(text_field_);
    layout->addWidget(text_field_);
    body_layout_->addWidget(field);
    return text_field_;
}

void AppDialog::setTextValue(const QString& value) {
    if (text_field_) text_field_->setText(value);
}

QString AppDialog::textValue() const {
    return text_field_ ? text_field_->text() : QString();
}

void AppDialog::reject() {
    if (!clicked_button_) clicked_button_ = escape_button_;
    QDialog::reject();
}

void AppDialog::OnButtonClicked(QPushButton* target) {
    clicked_button_ = target;
    // Route through the virtual slots so subclasses can react to dismissal.
    if (IsNegative(button_info_.value(target).role)) {
        reject();
    } else {
        accept();
    }
}

void AppDialog::showEvent(QShowEvent* event) {
    if (parentWidget()) {
        if (!backdrop_) {
            backdrop_ = new QWidget(parentWidget()->window());
            backdrop_->setObjectName(QStringLiteral("AppDialogBackdrop"));
            backdrop_->setAttribute(Qt::WA_StyledBackground);
        }
        backdrop_->parentWidget()->installEventFilter(this);
        backdrop_->setGeometry(backdrop_->parentWidget()->rect());
        backdrop_->show();
        backdrop_->raise();
    }
    QDialog::showEvent(event);
    if (text_field_) {
        text_field_->setFocus(Qt::OtherFocusReason);
        text_field_->selectAll();
    } else if (default_button_) {
        default_button_->setFocus(Qt::OtherFocusReason);
    }
}

void AppDialog::hideEvent(QHideEvent* event) {
    if (backdrop_) {
        backdrop_->hide();
        if (backdrop_->parentWidget()) {
            backdrop_->parentWidget()->removeEventFilter(this);
        }
    }
    QDialog::hideEvent(event);
}

bool AppDialog::eventFilter(QObject* watched, QEvent* event) {
    if (backdrop_ && watched == backdrop_->parentWidget() &&
        event->type() == QEvent::Resize) {
        backdrop_->setGeometry(backdrop_->parentWidget()->rect());
    }
    return QDialog::eventFilter(watched, event);
}

void AppDialog::RefreshThemeAssets() {
    auto* theme = ThemeManager::Instance();
    QColor shadow_color = theme->Color(QStringLiteral("dialog-shadow"));
    shadow_color.setAlphaF(0.2);
    shadow_->setColor(shadow_color);
    icon_->setPixmap(IconLoader::Load(icon_name_, theme->Color(icon_token_), 16)
                         .pixmap(16, 16));
    close_button_->setIcon(IconLoader::Load(
        QStringLiteral("x"), theme->Color(QStringLiteral("muted-7")), 16));
}

AppDialog::StandardButton AppDialog::ShowMessage(
    QWidget* parent, Tone tone, const QString& title, const QString& text,
    StandardButtons buttons, StandardButton default_button) {
    AppDialog dialog(title, parent);
    dialog.SetTone(tone);
    dialog.SetMessage(text);
    if (buttons == QMessageBox::NoButton) buttons = QMessageBox::Ok;
    // Footer order: dismissive actions first, the primary action last.
    static constexpr StandardButton kOrder[] = {
        QMessageBox::Cancel, QMessageBox::No,   QMessageBox::Close,
        QMessageBox::Abort,  QMessageBox::Discard, QMessageBox::Ignore,
        QMessageBox::Retry,  QMessageBox::Save, QMessageBox::Ok,
        QMessageBox::Yes,
    };
    for (const auto which : kOrder) {
        if (buttons.testFlag(which)) dialog.AddButton(which);
    }
    if (dialog.buttons_.empty()) dialog.AddButton(QMessageBox::Ok);
    if (default_button != QMessageBox::NoButton && dialog.button(default_button)) {
        dialog.SetDefaultButton(default_button);
    } else {
        dialog.SetDefaultButton(dialog.buttons_.back());
    }
    if (!dialog.escape_button_ && dialog.buttons_.size() == 1) {
        dialog.SetEscapeButton(dialog.buttons_.front());
    }
    dialog.exec();
    const auto result = dialog.ResultButton();
    if (result != QMessageBox::NoButton) return result;
    return dialog.standardButton(dialog.escape_button_);
}

AppDialog::StandardButton AppDialog::Information(
    QWidget* parent, const QString& title, const QString& text,
    StandardButtons buttons, StandardButton default_button) {
    return ShowMessage(parent, Tone::kInfo, title, text, buttons,
                       default_button);
}

AppDialog::StandardButton AppDialog::Warning(
    QWidget* parent, const QString& title, const QString& text,
    StandardButtons buttons, StandardButton default_button) {
    return ShowMessage(parent, Tone::kWarning, title, text, buttons,
                       default_button);
}

AppDialog::StandardButton AppDialog::Critical(
    QWidget* parent, const QString& title, const QString& text,
    StandardButtons buttons, StandardButton default_button) {
    return ShowMessage(parent, Tone::kDanger, title, text, buttons,
                       default_button);
}

AppDialog::StandardButton AppDialog::Question(
    QWidget* parent, const QString& title, const QString& text,
    StandardButtons buttons, StandardButton default_button) {
    return ShowMessage(parent, Tone::kQuestion, title, text, buttons,
                       default_button);
}

bool AppDialog::Confirm(QWidget* parent, const QString& title,
                        const QString& text, const QString& confirm_text,
                        ConfirmKind kind) {
    AppDialog dialog(title, parent);
    const bool danger = kind != ConfirmKind::kNormal;
    if (danger) {
        dialog.SetIcon(QStringLiteral("trash-2"), QStringLiteral("danger"));
    } else {
        dialog.SetTone(Tone::kQuestion);
    }
    dialog.SetMessage(text);
    dialog.AddButton(QMessageBox::No);
    auto* confirm = dialog.AddButton(
        QMessageBox::Yes,
        danger ? ButtonStyle::kDanger : ButtonStyle::kPrimary);
    confirm->setText(confirm_text);
    dialog.SetDefaultButton(QMessageBox::Yes);
    dialog.SetEscapeButton(QMessageBox::No);
    dialog.exec();
    return dialog.ResultButton() == QMessageBox::Yes;
}

QString AppDialog::GetText(QWidget* parent, const QString& title,
                           const QString& label, QLineEdit::EchoMode mode,
                           const QString& value, bool* ok,
                           const QString& placeholder) {
    AppDialog dialog(title, parent);
    dialog.SetIcon(mode == QLineEdit::Normal ? QStringLiteral("pencil")
                                             : QStringLiteral("key-round"),
                   QStringLiteral("primary"));
    dialog.AddTextField(label, value, mode, placeholder);
    dialog.AddButton(QMessageBox::Cancel);
    dialog.AddButton(QMessageBox::Ok);
    dialog.SetDefaultButton(QMessageBox::Ok);
    dialog.SetEscapeButton(QMessageBox::Cancel);
    dialog.exec();
    const bool accepted = dialog.ResultButton() == QMessageBox::Ok;
    if (ok) *ok = accepted;
    QString result = accepted ? dialog.textValue() : QString();
    dialog.setTextValue(QString());
    return result;
}

}  // namespace passvault::ui
