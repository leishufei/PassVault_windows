#include <gtest/gtest.h>

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFont>
#include <QFontInfo>
#include <QFontMetrics>
#include <QImage>
#include <QSettings>
#include <QCheckBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLayout>
#include <QList>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTabWidget>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>

#include "crypto/crypto_service.h"
#include "crypto/secure_bytes.h"
#include "crypto/session_key.h"
#include "model/category.h"
#include "model/password_entry.h"
#include "storage/category_dao.h"
#include "storage/database.h"
#include "storage/password_dao.h"
#include "storage/schema.h"
#include "session/session_manager.h"
#include "sync/google_drive_provider.h"
#include "sync/sync_manager.h"
#include "sync/sync_scheduler.h"
#include "ui/detail_panel.h"
#include "ui/clipboard_manager.h"
#include "ui/editor_panel.h"
#include "ui/main_window.h"
#include "ui/preferences_page.h"
#include "ui/theme_manager.h"

namespace {

namespace crypto = passvault::crypto;
using passvault::model::Category;
using passvault::model::PasswordEntry;
using passvault::storage::CategoryDao;
using passvault::storage::Database;
using passvault::storage::EnsureCurrentSchema;
using passvault::storage::PasswordDao;
using passvault::ui::DetailPanel;
using passvault::ui::EditorPanel;
using passvault::ui::MainWindow;

constexpr int kSidebarCountRole = Qt::UserRole + 1;

QByteArray IconPixels(const QIcon& icon) {
    const QImage image = icon.pixmap(32, 32).toImage().convertToFormat(
        QImage::Format_ARGB32);
    return QCryptographicHash::hash(
        QByteArray(reinterpret_cast<const char*>(image.constBits()),
                   static_cast<qsizetype>(image.sizeInBytes())),
        QCryptographicHash::Sha256);
}

class ThemeGuard {
 public:
    ThemeGuard() : previous_(passvault::ui::ThemeManager::Instance()->theme()) {}
    ~ThemeGuard() {
        passvault::ui::ThemeManager::Instance()->ApplyTheme(previous_);
    }

 private:
    passvault::ui::Theme previous_;
};

void ExpectCenteredTextFits(const QWidget* widget, const QString& text) {
    const QFontMetrics metrics(widget->font());
    const QRect rect = widget->contentsRect();
    const QRect glyphs = metrics.tightBoundingRect(text);
    const int baseline = rect.top() + (rect.height() - metrics.height()) / 2 +
                         metrics.ascent();
    EXPECT_GE(baseline + glyphs.top(), rect.top());
    EXPECT_LE(baseline + glyphs.bottom(), rect.bottom());
    EXPECT_LE(metrics.horizontalAdvance(text), rect.width());
}

class MainWindowTest : public ::testing::Test {
 protected:
    void SetUp() override {
        db_ = Database::OpenInMemory();
        EnsureCurrentSchema(*db_);
        password_dao_ = std::make_unique<PasswordDao>(*db_);
        category_dao_ = std::make_unique<CategoryDao>(*db_);

        std::array<std::uint8_t, crypto::SessionKey::kSize> raw{};
        for (std::size_t i = 0; i < raw.size(); ++i) {
            raw[i] = static_cast<std::uint8_t>(i + 1);
        }
        session_key_ = crypto::SessionKey::FromSecureBytes(
            crypto::SecureBytes(raw.data(), raw.size()));
        ASSERT_TRUE(session_key_.has_value());

        Category work;
        work.name = QStringLiteral("Work");
        work.uuid = QStringLiteral("cat-work");
        const auto cat_id = category_dao_->Insert(work);
        ASSERT_TRUE(cat_id.has_value());
        work_category_id_ = *cat_id;

        // Updated-at descending puts GitHub first, GitLab second, and Work App third.
        Seed(QStringLiteral("GitHub"), QStringLiteral("octocat"), false, 0,
             3000);
        Seed(QStringLiteral("GitLab"), QStringLiteral("root"), true, 0, 2000);
        Seed(QStringLiteral("Work App"), QStringLiteral("me"), false,
             work_category_id_, 1000);

        MainWindow::Deps deps;
        deps.password_dao = password_dao_.get();
        deps.category_dao = category_dao_.get();
        deps.session_key = &session_key_.value();
        window_ = std::make_unique<MainWindow>(deps);
    }

    void Seed(const QString& title, const QString& user, bool favorite,
              std::int64_t category_id, std::int64_t created,
              std::int64_t updated = -1,
              const QString& notes = QString()) {
        PasswordEntry e;
        e.uuid = QStringLiteral("uuid-%1").arg(title);
        e.title = title;
        e.username = user;
        e.is_favorite = favorite;
        e.category_id = category_id;
        e.created_at = created;
        e.updated_at = updated >= 0 ? updated : created;
        e.notes = notes;
        QByteArray iv(static_cast<int>(crypto::CryptoService::kIvSize), 0x11);
        const QByteArray utf8 = QStringLiteral("pw-%1").arg(title).toUtf8();
        e.password_iv = iv;
        e.encrypted_password = crypto::CryptoService::EncryptGcm(
            session_key_->data(), session_key_->size(),
            reinterpret_cast<const std::uint8_t*>(iv.constData()), iv.size(),
            reinterpret_cast<const std::uint8_t*>(utf8.constData()),
            utf8.size());
        ASSERT_TRUE(password_dao_->Insert(e).has_value());
    }

    QLineEdit* search() {
        return window_->findChild<QLineEdit*>(QStringLiteral("SearchBar"));
    }
    QListWidget* sections() {
        return window_->findChild<QListWidget*>(
            QStringLiteral("SidebarSectionList"));
    }
    QListWidget* categories() {
        return window_->findChild<QListWidget*>(
            QStringLiteral("SidebarCategoryList"));
    }
    QListWidget* passwords() {
        return window_->findChild<QListWidget*>(QStringLiteral("PasswordList"));
    }
    QLabel* listCount() {
        return window_->findChild<QLabel*>(QStringLiteral("ListCount"));
    }
    QPushButton* newButton() {
        return window_->findChild<QPushButton*>(
            QStringLiteral("NewPasswordButton"));
    }
    QToolButton* lockButton() {
        return window_->findChild<QToolButton*>(
            QStringLiteral("HeaderLockButton"));
    }
    QPushButton* settingsButton() {
        return window_->findChild<QPushButton*>(
            QStringLiteral("SidebarSettingsButton"));
    }
    DetailPanel* detail() { return window_->findChild<DetailPanel*>(); }
    EditorPanel* editor() { return window_->findChild<EditorPanel*>(); }

    std::unique_ptr<Database> db_;
    std::unique_ptr<PasswordDao> password_dao_;
    std::unique_ptr<CategoryDao> category_dao_;
    std::optional<crypto::SessionKey> session_key_;
    std::int64_t work_category_id_ = 0;
    std::unique_ptr<MainWindow> window_;
};

TEST_F(MainWindowTest, RendersSeedEntriesWithCount) {
    EXPECT_EQ(passwords()->count(), 3);
    EXPECT_EQ(listCount()->text(), QStringLiteral("3 项"));
}

TEST_F(MainWindowTest, SidebarBrandBadgeUsesFigmaSizeAndBackground) {
    ThemeGuard guard;
    passvault::ui::ThemeManager::Instance()->ApplyTheme(
        passvault::ui::Theme::kLight);
    window_->show();
    QApplication::processEvents();

    auto* badge = window_->findChild<QLabel*>(
        QStringLiteral("SidebarBrandBadge"));
    ASSERT_NE(badge, nullptr);
    EXPECT_EQ(badge->size(), QSize(34, 34));

    QImage image(badge->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    badge->render(&image);
    const QColor accent(QStringLiteral("#276cf0"));
    int accent_pixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) == accent) ++accent_pixels;
        }
    }
    EXPECT_GT(accent_pixels, 500);
}

TEST_F(MainWindowTest, ThemeRefreshPreservesSelectionAndChangesIcons) {
    ThemeGuard guard;
    auto* theme = passvault::ui::ThemeManager::Instance();
    theme->ApplyTheme(passvault::ui::Theme::kLight);

    passwords()->setCurrentRow(1);
    const std::int64_t selected_id = detail()->entry_id();
    ASSERT_GT(selected_id, 0);
    auto* settings = settingsButton();
    ASSERT_NE(settings, nullptr);
    const QByteArray light_icon = IconPixels(settings->icon());

    theme->ApplyTheme(passvault::ui::Theme::kDark);

    EXPECT_NE(IconPixels(settings->icon()), light_icon);
    EXPECT_EQ(detail()->entry_id(), selected_id);
    ASSERT_NE(passwords()->currentItem(), nullptr);
    EXPECT_EQ(passwords()->currentItem()->data(Qt::UserRole).toLongLong(),
              selected_id);
}

TEST_F(MainWindowTest, SidebarActionsAreIconButtonsWithTooltips) {
    window_->show();
    QApplication::processEvents();
    auto* sync = window_->findChild<QPushButton*>("SidebarSyncButton");
    ASSERT_NE(sync, nullptr);
    EXPECT_EQ(sync->text(), QStringLiteral("云端同步"));
    EXPECT_EQ(settingsButton()->text(), QStringLiteral("设置"));
    EXPECT_FALSE(sync->icon().isNull());
    EXPECT_FALSE(settingsButton()->icon().isNull());
    EXPECT_EQ(settingsButton()->toolTip(), QStringLiteral("设置"));
    EXPECT_EQ(sync->toolTip(), QStringLiteral("立即同步"));
    EXPECT_EQ(sync->geometry().left(), settingsButton()->geometry().left());
    EXPECT_EQ(sync->width(), settingsButton()->width());
    EXPECT_GT(settingsButton()->geometry().top(), sync->geometry().bottom());
    settingsButton()->click();
    EXPECT_TRUE(window_->preferences_page()->isVisible());
}

