#include "ui/main_window.h"

#include <QCloseEvent>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsEffect>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QTimer>
#include <QWindowStateChangeEvent>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <exception>
#include <limits>

#include "crypto/crypto_service.h"
#include "crypto/random.h"
#include "crypto/session_key.h"
#include "generator/password_strength.h"
#include "storage/category_dao.h"
#include "storage/database.h"
#include "storage/password_dao.h"
#include "sync/sync_manager.h"
#include "sync/sync_scheduler.h"
#include "ui/clipboard_manager.h"
#include "ui/detail_panel.h"
#include "ui/elided_label.h"
#include "ui/editor_panel.h"
#include "ui/icon_loader.h"
#include "ui/preferences_page.h"
#include "ui/theme_manager.h"
#include "ui/toast.h"

namespace passvault::ui {

namespace {

constexpr std::int64_t kSectionAll = -1;
constexpr std::int64_t kSectionFavorites = -2;
constexpr std::int64_t kSectionUncategorized = 0;
constexpr std::int64_t kSectionTrash = -3;

constexpr int kSidebarWidth = 208;
constexpr int kDetailWidth = 400;
constexpr int kPasswordListMinWidth = 420;
constexpr int kWorkspaceHeaderHeight = 64;
constexpr int kSearchMinWidth = 260;
constexpr int kSearchMaxWidth = 420;
constexpr int kSearchHeight = 32;
constexpr int kIconSize = 16;
constexpr int kSectionIconSize = 16;
constexpr int kCardHeight = 62;
constexpr int kSidebarBreakpoint = 1024;
constexpr int kSidebarCountRole = Qt::UserRole + 1;
constexpr int kSidebarSystemRole = Qt::UserRole + 2;
constexpr int kSidebarIconRole = Qt::UserRole + 3;
constexpr int kShowZeroCountRole = Qt::UserRole + 5;

void SetCardFavorite(QWidget* card, bool favorite) {
    if (auto* button = card->findChild<QToolButton*>(
            QStringLiteral("PasswordCardFavoriteButton"))) {
        button->setToolTip(favorite ? QStringLiteral("取消收藏") : QStringLiteral("收藏"));
        button->setAccessibleName(button->toolTip());
        button->setIcon(IconLoader::Load(
            QStringLiteral("star"), ThemeManager::Instance()->Color(
                favorite ? QStringLiteral("primary") : QStringLiteral("text-secondary")), 16));
    }
}

class PasswordList final : public QListWidget {
 public:
    using QListWidget::QListWidget;
    bool selection_only_gesture() const { return selection_only_gesture_; }

 protected:
    void mousePressEvent(QMouseEvent* event) override {
        selection_only_gesture_ = event->button() == Qt::RightButton ||
                                  event->modifiers().testFlag(Qt::ControlModifier) ||
                                  event->modifiers().testFlag(Qt::ShiftModifier);
        QListWidget::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        QListWidget::mouseReleaseEvent(event);
        selection_only_gesture_ = false;
    }

 private:
    bool selection_only_gesture_ = false;
};

constexpr int kSearchGroupRole = Qt::UserRole + 4;

class PasswordItemDelegate final : public QStyledItemDelegate {
 public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        if (!index.data(kSearchGroupRole).toBool()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        painter->save();
        auto* theme = ThemeManager::Instance();
        painter->fillRect(option.rect, theme->Color(QStringLiteral("bg-canvas")));
        QFont font = option.font;
        font.setPixelSize(13);
        font.setWeight(QFont::DemiBold);
        painter->setFont(font);
        painter->setPen(theme->Color(QStringLiteral("text-secondary")));
        painter->drawText(option.rect.adjusted(4, 0, -4, 0),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          index.data(Qt::DisplayRole).toString());
        painter->restore();
    }
};

class PasswordCard final : public QFrame {
 protected:
    void resizeEvent(QResizeEvent* event) override {
        QFrame::resizeEvent(event);
        if (auto* time = findChild<QLabel*>(QStringLiteral("PasswordCardTime"))) {
            time->setVisible(!property("trash").toBool() && width() >= (findChild<QToolButton*>(
                QStringLiteral("PasswordCardEditButton")) ? 480 : 360));
        }
    }
};

class SidebarItemDelegate final : public QStyledItemDelegate {
 public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem background(option);
        initStyleOption(&background, index);
        background.text.clear();
        background.icon = {};
        QStyle* style = option.widget ? option.widget->style()
                                      : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &background, painter,
                           option.widget);

        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        auto* theme = ThemeManager::Instance();
        const QColor foreground = theme->Color(
            selected ? QStringLiteral("primary-text-on-tint")
                     : hovered ? QStringLiteral("text-primary")
                               : QStringLiteral("text-secondary"));
        const QRect content = option.rect.adjusted(16, 0, -16, 0);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QString icon_name =
            index.data(kSidebarIconRole).toString();
        const QRect icon_rect(content.left(),
                              content.center().y() - kSectionIconSize / 2,
                              kSectionIconSize, kSectionIconSize);
        IconLoader::Load(icon_name, foreground, kSectionIconSize)
            .paint(painter, icon_rect);

        QRect text_rect = content.adjusted(kSectionIconSize + 12, 0, 0, 0);
        bool has_count = false;
        const int count = index.data(kSidebarCountRole).toInt(&has_count);
        if (has_count && (count > 0 || index.data(kShowZeroCountRole).toBool())) {
            QFont count_font = option.font;
            count_font.setPixelSize(11);
            count_font.setWeight(selected ? QFont::Bold : QFont::Medium);
            const QFontMetrics count_metrics(count_font);
            const QString count_text = QString::number(count);
            const int badge_width =
                std::max(22, count_metrics.horizontalAdvance(count_text) + 12);
            const QRect badge_rect(content.right() - badge_width + 1,
                                   content.center().y() - 9, badge_width, 18);
            painter->setPen(Qt::NoPen);
            painter->setBrush(theme->Color(
                selected ? QStringLiteral("primary-tint-2")
                         : QStringLiteral("bg-muted")));
            painter->drawRoundedRect(badge_rect, 9, 9);
            painter->setFont(count_font);
            painter->setPen(selected ? foreground
                                     : theme->Color(QStringLiteral("muted-8")));
            painter->drawText(badge_rect, Qt::AlignCenter, count_text);
            text_rect.setRight(badge_rect.left() - 8);
        }

        QFont text_font = option.font;
        if (selected) text_font.setWeight(QFont::Bold);
        painter->setFont(text_font);
        painter->setPen(foreground);
        painter->drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(text_font).elidedText(
                              index.data(Qt::DisplayRole).toString(),
                              Qt::ElideRight, text_rect.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(28);
        return size;
    }
};

QString RelativeTimeShort(std::int64_t ms_since_epoch) {
    if (ms_since_epoch <= 0) return {};
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch();
    const std::int64_t diff = now - ms_since_epoch;
    if (diff < 60 * 1000) return QStringLiteral("刚刚");
    if (diff < 60 * 60 * 1000) {
        return QStringLiteral("%1 分钟前").arg(diff / (60 * 1000));
    }
    if (diff < 24 * 60 * 60 * 1000LL) {
        return QStringLiteral("%1 小时前").arg(diff / (60 * 60 * 1000));
    }
    const std::int64_t days = diff / (24 * 60 * 60 * 1000LL);
    if (days < 30) return QStringLiteral("%1 天前").arg(days);
    return QDateTime::fromMSecsSinceEpoch(ms_since_epoch)
        .toString(QStringLiteral("yyyy-MM-dd"));
}

QString AvatarInitial(const model::PasswordEntry& entry) {
    const QString source =
        !entry.title.isEmpty() ? entry.title
                                : !entry.website.isEmpty() ? entry.website
                                                            : entry.username;
    if (source.isEmpty()) return QStringLiteral("?");
    return QString(source.at(0).toUpper());
}

}  // namespace

MainWindow::MainWindow(const Deps& deps, QWidget* parent)
    : QMainWindow(parent), deps_(deps) {
    setWindowTitle(QStringLiteral("PassVault · 密码保险库"));
    resize(1200, 800);

    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("WorkspaceRoot"));
    auto* root_layout = new QVBoxLayout(root);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    auto* container = new QWidget(root);
    container->setObjectName(QStringLiteral("WorkspaceContainer"));
    auto* container_layout = new QVBoxLayout(container);
    container_layout->setContentsMargins(0, 0, 0, 0);
    container_layout->setSpacing(0);

    central_stack_ = new QStackedWidget(container);
    container_layout->addWidget(central_stack_);

    workspace_page_ = new QWidget(central_stack_);
    auto* ws_layout = new QHBoxLayout(workspace_page_);
    ws_layout->setContentsMargins(0, 0, 0, 0);
    ws_layout->setSpacing(0);
    sidebar_ = BuildSidebar();
    ws_layout->addWidget(sidebar_);

    workspace_ = new QWidget(workspace_page_);
    workspace_->setObjectName(QStringLiteral("VaultWorkspace"));
    workspace_->setFocusPolicy(Qt::StrongFocus);
    auto* workspace_layout = new QVBoxLayout(workspace_);
    workspace_layout->setContentsMargins(0, 0, 0, 0);
    workspace_layout->setSpacing(0);
    workspace_layout->addWidget(BuildWorkspaceHeader());

    workspace_content_ = new QWidget(workspace_);
    workspace_content_->setObjectName(QStringLiteral("VaultWorkspaceContent"));
    workspace_content_->installEventFilter(this);
    workspace_content_layout_ = new QHBoxLayout(workspace_content_);
    workspace_content_layout_->setContentsMargins(0, 0, 0, 0);
    workspace_content_layout_->setSpacing(0);
    password_list_column_ = BuildPasswordListColumn();
    workspace_content_layout_->addWidget(password_list_column_, 1);
    BuildDetailColumn();
    workspace_content_layout_->addWidget(detail_panel_, 1);
    workspace_layout->addWidget(workspace_content_, 1);
    ws_layout->addWidget(workspace_, 1);
    central_stack_->addWidget(workspace_page_);

    editor_panel_ = new EditorPanel(workspace_page_);
    editor_panel_->installEventFilter(this);
    connect(editor_panel_, &EditorPanel::SaveRequested, this,
            &MainWindow::OnEditorSaveRequested);
    connect(editor_panel_, &EditorPanel::CancelRequested, this,
            &MainWindow::OnEditorCancelRequested);

    preferences_page_ = new PreferencesPage(central_stack_);
    connect(preferences_page_, &PreferencesPage::BackRequested, this,
            [this]() { central_stack_->setCurrentWidget(workspace_page_); });
    central_stack_->addWidget(preferences_page_);
    const auto apply_visibility = [this](bool visible) {
        detail_panel_->SetDefaultPasswordVisible(visible);
        editor_panel_->SetDefaultPasswordVisible(visible);
    };
    apply_visibility(preferences_page_->DefaultPasswordVisible());
    connect(preferences_page_, &PreferencesPage::DefaultPasswordVisibilityChanged,
            this, apply_visibility);

    root_layout->addWidget(container);
    setCentralWidget(root);

    connect(ThemeManager::Instance(), &ThemeManager::ThemeChanged, this,
            [this](Theme) { RefreshThemeAssets(); });

    if (deps_.sync_manager) {
        connect(deps_.sync_manager, &sync::SyncManager::SyncStarted, this,
                &MainWindow::OnSyncStarted);
        connect(deps_.sync_manager, &sync::SyncManager::SyncFinished, this,
                &MainWindow::OnSyncFinished);
    }
    Reload();
    UpdateResponsiveLayout();
}

