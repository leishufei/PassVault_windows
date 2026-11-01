#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTabWidget>
#include <QTabBar>
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
    option.rect = QRect(0, 0, 38, 28);
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
    QComboBox* autoLockCombo() {
        return page_.findChild<QComboBox*>(QStringLiteral("AutoLockCombo"));
    }
    QComboBox* clipboardCombo() {
        return page_.findChild<QComboBox*>(QStringLiteral("ClipboardCombo"));
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
    QLabel* driveStatus() {
        return page_.findChild<QLabel*>(QStringLiteral("SyncStateText"));
    }
    QLabel* accountEmail() {
        return page_.findChild<QLabel*>(QStringLiteral("SyncAccountEmail"));
    }
    QWidget* authorizeCard() {
        return page_.findChild<QWidget*>(QStringLiteral("SyncAuthCard"));
    }
    QWidget* accountCard() {
        return page_.findChild<QWidget*>(QStringLiteral("SyncAccountCard"));
    }
    QWidget* lastSyncCard() {
        return page_.findChild<QWidget*>(QStringLiteral("SyncLastCard"));
    }
    QPushButton* changeAccount() {
        return page_.findChild<QPushButton*>(QStringLiteral("ChangeAccountButton"));
    }
    QFrame* outerCard() {
        return page_.findChild<QFrame*>(QStringLiteral("PreferencesOuterCard"));
    }
    QTabWidget* tabs() {
        return page_.findChild<QTabWidget*>(QStringLiteral("PreferencesTabs"));
    }
};

TEST_F(PreferencesPageTest, MouseSwitchClickDoesNotKeepFocusOutline) {
    page_.show();
    auto* toggle = page_.findChild<QCheckBox*>("DefaultShowPasswords");
    ASSERT_NE(toggle, nullptr);
    QApplication::processEvents();
    QTest::mouseClick(toggle, Qt::LeftButton, Qt::NoModifier, QPoint(8, toggle->height() / 2));
    EXPECT_FALSE(toggle->hasFocus());
    EXPECT_FALSE(toggle->property("keyboardFocus").toBool());
}

TEST_F(PreferencesPageTest, TabsHavePaddingAndDoNotOverlap) {
    ThemeGuard guard;
    passvault::ui::ThemeManager::Instance()->ApplyTheme(passvault::ui::Theme::kLight);
    page_.resize(1200, 800);
    page_.show();
    QApplication::processEvents();
    auto* tabs = page_.findChild<QTabWidget*>("PreferencesTabs");
    const QString capture_dir = qEnvironmentVariable("PASSVAULT_UI_CAPTURE_DIR");
    if (!capture_dir.isEmpty()) {
        EXPECT_TRUE(page_.grab().save(QDir(capture_dir).filePath("settings.png")));
    }
    auto* bar = tabs->tabBar();
    EXPECT_GE(bar->x(), 16);
    for (int i = 0; i < bar->count(); ++i) {
        const QRect rect = bar->tabRect(i);
        EXPECT_GE(rect.width(), 64);
        EXPECT_GE(rect.height(), 34);
        if (i > 0) {
            EXPECT_LT(bar->tabRect(i - 1).right(), rect.left());
        }
    }
}

TEST_F(PreferencesPageTest, MouseFocusDoesNotLeaveKeyboardOutline) {
    page_.resize(1200, 800);
    page_.show();
    QApplication::processEvents();
    auto* button = page_.findChild<QPushButton*>("ImportButton");
    ASSERT_NE(button, nullptr);
    QTest::mouseClick(button, Qt::LeftButton);
    EXPECT_FALSE(button->property("keyboardFocus").toBool());
    button->clearFocus();
    button->setFocus(Qt::TabFocusReason);
    QApplication::processEvents();
    EXPECT_TRUE(button->property("keyboardFocus").toBool());
    QTest::mouseClick(button, Qt::LeftButton);
    EXPECT_FALSE(button->property("keyboardFocus").toBool());
    const QString capture_dir = qEnvironmentVariable("PASSVAULT_UI_CAPTURE_DIR");
    if (!capture_dir.isEmpty()) {
        EXPECT_TRUE(page_.grab().save(
            QDir(capture_dir).filePath("settings-mouse-focus.png")));
    }
}

