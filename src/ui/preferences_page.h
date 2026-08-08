#pragma once

#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace passvault::ui {

class PreferencesPage : public QWidget {
    Q_OBJECT

 public:
    explicit PreferencesPage(QWidget* parent = nullptr);

 signals:
    void ThemeChanged(int theme_index);
    void AutoLockChanged(int minutes);
    void ClipboardTimeoutChanged(int seconds);

    void ConnectGoogleDriveRequested();
    void DisconnectGoogleDriveRequested();
    void SyncNowRequested();
    void ImportCsvRequested();
    void ExportCsvRequested();

    void ChangeMasterPasswordRequested();
    void EnableHelloRequested();
    void DisableHelloRequested();

    void BackRequested();

 public slots:
    void SetHelloAvailable(bool available);
    void SetHelloEnabled(bool enabled);
    void SetGoogleDriveConnected(bool connected, const QString& account_hint);

 private:
    QWidget* BuildHeader();
    QWidget* BuildGeneralTab();
    QWidget* BuildSyncTab();
    QWidget* BuildSecurityTab();
    void RefreshThemeAssets();

    QComboBox* theme_combo_ = nullptr;
    QSpinBox* auto_lock_spin_ = nullptr;
    QSpinBox* clipboard_spin_ = nullptr;

    QLabel* drive_status_label_ = nullptr;
    QPushButton* drive_connect_button_ = nullptr;
    QPushButton* drive_disconnect_button_ = nullptr;
    QPushButton* sync_now_button_ = nullptr;

    QCheckBox* hello_toggle_ = nullptr;
    QLabel* hello_desc_ = nullptr;
};

}  // namespace passvault::ui
