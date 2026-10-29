#include "app/application.h"
#include "app/password_change_startup.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
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
#include "master_password/password_change_coordinator.h"
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

// Durable recovery and failure-boundary tests cover the enabled flow.
constexpr bool kMasterPasswordChangeReady = true;

}  // namespace

class Application::Impl {
 public:
    explicit Impl(LocalResetPaths paths, sync::CloudStorageProvider* provider_override,
                  std::function<bool(const QString&)> reset_io_check)
        : reset_(std::move(paths)), instance_lock_(reset_.paths().control_directory),
          application_context_(std::make_unique<QObject>()),
          hello_(reset_.paths().DataFile("hello_unlock.dat")),
          provider_override_(provider_override) {
        reset_.set_io_check(std::move(reset_io_check));
    }

    ~Impl() {
        application_context_.reset();
        runtime_context_.reset();
        if (auto_lock_) auto_lock_->Stop();
        session::SessionManager::Instance()->Lock();
    }

    int Run() {
        if (!instance_lock_.TryLock()) {
            QMessageBox::warning(nullptr, QStringLiteral("PassVault 已在运行"),
                QStringLiteral("同一 Windows 用户只能运行一个 PassVault 实例。请关闭其他安装副本后重试。"));
            return 1;
        }
        if (reset_.Inspect() != LocalReset::Intent::kAbsent) {
            resetting_ = true;
            if (!ContinueReset()) return 1;
            session::SessionManager::RecreateAfterLocalReset();
            ui::ThemeManager::Instance()->ApplyTheme(ui::Theme::kSystem);
            resetting_ = false;
        }
        auto* theme = ui::ThemeManager::Instance();
        static constexpr const char* kFontResources[] = {
            ":/fonts/NotoSansSC-VariableFont_wght.ttf",
            ":/fonts/DMMono-Regular.ttf",
            ":/fonts/DMMono-Medium.ttf",
        };
        for (const char* path : kFontResources) {
            if (QFontDatabase::addApplicationFont(
                    QString::fromLatin1(path)) < 0) {
                qWarning() << "failed to load font" << path;
            }
        }
        if (auto* q = qApp) {
            QFont app_font;
            app_font.setFamilies({QStringLiteral("Noto Sans SC"),
                                  QStringLiteral("Microsoft YaHei UI"),
                                  QStringLiteral("Segoe UI")});
            app_font.setPixelSize(13);
            q->setFont(app_font);
            q->setStyleSheet(theme->LoadStyleSheet());
        }

        if (!InitializeRuntime()) return initialization_failed_ ? 1 : 0;
        return QApplication::exec();
    }