MainWindow::~MainWindow() = default;

void MainWindow::SetMigrationActive(bool active) {
    migration_active_ = active;
    centralWidget()->setEnabled(!active);
    if (active) {
        SetSessionKey(nullptr);
        ClipboardManager::Instance()->ClearSensitive();
    }
}

void MainWindow::SetResetConfirmationActive(bool active) {
    reset_confirmation_active_ = active;
    centralWidget()->setEnabled(!active && !migration_active_);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (reset_confirmation_active_ || migration_active_) {
        event->ignore();
        emit DeferredExitRequested();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::SetSessionKey(const crypto::SessionKey* session_key) {
    deps_.session_key = session_key;
    if (session_key) return;
    detail_panel_->ClearEntry();
    editor_panel_->ClearSensitiveState();
    ClipboardManager::Instance()->ClearSensitive();
    editor_entry_id_ = -1;
    ShowListPane();
}

QWidget* MainWindow::BuildSidebar() {
    auto* sidebar = new QFrame();
    sidebar->setObjectName(QStringLiteral("Sidebar"));
    sidebar->setFixedWidth(kSidebarWidth);
    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(12, 20, 12, 16);
    layout->setSpacing(0);

    auto* brand = new QWidget(sidebar);
    auto* brand_layout = new QHBoxLayout(brand);
    brand_layout->setContentsMargins(8, 0, 0, 18);
    brand_layout->setSpacing(10);
    auto* brand_icon = new QLabel(brand);
    brand_icon->setPixmap(QPixmap(QStringLiteral(":/icons/passvault.png"))
                              .scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    brand_layout->addWidget(brand_icon);
    auto* brand_name = new QLabel(QStringLiteral("PassVault"), brand);
    brand_name->setObjectName(QStringLiteral("SidebarBrandName"));
    brand_layout->addWidget(brand_name);
    brand_layout->addStretch(1);
    layout->addWidget(brand);

    auto* new_button = new QPushButton(QStringLiteral("新建密码"), sidebar);
    new_button->setFocusPolicy(Qt::TabFocus);
    new_button->setObjectName(QStringLiteral("NewPasswordButton"));
    new_button->setProperty("accent", true);
    new_button->setCursor(Qt::PointingHandCursor);
    new_button->setIcon(IconLoader::Load(
        QStringLiteral("plus"),
        ThemeManager::Instance()->Color(QStringLiteral("accent-fg")),
        17));
    new_button->setIconSize(QSize(17, 17));
    connect(new_button, &QPushButton::clicked, this,
            &MainWindow::OnNewPassword);
    new_button->setFixedHeight(32);
    layout->addWidget(new_button);
    layout->addSpacing(16);

    sections_list_ = new QListWidget(sidebar);
    sections_list_->setObjectName(QStringLiteral("SidebarSectionList"));
    sections_list_->setFrameShape(QFrame::NoFrame);
    sections_list_->setIconSize(QSize(kSectionIconSize, kSectionIconSize));
    sections_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    sections_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sections_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sections_list_->setItemDelegate(new SidebarItemDelegate(sections_list_));
    sections_list_->setFixedHeight(4 * 28);
    connect(sections_list_, &QListWidget::itemSelectionChanged, this,
            &MainWindow::OnSectionSelectionChanged);
    layout->addWidget(sections_list_);
    layout->addSpacing(16);

    auto* divider = new QFrame(sidebar);
    divider->setObjectName(QStringLiteral("SidebarDivider"));
    divider->setFixedHeight(1);
    layout->addWidget(divider);
    layout->addSpacing(16);

    auto* cat_header = new QWidget(sidebar);
    auto* cat_header_layout = new QHBoxLayout(cat_header);
    cat_header_layout->setContentsMargins(12, 0, 12, 0);
    cat_header_layout->setSpacing(6);
    auto* cat_title = new QLabel(QStringLiteral("分类"), cat_header);
    cat_title->setObjectName(QStringLiteral("SidebarSectionHeader"));
    cat_header_layout->addWidget(cat_title, 1);
    add_category_button_ = new QToolButton(cat_header);
    add_category_button_->setObjectName(
        QStringLiteral("SidebarAddCategoryButton"));
    add_category_button_->setToolTip(QStringLiteral("新增分类"));
    add_category_button_->setCursor(Qt::PointingHandCursor);
    add_category_button_->setFixedSize(28, 28);
    add_category_button_->setIcon(IconLoader::Load(
        QStringLiteral("plus"),
        ThemeManager::Instance()->Color(QStringLiteral("text-tertiary")),
        14));
    add_category_button_->setIconSize(QSize(14, 14));
    connect(add_category_button_, &QToolButton::clicked, this,
            &MainWindow::OnAddCategory);
    cat_header_layout->addWidget(add_category_button_);
    cat_header->setFixedHeight(28);
    layout->addWidget(cat_header);
    layout->addSpacing(8);

    categories_list_ = new QListWidget(sidebar);
    categories_list_->setObjectName(QStringLiteral("SidebarCategoryList"));
    categories_list_->setFrameShape(QFrame::NoFrame);
    categories_list_->setIconSize(QSize(kSectionIconSize, kSectionIconSize));
    categories_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    categories_list_->setDragDropMode(QAbstractItemView::InternalMove);
    categories_list_->setDefaultDropAction(Qt::MoveAction);
    categories_list_->setDragDropOverwriteMode(false);
    categories_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    categories_list_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    connect(categories_list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (suppress_selection_ || !item) return;
        const auto id = item->data(Qt::UserRole).toLongLong();
        const auto name = item->text();
        if (name == CategoryName(id)) return;
        QTimer::singleShot(0, this, [this, id, name]() {
            UpdateCategoryName(id, name);
            Reload();
        });
    });
    connect(categories_list_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& position) {
                auto* item = categories_list_->itemAt(position);
                if (!item || !deps_.session_key) return;
                const auto id = item->data(Qt::UserRole).toLongLong();
                QMenu menu(this);
                auto* rename = menu.addAction(QStringLiteral("重命名"));
                rename->setObjectName(QStringLiteral("RenameCategoryAction"));
                auto* remove = menu.addAction(QStringLiteral("删除分类"));
                remove->setObjectName(QStringLiteral("DeleteCategoryAction"));
                auto* chosen = menu.exec(
                    categories_list_->viewport()->mapToGlobal(position));
                if (chosen == rename) OnRenameCategory(id);
                if (chosen == remove) OnDeleteCategory(id);
            });
    connect(categories_list_->model(), &QAbstractItemModel::rowsMoved, this,
            &MainWindow::PersistCategoryOrder, Qt::QueuedConnection);
    categories_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    categories_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    categories_list_->setItemDelegate(
        new SidebarItemDelegate(categories_list_));
    connect(categories_list_, &QListWidget::itemSelectionChanged, this,
            &MainWindow::OnCategorySelectionChanged);
    layout->addWidget(categories_list_, 1);

    auto* footer = new QWidget(sidebar);
    footer->setObjectName(QStringLiteral("SidebarFooter"));
    auto* footer_layout = new QVBoxLayout(footer);
    footer_layout->setContentsMargins(0, 12, 0, 0);
    footer_layout->setSpacing(4);

    sync_button_ = new QPushButton(footer);
    sync_button_->setObjectName(QStringLiteral("SidebarSyncButton"));
    sync_button_->setAccessibleName(QStringLiteral("立即同步"));
    sync_button_->setToolTip(QStringLiteral("立即同步"));
    sync_button_->setFocusPolicy(Qt::TabFocus);
    sync_button_->setFixedHeight(32);
    sync_button_->setCursor(Qt::PointingHandCursor);
    sync_button_->setIconSize(QSize(kIconSize, kIconSize));
    sync_button_->setProperty("state", QStringLiteral("idle"));
    RefreshSyncButton();
    connect(sync_button_, &QPushButton::clicked, this,
            &MainWindow::OnSyncClicked);
    footer_layout->addWidget(sync_button_);

    auto* settings_button = new QPushButton(QStringLiteral("设置"), footer);
    settings_button->setObjectName(QStringLiteral("SidebarSettingsButton"));
    settings_button->setAccessibleName(QStringLiteral("设置"));
    settings_button->setToolTip(QStringLiteral("设置"));
    settings_button->setFocusPolicy(Qt::TabFocus);
    settings_button->setFixedHeight(32);
    settings_button->setCursor(Qt::PointingHandCursor);
    settings_button->setIcon(IconLoader::Load(
        QStringLiteral("settings-2"),
        ThemeManager::Instance()->Color(QStringLiteral("text-secondary")),
        kIconSize));
    settings_button->setIconSize(QSize(kIconSize, kIconSize));
    connect(settings_button, &QPushButton::clicked, this, [this]() {
        central_stack_->setCurrentWidget(preferences_page_);
    });
    footer_layout->addWidget(settings_button);

    layout->addWidget(footer);
    return sidebar;
}

QWidget* MainWindow::BuildWorkspaceHeader() {
    auto* header = new QWidget();
    header->setObjectName(QStringLiteral("VaultWorkspaceHeader"));
    header->setFixedHeight(kWorkspaceHeaderHeight);
    auto* header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(0);

    auto* controls = new QWidget(header);
    controls->setObjectName(QStringLiteral("VaultWorkspaceHeaderControls"));
    auto* controls_layout = new QHBoxLayout(controls);
    controls_layout->setContentsMargins(24, 0, 24, 0);
    controls_layout->setSpacing(10);

    navigation_button_ = new QToolButton(controls);
    navigation_button_->setObjectName(
        QStringLiteral("HeaderNavigationButton"));
    navigation_button_->setToolTip(QStringLiteral("展开导航"));
    navigation_button_->setCursor(Qt::PointingHandCursor);
    navigation_button_->setFixedSize(32, 32);
    navigation_button_->setIcon(IconLoader::Load(
        QStringLiteral("list"),
        ThemeManager::Instance()->Color(QStringLiteral("text-secondary")),
        kIconSize));
    navigation_button_->setIconSize(QSize(kIconSize, kIconSize));
    connect(navigation_button_, &QToolButton::clicked, this,
            &MainWindow::ToggleCompactSidebar);
    controls_layout->addWidget(navigation_button_, 0, Qt::AlignVCenter);

    back_to_list_button_ = new QToolButton(controls);
    back_to_list_button_->setObjectName(
        QStringLiteral("HeaderBackToListButton"));
    back_to_list_button_->setToolTip(QStringLiteral("返回密码列表"));
    back_to_list_button_->setCursor(Qt::PointingHandCursor);
    back_to_list_button_->setFixedSize(32, 32);
    back_to_list_button_->setIcon(IconLoader::Load(
        QStringLiteral("arrow-left"),
        ThemeManager::Instance()->Color(QStringLiteral("text-secondary")),
        kIconSize));
    back_to_list_button_->setIconSize(QSize(kIconSize, kIconSize));
    connect(back_to_list_button_, &QToolButton::clicked, this,
            &MainWindow::ShowListPane);
    controls_layout->addWidget(back_to_list_button_, 0, Qt::AlignVCenter);

    search_container_ = new QFrame(controls);
    search_container_->setObjectName(QStringLiteral("SearchBarContainer"));
    search_container_->setProperty("focused", false);
    search_container_->setFixedHeight(kSearchHeight);
    search_container_->setMinimumWidth(kSearchMinWidth);
    search_container_->setMaximumWidth(kSearchMaxWidth);
    search_container_->setSizePolicy(QSizePolicy::Expanding,
                                     QSizePolicy::Fixed);
    auto* search_layout = new QHBoxLayout(search_container_);
    search_layout->setContentsMargins(0, 0, 8, 0);
    search_layout->setSpacing(6);

    search_ = new QLineEdit(search_container_);
    search_->setObjectName(QStringLiteral("SearchBar"));
    search_->installEventFilter(this);
    search_->setPlaceholderText(QStringLiteral("搜索标题、用户名、网址或备注"));
    search_->setClearButtonEnabled(true);
    search_action_ = search_->addAction(
        IconLoader::Load(
            QStringLiteral("search"),
            ThemeManager::Instance()->Color(QStringLiteral("text-tertiary")),
            16),
        QLineEdit::LeadingPosition);
    connect(search_, &QLineEdit::textChanged, this,
            &MainWindow::OnSearchChanged);
    connect(qApp, &QApplication::focusChanged, search_container_,
            [this](QWidget*, QWidget* focused_widget) {
                const bool focused =
                    focused_widget == search_ ||
                    (focused_widget != nullptr &&
                     search_->isAncestorOf(focused_widget));
                if (search_container_->property("focused").toBool() == focused) {
                    return;
                }
                search_container_->setProperty("focused", focused);
                search_container_->style()->unpolish(search_container_);
                search_container_->style()->polish(search_container_);
                search_container_->update();
            });
    search_layout->addWidget(search_, 1);

    auto* shortcut = new QLabel(QStringLiteral("Ctrl + F"), search_container_);
    shortcut->setObjectName(QStringLiteral("SearchShortcut"));
    shortcut->setAlignment(Qt::AlignCenter);
    search_layout->addWidget(shortcut);
    controls_layout->addWidget(search_container_, 1, Qt::AlignVCenter);
    controls_layout->addStretch(1);

    auto* lock_button = new QToolButton(controls);
    lock_button->setObjectName(QStringLiteral("HeaderLockButton"));
    lock_button->setToolTip(QStringLiteral("锁定保险库"));
    lock_button->setCursor(Qt::PointingHandCursor);
    lock_button->setFixedSize(32, 32);
    lock_button->setIcon(IconLoader::Load(
        QStringLiteral("lock-keyhole"),
        ThemeManager::Instance()->Color(QStringLiteral("text-secondary")),
        kIconSize));
    lock_button->setIconSize(QSize(kIconSize, kIconSize));
    connect(lock_button, &QToolButton::clicked, this,
            &MainWindow::LockRequested);
    controls_layout->addWidget(lock_button, 0, Qt::AlignVCenter);

    auto* sep = new QFrame(controls);
    sep->setObjectName(QStringLiteral("HeaderSeparator"));
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedSize(1, 20);
    controls_layout->addWidget(sep, 0, Qt::AlignVCenter);

    more_button_ = new QToolButton(controls);
    more_button_->setObjectName(QStringLiteral("HeaderMoreButton"));
    more_button_->setToolTip(QStringLiteral("更多操作暂不可用"));
    more_button_->setFixedSize(32, 32);
    more_button_->setIcon(IconLoader::Load(
        QStringLiteral("more-horizontal"),
        ThemeManager::Instance()->Color(QStringLiteral("text-tertiary")),
        kIconSize));
    more_button_->setIconSize(QSize(kIconSize, kIconSize));
    more_button_->setEnabled(false);
    controls_layout->addWidget(more_button_, 0, Qt::AlignVCenter);

    header_layout->addWidget(controls, 1);

    auto* divider = new QFrame(header);
    divider->setObjectName(QStringLiteral("VaultWorkspaceHeaderDivider"));
    divider->setFixedHeight(1);
    header_layout->addWidget(divider);

    return header;
}

QWidget* MainWindow::BuildPasswordListColumn() {
    auto* column = new QFrame();
    column->setObjectName(QStringLiteral("PasswordListColumn"));
    column->installEventFilter(this);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* title_row = new QWidget(column);
    title_row->setObjectName(QStringLiteral("ListTitleRow"));
    title_row->installEventFilter(this);
    auto* title_layout = new QHBoxLayout(title_row);
    title_layout->setContentsMargins(24, 20, 24, 8);
    title_layout->setSpacing(6);

    list_title_ = new QLabel(QStringLiteral("所有密码"), title_row);
    list_title_->setObjectName(QStringLiteral("ListTitle"));
    title_layout->addWidget(list_title_);
    list_count_ = new QLabel(QStringLiteral("0 项"), title_row);
    list_count_->setObjectName(QStringLiteral("ListCount"));
    title_layout->addWidget(list_count_);
    title_layout->addStretch(1);

    sort_button_ = new QPushButton(QStringLiteral("按更新时间 · 降序"), title_row);
    sort_button_->setObjectName(QStringLiteral("ListSortMenu"));
    sort_button_->setProperty("flat", true);
    sort_button_->setToolTip(QStringLiteral("选择密码排序方式"));
    sort_button_->setLayoutDirection(Qt::RightToLeft);
    sort_button_->setIcon(IconLoader::Load(
        QStringLiteral("chevron-down"),
        ThemeManager::Instance()->Color(QStringLiteral("text-tertiary")),
        12));
    sort_button_->setIconSize(QSize(12, 12));
    auto* sort_menu = new QMenu(sort_button_);
    auto* sort_group = new QActionGroup(sort_menu);
    const std::pair<QString, SortOrder> orders[] = {
        {QStringLiteral("按更新时间"), SortOrder::kUpdated},
        {QStringLiteral("按创建时间"), SortOrder::kCreated},
        {QStringLiteral("按标题"), SortOrder::kTitle},
    };
    const auto update_sort = [this]() {
        const QString label = sort_order_ == SortOrder::kUpdated
            ? QStringLiteral("按更新时间")
            : sort_order_ == SortOrder::kCreated ? QStringLiteral("按创建时间")
                                                : QStringLiteral("按标题");
        sort_button_->setText(QStringLiteral("%1 · %2").arg(
            label, sort_ascending_ ? QStringLiteral("升序")
                                   : QStringLiteral("降序")));
        RefreshPasswordList();
    };
    for (const auto& order : orders) {
        auto* action = sort_menu->addAction(order.first);
        action->setCheckable(true);
        action->setChecked(order.second == sort_order_);
        sort_group->addAction(action);
        connect(action, &QAction::triggered, this, [this, order, update_sort]() {
            sort_order_ = order.second;
            update_sort();
        });
    }
    sort_menu->addSeparator();
    auto* direction_group = new QActionGroup(sort_menu);
    for (bool ascending : {true, false}) {
        auto* action = sort_menu->addAction(
            ascending ? QStringLiteral("升序") : QStringLiteral("降序"));
        action->setObjectName(ascending ? QStringLiteral("SortAscendingAction")
                                       : QStringLiteral("SortDescendingAction"));
        action->setCheckable(true);
        action->setChecked(ascending == sort_ascending_);
        direction_group->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, ascending, update_sort]() {
                    sort_ascending_ = ascending;
                    update_sort();
                });
    }
    sort_button_->setMenu(sort_menu);
    title_layout->addWidget(sort_button_);
    restore_selected_button_ = new QPushButton(QStringLiteral("还原"), title_row);
    restore_selected_button_->setObjectName(
        QStringLiteral("RestoreSelectedButton"));
    permanently_delete_button_ = new QPushButton(
        QStringLiteral("永久删除"), title_row);
    permanently_delete_button_->setObjectName(
        QStringLiteral("PermanentlyDeleteButton"));
    empty_trash_button_ = new QPushButton(QStringLiteral("清空"), title_row);
    empty_trash_button_->setObjectName(QStringLiteral("EmptyTrashButton"));
    for (auto* button : {restore_selected_button_, permanently_delete_button_,
                         empty_trash_button_}) {
        button->setProperty("flat", true);
        title_layout->addWidget(button);
    }
    connect(restore_selected_button_, &QPushButton::clicked, this,
            &MainWindow::OnRestoreSelected);
    connect(permanently_delete_button_, &QPushButton::clicked, this,
            &MainWindow::OnDeleteSelected);
    connect(empty_trash_button_, &QPushButton::clicked, this,
            &MainWindow::OnEmptyTrash);

    layout->addWidget(title_row);

    list_content_stack_ = new QStackedWidget(column);
    list_content_stack_->setObjectName(QStringLiteral("ListContentStack"));
    layout->addWidget(list_content_stack_, 1);
    password_list_ = new PasswordList(list_content_stack_);
    password_list_->setObjectName(QStringLiteral("PasswordList"));
    password_list_->setFrameShape(QFrame::NoFrame);
    password_list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    password_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(password_list_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                auto* item = password_list_->itemAt(pos);
                if (!item || !(item->flags() & Qt::ItemIsSelectable)) return;
                if (!item->isSelected()) {
                    QSignalBlocker blocker(password_list_);
                    password_list_->setCurrentItem(item);
                }
                UpdateTrashActions();
                QMenu menu(this);
                menu.setObjectName(QStringLiteral("PasswordContextMenu"));
                if (selected_category_ == kSectionTrash) {
                    auto* restore = menu.addAction(QStringLiteral("还原"));
                    connect(restore, &QAction::triggered, this,
                            &MainWindow::OnRestoreSelected);
                }
                if (selected_category_ != kSectionTrash) {
                    const auto ids = SelectedPasswordIds();
                    const auto id = item->data(Qt::UserRole).toLongLong();
                    auto* edit = menu.addAction(QStringLiteral("编辑"));
                    edit->setEnabled(ids.size() == 1);
                    connect(edit, &QAction::triggered, this,
                            [this, id]() { OnEditRequested(id); });
                    PopulateCategoryMenu(menu.addMenu(QStringLiteral("分类")), ids);
                    const auto entry = deps_.password_dao->FindById(id);
                    if (entry && !entry->is_favorite) {
                        auto* favorite = menu.addAction(QStringLiteral("收藏"));
                        connect(favorite, &QAction::triggered, this, [this, ids]() {
                            for (const auto selected_id : ids) {
                                OnFavoriteToggleRequested(selected_id, true);
                            }
                        });
                    }
                }
                auto* remove = menu.addAction(selected_category_ == kSectionTrash
                    ? QStringLiteral("永久删除") : QStringLiteral("移入回收站"));
                connect(remove, &QAction::triggered, this,
                        &MainWindow::OnDeleteSelected);
                menu.exec(password_list_->viewport()->mapToGlobal(pos));
            });
    connect(password_list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
                if (selected_category_ != kSectionTrash && item &&
                    (item->flags() & Qt::ItemIsSelectable)) {
                    OnEditRequested(item->data(Qt::UserRole).toLongLong());
                }
            });
    password_list_->installEventFilter(this);
    password_list_->setSpacing(6);
    password_list_->setUniformItemSizes(true);
    password_list_->setItemDelegate(new PasswordItemDelegate(password_list_));
    password_list_->viewport()->installEventFilter(this);
    connect(password_list_, &QListWidget::itemSelectionChanged, this,
            &MainWindow::OnPasswordSelectionChanged);
    connect(password_list_, &QListWidget::itemClicked, this,
            &MainWindow::OnPasswordSelectionChanged);
    list_content_stack_->addWidget(password_list_);

    empty_state_ = new QLabel(QStringLiteral("暂无密码"), list_content_stack_);
    empty_state_->setObjectName(QStringLiteral("EmptyState"));
    empty_state_->setAlignment(Qt::AlignCenter);
    empty_state_->installEventFilter(this);
    list_content_stack_->addWidget(empty_state_);

    return column;
}

