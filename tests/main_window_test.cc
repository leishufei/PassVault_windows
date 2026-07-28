#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QLayout>
#include <QList>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTest>
#include <QToolButton>
#include <QWidget>

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
#include "ui/detail_panel.h"
#include "ui/editor_panel.h"
#include "ui/main_window.h"
#include "ui/preferences_page.h"

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

// Locate the Nth widget of type T with the given (shared) objectName.
template <typename T>
T* NthNamed(const QObject& root, const char* name, int n) {
    const QList<T*> list = root.findChildren<T*>(QString::fromLatin1(name));
    return n < list.size() ? list.at(n) : nullptr;
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

        // createdAt descending puts github at row 0, gitlab at 1, workapp at 2.
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
              std::int64_t category_id, std::int64_t created) {
        PasswordEntry e;
        e.uuid = QStringLiteral("uuid-%1").arg(title);
        e.title = title;
        e.username = user;
        e.is_favorite = favorite;
        e.category_id = category_id;
        e.created_at = created;
        e.updated_at = created;
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

TEST_F(MainWindowTest, WorkspaceHeaderSpansListAndDetail) {
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
    EXPECT_EQ(list_column->height(), detail()->height());
}

TEST_F(MainWindowTest, SearchFiltersAndClears) {
    search()->setText(QStringLiteral("git"));
    EXPECT_EQ(passwords()->count(), 2);
    search()->setText(QString());
    EXPECT_EQ(passwords()->count(), 3);
}

TEST_F(MainWindowTest, FavoritesSectionShowsOnlyFavorites) {
    // Sections: 0 全部 / 1 收藏夹 / 2 未分类 / 3 回收站.
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

TEST_F(MainWindowTest, NewButtonOpensEditor) {
    EXPECT_FALSE(editor()->IsOpen());
    newButton()->click();
    EXPECT_TRUE(editor()->IsOpen());
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

    // DetailHeaderButton QToolButtons: [0] more, [1] favorite.
    auto* favorite = NthNamed<QToolButton>(*detail(), "DetailHeaderButton", 1);
    ASSERT_NE(favorite, nullptr);
    favorite->click();

    EXPECT_EQ(password_dao_->FindById(id)->is_favorite, !before);
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

// Skipped: delete flow goes through a blocking QMessageBox::question, which
// cannot be driven headlessly without refactoring the confirm prompt to be
// injectable. Verified manually. import/export/change-master toolbar signals
// are emitted by PreferencesPage (covered by preferences_page_test), not by
// MainWindow itself.

}  // namespace
