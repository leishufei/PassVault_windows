#include <gtest/gtest.h>

#include <QApplication>
#include <QCryptographicHash>
#include <QFontInfo>
#include <QFontMetrics>
#include <QImage>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QSignalSpy>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>

#include <utility>

#include "model/password_entry.h"
#include "ui/detail_panel.h"
#include "ui/elided_label.h"
#include "ui/theme_manager.h"

namespace {

using passvault::ui::DetailPanel;
using passvault::ui::ElidedLabel;
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
// [2] password-copy, [3] website-open, [4] website-copy.
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
    QToolButton* copyPasswordButton() {
        return NthNamed<QToolButton>(panel_, "DetailFieldButton", 2);
    }
    QToolButton* openWebsiteButton() {
        return NthNamed<QToolButton>(panel_, "DetailFieldButton", 3);
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

TEST_F(DetailPanelTest, WebsiteRowOffersCopyNextToOpen) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));
    auto* open = openWebsiteButton();
    auto* copy = NthNamed<QToolButton>(panel_, "DetailFieldButton", 4);
    ASSERT_NE(open, nullptr);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->toolTip(), QStringLiteral("复制网址"));
    EXPECT_TRUE(copy->isEnabled());
    EXPECT_EQ(copy->parentWidget(), open->parentWidget());
    QSignalSpy copied(&panel_, &DetailPanel::CopyWebsiteRequested);
    copy->click();
    ASSERT_EQ(copied.count(), 1);
    EXPECT_EQ(copied.at(0).at(0).toLongLong(), 77);

    auto entry = MakeEntry();
    entry.website.clear();
    panel_.SetEntry(entry, QStringLiteral("secret"));
    EXPECT_FALSE(copy->isEnabled());
}

TEST_F(DetailPanelTest, FieldButtonsShareTheValueLine) {
    panel_.resize(400, 700);
    panel_.show();
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));
    QApplication::processEvents();
    const std::pair<QLabel*, QToolButton*> rows[] = {
        {usernameValue(), usernameCopy()},
        {passwordValue(), copyPasswordButton()},
        {websiteValue(), openWebsiteButton()},
    };
    for (const auto& [value, button] : rows) {
        ASSERT_NE(value, nullptr);
        ASSERT_NE(button, nullptr);
        EXPECT_EQ(button->height(), value->height());
        const QPoint value_top = value->mapTo(&panel_, QPoint(0, 0));
        const QPoint button_top = button->mapTo(&panel_, QPoint(0, 0));
        EXPECT_EQ(button_top.y(), value_top.y());
    }
}

TEST_F(DetailPanelTest, SetEntryPopulatesAndHasEntry) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));
    EXPECT_TRUE(panel_.HasEntry());
    EXPECT_EQ(panel_.entry_id(), 77);
    EXPECT_EQ(title()->accessibleName(), QStringLiteral("GitHub"));
    EXPECT_EQ(usernameValue()->accessibleName(), QStringLiteral("octocat"));
    EXPECT_EQ(websiteValue()->accessibleName(),
              QStringLiteral("https://github.com"));
}

TEST(ElidedLabelTest, ResizesTextAndPreservesFullValue) {
    const QString full_text = QStringLiteral(
        "A deliberately long value for responsive elision coverage");
    ElidedLabel label;
    label.resize(80, 24);
    label.SetFullText(full_text);

    EXPECT_NE(label.text(), full_text);
    EXPECT_TRUE(label.text().endsWith(QChar(0x2026)));
    EXPECT_EQ(label.toolTip(), full_text);
    EXPECT_EQ(label.accessibleName(), full_text);

    label.resize(label.fontMetrics().horizontalAdvance(full_text) + 10, 24);
    QApplication::processEvents();
    EXPECT_EQ(label.text(), full_text);
}