QWidget* MainWindow::BuildDetailColumn() {
    detail_panel_ = new DetailPanel();
    connect(detail_panel_, &DetailPanel::CloseRequested, this,
            &MainWindow::ShowListPane);
    auto* close_detail = new QAction(detail_panel_);
    close_detail->setShortcut(QKeySequence(Qt::Key_Escape));
    close_detail->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    detail_panel_->addAction(close_detail);
    connect(close_detail, &QAction::triggered, this, &MainWindow::ShowListPane);
    connect(detail_panel_, &DetailPanel::EditRequested, this,
            &MainWindow::OnEditRequested);
    connect(detail_panel_, &DetailPanel::CategoryChangeRequested, this,
            &MainWindow::OnCategoryChangeRequested);
    connect(detail_panel_, &DetailPanel::RestoreRequested, this,
            [this](std::int64_t id) { RestoreEntries({id}); });
    connect(detail_panel_, &DetailPanel::DeleteRequested, this,
            &MainWindow::OnDeleteRequested);
    connect(detail_panel_, &DetailPanel::CopyPasswordRequested, this,
            &MainWindow::OnCopyPasswordRequested);
    connect(detail_panel_, &DetailPanel::CopyUsernameRequested, this,
            &MainWindow::OnCopyUsernameRequested);
    connect(detail_panel_, &DetailPanel::OpenWebsiteRequested, this,
            &MainWindow::OnOpenWebsiteRequested);
    connect(detail_panel_, &DetailPanel::FavoriteToggleRequested, this,
            &MainWindow::OnFavoriteToggleRequested);
    return detail_panel_;
}

