#include <gtest/gtest.h>

#include <QApplication>
#include <QCryptographicHash>
#include <QImage>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QSignalSpy>
#include <QToolButton>

#include "model/password_entry.h"
#include "ui/detail_panel.h"
#include "ui/theme_manager.h"

namespace {

using passvault::ui::DetailPanel;
using passvault::model::PasswordEntry;

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

// Field buttons share one QSS objectName, so locate them by creation order.
template <typename T>
T* NthNamed(const DetailPanel& p, const char* name, int n) {
    const QList<T*> list = p.findChildren<T*>(QString::fromLatin1(name));
    return n < list.size() ? list.at(n) : nullptr;
}

// DetailFieldButton QToolButtons in order: [0] username-copy, [1] password-toggle,
// [2] password-copy, [3] website-open.
// The favorite action is the only QToolButton named DetailHeaderButton.

PasswordEntry MakeEntry() {
    PasswordEntry e;
    e.id = 77;
    e.title = QStringLiteral("GitHub");
    e.username = QStringLiteral("octocat");
    e.website = QStringLiteral("https://github.com");
    e.is_favorite = false;
    return e;
}

class DetailPanelTest : public ::testing::Test {
 protected:
    DetailPanel panel_;

    QLabel* title() {
        return panel_.findChild<QLabel*>(QStringLiteral("DetailTitle"));
    }
    QLabel* usernameValue() {
        return panel_.findChild<QLabel*>(QStringLiteral("DetailFieldValue"));
    }
    QLabel* passwordValue() {
        return panel_.findChild<QLabel*>(QStringLiteral("DetailFieldValueMono"));
    }
    QLabel* websiteValue() {
        return panel_.findChild<QLabel*>(QStringLiteral("DetailFieldValueLink"));
    }
    QPushButton* editButton() {
        return panel_.findChild<QPushButton*>(QStringLiteral("DetailHeaderButton"));
    }
    QPushButton* copyPasswordButton() {
        return panel_.findChild<QPushButton*>(
            QStringLiteral("DetailSecondaryButton"));
    }
    QPushButton* openWebsiteButton() {
        return panel_.findChild<QPushButton*>(
            QStringLiteral("DetailPrimaryButton"));
    }
    QToolButton* deleteButton() {
        return panel_.findChild<QToolButton*>(
            QStringLiteral("DetailHeaderDeleteButton"));
    }
    QToolButton* favoriteButton() {
        return NthNamed<QToolButton>(panel_, "DetailHeaderButton", 0);
    }
    QToolButton* usernameCopy() {
        return NthNamed<QToolButton>(panel_, "DetailFieldButton", 0);
    }
    QToolButton* passwordToggle() {
        return NthNamed<QToolButton>(panel_, "DetailFieldButton", 1);
    }
};

TEST_F(DetailPanelTest, SetEntryPopulatesAndHasEntry) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));
    EXPECT_TRUE(panel_.HasEntry());
    EXPECT_EQ(panel_.entry_id(), 77);
    EXPECT_EQ(title()->text(), QStringLiteral("GitHub"));
    EXPECT_EQ(usernameValue()->text(), QStringLiteral("octocat"));
    EXPECT_EQ(websiteValue()->text(), QStringLiteral("https://github.com"));
}

TEST(DetailPanelThemeTest, ThemeRefreshPreservesEntryAndPasswordVisibility) {
    ThemeGuard guard;
    auto* theme = passvault::ui::ThemeManager::Instance();
    theme->ApplyTheme(passvault::ui::Theme::kLight);
    DetailPanel panel;
    PasswordEntry entry = MakeEntry();
    panel.SetEntry(entry, QStringLiteral("secret-value"));

    auto* toggle = NthNamed<QToolButton>(panel, "DetailFieldButton", 1);
    ASSERT_NE(toggle, nullptr);
    toggle->click();
    auto* password = panel.findChild<QLabel*>(
        QStringLiteral("DetailFieldValueMono"));
    ASSERT_NE(password, nullptr);
    EXPECT_EQ(password->text(), QStringLiteral("secret-value"));
    const QByteArray light_icon = IconPixels(toggle->icon());

    theme->ApplyTheme(passvault::ui::Theme::kDark);

    EXPECT_NE(IconPixels(toggle->icon()), light_icon);
    EXPECT_EQ(panel.entry_id(), entry.id);
    EXPECT_EQ(password->text(), QStringLiteral("secret-value"));
}