TEST_F(PreferencesPageTest, MenusHaveTransparentFramelessCorners) {
    QMenu menu;
    menu.addAction(QStringLiteral("Action"));
    menu.ensurePolished();
    menu.popup(QPoint(100, 100));
    QApplication::processEvents();
    const QImage rendered = menu.grab().toImage();
    EXPECT_EQ(rendered.pixelColor(0, 0).alpha(), 0);
    const QString capture_dir = qEnvironmentVariable("PASSVAULT_UI_CAPTURE_DIR");
    if (!capture_dir.isEmpty()) {
        EXPECT_TRUE(rendered.save(QDir(capture_dir).filePath("menu.png")));
    }
    menu.close();
    EXPECT_TRUE(menu.testAttribute(Qt::WA_TranslucentBackground));
    EXPECT_TRUE(menu.windowFlags().testFlag(Qt::FramelessWindowHint));
    EXPECT_TRUE(menu.windowFlags().testFlag(Qt::NoDropShadowWindowHint));
}

TEST_F(PreferencesPageTest, SyncMarkUsesNeutralForeground) {
    auto* mark = page_.findChild<QLabel*>("SyncGoogleMark");
    ASSERT_NE(mark, nullptr);
    mark->ensurePolished();
    EXPECT_EQ(mark->palette().color(QPalette::WindowText),
              passvault::ui::ThemeManager::Instance()->Color("text-secondary"));
}

TEST_F(PreferencesPageTest, ThemeComboEmitsSignal) {
    auto* combo = themeCombo();
    ASSERT_NE(combo, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::ThemeChanged);
    combo->setCurrentIndex(1);
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toInt(), combo->itemData(1).toInt());
}

TEST_F(PreferencesPageTest, AutoLockComboEmitsSignal) {
    auto* spin = autoLockCombo();
    ASSERT_NE(spin, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::AutoLockChanged);
    spin->setCurrentIndex(spin->currentData().toInt() == 10 ? 2 : 1);
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toInt(), spin->currentData().toInt());
}

TEST_F(PreferencesPageTest, ClipboardComboEmitsSignal) {
    auto* spin = clipboardCombo();
    ASSERT_NE(spin, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::ClipboardTimeoutChanged);
    spin->setCurrentIndex(spin->currentData().toInt() == 60 ? 3 : 2);
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toInt(), spin->currentData().toInt());
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

TEST_F(PreferencesPageTest, ChangeMasterGateControlsRequestWithoutChangingCopy) {
    auto* button = changeMaster();
    ASSERT_NE(button, nullptr);
    QSignalSpy requested(&page_, &PreferencesPage::ChangeMasterPasswordRequested);
    page_.SetChangeMasterPasswordEnabled(true, QString());
    button->click();
    EXPECT_EQ(requested.count(), 1);

    page_.SetChangeMasterPasswordEnabled(false, QStringLiteral("unavailable"));
    button->click();
    EXPECT_EQ(requested.count(), 1);
    EXPECT_EQ(button->toolTip(), QStringLiteral("unavailable"));
    const auto labels = button->parentWidget()->findChildren<QLabel*>();
    for (const auto* label : labels) {
        EXPECT_FALSE(label->text().contains(QStringLiteral("unavailable")));
    }
}

