#include "ui/preferences_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/icon_loader.h"
#include "ui/theme_manager.h"

namespace passvault::ui {

namespace {

constexpr const char* kAutoLockKey = "session/auto_lock_minutes";
constexpr const char* kClipboardKey = "ui/clipboard_seconds";

constexpr int kHeaderHeight = 68;
constexpr int kOuterCardMaxWidth = 920;
constexpr int kIconBoxSize = 36;
constexpr int kIconGlyphSize = 18;

int LoadInt(const char* key, int fallback) {
    return QSettings().value(QString::fromLatin1(key), fallback).toInt();
}

void SaveInt(const char* key, int value) {
    QSettings().setValue(QString::fromLatin1(key), value);
}

void SetThemeIcon(QLabel* label, const QString& icon_name,
                  const QString& token = QStringLiteral("primary"),
                  int size = kIconGlyphSize) {
    label->setProperty("themeIconName", icon_name);
    label->setProperty("themeIconToken", token);
    label->setProperty("themeIconSize", size);
    label->setPixmap(
        IconLoader::Load(icon_name, ThemeManager::Instance()->Color(token), size)
            .pixmap(size, size));
}

QWidget* MakeSettingRow(const QString& icon_name, const QString& title,
                        const QString& desc, QWidget* control,
                        QWidget* parent) {
    auto* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("SettingRow"));

    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 12, 0, 12);
    h->setSpacing(14);

    auto* icon = new QLabel(row);
    icon->setObjectName(QStringLiteral("SettingRowIcon"));
    icon->setFixedSize(kIconBoxSize, kIconBoxSize);
    icon->setAlignment(Qt::AlignCenter);
    SetThemeIcon(icon, icon_name);
    h->addWidget(icon);

    auto* texts = new QWidget(row);
    auto* v = new QVBoxLayout(texts);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    auto* title_label = new QLabel(title, texts);
    title_label->setObjectName(QStringLiteral("SettingRowTitle"));
    auto* desc_label = new QLabel(desc, texts);
    desc_label->setObjectName(QStringLiteral("SettingRowDesc"));
    desc_label->setWordWrap(true);
    v->addWidget(title_label);
    v->addWidget(desc_label);
    h->addWidget(texts, 1);

    if (control) {
        control->setParent(row);
        h->addWidget(control);
    }
    return row;
}

QFrame* MakeRowSeparator(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setObjectName(QStringLiteral("SettingRowSeparator"));
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    return line;
}

}  // namespace

