#include "ui/editor_panel.h"

#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

#include "generator/password_strength.h"
#include "ui/icon_loader.h"

namespace passvault::ui {

namespace {

constexpr int kPanelWidth = 480;
constexpr int kAnimationMs = 220;

QString StrengthText(int level) {
    switch (level) {
        case 3:
            return QStringLiteral("强度：强");
        case 2:
            return QStringLiteral("强度：中");
        case 1:
        default:
            return QStringLiteral("强度：弱");
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
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(24, 0, 16, 0);
    header_layout->setSpacing(8);
    header_title_ = new QLabel(QStringLiteral("新建密码"), header);
    header_title_->setObjectName(QStringLiteral("EditorHeaderTitle"));
    header_close_ = new QToolButton(header);
    header_close_->setObjectName(QStringLiteral("EditorHeaderClose"));
    header_close_->setIcon(IconLoader::Load(QStringLiteral("x")));
    header_close_->setToolTip(QStringLiteral("关闭"));
    header_close_->setFixedSize(32, 32);
    connect(header_close_, &QToolButton::clicked, this,
            &EditorPanel::OnCancelClicked);
    header_layout->addWidget(header_title_, 1);
    header_layout->addWidget(header_close_);
    root->addWidget(header);

    root->addWidget(BuildOverviewPage(), 1);

    auto* footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("EditorFooter"));
    auto* footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(24, 16, 24, 16);
    footer_layout->setSpacing(8);
    footer_layout->addStretch(1);
    cancel_button_ = new QPushButton(QStringLiteral("取消"), footer);
    cancel_button_->setObjectName(QStringLiteral("EditorCancelButton"));
    cancel_button_->setProperty("flat", true);
    connect(cancel_button_, &QPushButton::clicked, this,
            &EditorPanel::OnCancelClicked);
    save_button_ = new QPushButton(QStringLiteral("保存"), footer);
    save_button_->setObjectName(QStringLiteral("EditorSaveButton"));
    save_button_->setProperty("accent", true);
    connect(save_button_, &QPushButton::clicked, this,
            &EditorPanel::OnSaveClicked);
    footer_layout->addWidget(cancel_button_);
    footer_layout->addWidget(save_button_);
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
    v->setContentsMargins(24, 12, 24, 12);
    v->setSpacing(10);

    auto add_field = [&](const QString& label_text, QWidget* editor) {
        auto* label = new QLabel(label_text, container);
        label->setObjectName(QStringLiteral("EditorFieldLabel"));
        v->addWidget(label);
        v->addWidget(editor);
    };

    title_input_ = new QLineEdit(container);
    title_input_->setObjectName(QStringLiteral("EditorTitleInput"));
    title_input_->setPlaceholderText(QStringLiteral("必填"));
    add_field(QStringLiteral("标题"), title_input_);

    category_combo_ = new QComboBox(container);
    category_combo_->setObjectName(QStringLiteral("EditorCategoryCombo"));
    // QComboBox popups default to QItemDelegate, which ignores stylesheet
    // ::item colors and paints the native (dark) selection highlight. A
    // QStyledItemDelegate makes the popup honor the QSS accent-soft selection.
    category_combo_->setItemDelegate(
        new QStyledItemDelegate(category_combo_));
    add_field(QStringLiteral("分类"), category_combo_);

    username_input_ = new QLineEdit(container);
    username_input_->setObjectName(QStringLiteral("EditorUsernameInput"));
    add_field(QStringLiteral("用户名"), username_input_);

    auto* password_label = new QLabel(QStringLiteral("密码"), container);
    password_label->setObjectName(QStringLiteral("EditorFieldLabel"));
    v->addWidget(password_label);

    auto* password_row = new QWidget(container);
    auto* password_row_layout = new QHBoxLayout(password_row);
    password_row_layout->setContentsMargins(0, 0, 0, 0);
    password_row_layout->setSpacing(6);
    password_input_ = new QLineEdit(password_row);
    password_input_->setObjectName(QStringLiteral("EditorPasswordInput"));
    password_input_->setEchoMode(QLineEdit::Password);
    password_input_->setProperty("mono", true);
    connect(password_input_, &QLineEdit::textChanged, this,
            &EditorPanel::OnPasswordTextChanged);
    preview_toggle_ = new QToolButton(password_row);
    preview_toggle_->setObjectName(QStringLiteral("EditorPreviewToggle"));
    preview_toggle_->setCheckable(true);
    preview_toggle_->setIcon(IconLoader::Load(QStringLiteral("eye")));
    preview_toggle_->setToolTip(QStringLiteral("显示 / 隐藏密码"));
    preview_toggle_->setFixedSize(32, 32);
    connect(preview_toggle_, &QToolButton::toggled, this,
            &EditorPanel::OnTogglePasswordPreview);
    generate_button_ = new QToolButton(password_row);
    generate_button_->setObjectName(QStringLiteral("EditorGenerateButton"));
    generate_button_->setProperty("soft", true);
    generate_button_->setText(QStringLiteral("生成"));
    generate_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    generate_button_->setToolTip(QStringLiteral("生成随机密码"));
    generate_button_->setFixedHeight(32);
    connect(generate_button_, &QToolButton::clicked, this,
            &EditorPanel::GenerateRequested);
    password_row_layout->addWidget(password_input_, 1);
    password_row_layout->addWidget(preview_toggle_);
    password_row_layout->addWidget(generate_button_);
    v->addWidget(password_row);

    auto* strength_row = new QWidget(container);
    auto* strength_layout = new QHBoxLayout(strength_row);
    strength_layout->setContentsMargins(0, 4, 0, 0);
    strength_layout->setSpacing(4);
    for (int i = 0; i < 4; ++i) {
        auto* seg = new QFrame(strength_row);
        seg->setProperty("strength-segment", true);
        seg->setFixedHeight(4);
        strength_segments_[i] = seg;
        strength_layout->addWidget(seg, 1);
    }
    strength_label_ = new QLabel(StrengthText(1), strength_row);
    strength_label_->setObjectName(QStringLiteral("EditorStrengthLabel"));
    strength_label_->setProperty("strength-label", true);
    strength_layout->addSpacing(8);
    strength_layout->addWidget(strength_label_);
    v->addWidget(strength_row);

    website_input_ = new QLineEdit(container);
    website_input_->setObjectName(QStringLiteral("EditorWebsiteInput"));
    website_input_->setPlaceholderText(QStringLiteral("https://..."));
    add_field(QStringLiteral("网址"), website_input_);

    notes_input_ = new QTextEdit(container);
    notes_input_->setObjectName(QStringLiteral("EditorNotesInput"));
    notes_input_->setPlaceholderText(QStringLiteral("备注 (可选)"));
    notes_input_->setFixedHeight(72);
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
    preview_toggle_->setIcon(IconLoader::Load(QStringLiteral("eye")));
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
    preview_toggle_->setIcon(IconLoader::Load(QStringLiteral("eye")));
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
    header_title_->setText(mode_ == Mode::kCreate
                                ? QStringLiteral("新建密码")
                                : QStringLiteral("编辑密码"));
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
    preview_toggle_->setIcon(IconLoader::Load(
        visible ? QStringLiteral("eye-off") : QStringLiteral("eye")));
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
    RepolishWidget(title_input_);
    RepolishWidget(username_input_);
    RepolishWidget(password_input_);

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