TEST_F(PreferencesPageTest, LeaveLockPersistsAndDangerousActionsStayGated) {
    auto* toggle = page_.findChild<QCheckBox*>("LeaveLockToggle");
    auto* wipe = page_.findChild<QPushButton*>("ClearLocalDataButton");
    auto* desc = page_.findChild<QLabel*>("SettingRowDesc");
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(wipe, nullptr);
    ASSERT_NE(desc, nullptr);
    EXPECT_FALSE(desc->isHidden());
    EXPECT_EQ(wipe->text(), QStringLiteral("清除本地数据"));
    EXPECT_TRUE(toggle->isEnabled());
    toggle->setChecked(true);
    PreferencesPage reopened;
    EXPECT_TRUE(reopened.findChild<QCheckBox*>("LeaveLockToggle")->isChecked());
    toggle->setChecked(false);
    EXPECT_FALSE(QSettings().value("session/lock_on_minimize").toBool());
    EXPECT_TRUE(wipe->isEnabled());
    QSignalSpy clear_requested(&page_, &PreferencesPage::ClearLocalDataRequested);
    wipe->click();
    EXPECT_EQ(clear_requested.count(), 1);
    EXPECT_FALSE(changeMaster()->isEnabled());
    EXPECT_EQ(page_.findChild<QCheckBox*>("HelloToggle"), nullptr);
}

TEST_F(PreferencesPageTest, SetGoogleDriveConnectedUpdatesStatus) {
    page_.SetGoogleDriveConnected(true, QStringLiteral("test@example.com"));
    auto* email = accountEmail();
    ASSERT_NE(email, nullptr);
    EXPECT_EQ(email->accessibleName(), QStringLiteral("test@example.com"));
    EXPECT_TRUE(syncNow()->isEnabled());
}

TEST_F(PreferencesPageTest, SetGoogleDriveDisconnectedUpdatesStatus) {
    page_.SetGoogleDriveConnected(false, QString());
    ASSERT_NE(authorizeCard(), nullptr);
    EXPECT_FALSE(authorizeCard()->isHidden());
    EXPECT_FALSE(syncNow()->isEnabled());
}

TEST_F(PreferencesPageTest, DisconnectedShowsAuthorizeCard) {
    page_.SetSyncState(PreferencesPage::SyncUiState::kDisconnected, QString());
    ASSERT_NE(authorizeCard(), nullptr);
    ASSERT_NE(accountCard(), nullptr);
    ASSERT_NE(lastSyncCard(), nullptr);
    EXPECT_FALSE(authorizeCard()->isHidden());
    EXPECT_TRUE(accountCard()->isHidden());
    EXPECT_TRUE(lastSyncCard()->isHidden());
    EXPECT_FALSE(syncNow()->isEnabled());
    EXPECT_TRUE(driveStatus()->parentWidget()->isHidden());
    EXPECT_TRUE(page_.findChild<QPushButton*>("AuthorizeCancelButton")->isHidden());
}

TEST_F(PreferencesPageTest, AuthorizingShowsWaitingAndDisablesSync) {
    page_.SetSyncState(PreferencesPage::SyncUiState::kAuthorizing, QString());
    EXPECT_FALSE(authorizeCard()->isHidden());
    EXPECT_TRUE(accountCard()->isHidden());
    EXPECT_FALSE(syncNow()->isEnabled());
    EXPECT_EQ(driveStatus()->text(), QStringLiteral("等待授权..."));
    EXPECT_FALSE(driveStatus()->parentWidget()->isHidden());
    EXPECT_FALSE(page_.findChild<QPushButton*>("AuthorizeCancelButton")->isHidden());
}

TEST_F(PreferencesPageTest, ConnectedShowsAccountAndLastSyncCards) {
    page_.SetSyncState(PreferencesPage::SyncUiState::kConnected,
                       QStringLiteral("user@example.com"));
    EXPECT_TRUE(authorizeCard()->isHidden());
    EXPECT_FALSE(accountCard()->isHidden());
    EXPECT_FALSE(lastSyncCard()->isHidden());
    EXPECT_TRUE(syncNow()->isEnabled());
    EXPECT_EQ(accountEmail()->accessibleName(), QStringLiteral("user@example.com"));
    EXPECT_TRUE(driveStatus()->parentWidget()->isHidden());
}

