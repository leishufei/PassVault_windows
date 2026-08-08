#include "ui/editor_panel.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShortcut>
#include <QSizePolicy>
#include <QSlider>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

#include "generator/password_generator.h"
#include "generator/password_strength.h"
#include "ui/icon_loader.h"
#include "ui/theme_manager.h"

namespace passvault::ui {

namespace {

constexpr int kPanelWidth = 400;
constexpr int kAnimationMs = 220;

QIcon LoadEditorIcon(const QString& name, const QString& color_token, int size) {
    return IconLoader::Load(
        name, ThemeManager::Instance()->Color(color_token), size);
}

QString StrengthText(int level) {
    switch (level) {
        case 3:
            return QStringLiteral("强");
        case 2:
            return QStringLiteral("中");
        case 1:
        default:
            return QStringLiteral("弱");
    }
}

void RepolishWidget(QWidget* w) {
    if (!w) return;
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

}  // namespace

EditorPanel::EditorPanel(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("EditorPanel"));
    setFocusPolicy(Qt::StrongFocus);

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(45);
    shadow->setColor(
        ThemeManager::Instance()->Color(QStringLiteral("shadow")));
    shadow->setOffset(-18, 0);
    setGraphicsEffect(shadow);

    BuildUi();
    hide();

    connect(ThemeManager::Instance(), &ThemeManager::ThemeChanged, this,
            [this](Theme) { RefreshThemeAssets(); });

    anim_ = new QPropertyAnimation(this, "geometry", this);
    anim_->setDuration(kAnimationMs);
    anim_->setEasingCurve(QEasingCurve::OutCubic);

    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    esc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(esc, &QShortcut::activated, this, [this]() {
        if (pages_ && pages_->currentWidget() == generator_page_) {
            OnHideGenerator();
            return;
        }
        OnCancelClicked();
    });

    if (parent) parent->installEventFilter(this);
}

EditorPanel::~EditorPanel() = default;

void EditorPanel::BuildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("EditorPages"));
    editor_page_ = BuildEditorPage();
    generator_page_ = BuildGeneratorPage();
    pages_->addWidget(editor_page_);
    pages_->addWidget(generator_page_);
    pages_->setCurrentWidget(editor_page_);
    root->addWidget(pages_);
}

QWidget* EditorPanel::BuildEditorPage() {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("EditorPage"));
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new QWidget(page);
    header->setObjectName(QStringLiteral("EditorHeader"));
    header->setFixedHeight(68);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(28, 0, 28, 0);
    header_layout->setSpacing(12);

    auto* header_back = new QToolButton(header);
    header_back->setObjectName(QStringLiteral("EditorHeaderBack"));
    header_back->setIcon(LoadEditorIcon(QStringLiteral("arrow-left"),
                                       QStringLiteral("muted-7"), 18));
    header_back->setIconSize(QSize(18, 18));
    header_back->setToolTip(QStringLiteral("返回"));
    header_back->setFixedSize(24, 24);
    connect(header_back, &QToolButton::clicked, this,
            &EditorPanel::OnCancelClicked);
    header_layout->addWidget(header_back, 0, Qt::AlignVCenter);

    header_title_ = new QLabel(QStringLiteral("新建密码"), header);
    header_title_->setObjectName(QStringLiteral("EditorHeaderTitle"));
    header_title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header_close_ = new QToolButton(header);
    header_close_->setObjectName(QStringLiteral("EditorHeaderClose"));
    header_close_->setIcon(LoadEditorIcon(QStringLiteral("x"),
                                          QStringLiteral("muted-7"), 20));
    header_close_->setIconSize(QSize(20, 20));
    header_close_->setToolTip(QStringLiteral("关闭"));
    header_close_->setFixedSize(32, 32);
    connect(header_close_, &QToolButton::clicked, this,
            &EditorPanel::OnCancelClicked);
    header_layout->addWidget(header_title_, 1);
    header_layout->addWidget(header_close_);
    root->addWidget(header);

    root->addWidget(BuildOverviewPage(), 1);

    auto* footer = new QWidget(page);
    footer->setObjectName(QStringLiteral("EditorFooter"));
    auto* footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(28, 16, 28, 16);
    footer_layout->setSpacing(8);
    cancel_button_ = new QPushButton(QStringLiteral("取消"), footer);
    cancel_button_->setObjectName(QStringLiteral("EditorCancelButton"));
    cancel_button_->setProperty("flat", true);
    cancel_button_->setFixedHeight(36);
    connect(cancel_button_, &QPushButton::clicked, this,
            &EditorPanel::OnCancelClicked);
    save_button_ = new QPushButton(QStringLiteral("创建密码"), footer);
    save_button_->setObjectName(QStringLiteral("EditorSaveButton"));
    save_button_->setProperty("accent", true);
    save_button_->setFixedHeight(36);
    connect(save_button_, &QPushButton::clicked, this,
            &EditorPanel::OnSaveClicked);
    footer_layout->addStretch(1);
    footer_layout->addWidget(cancel_button_);
    footer_layout->addWidget(save_button_);
    root->addWidget(footer);
    return page;
}