TEST_F(MainWindowTest, SidebarSyncDispatchesOnceAndRestoresButtonAfterFailure) {
    namespace sync = passvault::sync;
    // A null session fails before any provider or network access.
    sync::GoogleDriveProvider provider([] { return QString(); });
    sync::SyncManager manager(password_dao_.get(), category_dao_.get(), nullptr);
    manager.set_provider(&provider);
    sync::SyncScheduler scheduler(&manager);
    MainWindow::Deps deps;
    deps.password_dao = password_dao_.get();
    deps.category_dao = category_dao_.get();
    deps.session_key = &*session_key_;
    deps.sync_manager = &manager;
    deps.sync_scheduler = &scheduler;
    MainWindow window(deps);
    auto* button = window.findChild<QPushButton*>("SidebarSyncButton");
    ASSERT_NE(button, nullptr);
    QSignalSpy triggered(&scheduler, &sync::SyncScheduler::SyncTriggered);
    QSignalSpy finished(&manager, &sync::SyncManager::SyncFinished);
    QObject::connect(&scheduler, &sync::SyncScheduler::SyncTriggered,
                     &window, [button]() {
                         EXPECT_FALSE(button->isEnabled());
                         EXPECT_EQ(button->property("state").toString(), "busy");
                         EXPECT_EQ(button->toolTip(), QStringLiteral("同步中…"));
                         button->click();
                     });
    button->click();
    EXPECT_EQ(triggered.count(), 1);
    EXPECT_EQ(finished.count(), 1);
    EXPECT_TRUE(button->isEnabled());
    EXPECT_EQ(button->property("state").toString(), "error");
    EXPECT_TRUE(button->toolTip().contains(QStringLiteral("同步失败")));

    manager.SyncStarted();
    EXPECT_FALSE(button->isEnabled());
    manager.SyncFinished(sync::SyncManager::StatusKind::kSuccess, QString());
    EXPECT_TRUE(button->isEnabled());
    EXPECT_EQ(button->property("state").toString(), "ok");
    EXPECT_TRUE(button->toolTip().contains(QStringLiteral("已同步到云端")));

    scheduler.SetPaused(true);
    button->click();
    EXPECT_EQ(triggered.count(), 1);
    EXPECT_TRUE(button->isEnabled());
}

TEST_F(MainWindowTest, SidebarSyncWithoutConnectionDoesNotStayBusy) {
    auto* button = window_->findChild<QPushButton*>("SidebarSyncButton");
    ASSERT_NE(button, nullptr);
    button->click();
    EXPECT_TRUE(button->isEnabled());
    EXPECT_EQ(button->property("state").toString(), "error");
    EXPECT_TRUE(button->toolTip().contains(QStringLiteral("连接 Google 账号")));
}

TEST_F(MainWindowTest, WorkspaceHeaderSpansListAndDetail) {
    window_->resize(1280, 800);
    window_->show();
    QApplication::processEvents();

    auto* root = window_->findChild<QWidget*>(
        QStringLiteral("WorkspaceRoot"));
    auto* container = window_->findChild<QWidget*>(
        QStringLiteral("WorkspaceContainer"));
    auto* workspace = window_->findChild<QWidget*>(
        QStringLiteral("VaultWorkspace"));
    auto* header = window_->findChild<QWidget*>(
        QStringLiteral("VaultWorkspaceHeader"));
    auto* content = window_->findChild<QWidget*>(
        QStringLiteral("VaultWorkspaceContent"));
    auto* list_column = window_->findChild<QWidget*>(
        QStringLiteral("PasswordListColumn"));
    auto* divider = window_->findChild<QWidget*>(
        QStringLiteral("VaultWorkspaceHeaderDivider"));
    auto* search_container = window_->findChild<QWidget*>(
        QStringLiteral("SearchBarContainer"));
    auto* separator = window_->findChild<QWidget*>(
        QStringLiteral("HeaderSeparator"));
    auto* more_button = window_->findChild<QToolButton*>(
        QStringLiteral("HeaderMoreButton"));
    ASSERT_NE(root, nullptr);
    ASSERT_NE(container, nullptr);
    ASSERT_NE(workspace, nullptr);
    ASSERT_NE(header, nullptr);
    ASSERT_NE(content, nullptr);
    ASSERT_NE(list_column, nullptr);
    ASSERT_NE(divider, nullptr);
    ASSERT_NE(search_container, nullptr);
    ASSERT_NE(separator, nullptr);
    ASSERT_NE(more_button, nullptr);
    ASSERT_NE(detail(), nullptr);

    EXPECT_TRUE(root->layout()->contentsMargins().isNull());
    EXPECT_EQ(container->geometry(), root->rect());
    EXPECT_EQ(header->parentWidget(), workspace);
    EXPECT_EQ(content->parentWidget(), workspace);
    EXPECT_EQ(header->height(), 64);
    EXPECT_EQ(header->width(), content->width());
    EXPECT_EQ(content->y(), header->geometry().bottom() + 1);
    EXPECT_EQ(divider->height(), 1);
    EXPECT_EQ(divider->width(), header->width());
    EXPECT_EQ(search_container->height(), 36);
    EXPECT_GE(search_container->width(), 260);
    EXPECT_LE(search_container->width(), 420);
    EXPECT_EQ(search_container->x(), 24);
    EXPECT_GT(lockButton()->x(), search_container->geometry().right());
    EXPECT_EQ(lockButton()->geometry().center().y(),
              search_container->geometry().center().y());
    EXPECT_EQ(separator->geometry().center().y(),
              search_container->geometry().center().y());
    EXPECT_EQ(more_button->geometry().center().y(),
              search_container->geometry().center().y());
    EXPECT_EQ(detail()->parentWidget(), content);
    EXPECT_EQ(list_column->y(), detail()->y());
    EXPECT_GE(detail()->x(), list_column->geometry().right());
    EXPECT_EQ(detail()->height(), content->height());
}

TEST_F(MainWindowTest, SidebarCountsAreSeparateFromLabels) {
    ASSERT_EQ(sections()->count(), 4);
    EXPECT_EQ(sections()->item(0)->text(), QStringLiteral("全部密码"));
    EXPECT_EQ(sections()->item(0)->data(kSidebarCountRole).toInt(), 3);
    EXPECT_EQ(sections()->item(1)->text(), QStringLiteral("收藏夹"));
    EXPECT_EQ(sections()->item(1)->data(kSidebarCountRole).toInt(), 1);
    EXPECT_EQ(sections()->item(2)->data(kSidebarCountRole).toInt(), 2);
    EXPECT_EQ(sections()->item(3)->data(kSidebarCountRole).toInt(), 0);

    ASSERT_EQ(categories()->count(), 1);
    EXPECT_EQ(categories()->item(0)->text(), QStringLiteral("Work"));
    EXPECT_EQ(categories()->item(0)->data(kSidebarCountRole).toInt(), 1);
}

TEST_F(MainWindowTest, AddCategoryButtonIsAvailable) {
    auto* add_button = window_->findChild<QToolButton*>(
        QStringLiteral("SidebarAddCategoryButton"));
    ASSERT_NE(add_button, nullptr);
    EXPECT_TRUE(add_button->isEnabled());
    EXPECT_EQ(add_button->toolTip(), QStringLiteral("新增分类"));
}

TEST_F(MainWindowTest, PlaceholderHeaderActionsAreDisabled) {
    auto* more = window_->findChild<QToolButton*>(
        QStringLiteral("HeaderMoreButton"));
    auto* sort =
        window_->findChild<QPushButton*>(QStringLiteral("ListSortMenu"));
    ASSERT_NE(more, nullptr);
    ASSERT_NE(sort, nullptr);
    EXPECT_FALSE(more->isEnabled());
    EXPECT_TRUE(sort->isEnabled());
}

TEST_F(MainWindowTest, PasswordCardsUseTargetDensityAndListStateLayer) {
    window_->resize(1280, 800);
    window_->show();
    QApplication::processEvents();

    ASSERT_GT(passwords()->count(), 1);
    EXPECT_EQ(passwords()->visualItemRect(passwords()->item(0)).height(), 64);
    EXPECT_EQ(passwords()->spacing(), 6);
    auto* card = passwords()->itemWidget(passwords()->item(0));
    auto* fav_card = passwords()->itemWidget(passwords()->item(1));
    ASSERT_NE(card, nullptr);
    ASSERT_NE(fav_card, nullptr);
    EXPECT_FALSE(card->testAttribute(Qt::WA_TransparentForMouseEvents));
    EXPECT_EQ(card->findChild<QToolButton*>("PasswordCardDeleteButton"), nullptr);
    EXPECT_EQ(card->findChild<QLabel*>("PasswordCardStar"), nullptr);
    EXPECT_NE(fav_card->findChild<QLabel*>("PasswordCardStar"), nullptr);
    EXPECT_EQ(fav_card->findChild<QToolButton*>("PasswordCardDeleteButton"),
              nullptr);
    const QString stylesheet =
        passvault::ui::ThemeManager::Instance()->LoadStyleSheet();
    EXPECT_TRUE(
        stylesheet.contains(QStringLiteral("#PasswordList::item:focus")));
    EXPECT_TRUE(stylesheet.contains(
        QStringLiteral("padding: 6px 18px 20px 18px")));
}

TEST_F(MainWindowTest, LongCardIdentityElidesAndKeepsFullValues) {
    const QString long_title = QStringLiteral(
        "Long Identity Entry With A Deliberately Extended Title For Clipping "
        "Verification 0123456789").repeated(3);
    const QString long_username = QStringLiteral(
        "long.account.identifier.for.stage.two.acceptance.with.extra.characters"
        "@example.invalid").repeated(3);
    Seed(long_title, long_username, false, 0, 4000);
    window_->Reload();
    window_->resize(1280, 800);
    window_->show();
    QApplication::processEvents();

    auto* card = passwords()->itemWidget(passwords()->item(0));
    ASSERT_NE(card, nullptr);
    auto* title =
        card->findChild<QLabel*>(QStringLiteral("PasswordCardTitle"));
    auto* username =
        card->findChild<QLabel*>(QStringLiteral("PasswordCardMeta"));
    ASSERT_NE(title, nullptr);
    ASSERT_NE(username, nullptr);
    EXPECT_NE(title->text(), long_title);
    EXPECT_TRUE(title->text().endsWith(QChar(0x2026)));
    EXPECT_EQ(title->toolTip(), long_title);
    EXPECT_EQ(title->accessibleName(), long_title);
    EXPECT_NE(username->text(), long_username);
    EXPECT_TRUE(username->text().endsWith(QChar(0x2026)));
    EXPECT_EQ(username->toolTip(), long_username);
    EXPECT_EQ(username->accessibleName(), long_username);
}

TEST_F(MainWindowTest, SearchFocusUpdatesContainerState) {
    window_->show();
    QApplication::processEvents();

    auto* container = window_->findChild<QWidget*>(
        QStringLiteral("SearchBarContainer"));
    ASSERT_NE(container, nullptr);
    search()->setFocus();
    QApplication::processEvents();
    EXPECT_TRUE(container->property("focused").toBool());

    newButton()->setFocus();
    QApplication::processEvents();
    EXPECT_FALSE(container->property("focused").toBool());
}

