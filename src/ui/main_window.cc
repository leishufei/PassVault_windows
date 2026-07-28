#include "ui/main_window.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

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
#include "ui/editor_panel.h"
#include "ui/generator_dialog.h"
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
constexpr int kCardHeight = 68;

QString RelativeTimeShort(std::int64_t ms_since_epoch) {
    if (ms_since_epoch <= 0) return {};
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch();
    const std::int64_t diff = now - ms_since_epoch;
    if (diff < 60 * 1000) return QStringLiteral("刚刚");
    if (diff < 60 * 60 * 1000) {
        return QStringLiteral("%1 分钟").arg(diff / (60 * 1000));
    }
    if (diff < 24 * 60 * 60 * 1000LL) {
        return QStringLiteral("%1 小时").arg(diff / (60 * 60 * 1000));
    }
    const std::int64_t days = diff / (24 * 60 * 60 * 1000LL);
    if (days < 30) return QStringLiteral("%1 天").arg(days);
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
    root_layout->setContentsMargins(16, 16, 16, 16);
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
    ws_layout->addWidget(BuildSidebar());

    auto* workspace = new QWidget(workspace_page_);
    workspace->setObjectName(QStringLiteral("VaultWorkspace"));
    auto* workspace_layout = new QVBoxLayout(workspace);
    workspace_layout->setContentsMargins(0, 0, 0, 0);
    workspace_layout->setSpacing(0);
    workspace_layout->addWidget(BuildWorkspaceHeader());

    auto* content = new QWidget(workspace);
    content->setObjectName(QStringLiteral("VaultWorkspaceContent"));
    auto* content_layout = new QHBoxLayout(content);
    content_layout->setContentsMargins(0, 0, 0, 0);
    content_layout->setSpacing(0);
    content_layout->addWidget(BuildPasswordListColumn(), 1);
    content_layout->addWidget(BuildDetailColumn());
    workspace_layout->addWidget(content, 1);
    ws_layout->addWidget(workspace, 1);
    central_stack_->addWidget(workspace_page_);

    editor_panel_ = new EditorPanel(workspace_page_);
    connect(editor_panel_, &EditorPanel::SaveRequested, this,
            &MainWindow::OnEditorSaveRequested);
    connect(editor_panel_, &EditorPanel::CancelRequested, this,
            &MainWindow::OnEditorCancelRequested);
    connect(editor_panel_, &EditorPanel::GenerateRequested, this,
            &MainWindow::OnEditorGenerateRequested);

    preferences_page_ = new PreferencesPage(central_stack_);
    connect(preferences_page_, &PreferencesPage::BackRequested, this,
            [this]() { central_stack_->setCurrentWidget(workspace_page_); });
    central_stack_->addWidget(preferences_page_);

    root_layout->addWidget(container);
    setCentralWidget(root);

    if (deps_.sync_manager) {
        connect(deps_.sync_manager, &sync::SyncManager::SyncStarted, this,
                &MainWindow::OnSyncStarted);
        connect(deps_.sync_manager, &sync::SyncManager::SyncFinished, this,
                &MainWindow::OnSyncFinished);
    }
    Reload();
}

MainWindow::~MainWindow() = default;