TEST_F(DetailPanelTest, HeaderAlignsIdentityAndActionsOnOneRow) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));
    panel_.resize(400, 700);
    panel_.show();
    QApplication::processEvents();

    auto* header = panel_.findChild<QWidget*>(QStringLiteral("DetailHeader"));
    auto* icon = panel_.findChild<QWidget*>(QStringLiteral("DetailIcon"));
    auto* actions = panel_.findChild<QWidget*>(
        QStringLiteral("DetailHeaderActions"));
    ASSERT_NE(header, nullptr);
    ASSERT_NE(icon, nullptr);
    ASSERT_NE(actions, nullptr);
    EXPECT_EQ(header->y(), 28);
    EXPECT_EQ(icon->y(), actions->y());
    ASSERT_NE(favoriteButton(), nullptr);
    EXPECT_TRUE(favoriteButton()->isVisible());
    EXPECT_EQ(favoriteButton()->parentWidget(), actions);
}

TEST_F(DetailPanelTest, ClearEntryResetsState) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));
    panel_.ClearEntry();
    EXPECT_FALSE(panel_.HasEntry());
    EXPECT_EQ(panel_.entry_id(), -1);
}

TEST_F(DetailPanelTest, EditButtonEmitsWithId) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));
    QSignalSpy spy(&panel_, &DetailPanel::EditRequested);
    editButton()->click();
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toLongLong(), 77);
}

TEST_F(DetailPanelTest, ExplicitDeleteButtonEmitsDeleteWithId) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));
    QSignalSpy spy(&panel_, &DetailPanel::DeleteRequested);
    ASSERT_NE(deleteButton(), nullptr);
    EXPECT_EQ(deleteButton()->toolTip(), QStringLiteral("删除密码"));
    deleteButton()->click();
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toLongLong(), 77);
}

TEST_F(DetailPanelTest, CopyPasswordButtonEmitsWithId) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));
    QSignalSpy spy(&panel_, &DetailPanel::CopyPasswordRequested);
    copyPasswordButton()->click();
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toLongLong(), 77);
}

TEST_F(DetailPanelTest, CopyUsernameButtonEmitsWithId) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));
    QSignalSpy spy(&panel_, &DetailPanel::CopyUsernameRequested);
    ASSERT_NE(usernameCopy(), nullptr);
    usernameCopy()->click();
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toLongLong(), 77);
}

TEST_F(DetailPanelTest, OpenWebsiteButtonEmitsWithId) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));
    QSignalSpy spy(&panel_, &DetailPanel::OpenWebsiteRequested);
    openWebsiteButton()->click();
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toLongLong(), 77);
}

TEST_F(DetailPanelTest, FavoriteToggleEmitsWithDesired) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));  // not favorite
    QSignalSpy spy(&panel_, &DetailPanel::FavoriteToggleRequested);
    ASSERT_NE(favoriteButton(), nullptr);
    EXPECT_FALSE(favoriteButton()->isHidden());
    EXPECT_EQ(favoriteButton()->accessibleName(), QStringLiteral("收藏"));
    favoriteButton()->click();  // toggles unchecked -> checked
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toLongLong(), 77);
    EXPECT_TRUE(spy.at(0).at(1).toBool());
}

TEST_F(DetailPanelTest, PasswordToggleSwitchesMaskedAndPlain) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));
    // Masked initially: bullets, not the plain text.
    EXPECT_NE(passwordValue()->text(), QStringLiteral("secret"));
    ASSERT_NE(passwordToggle(), nullptr);
    passwordToggle()->click();
    EXPECT_EQ(passwordValue()->text(), QStringLiteral("secret"));
    passwordToggle()->click();
    EXPECT_NE(passwordValue()->text(), QStringLiteral("secret"));
}

}  // namespace