TEST_F(MainWindowTest, SearchIncludesNotesAndShowsSearchEmptyState) {
    Seed(QStringLiteral("Documentation"), QStringLiteral("reader"), false, 0,
         4000, 4000, QStringLiteral("stage-two-unique-note"));
    window_->Reload();

    search()->setText(QStringLiteral("stage-two-unique-note"));
    EXPECT_EQ(passwords()->count(), 1);
    auto* title = window_->findChild<QLabel*>(QStringLiteral("ListTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text(), QStringLiteral("搜索结果"));

    search()->setText(QStringLiteral("no-such-stage-two-entry"));
    auto* empty = window_->findChild<QLabel*>(QStringLiteral("EmptyState"));
    ASSERT_NE(empty, nullptr);
    EXPECT_FALSE(empty->isHidden());
    EXPECT_EQ(empty->text(), QStringLiteral("没有搜索结果"));
}

TEST_F(MainWindowTest, ListUsesUpdatedTimeAndRelativeLabel) {
    const std::int64_t updated =
        QDateTime::currentMSecsSinceEpoch() - 2 * 60 * 1000;
    Seed(QStringLiteral("Recently Updated"), QStringLiteral("recent"), false,
         0, 1, updated);
    window_->Reload();
    window_->resize(1280, 800);
    window_->show();
    QApplication::processEvents();

    auto* card = passwords()->itemWidget(passwords()->item(0));
    ASSERT_NE(card, nullptr);
    auto* title =
        card->findChild<QLabel*>(QStringLiteral("PasswordCardTitle"));
    auto* time =
        card->findChild<QLabel*>(QStringLiteral("PasswordCardTime"));
    ASSERT_NE(title, nullptr);
    ASSERT_NE(time, nullptr);
    EXPECT_EQ(title->text(), QStringLiteral("Recently Updated"));
    EXPECT_EQ(time->text(), QStringLiteral("2 分钟前"));
}

TEST_F(MainWindowTest, SearchFiltersAndClears) {
    search()->setText(QStringLiteral("git"));
    EXPECT_EQ(passwords()->count(), 2);
    search()->setText(QString());
    EXPECT_EQ(passwords()->count(), 3);
}

TEST_F(MainWindowTest, EmptyUsernameStaysBlankInTheListAndDetail) {
    Seed(QStringLiteral("Password only"), QString(), false, 0, 5000);
    window_->Reload();
    auto* card = passwords()->itemWidget(passwords()->item(0));
    ASSERT_NE(card, nullptr);
    auto* username = card->findChild<QLabel*>("PasswordCardMeta");
    ASSERT_NE(username, nullptr);
    EXPECT_TRUE(username->text().isEmpty());
    passwords()->setCurrentRow(0);
    EXPECT_TRUE(detail()->findChild<QLabel*>("DetailFieldValue")->text().isEmpty());
}

TEST_F(MainWindowTest, EmptyStatesKeepHeaderAndFillTheSameContentArea) {
    window_->show();
    QApplication::processEvents();
    auto* title = window_->findChild<QLabel*>("ListTitle");
    auto* sort = window_->findChild<QPushButton*>("ListSortMenu");
    auto* empty = window_->findChild<QLabel*>("EmptyState");
    ASSERT_NE(title, nullptr);
    ASSERT_NE(sort, nullptr);
    ASSERT_NE(empty, nullptr);
    const QPoint title_position = title->mapTo(window_.get(), QPoint());
    const QPoint count_position = listCount()->mapTo(window_.get(), QPoint());
    const QPoint sort_position = sort->mapTo(window_.get(), QPoint());
    const QRect content_rect(passwords()->mapTo(window_.get(), QPoint()),
                             passwords()->size());
    const auto expect_fixed_header = [&]() {
        EXPECT_EQ(title->mapTo(window_.get(), QPoint()), title_position);
        EXPECT_EQ(listCount()->mapTo(window_.get(), QPoint()).y(),
                  count_position.y());
        EXPECT_EQ(sort->mapTo(window_.get(), QPoint()).y(), sort_position.y());
        EXPECT_EQ(QRect(empty->mapTo(window_.get(), QPoint()), empty->size()),
                  content_rect);
        EXPECT_EQ(listCount()->text(), QStringLiteral("0 项"));
    };
    search()->setText(QStringLiteral("no-matching-entry"));
    QApplication::processEvents();
    EXPECT_TRUE(empty->isVisible());
    EXPECT_FALSE(passwords()->isVisible());
    EXPECT_EQ(empty->text(), QStringLiteral("没有搜索结果"));
    expect_fixed_header();

    search()->clear();
    sections()->setCurrentRow(3);
    QApplication::processEvents();
    EXPECT_EQ(empty->text(), QStringLiteral("回收站为空"));
    expect_fixed_header();

    for (const auto& entry : password_dao_->ListActive()) {
        ASSERT_TRUE(password_dao_->LogicalDelete(entry.id, 5000));
    }
    sections()->setCurrentRow(0);
    window_->Reload();
    QApplication::processEvents();
    EXPECT_EQ(empty->text(), QStringLiteral("暂无密码"));
    expect_fixed_header();
    RecordProperty("header_y", title_position.y());
    RecordProperty("content_height", content_rect.height());
}

TEST_F(MainWindowTest, MouseCreateFocusesTitleAndPreservesKeyboardNavigation) {
    window_->show();
    QApplication::processEvents();
    QTest::mouseClick(newButton(), Qt::LeftButton);
    auto* title = editor()->findChild<QLineEdit*>("EditorTitleInput");
    ASSERT_NE(title, nullptr);
    EXPECT_TRUE(title->hasFocus());
    EXPECT_FALSE(newButton()->hasFocus());
    EXPECT_TRUE(newButton()->focusPolicy() & Qt::TabFocus);
    QTest::keyClick(title, Qt::Key_Tab);
    EXPECT_TRUE(editor()->findChild<QLineEdit*>("EditorUsernameInput")->hasFocus());
}

TEST_F(MainWindowTest, SearchEscapeAndBlankClicksKeepTheFilter) {
    window_->show();
    QApplication::processEvents();
    search()->setText(QStringLiteral("Git"));
    passwords()->setCurrentRow(0);
    ASSERT_TRUE(detail()->HasEntry());
    search()->setFocus();
    QAction global_escape(window_.get());
    global_escape.setShortcut(QKeySequence(Qt::Key_Escape));
    window_->addAction(&global_escape);
    QSignalSpy shortcut(&global_escape, &QAction::triggered);
    QTest::keyClick(search(), Qt::Key_Escape);
    EXPECT_EQ(shortcut.count(), 0);
    window_->removeAction(&global_escape);
    auto* workspace = window_->findChild<QWidget*>("VaultWorkspace");
    ASSERT_NE(workspace, nullptr);
    EXPECT_TRUE(workspace->hasFocus());
    EXPECT_TRUE(detail()->HasEntry());
    EXPECT_EQ(search()->text(), QStringLiteral("Git"));
    EXPECT_EQ(passwords()->count(), 2);

    search()->setFocus();
    QTest::mouseClick(passwords()->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(10, passwords()->viewport()->height() - 10));
    EXPECT_TRUE(workspace->hasFocus());
    EXPECT_FALSE(detail()->HasEntry());
    EXPECT_EQ(search()->text(), QStringLiteral("Git"));
    EXPECT_EQ(passwords()->count(), 2);

    search()->setText(QStringLiteral("no-matching-entry"));
    search()->setFocus();
    auto* empty = window_->findChild<QLabel*>("EmptyState");
    ASSERT_NE(empty, nullptr);
    QTest::mouseClick(empty, Qt::LeftButton);
    EXPECT_TRUE(workspace->hasFocus());
    EXPECT_EQ(search()->text(), QStringLiteral("no-matching-entry"));
}

TEST_F(MainWindowTest, EditorAndModalEscapeDoNotCloseTheSelectedDetail) {
    window_->show();
    QApplication::processEvents();
    passwords()->setCurrentRow(0);
    ASSERT_TRUE(detail()->HasEntry());
    QTest::mouseClick(newButton(), Qt::LeftButton);
    auto* title = editor()->findChild<QLineEdit*>("EditorTitleInput");
    ASSERT_NE(title, nullptr);
    QTest::keyClick(title, Qt::Key_Escape);
    EXPECT_FALSE(editor()->IsOpen());
    EXPECT_TRUE(detail()->HasEntry());

    QMessageBox message(QMessageBox::Question, QStringLiteral("Test"),
                         QStringLiteral("Cancel this operation?"),
                         QMessageBox::Yes | QMessageBox::No, window_.get());
    message.setEscapeButton(QMessageBox::No);
    message.setWindowModality(Qt::WindowModal);
    message.show();
    QApplication::processEvents();
    QTest::keyClick(&message, Qt::Key_Escape);
    EXPECT_EQ(message.result(), QMessageBox::No);
    EXPECT_TRUE(detail()->HasEntry());
}

TEST_F(MainWindowTest, FavoritesSectionShowsOnlyFavorites) {
    // Rows are all, favorites, uncategorized, and trash.
    sections()->setCurrentRow(1);
    EXPECT_EQ(passwords()->count(), 1);
}

TEST_F(MainWindowTest, CategorySelectionFilters) {
    ASSERT_EQ(categories()->count(), 1);
    categories()->setCurrentRow(0);
    EXPECT_EQ(passwords()->count(), 1);
}

TEST_F(MainWindowTest, SelectingRowPopulatesDetail) {
    passwords()->setCurrentRow(0);
    ASSERT_TRUE(detail()->HasEntry());
    const std::int64_t row_id =
        passwords()->item(0)->data(Qt::UserRole).toLongLong();
    EXPECT_EQ(detail()->entry_id(), row_id);
}

// UI Automation can select through the model without setting currentItem().
TEST_F(MainWindowTest, SelectionWithoutCurrentItemPopulatesDetail) {
    auto* list = passwords();
    list->selectionModel()->select(list->model()->index(0, 0),
                                   QItemSelectionModel::ClearAndSelect);
    ASSERT_TRUE(detail()->HasEntry());
    const std::int64_t row_id =
        list->item(0)->data(Qt::UserRole).toLongLong();
    EXPECT_EQ(detail()->entry_id(), row_id);
}

TEST_F(MainWindowTest, NewButtonOpensEditor) {
    EXPECT_FALSE(editor()->IsOpen());
    newButton()->click();
    EXPECT_TRUE(editor()->IsOpen());
}

TEST_F(MainWindowTest, NewPasswordButtonMatchesFigmaTypography) {
    window_->show();
    auto* button = newButton();
    ASSERT_NE(button, nullptr);
    button->ensurePolished();
    QApplication::processEvents();

    const QFontInfo button_font_info(button->font());
    EXPECT_EQ(button_font_info.family(), QStringLiteral("Noto Sans SC"));
    EXPECT_EQ(button_font_info.pixelSize(), 14);
    EXPECT_EQ(button_font_info.weight(), 600);
}

TEST_F(MainWindowTest, SearchShortcutUsesBodyTypography) {
    window_->show();
    auto* shortcut = window_->findChild<QLabel*>(
        QStringLiteral("SearchShortcut"));
    ASSERT_NE(shortcut, nullptr);
    shortcut->ensurePolished();
    QApplication::processEvents();

    const QFontInfo shortcut_font_info(shortcut->font());
    EXPECT_EQ(shortcut_font_info.family(), QStringLiteral("DM Mono"));
    EXPECT_EQ(shortcut_font_info.pixelSize(), 11);
}

TEST_F(MainWindowTest, SharedShellTypographyFitsWithoutClipping) {
    window_->show();
    QApplication::processEvents();

    auto* shortcut = window_->findChild<QLabel*>(
        QStringLiteral("SearchShortcut"));
    ASSERT_NE(shortcut, nullptr);
    ExpectCenteredTextFits(newButton(), newButton()->text());
    ExpectCenteredTextFits(shortcut, shortcut->text());
}

TEST_F(MainWindowTest, CtrlNOpensEditor) {
    QTest::keyClick(window_.get(), Qt::Key_N, Qt::ControlModifier);
    EXPECT_TRUE(editor()->IsOpen());
}

TEST_F(MainWindowTest, CreateFlowInsertsAndReloads) {
    newButton()->click();
    auto* ed = editor();
    ed->findChild<QLineEdit*>(QStringLiteral("EditorTitleInput"))
        ->setText(QStringLiteral("NewEntry"));
    ed->findChild<QLineEdit*>(QStringLiteral("EditorUsernameInput"))
        ->setText(QStringLiteral("newuser"));
    ed->findChild<QLineEdit*>(QStringLiteral("EditorPasswordInput"))
        ->setText(QStringLiteral("password123"));
    ed->findChild<QPushButton*>(QStringLiteral("EditorSaveButton"))->click();

    EXPECT_EQ(password_dao_->ListActive().size(), 4u);
    EXPECT_EQ(passwords()->count(), 4);
}

TEST_F(MainWindowTest, FavoriteToggleFromDetailPersists) {
    passwords()->setCurrentRow(0);
    const std::int64_t id = detail()->entry_id();
    ASSERT_GT(id, 0);
    const bool before = password_dao_->FindById(id)->is_favorite;

    QToolButton* favorite = nullptr;
    for (auto* button : detail()->findChildren<QToolButton*>()) {
        if (button->isCheckable() &&
            button->toolTip() == QStringLiteral("收藏")) {
            favorite = button;
            break;
        }
    }
    ASSERT_NE(favorite, nullptr);
    window_->show();
    QApplication::processEvents();
    EXPECT_TRUE(favorite->isVisible());
    favorite->click();

    EXPECT_EQ(password_dao_->FindById(id)->is_favorite, !before);
}

TEST_F(MainWindowTest, DeleteConfirmationDefaultsToNoAndCancelPreservesEntry) {
    passwords()->setCurrentRow(0);
    const std::int64_t id = detail()->entry_id();
    ASSERT_GT(id, 0);
    bool saw_default_no = false;
    bool saw_truthful_message = false;

    QTimer::singleShot(0, [&saw_default_no, &saw_truthful_message]() {
        auto* message =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(message, nullptr);
        auto* no_button = message->button(QMessageBox::No);
        saw_default_no = message->defaultButton() == no_button;
        saw_truthful_message =
            message->text() == QStringLiteral("确定将该密码移至回收站吗？");
        ASSERT_NE(no_button, nullptr);
        no_button->click();
    });
    detail()
        ->findChild<QToolButton*>(QStringLiteral("DetailHeaderDeleteButton"))
        ->click();

    EXPECT_TRUE(saw_default_no);
    EXPECT_TRUE(saw_truthful_message);
    ASSERT_TRUE(password_dao_->FindById(id).has_value());
    EXPECT_FALSE(password_dao_->FindById(id)->is_deleted);
    EXPECT_TRUE(detail()->HasEntry());
    EXPECT_EQ(detail()->entry_id(), id);
}

TEST_F(MainWindowTest, DeleteConfirmationLogicallyDeletesEntry) {
    passwords()->setCurrentRow(0);
    const std::int64_t id = detail()->entry_id();
    ASSERT_GT(id, 0);

    QTimer::singleShot(0, []() {
        auto* message =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(message, nullptr);
        auto* yes_button = message->button(QMessageBox::Yes);
        ASSERT_NE(yes_button, nullptr);
        yes_button->click();
    });
    detail()
        ->findChild<QToolButton*>(QStringLiteral("DetailHeaderDeleteButton"))
        ->click();

    ASSERT_TRUE(password_dao_->FindById(id).has_value());
    EXPECT_TRUE(password_dao_->FindById(id)->is_deleted);
    EXPECT_FALSE(detail()->HasEntry());
    EXPECT_EQ(passwords()->count(), 2);
}

TEST_F(MainWindowTest, DeleteFailurePreservesDetailAndShowsError) {
    passwords()->setCurrentRow(0);
    const std::int64_t id = detail()->entry_id();
    ASSERT_GT(id, 0);
    ASSERT_TRUE(db_->Execute("DROP TABLE password_entries"));

    QTimer::singleShot(0, []() {
        auto* message =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(message, nullptr);
        auto* yes_button = message->button(QMessageBox::Yes);
        ASSERT_NE(yes_button, nullptr);
        yes_button->click();
    });
    detail()
        ->findChild<QToolButton*>(QStringLiteral("DetailHeaderDeleteButton"))
        ->click();

    EXPECT_TRUE(detail()->HasEntry());
    EXPECT_EQ(detail()->entry_id(), id);
    EXPECT_NE(window_->findChild<QWidget*>(QStringLiteral("ToastError")),
              nullptr);
}

TEST_F(MainWindowTest, LockButtonEmitsLockRequested) {
    QSignalSpy spy(window_.get(), &MainWindow::LockRequested);
    lockButton()->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(MainWindowTest, CtrlLEmitsLockRequested) {
    QSignalSpy spy(window_.get(), &MainWindow::LockRequested);
    QTest::keyClick(window_.get(), Qt::Key_L, Qt::ControlModifier);
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(MainWindowTest, SettingsButtonSwitchesToPreferencesPage) {
    settingsButton()->click();
    auto* stack = window_->findChild<QStackedWidget*>();
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(stack->currentWidget(), window_->preferences_page());
}

TEST_F(MainWindowTest, BlankClickClearsSelectionAndAllowsSameRowToReopen) {
    window_->show();
    QApplication::processEvents();
    passwords()->setCurrentRow(0);
    const QPoint blank(20, passwords()->viewport()->height() - 10);
    ASSERT_EQ(passwords()->itemAt(blank), nullptr);
    QTest::mouseClick(passwords()->viewport(), Qt::LeftButton, Qt::NoModifier,
                      blank);
    EXPECT_TRUE(passwords()->selectedItems().isEmpty());
    EXPECT_EQ(passwords()->currentItem(), nullptr);
    EXPECT_FALSE(detail()->HasEntry());
    EXPECT_TRUE(detail()->isVisible());
    QTest::mouseClick(passwords()->viewport(), Qt::LeftButton, Qt::NoModifier,
                      passwords()->visualItemRect(passwords()->item(0)).center());
    EXPECT_TRUE(detail()->HasEntry());
    EXPECT_TRUE(detail()->isVisible());
}

TEST_F(MainWindowTest, EscapeFromListClearsSelectionAndDetail) {
    window_->show();
    passwords()->setCurrentRow(0);
    QTest::keyClick(passwords(), Qt::Key_Escape);
    EXPECT_TRUE(passwords()->selectedItems().isEmpty());
    EXPECT_EQ(passwords()->currentItem(), nullptr);
    EXPECT_FALSE(detail()->HasEntry());
    EXPECT_TRUE(detail()->isVisible());
}

TEST_F(MainWindowTest, ClearingSelectionDoesNotReopenCurrentPassword) {
    passwords()->setCurrentRow(0);
    passwords()->clearSelection();
    EXPECT_EQ(passwords()->currentItem(), nullptr);
    EXPECT_FALSE(detail()->HasEntry());
}

TEST_F(MainWindowTest, LockClearsEditorAndDetailAndRebindingAllowsValidWrites) {
    passwords()->setCurrentRow(0);
    newButton()->click();
    editor()->ApplyGeneratedPassword(QStringLiteral("temporary-value"));
    window_->SetSessionKey(nullptr);
    EXPECT_FALSE(detail()->HasEntry());
    EXPECT_TRUE(detail()->findChild<QLabel*>("DetailFieldValueMono")->text().isEmpty());
    EXPECT_FALSE(editor()->IsOpen());
    EXPECT_TRUE(editor()->findChild<QLineEdit*>("EditorPasswordInput")->text().isEmpty());
    newButton()->click();
    EXPECT_FALSE(editor()->IsOpen());

    std::array<std::uint8_t, crypto::SessionKey::kSize> raw{};
    for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<std::uint8_t>(i + 1);
    session_key_.reset();
    session_key_ = crypto::SessionKey::FromSecureBytes(
        crypto::SecureBytes(raw.data(), raw.size()));
    window_->SetSessionKey(&*session_key_);
    newButton()->click();
    editor()->findChild<QLineEdit*>("EditorTitleInput")->setText("After unlock");
    editor()->ApplyGeneratedPassword(QString(19, QLatin1Char('a')));
    editor()->findChild<QPushButton*>("EditorSaveButton")->click();
    ASSERT_EQ(password_dao_->ListActive().size(), 4u);
    passwords()->setCurrentRow(0);
    detail()->findChildren<QToolButton*>("DetailFieldButton").at(2)->click();
    EXPECT_TRUE(QApplication::clipboard()->text() == QString(19, QLatin1Char('a')));
    QApplication::clipboard()->clear();
}

TEST_F(MainWindowTest, UndecryptableEntryCannotBeEditedOrOverwritten) {
    auto entry = password_dao_->ListActive().front();
    entry.encrypted_password = QByteArray(20, 'x');
    ASSERT_TRUE(password_dao_->Update(entry));
    window_->Reload();
    ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnEditRequested",
        Q_ARG(std::int64_t, entry.id)));
    EXPECT_FALSE(editor()->IsOpen());
    EXPECT_EQ(password_dao_->FindById(entry.id)->encrypted_password,
              entry.encrypted_password);
    EXPECT_NE(window_->findChild<QWidget*>("ToastError"), nullptr);
}

TEST_F(MainWindowTest, EditRefreshesSelectedDetailAndFavoriteTimestamp) {
    passwords()->setCurrentRow(0);
    const auto id = detail()->entry_id();
    ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnEditRequested",
        Q_ARG(std::int64_t, id)));
    editor()->ApplyGeneratedPassword(QString(23, QLatin1Char('a')));
    editor()->findChild<QLineEdit*>("EditorUsernameInput")->setText("new-user");
    editor()->findChild<QPushButton*>("EditorSaveButton")->click();
    EXPECT_EQ(detail()->findChild<QLabel*>("DetailFieldValueMono")->text().size(), 23);
    EXPECT_EQ(detail()->findChild<QLabel*>("DetailFieldValue")->accessibleName(),
              QStringLiteral("new-user"));
    const auto before = password_dao_->FindById(id)->updated_at;
    QTest::qWait(2);
    ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnFavoriteToggleRequested",
        Q_ARG(std::int64_t, id), Q_ARG(bool, true)));
    EXPECT_GT(password_dao_->FindById(id)->updated_at, before);
}

