#include "app/application.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QString>
#include <QTextStream>
#include <QTimer>

#include <memory>
#include <utility>

#include "crypto/secure_bytes.h"
#include "crypto/session_key.h"
#include "csv/csv_exporter.h"
#include "csv/csv_importer.h"
#include "csv/csv_reader.h"
#include "csv/csv_validator.h"
#include "hello/windows_hello_unlock.h"
#include "master_password/master_password_manager.h"
#include "master_password/master_password_store.h"
#include "model/category.h"
#include "model/password_entry.h"
#include "oauth/google_oauth_client.h"
#include "oauth/token_store.h"
#include "session/auto_lock_timer.h"
#include "session/session_manager.h"
#include "storage/category_dao.h"
#include "storage/database.h"
#include "storage/password_dao.h"
#include "storage/schema.h"
#include "sync/google_drive_provider.h"
#include "sync/sync_manager.h"
#include "sync/sync_scheduler.h"
#include "ui/clipboard_manager.h"
#include "ui/import_preview_dialog.h"
#include "ui/main_window.h"
#include "ui/master_password_dialog.h"
#include "ui/oauth_wizard_dialog.h"
#include "ui/preferences_page.h"
#include "ui/theme_manager.h"
#include "ui/toast.h"

#include "passvault/oauth/google_oauth_config.h"

namespace passvault::app {

namespace {

constexpr const char* kSettingsHelloEnabled = "session/hello_enabled";

QString DefaultDbPath() {
    return QCoreApplication::applicationDirPath() +
           QStringLiteral("/data/passvault.db");
}

}  // namespace

class Application::Impl {
 public:
    Impl() = default;

    int Run() {
        auto* theme = ui::ThemeManager::Instance();
        if (auto* q = qApp) {
            q->setStyleSheet(theme->LoadStyleSheet());
        }

        QDir().mkpath(QCoreApplication::applicationDirPath() +
                      QStringLiteral("/data"));

        database_ = storage::Database::Open(DefaultDbPath());
        if (!database_) {
            QMessageBox::critical(nullptr, QStringLiteral("启动失败"),
                                   QStringLiteral("无法打开数据库。"));
            return 1;
        }
        try {
            storage::EnsureCurrentSchema(*database_);
        } catch (const std::exception& e) {
            QMessageBox::critical(nullptr, QStringLiteral("启动失败"),
                                   QString::fromUtf8(e.what()));
            return 1;
        }

        password_dao_ = std::make_unique<storage::PasswordDao>(*database_);
        category_dao_ = std::make_unique<storage::CategoryDao>(*database_);
        master_store_ = std::make_unique<master_password::MasterPasswordStore>();
        master_manager_ =
            std::make_unique<master_password::MasterPasswordManager>(
                *master_store_);
        hello_ = std::make_unique<hello::WindowsHelloUnlock>();
        token_store_ = std::make_unique<oauth::TokenStore>();
        oauth_client_ =
            std::make_unique<oauth::GoogleOAuthClient>(token_store_.get());
        oauth_client_->set_client_id(
            QString::fromLatin1(oauth::kGoogleDesktopClientId));
        oauth_client_->set_client_secret(
            QString::fromLatin1(oauth::kGoogleDesktopClientSecret));
        oauth_client_->set_scope(
            QStringLiteral("https://www.googleapis.com/auth/drive.file"));

        drive_provider_ = std::make_unique<sync::GoogleDriveProvider>(
            [this]() { return oauth_client_->access_token(); });
        sync_manager_ = std::make_unique<sync::SyncManager>(
            password_dao_.get(), category_dao_.get(),
            session::SessionManager::Instance());
        sync_scheduler_ =
            std::make_unique<sync::SyncScheduler>(sync_manager_.get());
        auto_lock_ = std::make_unique<session::AutoLockTimer>();

        QObject::connect(session::SessionManager::Instance(),
                          &session::SessionManager::LockChanged,
                          [this](bool locked) {
                              if (locked) {
                                  OnLocked();
                              }
                          });
        QObject::connect(auto_lock_.get(), &session::AutoLockTimer::TimedOut,
                          []() {
                              session::SessionManager::Instance()->Lock();
                          });

        if (!UnlockFlow()) return 0;
        MaybeConfigureSyncProvider();
        ShowMainWindow();
        return QApplication::exec();
    }