void MainWindow::Reload() {
    if (!deps_.category_dao) return;
    categories_ = deps_.category_dao->ListActive();
    QList<model::Category> cat_list;
    for (const auto& c : categories_) cat_list.append(c);
    if (detail_panel_) detail_panel_->SetCategories(cat_list);
    RefreshSectionList();
    RefreshCategoryList();
    RefreshPasswordList();
}

void MainWindow::RefreshThemeAssets() {
    auto* theme = ThemeManager::Instance();
    const auto icon = [theme](const QString& name, const QString& token,
                              int size) {
        return IconLoader::Load(name, theme->Color(token), size);
    };

    if (auto* button = findChild<QPushButton*>(
            QStringLiteral("NewPasswordButton"))) {
        button->setIcon(
            icon(QStringLiteral("plus"), QStringLiteral("accent-fg"), 17));
    }
    if (auto* button = findChild<QToolButton*>(
            QStringLiteral("SidebarAddCategoryButton"))) {
        button->setIcon(icon(QStringLiteral("plus"),
                             QStringLiteral("text-tertiary"), 14));
    }
    RefreshSyncButton();
    if (auto* settings_button = findChild<QPushButton*>(
            QStringLiteral("SidebarSettingsButton"))) {
        settings_button->setIcon(
            icon(QStringLiteral("settings-2"),
                 QStringLiteral("text-secondary"), kIconSize));
    }
    if (search_action_) {
        search_action_->setIcon(icon(QStringLiteral("search"),
                                     QStringLiteral("text-tertiary"), 16));
    }
    if (navigation_button_) {
        navigation_button_->setIcon(icon(QStringLiteral("list"),
                                          QStringLiteral("text-secondary"),
                                          kIconSize));
    }
    if (back_to_list_button_) {
        back_to_list_button_->setIcon(icon(QStringLiteral("arrow-left"),
                                           QStringLiteral("text-secondary"),
                                           kIconSize));
    }
    if (auto* button = findChild<QToolButton*>(
            QStringLiteral("HeaderLockButton"))) {
        button->setIcon(
            icon(QStringLiteral("lock-keyhole"),
                 QStringLiteral("text-secondary"), kIconSize));
    }
    if (auto* button = findChild<QToolButton*>(
            QStringLiteral("HeaderMoreButton"))) {
        button->setIcon(icon(QStringLiteral("more-horizontal"),
                             QStringLiteral("text-tertiary"), kIconSize));
    }
    if (auto* button = findChild<QPushButton*>(
            QStringLiteral("ListSortMenu"))) {
        button->setIcon(icon(QStringLiteral("chevron-down"),
                             QStringLiteral("text-tertiary"), 12));
    }

    if (sections_list_) sections_list_->viewport()->update();
    if (categories_list_) categories_list_->viewport()->update();
    if (password_list_) {
        for (int i = 0; i < password_list_->count(); ++i) {
            auto* item = password_list_->item(i);
            auto* card = password_list_->itemWidget(item);
            const auto entry = FindEntry(
                item->data(Qt::UserRole).toLongLong());
            if (card && entry && !entry->is_deleted) {
                for (const auto& action : {
                         qMakePair("PasswordCardEditButton", "pencil"),
                         qMakePair("PasswordCardCategoryButton", "folder"),
                         qMakePair("PasswordCardDeleteButton", "trash-2")}) {
                    if (auto* button = card->findChild<QToolButton*>(
                            QString::fromLatin1(action.first))) {
                        button->setIcon(icon(QString::fromLatin1(action.second),
                            QString::fromLatin1(action.second) == QStringLiteral("trash-2")
                                ? QStringLiteral("danger") : QStringLiteral("text-secondary"), 16));
                    }
                }
                SetCardFavorite(card, entry->is_favorite);
            }

        }
    }
}

void MainWindow::RefreshSectionList() {
    if (!sections_list_) return;
    suppress_selection_ = true;
    sections_list_->clear();

    struct SectionDef {
        std::int64_t id;
        QString label;
        QString icon;
    };
    const SectionDef defs[] = {
        {kSectionAll, QStringLiteral("全部密码"), QStringLiteral("list")},
        {kSectionFavorites, QStringLiteral("收藏夹"), QStringLiteral("star")},
        {kSectionUncategorized, QStringLiteral("未分类"),
         QStringLiteral("folder")},
        {kSectionTrash, QStringLiteral("回收站"), QStringLiteral("trash-2")},
    };
    if (deps_.password_dao) {
        const auto all = deps_.password_dao->ListActive();
        int match_count = 0;
        int fav_count = 0;
        int uncat_count = 0;
        for (const auto& e : all) {
            if (!MatchesSearch(e)) continue;
            ++match_count;
            if (e.is_favorite) ++fav_count;
            if (e.category_id == 0) ++uncat_count;
        }
        int trash_count = 0;
        for (const auto& e : deps_.password_dao->ListIncludingDeleted()) {
            if (e.is_deleted && MatchesSearch(e)) ++trash_count;
        }
        const int counts[] = {
            match_count,
            fav_count,
            uncat_count,
            trash_count,
        };
        for (int i = 0; i < 4; ++i) {
            auto* item = new QListWidgetItem(sections_list_);
            item->setText(defs[i].label);
            item->setData(Qt::UserRole,
                          QVariant::fromValue<qint64>(defs[i].id));
            item->setData(kSidebarCountRole, counts[i]);
            item->setData(kShowZeroCountRole, !search_text_.isEmpty());
            item->setData(kSidebarSystemRole, true);
            item->setData(kSidebarIconRole, defs[i].icon);
            item->setData(Qt::AccessibleDescriptionRole,
                          QStringLiteral("%1 项").arg(counts[i]));
        }
    } else {
        for (const auto& d : defs) {
            auto* item = new QListWidgetItem(sections_list_);
            item->setText(d.label);
            item->setData(Qt::UserRole, QVariant::fromValue<qint64>(d.id));
            item->setData(kSidebarSystemRole, true);
            item->setData(kSidebarIconRole, d.icon);
        }
    }

    for (int i = 0; i < sections_list_->count(); ++i) {
        auto* it = sections_list_->item(i);
        if (it->data(Qt::UserRole).toLongLong() == selected_category_) {
            sections_list_->setCurrentItem(it);
            break;
        }
    }
    if (!sections_list_->currentItem() && categories_list_ &&
        !categories_list_->currentItem()) {
        sections_list_->setCurrentRow(0);
        selected_category_ = kSectionAll;
    }
    suppress_selection_ = false;
}

void MainWindow::RefreshCategoryList() {
    if (!categories_list_) return;
    suppress_selection_ = true;
    categories_list_->clear();
    QHash<std::int64_t, int> counts;
    if (deps_.password_dao) {
        for (const auto& entry : deps_.password_dao->ListActive()) {
            if (MatchesSearch(entry)) ++counts[entry.category_id];
        }
    }
    for (const auto& c : categories_) {
        if (c.is_deleted) continue;
        auto* item = new QListWidgetItem(categories_list_);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setText(c.name);
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
        item->setData(Qt::UserRole, QVariant::fromValue<qint64>(c.id));
        item->setData(kSidebarCountRole, counts.value(c.id));
        item->setData(kShowZeroCountRole, !search_text_.isEmpty());
        item->setData(kSidebarSystemRole, false);
        item->setData(kSidebarIconRole, QStringLiteral("folder"));
        item->setData(Qt::AccessibleDescriptionRole,
                      QStringLiteral("%1 项").arg(counts.value(c.id)));
        if (selected_category_ == c.id) {
            categories_list_->setCurrentItem(item);
        }
    }
    suppress_selection_ = false;
}

void MainWindow::OnSectionSelectionChanged() {
    if (suppress_selection_) return;
    // UI Automation selection can update the selection model without current.
    const auto selected = sections_list_->selectedItems();
    auto* item =
        selected.isEmpty() ? sections_list_->currentItem() : selected.first();
    if (!item) return;
    const auto category = item->data(Qt::UserRole).toLongLong();
    if (category == selected_category_) return;
    selected_category_ = category;
    detail_panel_->ClearEntry();
    suppress_selection_ = true;
    if (categories_list_) categories_list_->clearSelection();
    suppress_selection_ = false;
    RefreshPasswordList();
    showing_detail_ = false;
    if (width() < kSidebarBreakpoint) compact_sidebar_expanded_ = false;
    UpdateResponsiveLayout();
}

void MainWindow::OnCategorySelectionChanged() {
    if (suppress_selection_) return;
    const auto selected = categories_list_->selectedItems();
    auto* item =
        selected.isEmpty() ? categories_list_->currentItem() : selected.first();
    if (!item) return;
    const auto category = item->data(Qt::UserRole).toLongLong();
    if (category == selected_category_) return;
    selected_category_ = category;
    detail_panel_->ClearEntry();
    suppress_selection_ = true;
    if (sections_list_) sections_list_->clearSelection();
    suppress_selection_ = false;
    RefreshPasswordList();
    showing_detail_ = false;
    if (width() < kSidebarBreakpoint) compact_sidebar_expanded_ = false;
    UpdateResponsiveLayout();
}

bool MainWindow::MatchesSearch(const model::PasswordEntry& entry) const {
    return search_text_.isEmpty() ||
           entry.title.contains(search_text_, Qt::CaseInsensitive) ||
           entry.username.contains(search_text_, Qt::CaseInsensitive) ||
           entry.website.contains(search_text_, Qt::CaseInsensitive) ||
           entry.notes.contains(search_text_, Qt::CaseInsensitive);
}

QString MainWindow::CategoryName(std::int64_t category_id) const {
    for (const auto& category : categories_) {
        if (category.id == category_id) return category.name;
    }
    return QStringLiteral("未分类");
}