QWidget* MainWindow::BuildSidebar() {
    auto* sidebar = new QFrame();
    sidebar->setObjectName(QStringLiteral("Sidebar"));
    sidebar->setFixedWidth(kSidebarWidth);
    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(12, 20, 12, 16);
    layout->setSpacing(14);

    auto* brand = new QWidget(sidebar);
    brand->setObjectName(QStringLiteral("SidebarBrand"));
    auto* brand_layout = new QHBoxLayout(brand);
    brand_layout->setContentsMargins(12, 0, 12, 0);
    brand_layout->setSpacing(10);

    auto* brand_badge = new QLabel(brand);
    brand_badge->setObjectName(QStringLiteral("SidebarBrandBadge"));
    brand_badge->setFixedSize(32, 32);
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
    layout->addWidget(new_button);

    sections_list_ = new QListWidget(sidebar);
    sections_list_->setObjectName(QStringLiteral("SidebarSectionList"));
    sections_list_->setFrameShape(QFrame::NoFrame);
    sections_list_->setIconSize(QSize(kSectionIconSize, kSectionIconSize));
    sections_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(sections_list_, &QListWidget::itemSelectionChanged, this,
            &MainWindow::OnSectionSelectionChanged);
    layout->addWidget(sections_list_);

    auto* divider = new QFrame(sidebar);
    divider->setObjectName(QStringLiteral("SidebarDivider"));
    divider->setFixedHeight(1);
    layout->addWidget(divider);

    auto* cat_header = new QWidget(sidebar);
    auto* cat_header_layout = new QHBoxLayout(cat_header);
    cat_header_layout->setContentsMargins(12, 0, 12, 0);
    cat_header_layout->setSpacing(6);
    auto* cat_title = new QLabel(QStringLiteral("分类"), cat_header);
    cat_title->setObjectName(QStringLiteral("SidebarSectionHeader"));
    cat_header_layout->addWidget(cat_title, 1);
    auto* add_cat = new QToolButton(cat_header);
    add_cat->setObjectName(QStringLiteral("SidebarAddCategoryButton"));
    add_cat->setToolTip(QStringLiteral("新增分类"));
    add_cat->setCursor(Qt::PointingHandCursor);
    add_cat->setFixedSize(24, 24);
    add_cat->setIcon(IconLoader::Load(
        QStringLiteral("plus"),
        ThemeManager::Instance()->Color(QStringLiteral("text-tertiary")),
        14));
    add_cat->setIconSize(QSize(14, 14));
    cat_header_layout->addWidget(add_cat);
    layout->addWidget(cat_header);

    categories_list_ = new QListWidget(sidebar);
    categories_list_->setObjectName(QStringLiteral("SidebarCategoryList"));
    categories_list_->setFrameShape(QFrame::NoFrame);
    categories_list_->setIconSize(QSize(kSectionIconSize, kSectionIconSize));
    categories_list_->setSelectionMode(QAbstractItemView::SingleSelection);
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
    cloud_icon->setPixmap(
        IconLoader::Load(
            QStringLiteral("cloud"),
            QColor(0x42, 0x81, 0xf2),
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

    auto* search_container = new QFrame(controls);
    search_container->setObjectName(QStringLiteral("SearchBarContainer"));
    search_container->setFixedSize(kSearchWidth, kSearchHeight);
    auto* search_layout = new QHBoxLayout(search_container);
    search_layout->setContentsMargins(0, 0, 8, 0);
    search_layout->setSpacing(6);

    search_ = new QLineEdit(search_container);
    search_->setObjectName(QStringLiteral("SearchBar"));
    search_->setPlaceholderText(QStringLiteral("搜索标题、用户名、网址或备注"));
    search_->setClearButtonEnabled(true);
    search_->addAction(
        IconLoader::Load(
            QStringLiteral("search"),
            ThemeManager::Instance()->Color(QStringLiteral("text-tertiary")),
            16),
        QLineEdit::LeadingPosition);
    connect(search_, &QLineEdit::textChanged, this,
            &MainWindow::OnSearchChanged);
    search_layout->addWidget(search_, 1);

    auto* shortcut = new QLabel(QStringLiteral("Ctrl + F"), search_container);
    shortcut->setObjectName(QStringLiteral("SearchShortcut"));
    shortcut->setAlignment(Qt::AlignCenter);
    search_layout->addWidget(shortcut);
    controls_layout->addWidget(search_container);
    controls_layout->addStretch(1);

    auto* lock_button = new QToolButton(controls);
    lock_button->setObjectName(QStringLiteral("HeaderLockButton"));
    lock_button->setToolTip(QStringLiteral("锁定保险库"));
    lock_button->setCursor(Qt::PointingHandCursor);
    lock_button->setFixedSize(36, 36);
    lock_button->setIcon(IconLoader::Load(
        QStringLiteral("lock-keyhole"),
        QColor(QStringLiteral("#3b78df")),
        18));
    lock_button->setIconSize(QSize(18, 18));
    connect(lock_button, &QToolButton::clicked, this,
            &MainWindow::LockRequested);
    controls_layout->addWidget(lock_button);

    auto* sep = new QFrame(controls);
    sep->setObjectName(QStringLiteral("HeaderSeparator"));
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedSize(1, 20);
    controls_layout->addWidget(sep);

    auto* more_button = new QToolButton(controls);
    more_button->setObjectName(QStringLiteral("HeaderMoreButton"));
    more_button->setToolTip(QStringLiteral("更多操作"));
    more_button->setCursor(Qt::PointingHandCursor);
    more_button->setFixedSize(36, 36);
    more_button->setIcon(IconLoader::Load(
        QStringLiteral("more-horizontal"),
        QColor(QStringLiteral("#6b7b8f")),
        18));
    more_button->setIconSize(QSize(18, 18));
    controls_layout->addWidget(more_button);

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

    auto* title = new QLabel(QStringLiteral("所有密码"), title_row);
    title->setObjectName(QStringLiteral("ListTitle"));
    title_layout->addWidget(title);
    list_count_ = new QLabel(QStringLiteral("0 项"), title_row);
    list_count_->setObjectName(QStringLiteral("ListCount"));
    title_layout->addWidget(list_count_);
    title_layout->addStretch(1);

    auto* sort_button = new QPushButton(QStringLiteral("按更新时间"), title_row);
    sort_button->setObjectName(QStringLiteral("ListSortMenu"));
    sort_button->setProperty("flat", true);
    sort_button->setCursor(Qt::PointingHandCursor);
    sort_button->setLayoutDirection(Qt::RightToLeft);
    sort_button->setIcon(IconLoader::Load(
        QStringLiteral("chevron-down"),
        QColor(QStringLiteral("#7e8c9b")),
        12));
    sort_button->setIconSize(QSize(12, 12));
    title_layout->addWidget(sort_button);

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
    const QColor icon_color =
        ThemeManager::Instance()->Color(QStringLiteral("text-secondary"));

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
            item->setText(
                counts[i] > 0
                    ? QStringLiteral("%1  %2")
                          .arg(defs[i].label)
                          .arg(counts[i])
                    : defs[i].label);
            item->setData(Qt::UserRole,
                          QVariant::fromValue<qint64>(defs[i].id));
            item->setIcon(
                IconLoader::Load(defs[i].icon, icon_color, kSectionIconSize));
        }
    } else {
        for (const auto& d : defs) {
            auto* item = new QListWidgetItem(sections_list_);
            item->setText(d.label);
            item->setData(Qt::UserRole, QVariant::fromValue<qint64>(d.id));
            item->setIcon(
                IconLoader::Load(d.icon, icon_color, kSectionIconSize));
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
    const QColor icon_color =
        ThemeManager::Instance()->Color(QStringLiteral("text-secondary"));
    for (const auto& c : categories_) {
        if (c.is_deleted) continue;
        auto* item = new QListWidgetItem(categories_list_);
        item->setText(c.name);
        item->setData(Qt::UserRole, QVariant::fromValue<qint64>(c.id));
        item->setIcon(IconLoader::Load(QStringLiteral("folder"), icon_color,
                                       kSectionIconSize));
        if (selected_category_ == c.id) {
            categories_list_->setCurrentItem(item);
        }
    }
    suppress_selection_ = false;
}

void MainWindow::OnSectionSelectionChanged() {
    if (suppress_selection_) return;
    auto* item = sections_list_->currentItem();
    if (!item) return;
    selected_category_ = item->data(Qt::UserRole).toLongLong();
    suppress_selection_ = true;
    if (categories_list_) categories_list_->clearSelection();
    suppress_selection_ = false;
    RefreshPasswordList();
}

void MainWindow::OnCategorySelectionChanged() {
    if (suppress_selection_) return;
    auto* item = categories_list_->currentItem();
    if (!item) return;
    selected_category_ = item->data(Qt::UserRole).toLongLong();
    suppress_selection_ = true;
    if (sections_list_) sections_list_->clearSelection();
    suppress_selection_ = false;
    RefreshPasswordList();
}

void MainWindow::OnSearchChanged(const QString& text) {
    search_text_ = text.trimmed();
    RefreshPasswordList();
}

QWidget* MainWindow::CreatePasswordCard(const model::PasswordEntry& entry) {
    auto* card = new QFrame();
    card->setObjectName(QStringLiteral("PasswordCard"));
    card->setFrameShape(QFrame::NoFrame);
    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(12, 10, 12, 10);
    row->setSpacing(12);

    auto* avatar = new QLabel(AvatarInitial(entry), card);
    avatar->setObjectName(QStringLiteral("PasswordCardIcon"));
    avatar->setFixedSize(36, 36);
    avatar->setAlignment(Qt::AlignCenter);
    row->addWidget(avatar);

    auto* text_col = new QVBoxLayout();
    text_col->setContentsMargins(0, 0, 0, 0);
    text_col->setSpacing(3);
    auto* title = new QLabel(
        entry.title.isEmpty() ? QStringLiteral("(无标题)") : entry.title, card);
    title->setObjectName(QStringLiteral("PasswordCardTitle"));
    title->setTextInteractionFlags(Qt::NoTextInteraction);
    text_col->addWidget(title);
    auto* meta = new QLabel(
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
            : QColor(QStringLiteral("#8b98a8"));
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
    const bool empty = current_entries_.empty();
    empty_state_->setVisible(empty);
    password_list_->setVisible(!empty);
    suppress_selection_ = false;

    if (detail_panel_ && detail_panel_->HasEntry()) {
        const std::int64_t current = detail_panel_->entry_id();
        auto still = FindEntry(current);
        if (!still.has_value()) {
            detail_panel_->ClearEntry();
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
    auto* item = password_list_->currentItem();
    if (!item) {
        if (detail_panel_) detail_panel_->ClearEntry();
        return;
    }
    const std::int64_t id = item->data(Qt::UserRole).toLongLong();
    auto entry = FindEntry(id);
    if (!entry.has_value()) return;
    detail_panel_->SetEntry(*entry, DecryptPassword(*entry));
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

void MainWindow::OnEditorGenerateRequested() {
    if (!editor_panel_) return;
    GeneratorDialog g(this);
    if (g.exec() == QDialog::Accepted) {
        editor_panel_->ApplyGeneratedPassword(g.password());
    }
}

void MainWindow::OnEditRequested(std::int64_t entry_id) {
    OpenEditDialog(entry_id);
}

void MainWindow::OnDeleteRequested(std::int64_t entry_id) {
    auto reply = QMessageBox::question(
        this, QStringLiteral("删除确认"),
        QStringLiteral("确定删除该密码吗？可从回收站恢复。"));
    if (reply != QMessageBox::Yes) return;
    deps_.password_dao->LogicalDelete(entry_id,
                                       QDateTime::currentMSecsSinceEpoch());
    if (deps_.sync_scheduler) deps_.sync_scheduler->MarkDirty();
    if (detail_panel_) detail_panel_->ClearEntry();
    Reload();
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