PreferencesPage::PreferencesPage(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("PreferencesPage"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(BuildHeader());

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("PreferencesScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* scroll_body = new QWidget;
    scroll_body->setObjectName(QStringLiteral("PreferencesBody"));
    auto* body_layout = new QHBoxLayout(scroll_body);
    body_layout->setContentsMargins(24, 24, 24, 40);
    body_layout->setSpacing(0);
    body_layout->addStretch(1);

    auto* card = new QFrame(scroll_body);
    card->setObjectName(QStringLiteral("PreferencesOuterCard"));
    card->setMaximumWidth(kOuterCardMaxWidth);
    card->setMinimumWidth(560);
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(0, 0, 0, 0);
    card_layout->setSpacing(0);

    auto* tabs = new QTabWidget(card);
    tabs->setObjectName(QStringLiteral("PreferencesTabs"));
    tabs->addTab(BuildGeneralTab(), QStringLiteral("常规"));
    tabs->addTab(BuildSyncTab(), QStringLiteral("同步"));
    tabs->addTab(BuildSecurityTab(), QStringLiteral("安全"));
    card_layout->addWidget(tabs);

    body_layout->addWidget(card, 3);
    body_layout->addStretch(1);

    scroll->setWidget(scroll_body);
    root->addWidget(scroll, 1);

    connect(ThemeManager::Instance(), &ThemeManager::ThemeChanged, this,
            [this](Theme) { RefreshThemeAssets(); });
}

QWidget* PreferencesPage::BuildHeader() {
    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("PreferencesHeader"));
    header->setFixedHeight(kHeaderHeight);

    auto* header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(0);

    auto* controls = new QWidget(header);
    controls->setObjectName(QStringLiteral("PreferencesHeaderControls"));
    auto* h = new QHBoxLayout(controls);
    h->setContentsMargins(24, 0, 24, 0);
    h->setSpacing(12);

    auto* back = new QToolButton(controls);
    back->setObjectName(QStringLiteral("PreferencesBack"));
    back->setIcon(IconLoader::Load(
        QStringLiteral("arrow-left"),
        ThemeManager::Instance()->Color(QStringLiteral("text-primary")), 16));
    back->setIconSize(QSize(16, 16));
    back->setFixedSize(32, 32);
    back->setCursor(Qt::PointingHandCursor);
    back->setToolTip(QStringLiteral("返回"));
    connect(back, &QToolButton::clicked, this, &PreferencesPage::BackRequested);
    h->addWidget(back);

    auto* title = new QLabel(QStringLiteral("设置"), controls);
    title->setObjectName(QStringLiteral("PreferencesHeaderTitle"));
    h->addWidget(title);
    h->addStretch(1);

    header_layout->addWidget(controls, 1);

    auto* divider = new QFrame(header);
    divider->setObjectName(QStringLiteral("PreferencesHeaderDivider"));
    divider->setFixedHeight(1);
    header_layout->addWidget(divider);
    return header;
}

QWidget* PreferencesPage::BuildGeneralTab() {
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("PreferencesTabPage"));
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(28, 20, 28, 24);
    v->setSpacing(0);

    theme_combo_ = new QComboBox;
    theme_combo_->setObjectName(QStringLiteral("ThemeCombo"));
    theme_combo_->addItem(QStringLiteral("跟随系统"),
                          static_cast<int>(Theme::kSystem));
    theme_combo_->addItem(QStringLiteral("浅色"),
                          static_cast<int>(Theme::kLight));
    theme_combo_->addItem(QStringLiteral("深色"),
                          static_cast<int>(Theme::kDark));
    const int current = static_cast<int>(ThemeManager::Instance()->theme());
    for (int i = 0; i < theme_combo_->count(); ++i) {
        if (theme_combo_->itemData(i).toInt() == current) {
            theme_combo_->setCurrentIndex(i);
            break;
        }
    }
    theme_combo_->setMinimumWidth(160);
    connect(theme_combo_,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                const int v = theme_combo_->itemData(idx).toInt();
                ThemeManager::Instance()->ApplyTheme(static_cast<Theme>(v));
                emit ThemeChanged(v);
            });
    v->addWidget(MakeSettingRow(QStringLiteral("settings-2"),
                                 QStringLiteral("主题"),
                                 QStringLiteral("界面外观：跟随系统 / 浅色 / 深色"),
                                 theme_combo_, page));
    v->addWidget(MakeRowSeparator(page));

    auto_lock_spin_ = new QSpinBox;
    auto_lock_spin_->setObjectName(QStringLiteral("AutoLockSpin"));
    auto_lock_spin_->setRange(1, 120);
    auto_lock_spin_->setSuffix(QStringLiteral(" 分钟"));
    auto_lock_spin_->setValue(LoadInt(kAutoLockKey, 5));
    auto_lock_spin_->setMinimumWidth(140);
    connect(auto_lock_spin_,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
            [this](int val) {
                SaveInt(kAutoLockKey, val);
                emit AutoLockChanged(val);
            });
    v->addWidget(MakeSettingRow(QStringLiteral("lock-keyhole"),
                                 QStringLiteral("空闲自动锁定"),
                                 QStringLiteral("无操作超过设定时间后自动锁定应用"),
                                 auto_lock_spin_, page));
    v->addWidget(MakeRowSeparator(page));

    clipboard_spin_ = new QSpinBox;
    clipboard_spin_->setObjectName(QStringLiteral("ClipboardSpin"));
    clipboard_spin_->setRange(5, 300);
    clipboard_spin_->setSuffix(QStringLiteral(" 秒"));
    clipboard_spin_->setValue(LoadInt(kClipboardKey, 30));
    clipboard_spin_->setMinimumWidth(140);
    connect(clipboard_spin_,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
            [this](int val) {
                SaveInt(kClipboardKey, val);
                emit ClipboardTimeoutChanged(val);
            });
    v->addWidget(MakeSettingRow(QStringLiteral("copy"),
                                 QStringLiteral("剪贴板自动清空"),
                                 QStringLiteral("复制密码后经过设定时间自动清除"),
                                 clipboard_spin_, page));

    v->addStretch(1);
    return page;
}