void MainWindow::PopulateCategoryMenu(
    QMenu* menu, const std::vector<std::int64_t>& ids) {
    menu->setObjectName(QStringLiteral("ChangeCategoryMenu"));
    const auto add = [this, menu, &ids](std::int64_t id, const QString& name) {
        auto* action = menu->addAction(name);
        action->setData(QVariant::fromValue<qint64>(id));
        action->setEnabled(!migration_active_ && !reset_confirmation_active_);
        connect(action, &QAction::triggered, this,
                [this, ids, id]() { ChangeCategories(ids, id); });
    };
    add(0, QStringLiteral("未分类"));
    for (const auto& category : categories_) {
        if (!category.is_deleted) add(category.id, category.name);
    }
}

void MainWindow::OnCategoryChangeRequested(std::int64_t entry_id) {
    if (migration_active_ || reset_confirmation_active_) return;
    QMenu menu(this);
    PopulateCategoryMenu(&menu, {entry_id});
    auto* button = detail_panel_->findChild<QToolButton*>(
        QStringLiteral("DetailCategoryButton"));
    menu.exec(button->mapToGlobal(QPoint(0, button->height())));
}

void MainWindow::ChangeCategories(const std::vector<std::int64_t>& ids,
                                  std::int64_t category_id) {
    if (migration_active_ || reset_confirmation_active_ || !deps_.session_key ||
        !deps_.password_dao || !deps_.category_dao) return;
    if (category_id != 0) {
        const auto target = deps_.category_dao->FindById(category_id);
        if (!target || target->is_deleted) return;
    }
    bool changed = false;
    try {
        storage::Transaction transaction(deps_.password_dao->database());
        for (const auto id : ids) {
            const auto entry = deps_.password_dao->FindById(id);
            if (!entry || entry->is_deleted) return;
            if (entry->category_id == category_id) continue;
            if (entry->updated_at == std::numeric_limits<std::int64_t>::max()) return;
            const auto timestamp = std::max(entry->updated_at + 1,
                                            QDateTime::currentMSecsSinceEpoch());
            if (!deps_.password_dao->UpdateCategoryOf(id, category_id, timestamp)) {
                Toast::Show(this, QStringLiteral("更改分类失败"), Toast::Level::kError);
                return;
            }
            changed = true;
        }
        if (!transaction.Commit()) return;
    } catch (const std::exception&) {
        Toast::Show(this, QStringLiteral("更改分类失败"), Toast::Level::kError);
        return;
    }
    if (!changed) return;
    if (deps_.sync_scheduler) deps_.sync_scheduler->MarkDirty();
    Reload();
    if (detail_panel_->HasEntry() && !FindEntry(detail_panel_->entry_id())) {
        detail_panel_->ClearEntry();
        showing_detail_ = false;
        UpdateResponsiveLayout();
    }
}

void MainWindow::OnSearchChanged(const QString& text) {
    search_text_ = text.trimmed();
    RefreshSectionList();
    RefreshCategoryList();
    RefreshPasswordList();
    showing_detail_ = false;
    UpdateResponsiveLayout();
}