QWidget* EditorPanel::BuildOverviewPage() {
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("EditorScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // QScrollArea's viewport autofills its palette (default gray) and would
    // hide the panel's card background; keep it transparent for consistency.
    scroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("EditorContent"));
    auto* content_layout = new QHBoxLayout(content);
    content_layout->setContentsMargins(28, 24, 28, 24);
    content_layout->setSpacing(0);

    auto* container = new QWidget(content);
    container->setObjectName(QStringLiteral("EditorBody"));
    container->setFixedWidth(340);
    auto* v = new QVBoxLayout(container);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(16);

    auto* section_header = new QWidget(container);
    section_header->setObjectName(QStringLiteral("EditorSectionHeader"));
    auto* section_layout = new QVBoxLayout(section_header);
    section_layout->setContentsMargins(0, 0, 0, 16);
    section_layout->setSpacing(4);
    auto* section_title = new QLabel(QStringLiteral("基本信息"), section_header);
    section_title->setObjectName(QStringLiteral("EditorSectionTitle"));
    section_layout->addWidget(section_title);
    auto* section_description = new QLabel(
        QStringLiteral("保存登录所需的账号与网站信息"), section_header);
    section_description->setObjectName(
        QStringLiteral("EditorSectionDescription"));
    section_layout->addWidget(section_description);
    auto* section_divider = new QFrame(section_header);
    section_divider->setObjectName(QStringLiteral("EditorSectionDivider"));
    section_divider->setFixedHeight(1);
    section_layout->addWidget(section_divider);
    v->addWidget(section_header);

    auto add_field = [&](const QString& label_text, QWidget* editor) {
        auto* field = new QWidget(container);
        field->setObjectName(QStringLiteral("EditorField"));
        auto* field_layout = new QVBoxLayout(field);
        field_layout->setContentsMargins(0, 0, 0, 0);
        field_layout->setSpacing(6);
        auto* label = new QLabel(label_text, field);
        label->setObjectName(QStringLiteral("EditorFieldLabel"));
        editor->setParent(field);
        field_layout->addWidget(label);
        field_layout->addWidget(editor);
        v->addWidget(field);
        return field_layout;
    };

    title_input_ = new QLineEdit(container);
    title_input_->setObjectName(QStringLiteral("EditorTitleInput"));
    title_input_->setPlaceholderText(QStringLiteral("必填"));
    title_input_->setFixedHeight(36);
    auto* title_layout = add_field(QStringLiteral("标题"), title_input_);
    title_error_ = new QLabel(QStringLiteral("请输入标题。"),
                              title_input_->parentWidget());
    title_error_->setObjectName(QStringLiteral("EditorTitleError"));
    title_error_->setProperty("validation-error", true);
    title_error_->setVisible(false);
    title_layout->addWidget(title_error_);

    username_input_ = new QLineEdit(container);
    username_input_->setObjectName(QStringLiteral("EditorUsernameInput"));
    username_input_->setFixedHeight(36);
    add_field(QStringLiteral("用户名"), username_input_);

    auto* password_group = new QWidget(container);
    password_group->setObjectName(QStringLiteral("EditorPasswordGroup"));
    auto* password_group_layout = new QVBoxLayout(password_group);
    password_group_layout->setContentsMargins(0, 0, 0, 0);
    password_group_layout->setSpacing(6);

    auto* password_header = new QWidget(password_group);
    auto* password_header_layout = new QHBoxLayout(password_header);
    password_header_layout->setContentsMargins(0, 0, 0, 0);
    password_header_layout->setSpacing(8);
    auto* password_label = new QLabel(QStringLiteral("密码"), password_header);
    password_label->setObjectName(QStringLiteral("EditorFieldLabel"));
    password_header_layout->addWidget(password_label);
    password_header_layout->addStretch(1);
    generate_button_ = new QToolButton(password_header);
    generate_button_->setObjectName(QStringLiteral("EditorGenerateButton"));
    generate_button_->setText(QStringLiteral("生成密码"));
    generate_button_->setIcon(LoadEditorIcon(QStringLiteral("refresh-cw"),
                                             QStringLiteral("accent"), 12));
    generate_button_->setIconSize(QSize(12, 12));
    generate_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    generate_button_->setToolTip(QStringLiteral("生成密码"));
    generate_button_->setFixedHeight(18);
    connect(generate_button_, &QToolButton::clicked, this,
            &EditorPanel::OnShowGenerator);
    password_header_layout->addWidget(generate_button_);
    password_group_layout->addWidget(password_header);

    password_field_ = new QWidget(password_group);
    password_field_->setObjectName(QStringLiteral("EditorPasswordField"));
    password_field_->setFixedHeight(36);
    auto* password_row_layout = new QHBoxLayout(password_field_);
    password_row_layout->setContentsMargins(8, 0, 6, 0);
    password_row_layout->setSpacing(4);
    password_input_ = new QLineEdit(password_field_);
    password_input_->setObjectName(QStringLiteral("EditorPasswordInput"));
    password_input_->setEchoMode(QLineEdit::Password);
    password_input_->setProperty("mono", true);
    connect(password_input_, &QLineEdit::textChanged, this,
            &EditorPanel::OnPasswordTextChanged);
    preview_toggle_ = new QToolButton(password_field_);
    preview_toggle_->setObjectName(QStringLiteral("EditorPreviewToggle"));
    preview_toggle_->setCheckable(true);
    preview_toggle_->setIcon(LoadEditorIcon(QStringLiteral("eye"),
                                            QStringLiteral("hint-8"), 14));
    preview_toggle_->setIconSize(QSize(14, 14));
    preview_toggle_->setToolTip(QStringLiteral("显示 / 隐藏密码"));
    preview_toggle_->setFixedSize(24, 24);
    connect(preview_toggle_, &QToolButton::toggled, this,
            &EditorPanel::OnTogglePasswordPreview);
    password_row_layout->addWidget(password_input_, 1);
    password_row_layout->addWidget(preview_toggle_);
    password_group_layout->addWidget(password_field_);
    credentials_error_ = new QLabel(QStringLiteral("请输入用户名或密码。"),
                                    password_group);
    credentials_error_->setObjectName(
        QStringLiteral("EditorCredentialsError"));
    credentials_error_->setProperty("validation-error", true);
    credentials_error_->setVisible(false);
    password_group_layout->addWidget(credentials_error_);
    v->addWidget(password_group);

    auto* strength_group = new QWidget(container);
    strength_group->setObjectName(QStringLiteral("EditorStrengthGroup"));
    auto* strength_group_layout = new QVBoxLayout(strength_group);
    strength_group_layout->setContentsMargins(0, 0, 0, 0);
    strength_group_layout->setSpacing(6);

    auto* strength_header = new QWidget(strength_group);
    auto* strength_header_layout = new QHBoxLayout(strength_header);
    strength_header_layout->setContentsMargins(0, 0, 0, 0);
    strength_header_layout->setSpacing(8);
    auto* strength_title = new QLabel(QStringLiteral("安全强度"), strength_header);
    strength_title->setObjectName(QStringLiteral("EditorFieldLabel"));
    strength_header_layout->addWidget(strength_title);
    strength_header_layout->addStretch(1);
    strength_label_ = new QLabel(StrengthText(1), strength_header);
    strength_label_->setObjectName(QStringLiteral("EditorStrengthLabel"));
    strength_label_->setProperty("strength-label", true);
    strength_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    strength_header_layout->addWidget(strength_label_);
    strength_group_layout->addWidget(strength_header);

    auto* strength_row = new QWidget(strength_group);
    auto* strength_layout = new QHBoxLayout(strength_row);
    strength_layout->setContentsMargins(0, 0, 0, 0);
    strength_layout->setSpacing(4);
    for (int i = 0; i < 4; ++i) {
        auto* seg = new QFrame(strength_row);
        seg->setProperty("strength-segment", true);
        seg->setFixedHeight(4);
        strength_segments_[i] = seg;
        strength_layout->addWidget(seg, 1);
    }
    strength_group_layout->addWidget(strength_row);
    v->addWidget(strength_group);

    website_input_ = new QLineEdit(container);
    website_input_->setObjectName(QStringLiteral("EditorWebsiteInput"));
    website_input_->setPlaceholderText(QStringLiteral("https://..."));
    website_input_->setFixedHeight(36);
    add_field(QStringLiteral("网址"), website_input_);

    category_combo_ = new QComboBox(container);
    category_combo_->setObjectName(QStringLiteral("EditorCategoryCombo"));
    category_combo_->setFixedHeight(36);
    // Use the styled delegate so the popup follows the application's QSS.
    category_combo_->setItemDelegate(
        new QStyledItemDelegate(category_combo_));
    add_field(QStringLiteral("分类"), category_combo_);

    notes_input_ = new QTextEdit(container);
    notes_input_->setObjectName(QStringLiteral("EditorNotesInput"));
    notes_input_->setPlaceholderText(QStringLiteral("备注 (可选)"));
    notes_input_->setFixedHeight(96);
    add_field(QStringLiteral("备注"), notes_input_);

    v->addStretch(1);
    content_layout->addWidget(container, 1, Qt::AlignHCenter | Qt::AlignTop);
    scroll->setWidget(content);
    return scroll;
}