QWidget* PreferencesPage::BuildSyncTab() {
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("PreferencesTabPage"));
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(28, 20, 28, 24);
    v->setSpacing(0);

    auto* drive_actions = new QWidget;
    auto* drive_row = new QHBoxLayout(drive_actions);
    drive_row->setContentsMargins(0, 0, 0, 0);
    drive_row->setSpacing(8);
    drive_connect_button_ = new QPushButton(QStringLiteral("连接..."));
    drive_connect_button_->setObjectName(QStringLiteral("DriveConnectButton"));
    drive_connect_button_->setProperty("accent", true);
    drive_connect_button_->setVisible(true);
    connect(drive_connect_button_, &QPushButton::clicked, this,
            &PreferencesPage::ConnectGoogleDriveRequested);
    drive_disconnect_button_ = new QPushButton(QStringLiteral("断开"));
    drive_disconnect_button_->setObjectName(QStringLiteral("DriveDisconnectButton"));
    drive_disconnect_button_->setVisible(false);
    connect(drive_disconnect_button_, &QPushButton::clicked, this,
            &PreferencesPage::DisconnectGoogleDriveRequested);
    drive_row->addWidget(drive_connect_button_);
    drive_row->addWidget(drive_disconnect_button_);

    drive_status_label_ = new QLabel(QStringLiteral("未连接"));
    drive_status_label_->setObjectName(QStringLiteral("SettingRowStatus"));

    auto* drive_wrap = new QWidget(page);
    auto* wrap_row = new QHBoxLayout(drive_wrap);
    wrap_row->setContentsMargins(0, 12, 0, 12);
    wrap_row->setSpacing(14);
    auto* drive_icon = new QLabel(drive_wrap);
    drive_icon->setObjectName(QStringLiteral("SettingRowIcon"));
    drive_icon->setFixedSize(kIconBoxSize, kIconBoxSize);
    drive_icon->setAlignment(Qt::AlignCenter);
    SetThemeIcon(drive_icon, QStringLiteral("cloud"));
    wrap_row->addWidget(drive_icon);

    auto* drive_texts = new QWidget(drive_wrap);
    auto* drive_texts_v = new QVBoxLayout(drive_texts);
    drive_texts_v->setContentsMargins(0, 0, 0, 0);
    drive_texts_v->setSpacing(2);
    auto* drive_title = new QLabel(QStringLiteral("Google Drive"), drive_texts);
    drive_title->setObjectName(QStringLiteral("SettingRowTitle"));
    drive_texts_v->addWidget(drive_title);
    drive_texts_v->addWidget(drive_status_label_);
    wrap_row->addWidget(drive_texts, 1);
    wrap_row->addWidget(drive_actions);
    v->addWidget(drive_wrap);
    v->addWidget(MakeRowSeparator(page));

    sync_now_button_ = new QPushButton(QStringLiteral("立即同步"));
    sync_now_button_->setObjectName(QStringLiteral("SyncNowButton"));
    sync_now_button_->setEnabled(false);
    connect(sync_now_button_, &QPushButton::clicked, this,
            &PreferencesPage::SyncNowRequested);
    v->addWidget(MakeSettingRow(QStringLiteral("cloud"),
                                 QStringLiteral("立即同步"),
                                 QStringLiteral("手动触发一次云端同步"),
                                 sync_now_button_, page));
    v->addWidget(MakeRowSeparator(page));

    auto* import_button = new QPushButton(QStringLiteral("导入..."));
    import_button->setObjectName(QStringLiteral("ImportButton"));
    connect(import_button, &QPushButton::clicked, this,
            &PreferencesPage::ImportCsvRequested);
    v->addWidget(MakeSettingRow(QStringLiteral("folder-open"),
                                 QStringLiteral("导入 CSV"),
                                 QStringLiteral("从 CSV 文件批量导入密码"),
                                 import_button, page));
    v->addWidget(MakeRowSeparator(page));

    auto* export_button = new QPushButton(QStringLiteral("导出..."));
    export_button->setObjectName(QStringLiteral("ExportButton"));
    connect(export_button, &QPushButton::clicked, this,
            &PreferencesPage::ExportCsvRequested);
    v->addWidget(MakeSettingRow(QStringLiteral("folder"),
                                 QStringLiteral("导出 CSV"),
                                 QStringLiteral("将全部密码导出为 CSV 文件"),
                                 export_button, page));

    v->addStretch(1);
    return page;
}