TEST_F(MainWindowTest, SortMenuSupportsBothDirectionsForEveryField) {
    Seed("Zebra", "user", false, 0, 9000, 1);
    window_->Reload();
    auto* sort = window_->findChild<QPushButton*>("ListSortMenu");
    ASSERT_NE(sort->menu(), nullptr);
    auto* ascending = sort->findChild<QAction*>("SortAscendingAction");
    auto* descending = sort->findChild<QAction*>("SortDescendingAction");
    ASSERT_NE(ascending, nullptr);
    ASSERT_NE(descending, nullptr);
    EXPECT_TRUE(descending->isChecked());
    const auto titles = [this] {
        QStringList result;
        for (int i = 0; i < passwords()->count(); ++i) {
            const auto id = passwords()->item(i)->data(Qt::UserRole).toLongLong();
            result.append(password_dao_->FindById(id)->title);
        }
        return result;
    };
    const QStringList expected[] = {
        {"Zebra", "Work App", "GitLab", "GitHub"},
        {"Work App", "GitLab", "GitHub", "Zebra"},
        {"GitHub", "GitLab", "Work App", "Zebra"},
    };
    for (int field = 0; field < 3; ++field) {
        sort->menu()->actions()[field]->trigger();
        ascending->trigger();
        EXPECT_TRUE(ascending->isChecked());
        EXPECT_FALSE(descending->isChecked());
        EXPECT_EQ(titles(), expected[field]);
        EXPECT_TRUE(sort->text().endsWith(QStringLiteral("升序")));
        descending->trigger();
        auto reversed = expected[field];
        std::reverse(reversed.begin(), reversed.end());
        EXPECT_EQ(titles(), reversed);
        EXPECT_FALSE(ascending->isChecked());
        EXPECT_TRUE(descending->isChecked());
        EXPECT_TRUE(sort->text().endsWith(QStringLiteral("降序")));
    }
}