QWidget* EditorPanel::BuildGeneratorPage() {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("EditorGeneratorPage"));
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new QWidget(page);
    header->setObjectName(QStringLiteral("EditorGeneratorHeader"));
    header->setFixedHeight(68);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(28, 0, 28, 0);
    header_layout->setSpacing(12);

    auto* back = new QToolButton(header);
    back->setObjectName(QStringLiteral("EditorGeneratorBack"));
    back->setIcon(LoadEditorIcon(QStringLiteral("arrow-left"),
                                QStringLiteral("muted-7"), 18));
    back->setIconSize(QSize(18, 18));
    back->setToolTip(QStringLiteral("返回编辑"));
    back->setFixedSize(24, 24);
    connect(back, &QToolButton::clicked, this, &EditorPanel::OnHideGenerator);
    header_layout->addWidget(back, 0, Qt::AlignVCenter);

    auto* title = new QLabel(QStringLiteral("生成密码"), header);
    title->setObjectName(QStringLiteral("EditorGeneratorHeaderTitle"));
    header_layout->addWidget(title, 1);

    auto* close = new QToolButton(header);
    close->setObjectName(QStringLiteral("EditorGeneratorClose"));
    close->setIcon(
        LoadEditorIcon(QStringLiteral("x"), QStringLiteral("muted-7"), 20));
    close->setIconSize(QSize(20, 20));
    close->setToolTip(QStringLiteral("关闭"));
    close->setFixedSize(32, 32);
    connect(close, &QToolButton::clicked, this, &EditorPanel::OnHideGenerator);
    header_layout->addWidget(close);
    root->addWidget(header);

    auto* scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("EditorGeneratorScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("EditorGeneratorContent"));
    auto* content_layout = new QHBoxLayout(content);
    content_layout->setContentsMargins(28, 24, 28, 24);
    content_layout->setSpacing(0);

    auto* body = new QWidget(content);
    body->setObjectName(QStringLiteral("EditorGeneratorBody"));
    body->setFixedWidth(340);
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(0);

    auto* body_title = new QLabel(QStringLiteral("创建高强度密码"), body);
    body_title->setObjectName(QStringLiteral("EditorGeneratorTitle"));
    body_layout->addWidget(body_title);
    auto* description = new QLabel(
        QStringLiteral("为每个账号使用唯一密码，降低泄露风险。"), body);
    description->setObjectName(QStringLiteral("EditorGeneratorDescription"));
    body_layout->addWidget(description);

    auto* preview_card = new QFrame(body);
    preview_card->setObjectName(QStringLiteral("EditorGeneratorPreviewCard"));
    auto* preview_layout = new QHBoxLayout(preview_card);
    preview_layout->setContentsMargins(12, 8, 8, 8);
    preview_layout->setSpacing(8);
    generator_preview_ = new QLineEdit(preview_card);
    generator_preview_->setObjectName(QStringLiteral("EditorGeneratorPreview"));
    generator_preview_->setReadOnly(true);
    generator_preview_->setProperty("mono", true);
    preview_layout->addWidget(generator_preview_, 1);
    auto* refresh = new QToolButton(preview_card);
    refresh->setObjectName(QStringLiteral("EditorGeneratorRefresh"));
    refresh->setIcon(LoadEditorIcon(QStringLiteral("refresh-cw"),
                                   QStringLiteral("accent"), 16));
    refresh->setIconSize(QSize(16, 16));
    refresh->setToolTip(QStringLiteral("重新生成"));
    refresh->setFixedSize(28, 28);
    connect(refresh, &QToolButton::clicked, this,
            &EditorPanel::RegeneratePassword);
    preview_layout->addWidget(refresh);
    body_layout->addWidget(preview_card);

    auto* length_header = new QWidget(body);
    auto* length_header_layout = new QHBoxLayout(length_header);
    length_header_layout->setContentsMargins(0, 0, 0, 0);
    auto* length_title = new QLabel(QStringLiteral("密码长度"), length_header);
    length_title->setObjectName(QStringLiteral("EditorGeneratorGroupTitle"));
    length_header_layout->addWidget(length_title);
    length_header_layout->addStretch(1);
    generator_length_value_ = new QLabel(QStringLiteral("16"), length_header);
    generator_length_value_->setObjectName(
        QStringLiteral("EditorGeneratorLengthValue"));
    generator_length_value_->setProperty("mono", true);
    length_header_layout->addWidget(generator_length_value_);
    body_layout->addWidget(length_header);

    generator_length_ = new QSlider(Qt::Horizontal, body);
    generator_length_->setObjectName(
        QStringLiteral("EditorGeneratorLengthSlider"));
    generator_length_->setRange(8, 32);
    generator_length_->setValue(16);
    connect(generator_length_, &QSlider::valueChanged, this, [this](int value) {
        generator_length_value_->setText(QString::number(value));
        RegeneratePassword();
    });
    body_layout->addWidget(generator_length_);

    auto* length_limits = new QWidget(body);
    auto* length_limits_layout = new QHBoxLayout(length_limits);
    length_limits_layout->setContentsMargins(0, 0, 0, 0);
    auto* minimum = new QLabel(QStringLiteral("8"), length_limits);
    minimum->setObjectName(QStringLiteral("EditorGeneratorHint"));
    length_limits_layout->addWidget(minimum);
    length_limits_layout->addStretch(1);
    auto* maximum = new QLabel(QStringLiteral("32"), length_limits);
    maximum->setObjectName(QStringLiteral("EditorGeneratorHint"));
    length_limits_layout->addWidget(maximum);
    body_layout->addWidget(length_limits);

    auto* divider = new QFrame(body);
    divider->setObjectName(QStringLiteral("EditorGeneratorDivider"));
    divider->setFixedHeight(1);
    body_layout->addWidget(divider);

    auto* options_title = new QLabel(QStringLiteral("包含字符"), body);
    options_title->setObjectName(QStringLiteral("EditorGeneratorOptionsTitle"));
    body_layout->addWidget(options_title);

    auto add_option = [this, body, body_layout](const QString& object_name,
                                                const QString& label,
                                                const QString& detail,
                                                QCheckBox** target) {
        auto* row = new QWidget(body);
        row->setObjectName(QStringLiteral("EditorGeneratorOption"));
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(8, 6, 8, 6);
        row_layout->setSpacing(12);
        auto* option = new QCheckBox(label, row);
        option->setObjectName(object_name);
        option->setProperty("variant", QStringLiteral("generator-option"));
        option->setChecked(true);
        connect(option, &QCheckBox::toggled, this,
                &EditorPanel::RegeneratePassword);
        row_layout->addWidget(option, 1);
        auto* detail_label = new QLabel(detail, row);
        detail_label->setObjectName(QStringLiteral("EditorGeneratorHint"));
        detail_label->setProperty("mono", true);
        row_layout->addWidget(detail_label);
        body_layout->addWidget(row);
        *target = option;
    };
    add_option(QStringLiteral("EditorGeneratorUppercase"),
               QStringLiteral("大写字母"), QStringLiteral("A-Z"),
               &generator_uppercase_);
    add_option(QStringLiteral("EditorGeneratorLowercase"),
               QStringLiteral("小写字母"), QStringLiteral("a-z"),
               &generator_lowercase_);
    add_option(QStringLiteral("EditorGeneratorNumbers"), QStringLiteral("数字"),
               QStringLiteral("0-9"), &generator_numbers_);
    add_option(QStringLiteral("EditorGeneratorSymbols"),
               QStringLiteral("特殊符号"), QStringLiteral("!@#"),
               &generator_symbols_);

    generator_error_ = new QLabel(body);
    generator_error_->setObjectName(QStringLiteral("EditorGeneratorError"));
    generator_error_->setVisible(false);
    body_layout->addWidget(generator_error_);
    body_layout->addStretch(1);

    content_layout->addWidget(body, 1, Qt::AlignHCenter | Qt::AlignTop);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto* footer = new QWidget(page);
    footer->setObjectName(QStringLiteral("EditorGeneratorFooter"));
    auto* footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(28, 16, 28, 16);
    footer_layout->setSpacing(8);
    footer_layout->addStretch(1);
    auto* regenerate = new QPushButton(QStringLiteral("重新生成"), footer);
    regenerate->setObjectName(QStringLiteral("EditorGeneratorRegenerate"));
    regenerate->setProperty("flat", true);
    regenerate->setIcon(LoadEditorIcon(QStringLiteral("refresh-cw"),
                                      QStringLiteral("muted-3"), 14));
    regenerate->setIconSize(QSize(14, 14));
    regenerate->setFixedHeight(36);
    connect(regenerate, &QPushButton::clicked, this,
            &EditorPanel::RegeneratePassword);
    footer_layout->addWidget(regenerate);
    auto* apply = new QPushButton(QStringLiteral("使用此密码"), footer);
    apply->setObjectName(QStringLiteral("EditorGeneratorApply"));
    apply->setProperty("accent", true);
    apply->setFixedHeight(36);
    apply->setEnabled(!generator_preview_->text().isEmpty());
    connect(generator_preview_, &QLineEdit::textChanged, apply,
            [apply](const QString& password) {
                apply->setEnabled(!password.isEmpty());
            });
    connect(apply, &QPushButton::clicked, this,
            &EditorPanel::OnApplyGeneratedPassword);
    footer_layout->addWidget(apply);
    root->addWidget(footer);

    RegeneratePassword();
    return page;
}