TEST_F(DetailPanelTest, LongIdentityFieldsWrapAndKeepFullValues) {
    PasswordEntry entry = MakeEntry();
    entry.title = QStringLiteral(
        "Long Identity Entry With A Deliberately Extended Title For Clipping "
        "Verification 0123456789");
    entry.username = QStringLiteral(
        "long.account.identifier.for.stage.two.acceptance@example.invalid");
    entry.website = QStringLiteral(
        "https://a-deliberately-long-stage-two-hostname.example.invalid/path");
    panel_.resize(400, 700);
    panel_.SetEntry(entry, QStringLiteral("secret"));
    panel_.show();
    QApplication::processEvents();

    EXPECT_EQ(panel_.findChild<QLabel*>(QStringLiteral("DetailTitleValue")),
              nullptr);
    EXPECT_EQ(panel_.findChild<QLabel*>(QStringLiteral("DetailCategoryValue")),
              nullptr);
    EXPECT_EQ(title()->toolTip(), entry.title);
    EXPECT_EQ(title()->accessibleName(), entry.title);

    const QList<QPair<QLabel*, QString>> fields = {
        {usernameValue(), entry.username},
        {websiteValue(), entry.website},
    };
    for (const auto& [label, full_text] : fields) {
        ASSERT_NE(label, nullptr);
        EXPECT_EQ(label->accessibleName(), full_text);
        EXPECT_TRUE(label->wordWrap());
        EXPECT_TRUE(label->hasHeightForWidth());
        EXPECT_EQ(label->accessibleName(), full_text);
    }
}

TEST(DetailPanelThemeTest, ThemeRefreshPreservesEntryAndPasswordVisibility) {
    ThemeGuard guard;
    auto* theme = passvault::ui::ThemeManager::Instance();
    theme->ApplyTheme(passvault::ui::Theme::kLight);
    DetailPanel panel;
    PasswordEntry entry = MakeEntry();
    panel.SetEntry(entry, QStringLiteral("secret-value"));
    panel.show();
    QApplication::processEvents();

    auto* toggle = NthNamed<QToolButton>(panel, "DetailFieldButton", 1);
    ASSERT_NE(toggle, nullptr);
    toggle->click();
    auto* password = panel.findChild<QLabel*>(
        QStringLiteral("DetailFieldValueMono"));
    ASSERT_NE(password, nullptr);
    EXPECT_EQ(password->text(), QStringLiteral("secret-value"));
    auto* username = panel.findChild<QLabel*>(QStringLiteral("DetailFieldValue"));
    ASSERT_NE(username, nullptr);
    EXPECT_EQ(QFontInfo(password->font()).family(), QStringLiteral("Consolas"));
    EXPECT_EQ(QFontInfo(username->font()).family(),
              QFontInfo(QApplication::font()).family());
    EXPECT_EQ(QFontInfo(password->font()).pixelSize(), 14);
    EXPECT_EQ(QFontInfo(username->font()).pixelSize(), 14);
    const QByteArray light_icon = IconPixels(toggle->icon());

    theme->ApplyTheme(passvault::ui::Theme::kDark);

    EXPECT_NE(IconPixels(toggle->icon()), light_icon);
    EXPECT_EQ(panel.entry_id(), entry.id);
    EXPECT_EQ(password->text(), QStringLiteral("secret-value"));
    EXPECT_EQ(QFontInfo(password->font()).family(), QStringLiteral("Consolas"));
    EXPECT_EQ(QFontInfo(username->font()).family(),
              QFontInfo(QApplication::font()).family());
    EXPECT_EQ(QFontInfo(password->font()).pixelSize(), 14);
    EXPECT_EQ(QFontInfo(username->font()).pixelSize(), 14);
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
    EXPECT_EQ(header->y(), 24);
    EXPECT_EQ(icon->y(), actions->y());
    ASSERT_NE(favoriteButton(), nullptr);
    EXPECT_TRUE(favoriteButton()->isHidden());
    EXPECT_EQ(favoriteButton()->parentWidget(), actions);
    auto* tag = panel_.findChild<QLabel*>("DetailTag");
    ASSERT_NE(tag, nullptr);
    EXPECT_GE(tag->width(), tag->fontMetrics().horizontalAdvance(tag->text()) + 16);
}

TEST_F(DetailPanelTest, DetailUsesUpdatedTimeWithoutSyntheticSubtitle) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("secret"));
    auto* subtitle = panel_.findChild<QLabel*>(
        QStringLiteral("DetailSubtitle"));
    EXPECT_EQ(subtitle, nullptr);

    const auto stamp_labels = panel_.findChildren<QLabel*>(
        QStringLiteral("DetailStampLabel"));
    ASSERT_EQ(stamp_labels.size(), 2);
    EXPECT_EQ(stamp_labels.at(0)->text(), QStringLiteral("创建时间"));
    EXPECT_EQ(stamp_labels.at(1)->text(), QStringLiteral("更新时间"));
}

