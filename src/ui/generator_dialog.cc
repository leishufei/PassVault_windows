#include "ui/generator_dialog.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "generator/password_strength.h"
#include "ui/clipboard_manager.h"
#include "ui/icon_loader.h"
#include "ui/theme_manager.h"

namespace passvault::ui {

namespace {

QString StrengthText(int level) {
    switch (level) {
        case 3:
            return QStringLiteral("强");
        case 2:
            return QStringLiteral("中");
        case 1:
            return QStringLiteral("弱");
        default:
            return QStringLiteral("—");
    }
}

void Repolish(QWidget* widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QWidget* SectionHeader(QWidget* parent, const QString& title,
                       QWidget* trailing) {
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto* label = new QLabel(title, row);
    label->setObjectName(QStringLiteral("GeneratorGroupTitle"));
    layout->addWidget(label);
    layout->addStretch(1);
    if (trailing) {
        trailing->setParent(row);
        layout->addWidget(trailing);
    }
    return row;
}

}  // namespace

GeneratorDialog::GeneratorDialog(QWidget* parent)
    : AppDialog(QStringLiteral("密码生成器"), parent) {
    setObjectName(QStringLiteral("GeneratorDialog"));
    SetIcon(QStringLiteral("key-round"), QStringLiteral("primary"));
    SetCardWidth(440);
    BuildUi();
    RefreshIcons();
    connect(ThemeManager::Instance(), &ThemeManager::ThemeChanged, this,
            [this](Theme) { RefreshIcons(); });
    Regenerate();
}

GeneratorDialog::~GeneratorDialog() {
    if (preview_) preview_->setText(QString());
}

QString GeneratorDialog::password() const {
    return preview_ ? preview_->text() : QString();
}

void GeneratorDialog::BuildUi() {
    auto* root = body_layout();
    root->setSpacing(0);

    auto* preview_card = new QFrame(body());
    preview_card->setObjectName(QStringLiteral("GeneratorPreviewCard"));
    auto* preview_layout = new QHBoxLayout(preview_card);
    preview_layout->setContentsMargins(12, 8, 8, 8);
    preview_layout->setSpacing(4);
    preview_ = new QLineEdit(preview_card);
    preview_->setObjectName(QStringLiteral("GeneratorPreview"));
    preview_->setReadOnly(true);
    preview_->setAccessibleName(QStringLiteral("生成的密码"));
    preview_layout->addWidget(preview_, 1);
    const auto add_tool = [preview_card, preview_layout](
                              const QString& name, const QString& tooltip) {
        auto* button = new QToolButton(preview_card);
        button->setObjectName(name);
        button->setToolTip(tooltip);
        button->setAccessibleName(tooltip);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::TabFocus);
        button->setFixedSize(28, 28);
        button->setIconSize(QSize(16, 16));
        preview_layout->addWidget(button);
        return button;
    };
    refresh_ = add_tool(QStringLiteral("GeneratorRefresh"),
                        QStringLiteral("重新生成"));
    copy_ = add_tool(QStringLiteral("GeneratorCopy"), QStringLiteral("复制"));
    connect(refresh_, &QToolButton::clicked, this, &GeneratorDialog::Regenerate);
    connect(copy_, &QToolButton::clicked, this, &GeneratorDialog::CopyPassword);
    root->addWidget(preview_card);
    root->addSpacing(12);

    auto* strength_row = new QWidget(body());
    strength_row->setObjectName(QStringLiteral("GeneratorStrength"));
    auto* strength_layout = new QHBoxLayout(strength_row);
    strength_layout->setContentsMargins(0, 0, 0, 0);
    strength_layout->setSpacing(4);
    auto* strength_title = new QLabel(QStringLiteral("安全强度"), strength_row);
    strength_title->setObjectName(QStringLiteral("GeneratorHint"));
    strength_layout->addWidget(strength_title);
    strength_layout->addSpacing(8);
    for (std::size_t i = 0; i < strength_segments_.size(); ++i) {
        auto* segment = new QFrame(strength_row);
        segment->setProperty("strength-segment", true);
        segment->setFixedHeight(4);
        strength_segments_[i] = segment;
        strength_layout->addWidget(segment, 1);
    }
    strength_layout->addSpacing(8);
    strength_label_ = new QLabel(strength_row);
    strength_label_->setObjectName(QStringLiteral("GeneratorStrengthLabel"));
    strength_label_->setProperty("strength-label", true);
    strength_label_->setMinimumWidth(16);
    strength_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    strength_layout->addWidget(strength_label_);
    root->addWidget(strength_row);
    root->addSpacing(24);

    length_value_ = new QLabel(QString::number(kDefaultLength));
    length_value_->setObjectName(QStringLiteral("GeneratorLengthValue"));
    root->addWidget(
        SectionHeader(body(), QStringLiteral("密码长度"), length_value_));

    length_slider_ = new QSlider(Qt::Horizontal, body());
    length_slider_->setObjectName(QStringLiteral("GeneratorLengthSlider"));
    length_slider_->setRange(kMinLength, kMaxLength);
    length_slider_->setValue(kDefaultLength);
    length_slider_->setAccessibleName(QStringLiteral("密码长度"));
    connect(length_slider_, &QSlider::valueChanged, this, [this](int value) {
        length_value_->setText(QString::number(value));
        Regenerate();
    });
    root->addWidget(length_slider_);

    auto* limits = new QWidget(body());
    auto* limits_layout = new QHBoxLayout(limits);
    limits_layout->setContentsMargins(0, 0, 0, 0);
    auto* minimum = new QLabel(QString::number(kMinLength), limits);
    minimum->setObjectName(QStringLiteral("GeneratorHint"));
    limits_layout->addWidget(minimum);
    limits_layout->addStretch(1);
    auto* maximum = new QLabel(QString::number(kMaxLength), limits);
    maximum->setObjectName(QStringLiteral("GeneratorHint"));
    limits_layout->addWidget(maximum);
    root->addWidget(limits);