void EditorPanel::SetCategories(QList<model::Category> categories) {
    categories_ = std::move(categories);
    FillCategoryCombo();
}

void EditorPanel::FillCategoryCombo() {
    if (!category_combo_) return;
    const qint64 previous = category_combo_->currentData().toLongLong();
    category_combo_->clear();
    category_combo_->addItem(QStringLiteral("未分类"),
                              QVariant::fromValue<qint64>(0));
    for (const auto& c : categories_) {
        if (c.is_deleted) continue;
        category_combo_->addItem(c.name, QVariant::fromValue<qint64>(c.id));
    }
    for (int i = 0; i < category_combo_->count(); ++i) {
        if (category_combo_->itemData(i).toLongLong() == previous) {
            category_combo_->setCurrentIndex(i);
            break;
        }
    }
}

void EditorPanel::OpenForCreate() {
    mode_ = Mode::kCreate;
    entry_ = {};
    ResetFields();
    RefreshMode();
    UpdatePositionFromParent();
    show();
    raise();
    setFocus();
    AnimateIn();
    is_open_ = true;
    if (title_input_) title_input_->setFocus();
}

void EditorPanel::OpenForEdit(const DecryptedEntry& entry) {
    mode_ = Mode::kEdit;
    entry_ = entry;
    FillFromEntry(entry);
    RefreshMode();
    UpdatePositionFromParent();
    show();
    raise();
    setFocus();
    AnimateIn();
    is_open_ = true;
    if (title_input_) title_input_->setFocus();
}