TEST_F(DetailPanelTest, CompleteNotesAreScrollableAndDuplicateFooterIsRemoved) {
    auto entry = MakeEntry();
    entry.notes = QStringLiteral("<b>Literal note</b>\n").repeated(80);
    panel_.SetEntry(entry, QStringLiteral("secret"));
    panel_.resize(400, 500);
    panel_.show();
    QApplication::processEvents();
    auto* notes = panel_.findChild<QLabel*>("DetailNotesValue");
    auto* scroll = panel_.findChild<QScrollArea*>("DetailScroll");
    ASSERT_NE(notes, nullptr);
    ASSERT_NE(scroll, nullptr);
    EXPECT_EQ(notes->text(), entry.notes);
    EXPECT_EQ(notes->textFormat(), Qt::PlainText);
    EXPECT_TRUE(notes->wordWrap());
    EXPECT_GT(scroll->verticalScrollBar()->maximum(), 0);
    EXPECT_EQ(panel_.findChild<QPushButton*>("DetailSecondaryButton"), nullptr);
    EXPECT_EQ(panel_.findChild<QPushButton*>("DetailPrimaryButton"), nullptr);
    panel_.ClearEntry();
    EXPECT_TRUE(notes->text().isEmpty());
    EXPECT_TRUE(notes->accessibleName().isEmpty());
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

TEST_F(DetailPanelTest, WebsiteTextActivatesExistingOpenAction) {
    panel_.SetEntry(MakeEntry(), QStringLiteral("demo"));
    QSignalSpy spy(&panel_, &DetailPanel::OpenWebsiteRequested);
    EXPECT_TRUE(websiteValue()->textInteractionFlags() & Qt::LinksAccessibleByMouse);
    websiteValue()->linkActivated(QStringLiteral("website"));
    EXPECT_EQ(spy.count(), 1);
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
    EXPECT_TRUE(favoriteButton()->isHidden());
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

TEST_F(DetailPanelTest, EmptyFieldsStayVisibleAndDisableEmptyActions) {
    PasswordEntry entry = MakeEntry();
    panel_.SetEntry(entry, QStringLiteral("secret"));
    entry.username.clear();
    entry.website.clear();
    entry.notes.clear();
    entry.category_id = 99;
    panel_.SetEntry(entry, QString());
    panel_.resize(400, 700);
    panel_.show();
    QApplication::processEvents();

    EXPECT_TRUE(usernameValue()->text().isEmpty());
    EXPECT_TRUE(websiteValue()->text().isEmpty());
    EXPECT_TRUE(passwordValue()->text().isEmpty());
    auto* notes = panel_.findChild<QLabel*>("DetailNotesValue");
    ASSERT_NE(notes, nullptr);
    EXPECT_TRUE(notes->text().isEmpty());
    for (auto* value : {usernameValue(), passwordValue(), websiteValue(), notes}) {
        EXPECT_TRUE(value->isVisible());
        EXPECT_TRUE(value->parentWidget()->isVisible());
    }
    const auto dividers = panel_.findChildren<QFrame*>("DetailCardDivider");
    ASSERT_EQ(dividers.size(), 3);
    for (auto* divider : dividers) EXPECT_TRUE(divider->isVisible());
    const auto stamps = panel_.findChildren<QLabel*>("DetailStampValue");
    ASSERT_EQ(stamps.size(), 2);
    for (auto* stamp : stamps) EXPECT_TRUE(stamp->isVisible());
    auto* tag = panel_.findChild<QLabel*>("DetailTag");
    ASSERT_NE(tag, nullptr);
    EXPECT_TRUE(tag->text().isEmpty());
    EXPECT_TRUE(tag->isVisible());
    EXPECT_FALSE(usernameCopy()->isEnabled());
    EXPECT_FALSE(copyPasswordButton()->isEnabled());
    EXPECT_FALSE(passwordToggle()->isEnabled());
    QSignalSpy copied(&panel_, &DetailPanel::CopyUsernameRequested);
    usernameCopy()->click();
    EXPECT_EQ(copied.count(), 0);
    auto* websiteButton = NthNamed<QToolButton>(panel_, "DetailFieldButton", 3);
    ASSERT_NE(websiteButton, nullptr);
    EXPECT_FALSE(websiteButton->isEnabled());
    EXPECT_FALSE(openWebsiteButton()->isEnabled());
}

TEST_F(DetailPanelTest, UnavailablePasswordShowsPlaceholderAndDisablesActions) {
    panel_.SetEntry(MakeEntry(), std::nullopt);

    EXPECT_EQ(passwordValue()->text(), QStringLiteral("(无法解密)"));
    EXPECT_FALSE(passwordToggle()->isEnabled());
    auto* passwordCopy = NthNamed<QToolButton>(panel_, "DetailFieldButton", 2);
    ASSERT_NE(passwordCopy, nullptr);
    EXPECT_FALSE(passwordCopy->isEnabled());
    EXPECT_FALSE(copyPasswordButton()->isEnabled());
}

TEST_F(DetailPanelTest, EmptyPasswordRemainsBlankAndTimestampIncludesSeconds) {
    auto entry = MakeEntry();
    entry.created_at = 1700000012345;
    entry.updated_at = 1700000067890;
    panel_.SetEntry(entry, QString());
    EXPECT_TRUE(passwordValue()->text().isEmpty());
    EXPECT_FALSE(copyPasswordButton()->isEnabled());
    const auto stamps = panel_.findChildren<QLabel*>("DetailStampValue");
    ASSERT_EQ(stamps.size(), 2);
    EXPECT_EQ(stamps[0]->text(), QDateTime::fromMSecsSinceEpoch(entry.created_at)
                                  .toString("yyyy-MM-dd HH:mm:ss"));
    EXPECT_EQ(stamps[1]->text(), QDateTime::fromMSecsSinceEpoch(entry.updated_at)
                                  .toString("yyyy-MM-dd HH:mm:ss"));
}

}  // namespace

TEST_F(DetailPanelTest, TrashDetailsOfferRestoreAndPermanentDeleteWithoutEditOrFavorite) {
    auto entry = MakeEntry();
    entry.is_deleted = true;
    panel_.SetEntry(entry, QStringLiteral("synthetic"));
    auto* restore = panel_.findChild<QPushButton*>("DetailHeaderButton");
    ASSERT_NE(restore, nullptr);
    EXPECT_TRUE(restore->text().isEmpty());
    EXPECT_EQ(restore->toolTip(), QStringLiteral("还原"));
    EXPECT_FALSE(restore->icon().isNull());
    EXPECT_TRUE(restore->isEnabled());
    EXPECT_TRUE(restore->isHidden());
    EXPECT_TRUE(deleteButton()->isHidden());
    EXPECT_TRUE(favoriteButton()->isHidden());
    EXPECT_FALSE(favoriteButton()->isEnabled());
    EXPECT_TRUE(deleteButton()->isEnabled());
    EXPECT_EQ(deleteButton()->accessibleName(), QStringLiteral("永久删除"));
    QSignalSpy restored(&panel_, &DetailPanel::RestoreRequested);
    QSignalSpy edited(&panel_, &DetailPanel::EditRequested);
    QSignalSpy deleted(&panel_, &DetailPanel::DeleteRequested);
    restore->click();
    deleteButton()->click();
    EXPECT_EQ(restored.count(), 1);
    EXPECT_EQ(edited.count(), 0);
    EXPECT_EQ(deleted.count(), 1);
}

TEST_F(DetailPanelTest, DefaultVisibilityAndSameEntryRefresh) {
    const QString value = QStringLiteral("demo-value");
    auto entry = MakeEntry();
    panel_.SetEntry(entry, value);
    EXPECT_NE(passwordValue()->text(), value);
    panel_.SetDefaultPasswordVisible(true);
    EXPECT_EQ(passwordValue()->text(), value);
    passwordToggle()->click();
    panel_.SetEntry(entry, value);
    EXPECT_NE(passwordValue()->text(), value);
    ThemeGuard guard;
    passvault::ui::ThemeManager::Instance()->ApplyTheme(passvault::ui::Theme::kDark);
    EXPECT_NE(passwordValue()->text(), value);
    panel_.SetDefaultPasswordVisible(true);
    EXPECT_EQ(passwordValue()->text(), value);
    passwordToggle()->click();
    ++entry.id;
    panel_.SetEntry(entry, value);
    EXPECT_EQ(passwordValue()->text(), value);
    panel_.ClearEntry();
    EXPECT_TRUE(passwordValue()->text().isEmpty());
    panel_.SetEntry(entry, value);
    EXPECT_EQ(passwordValue()->text(), value);
    panel_.SetDefaultPasswordVisible(false);
    EXPECT_NE(passwordValue()->text(), value);
}
