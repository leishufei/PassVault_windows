#include <gtest/gtest.h>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QToolButton>

#include "ui/preferences_page.h"

namespace {

using passvault::ui::PreferencesPage;

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

TEST_F(PreferencesPageTest, ChangeMasterButtonEmitsSignal) {
    auto* btn = changeMaster();
    ASSERT_NE(btn, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::ChangeMasterPasswordRequested);
    btn->click();
    EXPECT_EQ(spy.count(), 1);
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

TEST_F(PreferencesPageTest, BackButtonEmitsSignal) {
    auto* back = page_.findChild<QToolButton*>(QStringLiteral("PreferencesBack"));
    ASSERT_NE(back, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::BackRequested);
    back->click();
    EXPECT_EQ(spy.count(), 1);
}

}  // namespace