void EditorPanel::Close() {
    if (!is_open_) return;
    is_open_ = false;
    if (pages_) pages_->setCurrentWidget(editor_page_);
    AnimateOut();
}

void EditorPanel::ResetFields() {
    title_input_->clear();
    website_input_->clear();
    username_input_->clear();
    password_input_->clear();
    notes_input_->clear();
    preview_toggle_->setChecked(false);
    password_input_->setEchoMode(QLineEdit::Password);
    preview_toggle_->setIcon(LoadEditorIcon(QStringLiteral("eye"),
                                            QStringLiteral("hint-8"), 14));
    FillCategoryCombo();
    if (category_combo_->count() > 0) category_combo_->setCurrentIndex(0);
    UpdateValidationErrors(false, false);
    UpdateStrength(0);
}

void EditorPanel::FillFromEntry(const DecryptedEntry& entry) {
    FillCategoryCombo();
    title_input_->setText(entry.entry.title);
    website_input_->setText(entry.entry.website);
    username_input_->setText(entry.entry.username);
    password_input_->setText(entry.password);
    notes_input_->setPlainText(entry.entry.notes);
    preview_toggle_->setChecked(false);
    password_input_->setEchoMode(QLineEdit::Password);
    preview_toggle_->setIcon(LoadEditorIcon(QStringLiteral("eye"),
                                            QStringLiteral("hint-8"), 14));
    for (int i = 0; i < category_combo_->count(); ++i) {
        if (category_combo_->itemData(i).toLongLong() ==
            entry.entry.category_id) {
            category_combo_->setCurrentIndex(i);
            break;
        }
    }
    UpdateValidationErrors(false, false);
    UpdateStrength(generator::CalculatePasswordStrength(entry.password));
}