QWidget* MainWindow::CreatePasswordCard(const model::PasswordEntry& entry) {
    auto* card = new PasswordCard();
    card->setObjectName(QStringLiteral("PasswordCard"));
    card->setFrameShape(QFrame::NoFrame);
    card->setProperty("trash", entry.is_deleted);
    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(16, 10, 16, 10);
    row->setSpacing(12);

    auto* avatar = new QLabel(AvatarInitial(entry), card);
    avatar->setObjectName(QStringLiteral("PasswordCardIcon"));
    avatar->setProperty(
        "colorIndex",
        static_cast<unsigned int>(entry.icon_color) % 6);
    avatar->setFixedSize(32, 32);
    avatar->setAlignment(Qt::AlignCenter);
    row->addWidget(avatar);

    auto* text_col = new QVBoxLayout();
    text_col->setContentsMargins(0, 0, 0, 0);
    text_col->setSpacing(2);
    auto* title = new ElidedLabel(
        entry.title.isEmpty() ? QStringLiteral("(无标题)") : entry.title, card);
    title->setObjectName(QStringLiteral("PasswordCardTitle"));
    title->setTextInteractionFlags(Qt::NoTextInteraction);
    text_col->addWidget(title);
    auto* meta = new ElidedLabel(entry.username, card);
    meta->setObjectName(QStringLiteral("PasswordCardMeta"));
    text_col->addWidget(meta);
    row->addLayout(text_col, 1);

    auto* right_col = new QHBoxLayout();
    right_col->setObjectName(QStringLiteral("PasswordCardMetaLayout"));
    right_col->setContentsMargins(0, 0, 0, 0);
    right_col->setSpacing(4);
    right_col->setAlignment(Qt::AlignVCenter);
    auto* time = new QLabel(RelativeTimeShort(entry.ContentUpdatedAt()), card);
    time->setObjectName(QStringLiteral("PasswordCardTime"));
    time->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    time->setFixedWidth(80);
    time->setVisible(!entry.is_deleted);
    const QString full_time =
        QDateTime::fromMSecsSinceEpoch(entry.ContentUpdatedAt())
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    time->setAccessibleName(full_time);
    card->setAccessibleDescription(full_time);
    right_col->addWidget(time);

    if (!entry.is_deleted) {
        const auto add_action = [this, card, right_col](const QString& name,
                                                       const QString& tooltip,
                                                       const QString& icon) {
            auto* button = new QToolButton(card);
            button->setObjectName(name);
            button->setProperty("passwordCardAction", true);
            button->setToolTip(tooltip);
            button->setAccessibleName(tooltip);
            button->setFixedSize(28, 28);
            button->setCursor(Qt::PointingHandCursor);
            button->setIcon(IconLoader::Load(icon, ThemeManager::Instance()->Color(
                icon == QStringLiteral("trash-2") ? QStringLiteral("danger")
                                                 : QStringLiteral("text-secondary")), 16));
            button->setIconSize(QSize(16, 16));
            right_col->addWidget(button);
            return button;
        };
        auto* edit = add_action(QStringLiteral("PasswordCardEditButton"),
                                QStringLiteral("编辑"), QStringLiteral("pencil"));
        connect(edit, &QToolButton::clicked, this,
                [this, id = entry.id]() { OnEditRequested(id); });
        auto* category = add_action(QStringLiteral("PasswordCardCategoryButton"),
                                    QStringLiteral("分类"), QStringLiteral("folder"));
        connect(category, &QToolButton::clicked, this, [this, category, id = entry.id]() {
            QMenu menu(this);
            PopulateCategoryMenu(&menu, {id});
            menu.exec(category->mapToGlobal(QPoint(0, category->height())));
        });
        auto* favorite = add_action(QStringLiteral("PasswordCardFavoriteButton"),
                                    QStringLiteral("收藏"), QStringLiteral("star"));
        connect(favorite, &QToolButton::clicked, this, [this, id = entry.id]() {
            const auto current = deps_.password_dao->FindById(id);
            if (current) OnFavoriteToggleRequested(id, !current->is_favorite);
        });
        auto* remove = add_action(QStringLiteral("PasswordCardDeleteButton"),
                                  QStringLiteral("删除"), QStringLiteral("trash-2"));
        connect(remove, &QToolButton::clicked, this,
                [this, id = entry.id]() { OnDeleteRequested(id); });
    }
    if (entry.is_deleted) {
        auto* restore = new QToolButton(card);
        restore->setToolTip(QStringLiteral("还原"));
        restore->setIcon(IconLoader::Load(
            QStringLiteral("rotate-ccw"),
            ThemeManager::Instance()->Color(QStringLiteral("text-secondary")), 16));
        restore->setIconSize(QSize(16, 16));
        restore->setObjectName(QStringLiteral("PasswordCardRestoreButton"));
        restore->setAccessibleName(QStringLiteral("还原"));
        restore->setCursor(Qt::PointingHandCursor);
        restore->setFixedSize(32, 32);
        connect(restore, &QToolButton::clicked, this,
                [this, id = entry.id]() { RestoreEntries({id}); });
        right_col->addWidget(restore);

        auto* remove = new QToolButton(card);
        remove->setObjectName(QStringLiteral("PasswordCardDeleteButton"));
        remove->setToolTip(QStringLiteral("永久删除"));
        remove->setAccessibleName(QStringLiteral("永久删除"));
        remove->setFixedSize(32, 32);
        remove->setCursor(Qt::PointingHandCursor);
        remove->setIcon(IconLoader::Load(
            QStringLiteral("trash-2"),
            ThemeManager::Instance()->Color(QStringLiteral("danger")), 16));
        remove->setIconSize(QSize(16, 16));
        connect(remove, &QToolButton::clicked, this,
                [this, id = entry.id]() { OnDeleteRequested(id); });
        right_col->addWidget(remove);
    }
    row->addLayout(right_col);
    if (!entry.is_deleted) SetCardFavorite(card, entry.is_favorite);
    for (auto* label : card->findChildren<QLabel*>()) {
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    card->setMinimumHeight(kCardHeight);
    return card;
}

void MainWindow::RefreshPasswordList() {
    if (!deps_.password_dao || !password_list_) return;
    suppress_selection_ = true;
    password_list_->clear();

    std::vector<model::PasswordEntry> rows;
    if (selected_category_ == kSectionTrash) {
        for (auto& entry : deps_.password_dao->ListIncludingDeleted()) {
            if (entry.is_deleted && (search_text_.isEmpty() ||
                entry.title.contains(search_text_, Qt::CaseInsensitive) ||
                entry.username.contains(search_text_, Qt::CaseInsensitive) ||
                entry.website.contains(search_text_, Qt::CaseInsensitive) ||
                entry.notes.contains(search_text_, Qt::CaseInsensitive))) {
                rows.push_back(std::move(entry));
            }
        }
    } else if (selected_category_ == kSectionAll) {
        rows = deps_.password_dao->ListActive();
    } else if (selected_category_ == kSectionFavorites) {
        for (auto& e : deps_.password_dao->ListActive()) {
            if (e.is_favorite) rows.push_back(std::move(e));
        }
    } else if (selected_category_ == kSectionUncategorized) {
        rows = deps_.password_dao->ListByCategory(0);
    } else {
        rows = deps_.password_dao->ListByCategory(selected_category_);
    }

    rows.erase(std::remove_if(rows.begin(), rows.end(), [this](const auto& entry) {
        return !MatchesSearch(entry);
    }), rows.end());

    std::stable_sort(rows.begin(), rows.end(),
                     [this](const model::PasswordEntry& lhs,
                        const model::PasswordEntry& rhs) {
                         if (sort_order_ == SortOrder::kTitle) {
                             const int comparison = QString::localeAwareCompare(
                                 lhs.title, rhs.title);
                             return sort_ascending_ ? comparison < 0
                                                    : comparison > 0;
                         }
                         const auto left = sort_order_ == SortOrder::kCreated
                             ? lhs.created_at : lhs.ContentUpdatedAt();
                         const auto right = sort_order_ == SortOrder::kCreated
                             ? rhs.created_at : rhs.ContentUpdatedAt();
                         return sort_ascending_ ? left < right : left > right;
                     });

    if (!search_text_.isEmpty()) {
        std::stable_sort(rows.begin(), rows.end(), [this](const auto& lhs,
                                                         const auto& rhs) {
            const int comparison = QString::localeAwareCompare(
                CategoryName(lhs.category_id), CategoryName(rhs.category_id));
            return comparison == 0 ? lhs.category_id < rhs.category_id
                                   : comparison < 0;
        });
    }
    current_entries_ = std::move(rows);

    password_list_->setUniformItemSizes(search_text_.isEmpty());
    std::optional<std::int64_t> group;
    for (const auto& e : current_entries_) {
        if (!search_text_.isEmpty() && group != e.category_id) {
            group = e.category_id;
            auto* heading = new QListWidgetItem(CategoryName(e.category_id),
                                                 password_list_);
            heading->setData(kSearchGroupRole, true);
            heading->setFlags(Qt::NoItemFlags);
            heading->setSizeHint(QSize(0, 28));
        }
        auto* item = new QListWidgetItem(password_list_);
        item->setData(Qt::UserRole, QVariant::fromValue<qint64>(e.id));
        QWidget* card = CreatePasswordCard(e);
        item->setSizeHint(QSize(0, kCardHeight));
        password_list_->setItemWidget(item, card);
    }

    list_count_->setText(
        QStringLiteral("%1 项").arg(current_entries_.size()));
    QString title = QStringLiteral("所有密码");
    if (selected_category_ == kSectionTrash) {
        title = QStringLiteral("回收站");
    } else if (!search_text_.isEmpty()) {
        title = QStringLiteral("搜索结果");
    } else if (selected_category_ == kSectionFavorites) {
        title = QStringLiteral("收藏夹");
    } else if (selected_category_ == kSectionUncategorized) {
        title = QStringLiteral("未分类");
    } else if (selected_category_ > 0) {
        const auto category = std::find_if(
            categories_.begin(), categories_.end(), [this](const auto& value) {
                return value.id == selected_category_;
            });
        if (category != categories_.end()) title = category->name;
    }
    list_title_->setText(title);
    const bool empty = current_entries_.empty();
    empty_state_->setText(
        !search_text_.isEmpty() ? QStringLiteral("没有搜索结果")
            : selected_category_ == kSectionTrash ? QStringLiteral("回收站为空")
                                                  : QStringLiteral("暂无密码"));
    list_content_stack_->setCurrentWidget(empty ? static_cast<QWidget*>(empty_state_)
                                               : password_list_);
    suppress_selection_ = false;
    UpdateTrashActions();

    if (detail_panel_ && detail_panel_->HasEntry()) {
        const std::int64_t current = detail_panel_->entry_id();
        auto still = deps_.password_dao->FindById(current);
        const bool trash = selected_category_ == kSectionTrash;
        if (!still || still->is_deleted != trash) {
            detail_panel_->ClearEntry();
            showing_detail_ = false;
        } else {
            detail_panel_->SetEntry(*still, DecryptPassword(*still));
        }
    }
    UpdateResponsiveLayout();
}

std::optional<model::PasswordEntry> MainWindow::FindEntry(
    std::int64_t id) const {
    for (const auto& e : current_entries_) {
        if (e.id == id) return e;
    }
    return std::nullopt;
}

void MainWindow::OnPasswordSelectionChanged() {
    if (suppress_selection_ || !deps_.session_key) return;
    UpdateTrashActions();
    if (static_cast<PasswordList*>(password_list_)->selection_only_gesture()) return;
    const auto selected = password_list_->selectedItems();
    if (selected.isEmpty()) {
        ShowListPane();
        return;
    }
    if (selected.size() > 1) {
        detail_panel_->ClearEntry();
        showing_detail_ = false;
        UpdateResponsiveLayout();
        return;
    }
    auto* item = selected.first();
    const std::int64_t id = item->data(Qt::UserRole).toLongLong();
    auto entry = FindEntry(id);
    if (!entry.has_value()) return;
    detail_panel_->SetEntry(*entry, DecryptPassword(*entry));
    showing_detail_ = true;
    UpdateResponsiveLayout();
}

std::optional<QString> MainWindow::DecryptPassword(
    const model::PasswordEntry& entry) const {
    if (!deps_.session_key) return std::nullopt;
    if (entry.encrypted_password.isEmpty() && entry.password_iv.isEmpty()) {
        return QString();
    }
    if (entry.encrypted_password.isEmpty() ||
        entry.password_iv.size() !=
            static_cast<int>(crypto::CryptoService::kIvSize)) {
        return std::nullopt;
    }
    const auto plaintext_opt = crypto::CryptoService::DecryptGcm(
        deps_.session_key->data(), deps_.session_key->size(),
        reinterpret_cast<const std::uint8_t*>(entry.password_iv.constData()),
        entry.password_iv.size(),
        reinterpret_cast<const std::uint8_t*>(
            entry.encrypted_password.constData()),
        entry.encrypted_password.size());
    if (!plaintext_opt.has_value()) return std::nullopt;
    return QString::fromUtf8(*plaintext_opt);
}

bool MainWindow::EncryptAndAssign(model::PasswordEntry* entry,
                                   const QString& plaintext) const {
    if (!deps_.session_key ||
        deps_.session_key->size() != crypto::SessionKey::kSize || !entry) {
        return false;
    }
    const QByteArray utf8 = plaintext.toUtf8();
    QByteArray iv(static_cast<int>(crypto::CryptoService::kIvSize), 0);
    crypto::Random::Fill(reinterpret_cast<std::uint8_t*>(iv.data()), iv.size());
    QByteArray ct = crypto::CryptoService::EncryptGcm(
        deps_.session_key->data(), deps_.session_key->size(),
        reinterpret_cast<const std::uint8_t*>(iv.constData()), iv.size(),
        reinterpret_cast<const std::uint8_t*>(utf8.constData()), utf8.size());
    if (ct.isEmpty()) return false;
    entry->encrypted_password = std::move(ct);
    entry->password_iv = std::move(iv);
    entry->strength = generator::CalculatePasswordStrength(plaintext);
    return true;
}

void MainWindow::OnNewPassword() {
    if (reset_confirmation_active_ || migration_active_) return;
    ShowPasswordCreate();
}

void MainWindow::OnAddCategory() {
    if (reset_confirmation_active_ || migration_active_) return;
    if (!deps_.category_dao || !deps_.session_key) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(
                             this, QStringLiteral("新增分类"),
                             QStringLiteral("分类名称"), QLineEdit::Normal,
                             QString(), &accepted)
                             .trimmed();
    if (!accepted || reset_confirmation_active_ || migration_active_ || !deps_.session_key) return;
    if (name.isEmpty()) {
        Toast::Show(this, QStringLiteral("分类名称不能为空"),
                    Toast::Level::kWarning);
        return;
    }
    const auto duplicate = std::find_if(
        categories_.begin(), categories_.end(), [&name](const auto& category) {
            return category.name.compare(name, Qt::CaseInsensitive) == 0;
        });
    if (duplicate != categories_.end()) {
        Toast::Show(this, QStringLiteral("分类已存在"),
                    Toast::Level::kWarning);
        return;
    }

    model::Category category;
    category.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    category.name = name;
    category.color = static_cast<int>(categories_.size() % 6);
    category.sort_order = 0;
    for (const auto& existing : categories_) {
        category.sort_order =
            std::max(category.sort_order, existing.sort_order + 1);
    }
    category.created_at = QDateTime::currentMSecsSinceEpoch();
    category.updated_at = category.created_at;
    const auto id = deps_.category_dao->Insert(category);
    if (!id.has_value()) {
        Toast::Show(this, QStringLiteral("新增分类失败"),
                    Toast::Level::kError);
        return;
    }
    selected_category_ = *id;
    if (deps_.sync_scheduler) deps_.sync_scheduler->MarkDirty();
    showing_detail_ = false;
    if (width() < kSidebarBreakpoint) compact_sidebar_expanded_ = false;
    Reload();
    UpdateResponsiveLayout();
}

void MainWindow::ShowPasswordCreate() {
    if (reset_confirmation_active_ || migration_active_) return;
    if (!editor_panel_ || !deps_.session_key) return;
    QList<model::Category> cats;
    for (const auto& c : categories_) cats.append(c);
    editor_panel_->SetCategories(std::move(cats));
    editor_entry_id_ = -1;
    editor_panel_->SetPanelWidth(detail_panel_->width());
    editor_panel_->OpenForCreate();
}

void MainWindow::OnRenameCategory(std::int64_t category_id) {
    if (reset_confirmation_active_ || migration_active_) return;
    if (!deps_.session_key || !deps_.category_dao) return;
    auto category = deps_.category_dao->FindById(category_id);
    if (!category || category->is_deleted) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("重命名分类"), QStringLiteral("分类名称"),
        QLineEdit::Normal, category->name, &accepted).trimmed();
    if (accepted && UpdateCategoryName(category_id, name)) Reload();
}

bool MainWindow::UpdateCategoryName(std::int64_t category_id, const QString& value) {
    if (reset_confirmation_active_ || migration_active_ ||
        !deps_.session_key || !deps_.category_dao) return false;
    auto category = deps_.category_dao->FindById(category_id);
    if (!category || category->is_deleted) return false;
    const auto name = value.trimmed();
    if (name == category->name) return false;
    if (name.isEmpty()) {
        Toast::Show(this, QStringLiteral("分类名称不能为空"),
                    Toast::Level::kWarning);
        return false;
    }
    for (const auto& existing : categories_) {
        if (existing.id != category_id &&
            existing.name.compare(name, Qt::CaseInsensitive) == 0) {
            Toast::Show(this, QStringLiteral("分类已存在"),
                        Toast::Level::kWarning);
            return false;
        }
    }
    category->name = name;
    category->updated_at = QDateTime::currentMSecsSinceEpoch();
    if (!deps_.category_dao->Update(*category)) {
        Toast::Show(this, QStringLiteral("分类更新失败"), Toast::Level::kError);
        return false;
    }
    if (deps_.sync_scheduler) deps_.sync_scheduler->MarkDirty();
    return true;
}

void MainWindow::OnDeleteCategory(std::int64_t category_id) {
    if (reset_confirmation_active_ || migration_active_) return;
    if (!deps_.session_key || !deps_.category_dao || !deps_.password_dao) return;
    const auto category = deps_.category_dao->FindById(category_id);
    if (!category || category->is_deleted) return;
    QMessageBox message(QMessageBox::Question, QStringLiteral("删除分类"),
                        QStringLiteral("删除分类“%1”及其中的所有密码？密码将移入回收站，不会永久删除。")
                            .arg(category->name),
                        QMessageBox::Yes | QMessageBox::No, this);
    message.setTextFormat(Qt::PlainText);
    message.button(QMessageBox::Yes)->setText(QStringLiteral("删除分类及密码"));
    message.button(QMessageBox::No)->setText(QStringLiteral("取消"));
    message.setDefaultButton(QMessageBox::No);
    message.setEscapeButton(QMessageBox::No);
    if (message.exec() != QMessageBox::Yes || !deps_.session_key) return;

    bool success = false;
    try {
        storage::Transaction transaction(deps_.password_dao->database());
        const auto now = QDateTime::currentMSecsSinceEpoch();
        success = deps_.password_dao->LogicalDeleteByCategory(category_id, now) &&
                  deps_.category_dao->LogicalDelete(category_id, now) &&
                  transaction.Commit();
    } catch (const std::exception&) {
        success = false;
    }
    if (!success) {
        Toast::Show(this, QStringLiteral("分类删除失败，未改变数据"),
                    Toast::Level::kError);
        return;
    }
    if (selected_category_ == category_id) selected_category_ = kSectionAll;
    if (deps_.sync_scheduler) deps_.sync_scheduler->MarkDirty();
    Reload();
}

