#include "ui/preferences_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
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
constexpr const char* kLastSyncKey = "sync/last_sync_ms";

constexpr int kHeaderHeight = 68;
constexpr int kOuterCardMaxWidth = 840;
constexpr int kIconBoxSize = 36;
constexpr int kIconGlyphSize = 18;
constexpr int kSyncCardGap = 16;
constexpr int kSyncCardPad = 16;

int LoadInt(const char* key, int fallback) {
    return QSettings().value(QString::fromLatin1(key), fallback).toInt();
}

void SaveInt(const char* key, int value) {
    QSettings().setValue(QString::fromLatin1(key), value);
}

std::int64_t LoadLastSyncMs() {
    return QSettings()
        .value(QString::fromLatin1(kLastSyncKey), 0)
        .toLongLong();
}

void SaveLastSyncMs(std::int64_t value) {
    QSettings().setValue(QString::fromLatin1(kLastSyncKey),
                         static_cast<qlonglong>(value));
}

QString FormatLastSync(std::int64_t epoch_ms) {
    if (epoch_ms <= 0) return QStringLiteral("从未同步");
    return QDateTime::fromMSecsSinceEpoch(epoch_ms)
        .toString(QStringLiteral("yyyy/MM/dd HH:mm"));
}

const char* SyncStateName(PreferencesPage::SyncUiState state) {
    switch (state) {
        case PreferencesPage::SyncUiState::kSyncing:
            return "busy";
        case PreferencesPage::SyncUiState::kSuccess:
            return "ok";
        case PreferencesPage::SyncUiState::kConflict:
            return "conflict";
        case PreferencesPage::SyncUiState::kError:
            return "error";
        case PreferencesPage::SyncUiState::kDisconnected:
        case PreferencesPage::SyncUiState::kAuthorizing:
        case PreferencesPage::SyncUiState::kConnected:
        default:
            return "idle";
    }
}