QWidget* PreferencesPage::BuildSecurityTab() {
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("PreferencesTabPage"));
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(28, 20, 28, 24);
    v->setSpacing(0);

    auto* change_master = new QPushButton(QStringLiteral("暂不可用"));
    change_master->setObjectName(QStringLiteral("ChangeMasterButton"));
    change_master->setEnabled(false);
    change_master->setToolTip(
        QStringLiteral("完整重加密与失败回滚完成前暂不可用"));
    v->addWidget(MakeSettingRow(
        QStringLiteral("key-round"), QStringLiteral("主密码"),
        QStringLiteral("完整重加密与失败回滚完成前暂不支持修改主密码"),
        change_master, page));
    v->addWidget(MakeRowSeparator(page));

    hello_toggle_ = new QCheckBox;
    hello_toggle_->setObjectName(QStringLiteral("HelloToggle"));
    hello_toggle_->setProperty("variant", "switch");
    hello_toggle_->setEnabled(false);
    connect(hello_toggle_, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            emit EnableHelloRequested();
        } else {
            emit DisableHelloRequested();
        }
    });

    hello_desc_ = new QLabel(QStringLiteral("检测中..."));
    hello_desc_->setObjectName(QStringLiteral("SettingRowDesc"));
    hello_desc_->setWordWrap(true);

    auto* hello_wrap = new QWidget(page);
    auto* hello_row = new QHBoxLayout(hello_wrap);
    hello_row->setContentsMargins(0, 12, 0, 12);
    hello_row->setSpacing(14);
    auto* hello_icon = new QLabel(hello_wrap);
    hello_icon->setObjectName(QStringLiteral("SettingRowIcon"));
    hello_icon->setFixedSize(kIconBoxSize, kIconBoxSize);
    hello_icon->setAlignment(Qt::AlignCenter);
    SetThemeIcon(hello_icon, QStringLiteral("shield-check"));
    hello_row->addWidget(hello_icon);

    auto* hello_texts = new QWidget(hello_wrap);
    auto* hello_texts_v = new QVBoxLayout(hello_texts);
    hello_texts_v->setContentsMargins(0, 0, 0, 0);
    hello_texts_v->setSpacing(2);
    auto* hello_title = new QLabel(QStringLiteral("Windows Hello"), hello_texts);
    hello_title->setObjectName(QStringLiteral("SettingRowTitle"));
    hello_texts_v->addWidget(hello_title);
    hello_texts_v->addWidget(hello_desc_);
    hello_row->addWidget(hello_texts, 1);
    hello_row->addWidget(hello_toggle_);
    v->addWidget(hello_wrap);

    v->addStretch(1);
    return page;
}

void PreferencesPage::RefreshThemeAssets() {
    if (auto* back = findChild<QToolButton*>(
            QStringLiteral("PreferencesBack"))) {
        back->setIcon(IconLoader::Load(
            QStringLiteral("arrow-left"),
            ThemeManager::Instance()->Color(QStringLiteral("text-primary")),
            16));
    }
    const auto icons =
        findChildren<QLabel*>(QStringLiteral("SettingRowIcon"));
    for (auto* icon : icons) {
        const QString name = icon->property("themeIconName").toString();
        const QString token = icon->property("themeIconToken").toString();
        const int size = icon->property("themeIconSize").toInt();
        if (name.isEmpty() || token.isEmpty() || size <= 0) continue;
        icon->setPixmap(IconLoader::Load(
                            name, ThemeManager::Instance()->Color(token), size)
                            .pixmap(size, size));
    }
}

void PreferencesPage::SetHelloAvailable(bool available) {
    if (!hello_toggle_) return;
    hello_toggle_->setEnabled(available);
    if (hello_desc_) {
        hello_desc_->setText(
            available
                ? QStringLiteral("已启用 Windows Hello。开启后解锁时可跳过主密码。")
                : QStringLiteral("当前设备未启用 Windows Hello 或不支持"));
    }
}

void PreferencesPage::SetHelloEnabled(bool enabled) {
    if (!hello_toggle_) return;
    QSignalBlocker b(hello_toggle_);
    hello_toggle_->setChecked(enabled);
}

void PreferencesPage::SetGoogleDriveConnected(bool connected,
                                              const QString& account_hint) {
    if (!drive_status_label_) return;
    drive_status_label_->setText(
        connected ? QStringLiteral("已连接 %1")
                        .arg(account_hint.isEmpty()
                                 ? QStringLiteral("Google Drive")
                                 : account_hint)
                  : QStringLiteral("未连接"));
    if (drive_connect_button_) drive_connect_button_->setVisible(!connected);
    if (drive_disconnect_button_)
        drive_disconnect_button_->setVisible(connected);
    if (sync_now_button_) sync_now_button_->setEnabled(connected);
}

}  // namespace passvault::ui