void MainWindow::PersistCategoryOrder() {
    if (reset_confirmation_active_ || migration_active_) return;
    if (suppress_selection_ || !deps_.session_key || !deps_.password_dao ||
        !deps_.category_dao) return;
    bool success = false;
    try {
        storage::Transaction transaction(deps_.password_dao->database());
        const auto now = QDateTime::currentMSecsSinceEpoch();
        success = true;
        for (int row = 0; row < categories_list_->count(); ++row) {
            const auto id = categories_list_->item(row)->data(Qt::UserRole).toLongLong();
            if (!deps_.category_dao->UpdateSortOrder(id, row, now)) {
                success = false;
                break;
            }
        }
        success = success && transaction.Commit();
    } catch (const std::exception&) {
        success = false;
    }
    if (success) {
        if (deps_.sync_scheduler) deps_.sync_scheduler->MarkDirty();
    } else {
        Toast::Show(this, QStringLiteral("分类排序保存失败"), Toast::Level::kError);
    }
    Reload();
}

void MainWindow::OpenEditDialog(std::int64_t entry_id) {
    if (reset_confirmation_active_ || migration_active_) return;
    if (!editor_panel_ || !deps_.session_key) return;
    auto found = deps_.password_dao->FindById(entry_id);
    if (!found.has_value() || found->is_deleted) return;
    QList<model::Category> cats;
    for (const auto& c : categories_) cats.append(c);
    editor_panel_->SetCategories(std::move(cats));
    const auto password = DecryptPassword(*found);
    if (!password.has_value()) {
        Toast::Show(this, QStringLiteral("无法解密密码，未打开编辑器"),
                    Toast::Level::kError);
        return;
    }
    EditorPanel::DecryptedEntry decrypted{*found, *password};
    editor_entry_id_ = entry_id;
    editor_panel_->SetPanelWidth(detail_panel_->width());
    editor_panel_->OpenForEdit(decrypted);
}

void MainWindow::OnEditorSaveRequested() {
    if (reset_confirmation_active_ || migration_active_) return;
    if (!editor_panel_ || !editor_panel_->IsOpen() || !deps_.session_key) return;
    auto result = editor_panel_->Result();
    if (editor_entry_id_ < 0) {
        model::PasswordEntry entry = result.entry;
        entry.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const std::int64_t now = QDateTime::currentMSecsSinceEpoch();
        entry.created_at = now;
        entry.updated_at = now;
        if (!EncryptAndAssign(&entry, result.password)) {
            Toast::Show(this, QStringLiteral("加密失败"), Toast::Level::kError);
            return;
        }
        if (!deps_.password_dao->Insert(entry).has_value()) {
            Toast::Show(this, QStringLiteral("保存失败"), Toast::Level::kError);
            return;
        }
        Toast::Show(this, QStringLiteral("已保存"), Toast::Level::kSuccess);
    } else {
        model::PasswordEntry updated = result.entry;
        const auto original = deps_.password_dao->FindById(editor_entry_id_);
        if (!original || original->is_deleted) return;
        const auto original_password = DecryptPassword(*original);
        if (!original_password) return;
        const bool content_changed =
            updated.title != original->title ||
            updated.username != original->username ||
            result.password != *original_password ||
            updated.website != original->website ||
            updated.app_package_name != original->app_package_name ||
            updated.notes != original->notes ||
            updated.category_id != original->category_id ||
            updated.icon_color != original->icon_color;
        if (!content_changed) {
            editor_entry_id_ = -1;
            editor_panel_->Close();
            Reload();
            return;
        }
        if (original->updated_at == std::numeric_limits<std::int64_t>::max()) {
            return;
        }
        updated.is_favorite = original->is_favorite;
        updated.updated_at = std::max(original->updated_at + 1,
                                     QDateTime::currentMSecsSinceEpoch());
        if (!EncryptAndAssign(&updated, result.password)) {
            Toast::Show(this, QStringLiteral("加密失败"), Toast::Level::kError);
            return;
        }
        if (!deps_.password_dao->Update(updated)) {
            Toast::Show(this, QStringLiteral("保存失败"), Toast::Level::kError);
            return;
        }
        Toast::Show(this, QStringLiteral("已更新"), Toast::Level::kSuccess);
    }
    if (deps_.sync_scheduler) deps_.sync_scheduler->MarkDirty();
    editor_entry_id_ = -1;
    editor_panel_->Close();
    Reload();
}

void MainWindow::OnEditorCancelRequested() {
    if (!editor_panel_) return;
    editor_entry_id_ = -1;
    editor_panel_->Close();
}

void MainWindow::OnEditRequested(std::int64_t entry_id) {
    OpenEditDialog(entry_id);
}

void MainWindow::OnDeleteRequested(std::int64_t entry_id) {
    DeleteEntries({entry_id});
}

void MainWindow::OnDeleteSelected() {
    DeleteEntries(SelectedPasswordIds());
}