 private:
    bool UnlockFlow() {
        if (!master_manager_->IsInitialized()) {
            ui::MasterPasswordDialog dlg(
                ui::MasterPasswordDialog::Mode::kSetup);
            if (dlg.exec() != QDialog::Accepted) return false;
            const QString pwd = dlg.NewPassword();
            auto payload = master_manager_->SetInitial(pwd.toStdString());
            if (!payload.has_value()) {
                QMessageBox::critical(nullptr, QStringLiteral("初始化失败"),
                                       QStringLiteral("无法保存主密码。"));
                return false;
            }
            session::SessionManager::Instance()->Unlock(
                std::move(payload->session_key),
                std::move(payload->master_password));
            return true;
        }

        const bool hello_wanted = QSettings()
                                       .value(kSettingsHelloEnabled, false)
                                       .toBool();
        if (hello_wanted && hello_->IsAvailable() && hello_->IsEnrolled()) {
            auto pwd_opt = hello_->Unlock();
            if (pwd_opt.has_value()) {
                auto payload = master_manager_->VerifyLocal(
                    pwd_opt->toStdString());
                if (payload.has_value()) {
                    session::SessionManager::Instance()->Unlock(
                        std::move(payload->session_key),
                        std::move(payload->master_password));
                    return true;
                }
            }
        }

        for (;;) {
            ui::MasterPasswordDialog dlg(
                ui::MasterPasswordDialog::Mode::kUnlock);
            dlg.SetHelloAvailable(hello_->IsAvailable() &&
                                   hello_->IsEnrolled());
            QObject::connect(&dlg, &ui::MasterPasswordDialog::HelloRequested,
                              [this, &dlg]() {
                                  auto pwd_opt = hello_->Unlock();
                                  if (!pwd_opt.has_value()) return;
                                  auto payload = master_manager_->VerifyLocal(
                                      pwd_opt->toStdString());
                                  if (!payload.has_value()) {
                                      dlg.SetErrorText(
                                          QStringLiteral(
                                              "已存储的 Hello 凭证与当前主密码不匹配。"));
                                      return;
                                  }
                                  session::SessionManager::Instance()->Unlock(
                                      std::move(payload->session_key),
                                      std::move(payload->master_password));
                                  dlg.accept();
                              });
            if (dlg.exec() != QDialog::Accepted) return false;
            if (session::SessionManager::Instance()->IsUnlocked()) return true;

            auto payload =
                master_manager_->VerifyLocal(dlg.NewPassword().toStdString());
            if (!payload.has_value()) {
                QMessageBox::warning(nullptr, QStringLiteral("主密码错误"),
                                      QStringLiteral("请重试。"));
                continue;
            }
            session::SessionManager::Instance()->Unlock(
                std::move(payload->session_key),
                std::move(payload->master_password));
            return true;
        }
    }

    void MaybeConfigureSyncProvider() {
        if (token_store_->Exists()) {
            sync_manager_->set_provider(drive_provider_.get());
        }
    }

    void ShowMainWindow() {
        ui::MainWindow::Deps deps;
        deps.password_dao = password_dao_.get();
        deps.category_dao = category_dao_.get();
        deps.session_key = session::SessionManager::Instance()->session_key();
        deps.sync_manager = sync_manager_.get();
        deps.sync_scheduler = sync_scheduler_.get();
        window_ = std::make_unique<ui::MainWindow>(deps);

        QObject::connect(window_.get(), &ui::MainWindow::LockRequested,
                          []() { session::SessionManager::Instance()->Lock(); });
        WirePreferencesPage();

        QObject::connect(qApp, &QApplication::applicationStateChanged,
                          [this](Qt::ApplicationState state) {
                              if (state == Qt::ApplicationActive) {
                                  auto_lock_->NotifyActivity();
                              }
                          });
        auto_lock_->Start();
        window_->show();
    }