TEST_F(MainWindowTest, RenameCategoryPersistsWithoutChangingItsPasswords) {
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        dialog->setTextValue("Renamed");
        dialog->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnRenameCategory",
        Q_ARG(std::int64_t, work_category_id_)));
    EXPECT_EQ(category_dao_->FindById(work_category_id_)->name,
              QStringLiteral("Renamed"));
    EXPECT_EQ(password_dao_->ListByCategory(work_category_id_).size(), 1u);
}

TEST_F(MainWindowTest, DeleteCategoryTrashesAllItsPasswordsAndPreservesCiphertext) {
    Seed("Second work", "user", false, work_category_id_, 1000);
    window_->Reload();
    const auto before = password_dao_->ListByCategory(work_category_id_);
    ASSERT_EQ(before.size(), 2u);
    categories()->setCurrentRow(0);
    passwords()->setCurrentRow(0);
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        EXPECT_EQ(dialog->defaultButton(), dialog->button(QMessageBox::No));
        EXPECT_TRUE(dialog->text().contains(QStringLiteral("回收站")));
        dialog->button(QMessageBox::Yes)->click();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnDeleteCategory",
        Q_ARG(std::int64_t, work_category_id_)));
    EXPECT_TRUE(category_dao_->FindById(work_category_id_)->is_deleted);
    EXPECT_EQ(password_dao_->ListActive().size(), 2u);
    EXPECT_FALSE(detail()->HasEntry());
    for (const auto& entry : before) {
        const auto after = password_dao_->FindById(entry.id);
        ASSERT_TRUE(after.has_value());
        EXPECT_EQ(after->category_id, work_category_id_);
        EXPECT_TRUE(after->is_deleted);
        EXPECT_EQ(after->encrypted_password, entry.encrypted_password);
        EXPECT_EQ(after->password_iv, entry.password_iv);
        EXPECT_GT(after->updated_at, entry.updated_at);
    }
    sections()->setCurrentRow(3);
    EXPECT_EQ(passwords()->count(), 2);
}

TEST_F(MainWindowTest, FailedCategoryDeleteRollsBackPasswordDeletion) {
    ASSERT_TRUE(db_->Execute("CREATE TRIGGER fail_category_delete BEFORE UPDATE "
                            "ON categories BEGIN SELECT RAISE(ABORT, 'blocked'); END"));
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        dialog->button(QMessageBox::Yes)->click();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnDeleteCategory",
        Q_ARG(std::int64_t, work_category_id_)));
    EXPECT_FALSE(category_dao_->FindById(work_category_id_)->is_deleted);
    EXPECT_EQ(password_dao_->ListByCategory(work_category_id_).size(), 1u);
    EXPECT_NE(window_->findChild<QWidget*>("ToastError"), nullptr);
}

TEST_F(MainWindowTest, FailedPasswordDeletionPreservesCategoryAndPasswords) {
    ASSERT_TRUE(db_->Execute("CREATE TRIGGER fail_password_delete BEFORE UPDATE "
                            "ON password_entries BEGIN SELECT RAISE(ABORT, 'blocked'); END"));
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        dialog->button(QMessageBox::Yes)->click();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnDeleteCategory",
        Q_ARG(std::int64_t, work_category_id_)));
    EXPECT_FALSE(category_dao_->FindById(work_category_id_)->is_deleted);
    EXPECT_EQ(password_dao_->ListByCategory(work_category_id_).size(), 1u);
    EXPECT_NE(window_->findChild<QWidget*>("ToastError"), nullptr);
}

TEST_F(MainWindowTest, CategoryDragOrderSurvivesReload) {
    Category second;
    second.uuid = "second-category";
    second.name = "Second";
    second.sort_order = 1;
    const auto id = category_dao_->Insert(second);
    ASSERT_TRUE(id.has_value());
    window_->Reload();
    EXPECT_EQ(categories()->dragDropMode(), QAbstractItemView::InternalMove);
    ASSERT_TRUE(categories()->model()->moveRow(QModelIndex(), 0, QModelIndex(), 2));
    QApplication::processEvents();
    const auto ordered = category_dao_->ListActive();
    ASSERT_EQ(ordered.size(), 2u);
    EXPECT_EQ(ordered.front().id, *id);
    window_->Reload();
    EXPECT_EQ(categories()->item(0)->data(Qt::UserRole).toLongLong(), *id);
}

TEST_F(MainWindowTest, CategoryMutationsScheduleSyncWithoutContactingProvider) {
    passvault::sync::GoogleDriveProvider provider([] { return QString(); });
    passvault::sync::SyncManager manager(password_dao_.get(), category_dao_.get(),
        passvault::session::SessionManager::Instance());
    manager.set_provider(&provider);
    passvault::sync::SyncScheduler scheduler(&manager);
    MainWindow::Deps deps;
    deps.password_dao = password_dao_.get();
    deps.category_dao = category_dao_.get();
    deps.session_key = &*session_key_;
    deps.sync_scheduler = &scheduler;
    window_ = std::make_unique<MainWindow>(deps);
    QSignalSpy scheduled(&scheduler, &passvault::sync::SyncScheduler::SyncScheduled);
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        dialog->setTextValue("Synced category");
        dialog->accept();
    });
    QMetaObject::invokeMethod(window_.get(), "OnAddCategory");
    EXPECT_EQ(scheduled.count(), 1);
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        dialog->setTextValue("Renamed work");
        dialog->accept();
    });
    QMetaObject::invokeMethod(window_.get(), "OnRenameCategory",
        Q_ARG(std::int64_t, work_category_id_));
    EXPECT_EQ(scheduled.count(), 2);
    ASSERT_TRUE(categories()->model()->moveRow(QModelIndex(), 0, QModelIndex(), 2));
    QApplication::processEvents();
    EXPECT_EQ(scheduled.count(), 3);
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        dialog->button(QMessageBox::Yes)->click();
    });
    QMetaObject::invokeMethod(window_.get(), "OnDeleteCategory",
        Q_ARG(std::int64_t, work_category_id_));
    EXPECT_EQ(scheduled.count(), 4);
    window_.reset();
}

TEST_F(MainWindowTest, CancelCategoryDeletePreservesCategoryAndPasswords) {
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        dialog->button(QMessageBox::No)->click();
    });
    QMetaObject::invokeMethod(window_.get(), "OnDeleteCategory",
        Q_ARG(std::int64_t, work_category_id_));
    EXPECT_FALSE(category_dao_->FindById(work_category_id_)->is_deleted);
    EXPECT_EQ(password_dao_->ListByCategory(work_category_id_).size(), 1u);
}

// Import/export/change-master signals are emitted by PreferencesPage and are
// covered by preferences_page_test.

TEST_F(MainWindowTest, ControlAndShiftSelectWithoutOpeningMultipleDetails) {
    window_->show();
    QApplication::processEvents();
    auto click_row = [this](int row, Qt::KeyboardModifiers modifiers) {
        const QRect rect = passwords()->visualItemRect(passwords()->item(row));
        QTest::mouseClick(passwords()->viewport(), Qt::LeftButton, modifiers,
                          QPoint(75, rect.center().y()));
    };
    click_row(0, Qt::NoModifier);
    const auto displayed = detail()->entry_id();
    click_row(1, Qt::ControlModifier);
    EXPECT_EQ(passwords()->selectedItems().size(), 2);
    EXPECT_TRUE(detail()->isVisible());
    EXPECT_EQ(detail()->entry_id(), displayed);
    click_row(2, Qt::ShiftModifier);
    EXPECT_GE(passwords()->selectedItems().size(), 2);
    QTest::keyClick(passwords(), Qt::Key_Escape);
    EXPECT_TRUE(passwords()->selectedItems().isEmpty());
    EXPECT_EQ(passwords()->currentItem(), nullptr);
}

TEST_F(MainWindowTest, DeleteKeyTrashesAllSelectedAndPreservesCiphertext) {
    window_->show();
    QApplication::processEvents();
    passwords()->setCurrentRow(0);
    passwords()->item(1)->setSelected(true);
    const auto first = password_dao_->FindById(
        passwords()->item(0)->data(Qt::UserRole).toLongLong());
    const auto second = password_dao_->FindById(
        passwords()->item(1)->data(Qt::UserRole).toLongLong());
    QTimer::singleShot(0, []() {
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(message, nullptr);
        EXPECT_TRUE(message->text().contains(QStringLiteral("2 条")));
        message->button(QMessageBox::Yes)->click();
    });
    QTest::keyClick(passwords(), Qt::Key_Delete);
    for (const auto& before : {first, second}) {
        ASSERT_TRUE(before);
        const auto after = password_dao_->FindById(before->id);
        ASSERT_TRUE(after);
        EXPECT_TRUE(after->is_deleted);
        EXPECT_EQ(after->encrypted_password, before->encrypted_password);
        EXPECT_EQ(after->password_iv, before->password_iv);
    }
    EXPECT_EQ(passwords()->count(), 1);
    EXPECT_FALSE(detail()->HasEntry());
}