void EditorPanel::UpdateValidationErrors(bool title_error,
                                         bool credentials_error) {
    title_input_->setProperty("error", title_error);
    username_input_->setProperty("error", credentials_error);
    password_input_->setProperty("error", credentials_error);
    password_field_->setProperty("error", credentials_error);
    title_error_->setVisible(title_error);
    credentials_error_->setVisible(credentials_error);
    RepolishWidget(title_input_);
    RepolishWidget(username_input_);
    RepolishWidget(password_input_);
    RepolishWidget(password_field_);
}

void EditorPanel::RefreshMode() {
    if (pages_) pages_->setCurrentWidget(editor_page_);
    if (mode_ == Mode::kCreate) {
        header_title_->setText(QStringLiteral("新建密码"));
        save_button_->setText(QStringLiteral("创建密码"));
        return;
    }
    header_title_->setText(
        QStringLiteral("编辑密码 · %1").arg(entry_.entry.title));
    save_button_->setText(QStringLiteral("保存更改"));
}

void EditorPanel::RefreshThemeAssets() {
    if (auto* shadow =
            qobject_cast<QGraphicsDropShadowEffect*>(graphicsEffect())) {
        shadow->setColor(
            ThemeManager::Instance()->Color(QStringLiteral("shadow")));
    }
    const auto set_tool_icon = [this](const QString& object_name,
                                      const QString& icon_name,
                                      const QString& token, int size) {
        if (auto* button = findChild<QToolButton*>(object_name)) {
            button->setIcon(LoadEditorIcon(icon_name, token, size));
        }
    };

    set_tool_icon(QStringLiteral("EditorHeaderBack"),
                  QStringLiteral("arrow-left"), QStringLiteral("muted-7"),
                  18);
    set_tool_icon(QStringLiteral("EditorHeaderClose"), QStringLiteral("x"),
                  QStringLiteral("muted-7"), 20);
    set_tool_icon(QStringLiteral("EditorGenerateButton"),
                  QStringLiteral("refresh-cw"), QStringLiteral("accent"), 12);
    if (preview_toggle_) {
        preview_toggle_->setIcon(LoadEditorIcon(
            preview_toggle_->isChecked() ? QStringLiteral("eye-off")
                                         : QStringLiteral("eye"),
            QStringLiteral("hint-8"), 14));
    }
    set_tool_icon(QStringLiteral("EditorGeneratorBack"),
                  QStringLiteral("arrow-left"), QStringLiteral("muted-7"),
                  18);
    set_tool_icon(QStringLiteral("EditorGeneratorClose"),
                  QStringLiteral("x"), QStringLiteral("muted-7"), 20);
    set_tool_icon(QStringLiteral("EditorGeneratorRefresh"),
                  QStringLiteral("refresh-cw"), QStringLiteral("accent"), 16);
    if (auto* regenerate = findChild<QPushButton*>(
            QStringLiteral("EditorGeneratorRegenerate"))) {
        regenerate->setIcon(LoadEditorIcon(
            QStringLiteral("refresh-cw"), QStringLiteral("muted-3"), 14));
    }
}

