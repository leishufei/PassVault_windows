#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTabWidget;

namespace passvault::ui {

class ElidedLabel;

class PreferencesPage : public QWidget {
    Q_OBJECT

 public:
    explicit PreferencesPage(QWidget* parent = nullptr);
    bool DefaultPasswordVisible() const;

    enum class SyncUiState {
        kDisconnected,
        kAuthorizing,
        kConnected,
        kSyncing,
        kSuccess,
        kConflict,
        kError,
        kConnecting,
        kConnectionError,
        kNeedsAuthorization,
        kMissingEmail,
    };

 signals:
    void ThemeChanged(int theme_index);
    void AutoLockChanged(int minutes);
    void ClipboardTimeoutChanged(int seconds);
    void DefaultPasswordVisibilityChanged(bool visible);

    void ConnectGoogleDriveRequested();
    void AuthorizeCancelled();
    void DisconnectGoogleDriveRequested();
    void SyncNowRequested();
    void RetryAccountRequested();
    void ImportCsvRequested();
    void ExportCsvRequested();

    void ChangeMasterPasswordRequested();
    void ClearLocalDataRequested();

    void BackRequested();

 public slots:
    void SetChangeMasterPasswordEnabled(bool enabled, const QString& reason);
    void SetGoogleDriveConnected(bool connected, const QString& account_hint);
    void SetSyncState(SyncUiState state, const QString& account_hint,
                      const QString& message = QString());
    void SetLastSyncAt(std::int64_t epoch_ms);

 private:
    QWidget* BuildHeader();
    QWidget* BuildGeneralTab();
    QWidget* BuildSyncTab();
    QWidget* BuildSecurityTab();
    QWidget* BuildAuthorizeCard();
    QWidget* BuildAccountCard();
    QWidget* BuildLastSyncCard();
    void RefreshThemeAssets();
    void ApplySyncState(SyncUiState state, const QString& message);
    void RefreshLastSyncLabel();

    QCheckBox* default_password_visible_ = nullptr;
    QComboBox* theme_combo_ = nullptr;
    QComboBox* auto_lock_combo_ = nullptr;
    QComboBox* clipboard_combo_ = nullptr;

    QPushButton* drive_connect_button_ = nullptr;
    QPushButton* drive_disconnect_button_ = nullptr;
    QPushButton* sync_now_button_ = nullptr;
    QPushButton* account_retry_button_ = nullptr;

    QWidget* sync_authorize_card_ = nullptr;
    QWidget* sync_account_card_ = nullptr;
    QWidget* sync_last_card_ = nullptr;
    QWidget* sync_state_row_ = nullptr;
    ElidedLabel* sync_account_email_ = nullptr;
    QLabel* sync_last_value_ = nullptr;
    QLabel* sync_state_dot_ = nullptr;
    QLabel* sync_state_text_ = nullptr;
    QPushButton* sync_change_account_button_ = nullptr;
    QString sync_account_hint_;
    std::int64_t sync_last_ms_ = 0;

    QPushButton* change_master_button_ = nullptr;
    QLabel* change_master_desc_ = nullptr;
};

}  // namespace passvault::ui