TEST_F(PreferencesPageTest, ConnectedWithoutAccountHintDoesNotSayDisconnected) {
    page_.SetSyncState(PreferencesPage::SyncUiState::kConnected, QString());
    EXPECT_TRUE(accountEmail()->accessibleName().isEmpty());
    EXPECT_EQ(page_.findChild<QLabel*>("SyncAccountTitle")->text(),
              QStringLiteral("已连接"));
    EXPECT_FALSE(accountCard()->isHidden());
    EXPECT_TRUE(syncNow()->isEnabled());
    page_.SetSyncState(PreferencesPage::SyncUiState::kDisconnected, QString());
    EXPECT_FALSE(authorizeCard()->isHidden());
    EXPECT_TRUE(accountCard()->isHidden());
    EXPECT_EQ(page_.findChild<QLabel*>("SyncAuthTitle")->text(),
              QStringLiteral("未授权 Google 账号"));
}

TEST_F(PreferencesPageTest, SyncingDisablesSyncNowButton) {
    page_.SetSyncState(PreferencesPage::SyncUiState::kConnected,
                       QStringLiteral("user@example.com"));
    page_.SetSyncState(PreferencesPage::SyncUiState::kSyncing, QString());
    EXPECT_FALSE(syncNow()->isEnabled());
    EXPECT_EQ(driveStatus()->text(), QStringLiteral("同步中..."));
    EXPECT_FALSE(driveStatus()->parentWidget()->isHidden());
}

TEST_F(PreferencesPageTest, SuccessEnablesSyncNowAndClearsStatus) {
    page_.SetSyncState(PreferencesPage::SyncUiState::kConnected,
                       QStringLiteral("user@example.com"));
    page_.SetSyncState(PreferencesPage::SyncUiState::kSuccess, QString());
    EXPECT_TRUE(syncNow()->isEnabled());
    EXPECT_TRUE(driveStatus()->text().isEmpty());
    EXPECT_TRUE(driveStatus()->parentWidget()->isHidden());
    EXPECT_EQ(syncNow()->text(), QStringLiteral("立即同步"));
}

TEST_F(PreferencesPageTest, ConflictShowsConflictTextAndKeepsSyncEnabled) {
    page_.SetSyncState(PreferencesPage::SyncUiState::kConnected,
                       QStringLiteral("user@example.com"));
    page_.SetSyncState(PreferencesPage::SyncUiState::kConflict, QString());
    EXPECT_TRUE(syncNow()->isEnabled());
    EXPECT_EQ(driveStatus()->text(),
              QStringLiteral("同步冲突，请稍后重试"));
    EXPECT_FALSE(driveStatus()->parentWidget()->isHidden());
}

TEST_F(PreferencesPageTest, ErrorShowsFailureTextAndKeepsSyncEnabled) {
    page_.SetSyncState(PreferencesPage::SyncUiState::kConnected,
                       QStringLiteral("user@example.com"));
    page_.SetSyncState(PreferencesPage::SyncUiState::kError, QString(),
                       QStringLiteral("多次重试后仍存在并发冲突"));
    EXPECT_TRUE(syncNow()->isEnabled());
    EXPECT_EQ(driveStatus()->text(),
              QStringLiteral("同步失败：多次重试后仍存在并发冲突"));
    EXPECT_FALSE(driveStatus()->parentWidget()->isHidden());
}

TEST_F(PreferencesPageTest, ChangeAccountButtonEmitsConnectRequested) {
    page_.SetSyncState(PreferencesPage::SyncUiState::kConnected,
                       QStringLiteral("user@example.com"));
    auto* btn = changeAccount();
    ASSERT_NE(btn, nullptr);
    QSignalSpy spy(&page_, &PreferencesPage::ConnectGoogleDriveRequested);
    btn->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(PreferencesPageTest, SetLastSyncAtZeroShowsNever) {
    page_.SetLastSyncAt(0);
    auto* value = page_.findChild<QLabel*>(QStringLiteral("SyncLastValue"));
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->text(), QStringLiteral("从未同步"));
}

TEST_F(PreferencesPageTest, SetLastSyncAtFormatsAsFigmaStyle) {
    const qint64 epoch_ms = 1785406920000LL;
    page_.SetLastSyncAt(epoch_ms);
    auto* value = page_.findChild<QLabel*>(QStringLiteral("SyncLastValue"));
    ASSERT_NE(value, nullptr);
    const QString expected =
        QDateTime::fromMSecsSinceEpoch(epoch_ms)
            .toString(QStringLiteral("yyyy/MM/dd HH:mm"));
    EXPECT_EQ(value->text(), expected);
    EXPECT_TRUE(expected.contains(QLatin1Char('/')));
}