TEST_F(MainWindowTest, BatchDeleteFailureAndCancelKeepAllSelections) {
    passwords()->setCurrentRow(0);
    passwords()->item(1)->setSelected(true);
    ASSERT_TRUE(db_->Execute(
        "CREATE TRIGGER fail_second_delete BEFORE UPDATE ON password_entries "
        "WHEN OLD.title = 'GitLab' BEGIN SELECT RAISE(ABORT, 'test'); END"));
    for (const auto answer : {QMessageBox::No, QMessageBox::Yes}) {
        QTimer::singleShot(0, [answer]() {
            auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            ASSERT_NE(message, nullptr);
            message->button(answer)->click();
        });
        ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnDeleteSelected"));
        EXPECT_EQ(password_dao_->ListActive().size(), 3u);
        EXPECT_EQ(passwords()->selectedItems().size(), 2);
    }
}

TEST_F(MainWindowTest, RowDeleteOnlyTrashesItsOwnRecord) {
    window_->show();
    QApplication::processEvents();
    passwords()->setCurrentRow(0);
    passwords()->item(1)->setSelected(true);
    const auto id = passwords()->item(2)->data(Qt::UserRole).toLongLong();
    auto* card = passwords()->itemWidget(passwords()->item(2));
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->findChild<QToolButton*>("PasswordCardDeleteButton"), nullptr);
    QTimer::singleShot(0, []() {
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(message, nullptr);
        message->button(QMessageBox::Yes)->click();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnDeleteRequested",
                                          Q_ARG(std::int64_t, id)));
    EXPECT_TRUE(password_dao_->FindById(id)->is_deleted);
    EXPECT_EQ(password_dao_->ListActive().size(), 2u);
}

TEST_F(MainWindowTest, ContextMenuDeletesTheExistingMultipleSelection) {
    window_->show();
    QApplication::processEvents();
    passwords()->setCurrentRow(0);
    passwords()->item(1)->setSelected(true);
    QTimer::singleShot(0, [this]() {
        auto* menu = window_->findChild<QMenu*>("PasswordContextMenu");
        ASSERT_NE(menu, nullptr);
        ASSERT_EQ(menu->actions().size(), 1);
        QTimer::singleShot(0, []() {
            auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            ASSERT_NE(message, nullptr);
            message->button(QMessageBox::Yes)->click();
        });
        menu->actions().front()->trigger();
        menu->close();
    });
    const auto rect = passwords()->visualItemRect(passwords()->item(1));
    passwords()->customContextMenuRequested(QPoint(75, rect.center().y()));
    EXPECT_EQ(password_dao_->ListActive().size(), 1u);
}

TEST_F(MainWindowTest, EscapeFromSettingsReturnsToWorkspace) {
    window_->show();
    QApplication::processEvents();
    settingsButton()->click();
    auto* tabs = window_->preferences_page()->findChild<QTabWidget*>("PreferencesTabs");
    ASSERT_NE(tabs, nullptr);
    tabs->setFocus();
    QTest::keyClick(tabs, Qt::Key_Escape);
    EXPECT_FALSE(window_->preferences_page()->isVisible());
    EXPECT_TRUE(passwords()->isVisible());
}

TEST_F(MainWindowTest, MinimizeLockRespectsSettingAndSession) {
    QSignalSpy locked(window_.get(), &MainWindow::LockRequested);
    QSettings().setValue("session/lock_on_minimize", false);
    window_->show();
    window_->showMinimized();
    QApplication::processEvents();
    EXPECT_EQ(locked.count(), 0);
    window_->showNormal();
    QSettings().setValue("session/lock_on_minimize", true);
    window_->showMinimized();
    QApplication::processEvents();
    EXPECT_EQ(locked.count(), 1);
    window_->showNormal();
    window_->SetSessionKey(nullptr);
    window_->showMinimized();
    QApplication::processEvents();
    EXPECT_EQ(locked.count(), 1);
    QSettings().setValue("session/lock_on_minimize", false);
}

}  // namespace

TEST_F(MainWindowTest, TrashSearchStaysDeletedAndRestorePreservesCiphertext) {
    const auto all = password_dao_->ListActive();
    const auto original = all.front();
    ASSERT_TRUE(password_dao_->LogicalDelete(original.id, 5000));
    sections()->setCurrentRow(3);
    window_->Reload();
    search()->setText(QStringLiteral("Git"));
    ASSERT_EQ(passwords()->count(), 1);
    EXPECT_EQ(passwords()->item(0)->data(Qt::UserRole).toLongLong(), original.id);
    auto* restore = window_->findChild<QPushButton*>("RestoreSelectedButton");
    auto* remove = window_->findChild<QPushButton*>("PermanentlyDeleteButton");
    ASSERT_NE(restore, nullptr);
    ASSERT_NE(remove, nullptr);
    EXPECT_FALSE(restore->isEnabled());
    EXPECT_FALSE(remove->isEnabled());
    passwords()->setCurrentRow(0);
    EXPECT_TRUE(restore->isEnabled());
    EXPECT_TRUE(remove->isEnabled());
    ASSERT_TRUE(detail()->HasEntry());
    EXPECT_EQ(detail()->findChild<QToolButton*>("DetailHeaderDeleteButton")->toolTip(),
              QStringLiteral("永久删除"));
    ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnEditRequested",
                                         Q_ARG(std::int64_t, original.id)));
    EXPECT_FALSE(editor()->IsOpen());
    ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnFavoriteToggleRequested",
        Q_ARG(std::int64_t, original.id), Q_ARG(bool, true)));
    EXPECT_EQ(password_dao_->FindById(original.id)->is_favorite, original.is_favorite);
    restore->click();
    const auto restored = password_dao_->FindById(original.id);
    ASSERT_TRUE(restored);
    EXPECT_FALSE(restored->is_deleted);
    EXPECT_EQ(restored->encrypted_password, original.encrypted_password);
    EXPECT_EQ(restored->password_iv, original.password_iv);
    EXPECT_EQ(passwords()->count(), 0);
    EXPECT_EQ(listCount()->text(), QStringLiteral("0 项"));
    EXPECT_EQ(sections()->item(3)->data(kSidebarCountRole).toInt(), 0);
    EXPECT_EQ(search()->text(), QStringLiteral("Git"));
}

TEST_F(MainWindowTest, FilteredEmptyTrashConfirmsAllAndCancelPreservesContent) {
    for (const auto& entry : password_dao_->ListActive()) {
        ASSERT_TRUE(password_dao_->LogicalDelete(entry.id, 5000));
    }
    sections()->setCurrentRow(3);
    window_->Reload();
    search()->setText(QStringLiteral("GitHub"));
    ASSERT_EQ(passwords()->count(), 1);
    auto* clear = window_->findChild<QPushButton*>("EmptyTrashButton");
    ASSERT_NE(clear, nullptr);
    ASSERT_TRUE(clear->isEnabled());
    for (const auto answer : {QMessageBox::No, QMessageBox::Yes}) {
        QTimer::singleShot(0, [answer]() {
            auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            ASSERT_NE(message, nullptr);
            EXPECT_TRUE(message->text().contains(QStringLiteral("3 条")));
            EXPECT_EQ(message->defaultButton(), message->button(QMessageBox::No));
            message->button(answer)->click();
        });
        clear->click();
        EXPECT_EQ(password_dao_->ListIncludingDeleted().size(),
                  answer == QMessageBox::No ? 3u : 0u);
    }
    EXPECT_EQ(password_dao_->ListDeletionMarkers()->size(), 3u);
    EXPECT_FALSE(clear->isEnabled());
    EXPECT_EQ(search()->text(), QStringLiteral("GitHub"));
}

TEST_F(MainWindowTest, EmptyTrashKeepsEntriesAddedWhileConfirmationIsOpen) {
    const auto confirmed_entries = password_dao_->ListActive();
    for (const auto& entry : confirmed_entries) {
        ASSERT_TRUE(password_dao_->LogicalDelete(entry.id, 5000));
    }
    sections()->setCurrentRow(3);
    window_->Reload();
    search()->setText(QStringLiteral("GitHub"));
    ASSERT_EQ(passwords()->count(), 1);
    auto* clear = window_->findChild<QPushButton*>("EmptyTrashButton");
    ASSERT_NE(clear, nullptr);
    QTimer::singleShot(0, [this]() {
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(message, nullptr);
        EXPECT_TRUE(message->text().contains(QStringLiteral("3 条")));
        Seed(QStringLiteral("late-entry"), QStringLiteral("synthetic"), false, 0, 6000);
        const auto added = password_dao_->ListActive();
        ASSERT_EQ(added.size(), 1u);
        ASSERT_TRUE(password_dao_->LogicalDelete(added.front().id, 7000));
        message->button(QMessageBox::Yes)->click();
    });
    clear->click();
    const auto remaining = password_dao_->ListIncludingDeleted();
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining.front().title, QStringLiteral("late-entry"));
    EXPECT_TRUE(remaining.front().is_deleted);
    const auto markers = password_dao_->ListDeletionMarkers();
    ASSERT_TRUE(markers);
    EXPECT_EQ(markers->size(), confirmed_entries.size());
    for (const auto& entry : confirmed_entries) {
        EXPECT_FALSE(password_dao_->FindByUuid(entry.uuid));
        EXPECT_TRUE(std::any_of(markers->begin(), markers->end(),
            [&entry](const auto& marker) { return marker.uuid == entry.uuid; }));
    }
    EXPECT_EQ(passwords()->count(), 0);
    EXPECT_EQ(sections()->item(3)->data(kSidebarCountRole).toInt(), 1);
    EXPECT_TRUE(clear->isEnabled());
}

TEST_F(MainWindowTest, TrashControlShiftSelectionAndDeleteKeyRemoveActualSelection) {
    for (const auto& entry : password_dao_->ListActive()) {
        ASSERT_TRUE(password_dao_->LogicalDelete(entry.id, 5000));
    }
    sections()->setCurrentRow(3);
    window_->Reload();
    window_->show();
    QApplication::processEvents();
    const auto click_row = [this](int row, Qt::KeyboardModifiers modifiers) {
        const auto rect = passwords()->visualItemRect(passwords()->item(row));
        QTest::mouseClick(passwords()->viewport(), Qt::LeftButton, modifiers,
                          QPoint(75, rect.center().y()));
    };
    click_row(0, Qt::NoModifier);
    QTest::keyClick(passwords(), Qt::Key_Escape);
    EXPECT_FALSE(window_->findChild<QPushButton*>("RestoreSelectedButton")->isEnabled());
    EXPECT_FALSE(window_->findChild<QPushButton*>("PermanentlyDeleteButton")->isEnabled());
    click_row(0, Qt::NoModifier);
    click_row(2, Qt::ControlModifier);
    EXPECT_EQ(passwords()->selectedItems().size(), 2);
    click_row(0, Qt::NoModifier);
    click_row(1, Qt::ShiftModifier);
    ASSERT_EQ(passwords()->selectedItems().size(), 2);
    EXPECT_EQ(detail()->entry_id(), passwords()->item(0)->data(Qt::UserRole).toLongLong());
    QTimer::singleShot(0, []() {
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(message, nullptr);
        EXPECT_TRUE(message->text().contains(QStringLiteral("2 条")));
        message->button(QMessageBox::Yes)->click();
    });
    QTest::keyClick(passwords(), Qt::Key_Delete);
    EXPECT_EQ(passwords()->count(), 1);
    EXPECT_EQ(password_dao_->ListDeletionMarkers()->size(), 2u);
    EXPECT_EQ(sections()->item(3)->data(kSidebarCountRole).toInt(), 1);
}