QString GoogleMarkStyle() {
    return QStringLiteral(
        "background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: 18px; font-size: 15px; font-weight: 700;")
        .arg(ThemeManager::Instance()->ColorHex(QStringLiteral("card")),
             ThemeManager::Instance()->ColorHex(
                 QStringLiteral("brand-google-fg")),
             ThemeManager::Instance()->ColorHex(
                 QStringLiteral("border-section-3")));
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

QWidget* MakeSettingRowWithLabel(const QString& icon_name, const QString& title,
                                 QLabel* desc_label, QWidget* control,
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
    v->addWidget(title_label);
    v->addWidget(desc_label);
    h->addWidget(texts, 1);

    if (control) {
        control->setParent(row);
        h->addWidget(control);
    }
    return row;
}

QWidget* MakeSettingRow(const QString& icon_name, const QString& title,
                        const QString& desc, QWidget* control,
                        QWidget* parent) {
    auto* desc_label = new QLabel(desc);
    desc_label->setObjectName(QStringLiteral("SettingRowDesc"));
    desc_label->setWordWrap(true);
    return MakeSettingRowWithLabel(icon_name, title, desc_label, control, parent);
}

QFrame* MakeRowSeparator(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setObjectName(QStringLiteral("SettingRowSeparator"));
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    return line;
}

QWidget* MakeSectionHeader(const QString& title, QWidget* parent) {
    auto* header = new QWidget(parent);
    header->setObjectName(QStringLiteral("SettingSectionHeader"));
    auto* v = new QVBoxLayout(header);
    v->setContentsMargins(0, 12, 0, 4);
    v->setSpacing(0);
    auto* label = new QLabel(title, header);
    label->setObjectName(QStringLiteral("SettingSectionTitle"));
    v->addWidget(label);
    return header;
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

    // 交叉轴(垂直)按内容高度对齐：内容少时卡片不再被 QHBoxLayout 拉满整段
    // 滚动区高度，而是贴合内容；内容多时卡片仍按 sizeHint 撑高滚动区并触发滚动。
    body_layout->addWidget(card, 3, Qt::AlignTop);
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
    v->addWidget(MakeSectionHeader(QStringLiteral("外观"), page));
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
    v->addWidget(MakeSectionHeader(QStringLiteral("安全"), page));
    v->addWidget(MakeSettingRow(QStringLiteral("lock-keyhole"),
                                 QStringLiteral("空闲自动锁定"),
                                 QStringLiteral("无操作超过设定时间后自动锁定应用"),
                                 auto_lock_spin_, page));
    v->addWidget(MakeRowSeparator(page));

    v->addWidget(MakeSectionHeader(QStringLiteral("隐私"), page));
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
    v->addWidget(MakeRowSeparator(page));

    v->addWidget(MakeSectionHeader(QStringLiteral("数据导入与导出"), page));
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

QWidget* PreferencesPage::BuildAuthorizeCard() {
    auto* card = new QWidget;
    card->setObjectName(QStringLiteral("SyncAuthCard"));
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(kSyncCardPad, kSyncCardPad, kSyncCardPad,
                          kSyncCardPad);
    v->setSpacing(0);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(12);
    auto* mark = new QLabel(QStringLiteral("G"), card);
    mark->setObjectName(QStringLiteral("SyncGoogleMark"));
    mark->setFixedSize(36, 36);
    mark->setAlignment(Qt::AlignCenter);
    mark->setStyleSheet(GoogleMarkStyle());
    top->addWidget(mark);

    auto* texts = new QWidget(card);
    auto* tv = new QVBoxLayout(texts);
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("通过 Google 认证"), texts);
    title->setObjectName(QStringLiteral("SyncAuthTitle"));
    tv->addWidget(title);
    auto* desc =
        new QLabel(QStringLiteral("授权后将绑定此保险库的唯一同步账号。"),
                   texts);
    desc->setObjectName(QStringLiteral("SyncAuthDesc"));
    desc->setWordWrap(true);
    tv->addWidget(desc);
    top->addWidget(texts, 1);
    v->addLayout(top);

    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, 16, 0, 0);
    actions->setSpacing(8);
    auto* cancel = new QPushButton(QStringLiteral("取消"), card);
    cancel->setObjectName(QStringLiteral("AuthorizeCancelButton"));
    connect(cancel, &QPushButton::clicked, this,
            &PreferencesPage::AuthorizeCancelled);
    drive_connect_button_ = new QPushButton(QStringLiteral("继续使用 Google"),
                                            card);
    drive_connect_button_->setObjectName(QStringLiteral("DriveConnectButton"));
    drive_connect_button_->setProperty("accent", true);
    connect(drive_connect_button_, &QPushButton::clicked, this,
            &PreferencesPage::ConnectGoogleDriveRequested);
    actions->addStretch(1);
    actions->addWidget(cancel);
    actions->addWidget(drive_connect_button_);
    v->addLayout(actions);
    return card;
}

QWidget* PreferencesPage::BuildAccountCard() {
    auto* card = new QWidget;
    card->setObjectName(QStringLiteral("SyncAccountCard"));
    auto* h = new QHBoxLayout(card);
    h->setContentsMargins(kSyncCardPad, kSyncCardPad, kSyncCardPad,
                          kSyncCardPad);
    h->setSpacing(12);

    auto* mark = new QLabel(QStringLiteral("G"), card);
    mark->setObjectName(QStringLiteral("SyncGoogleMark"));
    mark->setFixedSize(36, 36);
    mark->setAlignment(Qt::AlignCenter);
    mark->setStyleSheet(GoogleMarkStyle());
    h->addWidget(mark);

    auto* texts = new QWidget(card);
    auto* tv = new QVBoxLayout(texts);
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("Google"), texts);
    title->setObjectName(QStringLiteral("SyncAccountTitle"));
    tv->addWidget(title);
    sync_account_email_ = new QLabel(QStringLiteral("未连接"), texts);
    sync_account_email_->setObjectName(QStringLiteral("SyncAccountEmail"));
    sync_account_email_->setWordWrap(false);
    tv->addWidget(sync_account_email_);
    h->addWidget(texts, 1);

    sync_change_account_button_ =
        new QPushButton(QStringLiteral("更换账号"), card);
    sync_change_account_button_->setObjectName(
        QStringLiteral("ChangeAccountButton"));
    connect(sync_change_account_button_, &QPushButton::clicked, this,
            &PreferencesPage::ConnectGoogleDriveRequested);
    h->addWidget(sync_change_account_button_);

    drive_disconnect_button_ = new QPushButton(QStringLiteral("断开"), card);
    drive_disconnect_button_->setObjectName(
        QStringLiteral("DriveDisconnectButton"));
    connect(drive_disconnect_button_, &QPushButton::clicked, this,
            &PreferencesPage::DisconnectGoogleDriveRequested);
    h->addWidget(drive_disconnect_button_);
    return card;
}