TEST_F(PreferencesPageTest, CsvButtonsLiveInGeneralTab) {
    auto* tabs = this->tabs();
    ASSERT_NE(tabs, nullptr);
    auto* import = importBtn();
    ASSERT_NE(import, nullptr);
    auto* export_b = exportBtn();
    ASSERT_NE(export_b, nullptr);
    EXPECT_EQ(tabs->indexOf(tabs->currentWidget()), 0);
    for (int i = 0; i < tabs->count(); ++i) {
        const bool has_import =
            tabs->widget(i)->findChild<QPushButton*>(
                QStringLiteral("ImportButton")) != nullptr;
        const bool has_export =
            tabs->widget(i)->findChild<QPushButton*>(
                QStringLiteral("ExportButton")) != nullptr;
        if (i == 0) {
            EXPECT_TRUE(has_import);
            EXPECT_TRUE(has_export);
        } else {
            EXPECT_FALSE(has_import);
            EXPECT_FALSE(has_export);
        }
    }
}

TEST_F(PreferencesPageTest, LongAccountEmailDoesNotOverflowCard) {
    page_.resize(640, 800);
    tabs()->setCurrentIndex(1);
    page_.show();
    QApplication::processEvents();
    page_.SetSyncState(
        PreferencesPage::SyncUiState::kConnected,
        QStringLiteral("a-very-long-mailbox-name-for-acceptance@example-domain.com"));
    QApplication::processEvents();
    auto* email = accountEmail();
    auto* card = accountCard();
    ASSERT_NE(email, nullptr);
    ASSERT_NE(card, nullptr);
    EXPECT_TRUE(card->rect().contains(
        QRect(email->mapTo(card, QPoint(0, 0)), email->size())));
    EXPECT_EQ(email->toolTip(),
              QStringLiteral("a-very-long-mailbox-name-for-acceptance@example-domain.com"));
    EXPECT_EQ(email->accessibleName(), email->toolTip());
    EXPECT_NE(email->text(), email->toolTip());
    EXPECT_LE(email->fontMetrics().horizontalAdvance(email->text()),
              email->contentsRect().width());
    EXPECT_FALSE(email->wordWrap());
}

TEST_F(PreferencesPageTest, LeaveLockToggleUsesSwitchVariant) {
    EXPECT_EQ(page_.findChild<QCheckBox*>("LeaveLockToggle")->property("variant").toString(),
              QStringLiteral("switch"));
}