EditorPanel::DecryptedEntry EditorPanel::Result() const {
    DecryptedEntry out = entry_;
    out.entry.title = title_input_->text().trimmed();
    out.entry.website = website_input_->text().trimmed();
    out.entry.username = username_input_->text();
    out.entry.notes = notes_input_->toPlainText();
    out.entry.category_id = category_combo_->currentData().toLongLong();
    out.password = password_input_->text();
    out.entry.strength = generator::CalculatePasswordStrength(out.password);
    return out;
}

void EditorPanel::ApplyGeneratedPassword(const QString& password) {
    password_input_->setText(password);
}

void EditorPanel::OnPasswordTextChanged(const QString& value) {
    UpdateStrength(generator::CalculatePasswordStrength(value));
}

void EditorPanel::OnTogglePasswordPreview() {
    const bool visible = preview_toggle_->isChecked();
    password_input_->setEchoMode(visible ? QLineEdit::Normal
                                          : QLineEdit::Password);
    preview_toggle_->setIcon(LoadEditorIcon(
        visible ? QStringLiteral("eye-off") : QStringLiteral("eye"),
        QStringLiteral("hint-8"), 14));
}

void EditorPanel::OnShowGenerator() {
    if (!pages_) return;
    if (password_input_ && !password_input_->text().isEmpty()) {
        generator_preview_->setText(password_input_->text());
        generator_error_->setVisible(false);
    } else {
        RegeneratePassword();
    }
    pages_->setCurrentWidget(generator_page_);
}

