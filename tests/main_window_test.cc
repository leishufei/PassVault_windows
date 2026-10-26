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
    auto* settings = window_->findChild<QPushButton*>(
        QStringLiteral("SidebarSettingsButton"));
    ASSERT_NE(settings, nullptr);
    const QByteArray light_icon = IconPixels(settings->icon());

    theme->ApplyTheme(passvault::ui::Theme::kDark);

    EXPECT_NE(IconPixels(settings->icon()), light_icon);
    EXPECT_EQ(detail()->entry_id(), selected_id);
    ASSERT_NE(passwords()->currentItem(), nullptr);
    EXPECT_EQ(passwords()->currentItem()->data(Qt::UserRole).toLongLong(),
              selected_id);
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
    EXPECT_EQ(header->height(), 68);
    EXPECT_EQ(header->width(), content->width());
    EXPECT_EQ(content->y(), header->geometry().bottom() + 1);
    EXPECT_EQ(divider->height(), 1);
    EXPECT_EQ(divider->width(), header->width());
    EXPECT_EQ(search_container->size(), QSize(390, 36));
    EXPECT_EQ(search_container->x(), 28);
    EXPECT_GT(lockButton()->x(), search_container->geometry().right());
    EXPECT_EQ(lockButton()->geometry().center().y(),
              search_container->geometry().center().y());
    EXPECT_EQ(separator->geometry().center().y(),
              search_container->geometry().center().y());
    EXPECT_EQ(more_button->geometry().center().y(),
              search_container->geometry().center().y());
    EXPECT_EQ(list_column->y(), detail()->y());
    EXPECT_EQ(detail()->height(), window_->centralWidget()->height());
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
    EXPECT_EQ(passwords()->visualItemRect(passwords()->item(0)).height(), 60);
    EXPECT_EQ(passwords()->spacing(), 6);
    auto* card = passwords()->itemWidget(passwords()->item(0));
    ASSERT_NE(card, nullptr);
    EXPECT_FALSE(card->testAttribute(Qt::WA_TransparentForMouseEvents));
    EXPECT_NE(card->findChild<QToolButton*>("PasswordCardDeleteButton"), nullptr);
    const QString stylesheet =
        passvault::ui::ThemeManager::Instance()->LoadStyleSheet();
    EXPECT_TRUE(
        stylesheet.contains(QStringLiteral("#PasswordList::item:focus")));
    EXPECT_TRUE(stylesheet.contains(
        QStringLiteral("padding: 6px 22px 20px 22px")));
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
    EXPECT_EQ(empty->text(), QStringLiteral("没有找到匹配的密码"));
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

TEST_F(MainWindowTest, SearchShortcutUsesMonoTypography) {
    window_->show();
    auto* shortcut = window_->findChild<QLabel*>(
        QStringLiteral("SearchShortcut"));
    ASSERT_NE(shortcut, nullptr);
    shortcut->ensurePolished();
    QApplication::processEvents();

    const QFontInfo shortcut_font_info(shortcut->font());
    EXPECT_EQ(shortcut_font_info.family(), QStringLiteral("DM Mono"));
    EXPECT_EQ(shortcut_font_info.pixelSize(), 9);
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
    EXPECT_FALSE(detail()->isVisible());
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
    EXPECT_FALSE(detail()->isVisible());
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
    click_row(1, Qt::ControlModifier);
    EXPECT_EQ(passwords()->selectedItems().size(), 2);
    EXPECT_FALSE(detail()->isVisible());
    EXPECT_FALSE(detail()->HasEntry());
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
    auto* remove = card->findChild<QToolButton*>("PasswordCardDeleteButton");
    ASSERT_NE(remove, nullptr);
    QTimer::singleShot(0, []() {
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        ASSERT_NE(message, nullptr);
        message->button(QMessageBox::Yes)->click();
    });
    QTest::mouseClick(remove, Qt::LeftButton);
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