TEST_F(PreferencesPageTest, LeaveLockToggleIndicatorMatchesFigmaSize) {
    auto* tabs = page_.findChild<QTabWidget*>(QStringLiteral("PreferencesTabs"));
    auto* toggle = page_.findChild<QCheckBox*>("LeaveLockToggle");
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
    EXPECT_EQ(indicator.size(), QSize(30, 18));
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

TEST_F(PreferencesPageTest, LeaveLockToggleRendersDistinctSwitchStates) {
    ThemeGuard guard;
    for (const auto theme : {passvault::ui::Theme::kLight,
                             passvault::ui::Theme::kDark}) {
        const bool dark = theme == passvault::ui::Theme::kDark;
        const QColor off_color(dark ? "#6f6f77" : "#888890");
        const QColor on_color(dark ? "#2383e2" : "#0075de");
        const QColor disabled_thumb(dark ? "#8c8c8f" : "#ffffff");
        const QColor disabled_off(dark ? "#4b4b52" : "#c9c9cd");
        const QColor disabled_on(dark ? "#295482" : "#8cc1f0");
        SCOPED_TRACE(dark ? "dark" : "light");
        passvault::ui::ThemeManager::Instance()->ApplyTheme(theme);
        QWidget parent;
        parent.resize(1200, 800);
        PreferencesPage page(&parent);
        page.resize(parent.size());
        auto* tabs =
            page.findChild<QTabWidget*>(QStringLiteral("PreferencesTabs"));
        auto* toggle =
            page.findChild<QCheckBox*>(QStringLiteral("LeaveLockToggle"));
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
        EXPECT_EQ(ImageHash(off), ImageHash(off_active));
        EXPECT_EQ(ImageHash(on), ImageHash(on_active));
        EXPECT_NE(ImageHash(off_disabled), ImageHash(off));
        EXPECT_NE(ImageHash(on_disabled), ImageHash(on));
        EXPECT_NE(ImageHash(off_disabled), ImageHash(on_disabled));

        EXPECT_EQ(IndicatorPixel(off, 10, 10), QColor("#ffffff"));
        EXPECT_EQ(IndicatorPixel(off, 25, 10), off_color);
        EXPECT_EQ(IndicatorPixel(on, 10, 10), on_color);
        EXPECT_EQ(IndicatorPixel(on, 26, 10), QColor("#ffffff"));
        EXPECT_EQ(IndicatorPixel(off_active, 10, 10), QColor("#ffffff"));
        EXPECT_EQ(IndicatorPixel(off_active, 25, 10), off_color);
        EXPECT_EQ(IndicatorPixel(on_active, 10, 10), on_color);
        EXPECT_EQ(IndicatorPixel(on_active, 26, 10), QColor("#ffffff"));
        EXPECT_EQ(IndicatorPixel(off_focused, 10, 10), QColor("#ffffff"));
        EXPECT_EQ(IndicatorPixel(off_focused, 25, 10), off_color);
        EXPECT_EQ(IndicatorPixel(on_focused, 10, 10), on_color);
        EXPECT_EQ(IndicatorPixel(on_focused, 26, 10), QColor("#ffffff"));
        EXPECT_EQ(IndicatorPixel(off_disabled, 10, 10),
                  disabled_thumb);
        EXPECT_EQ(IndicatorPixel(off_disabled, 25, 10), disabled_off);
        EXPECT_EQ(IndicatorPixel(on_disabled, 10, 10), disabled_on);
        EXPECT_EQ(IndicatorPixel(on_disabled, 26, 10),
                  disabled_thumb);
    }
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

    EXPECT_EQ(header->height(), 64);
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
    EXPECT_EQ(title_font_info.family(), QFontInfo(QApplication::font()).family());
    EXPECT_EQ(title_font_info.pixelSize(), 16);
    EXPECT_EQ(title_font_info.weight(), 600);
}

TEST_F(PreferencesPageTest, SyncLabelsUseApplicationTypographyInBothThemes) {
    auto* theme = passvault::ui::ThemeManager::Instance();
    const auto previous = theme->theme();
    const QFont previous_font = QApplication::font();
    page_.resize(840, 640);
    tabs()->setCurrentIndex(1);
    page_.show();
    for (auto value : {passvault::ui::Theme::kLight,
                       passvault::ui::Theme::kDark}) {
        theme->ApplyTheme(value);
        QApplication::processEvents();
        for (const char* name : {"SyncHeadingTitle", "SyncHeadingDesc",
                                 "SyncAuthTitle", "SyncAuthDesc",
                                 "SyncAccountTitle", "SyncAccountEmail",
                                 "SyncLastTitle", "SyncLastValue",
                                 "SyncStateText"}) {
            auto* label = page_.findChild<QLabel*>(QString::fromLatin1(name));
            EXPECT_NE(label, nullptr) << name;
            if (!label) continue;
            label->ensurePolished();
            EXPECT_EQ(QFontInfo(label->font()).family(),
                      QFontInfo(QApplication::font()).family()) << name;
        }
        const QString capture = qEnvironmentVariable("PASSVAULT_UI_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            page_.grab().save(capture + (value == passvault::ui::Theme::kLight
                                            ? "/sync-light.png" : "/sync-dark.png"));
        }
    }
    QFont application_font = previous_font;
    application_font.setFamily(QStringLiteral("Arial"));
    QApplication::setFont(application_font);
    theme->ApplyTheme(passvault::ui::Theme::kLight);
    EXPECT_EQ(QFontInfo(page_.findChild<QLabel*>("SyncHeadingTitle")->font()).family(),
              QFontInfo(QApplication::font()).family());
    QApplication::setFont(previous_font);
    theme->ApplyTheme(previous);
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
    auto* auto_lock = page.findChild<QComboBox*>(QStringLiteral("AutoLockCombo"));
    auto* back = page.findChild<QToolButton*>(QStringLiteral("PreferencesBack"));
    ASSERT_NE(tabs, nullptr);
    ASSERT_NE(auto_lock, nullptr);
    ASSERT_NE(back, nullptr);
    tabs->setCurrentIndex(2);
    auto_lock->setCurrentIndex(auto_lock->findData(30));
    const QByteArray light_icon = IconPixels(back->icon());

    theme->ApplyTheme(passvault::ui::Theme::kDark);

    EXPECT_NE(IconPixels(back->icon()), light_icon);
    EXPECT_EQ(tabs->currentIndex(), 2);
    EXPECT_EQ(auto_lock->currentData().toInt(), 30);
}