    void WirePreferencesPage() {
        auto* page = window_->preferences_page();
        if (!page) return;
        page->SetHelloAvailable(hello_->IsAvailable());
        page->SetHelloEnabled(
            QSettings().value(kSettingsHelloEnabled, false).toBool());
        page->SetGoogleDriveConnected(token_store_->Exists(), QString());

        QObject::connect(page,
                          &ui::PreferencesPage::ChangeMasterPasswordRequested,
                          [this, page]() { ChangeMasterPassword(page); });
        QObject::connect(
            page, &ui::PreferencesPage::EnableHelloRequested, [this, page]() {
                const auto* pwd = session::SessionManager::Instance()
                                       ->master_password();
                if (!pwd) return;
                QByteArray view(reinterpret_cast<const char*>(pwd->data()),
                                pwd->size());
                if (!hello_->Enroll(QString::fromUtf8(view))) {
                    ui::Toast::Show(page,
                                    QStringLiteral("Windows Hello 注册失败"),
                                    ui::Toast::Level::kError);
                    page->SetHelloEnabled(false);
                    return;
                }
                QSettings().setValue(kSettingsHelloEnabled, true);
                ui::Toast::Show(page,
                                QStringLiteral("已启用 Windows Hello"),
                                ui::Toast::Level::kSuccess);
            });
        QObject::connect(page, &ui::PreferencesPage::DisableHelloRequested,
                          [this, page]() {
                              hello_->Disable();
                              QSettings().setValue(kSettingsHelloEnabled, false);
                              ui::Toast::Show(
                                  page,
                                  QStringLiteral("已关闭 Windows Hello"),
                                  ui::Toast::Level::kInfo);
                          });
        QObject::connect(page,
                          &ui::PreferencesPage::ConnectGoogleDriveRequested,
                          [this, page]() {
                              ui::OAuthWizardDialog wiz(oauth_client_.get(),
                                                        page);
                              if (wiz.exec() != QDialog::Accepted) return;
                              sync_manager_->set_provider(drive_provider_.get());
                              page->SetGoogleDriveConnected(true, QString());
                          });
        QObject::connect(page,
                          &ui::PreferencesPage::DisconnectGoogleDriveRequested,
                          [this, page]() {
                              oauth_client_->RevokeTokens();
                              sync_manager_->set_provider(nullptr);
                              page->SetGoogleDriveConnected(false, QString());
                          });
        QObject::connect(page, &ui::PreferencesPage::SyncNowRequested,
                          [this]() { sync_scheduler_->SyncImmediately(); });
        QObject::connect(page, &ui::PreferencesPage::ImportCsvRequested,
                          [this, page]() { ImportCsv(page); });
        QObject::connect(page, &ui::PreferencesPage::ExportCsvRequested,
                          [this, page]() { ExportCsv(page); });
        QObject::connect(page, &ui::PreferencesPage::AutoLockChanged,
                          [this](int minutes) {
                              auto_lock_->SetTimeoutMs(minutes * 60 * 1000);
                          });
        QObject::connect(page, &ui::PreferencesPage::ClipboardTimeoutChanged,
                          [](int seconds) {
                              ui::ClipboardManager::Instance()
                                  ->set_default_timeout_ms(seconds * 1000);
                          });
    }

    void ChangeMasterPassword(QWidget* parent) {
        ui::MasterPasswordDialog dlg(ui::MasterPasswordDialog::Mode::kChange,
                                     parent);
        if (dlg.exec() != QDialog::Accepted) return;
        auto payload = master_manager_->ChangePassword(
            dlg.OldPassword().toStdString(),
            dlg.NewPassword().toStdString());
        if (!payload.has_value()) {
            QMessageBox::warning(parent, QStringLiteral("修改失败"),
                                  QStringLiteral("当前主密码不正确。"));
            return;
        }
        // TODO(task-20b): re-encrypt all password rows with the new session key
        //                 inside a transaction, then push to cloud via
        //                 SyncManager::ChangeCloudMasterPassword.
        session::SessionManager::Instance()->Unlock(
            std::move(payload->session_key),
            std::move(payload->master_password));
        ui::Toast::Show(parent, QStringLiteral("主密码已更新"),
                        ui::Toast::Level::kSuccess);
    }