 private:
    bool InitializeRuntime() {
        runtime_context_ = std::make_unique<QObject>();
        initialization_failed_ = true;
        if (!QDir().mkpath(QDir(reset_.paths().installation).filePath("data"))) return false;
        try {
            database_ = storage::Database::Open(reset_.paths().DataFile("passvault.db"));
        } catch (const std::exception&) {
            QMessageBox::critical(nullptr, QStringLiteral("启动失败"),
                                  QStringLiteral("无法打开数据库。"));
            return false;
        }
        if (!database_) {
            QMessageBox::critical(nullptr, QStringLiteral("启动失败"),
                                   QStringLiteral("无法打开数据库。"));
            return false;
        }
        try {
            storage::EnsureCurrentSchema(*database_);
        } catch (const std::exception& e) {
            QMessageBox::critical(nullptr, QStringLiteral("启动失败"),
                                   QString::fromUtf8(e.what()));
            return false;
        }

        password_dao_ = std::make_unique<storage::PasswordDao>(*database_);
        category_dao_ = std::make_unique<storage::CategoryDao>(*database_);
        master_store_ = std::make_unique<master_password::MasterPasswordStore>(reset_.paths().DataFile("master.dat"));
        master_manager_ =
            std::make_unique<master_password::MasterPasswordManager>(
                *master_store_);
        password_change_ =
            std::make_unique<master_password::PasswordChangeCoordinator>(
                *master_store_, *master_manager_, *password_dao_, *category_dao_,
                [this]() { return hello_.Invalidate(); });
        token_store_ = std::make_unique<oauth::TokenStore>(reset_.paths().token_file);
        oauth_client_ =
            std::make_unique<oauth::GoogleOAuthClient>(token_store_.get());
        oauth_client_->set_client_id(
            QString::fromLatin1(oauth::kGoogleDesktopClientId));
        oauth_client_->set_client_secret(
            QString::fromLatin1(oauth::kGoogleDesktopClientSecret));
        oauth_client_->set_scope(
            QStringLiteral("https://www.googleapis.com/auth/drive.file"));

        drive_provider_ = std::make_unique<sync::GoogleDriveProvider>(
            [this]() { return oauth_client_->EnsureAccessToken(); });
        sync_manager_ = std::make_unique<sync::SyncManager>(
            password_dao_.get(), category_dao_.get(),
            session::SessionManager::Instance());
        sync_scheduler_ =
            std::make_unique<sync::SyncScheduler>(sync_manager_.get());
        auto_lock_ = std::make_unique<session::AutoLockTimer>();
        QSettings settings;
        auto_lock_->SetTimeoutMs(
            qBound(1, settings.value("session/auto_lock_minutes", 5).toInt(), 120) *
            60 * 1000);
        ui::ClipboardManager::Instance()->set_default_timeout_ms(
            qBound(5, settings.value("ui/clipboard_seconds", 30).toInt(), 300) *
            1000);

        QObject::connect(session::SessionManager::Instance(),
                          &session::SessionManager::LockChanged,
                          runtime_context_.get(), [this](bool locked) {
                              if (locked) {
                                  OnLocked();
                              }
                          });
        QObject::connect(auto_lock_.get(), &session::AutoLockTimer::TimedOut,
                          runtime_context_.get(), []() {
                              session::SessionManager::Instance()->Lock();
                          });

        if (token_store_->Exists() &&
            !password_change_->SaveBinding(master_password::CloudBinding::kBound)) {
            QMessageBox::critical(nullptr, QStringLiteral("启动失败"),
                                  QStringLiteral("无法保存云端绑定历史。"));
            return false;
        }
        initialization_failed_ = false;
        if (!UnlockVault()) return false;
        MaybeConfigureSyncProvider();
        ShowMainWindow();
        return true;
    }

    bool UnlockFlow() {
        if (!master_manager_->IsInitialized()) {
            for (;;) {
                ui::MasterPasswordDialog dlg(
                    ui::MasterPasswordDialog::Mode::kSetup);
                if (dlg.exec() != QDialog::Accepted) return false;
                const QString pwd = dlg.NewPassword();
                auto payload = master_manager_->SetInitial(pwd.toStdString());
                if (!payload.has_value()) {
                    if (QMessageBox::warning(nullptr, QStringLiteral("初始化失败"),
                            QStringLiteral("无法保存主密码。请重试或退出。"),
                            QMessageBox::Retry | QMessageBox::Close,
                            QMessageBox::Close) == QMessageBox::Retry) continue;
                    return false;
                }
                while (!password_change_->SaveBinding(master_password::CloudBinding::kLocal)) {
                    if (QMessageBox::warning(nullptr, QStringLiteral("初始化失败"),
                            QStringLiteral("无法记录保险库状态。请重试或退出。"),
                            QMessageBox::Retry | QMessageBox::Close,
                            QMessageBox::Close) != QMessageBox::Retry) return false;
                }
                session::SessionManager::Instance()->Unlock(
                    std::move(payload->session_key),
                    std::move(payload->master_password));
                return true;
            }
        }

        for (;;) {
            ui::MasterPasswordDialog dlg(
                ui::MasterPasswordDialog::Mode::kUnlock);
            if (dlg.exec() != QDialog::Accepted) return false;
            if (session::SessionManager::Instance()->IsUnlocked()) return true;

            auto payload =
                master_manager_->VerifyLocal(dlg.NewPassword().toStdString());
            if (!payload.has_value()) {
                QMessageBox::warning(
                    nullptr, QStringLiteral("解锁失败"),
                    master_manager_->last_error() == master_password::VerifyError::kWrongPassword
                        ? QStringLiteral("主密码不正确，请重试。")
                        : QStringLiteral("读取主密码记录失败，请检查文件后重试。"));
                continue;
            }
            session::SessionManager::Instance()->Unlock(
                std::move(payload->session_key),
                std::move(payload->master_password));
            return true;
        }
    }