TEST_F(MainWindowTest, TrashContextMenuRestoresMultipleSelectionAndHasPermanentDelete) {
    for (const auto& entry : password_dao_->ListActive()) {
        ASSERT_TRUE(password_dao_->LogicalDelete(entry.id, 5000));
    }
    sections()->setCurrentRow(3);
    window_->Reload();
    window_->show();
    QApplication::processEvents();
    passwords()->setCurrentRow(0);
    passwords()->item(1)->setSelected(true);
    QTimer::singleShot(0, [this]() {
        auto* menu = window_->findChild<QMenu*>("PasswordContextMenu");
        ASSERT_NE(menu, nullptr);
        ASSERT_EQ(menu->actions().size(), 2);
        EXPECT_EQ(menu->actions()[0]->text(), QStringLiteral("还原"));
        EXPECT_EQ(menu->actions()[1]->text(), QStringLiteral("永久删除"));
        menu->actions().front()->trigger();
        menu->close();
    });
    const auto rect = passwords()->visualItemRect(passwords()->item(1));
    passwords()->customContextMenuRequested(QPoint(75, rect.center().y()));
    EXPECT_EQ(password_dao_->ListActive().size(), 2u);
    EXPECT_EQ(passwords()->count(), 1);
}

TEST_F(MainWindowTest, TrashRowActionsScheduleDebouncedSyncAndPreserveUnselectedRows) {
    for (const auto& entry : password_dao_->ListActive()) {
        ASSERT_TRUE(password_dao_->LogicalDelete(entry.id, 5000));
    }
    passvault::sync::GoogleDriveProvider provider([] { return QString(); });
    passvault::sync::SyncManager manager(password_dao_.get(), category_dao_.get(),
        passvault::session::SessionManager::Instance());
    manager.set_provider(&provider);
    passvault::sync::SyncScheduler scheduler(&manager);
    EXPECT_EQ(scheduler.debounce_ms(), 5000);
    MainWindow::Deps deps;
    deps.password_dao = password_dao_.get();
    deps.category_dao = category_dao_.get();
    deps.session_key = &*session_key_;
    deps.sync_scheduler = &scheduler;
    window_ = std::make_unique<MainWindow>(deps);
    QSignalSpy scheduled(&scheduler, &passvault::sync::SyncScheduler::SyncScheduled);
    sections()->setCurrentRow(3);
    auto* card = passwords()->itemWidget(passwords()->item(0));
    auto* restore = card->findChild<QToolButton*>("PasswordCardRestoreButton");
    ASSERT_NE(restore, nullptr);
    restore->click();
    EXPECT_EQ(scheduled.count(), 1);
    EXPECT_EQ(passwords()->count(), 2);
    EXPECT_EQ(password_dao_->ListActive().size(), 1u);
    card = passwords()->itemWidget(passwords()->item(0));
    auto* remove = card->findChild<QToolButton*>("PasswordCardDeleteButton");
    ASSERT_NE(remove, nullptr);
    QTimer::singleShot(0, []() {
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(message, nullptr);
        EXPECT_TRUE(message->text().contains(QStringLiteral("1 条")));
        message->button(QMessageBox::Yes)->click();
    });
    remove->click();
    EXPECT_EQ(scheduled.count(), 2);
    EXPECT_EQ(passwords()->count(), 1);
    EXPECT_EQ(password_dao_->ListDeletionMarkers()->size(), 1u);
    EXPECT_TRUE(scheduler.IsPending());
    window_.reset();
}

TEST_F(MainWindowTest, MigrationBlocksWritesClearsEditorAndDefersExit) {
    window_->show();
    newButton()->click();
    auto* title = editor()->findChild<QLineEdit*>(QStringLiteral("EditorTitleInput"));
    ASSERT_NE(title, nullptr);
    title->setText(QStringLiteral("pending isolated edit"));
    auto* password = editor()->findChild<QLineEdit*>(QStringLiteral("EditorPasswordInput"));
    ASSERT_NE(password, nullptr);
    password->setText(QStringLiteral("isolated pending password"));
    const auto before = password_dao_->ReadAll();
    ASSERT_TRUE(before);
    QSignalSpy exit(window_.get(), &MainWindow::DeferredExitRequested);
    passvault::ui::ClipboardManager::Instance()->CopySensitive(QStringLiteral("isolated clipboard"));
    window_->SetMigrationActive(true);
    EXPECT_TRUE(QApplication::clipboard()->text().isEmpty());
    EXPECT_TRUE(title->text().isEmpty());
    EXPECT_TRUE(password->text().isEmpty());
    EXPECT_FALSE(detail()->HasEntry());
    EXPECT_FALSE(newButton()->isEnabled());
    EXPECT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnEditorSaveRequested"));
    QMetaObject::invokeMethod(window_.get(), "OnNewPassword");
    QMetaObject::invokeMethod(window_.get(), "OnFavoriteToggleRequested",
        Q_ARG(std::int64_t, before->front().id), Q_ARG(bool, true));
    EXPECT_FALSE(window_->close());
    EXPECT_EQ(exit.count(), 1);
    EXPECT_TRUE(window_->isVisible());
    const auto after = password_dao_->ReadAll();
    ASSERT_TRUE(after);
    ASSERT_EQ(before->size(), after->size());
    for (std::size_t i = 0; i < after->size(); ++i) {
        EXPECT_EQ(after->at(i).encrypted_password, before->at(i).encrypted_password);
        EXPECT_EQ(after->at(i).updated_at, before->at(i).updated_at);
        EXPECT_EQ(after->at(i).is_favorite, before->at(i).is_favorite);
    }
    window_->SetMigrationActive(false);
    window_->SetSessionKey(&*session_key_);
    newButton()->click();
    EXPECT_TRUE(newButton()->isEnabled());
    EXPECT_TRUE(editor()->isVisible());
}

TEST_F(MainWindowTest, ModifierGestureNeverOpensOrReplacesDetail) {
    window_->show();
    QApplication::processEvents();
    auto* list = passwords();
    const auto first = list->visualItemRect(list->item(0)).center();
    const auto second = list->visualItemRect(list->item(1)).center();
    QTest::mousePress(list->viewport(), Qt::LeftButton, Qt::ControlModifier, first);
    EXPECT_FALSE(detail()->HasEntry());
    QTest::mouseRelease(list->viewport(), Qt::LeftButton, Qt::NoModifier, first);
    EXPECT_FALSE(detail()->HasEntry());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, first);
    ASSERT_TRUE(detail()->HasEntry());
    const auto id = detail()->entry_id();
    QTest::mousePress(list->viewport(), Qt::LeftButton, Qt::ControlModifier, second);
    EXPECT_EQ(detail()->entry_id(), id);
    QTest::mouseRelease(list->viewport(), Qt::LeftButton, Qt::NoModifier, second);
    EXPECT_EQ(detail()->entry_id(), id);
    QTest::mouseClick(list->viewport(), Qt::LeftButton,
                      Qt::ControlModifier | Qt::ShiftModifier, second);
    EXPECT_EQ(detail()->entry_id(), id);
}

TEST_F(MainWindowTest, RightMouseGestureNeverOpensOrReplacesDetail) {
    window_->show();
    QApplication::processEvents();
    auto* list = passwords();
    const auto first = list->visualItemRect(list->item(0)).center();
    const auto second = list->visualItemRect(list->item(1)).center();

    QTest::mousePress(list->viewport(), Qt::RightButton, Qt::NoModifier, first);
    EXPECT_FALSE(detail()->HasEntry());
    EXPECT_TRUE(detail()->isVisible());
    QTest::mouseRelease(list->viewport(), Qt::RightButton, Qt::NoModifier, first);
    EXPECT_FALSE(detail()->HasEntry());
    EXPECT_TRUE(list->item(0)->isSelected());

    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, first);
    ASSERT_TRUE(detail()->HasEntry());
    const auto displayed = detail()->entry_id();
    QTest::mousePress(list->viewport(), Qt::RightButton, Qt::NoModifier, second);
    EXPECT_EQ(detail()->entry_id(), displayed);
    QTest::mouseRelease(list->viewport(), Qt::RightButton, Qt::NoModifier, second);
    EXPECT_EQ(detail()->entry_id(), displayed);
}

TEST_F(MainWindowTest, ContextMenuTargetsUnselectedEntryWithoutOpeningDetail) {
    window_->show();
    QApplication::processEvents();
    auto* list = passwords();
    ASSERT_TRUE(list->selectedItems().isEmpty());
    auto* target = list->item(1);
    const auto pos = list->visualItemRect(target).center();
    bool menu_shown = false;
    QTimer::singleShot(0, [this, target, &menu_shown]() {
        auto* menu = window_->findChild<QMenu*>("PasswordContextMenu");
        if (!menu) {
            ADD_FAILURE() << "Password context menu was not created";
            return;
        }
        menu_shown = true;
        EXPECT_TRUE(target->isSelected());
        EXPECT_EQ(passwords()->selectedItems().size(), 1);
        EXPECT_FALSE(detail()->HasEntry());
        EXPECT_TRUE(detail()->isVisible());
        EXPECT_FALSE(menu->actions().isEmpty());
        menu->close();
    });
    list->customContextMenuRequested(pos);
    EXPECT_TRUE(menu_shown);
    EXPECT_FALSE(detail()->HasEntry());

    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    ASSERT_TRUE(detail()->HasEntry());
    EXPECT_EQ(detail()->entry_id(), target->data(Qt::UserRole).toLongLong());
}

TEST_F(MainWindowTest, DetailActionsUseDisplayedEntryWhileSelectionDiffers) {
    window_->show();
    QApplication::processEvents();
    auto* list = passwords();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(0)).center());
    const auto displayed = detail()->entry_id();
    const auto other = list->item(1)->data(Qt::UserRole).toLongLong();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier,
                      list->visualItemRect(list->item(1)).center());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier,
                      list->visualItemRect(list->item(0)).center());
    ASSERT_EQ(list->selectedItems().size(), 1);
    ASSERT_EQ(list->selectedItems().first()->data(Qt::UserRole).toLongLong(), other);
    EXPECT_EQ(detail()->entry_id(), displayed);
    detail()->findChild<QToolButton*>("DetailHeaderButton")->click();
    EXPECT_TRUE(password_dao_->FindById(displayed)->is_favorite);
    EXPECT_TRUE(password_dao_->FindById(other)->is_favorite);
    EXPECT_EQ(detail()->entry_id(), displayed);
    detail()->findChild<QPushButton*>("DetailHeaderButton")->click();
    ASSERT_TRUE(editor()->IsOpen());
    EXPECT_EQ(editor()->Result().entry.id, displayed);
    editor()->Close();
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        dialog->button(QMessageBox::Yes)->click();
    });
    detail()->findChild<QToolButton*>("DetailHeaderDeleteButton")->click();
    EXPECT_TRUE(password_dao_->FindById(displayed)->is_deleted);
    EXPECT_FALSE(password_dao_->FindById(other)->is_deleted);
    EXPECT_FALSE(detail()->HasEntry());
}

