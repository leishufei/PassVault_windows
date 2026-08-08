#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QFile>
#include <QFont>
#include <QFontInfo>
#include <QFontMetrics>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTabWidget>
#include <QToolButton>

#include "ui/preferences_page.h"
#include "ui/theme_manager.h"

namespace {

using passvault::ui::PreferencesPage;

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

struct RenderedSwitch {
    QImage image;
    QRect indicator;
};

RenderedSwitch RenderSwitchIndicator(QCheckBox* toggle, QStyle::State state) {
    QStyleOptionButton option;
    option.initFrom(toggle);
    option.rect = QRect(0, 0, 36, 20);
    option.state = state;

    QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    toggle->style()->drawControl(QStyle::CE_CheckBox, &option, &painter,
                                 toggle);
    painter.end();

    return {image, toggle->style()->subElementRect(
                       QStyle::SE_CheckBoxIndicator, &option, toggle)};
}

QByteArray ImageHash(const RenderedSwitch& rendered) {
    const auto* bytes =
        reinterpret_cast<const char*>(rendered.image.constBits());
    const QByteArray pixels(
        bytes, static_cast<qsizetype>(rendered.image.sizeInBytes()));
    return QCryptographicHash::hash(pixels, QCryptographicHash::Sha256);
}

QColor IndicatorPixel(const RenderedSwitch& rendered, int x, int y) {
    return rendered.image.pixelColor(rendered.indicator.x() + x,
                                     rendered.indicator.y() + y);
}

class PreferencesPageTest : public ::testing::Test {
 protected:
    PreferencesPage page_;

    QComboBox* themeCombo() {
        return page_.findChild<QComboBox*>(QStringLiteral("ThemeCombo"));
    }
    QSpinBox* autoLockSpin() {
        return page_.findChild<QSpinBox*>(QStringLiteral("AutoLockSpin"));
    }
    QSpinBox* clipboardSpin() {
        return page_.findChild<QSpinBox*>(QStringLiteral("ClipboardSpin"));
    }
    QPushButton* driveConnect() {
        return page_.findChild<QPushButton*>(QStringLiteral("DriveConnectButton"));
    }
    QPushButton* driveDisconnect() {
        return page_.findChild<QPushButton*>(QStringLiteral("DriveDisconnectButton"));
    }
    QPushButton* syncNow() {
        return page_.findChild<QPushButton*>(QStringLiteral("SyncNowButton"));
    }
    QPushButton* importBtn() {
        return page_.findChild<QPushButton*>(QStringLiteral("ImportButton"));
    }
    QPushButton* exportBtn() {
        return page_.findChild<QPushButton*>(QStringLiteral("ExportButton"));
    }
    QPushButton* changeMaster() {
        return page_.findChild<QPushButton*>(QStringLiteral("ChangeMasterButton"));
    }
    QCheckBox* helloToggle() {
        return page_.findChild<QCheckBox*>(QStringLiteral("HelloToggle"));
    }
    QLabel* driveStatus() {
        return page_.findChild<QLabel*>(QStringLiteral("SettingRowStatus"));
    }
};

TEST_F(PreferencesPageTest, ThemeComboEmitsSignal) {
    auto* combo = themeCombo();
    ASSERT_NE(combo, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::ThemeChanged);
    combo->setCurrentIndex(1);
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toInt(), combo->itemData(1).toInt());
}

TEST_F(PreferencesPageTest, AutoLockSpinEmitsSignal) {
    auto* spin = autoLockSpin();
    ASSERT_NE(spin, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::AutoLockChanged);
    spin->setValue(10);
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toInt(), 10);
}

TEST_F(PreferencesPageTest, ClipboardSpinEmitsSignal) {
    auto* spin = clipboardSpin();
    ASSERT_NE(spin, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::ClipboardTimeoutChanged);
    spin->setValue(60);
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toInt(), 60);
}