    void ImportCsv(QWidget* parent) {
        const QString path = QFileDialog::getOpenFileName(
            parent, QStringLiteral("选择 CSV 文件"), QString(),
            QStringLiteral("CSV (*.csv)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(parent, QStringLiteral("导入失败"),
                                  QStringLiteral("无法读取文件。"));
            return;
        }
        const QByteArray raw = f.readAll();
        const auto records = csv::CsvReader::ReadAll(raw);

        QList<csv::ExistingPasswordSnapshot> existing;
        for (const auto& e : password_dao_->ListActive()) {
            csv::ExistingPasswordSnapshot s;
            s.id = e.id;
            s.uuid = e.uuid;
            s.title = e.title;
            s.username = e.username;
            s.website = e.website;
            s.notes = e.notes;
            // password stays empty here — validator only diffs non-password fields.
            existing.append(std::move(s));
        }
        QStringList cat_names;
        for (const auto& c : category_dao_->ListActive()) cat_names << c.name;

        csv::CsvValidator validator;
        auto result = validator.Validate(records, existing, cat_names);

        ui::ImportPreviewDialog preview(std::move(result), parent);
        if (preview.exec() != QDialog::Accepted) return;

        const auto* sk = session::SessionManager::Instance()->session_key();
        if (!sk) return;
        auto summary = csv::CsvImporter::Apply(
            preview.validation(), *password_dao_, *category_dao_, *sk,
            QDateTime::currentMSecsSinceEpoch());
        if (!summary.has_value()) {
            ui::Toast::Show(parent, QStringLiteral("导入失败"),
                            ui::Toast::Level::kError);
            return;
        }
        ui::Toast::Show(parent,
                        QStringLiteral("已导入：新增 %1，更新 %2")
                            .arg(summary->inserted)
                            .arg(summary->updated),
                        ui::Toast::Level::kSuccess);
        sync_scheduler_->MarkDirty();
        if (window_) window_->Reload();
    }

    void ExportCsv(QWidget* parent) {
        const QString path = QFileDialog::getSaveFileName(
            parent, QStringLiteral("保存 CSV 到"), QString(),
            QStringLiteral("CSV (*.csv)"));
        if (path.isEmpty()) return;
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(parent, QStringLiteral("导出失败"),
                                  QStringLiteral("无法写入文件。"));
            return;
        }
        QList<csv::ExportEntry> entries;
        // TODO(task-20b): decrypt password and resolve category name per row.
        //                 minimal stub keeps CSV structure but leaves password
        //                 column empty — full impl lives with password re-encrypt
        //                 story alongside ChangeMasterPassword.
        if (!csv::CsvExporter::Export(&f, std::move(entries))) {
            QMessageBox::warning(parent, QStringLiteral("导出失败"),
                                  QStringLiteral("写入过程中出错。"));
            return;
        }
        f.commit();
        ui::Toast::Show(parent, QStringLiteral("已导出"),
                        ui::Toast::Level::kSuccess);
    }

    void OnLocked() {
        if (window_) window_->hide();
        UnlockFlow();
        if (window_) window_->show();
    }

    std::unique_ptr<storage::Database> database_;
    std::unique_ptr<storage::PasswordDao> password_dao_;
    std::unique_ptr<storage::CategoryDao> category_dao_;
    std::unique_ptr<master_password::MasterPasswordStore> master_store_;
    std::unique_ptr<master_password::MasterPasswordManager> master_manager_;
    std::unique_ptr<hello::WindowsHelloUnlock> hello_;
    std::unique_ptr<oauth::TokenStore> token_store_;
    std::unique_ptr<oauth::GoogleOAuthClient> oauth_client_;
    std::unique_ptr<sync::GoogleDriveProvider> drive_provider_;
    std::unique_ptr<sync::SyncManager> sync_manager_;
    std::unique_ptr<sync::SyncScheduler> sync_scheduler_;
    std::unique_ptr<session::AutoLockTimer> auto_lock_;
    std::unique_ptr<ui::MainWindow> window_;
};

Application::Application() : impl_(std::make_unique<Impl>()) {}
Application::~Application() = default;

int Application::Run() { return impl_->Run(); }

}  // namespace passvault::app
