#include "ui/editor_panel.h"

#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShortcut>
#include <QSizePolicy>
#include <QStyledItemDelegate>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

#include "generator/password_strength.h"
#include "ui/icon_loader.h"
#include "ui/theme_manager.h"

namespace passvault::ui {

namespace {

constexpr int kPanelWidth = 372;
constexpr int kNavigationWidth = 98;
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
    shadow->setColor(QColor(50, 73, 105, 36));
    shadow->setOffset(-18, 0);
    setGraphicsEffect(shadow);

    BuildUi();
    hide();

    anim_ = new QPropertyAnimation(this, "geometry", this);
    anim_->setDuration(kAnimationMs);
    anim_->setEasingCurve(QEasingCurve::OutCubic);

    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    esc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(esc, &QShortcut::activated, this, &EditorPanel::OnCancelClicked);

    if (parent) parent->installEventFilter(this);
}

EditorPanel::~EditorPanel() = default;

void EditorPanel::BuildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("EditorHeader"));
    header->setFixedHeight(68);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(20, 0, 20, 0);
    header_layout->setSpacing(8);

    auto* header_back = new QLabel(header);
    header_back->setObjectName(QStringLiteral("EditorHeaderBackIcon"));
    header_back->setFixedSize(16, 16);
    header_back->setPixmap(LoadEditorIcon(QStringLiteral("arrow-left"),
                                         QStringLiteral("text-primary"), 16)
                               .pixmap(16, 16));
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

    auto* content = new QWidget(this);
    content->setObjectName(QStringLiteral("EditorContent"));
    auto* content_layout = new QHBoxLayout(content);
    content_layout->setContentsMargins(0, 0, 0, 0);
    content_layout->setSpacing(0);

    navigation_ = new QListWidget(content);
    navigation_->setObjectName(QStringLiteral("EditorNavigation"));
    navigation_->setProperty("variant", QStringLiteral("editor-nav"));
    navigation_->setFixedWidth(kNavigationWidth);
    navigation_->setFocusPolicy(Qt::NoFocus);
    navigation_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation_->addItems({QStringLiteral("基本信息"),
                           QStringLiteral("更多信息"),
                           QStringLiteral("高级设置")});
    navigation_->setCurrentRow(0);
    content_layout->addWidget(navigation_);
    content_layout->addWidget(BuildOverviewPage(), 1);
    root->addWidget(content, 1);

    auto* footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("EditorFooter"));
    auto* footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(16, 16, 16, 16);
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
    footer_layout->addWidget(cancel_button_, 1);
    footer_layout->addWidget(save_button_, 1);
    root->addWidget(footer);
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

    auto* container = new QWidget;
    container->setObjectName(QStringLiteral("EditorBody"));
    auto* v = new QVBoxLayout(container);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(16);

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
    };

    title_input_ = new QLineEdit(container);
    title_input_->setObjectName(QStringLiteral("EditorTitleInput"));
    title_input_->setPlaceholderText(QStringLiteral("必填"));
    title_input_->setFixedHeight(36);
    add_field(QStringLiteral("标题"), title_input_);

    username_input_ = new QLineEdit(container);
    username_input_->setObjectName(QStringLiteral("EditorUsernameInput"));
    username_input_->setFixedHeight(36);
    add_field(QStringLiteral("用户名"), username_input_);

    auto* password_group = new QWidget(container);
    password_group->setObjectName(QStringLiteral("EditorPasswordGroup"));
    auto* password_group_layout = new QVBoxLayout(password_group);
    password_group_layout->setContentsMargins(0, 0, 0, 0);
    password_group_layout->setSpacing(6);
    auto* password_label = new QLabel(QStringLiteral("密码"), password_group);
    password_label->setObjectName(QStringLiteral("EditorFieldLabel"));
    password_group_layout->addWidget(password_label);

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
    generate_button_ = new QToolButton(strength_header);
    generate_button_->setObjectName(QStringLiteral("EditorGenerateButton"));
    generate_button_->setText(QStringLiteral("生成"));
    generate_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    generate_button_->setToolTip(QStringLiteral("生成随机密码"));
    generate_button_->setFixedHeight(18);
    connect(generate_button_, &QToolButton::clicked, this,
            &EditorPanel::GenerateRequested);
    strength_header_layout->addWidget(generate_button_);
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
    strength_label_ = new QLabel(StrengthText(1), strength_group);
    strength_label_->setObjectName(QStringLiteral("EditorStrengthLabel"));
    strength_label_->setProperty("strength-label", true);
    strength_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    strength_group_layout->addWidget(strength_label_);
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
    scroll->setWidget(container);
    return scroll;
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
    UpdateStrength(generator::CalculatePasswordStrength(entry.password));
}

void EditorPanel::RefreshMode() {
    if (navigation_) navigation_->setCurrentRow(0);
    if (mode_ == Mode::kCreate) {
        header_title_->setText(QStringLiteral("新建密码"));
        save_button_->setText(QStringLiteral("创建密码"));
        return;
    }
    header_title_->setText(
        QStringLiteral("编辑密码 · %1").arg(entry_.entry.title));
    save_button_->setText(QStringLiteral("保存更改"));
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

    title_input_->setProperty("error", title_empty);
    username_input_->setProperty("error", credentials_empty);
    password_input_->setProperty("error", credentials_empty);
    password_field_->setProperty("error", credentials_empty);
    RepolishWidget(title_input_);
    RepolishWidget(username_input_);
    RepolishWidget(password_input_);
    RepolishWidget(password_field_);

    if (title_empty || credentials_empty) return;
    emit SaveRequested();
}

void EditorPanel::OnCancelClicked() { emit CancelRequested(); }

void EditorPanel::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
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