    void MaybeConfigureSyncProvider() {
        if (provider_override_) {
            sync_manager_->set_provider(provider_override_);
            return;
        }
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
                          runtime_context_.get(), []() { session::SessionManager::Instance()->Lock(); });
        QObject::connect(window_.get(), &ui::MainWindow::DeferredExitRequested,
                          runtime_context_.get(), [this]() {
                              exit_requested_ = true;
                              session::SessionManager::Instance()->Lock();
                          });
        WirePreferencesPage();

        QObject::connect(qApp, &QApplication::applicationStateChanged,
                          runtime_context_.get(), [this](Qt::ApplicationState state) {
                              if (state == Qt::ApplicationActive) {
                                  auto_lock_->NotifyActivity();
                              }
                          });
        auto_lock_->Start();
        window_->show();
    }

    void LookupGoogleAccount() {
        account_startup_pending_ = false;
        if (reset_confirming_ || resetting_ || migration_active_) return;
        account_connection_ready_ = false;
        window_->preferences_page()->SetSyncState(
            ui::PreferencesPage::SyncUiState::kConnecting, QString());
        oauth_client_->LookupAccount();
    }

    void WirePreferencesPage() {
        auto* page = window_->preferences_page();
        if (!page) return;
        account_connection_ready_ = provider_override_ != nullptr;
        account_startup_pending_ = false;
        auto_lock_->SetTimeoutMs(
            QSettings().value("session/auto_lock_minutes", 5).toInt() * 60000);
        ui::ClipboardManager::Instance()->set_default_timeout_ms(
            QSettings().value("ui/clipboard_seconds", 30).toInt() * 1000);
        page->SetChangeMasterPasswordEnabled(
            kMasterPasswordChangeReady,
            QStringLiteral("修改主密码暂不可用"));
        page->SetSyncState(ui::PreferencesPage::SyncUiState::kDisconnected,
                           QString());
        QObject::connect(oauth_client_.get(),
                         &oauth::GoogleOAuthClient::AccountLookupFinished,
                         runtime_context_.get(),
                         [this, page](oauth::GoogleOAuthClient::AccountStatus status,
                                      const QString& email) {
            using Status = oauth::GoogleOAuthClient::AccountStatus;
            using State = ui::PreferencesPage::SyncUiState;
            account_connection_ready_ = status == Status::kConnected;
            State state = State::kConnectionError;
            switch (status) {
                case Status::kConnected: state = State::kConnected; break;
                case Status::kNeedsAuthorization: state = State::kNeedsAuthorization; break;
                case Status::kMissingEmail: state = State::kMissingEmail; break;
                case Status::kNetworkError: break;
            }
            page->SetSyncState(state, email);
        });
        QObject::connect(page, &ui::PreferencesPage::RetryAccountRequested,
                         runtime_context_.get(), [this]() { LookupGoogleAccount(); });
        if (token_store_->Exists() && !provider_override_) {
            account_startup_pending_ = true;
            page->SetSyncState(ui::PreferencesPage::SyncUiState::kConnecting, QString());
            QTimer::singleShot(0, runtime_context_.get(), [this]() {
                if (account_startup_pending_ && token_store_->Exists()) {
                    LookupGoogleAccount();
                }
            });
        }

        QObject::connect(page, &ui::PreferencesPage::ClearLocalDataRequested,
                         runtime_context_.get(), [this]() { RequestLocalReset(); });
        QObject::connect(page,
                          &ui::PreferencesPage::ChangeMasterPasswordRequested,
                          runtime_context_.get(), [this, page]() { ChangeMasterPassword(page); });
        QObject::connect(page,
                          &ui::PreferencesPage::ConnectGoogleDriveRequested,
                          runtime_context_.get(), [this, page]() {
                              if (reset_confirming_ || resetting_ || migration_active_) return;
                              account_connection_ready_ = false;
                              account_startup_pending_ = false;
                              oauth_client_->CancelAccountLookup();
                              drive_provider_->SignOut();
                              page->SetSyncState(
                                  ui::PreferencesPage::SyncUiState::kAuthorizing,
                                  QString());
                              ui::OAuthWizardDialog wiz(oauth_client_.get(),
                                                        page);
                              if (wiz.exec() != QDialog::Accepted) {
                                  page->SetSyncState(
                                      ui::PreferencesPage::SyncUiState::
                                          kDisconnected,
                                      QString());
                                  return;
                              }
                              if (!password_change_->SaveBinding(
                                      master_password::CloudBinding::kBound)) {
                                  QMessageBox::warning(page, QStringLiteral("连接失败"),
                                                       QStringLiteral("无法保存云端绑定历史。"));
                                  return;
                              }
                              sync_manager_->set_provider(drive_provider_.get());
                              LookupGoogleAccount();
                          });
        QObject::connect(page,
                          &ui::PreferencesPage::AuthorizeCancelled,
                          runtime_context_.get(), [this, page]() {
                              account_connection_ready_ = false;
                              account_startup_pending_ = false;
                              oauth_client_->CancelAuthorization();
                              page->SetSyncState(
                                  ui::PreferencesPage::SyncUiState::kDisconnected,
                                  QString());
                          });
        QObject::connect(page,
                          &ui::PreferencesPage::DisconnectGoogleDriveRequested,
                          runtime_context_.get(), [this, page]() {
                              if (reset_confirming_ || resetting_ || migration_active_) return;
                              account_connection_ready_ = false;
                              account_startup_pending_ = false;
                              oauth_client_->CancelAccountLookup();
                              drive_provider_->SignOut();
                              oauth_client_->RevokeTokens();
                              sync_manager_->set_provider(nullptr);
                              page->SetSyncState(
                                  ui::PreferencesPage::SyncUiState::kDisconnected,
                                  QString());
                          });
        QObject::connect(page, &ui::PreferencesPage::SyncNowRequested,
                          runtime_context_.get(), [this]() { if (!reset_confirming_ && !resetting_) sync_scheduler_->SyncImmediately(); });
        if (sync_manager_) {
            QObject::connect(
                sync_manager_.get(), &sync::SyncManager::SyncStarted,
                runtime_context_.get(), [this, page]() {
                    if (!account_connection_ready_) return;
                    page->SetSyncState(
                        ui::PreferencesPage::SyncUiState::kSyncing, QString());
                });
            QObject::connect(
                sync_manager_.get(), &sync::SyncManager::SyncFinished,
                runtime_context_.get(), [this, page](sync::SyncManager::StatusKind kind,
                             const QString& message) {
                    if (!account_connection_ready_) return;
                    using SyncUiState = ui::PreferencesPage::SyncUiState;
                    using StatusKind = sync::SyncManager::StatusKind;
                    SyncUiState state = SyncUiState::kError;
                    if (kind == StatusKind::kSuccess) {
                        state = SyncUiState::kSuccess;
                    } else if (kind == StatusKind::kConflict) {
                        state = SyncUiState::kConflict;
                    }
                    page->SetSyncState(state, QString(), message);
                });
        }
        QObject::connect(page, &ui::PreferencesPage::ImportCsvRequested,
                          runtime_context_.get(), [this, page]() { ImportCsv(page); });
        QObject::connect(page, &ui::PreferencesPage::ExportCsvRequested,
                          runtime_context_.get(), [this, page]() { ExportCsv(page); });
        QObject::connect(page, &ui::PreferencesPage::AutoLockChanged,
                          runtime_context_.get(), [this](int minutes) {
                              auto_lock_->SetTimeoutMs(minutes * 60 * 1000);
                          });
        QObject::connect(page, &ui::PreferencesPage::ClipboardTimeoutChanged,
                          runtime_context_.get(), [](int seconds) {
                              ui::ClipboardManager::Instance()
                                  ->set_default_timeout_ms(seconds * 1000);
                          });
    }

    bool UnlockVault() {
        return UnlockAfterPasswordRecovery(
            *password_change_, drive_provider_.get(),
            [this](const QString& message) {
                QMessageBox prompt(QMessageBox::Warning, QStringLiteral("恢复主密码迁移"),
                                   message, QMessageBox::NoButton);
                auto* retry = prompt.addButton(QStringLiteral("重试"), QMessageBox::AcceptRole);
                auto* login = prompt.addButton(QStringLiteral("重新登录云端"), QMessageBox::ActionRole);
                prompt.addButton(QStringLiteral("退出"), QMessageBox::RejectRole);
                prompt.exec();
                if (prompt.clickedButton() == retry) return true;
                if (prompt.clickedButton() == login) {
                    ui::OAuthWizardDialog wizard(oauth_client_.get());
                    wizard.exec();
                    return true;
                }
                return false;
            },
            [this]() { return UnlockFlow(); });
    }

    bool ConfirmBinding(QWidget* parent) {
        using master_password::CloudBinding;
        auto binding = password_change_->ReadBinding();
        if (binding == CloudBinding::kError) {
            QMessageBox::warning(parent, QStringLiteral("修改失败"),
                                 QStringLiteral("读取云端绑定历史失败。"));
            return false;
        }
        if (binding == CloudBinding::kUnknown) {
            QMessageBox prompt(QMessageBox::Question, QStringLiteral("确认保险库历史"),
                QStringLiteral("此旧保险库没有云端绑定记录。请选择它的使用历史；"
                               "曾绑定云端的保险库必须联网核对后才能改密。"),
                QMessageBox::NoButton, parent);
            auto* local = prompt.addButton(QStringLiteral("一直仅在本地使用"), QMessageBox::AcceptRole);
            auto* bound = prompt.addButton(QStringLiteral("曾绑定云端"), QMessageBox::ActionRole);
            prompt.addButton(QMessageBox::Cancel);
            prompt.exec();
            if (prompt.clickedButton() == local) binding = CloudBinding::kLocal;
            else if (prompt.clickedButton() == bound) binding = CloudBinding::kBound;
            else return false;
            if (!password_change_->SaveBinding(binding)) return false;
        }
        if (binding == CloudBinding::kBound && !token_store_->Exists()) {
            QMessageBox::information(parent, QStringLiteral("需要重新登录"),
                QStringLiteral("此保险库曾绑定云端，请先在设置中重新登录，再重试修改主密码。"));
            return false;
        }
        return true;
    }

    void ChangeMasterPassword(QWidget* parent) {
        if (!kMasterPasswordChangeReady || reset_confirming_ || resetting_ || migration_active_ || sync_manager_->is_busy()) return;
        auto* session = session::SessionManager::Instance();
        if (!session->BeginPasswordChange()) return;
        migration_active_ = true;
        exit_requested_ = false;
        sync_scheduler_->SetPaused(true);
        window_->SetMigrationActive(true);
        master_password::PasswordChangeCoordinator::Result result;
        {
            ui::MasterPasswordDialog dlg(ui::MasterPasswordDialog::Mode::kChange, window_.get());
            if (dlg.exec() == QDialog::Accepted && ConfirmBinding(window_.get()) &&
                !session->lock_requested() && !exit_requested_) {
                result = password_change_->Change(dlg.OldPassword(), dlg.NewPassword(),
                                                  dlg.ConfirmedPassword(), drive_provider_.get());
            }
        }
        using Outcome = master_password::PasswordChangeCoordinator::Outcome;
        const bool pending = result.outcome == Outcome::kLocked;
        const bool must_lock = pending || exit_requested_ || session->lock_requested();
        if (result.outcome == Outcome::kCompleted && !must_lock && result.payload) {
            session->Unlock(std::move(result.payload->session_key),
                            std::move(result.payload->master_password));
        }
        session->EndPasswordChange(must_lock);
        migration_active_ = false;
        if (exit_requested_) {
            QApplication::quit();
            return;
        }
        if (!session->IsUnlocked()) {
            OnLocked();
            return;
        }
        window_->SetMigrationActive(false);
        window_->SetSessionKey(session->session_key());
        window_->Reload();
        sync_scheduler_->SetPaused(false);
        if (!result.message.isEmpty()) {
            if (result.outcome == Outcome::kCompleted) {
                ui::Toast::Show(parent, result.message, ui::Toast::Level::kSuccess);
            } else {
                QMessageBox::warning(parent, QStringLiteral("主密码未改变"), result.message);
            }
        }
    }

    void ImportCsv(QWidget* parent) {
        if (reset_confirming_ || resetting_ || migration_active_) return;
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
        if (preview.exec() != QDialog::Accepted || reset_confirming_ || resetting_ || migration_active_) return;

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
        if (reset_confirming_ || resetting_ || migration_active_) return;
        const QString path = QFileDialog::getSaveFileName(
            parent, QStringLiteral("保存 CSV 到"), QString(),
            QStringLiteral("CSV (*.csv)"));
        if (path.isEmpty()) return;

        const auto* session_key =
            session::SessionManager::Instance()->session_key();
        if (!session_key) {
            QMessageBox::warning(parent, QStringLiteral("导出失败"),
                                  QStringLiteral("保险库当前未解锁。"));
            return;
        }
        auto entries = csv::CsvExporter::CollectEntries(
            *password_dao_, *category_dao_, *session_key);
        if (!entries.has_value()) {
            QMessageBox::warning(
                parent, QStringLiteral("导出失败"),
                QStringLiteral("存在无法解密的密码，未生成导出文件。"));
            return;
        }

        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(parent, QStringLiteral("导出失败"),
                                  QStringLiteral("无法写入文件。"));
            return;
        }
        const int exported_count = entries->size();
        if (!csv::CsvExporter::Export(&f, std::move(*entries))) {
            QMessageBox::warning(parent, QStringLiteral("导出失败"),
                                  QStringLiteral("写入过程中出错。"));
            return;
        }
        if (!f.commit()) {
            QMessageBox::warning(parent, QStringLiteral("导出失败"),
                                  QStringLiteral("无法提交导出文件。"));
            return;
        }
        ui::Toast::Show(parent,
                        QStringLiteral("已导出 %1 条密码").arg(exported_count),
                        ui::Toast::Level::kSuccess);
    }

    void OnLocked() {
        if (auto_lock_) auto_lock_->Stop();
        ui::ClipboardManager::Instance()->ClearSensitive();
        if (window_) {
            window_->SetSessionKey(nullptr);
            window_->hide();
        }
        if (reset_confirming_ || resetting_ || migration_active_) return;
        if (!UnlockVault()) {
            QApplication::quit();
            return;
        }
        if (window_) {
            window_->SetMigrationActive(false);
            window_->SetSessionKey(
                session::SessionManager::Instance()->session_key());
            window_->Reload();
            if (window_->isMinimized()) {
                window_->showNormal();
            } else {
                window_->show();
            }
        }
        sync_scheduler_->SetPaused(false);
        auto_lock_->Start();
    }

    LocalReset::Eligibility ResetEligibility() const {
        auto* session = session::SessionManager::Instance();
        return {session->IsUnlocked(), migration_active_ || session->password_change_active(),
                sync_manager_ && sync_manager_->is_busy(),
                (oauth_client_ && oauth_client_->is_busy()) ||
                    (drive_provider_ && drive_provider_->is_busy())};
    }

    void RequestLocalReset() {
        if (reset_confirming_ || resetting_ || QApplication::activeModalWidget()) return;
        if (!reset_.Eligible(ResetEligibility())) {
            QMessageBox::warning(window_.get(), QStringLiteral("暂时无法清理"),
                QStringLiteral("请先完成同步、登录或主密码迁移，保持保险库解锁后重试。"));
            return;
        }
        reset_confirming_ = true;
        exit_requested_ = false;
        sync_scheduler_->SetPaused(true);
        oauth_client_->SetPaused(true);
        window_->SetResetConfirmationActive(true);
        bool started = false;
        {
            bool accepted = false;
            QString password = QInputDialog::getText(window_.get(),
                QStringLiteral("验证当前主密码"), QStringLiteral("请输入当前主密码以清除本地数据"),
                QLineEdit::Password, QString(), &accepted);
            if (accepted && !exit_requested_ && session::SessionManager::Instance()->IsUnlocked()) {
                const auto answer = QMessageBox::warning(window_.get(),
                    QStringLiteral("最终确认清除本地数据"),
                    QStringLiteral("将清除当前安装的本地保险库、此 Windows 用户共享的登录凭据和本应用设置。"
                        "未同步的新增、修改、删除及草稿会永久丢失；没有云备份将无法恢复。"
                        "云端备份保持不变，本次不会自动同步。恢复需要原账号及对应云备份密码。"
                        "当前首次设置要求至少 12 位，旧短密码备份存在恢复限制；本次不提供云恢复入口。"
                        "完成后将在当前进程返回首次设置。确定继续？"),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
                if (answer == QMessageBox::Yes && !exit_requested_) {
                    started = reset_.Begin(ResetEligibility(), true, [this, &password]() {
                        auto bytes = password.toUtf8();
                        auto verified = master_manager_->VerifyLocal(
                            std::string_view(bytes.constData(), bytes.size()));
                        bytes.fill('\0');
                        return verified.has_value() && reset_.Eligible(ResetEligibility());
                    });
                    if (!started) QMessageBox::warning(window_.get(), QStringLiteral("未清除数据"),
                        QStringLiteral("当前主密码验证或本地记录检查失败，数据保持原状。"));
                }
            }
            password.fill(QChar(0));
            password.clear();
        }
        reset_confirming_ = false;
        if (!started) {
            oauth_client_->SetPaused(false);
            window_->SetResetConfirmationActive(false);
            if (exit_requested_) { QApplication::quit(); return; }
            if (!session::SessionManager::Instance()->IsUnlocked()) { OnLocked(); return; }
            sync_scheduler_->SetPaused(false);
            return;
        }
        resetting_ = true;
        QTimer::singleShot(0, application_context_.get(), [this]() {
            const bool quit_on_close = QApplication::quitOnLastWindowClosed();
            QApplication::setQuitOnLastWindowClosed(false);
            const bool completed = ContinueReset();
            if (completed) {
                session::SessionManager::RecreateAfterLocalReset();
                ui::ThemeManager::Instance()->ApplyTheme(ui::Theme::kSystem);
                resetting_ = false;
            }
            const bool initialized = completed && InitializeRuntime();
            QApplication::setQuitOnLastWindowClosed(quit_on_close);
            if (!initialized) QApplication::exit(completed && !initialization_failed_ ? 0 : 1);
        });
    }

    bool ReleaseRuntime() {
        if ((sync_manager_ && sync_manager_->is_busy()) ||
            (oauth_client_ && oauth_client_->is_busy()) ||
            (drive_provider_ && drive_provider_->is_busy())) return false;
        runtime_context_.reset();
        if (auto_lock_) auto_lock_->Stop();
        if (sync_scheduler_) sync_scheduler_->SetPaused(true);
        if (oauth_client_) oauth_client_->SetPaused(true);
        if (window_) window_->SetSessionKey(nullptr);
        ui::ClipboardManager::Instance()->ClearSensitive();
        session::SessionManager::Instance()->Lock();
        window_.reset();
        auto_lock_.reset();
        sync_scheduler_.reset();
        sync_manager_.reset();
        drive_provider_.reset();
        if (oauth_client_ && !oauth_client_->ForgetCredentials()) return false;
        oauth_client_.reset();
        token_store_.reset();
        password_change_.reset();
        master_manager_.reset();
        master_store_.reset();
        category_dao_.reset();
        password_dao_.reset();
        if (database_ && !database_->Close()) return false;
        database_.reset();
        return true;
    }

    bool ContinueReset() {
        for (;;) {
            const auto intent = reset_.Inspect();
            if (intent == LocalReset::Intent::kOtherInstallation ||
                intent == LocalReset::Intent::kInvalid) {
                QMessageBox::critical(nullptr, QStringLiteral("清理尚未完成"),
                    intent == LocalReset::Intent::kOtherInstallation
                        ? QStringLiteral("另一个安装副本的清理尚未完成。请回到原安装副本完成清理。")
                        : QStringLiteral("本地清理标记损坏或无法读取。为保护数据，已阻止启动。"));
                return false;
            }
            QSettings settings;
            if (reset_.Resume(settings, [this]() { return ReleaseRuntime(); })) return true;
            const auto answer = QMessageBox::warning(nullptr, QStringLiteral("清理尚未完成"),
                QStringLiteral("部分本地数据可能已清除。请重试以继续完成，或退出后从本安装副本继续；无法返回原保险库。"),
                QMessageBox::Retry | QMessageBox::Close, QMessageBox::Close);
            if (answer != QMessageBox::Retry) return false;
        }
    }

    LocalReset reset_;
    UserInstanceLock instance_lock_;
    std::unique_ptr<QObject> application_context_;
    std::unique_ptr<QObject> runtime_context_;
    bool initialization_failed_ = false;
    bool account_connection_ready_ = false;
    bool account_startup_pending_ = false;
    bool reset_confirming_ = false;
    bool resetting_ = false;
    bool migration_active_ = false;
    bool exit_requested_ = false;
    hello::WindowsHelloUnlock hello_;
    sync::CloudStorageProvider* provider_override_ = nullptr;
    std::unique_ptr<master_password::PasswordChangeCoordinator> password_change_;
    std::unique_ptr<storage::Database> database_;
    std::unique_ptr<storage::PasswordDao> password_dao_;
    std::unique_ptr<storage::CategoryDao> category_dao_;
    std::unique_ptr<master_password::MasterPasswordStore> master_store_;
    std::unique_ptr<master_password::MasterPasswordManager> master_manager_;
    std::unique_ptr<oauth::TokenStore> token_store_;
    std::unique_ptr<oauth::GoogleOAuthClient> oauth_client_;
    std::unique_ptr<sync::GoogleDriveProvider> drive_provider_;
    std::unique_ptr<sync::SyncManager> sync_manager_;
    std::unique_ptr<sync::SyncScheduler> sync_scheduler_;
    std::unique_ptr<session::AutoLockTimer> auto_lock_;
    std::unique_ptr<ui::MainWindow> window_;
};

Application::Application() : Application(LocalResetPaths::Default()) {}
Application::Application(LocalResetPaths paths,
                         sync::CloudStorageProvider* provider_override,
                         std::function<bool(const QString&)> reset_io_check)
    : impl_(std::make_unique<Impl>(std::move(paths), provider_override,
                                   std::move(reset_io_check))) {}
Application::~Application() = default;

int Application::Run() { return impl_->Run(); }

}  // namespace passvault::app