QWidget* PreferencesPage::BuildLastSyncCard() {
    auto* card = new QWidget;
    card->setObjectName(QStringLiteral("SyncLastCard"));
    auto* h = new QHBoxLayout(card);
    h->setContentsMargins(kSyncCardPad, kSyncCardPad, kSyncCardPad,
                          kSyncCardPad);
    h->setSpacing(12);

    auto* texts = new QWidget(card);
    auto* tv = new QVBoxLayout(texts);
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("上次同步"), texts);
    title->setObjectName(QStringLiteral("SyncLastTitle"));
    tv->addWidget(title);
    sync_last_value_ = new QLabel(texts);
    sync_last_value_->setObjectName(QStringLiteral("SyncLastValue"));
    tv->addWidget(sync_last_value_);
    h->addWidget(texts, 1);

    sync_now_button_ = new QPushButton(QStringLiteral("立即同步"), card);
    sync_now_button_->setObjectName(QStringLiteral("SyncNowButton"));
    sync_now_button_->setProperty("accent", true);
    connect(sync_now_button_, &QPushButton::clicked, this,
            &PreferencesPage::SyncNowRequested);
    h->addWidget(sync_now_button_);
    return card;
}

QWidget* PreferencesPage::BuildSyncTab() {
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("PreferencesTabPage"));
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(28, 20, 28, 24);
    v->setSpacing(0);

    auto* heading = new QWidget(page);
    auto* hv = new QVBoxLayout(heading);
    hv->setContentsMargins(0, 0, 0, 16);
    hv->setSpacing(2);
    auto* heading_title = new QLabel(QStringLiteral("Google 同步"), heading);
    heading_title->setObjectName(QStringLiteral("SyncHeadingTitle"));
    hv->addWidget(heading_title);
    auto* heading_desc =
        new QLabel(QStringLiteral("使用 Google 账号安全地同步加密保险库"),
                   heading);
    heading_desc->setObjectName(QStringLiteral("SyncHeadingDesc"));
    hv->addWidget(heading_desc);
    v->addWidget(heading);

    sync_authorize_card_ = BuildAuthorizeCard();
    v->addWidget(sync_authorize_card_);

    sync_account_card_ = BuildAccountCard();
    sync_account_card_->setVisible(false);
    v->addWidget(sync_account_card_);

    sync_last_card_ = BuildLastSyncCard();
    sync_last_card_->setVisible(false);
    v->addWidget(sync_last_card_);

    auto* state_row = new QWidget(page);
    state_row->setObjectName(QStringLiteral("SyncStateRow"));
    auto* sr = new QHBoxLayout(state_row);
    sr->setContentsMargins(0, 12, 0, 0);
    sr->setSpacing(8);
    sync_state_dot_ = new QLabel(state_row);
    sync_state_dot_->setObjectName(QStringLiteral("SyncStateDot"));
    sync_state_dot_->setFixedSize(8, 8);
    sync_state_dot_->setProperty("state", QStringLiteral("idle"));
    sr->addWidget(sync_state_dot_);
    sync_state_text_ = new QLabel(state_row);
    sync_state_text_->setObjectName(QStringLiteral("SyncStateText"));
    sync_state_text_->setWordWrap(true);
    sr->addWidget(sync_state_text_, 1);
    v->addWidget(state_row);

    auto* hint_row = new QWidget(page);
    hint_row->setObjectName(QStringLiteral("SyncE2eHintRow"));
    auto* hr = new QHBoxLayout(hint_row);
    hr->setContentsMargins(0, 12, 0, 0);
    hr->setSpacing(6);
    auto* hint_icon = new QLabel(hint_row);
    hint_icon->setObjectName(QStringLiteral("SyncE2eHintIcon"));
    SetThemeIcon(hint_icon, QStringLiteral("cloud"),
                 QStringLiteral("primary-icon-cloud"), 16);
    hr->addWidget(hint_icon);
    auto* hint = new QLabel(
        QStringLiteral("同步前会在本设备完成端到端加密"), hint_row);
    hint->setObjectName(QStringLiteral("SyncE2eHint"));
    hr->addWidget(hint, 1);
    v->addWidget(hint_row);

    sync_last_ms_ = LoadLastSyncMs();
    RefreshLastSyncLabel();
    ApplySyncState(SyncUiState::kDisconnected, QString());
    v->addStretch(1);
    return page;
}

