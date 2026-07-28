#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QSignalSpy>
#include <QToolButton>

#include "model/password_entry.h"
#include "ui/detail_panel.h"

namespace {

using passvault::ui::DetailPanel;
using passvault::model::PasswordEntry;

// The header and field buttons share QSS objectNames ("DetailHeaderButton",
// "DetailFieldButton") so findChild can't target one; locate by type + name +
// creation-order index (findChildren preserves depth-first construction order).
template <typename T>
T* NthNamed(const DetailPanel& p, const char* name, int n) {
    const QList<T*> list = p.findChildren<T*>(QString::fromLatin1(name));
    return n < list.size() ? list.at(n) : nullptr;
}

// DetailFieldButton QToolButtons in order: [0] username-copy, [1] password-toggle,
// [2] password-copy, [3] website-open.
// DetailHeaderButton QToolButtons in order: [0] more(=delete), [1] favorite.
// header-edit is the only QPushButton named DetailHeaderButton.

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
    QToolButton* moreButton() {
        return NthNamed<QToolButton>(panel_, "DetailHeaderButton", 0);
    }
    QToolButton* favoriteButton() {
        return NthNamed<QToolButton>(panel_, "DetailHeaderButton", 1);
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
    EXPECT_FALSE(favoriteButton()->isVisible());
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

TEST_F(DetailPanelTest, MoreButtonEmitsDeleteWithId) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));
    QSignalSpy spy(&panel_, &DetailPanel::DeleteRequested);
    ASSERT_NE(moreButton(), nullptr);
    moreButton()->click();
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