TEST_F(PreferencesPageTest, OuterCardMaxWidthIsDefined) {
    auto* card = outerCard();
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->maximumWidth(), 840);
}

TEST_F(PreferencesPageTest, OuterCardShrinksToContentNotViewport) {
    page_.resize(1200, 800);
    page_.show();
    QApplication::processEvents();

    auto* scroll =
        page_.findChild<QScrollArea*>(QStringLiteral("PreferencesScroll"));
    ASSERT_NE(scroll, nullptr);
    auto* card = outerCard();
    ASSERT_NE(card, nullptr);

    const int viewport_h = scroll->viewport()->height();
    // 内容少时卡片应按内容高度收缩，不被拉满整段滚动区。
    EXPECT_LT(card->height(), viewport_h);
}

TEST_F(PreferencesPageTest, GeneralTabKeepsSectionTitles) {
    auto* tabs = this->tabs();
    ASSERT_NE(tabs, nullptr);
    EXPECT_EQ(tabs->count(), 3);
    auto* tab_page = tabs->widget(0);
    ASSERT_NE(tab_page, nullptr);
    const auto titles =
        tab_page->findChildren<QLabel*>(QStringLiteral("SettingSectionTitle"));
    EXPECT_FALSE(titles.isEmpty());
    for (auto* title : titles) {
        EXPECT_FALSE(title->text().isEmpty());
    }
}

TEST_F(PreferencesPageTest, TabsSwitchBetweenPages) {
    page_.show();
    QApplication::processEvents();
    auto* tabs = this->tabs();
    ASSERT_NE(tabs, nullptr);
    EXPECT_EQ(tabs->count(), 3);

    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i);
        QApplication::processEvents();
        EXPECT_EQ(tabs->currentIndex(), i);
        EXPECT_TRUE(tabs->widget(i)->isVisible());
        for (int j = 0; j < tabs->count(); ++j) {
            if (j == i) continue;
            EXPECT_FALSE(tabs->widget(j)->isVisible());
        }
    }
}

TEST_F(PreferencesPageTest, DefaultValuesWhenUnset) {
    QSettings settings;
    settings.remove("session/auto_lock_minutes");
    settings.remove("ui/clipboard_seconds");
    PreferencesPage page;
    auto* auto_lock = page.findChild<QComboBox*>(QStringLiteral("AutoLockCombo"));
    auto* clipboard = page.findChild<QComboBox*>(QStringLiteral("ClipboardCombo"));
    auto* theme = page.findChild<QComboBox*>(QStringLiteral("ThemeCombo"));
    ASSERT_NE(auto_lock, nullptr);
    ASSERT_NE(clipboard, nullptr);
    ASSERT_NE(theme, nullptr);
    EXPECT_EQ(auto_lock->currentData().toInt(), 5);
    EXPECT_EQ(clipboard->currentData().toInt(), 30);
    EXPECT_EQ(theme->count(), 3);
}