TEST_F(MainWindowTest, PasswordPreferencePersistsAndOverridesOpenPanels) {
    auto* prefs = window_->preferences_page();
    auto* toggle = prefs->findChild<QCheckBox*>("DefaultShowPasswords");
    ASSERT_NE(toggle, nullptr);
    toggle->setChecked(false);
    passwords()->setCurrentRow(0);
    auto* value = detail()->findChild<QLabel*>("DetailFieldValueMono");
    const auto id = detail()->entry_id();
    const auto plain = QStringLiteral("pw-GitHub");
    toggle->setChecked(true);
    EXPECT_EQ(value->text(), plain);
    ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnEditRequested", Q_ARG(std::int64_t, id)));
    auto* input = editor()->findChild<QLineEdit*>("EditorPasswordInput");
    EXPECT_EQ(input->echoMode(), QLineEdit::Normal);
    editor()->findChild<QToolButton*>("EditorPreviewToggle")->click();
    detail()->findChildren<QToolButton*>("DetailFieldButton").at(1)->click();
    window_->Reload();
    EXPECT_NE(value->text(), plain);
    EXPECT_EQ(input->echoMode(), QLineEdit::Password);
    toggle->setChecked(false);
    toggle->setChecked(true);
    EXPECT_EQ(value->text(), plain);
    EXPECT_EQ(input->echoMode(), QLineEdit::Normal);
    passvault::ui::PreferencesPage reopened;
    EXPECT_TRUE(reopened.DefaultPasswordVisible());
    auto* generated = editor()->findChild<QLineEdit*>("EditorGeneratorPreview");
    ASSERT_NE(generated, nullptr);
    generated->setText(QStringLiteral("temporary-demo"));
    passvault::ui::ClipboardManager::Instance()->CopySensitive(QStringLiteral("temporary-demo"));
    window_->SetSessionKey(nullptr);
    EXPECT_TRUE(generated->text().isEmpty());
    EXPECT_TRUE(QApplication::clipboard()->text().isEmpty());
    EXPECT_FALSE(detail()->HasEntry());
    EXPECT_TRUE(value->text().isEmpty());
    EXPECT_TRUE(input->text().isEmpty());
    EXPECT_EQ(input->echoMode(), QLineEdit::Password);
    toggle->setChecked(false);
}

TEST_F(MainWindowTest, CardActionsReachableAndTimeThreshold) {
    auto entry = password_dao_->ListActive().front();
    entry.title = QString(180, QChar('T'));
    entry.username = QString(180, QChar('U'));
    entry.is_favorite = true;
    ASSERT_TRUE(password_dao_->Update(entry));
    window_->Reload();
    window_->show();
    QApplication::processEvents();
    auto* card = passwords()->itemWidget(passwords()->item(0));
    auto* time = card->findChild<QLabel*>("PasswordCardTime");
    auto* star = card->findChild<QLabel*>("PasswordCardStar");
    auto* remove = card->findChild<QToolButton*>("PasswordCardDeleteButton");
    ASSERT_NE(time, nullptr);
    ASSERT_NE(star, nullptr);
    EXPECT_EQ(remove, nullptr);
    for (int width : {480, 360, 359, 300}) {
        card->resize(width, 64);
        QApplication::processEvents();
        EXPECT_EQ(time->isHidden(), width < 360);
        EXPECT_EQ(star->size(), QSize(20, 20));
        EXPECT_LE(star->geometry().right(), card->width());
        EXPECT_FALSE(time->toolTip().isEmpty());
        EXPECT_EQ(time->toolTip(), time->accessibleName());
        auto* title = card->findChild<QLabel*>("PasswordCardTitle");
        auto* username = card->findChild<QLabel*>("PasswordCardMeta");
        EXPECT_LE(title->geometry().right(),
                  (width >= 360 ? time : star)->geometry().left());
        EXPECT_LE(username->geometry().right(),
                  (width >= 360 ? time : star)->geometry().left());
        if (width >= 360) {
            EXPECT_EQ(time->width(), 80);
            EXPECT_LE(time->geometry().right(), star->geometry().left());
        }
    }
}

TEST_F(MainWindowTest, ShiftOnlyGestureKeepsClosedDetailAndPlainClickReopens) {
    window_->show();
    QApplication::processEvents();
    auto* list = passwords();
    const auto point = list->visualItemRect(list->item(0)).center();
    QTest::mousePress(list->viewport(), Qt::LeftButton, Qt::ShiftModifier, point);
    EXPECT_FALSE(detail()->HasEntry());
    QTest::mouseRelease(list->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    EXPECT_FALSE(detail()->HasEntry());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier, point);
    EXPECT_FALSE(detail()->HasEntry());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    EXPECT_TRUE(detail()->HasEntry());
    detail()->CloseRequested();
    EXPECT_FALSE(detail()->HasEntry());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    EXPECT_TRUE(detail()->HasEntry());
}

TEST_F(MainWindowTest, RemovingOtherEntryPreservesDisplayedDetailAndTemporaryEye) {
    window_->show();
    QApplication::processEvents();
    passwords()->setCurrentRow(0);
    const auto displayed = detail()->entry_id();
    auto* value = detail()->findChild<QLabel*>("DetailFieldValueMono");
    detail()->findChildren<QToolButton*>("DetailFieldButton").at(1)->click();
    const auto previous_text = value->text();
    const auto other_id =
        passwords()->item(1)->data(Qt::UserRole).toLongLong();
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        dialog->button(QMessageBox::Yes)->click();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnDeleteRequested",
                                          Q_ARG(std::int64_t, other_id)));
    EXPECT_EQ(detail()->entry_id(), displayed);
    EXPECT_EQ(value->text(), previous_text);
    search()->setText(QStringLiteral("Work"));
    EXPECT_EQ(detail()->entry_id(), displayed);
    EXPECT_EQ(value->text(), previous_text);
}

TEST_F(MainWindowTest, TrashDetailRestoreTargetsDisplayedEntryAndOtherRestoreKeepsIt) {
    for (const auto& entry : password_dao_->ListActive()) {
        ASSERT_TRUE(password_dao_->LogicalDelete(entry.id, 5000));
    }
    sections()->setCurrentRow(3);
    window_->show();
    QApplication::processEvents();
    auto* list = passwords();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(0)).center());
    const auto displayed = detail()->entry_id();
    auto* other_card = list->itemWidget(list->item(1));
    const auto other = list->item(1)->data(Qt::UserRole).toLongLong();
    EXPECT_EQ(other_card->findChild<QLabel*>("PasswordCardStar"), nullptr);
    auto* restore = other_card->findChild<QToolButton*>("PasswordCardRestoreButton");
    EXPECT_GE(restore->width(), 48);
    EXPECT_GE(restore->height(), 32);
    restore->click();
    EXPECT_EQ(detail()->entry_id(), displayed);
    EXPECT_FALSE(password_dao_->FindById(other)->is_deleted);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier,
                      list->visualItemRect(list->item(0)).center());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier,
                      list->visualItemRect(list->item(1)).center());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier,
                      list->visualItemRect(list->item(0)).center());
    EXPECT_EQ(detail()->entry_id(), displayed);
    ASSERT_EQ(list->selectedItems().size(), 1);
    EXPECT_NE(list->selectedItems().first()->data(Qt::UserRole).toLongLong(), displayed);
    detail()->findChild<QPushButton*>("DetailHeaderButton")->click();
    EXPECT_FALSE(password_dao_->FindById(displayed)->is_deleted);
    EXPECT_FALSE(detail()->HasEntry());
}

TEST_F(MainWindowTest, ClosedDetailStaysClosedWhenControlSelectionReturnsToOne) {
    window_->show();
    QApplication::processEvents();
    auto* list = passwords();
    for (int row : {0, 1, 0}) {
        const auto point = list->visualItemRect(list->item(row)).center();
        QTest::mousePress(list->viewport(), Qt::LeftButton, Qt::ControlModifier, point);
        EXPECT_FALSE(detail()->HasEntry());
        QTest::mouseRelease(list->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        EXPECT_FALSE(detail()->HasEntry());
    }
    ASSERT_EQ(list->selectedItems().size(), 1);
    EXPECT_EQ(list->selectedItems().first(), list->item(1));
}

TEST_F(MainWindowTest, SameEntrySaveFavoriteThemeAndSyncKeepTemporaryEye) {
    ThemeGuard guard;
    passwords()->setCurrentRow(0);
    const auto displayed = detail()->entry_id();
    auto* value = detail()->findChild<QLabel*>("DetailFieldValueMono");
    detail()->findChildren<QToolButton*>("DetailFieldButton").at(1)->click();
    const auto previous = value->text();
    ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnEditRequested",
                                         Q_ARG(std::int64_t, displayed)));
    editor()->findChild<QPushButton*>("EditorSaveButton")->click();
    EXPECT_EQ(detail()->entry_id(), displayed);
    EXPECT_EQ(value->text(), previous);
    detail()->findChild<QToolButton*>("DetailHeaderButton")->click();
    EXPECT_EQ(value->text(), previous);
    passvault::ui::ThemeManager::Instance()->ApplyTheme(passvault::ui::Theme::kDark);
    EXPECT_EQ(value->text(), previous);
    const auto kind = passvault::sync::SyncManager::StatusKind::kSuccess;
    const QString message;
    ASSERT_TRUE(QMetaObject::invokeMethod(window_.get(), "OnSyncFinished",
        QGenericArgument("sync::SyncManager::StatusKind", &kind),
        QGenericArgument("QString", &message)));
    EXPECT_EQ(detail()->entry_id(), displayed);
    EXPECT_EQ(value->text(), previous);
}

TEST_F(MainWindowTest, RecreatedWindowReadsPersistedVisibilityForNormalAndTrash) {
    QSettings().setValue("ui/default_show_passwords", true);
    MainWindow::Deps deps;
    deps.password_dao = password_dao_.get();
    deps.category_dao = category_dao_.get();
    deps.session_key = &*session_key_;
    window_ = std::make_unique<MainWindow>(deps);
    passwords()->setCurrentRow(0);
    auto* value = detail()->findChild<QLabel*>("DetailFieldValueMono");
    EXPECT_EQ(value->text(), QStringLiteral("pw-GitHub"));
    const auto id = detail()->entry_id();
    ASSERT_TRUE(password_dao_->LogicalDelete(id, 5000));
    sections()->setCurrentRow(3);
    passwords()->setCurrentRow(0);
    EXPECT_EQ(value->text(), QStringLiteral("pw-GitHub"));
    QSettings().setValue("ui/default_show_passwords", false);
}