void MainWindow::DeleteEntries(const std::vector<std::int64_t>& ids) {
    if (reset_confirmation_active_ || migration_active_) return;
    if (!deps_.session_key || !deps_.password_dao || ids.empty()) return;
    if (selected_category_ == kSectionTrash) {
        PermanentlyDeleteEntries(ids, false);
        return;
    }
    auto reply = QMessageBox::question(
        this, QStringLiteral("删除确认"),
        ids.size() == 1 ? QStringLiteral("确定将该密码移至回收站吗？")
                        : QStringLiteral("确定将选中的 %1 条密码移至回收站吗？")
                              .arg(ids.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    if (!deps_.session_key) return;
    if (!deps_.password_dao->BulkLogicalDelete(
            ids, QDateTime::currentMSecsSinceEpoch())) {
        Toast::Show(this, QStringLiteral("删除失败"), Toast::Level::kError);
        return;
    }
    if (deps_.sync_scheduler) deps_.sync_scheduler->MarkDirty();
    Reload();
    UpdateResponsiveLayout();
}

std::vector<std::int64_t> MainWindow::SelectedPasswordIds() const {
    std::vector<std::int64_t> ids;
    for (const auto* item : password_list_->selectedItems()) {
        ids.push_back(item->data(Qt::UserRole).toLongLong());
    }
    return ids;
}

void MainWindow::UpdateTrashActions() {
    const bool trash = selected_category_ == kSectionTrash;
    const bool selected = !password_list_->selectedItems().isEmpty();
    restore_selected_button_->setVisible(trash);
    permanently_delete_button_->setVisible(trash);
    empty_trash_button_->setVisible(trash);
    restore_selected_button_->setEnabled(trash && selected);
    permanently_delete_button_->setEnabled(trash && selected);
    const auto ids = deps_.password_dao->ListTrashIds();
    empty_trash_button_->setEnabled(trash && ids && !ids->empty());
}

void MainWindow::OnRestoreSelected() {
    RestoreEntries(SelectedPasswordIds());
}

void MainWindow::RestoreEntries(const std::vector<std::int64_t>& ids) {
    if (reset_confirmation_active_ || migration_active_) return;
    if (!deps_.session_key || selected_category_ != kSectionTrash ||
        ids.empty()) return;
    FinishTrashOperation(deps_.password_dao->BulkRestore(
        ids, QDateTime::currentMSecsSinceEpoch()));
}

void MainWindow::OnEmptyTrash() {
    if (reset_confirmation_active_ || migration_active_) return;
    PermanentlyDeleteEntries({}, true);
}

void MainWindow::PermanentlyDeleteEntries(
    const std::vector<std::int64_t>& ids, bool empty_trash) {
    if (reset_confirmation_active_ || migration_active_) return;
    if (!deps_.session_key || selected_category_ != kSectionTrash) return;
    const auto trash_ids = deps_.password_dao->ListTrashIds();
    if (!trash_ids) {
        Toast::Show(this, QStringLiteral("读取回收站失败"), Toast::Level::kError);
        return;
    }
    std::vector<std::int64_t> targets;
    for (const auto id : *trash_ids) {
        if (empty_trash || std::find(ids.begin(), ids.end(), id) != ids.end()) {
            targets.push_back(id);
        }
    }
    if (targets.empty()) return;
    const auto reply = QMessageBox::question(
        this, QStringLiteral("永久删除确认"),
        empty_trash ? QStringLiteral("确定清空整个回收站中的 %1 条密码吗？此操作无法撤销。")
                          .arg(targets.size())
                    : QStringLiteral("确定永久删除所选的 %1 条密码吗？此操作无法撤销。")
                          .arg(targets.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes || !deps_.session_key) return;
    // Keep the confirmed target set stable across the modal event loop.
    FinishTrashOperation(deps_.password_dao->BulkPermanentlyDelete(
        targets, QDateTime::currentMSecsSinceEpoch()));
}

void MainWindow::FinishTrashOperation(const std::optional<int>& count) {
    if (!count) {
        Toast::Show(this, QStringLiteral("回收站操作失败"), Toast::Level::kError);
        return;
    }
    if (*count > 0 && deps_.sync_scheduler) deps_.sync_scheduler->MarkDirty();
    Reload();
    UpdateResponsiveLayout();
}

void MainWindow::OnCopyPasswordRequested(std::int64_t entry_id) {
    if (!deps_.session_key) return;
    auto entry = FindEntry(entry_id);
    if (!entry) entry = deps_.password_dao->FindById(entry_id);
    if (!entry) return;
    const auto plain = DecryptPassword(*entry);
    if (!plain.has_value()) {
        Toast::Show(this, QStringLiteral("无法解密密码"),
                    Toast::Level::kError);
        return;
    }
    if (plain->isEmpty()) return;
    ClipboardManager::Instance()->CopySensitive(*plain);
    Toast::Show(this, QStringLiteral("密码已复制，%1 秒后清除")
                          .arg(ClipboardManager::Instance()->timeout_ms() /
                                1000),
                Toast::Level::kSuccess);
}

void MainWindow::OnCopyUsernameRequested(std::int64_t entry_id) {
    if (!deps_.session_key) return;
    auto entry = FindEntry(entry_id);
    if (!entry) entry = deps_.password_dao->FindById(entry_id);
    if (!entry || entry->username.isEmpty()) return;
    ClipboardManager::Instance()->CopyPlain(entry->username);
    Toast::Show(this, QStringLiteral("用户名已复制"), Toast::Level::kSuccess);
}

void MainWindow::OnOpenWebsiteRequested(std::int64_t entry_id) {
    if (!deps_.session_key) return;
    auto entry = FindEntry(entry_id);
    if (!entry) entry = deps_.password_dao->FindById(entry_id);
    if (!entry || entry->website.isEmpty()) return;
    QString url = entry->website;
    if (!url.startsWith(QStringLiteral("http://"),
                         Qt::CaseInsensitive) &&
        !url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        url = QStringLiteral("https://") + url;
    }
    QDesktopServices::openUrl(QUrl(url));
}

void MainWindow::OnFavoriteToggleRequested(std::int64_t entry_id, bool desired) {
    if (reset_confirmation_active_ || migration_active_) return;
    if (!deps_.session_key) return;
    const auto entry = deps_.password_dao->FindById(entry_id);
    if (!entry || entry->is_deleted) return;
    const bool saved = deps_.password_dao->SetFavorite(entry_id, desired);
    const bool favorite = saved ? desired : entry->is_favorite;
    for (auto& cached : current_entries_) {
        if (cached.id == entry_id) cached.is_favorite = favorite;
    }
    for (int i = 0; i < password_list_->count(); ++i) {
        auto* item = password_list_->item(i);
        if (item->data(Qt::UserRole).toLongLong() != entry_id) continue;
        SetCardFavorite(password_list_->itemWidget(item), favorite);
        break;
    }
    if (detail_panel_->entry_id() == entry_id) {
        detail_panel_->SetFavorite(favorite);
    }
    if (!saved) {
        Toast::Show(this, QStringLiteral("收藏更新失败"), Toast::Level::kError);
        return;
    }
    if (entry->is_favorite == favorite) return;
    for (int i = 0; i < sections_list_->count(); ++i) {
        auto* item = sections_list_->item(i);
        if (item->data(Qt::UserRole).toLongLong() != kSectionFavorites) continue;
        const int count = item->data(kSidebarCountRole).toInt() +
                          (favorite ? 1 : -1);
        item->setData(kSidebarCountRole, count);
        item->setData(Qt::AccessibleDescriptionRole,
                      QStringLiteral("%1 项").arg(count));
        break;
    }
    if (deps_.sync_scheduler) deps_.sync_scheduler->MarkDirty();
    if (selected_category_ == kSectionFavorites) Reload();
}

void MainWindow::OnSyncClicked() {
    if (reset_confirmation_active_ || migration_active_ || !deps_.session_key) return;
    if (!deps_.sync_scheduler || !deps_.sync_manager ||
        !deps_.sync_manager->provider()) {
        const QString message = QStringLiteral("请先在设置中连接 Google 账号");
        UpdateSyncStatus(sync::SyncManager::StatusKind::kError, message);
        Toast::Show(this, message, Toast::Level::kWarning);
        return;
    }
    if (deps_.sync_scheduler->is_paused()) return;
    if (deps_.sync_manager->is_busy()) {
        OnSyncStarted();
        return;
    }
    OnSyncStarted();
    sync_button_->repaint();
    deps_.sync_scheduler->SyncImmediately();
}

void MainWindow::RefreshSyncButton() {
    if (!sync_button_) return;
    const QString state = sync_button_->property("state").toString();
    QString color = QStringLiteral("text-secondary");
    QString label = QStringLiteral("云端同步");
    if (state == QStringLiteral("busy")) {
        color = QStringLiteral("primary");
        label = QStringLiteral("同步中…");
    } else if (state == QStringLiteral("ok")) {
        color = QStringLiteral("success");
        label = QStringLiteral("已同步");
    } else if (state == QStringLiteral("conflict")) {
        color = QStringLiteral("warn");
        label = QStringLiteral("同步冲突");
    } else if (state == QStringLiteral("error")) {
        color = QStringLiteral("danger");
        label = QStringLiteral("同步异常");
    }
    sync_button_->setText(label);
    sync_button_->setIcon(IconLoader::Load(
        state == QStringLiteral("busy") ? QStringLiteral("refresh-cw")
                                         : QStringLiteral("cloud"),
        ThemeManager::Instance()->Color(color), kIconSize));
    sync_button_->style()->unpolish(sync_button_);
    sync_button_->style()->polish(sync_button_);
}

void MainWindow::OnSyncStarted() {
    sync_button_->setEnabled(false);
    sync_button_->setToolTip(QStringLiteral("同步中…"));
    sync_button_->setAccessibleName(QStringLiteral("同步中"));
    sync_button_->setProperty("state", QStringLiteral("busy"));
    RefreshSyncButton();
}

void MainWindow::OnSyncFinished(sync::SyncManager::StatusKind kind,
                                const QString& message) {
    switch (kind) {
        case sync::SyncManager::StatusKind::kSuccess:
            UpdateSyncStatus(kind, QStringLiteral("已同步到云端"));
            Toast::Show(this, QStringLiteral("同步完成"),
                        Toast::Level::kSuccess);
            break;
        case sync::SyncManager::StatusKind::kConflict:
            UpdateSyncStatus(kind, QStringLiteral("同步冲突，请稍后重试"));
            Toast::Show(this, QStringLiteral("同步冲突，请稍后重试"),
                        Toast::Level::kWarning);
            break;
        case sync::SyncManager::StatusKind::kError:
        default:
            UpdateSyncStatus(kind, QStringLiteral("同步失败：%1").arg(message));
            Toast::Show(this, QStringLiteral("同步失败：%1").arg(message),
                        Toast::Level::kError);
            break;
    }
    Reload();
}

void MainWindow::UpdateSyncStatus(sync::SyncManager::StatusKind kind,
                                  const QString& message) {
    sync_button_->setEnabled(true);
    sync_button_->setToolTip(message + QStringLiteral("\n点击立即同步"));
    sync_button_->setAccessibleName(QStringLiteral("立即同步"));
    const char* state = "ok";
    if (kind == sync::SyncManager::StatusKind::kConflict) {
        state = "conflict";
    } else if (kind == sync::SyncManager::StatusKind::kError) {
        state = "error";
    }
    sync_button_->setProperty("state", QString::fromLatin1(state));
    RefreshSyncButton();
}

void MainWindow::UpdateResponsiveLayout() {
    if (!sidebar_ || !workspace_content_ || !workspace_content_layout_ ||
        !password_list_column_ || !detail_panel_ || !navigation_button_ ||
        !back_to_list_button_ || !search_container_) {
        return;
    }

    const int client_width = centralWidget() ? centralWidget()->width() : width();
    const bool compact = client_width < kSidebarBreakpoint;

    if (!compact) compact_sidebar_expanded_ = false;
    sidebar_->setVisible(!compact || compact_sidebar_expanded_);
    navigation_button_->setVisible(compact);
    password_list_column_->setVisible(true);

    if (!compact) {
        back_to_list_button_->setVisible(false);
        if (auto* fx = detail_panel_->graphicsEffect()) fx->setEnabled(false);

        const int avail = std::max(780, client_width - kSidebarWidth);
        const int list_width = std::clamp(avail * 65 / 100,
                                          kPasswordListMinWidth,
                                          avail - 360);
        password_list_column_->setMinimumWidth(list_width);
        password_list_column_->setMaximumWidth(list_width);
        detail_panel_->setMinimumWidth(360);
        detail_panel_->setMaximumWidth(QWIDGETSIZE_MAX);

        if (detail_panel_->parentWidget() != workspace_content_) {
            detail_panel_->setParent(workspace_content_);
        }
        if (workspace_content_layout_->indexOf(detail_panel_) < 0) {
            workspace_content_layout_->addWidget(detail_panel_, 1);
        }
        detail_panel_->setVisible(true);
        search_container_->setMinimumWidth(kSearchMinWidth);
        search_container_->setMaximumWidth(kSearchMaxWidth);
        workspace_content_layout_->activate();
    } else {
        const bool show_detail =
            showing_detail_ && detail_panel_->HasEntry() &&
            (!editor_panel_ || !editor_panel_->isVisible());
        back_to_list_button_->setVisible(show_detail);
        if (auto* fx = detail_panel_->graphicsEffect()) fx->setEnabled(true);

        if (workspace_content_layout_->indexOf(detail_panel_) >= 0) {
            workspace_content_layout_->removeWidget(detail_panel_);
        }
        if (detail_panel_->parentWidget() != workspace_content_) {
            detail_panel_->setParent(workspace_content_);
        }
        password_list_column_->setMinimumWidth(0);
        password_list_column_->setMaximumWidth(QWIDGETSIZE_MAX);
        workspace_content_layout_->activate();

        const int content_width =
            std::max(1, workspace_content_->width() > 0
                            ? workspace_content_->width()
                            : client_width);
        const int content_height =
            std::max(1, workspace_content_->height() > 0
                            ? workspace_content_->height()
                            : std::max(1, height() - kWorkspaceHeaderHeight));
        const int drawer_width = std::min(kDetailWidth, content_width);
        detail_panel_->setFixedWidth(drawer_width);
        detail_panel_->setGeometry(content_width - drawer_width, 0,
                                   drawer_width, content_height);
        detail_panel_->setVisible(show_detail);
        if (show_detail) detail_panel_->raise();
        search_container_->setMinimumWidth(140);
        search_container_->setMaximumWidth(kSearchMaxWidth);
    }
    if (editor_panel_) editor_panel_->SetPanelWidth(detail_panel_->width());
}

void MainWindow::ShowListPane() {
    if (detail_panel_) detail_panel_->ClearEntry();
    showing_detail_ = false;
    UpdateResponsiveLayout();
    if (password_list_) {
        const QSignalBlocker blocker(password_list_);
        password_list_->setFocus();
        password_list_->clearSelection();
        password_list_->setCurrentItem(nullptr);
        UpdateTrashActions();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == search_ &&
        (event->type() == QEvent::ShortcutOverride ||
         event->type() == QEvent::KeyPress)) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            if (event->type() == QEvent::KeyPress) workspace_->setFocus();
            event->accept();
            return true;
        }
    }
    if (watched == editor_panel_ &&
        (event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
        UpdateResponsiveLayout();
    }
    if (watched == password_list_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Delete && key->modifiers() == Qt::NoModifier) {
            OnDeleteSelected();
            return true;
        }
    }
    if (watched == workspace_content_ && event->type() == QEvent::Resize) {
        UpdateResponsiveLayout();
    }
    if (password_list_ && event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (detail_panel_->isVisible() &&
            detail_panel_->rect().contains(detail_panel_->mapFromGlobal(
                mouse->globalPosition().toPoint()))) {
            return QMainWindow::eventFilter(watched, event);
        }
        const bool list_blank = watched == password_list_->viewport() &&
            !password_list_->itemAt(mouse->position().toPoint());
        if (mouse->button() == Qt::LeftButton &&
            !mouse->modifiers().testFlag(Qt::ControlModifier) &&
            !mouse->modifiers().testFlag(Qt::ShiftModifier) &&
            (list_blank || watched == password_list_column_ ||
             watched->objectName() == QStringLiteral("ListTitleRow") ||
             watched == workspace_content_ || watched == empty_state_)) {
            ShowListPane();
            workspace_->setFocus(Qt::MouseFocusReason);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::ToggleCompactSidebar() {
    const int client_width = centralWidget() ? centralWidget()->width() : width();
    if (client_width >= kSidebarBreakpoint) return;
    compact_sidebar_expanded_ = !compact_sidebar_expanded_;
    UpdateResponsiveLayout();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    UpdateResponsiveLayout();
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange || !isMinimized()) return;
    const auto* state = static_cast<QWindowStateChangeEvent*>(event);
    if ((state->oldState() & Qt::WindowMinimized) || !deps_.session_key ||
        !QSettings().value("session/lock_on_minimize", false).toBool()) return;
    QTimer::singleShot(0, this, [this]() {
        if (deps_.session_key) emit LockRequested();
    });
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape &&
        central_stack_->currentWidget() == preferences_page_) {
        central_stack_->setCurrentWidget(workspace_page_);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape &&
        (showing_detail_ || !password_list_->selectedItems().isEmpty())) {
        ShowListPane();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F &&
        (event->modifiers() & Qt::ControlModifier)) {
        if (search_) search_->setFocus();
        return;
    }
    if (event->key() == Qt::Key_L &&
        (event->modifiers() & Qt::ControlModifier)) {
        emit LockRequested();
        return;
    }
    if (event->key() == Qt::Key_N &&
        (event->modifiers() & Qt::ControlModifier)) {
        OnNewPassword();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

}  // namespace passvault::ui
