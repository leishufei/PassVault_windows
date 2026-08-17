#include "ui/main_window.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

#include "crypto/crypto_service.h"
#include "crypto/random.h"
#include "crypto/session_key.h"
#include "generator/password_strength.h"
#include "storage/category_dao.h"
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

constexpr int kSidebarWidth = 224;
constexpr int kDetailWidth = 400;
constexpr int kPasswordListMinWidth = 380;
constexpr int kWorkspaceHeaderHeight = 68;
constexpr int kSearchWidth = 390;
constexpr int kSearchHeight = 36;
constexpr int kIconSize = 16;
constexpr int kSectionIconSize = 18;
constexpr int kCardHeight = 60;
constexpr int kDesktopBreakpoint = 1280;
constexpr int kSidebarBreakpoint = 1024;
constexpr int kSidebarCountRole = Qt::UserRole + 1;
constexpr int kSidebarSystemRole = Qt::UserRole + 2;
constexpr int kSidebarIconRole = Qt::UserRole + 3;

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
        const QColor foreground = selected
                                      ? theme->Color(QStringLiteral(
                                            "primary-text-on-tint"))
                                      : theme->Color(hovered
                                            ? QStringLiteral("text-primary")
                                            : QStringLiteral("text-secondary"));
        const QRect content = option.rect.adjusted(12, 0, -12, 0);

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
        if (has_count && count > 0) {
            QFont count_font = option.font;
            count_font.setPixelSize(10);
            count_font.setWeight(QFont::DemiBold);
            const QFontMetrics count_metrics(count_font);
            const QString count_text = QString::number(count);
            const int badge_width =
                std::max(20, count_metrics.horizontalAdvance(count_text) + 12);
            const QRect badge_rect(content.right() - badge_width + 1,
                                   content.center().y() - 8, badge_width, 16);
            const bool system_section =
                index.data(kSidebarSystemRole).toBool();
            painter->setPen(Qt::NoPen);
            painter->setBrush(theme->Color(
                selected || system_section ? QStringLiteral("primary-tint-2")
                                           : QStringLiteral("bg-muted")));
            painter->drawRoundedRect(badge_rect, 8, 8);
            painter->setFont(count_font);
            painter->setPen(theme->Color(
                selected || system_section
                    ? QStringLiteral("primary-text-on-tint")
                    : QStringLiteral("muted-8")));
            painter->drawText(badge_rect, Qt::AlignCenter, count_text);
            text_rect.setRight(badge_rect.left() - 8);
        }

        QFont text_font = option.font;
        if (selected) text_font.setWeight(QFont::DemiBold);
        painter->setFont(text_font);
        painter->setPen(foreground);
        painter->drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter,
                          option.fontMetrics.elidedText(
                              index.data(Qt::DisplayRole).toString(),
                              Qt::ElideRight, text_rect.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(index.data(kSidebarSystemRole).toBool() ? 36 : 32);
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
    setWindowTitle(QStringLiteral("PassVault"));
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
    auto* workspace_layout = new QVBoxLayout(workspace_);
    workspace_layout->setContentsMargins(0, 0, 0, 0);
    workspace_layout->setSpacing(0);
    workspace_layout->addWidget(BuildWorkspaceHeader());

    workspace_content_ = new QWidget(workspace_);
    workspace_content_->setObjectName(QStringLiteral("VaultWorkspaceContent"));
    workspace_content_layout_ = new QHBoxLayout(workspace_content_);
    workspace_content_layout_->setContentsMargins(0, 0, 0, 0);
    workspace_content_layout_->setSpacing(0);
    password_list_column_ = BuildPasswordListColumn();
    workspace_content_layout_->addWidget(password_list_column_, 1);
    workspace_content_layout_->addWidget(BuildDetailColumn());
    workspace_layout->addWidget(workspace_content_, 1);
    ws_layout->addWidget(workspace_, 1);
    central_stack_->addWidget(workspace_page_);

    editor_panel_ = new EditorPanel(workspace_page_);
    connect(editor_panel_, &EditorPanel::SaveRequested, this,
            &MainWindow::OnEditorSaveRequested);
    connect(editor_panel_, &EditorPanel::CancelRequested, this,
            &MainWindow::OnEditorCancelRequested);

    preferences_page_ = new PreferencesPage(central_stack_);
    connect(preferences_page_, &PreferencesPage::BackRequested, this,
            [this]() { central_stack_->setCurrentWidget(workspace_page_); });
    central_stack_->addWidget(preferences_page_);

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

QWidget* MainWindow::BuildSidebar() {
    auto* sidebar = new QFrame();
    sidebar->setObjectName(QStringLiteral("Sidebar"));
    sidebar->setFixedWidth(kSidebarWidth);
    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(12, 20, 12, 16);
    layout->setSpacing(0);

    auto* brand = new QWidget(sidebar);
    brand->setObjectName(QStringLiteral("SidebarBrand"));
    auto* brand_layout = new QHBoxLayout(brand);
    brand_layout->setContentsMargins(12, 0, 12, 0);
    brand_layout->setSpacing(10);

    auto* brand_badge = new QLabel(brand);
    brand_badge->setObjectName(QStringLiteral("SidebarBrandBadge"));
    brand_badge->setFixedSize(34, 34);
    brand_badge->setAlignment(Qt::AlignCenter);
    const QColor badge_icon_color =
        ThemeManager::Instance()->Color(QStringLiteral("accent-fg"));
    brand_badge->setPixmap(
        IconLoader::Load(QStringLiteral("vault"), badge_icon_color, 18)
            .pixmap(18, 18));
    brand_layout->addWidget(brand_badge);

    auto* brand_name = new QLabel(QStringLiteral("PassVault"), brand);
    brand_name->setObjectName(QStringLiteral("SidebarBrandName"));
    brand_layout->addWidget(brand_name, 1);

    layout->addWidget(brand);
    layout->addSpacing(30);

    auto* new_button = new QPushButton(QStringLiteral("新建密码"), sidebar);
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
    new_button->setFixedHeight(40);
    layout->addWidget(new_button);
    layout->addSpacing(20);

    sections_list_ = new QListWidget(sidebar);
    sections_list_->setObjectName(QStringLiteral("SidebarSectionList"));
    sections_list_->setFrameShape(QFrame::NoFrame);
    sections_list_->setIconSize(QSize(kSectionIconSize, kSectionIconSize));
    sections_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    sections_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sections_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sections_list_->setItemDelegate(new SidebarItemDelegate(sections_list_));
    sections_list_->setFixedHeight(4 * 36);
    connect(sections_list_, &QListWidget::itemSelectionChanged, this,
            &MainWindow::OnSectionSelectionChanged);
    layout->addWidget(sections_list_);
    layout->addSpacing(20);

    auto* divider = new QFrame(sidebar);
    divider->setObjectName(QStringLiteral("SidebarDivider"));
    divider->setFixedHeight(1);
    layout->addWidget(divider);
    layout->addSpacing(20);

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
    add_category_button_->setFixedSize(24, 24);
    add_category_button_->setIcon(IconLoader::Load(
        QStringLiteral("plus"),
        ThemeManager::Instance()->Color(QStringLiteral("text-tertiary")),
        14));
    add_category_button_->setIconSize(QSize(14, 14));
    connect(add_category_button_, &QToolButton::clicked, this,
            &MainWindow::OnAddCategory);
    cat_header_layout->addWidget(add_category_button_);
    cat_header->setFixedHeight(24);
    layout->addWidget(cat_header);
    layout->addSpacing(8);

    categories_list_ = new QListWidget(sidebar);
    categories_list_->setObjectName(QStringLiteral("SidebarCategoryList"));
    categories_list_->setFrameShape(QFrame::NoFrame);
    categories_list_->setIconSize(QSize(kSectionIconSize, kSectionIconSize));
    categories_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    categories_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    categories_list_->setItemDelegate(
        new SidebarItemDelegate(categories_list_));
    connect(categories_list_, &QListWidget::itemSelectionChanged, this,
            &MainWindow::OnCategorySelectionChanged);
    layout->addWidget(categories_list_, 1);

    auto* footer = new QWidget(sidebar);
    footer->setObjectName(QStringLiteral("SidebarFooter"));
    auto* footer_layout = new QVBoxLayout(footer);
    footer_layout->setContentsMargins(0, 0, 0, 0);
    footer_layout->setSpacing(10);

    auto* sync_row = new QWidget(footer);
    sync_row->setObjectName(QStringLiteral("SyncStatusRow"));
    auto* sync_layout = new QHBoxLayout(sync_row);
    sync_layout->setContentsMargins(0, 0, 0, 0);
    sync_layout->setSpacing(8);
    auto* cloud_icon = new QLabel(sync_row);
    cloud_icon->setObjectName(QStringLiteral("SyncStatusIcon"));
    cloud_icon->setPixmap(
        IconLoader::Load(
            QStringLiteral("cloud"),
            ThemeManager::Instance()->Color(QStringLiteral("primary")),
            16)
            .pixmap(16, 16));
    sync_layout->addWidget(cloud_icon);
    sync_status_label_ = new QLabel(QStringLiteral("等待同步"), sync_row);
    sync_status_label_->setObjectName(QStringLiteral("SyncStatusText"));
    sync_layout->addWidget(sync_status_label_, 1);
    sync_status_dot_ = new QLabel(sync_row);
    sync_status_dot_->setObjectName(QStringLiteral("SyncStatusDot"));
    sync_status_dot_->setFixedSize(8, 8);
    sync_status_dot_->setProperty("state", QStringLiteral("idle"));
    sync_layout->addWidget(sync_status_dot_);
    footer_layout->addWidget(sync_row);

    auto* settings_button = new QPushButton(QStringLiteral("设置"), footer);
    settings_button->setObjectName(QStringLiteral("SidebarSettingsButton"));
    settings_button->setProperty("flat", true);
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
    controls_layout->setContentsMargins(28, 0, 28, 0);
    controls_layout->setSpacing(10);

    navigation_button_ = new QToolButton(controls);
    navigation_button_->setObjectName(
        QStringLiteral("HeaderNavigationButton"));
    navigation_button_->setToolTip(QStringLiteral("展开导航"));
    navigation_button_->setCursor(Qt::PointingHandCursor);
    navigation_button_->setFixedSize(36, 36);
    navigation_button_->setIcon(IconLoader::Load(
        QStringLiteral("list"),
        ThemeManager::Instance()->Color(QStringLiteral("text-secondary")),
        18));
    navigation_button_->setIconSize(QSize(18, 18));
    connect(navigation_button_, &QToolButton::clicked, this,
            &MainWindow::ToggleCompactSidebar);
    controls_layout->addWidget(navigation_button_, 0, Qt::AlignVCenter);

    back_to_list_button_ = new QToolButton(controls);
    back_to_list_button_->setObjectName(
        QStringLiteral("HeaderBackToListButton"));
    back_to_list_button_->setToolTip(QStringLiteral("返回密码列表"));
    back_to_list_button_->setCursor(Qt::PointingHandCursor);
    back_to_list_button_->setFixedSize(36, 36);
    back_to_list_button_->setIcon(IconLoader::Load(
        QStringLiteral("arrow-left"),
        ThemeManager::Instance()->Color(QStringLiteral("text-secondary")),
        18));
    back_to_list_button_->setIconSize(QSize(18, 18));
    connect(back_to_list_button_, &QToolButton::clicked, this,
            &MainWindow::ShowListPane);
    controls_layout->addWidget(back_to_list_button_, 0, Qt::AlignVCenter);

    search_container_ = new QFrame(controls);
    search_container_->setObjectName(QStringLiteral("SearchBarContainer"));
    search_container_->setProperty("focused", false);
    search_container_->setFixedHeight(kSearchHeight);
    search_container_->setMinimumWidth(kSearchWidth);
    search_container_->setMaximumWidth(kSearchWidth);
    search_container_->setSizePolicy(QSizePolicy::Expanding,
                                     QSizePolicy::Fixed);
    auto* search_layout = new QHBoxLayout(search_container_);
    search_layout->setContentsMargins(0, 0, 8, 0);
    search_layout->setSpacing(6);

    search_ = new QLineEdit(search_container_);
    search_->setObjectName(QStringLiteral("SearchBar"));
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
    lock_button->setFixedSize(36, 36);
    lock_button->setIcon(IconLoader::Load(
        QStringLiteral("lock-keyhole"),
        ThemeManager::Instance()->Color(QStringLiteral("primary")),
        18));
    lock_button->setIconSize(QSize(18, 18));
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
    more_button_->setFixedSize(36, 36);
    more_button_->setIcon(IconLoader::Load(
        QStringLiteral("more-horizontal"),
        ThemeManager::Instance()->Color(QStringLiteral("text-tertiary")),
        18));
    more_button_->setIconSize(QSize(18, 18));
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
    column->setMinimumWidth(kPasswordListMinWidth);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* title_row = new QWidget(column);
    title_row->setObjectName(QStringLiteral("ListTitleRow"));
    auto* title_layout = new QHBoxLayout(title_row);
    title_layout->setContentsMargins(28, 28, 28, 8);
    title_layout->setSpacing(12);

    list_title_ = new QLabel(QStringLiteral("所有密码"), title_row);
    list_title_->setObjectName(QStringLiteral("ListTitle"));
    title_layout->addWidget(list_title_);
    list_count_ = new QLabel(QStringLiteral("0 项"), title_row);
    list_count_->setObjectName(QStringLiteral("ListCount"));
    title_layout->addWidget(list_count_);
    title_layout->addStretch(1);

    sort_button_ = new QPushButton(QStringLiteral("按更新时间"), title_row);
    sort_button_->setObjectName(QStringLiteral("ListSortMenu"));
    sort_button_->setProperty("flat", true);
    sort_button_->setToolTip(QStringLiteral("当前固定按更新时间排序"));
    sort_button_->setLayoutDirection(Qt::RightToLeft);
    sort_button_->setIcon(IconLoader::Load(
        QStringLiteral("chevron-down"),
        ThemeManager::Instance()->Color(QStringLiteral("text-tertiary")),
        12));
    sort_button_->setIconSize(QSize(12, 12));
    sort_button_->setEnabled(false);
    title_layout->addWidget(sort_button_);

    layout->addWidget(title_row);

    password_list_ = new QListWidget(column);
    password_list_->setObjectName(QStringLiteral("PasswordList"));
    password_list_->setFrameShape(QFrame::NoFrame);
    password_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    password_list_->setSpacing(6);
    password_list_->setUniformItemSizes(true);
    connect(password_list_, &QListWidget::itemSelectionChanged, this,
            &MainWindow::OnPasswordSelectionChanged);
    layout->addWidget(password_list_, 1);

    empty_state_ = new QLabel(QStringLiteral("暂无密码，点击「新建密码」开始"),
                              column);
    empty_state_->setObjectName(QStringLiteral("EmptyState"));
    empty_state_->setAlignment(Qt::AlignCenter);
    empty_state_->setVisible(false);
    layout->addWidget(empty_state_);

    return column;
}

QWidget* MainWindow::BuildDetailColumn() {
    detail_panel_ = new DetailPanel();
    detail_panel_->setFixedWidth(kDetailWidth);
    connect(detail_panel_, &DetailPanel::EditRequested, this,
            &MainWindow::OnEditRequested);
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

    if (auto* badge = findChild<QLabel*>(
            QStringLiteral("SidebarBrandBadge"))) {
        badge->setPixmap(
            icon(QStringLiteral("vault"), QStringLiteral("accent-fg"), 18)
                .pixmap(18, 18));
    }
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
    if (auto* sync_icon = findChild<QLabel*>(
            QStringLiteral("SyncStatusIcon"))) {
        sync_icon->setPixmap(
            icon(QStringLiteral("cloud"), QStringLiteral("primary"), 16)
                .pixmap(16, 16));
    }
    if (auto* button = findChild<QPushButton*>(
            QStringLiteral("SidebarSettingsButton"))) {
        button->setIcon(icon(QStringLiteral("settings-2"),
                             QStringLiteral("text-secondary"), kIconSize));
    }
    if (search_action_) {
        search_action_->setIcon(icon(QStringLiteral("search"),
                                     QStringLiteral("text-tertiary"), 16));
    }
    if (navigation_button_) {
        navigation_button_->setIcon(icon(QStringLiteral("list"),
                                          QStringLiteral("text-secondary"),
                                          18));
    }
    if (back_to_list_button_) {
        back_to_list_button_->setIcon(icon(QStringLiteral("arrow-left"),
                                           QStringLiteral("text-secondary"),
                                           18));
    }
    if (auto* button = findChild<QToolButton*>(
            QStringLiteral("HeaderLockButton"))) {
        button->setIcon(
            icon(QStringLiteral("lock-keyhole"), QStringLiteral("primary"),
                 18));
    }
    if (auto* button = findChild<QToolButton*>(
            QStringLiteral("HeaderMoreButton"))) {
        button->setIcon(icon(QStringLiteral("more-horizontal"),
                             QStringLiteral("text-tertiary"), 18));
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
            auto* star = card ? card->findChild<QLabel*>(
                                    QStringLiteral("PasswordCardStar"))
                              : nullptr;
            const auto entry = FindEntry(
                item->data(Qt::UserRole).toLongLong());
            if (!star || !entry.has_value()) continue;
            const QColor star_color = entry->is_favorite
                ? theme->Color(QStringLiteral("warning"))
                : theme->Color(QStringLiteral("text-quaternary"));
            star->setPixmap(
                IconLoader::Load(QStringLiteral("star"), star_color, 14)
                    .pixmap(14, 14));
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
        int fav_count = 0;
        int uncat_count = 0;
        for (const auto& e : all) {
            if (e.is_favorite) ++fav_count;
            if (e.category_id == 0) ++uncat_count;
        }
        int trash_count = 0;
        for (const auto& e : deps_.password_dao->ListIncludingDeleted()) {
            if (e.is_deleted) ++trash_count;
        }
        const int counts[] = {
            static_cast<int>(all.size()),
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
            ++counts[entry.category_id];
        }
    }
    for (const auto& c : categories_) {
        if (c.is_deleted) continue;
        auto* item = new QListWidgetItem(categories_list_);
        item->setText(c.name);
        item->setData(Qt::UserRole, QVariant::fromValue<qint64>(c.id));
        item->setData(kSidebarCountRole, counts.value(c.id));
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
    selected_category_ = item->data(Qt::UserRole).toLongLong();
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
    selected_category_ = item->data(Qt::UserRole).toLongLong();
    suppress_selection_ = true;
    if (sections_list_) sections_list_->clearSelection();
    suppress_selection_ = false;
    RefreshPasswordList();
    showing_detail_ = false;
    if (width() < kSidebarBreakpoint) compact_sidebar_expanded_ = false;
    UpdateResponsiveLayout();
}

void MainWindow::OnSearchChanged(const QString& text) {
    search_text_ = text.trimmed();
    RefreshPasswordList();
    showing_detail_ = false;
    UpdateResponsiveLayout();
}

QWidget* MainWindow::CreatePasswordCard(const model::PasswordEntry& entry) {
    auto* card = new QFrame();
    card->setObjectName(QStringLiteral("PasswordCard"));
    card->setFrameShape(QFrame::NoFrame);
    card->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(12, 12, 12, 12);
    row->setSpacing(12);

    auto* avatar = new QLabel(AvatarInitial(entry), card);
    avatar->setObjectName(QStringLiteral("PasswordCardIcon"));
    avatar->setProperty(
        "colorIndex",
        static_cast<unsigned int>(entry.icon_color) % 6);
    avatar->setFixedSize(36, 36);
    avatar->setAlignment(Qt::AlignCenter);
    row->addWidget(avatar);

    auto* text_col = new QVBoxLayout();
    text_col->setContentsMargins(0, 0, 0, 0);
    text_col->setSpacing(3);
    auto* title = new ElidedLabel(
        entry.title.isEmpty() ? QStringLiteral("(无标题)") : entry.title, card);
    title->setObjectName(QStringLiteral("PasswordCardTitle"));
    title->setTextInteractionFlags(Qt::NoTextInteraction);
    text_col->addWidget(title);
    auto* meta = new ElidedLabel(
        entry.username.isEmpty() ? QStringLiteral("(无用户名)") : entry.username,
        card);
    meta->setObjectName(QStringLiteral("PasswordCardMeta"));
    text_col->addWidget(meta);
    row->addLayout(text_col, 1);

    auto* right_col = new QVBoxLayout();
    right_col->setContentsMargins(0, 0, 0, 0);
    right_col->setSpacing(4);
    right_col->setAlignment(Qt::AlignRight);
    auto* time = new QLabel(RelativeTimeShort(entry.updated_at), card);
    time->setObjectName(QStringLiteral("PasswordCardTime"));
    time->setAlignment(Qt::AlignRight);
    right_col->addWidget(time);
    {
        auto* star = new QLabel(card);
        star->setObjectName(QStringLiteral("PasswordCardStar"));
        QColor star_color = entry.is_favorite
            ? ThemeManager::Instance()->Color(QStringLiteral("warning"))
            : ThemeManager::Instance()->Color(
                  QStringLiteral("text-quaternary"));
        star->setPixmap(
            IconLoader::Load(QStringLiteral("star"), star_color, 14)
                .pixmap(14, 14));
        star->setAlignment(Qt::AlignRight);
        right_col->addWidget(star);
    }
    row->addLayout(right_col);

    card->setMinimumHeight(kCardHeight);
    return card;
}

void MainWindow::RefreshPasswordList() {
    if (!deps_.password_dao || !password_list_) return;
    suppress_selection_ = true;
    password_list_->clear();

    std::vector<model::PasswordEntry> rows;
    if (!search_text_.isEmpty()) {
        rows = deps_.password_dao->Search(search_text_);
    } else if (selected_category_ == kSectionAll) {
        rows = deps_.password_dao->ListActive();
    } else if (selected_category_ == kSectionFavorites) {
        for (auto& e : deps_.password_dao->ListActive()) {
            if (e.is_favorite) rows.push_back(std::move(e));
        }
    } else if (selected_category_ == kSectionTrash) {
        for (auto& e : deps_.password_dao->ListIncludingDeleted()) {
            if (e.is_deleted) rows.push_back(std::move(e));
        }
    } else if (selected_category_ == kSectionUncategorized) {
        rows = deps_.password_dao->ListByCategory(0);
    } else {
        rows = deps_.password_dao->ListByCategory(selected_category_);
    }

    std::stable_sort(rows.begin(), rows.end(),
                     [](const model::PasswordEntry& lhs,
                        const model::PasswordEntry& rhs) {
                         return lhs.updated_at > rhs.updated_at;
                     });

    current_entries_ = std::move(rows);

    for (const auto& e : current_entries_) {
        auto* item = new QListWidgetItem(password_list_);
        item->setData(Qt::UserRole, QVariant::fromValue<qint64>(e.id));
        QWidget* card = CreatePasswordCard(e);
        item->setSizeHint(QSize(0, kCardHeight));
        password_list_->setItemWidget(item, card);
    }

    list_count_->setText(
        QStringLiteral("%1 项").arg(current_entries_.size()));
    QString title = QStringLiteral("所有密码");
    if (!search_text_.isEmpty()) {
        title = QStringLiteral("搜索结果");
    } else if (selected_category_ == kSectionFavorites) {
        title = QStringLiteral("收藏夹");
    } else if (selected_category_ == kSectionUncategorized) {
        title = QStringLiteral("未分类");
    } else if (selected_category_ == kSectionTrash) {
        title = QStringLiteral("回收站");
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
        search_text_.isEmpty()
            ? QStringLiteral("暂无密码，点击「新建密码」开始")
            : QStringLiteral("没有找到匹配的密码"));
    empty_state_->setVisible(empty);
    password_list_->setVisible(!empty);
    suppress_selection_ = false;

    if (detail_panel_ && detail_panel_->HasEntry()) {
        const std::int64_t current = detail_panel_->entry_id();
        auto still = FindEntry(current);
        if (!still.has_value()) {
            detail_panel_->ClearEntry();
            showing_detail_ = false;
        }
    }
}

std::optional<model::PasswordEntry> MainWindow::FindEntry(
    std::int64_t id) const {
    for (const auto& e : current_entries_) {
        if (e.id == id) return e;
    }
    return std::nullopt;
}

void MainWindow::OnPasswordSelectionChanged() {
    if (suppress_selection_) return;
    const auto selected = password_list_->selectedItems();
    auto* item =
        selected.isEmpty() ? password_list_->currentItem() : selected.first();
    if (!item) {
        if (detail_panel_) detail_panel_->ClearEntry();
        return;
    }
    const std::int64_t id = item->data(Qt::UserRole).toLongLong();
    auto entry = FindEntry(id);
    if (!entry.has_value()) return;
    detail_panel_->SetEntry(*entry, DecryptPassword(*entry));
    showing_detail_ = true;
    UpdateResponsiveLayout();
}

QString MainWindow::DecryptPassword(const model::PasswordEntry& entry) const {
    if (!deps_.session_key) return {};
    if (entry.encrypted_password.isEmpty() ||
        entry.password_iv.size() !=
            static_cast<int>(crypto::CryptoService::kIvSize)) {
        return {};
    }
    const auto plaintext_opt = crypto::CryptoService::DecryptGcm(
        deps_.session_key->data(), deps_.session_key->size(),
        reinterpret_cast<const std::uint8_t*>(entry.password_iv.constData()),
        entry.password_iv.size(),
        reinterpret_cast<const std::uint8_t*>(
            entry.encrypted_password.constData()),
        entry.encrypted_password.size());
    if (!plaintext_opt.has_value()) return {};
    return QString::fromUtf8(*plaintext_opt);
}

bool MainWindow::EncryptAndAssign(model::PasswordEntry* entry,
                                   const QString& plaintext) const {
    if (!deps_.session_key || !entry) return false;
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

void MainWindow::OnNewPassword() { ShowPasswordCreate(); }

void MainWindow::OnAddCategory() {
    if (!deps_.category_dao) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(
                             this, QStringLiteral("新增分类"),
                             QStringLiteral("分类名称"), QLineEdit::Normal,
                             QString(), &accepted)
                             .trimmed();
    if (!accepted) return;
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
    showing_detail_ = false;
    if (width() < kSidebarBreakpoint) compact_sidebar_expanded_ = false;
    Reload();
    UpdateResponsiveLayout();
}

void MainWindow::ShowPasswordCreate() {
    if (!editor_panel_) return;
    QList<model::Category> cats;
    for (const auto& c : categories_) cats.append(c);
    editor_panel_->SetCategories(std::move(cats));
    editor_entry_id_ = -1;
    editor_panel_->OpenForCreate();
}

void MainWindow::OpenEditDialog(std::int64_t entry_id) {
    if (!editor_panel_) return;
    auto found = deps_.password_dao->FindById(entry_id);
    if (!found.has_value()) return;
    QList<model::Category> cats;
    for (const auto& c : categories_) cats.append(c);
    editor_panel_->SetCategories(std::move(cats));
    EditorPanel::DecryptedEntry decrypted{*found, DecryptPassword(*found)};
    editor_entry_id_ = entry_id;
    editor_panel_->OpenForEdit(decrypted);
}

void MainWindow::OnEditorSaveRequested() {
    if (!editor_panel_) return;
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
        updated.updated_at = QDateTime::currentMSecsSinceEpoch();
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
    auto reply = QMessageBox::question(
        this, QStringLiteral("删除确认"),
        QStringLiteral("确定将该密码移至回收站吗？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    if (!deps_.password_dao->LogicalDelete(
            entry_id, QDateTime::currentMSecsSinceEpoch())) {
        Toast::Show(this, QStringLiteral("删除失败"), Toast::Level::kError);
        return;
    }
    if (deps_.sync_scheduler) deps_.sync_scheduler->MarkDirty();
    if (detail_panel_) detail_panel_->ClearEntry();
    showing_detail_ = false;
    Reload();
    UpdateResponsiveLayout();
}

void MainWindow::OnCopyPasswordRequested(std::int64_t entry_id) {
    auto entry = FindEntry(entry_id);
    if (!entry) entry = deps_.password_dao->FindById(entry_id);
    if (!entry) return;
    const QString plain = DecryptPassword(*entry);
    if (plain.isEmpty()) {
        Toast::Show(this, QStringLiteral("无法解密密码"),
                    Toast::Level::kError);
        return;
    }
    ClipboardManager::Instance()->CopySensitive(plain);
    Toast::Show(this, QStringLiteral("密码已复制，%1 秒后清除")
                          .arg(ClipboardManager::Instance()->timeout_ms() /
                                1000),
                Toast::Level::kSuccess);
}

void MainWindow::OnCopyUsernameRequested(std::int64_t entry_id) {
    auto entry = FindEntry(entry_id);
    if (!entry) entry = deps_.password_dao->FindById(entry_id);
    if (!entry || entry->username.isEmpty()) return;
    ClipboardManager::Instance()->CopyPlain(entry->username);
    Toast::Show(this, QStringLiteral("用户名已复制"), Toast::Level::kSuccess);
}

void MainWindow::OnOpenWebsiteRequested(std::int64_t entry_id) {
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
    deps_.password_dao->SetFavorite(entry_id, desired);
    if (deps_.sync_scheduler) deps_.sync_scheduler->MarkDirty();
    Reload();
    if (detail_panel_) {
        auto entry = FindEntry(entry_id);
        if (entry) {
            detail_panel_->SetEntry(*entry, DecryptPassword(*entry));
        }
    }
}

void MainWindow::OnSyncNow() {
    if (!deps_.sync_scheduler) {
        Toast::Show(this, QStringLiteral("请先在设置中连接 Google Drive。"),
                    Toast::Level::kWarning);
        return;
    }
    deps_.sync_scheduler->SyncImmediately();
}

void MainWindow::OnSyncStarted() {
    UpdateSyncStatus(true, QStringLiteral("同步中..."));
    if (sync_status_dot_) {
        sync_status_dot_->setProperty("state", QStringLiteral("busy"));
        sync_status_dot_->style()->unpolish(sync_status_dot_);
        sync_status_dot_->style()->polish(sync_status_dot_);
    }
}

void MainWindow::OnSyncFinished(bool success, const QString& message) {
    UpdateSyncStatus(success, success ? QStringLiteral("已同步到云端")
                                       : QStringLiteral("同步失败：%1")
                                             .arg(message));
    Toast::Show(this,
                success ? QStringLiteral("同步完成")
                        : QStringLiteral("同步失败：%1").arg(message),
                success ? Toast::Level::kSuccess : Toast::Level::kError);
    Reload();
}

void MainWindow::UpdateSyncStatus(bool success, const QString& message) {
    if (sync_status_label_) sync_status_label_->setText(message);
    if (sync_status_dot_) {
        sync_status_dot_->setProperty(
            "state", success ? QStringLiteral("ok") : QStringLiteral("error"));
        sync_status_dot_->style()->unpolish(sync_status_dot_);
        sync_status_dot_->style()->polish(sync_status_dot_);
    }
}

void MainWindow::UpdateResponsiveLayout() {
    if (!sidebar_ || !workspace_content_layout_ || !password_list_column_ ||
        !detail_panel_ || !navigation_button_ || !back_to_list_button_ ||
        !search_container_) {
        return;
    }

    const int client_width = centralWidget() ? centralWidget()->width() : width();
    const bool desktop = client_width >= kDesktopBreakpoint;
    const bool compact = client_width < kSidebarBreakpoint;
    const bool show_detail =
        !desktop && showing_detail_ && detail_panel_->HasEntry();

    if (!compact) compact_sidebar_expanded_ = false;
    sidebar_->setVisible(!compact || compact_sidebar_expanded_);
    navigation_button_->setVisible(compact);
    back_to_list_button_->setVisible(show_detail);

    if (desktop) {
        password_list_column_->setMinimumWidth(kPasswordListMinWidth);
        password_list_column_->setVisible(true);
        detail_panel_->setMinimumWidth(kDetailWidth);
        detail_panel_->setMaximumWidth(kDetailWidth);
        detail_panel_->setSizePolicy(QSizePolicy::Fixed,
                                     QSizePolicy::Expanding);
        detail_panel_->setVisible(true);
        workspace_content_layout_->setStretchFactor(password_list_column_, 1);
        workspace_content_layout_->setStretchFactor(detail_panel_, 0);
        search_container_->setMinimumWidth(kSearchWidth);
        search_container_->setMaximumWidth(kSearchWidth);
        return;
    }

    password_list_column_->setMinimumWidth(0);
    password_list_column_->setVisible(!show_detail);
    detail_panel_->setMinimumWidth(0);
    detail_panel_->setMaximumWidth(QWIDGETSIZE_MAX);
    detail_panel_->setSizePolicy(QSizePolicy::Expanding,
                                 QSizePolicy::Expanding);
    detail_panel_->setVisible(show_detail);
    workspace_content_layout_->setStretchFactor(password_list_column_,
                                                show_detail ? 0 : 1);
    workspace_content_layout_->setStretchFactor(detail_panel_,
                                                show_detail ? 1 : 0);
    search_container_->setMinimumWidth(compact ? 120 : kSearchWidth);
    search_container_->setMaximumWidth(kSearchWidth);
}

void MainWindow::ShowListPane() {
    showing_detail_ = false;
    UpdateResponsiveLayout();
    if (password_list_) password_list_->setFocus();
}

void MainWindow::ToggleCompactSidebar() {
    if (width() >= kSidebarBreakpoint) return;
    compact_sidebar_expanded_ = !compact_sidebar_expanded_;
    UpdateResponsiveLayout();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    UpdateResponsiveLayout();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
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