void EditorPanel::OnHideGenerator() {
    if (!pages_) return;
    pages_->setCurrentWidget(editor_page_);
    if (generate_button_) generate_button_->setFocus();
}

void EditorPanel::RegeneratePassword() {
    if (!generator_preview_ || !generator_length_ || !generator_uppercase_ ||
        !generator_lowercase_ || !generator_numbers_ || !generator_symbols_) {
        return;
    }

    generator::PasswordConfig config;
    config.length = generator_length_->value();
    config.include_uppercase = generator_uppercase_->isChecked();
    config.include_lowercase = generator_lowercase_->isChecked();
    config.include_numbers = generator_numbers_->isChecked();
    config.include_symbols = generator_symbols_->isChecked();
    const auto generated = generator::PasswordGenerator::Generate(config);
    if (!generated.has_value()) {
        generator_preview_->clear();
        generator_error_->setText(QStringLiteral("至少选择一类字符。"));
        generator_error_->setVisible(true);
        return;
    }

    generator_preview_->setText(*generated);
    generator_error_->setVisible(false);
}

void EditorPanel::OnApplyGeneratedPassword() {
    if (!generator_preview_ || generator_preview_->text().isEmpty()) return;
    ApplyGeneratedPassword(generator_preview_->text());
    OnHideGenerator();
}

void EditorPanel::UpdateStrength(int level) {
    for (int i = 0; i < 4; ++i) {
        auto* seg = strength_segments_[i];
        const bool filled = i < level + 1 && level > 0;
        seg->setProperty("filled", filled);
        seg->setProperty("level", filled ? QString::number(level) : QString());
        RepolishWidget(seg);
    }
    strength_label_->setText(StrengthText(level));
}

void EditorPanel::OnSaveClicked() {
    const bool title_empty = title_input_->text().trimmed().isEmpty();
    const bool username_empty = username_input_->text().trimmed().isEmpty();
    const bool password_empty = password_input_->text().isEmpty();
    const bool credentials_empty = username_empty && password_empty;

    UpdateValidationErrors(title_empty, credentials_empty);

    if (title_empty || credentials_empty) return;
    emit SaveRequested();
}

void EditorPanel::OnCancelClicked() { emit CancelRequested(); }

void EditorPanel::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (pages_ && pages_->currentWidget() == generator_page_) {
            OnHideGenerator();
            event->accept();
            return;
        }
        OnCancelClicked();
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

bool EditorPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parent() && event->type() == QEvent::Resize) {
        UpdatePositionFromParent();
    }
    return QFrame::eventFilter(watched, event);
}

void EditorPanel::UpdatePositionFromParent() {
    auto* p = parentWidget();
    if (!p) return;
    const int h = p->height();
    const int x_open = p->width() - kPanelWidth;
    if (is_open_ || isVisible()) {
        setGeometry(x_open, 0, kPanelWidth, h);
    } else {
        setGeometry(p->width(), 0, kPanelWidth, h);
    }
}

void EditorPanel::AnimateIn() {
    auto* p = parentWidget();
    if (!p) return;
    const int h = p->height();
    const int x_open = p->width() - kPanelWidth;
    anim_->stop();
    // Drop any finished->hide() handler left over from a previous AnimateOut,
    // otherwise the completing open animation would hide the panel (flash).
    disconnect(anim_, &QPropertyAnimation::finished, this, nullptr);
    anim_->setStartValue(QRect(p->width(), 0, kPanelWidth, h));
    anim_->setEndValue(QRect(x_open, 0, kPanelWidth, h));
    anim_->start();
}

void EditorPanel::AnimateOut() {
    auto* p = parentWidget();
    if (!p) {
        hide();
        return;
    }
    const int h = p->height();
    const int x_open = p->width() - kPanelWidth;
    anim_->stop();
    anim_->setStartValue(QRect(x_open, 0, kPanelWidth, h));
    anim_->setEndValue(QRect(p->width(), 0, kPanelWidth, h));
    disconnect(anim_, &QPropertyAnimation::finished, this, nullptr);
    connect(anim_, &QPropertyAnimation::finished, this, [this]() { hide(); });
    anim_->start();
}

}  // namespace passvault::ui