    auto* divider = new QFrame(body());
    divider->setObjectName(QStringLiteral("GeneratorDivider"));
    divider->setFixedHeight(1);
    root->addSpacing(20);
    root->addWidget(divider);
    root->addSpacing(16);

    root->addWidget(
        SectionHeader(body(), QStringLiteral("包含字符"), nullptr));
    root->addSpacing(8);
    const auto add_option = [this, root](const QString& object_name,
                                         const QString& label,
                                         const QString& detail) {
        auto* row = new QWidget(body());
        row->setObjectName(QStringLiteral("GeneratorOption"));
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(8, 6, 8, 6);
        row_layout->setSpacing(12);
        auto* option = new QCheckBox(label, row);
        option->setObjectName(object_name);
        option->setChecked(true);
        connect(option, &QCheckBox::toggled, this,
                [this](bool) { Regenerate(); });
        row_layout->addWidget(option, 1);
        auto* hint = new QLabel(detail, row);
        hint->setObjectName(QStringLiteral("GeneratorHint"));
        row_layout->addWidget(hint);
        root->addWidget(row);
        return option;
    };
    upper_ = add_option(QStringLiteral("GeneratorUpper"),
                        QStringLiteral("大写字母"), QStringLiteral("A-Z"));
    lower_ = add_option(QStringLiteral("GeneratorLower"),
                        QStringLiteral("小写字母"), QStringLiteral("a-z"));
    number_ = add_option(QStringLiteral("GeneratorNumber"),
                         QStringLiteral("数字"), QStringLiteral("0-9"));
    symbol_ = add_option(QStringLiteral("GeneratorSymbol"),
                         QStringLiteral("特殊符号"), QStringLiteral("!@#"));

    error_ = new QLabel(body());
    error_->setObjectName(QStringLiteral("FormError"));
    error_->setVisible(false);
    root->addSpacing(8);
    root->addWidget(error_);

    status_ = new QLabel(body());
    status_->setObjectName(QStringLiteral("GeneratorStatus"));
    status_->setWordWrap(true);
    status_->setVisible(false);
    root->addWidget(status_);

    AddButton(QMessageBox::Close);
    copy_button_ = AddButton(QStringLiteral("复制密码"), QMessageBox::ActionRole,
                             ButtonStyle::kPrimary);
    copy_button_->setObjectName(QStringLiteral("GeneratorCopyButton"));
    // Copying keeps the generator open so several passwords can be taken.
    disconnect(copy_button_, &QPushButton::clicked, this, nullptr);
    connect(copy_button_, &QPushButton::clicked, this,
            &GeneratorDialog::CopyPassword);
    SetDefaultButton(copy_button_);
}

generator::PasswordConfig GeneratorDialog::CurrentConfig() const {
    generator::PasswordConfig config;
    config.length = length_slider_ ? length_slider_->value() : kDefaultLength;
    config.include_uppercase = upper_ && upper_->isChecked();
    config.include_lowercase = lower_ && lower_->isChecked();
    config.include_numbers = number_ && number_->isChecked();
    config.include_symbols = symbol_ && symbol_->isChecked();
    return config;
}

void GeneratorDialog::UpdateStrengthUi(const QString& password) {
    const int level =
        password.isEmpty() ? 0 : generator::CalculatePasswordStrength(password);
    // Same mapping as the editor: weak fills two segments, strong all four.
    for (std::size_t i = 0; i < strength_segments_.size(); ++i) {
        auto* segment = strength_segments_[i];
        const bool filled = level > 0 && static_cast<int>(i) < level + 1;
        segment->setProperty("filled", filled);
        segment->setProperty("level",
                             filled ? QString::number(level) : QString());
        Repolish(segment);
    }
    strength_label_->setText(StrengthText(level));
}

void GeneratorDialog::Regenerate() {
    if (!preview_ || !length_slider_ || !upper_ || !lower_ || !number_ ||
        !symbol_) {
        return;
    }
    status_->setVisible(false);
    const auto generated =
        generator::PasswordGenerator::Generate(CurrentConfig());
    if (!generated.has_value()) {
        preview_->clear();
        error_->setText(QStringLiteral("至少选择一类字符。"));
        error_->setVisible(true);
        UpdateStrengthUi(QString());
        copy_->setEnabled(false);
        copy_button_->setEnabled(false);
        return;
    }
    error_->setVisible(false);
    preview_->setText(*generated);
    preview_->setCursorPosition(0);
    UpdateStrengthUi(*generated);
    copy_->setEnabled(true);
    copy_button_->setEnabled(true);
}

void GeneratorDialog::CopyPassword() {
    if (!preview_ || preview_->text().isEmpty()) return;
    auto* clipboard = ClipboardManager::Instance();
    clipboard->CopySensitive(preview_->text());
    status_->setText(QStringLiteral("已复制，%1 秒后从剪贴板清除")
                         .arg(clipboard->timeout_ms() / 1000));
    status_->setVisible(true);
}

void GeneratorDialog::RefreshIcons() {
    auto* theme = ThemeManager::Instance();
    refresh_->setIcon(IconLoader::Load(QStringLiteral("refresh-cw"),
                                       theme->Color(QStringLiteral("accent")),
                                       16));
    copy_->setIcon(IconLoader::Load(QStringLiteral("copy"),
                                    theme->Color(QStringLiteral("accent")), 16));
}

}  // namespace passvault::ui