QWidget* PreferencesPage::BuildSecurityTab() {
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("PreferencesTabPage"));
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(28, 20, 28, 24);
    v->setSpacing(0);

    v->addWidget(MakeSectionHeader(QStringLiteral("主密码"), page));
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

    v->addWidget(MakeSectionHeader(QStringLiteral("Windows Hello"), page));
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

    v->addWidget(MakeSettingRowWithLabel(
        QStringLiteral("shield-check"), QStringLiteral("Windows Hello"),
        hello_desc_, hello_toggle_, page));

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
    SetSyncState(connected ? SyncUiState::kConnected : SyncUiState::kDisconnected,
                 account_hint);
}

void PreferencesPage::RefreshLastSyncLabel() {
    if (!sync_last_value_) return;
    sync_last_value_->setText(FormatLastSync(sync_last_ms_));
}

void PreferencesPage::ApplySyncState(SyncUiState state,
                                     const QString& message) {
    const bool authorized = state == SyncUiState::kConnected ||
                            state == SyncUiState::kSyncing ||
                            state == SyncUiState::kSuccess ||
                            state == SyncUiState::kConflict ||
                            state == SyncUiState::kError;

    if (sync_authorize_card_) sync_authorize_card_->setVisible(!authorized);
    if (sync_account_card_) sync_account_card_->setVisible(authorized);
    if (sync_last_card_) sync_last_card_->setVisible(authorized);
    if (sync_now_button_) {
        sync_now_button_->setEnabled(authorized &&
                                     state != SyncUiState::kSyncing);
        sync_now_button_->setText(state == SyncUiState::kSyncing
                                      ? QStringLiteral("同步中...")
                                      : QStringLiteral("立即同步"));
    }

    QString text;
    switch (state) {
        case SyncUiState::kAuthorizing:
            text = QStringLiteral("等待授权...");
            break;
        case SyncUiState::kSyncing:
            text = QStringLiteral("同步中...");
            break;
        case SyncUiState::kConflict:
            text = QStringLiteral("同步冲突，请稍后重试");
            break;
        case SyncUiState::kError:
            text = message.isEmpty()
                       ? QStringLiteral("同步失败")
                       : QStringLiteral("同步失败：%1").arg(message);
            break;
        case SyncUiState::kDisconnected:
        case SyncUiState::kConnected:
        case SyncUiState::kSuccess:
        default:
            break;
    }
    if (sync_state_text_) sync_state_text_->setText(text);

    if (sync_state_dot_) {
        sync_state_dot_->setProperty(
            "state", QString::fromLatin1(SyncStateName(state)));
        sync_state_dot_->style()->unpolish(sync_state_dot_);
        sync_state_dot_->style()->polish(sync_state_dot_);
    }
}

void PreferencesPage::SetSyncState(SyncUiState state,
                                   const QString& account_hint,
                                   const QString& message) {
    if (!account_hint.isEmpty()) sync_account_hint_ = account_hint;
    if (sync_account_email_ && !sync_account_hint_.isEmpty()) {
        sync_account_email_->setText(sync_account_hint_);
    }
    if (state == SyncUiState::kDisconnected) {
        sync_account_hint_.clear();
        if (sync_account_email_) {
            sync_account_email_->setText(QStringLiteral("未连接"));
        }
    }
    if (state == SyncUiState::kSuccess) {
        sync_last_ms_ = QDateTime::currentMSecsSinceEpoch();
        SaveLastSyncMs(sync_last_ms_);
        RefreshLastSyncLabel();
    }
    ApplySyncState(state, message);
}

void PreferencesPage::SetLastSyncAt(std::int64_t epoch_ms) {
    sync_last_ms_ = epoch_ms;
    SaveLastSyncMs(epoch_ms);
    RefreshLastSyncLabel();
}

}  // namespace passvault::ui