TEST_F(PreferencesPageTest, TimeChoicesPersistAndLegacyValuesUseDefaults) {
    auto* auto_lock = autoLockCombo();
    auto* clipboard = clipboardCombo();
    ASSERT_NE(auto_lock, nullptr);
    ASSERT_NE(clipboard, nullptr);
    ASSERT_EQ(auto_lock->count(), 4);
    ASSERT_EQ(clipboard->count(), 5);
    for (int i = 0; i < auto_lock->count(); ++i) {
        EXPECT_EQ(auto_lock->itemData(i).toInt(), (QList<int>{5, 10, 30, 60})[i]);
        auto_lock->setCurrentIndex(i);
        PreferencesPage reopened;
        EXPECT_EQ(reopened.findChild<QComboBox*>("AutoLockCombo")->currentData(),
                  auto_lock->currentData());
    }
    for (int i = 0; i < clipboard->count(); ++i) {
        EXPECT_EQ(clipboard->itemData(i).toInt(),
                  (QList<int>{15, 30, 60, 120, 300})[i]);
        clipboard->setCurrentIndex(i);
        PreferencesPage reopened;
        EXPECT_EQ(reopened.findChild<QComboBox*>("ClipboardCombo")->currentData(),
                  clipboard->currentData());
    }
    QSettings().setValue("session/auto_lock_minutes", 17);
    QSettings().setValue("ui/clipboard_seconds", 42);
    PreferencesPage migrated;
    EXPECT_EQ(migrated.findChild<QComboBox*>("AutoLockCombo")->currentData().toInt(), 5);
    EXPECT_EQ(migrated.findChild<QComboBox*>("ClipboardCombo")->currentData().toInt(), 30);
    EXPECT_EQ(QSettings().value("session/auto_lock_minutes").toInt(), 5);
    EXPECT_EQ(QSettings().value("ui/clipboard_seconds").toInt(), 30);
}

}  // namespace

TEST_F(PreferencesPageTest, DefaultPasswordVisibilityDefaultsOffAndPersists) {
    QSettings().remove("ui/default_show_passwords");
    passvault::ui::PreferencesPage page;
    EXPECT_FALSE(page.DefaultPasswordVisible());
    QSignalSpy changed(&page, &passvault::ui::PreferencesPage::DefaultPasswordVisibilityChanged);
    auto* toggle = page.findChild<QCheckBox*>("DefaultShowPasswords");
    ASSERT_NE(toggle, nullptr);
    toggle->setChecked(true);
    ASSERT_EQ(changed.count(), 1);
    EXPECT_TRUE(changed.front().front().toBool());
    QSettings().sync();
    EXPECT_TRUE(QSettings().value("ui/default_show_passwords").toBool());
    passvault::ui::PreferencesPage reopened;
    EXPECT_TRUE(reopened.DefaultPasswordVisible());
    toggle->setChecked(false);
}

TEST_F(PreferencesPageTest, AccountConnectionStatesClearOldEmailAndOfferRetry) {
    page_.SetSyncState(PreferencesPage::SyncUiState::kConnected,
                       QStringLiteral("previous@example.invalid"));
    page_.SetSyncState(PreferencesPage::SyncUiState::kConnecting, QString());
    EXPECT_TRUE(accountEmail()->accessibleName().isEmpty());
    EXPECT_FALSE(syncNow()->isEnabled());
    auto* retry = page_.findChild<QPushButton*>("AccountRetryButton");
    ASSERT_NE(retry, nullptr);
    EXPECT_TRUE(retry->isHidden());
    page_.SetSyncState(PreferencesPage::SyncUiState::kConnectionError, QString());
    EXPECT_FALSE(retry->isHidden());
    EXPECT_TRUE(driveDisconnect()->isHidden());
    EXPECT_FALSE(syncNow()->isEnabled());
    QSignalSpy requested(&page_, &PreferencesPage::RetryAccountRequested);
    retry->click();
    EXPECT_EQ(requested.count(), 1);
    page_.SetSyncState(PreferencesPage::SyncUiState::kNeedsAuthorization, QString());
    EXPECT_FALSE(authorizeCard()->isHidden());
    EXPECT_TRUE(accountCard()->isHidden());
    EXPECT_TRUE(retry->isHidden());
}