TEST_F(PreferencesPageTest, DriveConnectButtonEmitsSignal) {
    auto* btn = driveConnect();
    ASSERT_NE(btn, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::ConnectGoogleDriveRequested);
    btn->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(PreferencesPageTest, DriveDisconnectButtonEmitsSignal) {
    auto* btn = driveDisconnect();
    ASSERT_NE(btn, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::DisconnectGoogleDriveRequested);
    btn->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(PreferencesPageTest, SyncNowButtonEmitsSignal) {
    auto* btn = syncNow();
    ASSERT_NE(btn, nullptr);
    btn->setEnabled(true);
    QSignalSpy spy(&page_, &PreferencesPage::SyncNowRequested);
    btn->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(PreferencesPageTest, ImportButtonEmitsSignal) {
    auto* btn = importBtn();
    ASSERT_NE(btn, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::ImportCsvRequested);
    btn->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(PreferencesPageTest, ExportButtonEmitsSignal) {
    auto* btn = exportBtn();
    ASSERT_NE(btn, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::ExportCsvRequested);
    btn->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(PreferencesPageTest, ChangeMasterButtonIsDisabledUntilReencryptExists) {
    auto* btn = changeMaster();
    ASSERT_NE(btn, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::ChangeMasterPasswordRequested);
    EXPECT_FALSE(btn->isEnabled());
    btn->click();
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(PreferencesPageTest, HelloToggleEmitsEnableSignal) {
    auto* toggle = helloToggle();
    ASSERT_NE(toggle, nullptr);
    toggle->setEnabled(true);
    QSignalSpy spy(&page_, &PreferencesPage::EnableHelloRequested);
    toggle->setChecked(true);
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(PreferencesPageTest, HelloToggleEmitsDisableSignal) {
    auto* toggle = helloToggle();
    ASSERT_NE(toggle, nullptr);
    toggle->setEnabled(true);
    toggle->setChecked(true);
    QSignalSpy spy(&page_, &PreferencesPage::DisableHelloRequested);
    toggle->setChecked(false);
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(PreferencesPageTest, SetHelloAvailableFalseDisablesToggle) {
    page_.SetHelloAvailable(false);
    auto* toggle = helloToggle();
    ASSERT_NE(toggle, nullptr);
    EXPECT_FALSE(toggle->isEnabled());
}

TEST_F(PreferencesPageTest, SetHelloAvailableTrueEnablesToggle) {
    page_.SetHelloAvailable(true);
    auto* toggle = helloToggle();
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->isEnabled());
}

TEST_F(PreferencesPageTest, SetHelloEnabledChecksToggle) {
    page_.SetHelloEnabled(true);
    auto* toggle = helloToggle();
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->isChecked());
}

TEST_F(PreferencesPageTest, SetGoogleDriveConnectedUpdatesStatus) {
    page_.SetGoogleDriveConnected(true, QStringLiteral("test@example.com"));
    auto* status = driveStatus();
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->text().contains(QStringLiteral("test@example.com")));
    EXPECT_TRUE(syncNow()->isEnabled());
}

TEST_F(PreferencesPageTest, SetGoogleDriveDisconnectedUpdatesStatus) {
    page_.SetGoogleDriveConnected(false, QString());
    auto* status = driveStatus();
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->text().contains(QStringLiteral("未连接")));
    EXPECT_FALSE(syncNow()->isEnabled());
}

TEST_F(PreferencesPageTest, HelloToggleUsesSwitchVariant) {
    EXPECT_EQ(helloToggle()->property("variant").toString(),
              QStringLiteral("switch"));
}

TEST_F(PreferencesPageTest, HelloToggleIndicatorMatchesFigmaSize) {
    auto* tabs = page_.findChild<QTabWidget*>(QStringLiteral("PreferencesTabs"));
    auto* toggle = helloToggle();
    ASSERT_NE(tabs, nullptr);
    ASSERT_NE(toggle, nullptr);
    tabs->setCurrentIndex(2);
    page_.show();
    toggle->ensurePolished();
    QApplication::processEvents();

    QStyleOptionButton option;
    option.initFrom(toggle);
    const QRect indicator = toggle->style()->subElementRect(
        QStyle::SE_CheckBoxIndicator, &option, toggle);
    EXPECT_EQ(indicator.size(), QSize(36, 20));
}

TEST_F(PreferencesPageTest, SwitchResourcesExist) {
    static constexpr const char* kResources[] = {
        ":/icons/switch-off.svg",          ":/icons/switch-on.svg",
        ":/icons/switch-off-active.svg",   ":/icons/switch-on-active.svg",
        ":/icons/switch-off-disabled.svg", ":/icons/switch-on-disabled.svg",
    };
    for (const char* resource : kResources) {
        EXPECT_TRUE(QFile::exists(QString::fromLatin1(resource))) << resource;
    }
}

TEST_F(PreferencesPageTest, HelloToggleRendersDistinctSwitchStates) {
    QWidget parent;
    parent.resize(1200, 800);
    PreferencesPage page(&parent);
    page.resize(parent.size());
    auto* tabs =
        page.findChild<QTabWidget*>(QStringLiteral("PreferencesTabs"));
    auto* toggle = page.findChild<QCheckBox*>(QStringLiteral("HelloToggle"));
    ASSERT_NE(tabs, nullptr);
    ASSERT_NE(toggle, nullptr);
    tabs->setCurrentIndex(2);
    parent.show();
    toggle->ensurePolished();
    toggle->setEnabled(true);
    toggle->clearFocus();
    QApplication::processEvents();

    const RenderedSwitch off = RenderSwitchIndicator(
        toggle, QStyle::State_Enabled | QStyle::State_Off);
    const RenderedSwitch on = RenderSwitchIndicator(
        toggle, QStyle::State_Enabled | QStyle::State_On);
    const RenderedSwitch off_active = RenderSwitchIndicator(
        toggle, QStyle::State_Enabled | QStyle::State_Off |
                    QStyle::State_MouseOver);
    const RenderedSwitch on_active = RenderSwitchIndicator(
        toggle, QStyle::State_Enabled | QStyle::State_On |
                    QStyle::State_MouseOver);

    toggle->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    ASSERT_TRUE(toggle->hasFocus());
    const RenderedSwitch off_focused = RenderSwitchIndicator(
        toggle, QStyle::State_Enabled | QStyle::State_Off |
                    QStyle::State_HasFocus);
    const RenderedSwitch on_focused = RenderSwitchIndicator(
        toggle, QStyle::State_Enabled | QStyle::State_On |
                    QStyle::State_HasFocus);

    toggle->clearFocus();
    toggle->setEnabled(false);
    QApplication::processEvents();
    const RenderedSwitch off_disabled =
        RenderSwitchIndicator(toggle, QStyle::State_Off);
    const RenderedSwitch on_disabled =
        RenderSwitchIndicator(toggle, QStyle::State_On);

    EXPECT_NE(ImageHash(off), ImageHash(on));
    EXPECT_NE(ImageHash(off), ImageHash(off_active));
    EXPECT_NE(ImageHash(on), ImageHash(on_active));
    EXPECT_NE(ImageHash(off_disabled), ImageHash(off));
    EXPECT_NE(ImageHash(on_disabled), ImageHash(on));
    EXPECT_NE(ImageHash(off_disabled), ImageHash(on_disabled));

    EXPECT_EQ(IndicatorPixel(off, 10, 10), QColor("#ffffff"));
    EXPECT_EQ(IndicatorPixel(off, 25, 10), QColor("#cbd5e1"));
    EXPECT_EQ(IndicatorPixel(on, 10, 10), QColor("#276cf0"));
    EXPECT_EQ(IndicatorPixel(on, 26, 10), QColor("#ffffff"));
    EXPECT_EQ(IndicatorPixel(off_active, 10, 10), QColor("#ffffff"));
    EXPECT_EQ(IndicatorPixel(off_active, 25, 10), QColor("#b8c5d6"));
    EXPECT_EQ(IndicatorPixel(on_active, 10, 10), QColor("#1d5edb"));
    EXPECT_EQ(IndicatorPixel(on_active, 26, 10), QColor("#ffffff"));
    EXPECT_EQ(IndicatorPixel(off_focused, 10, 10), QColor("#ffffff"));
    EXPECT_EQ(IndicatorPixel(off_focused, 25, 10), QColor("#b8c5d6"));
    EXPECT_EQ(IndicatorPixel(on_focused, 10, 10), QColor("#1d5edb"));
    EXPECT_EQ(IndicatorPixel(on_focused, 26, 10), QColor("#ffffff"));
    EXPECT_EQ(IndicatorPixel(off_disabled, 10, 10), QColor("#f8fafc"));
    EXPECT_EQ(IndicatorPixel(off_disabled, 25, 10), QColor("#e2e8f0"));
    EXPECT_EQ(IndicatorPixel(on_disabled, 10, 10), QColor("#9bb9f2"));
    EXPECT_EQ(IndicatorPixel(on_disabled, 26, 10), QColor("#f8fafc"));
}

TEST_F(PreferencesPageTest, BackButtonMatchesFigmaSize) {
    auto* back =
        page_.findChild<QToolButton*>(QStringLiteral("PreferencesBack"));
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->size(), QSize(32, 32));
}

TEST_F(PreferencesPageTest, HeaderIncludesDividerWithinFigmaHeight) {
    page_.resize(1200, 800);
    page_.show();
    QApplication::processEvents();

    auto* header =
        page_.findChild<QWidget*>(QStringLiteral("PreferencesHeader"));
    auto* divider = page_.findChild<QWidget*>(
        QStringLiteral("PreferencesHeaderDivider"));
    ASSERT_NE(header, nullptr);
    ASSERT_NE(divider, nullptr);

    EXPECT_EQ(header->height(), 68);
    EXPECT_EQ(divider->height(), 1);
    EXPECT_EQ(divider->width(), header->width());
    EXPECT_EQ(divider->geometry().bottom(), header->rect().bottom());
}

TEST_F(PreferencesPageTest, HeaderTitleMatchesFigmaTypography) {
    auto* title =
        page_.findChild<QLabel*>(QStringLiteral("PreferencesHeaderTitle"));
    ASSERT_NE(title, nullptr);
    page_.show();
    title->ensurePolished();
    QApplication::processEvents();

    const QFontInfo title_font_info(title->font());
    EXPECT_EQ(title_font_info.family(), QStringLiteral("Noto Sans SC"));
    EXPECT_EQ(title_font_info.pixelSize(), 16);
    EXPECT_EQ(title_font_info.weight(), 700);
}

TEST_F(PreferencesPageTest, HeaderTypographyBaselineFitsWithoutClipping) {
    auto* title =
        page_.findChild<QLabel*>(QStringLiteral("PreferencesHeaderTitle"));
    ASSERT_NE(title, nullptr);
    page_.show();
    QApplication::processEvents();

    ExpectCenteredTextFits(title, title->text());
}

TEST_F(PreferencesPageTest, BackButtonEmitsSignal) {
    auto* back = page_.findChild<QToolButton*>(QStringLiteral("PreferencesBack"));
    ASSERT_NE(back, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::BackRequested);
    back->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST(PreferencesPageThemeTest, ThemeRefreshPreservesTabAndControlValues) {
    ThemeGuard guard;
    auto* theme = passvault::ui::ThemeManager::Instance();
    theme->ApplyTheme(passvault::ui::Theme::kLight);
    PreferencesPage page;
    auto* tabs = page.findChild<QTabWidget*>(QStringLiteral("PreferencesTabs"));
    auto* auto_lock = page.findChild<QSpinBox*>(QStringLiteral("AutoLockSpin"));
    auto* back = page.findChild<QToolButton*>(QStringLiteral("PreferencesBack"));
    ASSERT_NE(tabs, nullptr);
    ASSERT_NE(auto_lock, nullptr);
    ASSERT_NE(back, nullptr);
    tabs->setCurrentIndex(2);
    auto_lock->setValue(17);
    const QByteArray light_icon = IconPixels(back->icon());

    theme->ApplyTheme(passvault::ui::Theme::kDark);

    EXPECT_NE(IconPixels(back->icon()), light_icon);
    EXPECT_EQ(tabs->currentIndex(), 2);
    EXPECT_EQ(auto_lock->value(), 17);
}

}  // namespace
