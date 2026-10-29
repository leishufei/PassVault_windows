#include "app/local_reset.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>
#include <cstring>
#include <functional>

#include "app/application.h"
#include "crypto/crypto_service.h"
#include "crypto/random.h"
#include "master_password/master_password_manager.h"
#include "master_password/password_change_coordinator.h"
#include "model/cloud_backup_format.h"
#include "model/sync_payload_v2.h"
#include "oauth/google_oauth_client.h"
#include "oauth/loopback_http_server.h"
#include "oauth/token_store.h"
#include "session/session_manager.h"
#include "storage/category_dao.h"
#include "storage/database.h"
#include "storage/file_status.h"
#include "storage/password_dao.h"
#include "storage/schema.h"
#include "sync/cloud_crypto_service.h"
#include "sync/cloud_storage_provider.h"
#include "ui/clipboard_manager.h"
#include "ui/editor_panel.h"
#include "ui/main_window.h"
#include "ui/master_password_dialog.h"
#include "ui/preferences_page.h"
#include "ui/theme_manager.h"

namespace {

using passvault::app::LocalReset;
using passvault::app::LocalResetPaths;
using passvault::app::UserInstanceLock;
using passvault::master_password::MasterPasswordManager;
using passvault::master_password::MasterPasswordStore;
using passvault::master_password::PasswordChangeCoordinator;
using passvault::storage::Database;

void Write(const QString& path, const QByteArray& bytes = "isolated-demo") {
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(bytes), bytes.size());
}

QByteArray Read(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return file.readAll();
}

class LocalResetTest : public testing::Test {
 protected:
  LocalResetTest()
      : paths_{dir_.path() + "/install", dir_.path() + "/control",
               dir_.path() + "/shared/google_token.dat"},
        reset_(paths_),
        settings_(dir_.path() + "/settings.ini", QSettings::IniFormat),
        store_(paths_.DataFile("master.dat")),
        manager_(store_) {}
  void SetUp() override {
    ASSERT_TRUE(dir_.isValid());
    ASSERT_TRUE(QDir().mkpath(paths_.installation + "/data"));
    auto initial = manager_.SetInitial("isolated-master-123");
    ASSERT_TRUE(initial);
    auto db = Database::Open(paths_.DataFile("passvault.db"));
    passvault::storage::EnsureCurrentSchema(*db);
    passvault::storage::PasswordDao passwords(*db);
    passvault::model::PasswordEntry entry;
    entry.uuid = "demo-active";
    entry.title = "demo";
    entry.password_iv.resize(12);
    passvault::crypto::Random::Fill(entry.password_iv.data(), 12);
    entry.encrypted_password = passvault::crypto::CryptoService::EncryptGcm(
        initial->session_key.data(), initial->session_key.size(),
        reinterpret_cast<const std::uint8_t*>(entry.password_iv.constData()),
        12, reinterpret_cast<const std::uint8_t*>("demo"), 4);
    ASSERT_TRUE(passwords.Insert(entry));
    entry.uuid = "demo-trash";
    entry.is_deleted = true;
    ASSERT_TRUE(passwords.Insert(entry));
    ASSERT_TRUE(passwords.ApplyDeletionMarker("demo-marker", 123));
    ASSERT_TRUE(db->Close());
    for (const auto* name : {"hello_unlock.dat", "master.dat.binding"}) {
      Write(paths_.DataFile(name));
    }
    Write(paths_.token_file);
    Write(paths_.DataFile("keep.csv"));
    Write(paths_.DataFile("unknown.dat"));
    Write(dir_.path() + "/other-install/data/passvault.db");
    settings_.setValue("ui/default_show_passwords", true);
    settings_.setValue("hello/enabled", true);
    settings_.sync();
    master_bytes_ = Read(store_.file_path());
    db_bytes_ = Read(paths_.DataFile("passvault.db"));
  }
  bool Begin() {
    return reset_.Begin({true, false, false, false}, true, [this]() {
      return manager_.VerifyLocal("isolated-master-123").has_value();
    });
  }
  void ExpectUntouched() {
    EXPECT_EQ(Read(store_.file_path()), master_bytes_);
    EXPECT_EQ(Read(paths_.DataFile("passvault.db")), db_bytes_);
    EXPECT_TRUE(QFile::exists(paths_.token_file));
    EXPECT_TRUE(settings_.value("ui/default_show_passwords").toBool());
  }
  void ExpectClean() {
    for (const auto& file : reset_.Files()) EXPECT_FALSE(QFile::exists(file));
    EXPECT_TRUE(settings_.allKeys().isEmpty());
    EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kAbsent);
    EXPECT_TRUE(QFile::exists(paths_.DataFile("keep.csv")));
    EXPECT_TRUE(QFile::exists(paths_.DataFile("unknown.dat")));
    EXPECT_TRUE(
        QFile::exists(dir_.path() + "/other-install/data/passvault.db"));
  }
  QTemporaryDir dir_;
  LocalResetPaths paths_;
  LocalReset reset_;
  QSettings settings_;
  MasterPasswordStore store_;
  MasterPasswordManager manager_;
  QByteArray master_bytes_;
  QByteArray db_bytes_;
};

TEST_F(LocalResetTest, CancelWrongPasswordAndVerifyIoNeverDelete) {
  EXPECT_FALSE(reset_.Begin({true}, false, [] {
    ADD_FAILURE();
    return true;
  }));
  EXPECT_FALSE(reset_.Begin({true}, true, [this] {
    return manager_.VerifyLocal("wrong-isolated-password").has_value();
  }));
  MasterPasswordStore unreadable(paths_.installation);
  MasterPasswordManager io_manager(unreadable);
  EXPECT_FALSE(reset_.Begin({true}, true, [&] {
    return io_manager.VerifyLocal("isolated-master-123").has_value();
  }));
  EXPECT_EQ(io_manager.last_error(),
            passvault::master_password::VerifyError::kIoError);
  ExpectUntouched();
}

TEST_F(LocalResetTest, IntentWriteFailureLeavesDataAndSettings) {
  reset_.set_io_check(
      [](const QString& operation) { return operation != "intent"; });
  EXPECT_FALSE(Begin());
  EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kAbsent);
  ExpectUntouched();
}

TEST_F(LocalResetTest, LockedMigrationSyncAndOauthBusyAreRejected) {
  for (const auto state : {LocalReset::Eligibility{},
                           {true, true, false, false},
                           {true, false, true, false},
                           {true, false, false, true}}) {
    EXPECT_FALSE(reset_.Begin(state, true, [] {
      ADD_FAILURE();
      return true;
    }));
  }
  ExpectUntouched();
}

TEST_F(LocalResetTest, IdleJournalIsParsedAndPendingCorruptUnreadableReject) {
  const auto path = paths_.DataFile("master.dat.migration");
  Write(path, "bad");
  EXPECT_FALSE(reset_.Eligible({true}));
  EXPECT_EQ(PasswordChangeCoordinator::InspectRecovery(path),
            PasswordChangeCoordinator::RecoveryStatus::kError);
  ASSERT_TRUE(QFile::remove(path));
  auto db = Database::Open(paths_.DataFile("passvault.db"));
  passvault::storage::PasswordDao passwords(*db);
  passvault::storage::CategoryDao categories(*db);
  PasswordChangeCoordinator change(store_, manager_, passwords, categories,
                                   [] { return true; });
  ASSERT_TRUE(
      change.SaveBinding(passvault::master_password::CloudBinding::kLocal));
  const auto result =
      change.Change("isolated-master-123", "different-master-123",
                    "different-master-123", nullptr);
  ASSERT_EQ(result.outcome, PasswordChangeCoordinator::Outcome::kCompleted);
  EXPECT_TRUE(reset_.Eligible({true}));
  ASSERT_TRUE(reset_.Begin({true}, true, [this] {
    return manager_.VerifyLocal("different-master-123").has_value();
  }));
  EXPECT_TRUE(reset_.Resume(settings_, [&] { return db->Close(); }));
  ExpectClean();
}

TEST_F(LocalResetTest, RealDatabaseBusyCloseRetainsHandleAndRetrySucceeds) {
  auto db = Database::Open(paths_.DataFile("passvault.db"));
  auto* original = db->handle();
  sqlite3_stmt* statement = nullptr;
  ASSERT_EQ(sqlite3_prepare_v2(original, "SELECT * FROM password_entries", -1,
                               &statement, nullptr),
            SQLITE_OK);
  ASSERT_TRUE(Begin());
  EXPECT_FALSE(reset_.Resume(settings_, [&] { return db->Close(); }));
  EXPECT_EQ(db->handle(), original);
  EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kOwned);
  ExpectUntouched();
  ASSERT_EQ(sqlite3_finalize(statement), SQLITE_OK);
  ASSERT_TRUE(reset_.Resume(settings_, [&] { return db->Close(); }));
  EXPECT_EQ(db->handle(), nullptr);
  ExpectClean();
}

class LocalResetFailureTest : public LocalResetTest,
                              public testing::WithParamInterface<int> {};

TEST_P(LocalResetFailureTest, EveryDurableFailureResumesAfterObjectRecreation) {
  ASSERT_TRUE(Begin());
  const auto files = reset_.Files();
  const int index = GetParam();
  const QString failed = index < files.size()    ? files[index]
                         : index == files.size() ? "settings"
                                                 : "marker";
  reset_.set_io_check(
      [&](const QString& operation) { return operation != failed; });
  EXPECT_FALSE(reset_.Resume(settings_, [] { return true; }));
  EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kOwned);
  QSettings reopened(settings_.fileName(), QSettings::IniFormat);
  LocalReset restarted(paths_);
  EXPECT_TRUE(restarted.Resume(reopened, [] { return true; }));
  settings_.sync();
  ExpectClean();
}
INSTANTIATE_TEST_SUITE_P(AllFilesSettingsMarker, LocalResetFailureTest,
                         testing::Range(0, 11));

TEST_F(LocalResetTest, RealOsFileDeletionFailureNeverReportsMissing) {
  ASSERT_TRUE(Begin());
  const auto path = paths_.DataFile("hello_unlock.dat");
#ifdef _WIN32
  HANDLE held =
      CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                  FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  ASSERT_NE(held, INVALID_HANDLE_VALUE);
  EXPECT_FALSE(reset_.Resume(settings_, [] { return true; }));
  EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kOwned);
  CloseHandle(held);
  EXPECT_TRUE(reset_.Resume(settings_, [] { return true; }));
  ExpectClean();
#endif
}

TEST_F(LocalResetTest,
       OtherInstallationAndBadMarkersBlockWithoutTokenMutation) {
  ASSERT_TRUE(Begin());
  const auto other = dir_.path() + "/other-install";
  LocalReset foreign({other, paths_.control_directory, paths_.token_file});
  EXPECT_EQ(foreign.Inspect(), LocalReset::Intent::kOtherInstallation);
  passvault::oauth::TokenStore new_tokens(paths_.token_file);
  ASSERT_TRUE(new_tokens.SaveRefreshToken("new-isolated-demo"));
  const auto new_token_bytes = Read(paths_.token_file);
  EXPECT_FALSE(foreign.Resume(settings_, [] {
    ADD_FAILURE();
    return true;
  }));
  EXPECT_TRUE(Read(paths_.token_file) == new_token_bytes);
  const auto original_marker =
      QJsonDocument::fromJson(Read(reset_.marker_path())).object();
  auto unknown_version = original_marker;
  unknown_version.insert("version", 2);
  auto arbitrary_path = original_marker;
  arbitrary_path.insert("deletePath", other);
  for (const auto& bytes :
       {QByteArray("bad"), QJsonDocument(unknown_version).toJson(),
        QJsonDocument(arbitrary_path).toJson()}) {
    Write(reset_.marker_path(), bytes);
    EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kInvalid);
    EXPECT_FALSE(reset_.Resume(settings_, [] {
      ADD_FAILURE();
      return true;
    }));
  }
  ExpectUntouched();
}

TEST_F(LocalResetTest, UnreadableResetMarkerIsNeverTreatedAsAbsent) {
#ifdef _WIN32
  ASSERT_TRUE(Begin());
  const auto path = reset_.marker_path();
  HANDLE held =
      CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0,
                  nullptr, OPEN_EXISTING, 0, nullptr);
  ASSERT_NE(held, INVALID_HANDLE_VALUE);
  EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kInvalid);
  EXPECT_FALSE(reset_.Resume(settings_, [] {
    ADD_FAILURE();
    return true;
  }));
  CloseHandle(held);
  EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kOwned);
  ExpectUntouched();
#endif
}

TEST_F(LocalResetTest, InstanceLockIsHeldAcrossResetAndReleasedNormally) {
  {
    UserInstanceLock first(paths_.control_directory);
    ASSERT_TRUE(first.TryLock());
    UserInstanceLock second(paths_.control_directory);
    EXPECT_FALSE(second.TryLock());
    ASSERT_TRUE(Begin());
    ASSERT_TRUE(reset_.Resume(settings_, [] { return true; }));
    UserInstanceLock after_reset(paths_.control_directory);
    EXPECT_FALSE(after_reset.TryLock());
  }
  UserInstanceLock next(paths_.control_directory);
  EXPECT_TRUE(next.TryLock());
}

TEST(LocalResetProcess, ChildLockProbe) {
  const auto path = qEnvironmentVariable("PASSVAULT_TEST_LOCK_DIRECTORY");
  if (path.isEmpty()) GTEST_SKIP();
  UserInstanceLock lock(path);
  const bool expected =
      qEnvironmentVariableIntValue("PASSVAULT_TEST_LOCK_EXPECTED") != 0;
  EXPECT_EQ(lock.TryLock(), expected);
}

TEST_F(LocalResetTest, TwoProcessLockUsesOnlyExplicitTemporaryDirectory) {
  const auto probe = [&](bool expected) {
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("PASSVAULT_TEST_LOCK_DIRECTORY",
                       paths_.control_directory);
    environment.insert("PASSVAULT_TEST_LOCK_EXPECTED", expected ? "1" : "0");
    process.setProcessEnvironment(environment);
    process.start(QCoreApplication::applicationFilePath(),
                  {"--gtest_filter=LocalResetProcess.ChildLockProbe"});
    EXPECT_TRUE(process.waitForFinished(15000));
    EXPECT_EQ(process.exitCode(), 0)
        << process.readAllStandardOutput().constData();
  };
  {
    UserInstanceLock first(paths_.control_directory);
    ASSERT_TRUE(first.TryLock());
    probe(false);
    ASSERT_TRUE(Begin());
    ASSERT_TRUE(reset_.Resume(settings_, [] { return true; }));
    probe(false);
  }
  probe(true);
}

class LateReply : public QNetworkReply {
 public:
  explicit LateReply(QObject* parent) : QNetworkReply(parent) {
    open(QIODevice::ReadOnly);
  }
  void Complete(bool timeout = false) {
    if (timeout) setError(TimeoutError, "injected timeout");
    setFinished(true);
    emit finished();
  }
  void abort() override {}

 protected:
  qint64 readData(char* data, qint64 maximum) override {
    const auto count = qMin(maximum, qint64(body_.size() - offset_));
    if (count <= 0) return -1;
    std::memcpy(data, body_.constData() + offset_, count);
    offset_ += count;
    return count;
  }

 private:
  QByteArray body_ =
      R"({"access_token":"demo-access","refresh_token":"demo-refresh","expires_in":3600})";
  qint64 offset_ = 0;
};

class LocalNetwork : public QNetworkAccessManager {
 public:
  int requests = 0;
  LateReply* last = nullptr;

 protected:
  QNetworkReply* createRequest(Operation, const QNetworkRequest&,
                               QIODevice*) override {
    ++requests;
    last = new LateReply(this);
    return last;
  }
};

TEST_F(LocalResetTest,
       OAuthLateRefreshTimeoutAndRevokeCannotResurrectCredentials) {
  for (int mode = 0; mode != 3; ++mode) {
    LocalNetwork network;
    passvault::oauth::TokenStore tokens(paths_.token_file);
    ASSERT_TRUE(tokens.SaveRefreshToken("demo-refresh"));
    passvault::oauth::GoogleOAuthClient oauth(&tokens);
    oauth.set_network_manager(&network);
    if (mode == 2)
      oauth.RevokeTokens();
    else
      oauth.RefreshAccessToken();
    ASSERT_TRUE(oauth.is_busy());
    ASSERT_EQ(network.requests, 1);
    oauth.SetPaused(true);
    ASSERT_TRUE(oauth.ForgetCredentials());
    ASSERT_FALSE(tokens.Exists());
    network.last->Complete(mode == 1);
    EXPECT_FALSE(oauth.HasAccessToken());
    EXPECT_FALSE(tokens.Exists());
    EXPECT_FALSE(oauth.is_busy());
    oauth.Authorize();
    oauth.RefreshAccessToken();
    oauth.RevokeTokens();
    EXPECT_EQ(network.requests, 1);
  }
}

TEST_F(LocalResetTest, PureLocalForgetChecksDeletionAndNeverRequestsNetwork) {
  LocalNetwork network;
  passvault::oauth::TokenStore tokens(paths_.token_file);
  passvault::oauth::GoogleOAuthClient oauth(&tokens);
  oauth.set_network_manager(&network);
  oauth.SetPaused(true);
  ASSERT_TRUE(Begin());
  EXPECT_TRUE(
      reset_.Resume(settings_, [&] { return oauth.ForgetCredentials(); }));
  EXPECT_EQ(network.requests, 0);
  ExpectClean();
}

class AuthorizationUrlCapture : public QObject {
  Q_OBJECT
 public:
  QUrl url;
 public slots:
  void Capture(const QUrl& value) { url = value; }
};

TEST_F(LocalResetTest,
       LateCodeExchangeAndQueuedLoopbackCannotWriteAfterForget) {
  AuthorizationUrlCapture capture;
  QDesktopServices::setUrlHandler("https", &capture, "Capture");
  LocalNetwork network;
  passvault::oauth::TokenStore tokens(paths_.token_file);
  passvault::oauth::GoogleOAuthClient oauth(&tokens);
  oauth.set_client_id("isolated-client");
  oauth.set_client_secret("isolated-client-secret");
  oauth.set_network_manager(&network);
  oauth.Authorize();
  QDesktopServices::unsetUrlHandler("https");
  ASSERT_TRUE(capture.url.isValid());
  const auto state = QUrlQuery(capture.url).queryItemValue("state");
  ASSERT_FALSE(state.isEmpty());
  auto* server = oauth.findChild<passvault::oauth::LoopbackHttpServer*>();
  ASSERT_NE(server, nullptr);
  server->CallbackReceived({{"state", state}, {"code", "isolated-code"}});
  ASSERT_EQ(network.requests, 1);
  ASSERT_TRUE(oauth.is_busy());
  oauth.SetPaused(true);
  ASSERT_TRUE(oauth.ForgetCredentials());
  server->CallbackReceived({{"state", state}, {"code", "isolated-late-code"}});
  network.last->Complete();
  EXPECT_EQ(network.requests, 1);
  EXPECT_FALSE(tokens.Exists());
  EXPECT_FALSE(oauth.HasAccessToken());
}

TEST_F(LocalResetTest, ConfirmationPauseAndCancelKeepNormalLoginState) {
  LocalNetwork network;
  passvault::oauth::TokenStore tokens(paths_.token_file);
  ASSERT_TRUE(tokens.SaveRefreshToken("demo-refresh"));
  passvault::oauth::GoogleOAuthClient oauth(&tokens);
  oauth.set_network_manager(&network);
  oauth.RefreshAccessToken();
  network.last->Complete();
  ASSERT_TRUE(oauth.HasAccessToken());
  const auto original = Read(paths_.token_file);
  oauth.SetPaused(true);
  oauth.SetPaused(false);
  EXPECT_TRUE(oauth.HasAccessToken());
  EXPECT_EQ(Read(paths_.token_file), original);
  EXPECT_EQ(network.requests, 1);
}

TEST_F(LocalResetTest,
       RefreshWaitTimeoutInvalidatesLateSuccessAfterLocalForget) {
  LocalNetwork network;
  passvault::oauth::TokenStore tokens(paths_.token_file);
  ASSERT_TRUE(tokens.SaveRefreshToken("demo-refresh"));
  passvault::oauth::GoogleOAuthClient oauth(&tokens);
  oauth.set_network_manager(&network);
  EXPECT_TRUE(oauth.EnsureAccessToken().isEmpty());
  ASSERT_EQ(network.requests, 1);
  EXPECT_TRUE(oauth.is_busy());
  EXPECT_FALSE(reset_.Eligible({true, false, false, oauth.is_busy()}));
  oauth.SetPaused(true);
  ASSERT_TRUE(oauth.ForgetCredentials());
  network.last->Complete();
  EXPECT_FALSE(tokens.Exists());
  EXPECT_FALSE(oauth.HasAccessToken());
}

TEST_F(LocalResetTest, ForgetCredentialsChecksRealTokenDeletionFailure) {
#ifdef _WIN32
  LocalNetwork network;
  passvault::oauth::TokenStore tokens(paths_.token_file);
  passvault::oauth::GoogleOAuthClient oauth(&tokens);
  oauth.set_network_manager(&network);
  ASSERT_TRUE(Begin());
  const auto path = paths_.token_file;
  HANDLE held =
      CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                  FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  ASSERT_NE(held, INVALID_HANDLE_VALUE);
  EXPECT_FALSE(
      reset_.Resume(settings_, [&] { return oauth.ForgetCredentials(); }));
  EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kOwned);
  EXPECT_TRUE(QFile::exists(path));
  EXPECT_EQ(network.requests, 0);
  CloseHandle(held);
  EXPECT_TRUE(
      reset_.Resume(settings_, [&] { return oauth.ForgetCredentials(); }));
  ExpectClean();
#endif
}

class ResetCloud : public passvault::sync::CloudStorageProvider {
 public:
  QByteArray bytes = "untouched-isolated-cloud";
  QString version = "2026-10-27T01:02:03.123456Z";
  int reads = 0;
  int uploads = 0;
  int signouts = 0;
  std::function<void()> on_read;
  QString ProviderName() const override { return "isolated fake"; }
  bool IsAuthenticated() const override { return true; }
  void SignOut() override { ++signouts; }
  bool UploadBackup(const QByteArray& data, const QString&, QString*) override {
    ++uploads;
    bytes = data;
    return true;
  }
  std::optional<QByteArray> DownloadBackup(const QString&, QString*) override {
    ++reads;
    return bytes;
  }
  std::optional<passvault::sync::CloudDownloadResult> DownloadBackupWithVersion(
      const QString&, QString*) override {
    ++reads;
    return passvault::sync::CloudDownloadResult{bytes, version};
  }
  passvault::sync::CloudReadResult ReadBackup(const QString&,
                                              QString*) override {
    ++reads;
    if (on_read) on_read();
    return {passvault::sync::CloudReadStatus::kFound, {bytes, version}};
  }
  passvault::sync::UploadIfMatchStatus UploadBackupIfMatch(
      const QByteArray& data, const QString&, const QString& expected,
      QString*) override {
    EXPECT_EQ(expected, version);
    ++uploads;
    bytes = data;
    return passvault::sync::UploadIfMatchStatus::kSuccess;
  }
};

TEST_F(LocalResetTest, PendingAndUnreadableMigrationRefuseBeforeIntent) {
  {
    auto db = Database::Open(paths_.DataFile("passvault.db"));
    passvault::storage::PasswordDao passwords(*db);
    passvault::storage::CategoryDao categories(*db);
    PasswordChangeCoordinator change(store_, manager_, passwords, categories,
                                     [] { return true; });
    ASSERT_TRUE(
        change.SaveBinding(passvault::master_password::CloudBinding::kLocal));
    change.set_checkpoint([](auto) { return false; });
    change.Change("isolated-master-123", "different-master-123",
                  "different-master-123", nullptr);
    EXPECT_EQ(PasswordChangeCoordinator::InspectRecovery(change.journal_path()),
              PasswordChangeCoordinator::RecoveryStatus::kPending);
    EXPECT_FALSE(Begin());
  }
  EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kAbsent);
#ifdef _WIN32
  const auto path = paths_.DataFile("master.dat.migration");
  HANDLE held =
      CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0,
                  nullptr, OPEN_EXISTING, 0, nullptr);
  ASSERT_NE(held, INVALID_HANDLE_VALUE);
  EXPECT_EQ(PasswordChangeCoordinator::InspectRecovery(path),
            PasswordChangeCoordinator::RecoveryStatus::kError);
  EXPECT_FALSE(Begin());
  CloseHandle(held);
#endif
  EXPECT_EQ(Read(store_.file_path()), master_bytes_);
}

TEST_F(LocalResetTest, SettingsRealSyncErrorPreservesIntentForForwardRetry) {
  ASSERT_TRUE(Begin());
  QSettings invalid(dir_.path(), QSettings::IniFormat);
  invalid.setValue("demo", true);
  EXPECT_FALSE(reset_.Resume(invalid, [] { return true; }));
  EXPECT_NE(invalid.status(), QSettings::NoError);
  EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kOwned);
  LocalReset restarted(paths_);
  EXPECT_TRUE(restarted.Resume(settings_, [] { return true; }));
  ExpectClean();
}

TEST_F(LocalResetTest, BadAndForeignStartupNeverOpenDatabaseOrReadCredentials) {
  ASSERT_TRUE(Begin());
  LocalResetPaths other{dir_.path() + "/other-install",
                        paths_.control_directory, paths_.token_file};
  for (int mode = 0; mode != 2; ++mode) {
    if (mode == 1) Write(reset_.marker_path(), "corrupt");
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [] {
      if (auto* box =
              qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        box->accept();
      }
    });
    timer.start(10);
    ResetCloud cloud;
    passvault::app::Application application(mode == 0 ? other : paths_, &cloud);
    EXPECT_EQ(application.Run(), 1);
    EXPECT_EQ(cloud.reads, 0);
    EXPECT_EQ(cloud.uploads, 0);
    EXPECT_EQ(Read(paths_.token_file), QByteArray("isolated-demo"));
    EXPECT_EQ(Read(paths_.DataFile("passvault.db")), db_bytes_);
    EXPECT_EQ(Read(other.DataFile("passvault.db")),
              QByteArray("isolated-demo"));
  }
}

class LocalResetRuntimeTest : public LocalResetTest,
                              public testing::WithParamInterface<int> {};

TEST_P(LocalResetRuntimeTest, ProductionRuntimeUsesOnlyInjectedTemporaryPaths) {
  const int mode = GetParam();
  // 0: rebuild and sync; 1: setup cancel; 2: final cancel; 3: wrong password;
  // 4: intent failure; 5: normal lock during confirmation; 6: resume startup.
  QSettings unrelated(QSettings::IniFormat, QSettings::UserScope,
                      "PassVaultTests", "UnrelatedApplication");
  unrelated.setValue("preserved", true);
  unrelated.sync();
  QSettings().clear();
  QSettings().setValue("ui/default_show_passwords", true);
  passvault::ui::ThemeManager::Instance()->ApplyTheme(
      passvault::ui::Theme::kDark);
  ResetCloud cloud;
  auto empty = passvault::model::SyncPayloadV2{}.ToJsonBytes();
  cloud.bytes =
      passvault::sync::CloudCryptoService::EncryptForCloud(
          empty, mode >= 11 ? "isolated-master-123" : "fresh-master-123")
          ->ToJsonBytes();
  const auto original_cloud = cloud.bytes;
  const auto original_version = cloud.version;
  int stage = mode == 6 ? 4 : 0;
  bool done = false;
  bool saw_setup = false;
  bool saw_confirmation = false;
  bool injected_setup_failure = false;
  bool saw_incomplete = false;
#ifdef _WIN32
  HANDLE held_master = INVALID_HANDLE_VALUE;
#endif
  QPointer<passvault::ui::MainWindow> old_window;
  QPointer<passvault::session::SessionManager> old_session;
  QPointer<passvault::ui::EditorPanel> old_editor;
  QObject driver_context;
  QElapsedTimer elapsed;
  elapsed.start();
  std::function<void()> step;
  step = [&]() {
    if (done) return;
    QTimer::singleShot(10, &driver_context, step);
    if (elapsed.elapsed() > 12000) {
      ADD_FAILURE() << "isolated runtime scenario timed out at stage " << stage;
      done = true;
      if (auto* dialog =
              qobject_cast<QDialog*>(QApplication::activeModalWidget()))
        dialog->reject();
      QApplication::quit();
      return;
    }
    auto* modal = QApplication::activeModalWidget();
    if (auto* dialog =
            qobject_cast<passvault::ui::MasterPasswordDialog*>(modal)) {
      if (stage == 0 || stage == 9) {
        dialog->findChild<QLineEdit*>("NewPasswordEdit")
            ->setText("isolated-master-123");
        stage = stage == 9 ? 8 : 1;
        dialog->findChild<QPushButton*>("PrimaryButton")->click();
        return;
      }
      if (stage == 4) {
        saw_setup = true;
        EXPECT_TRUE(old_window.isNull());
        EXPECT_TRUE(old_editor.isNull());
        EXPECT_TRUE(old_session.isNull());
        EXPECT_TRUE(QApplication::clipboard()->text().isEmpty());
        EXPECT_FALSE(QFile::exists(paths_.token_file));
        EXPECT_FALSE(
            QSettings().value("ui/default_show_passwords", false).toBool());
        EXPECT_EQ(passvault::ui::ThemeManager::Instance()->theme(),
                  passvault::ui::Theme::kSystem);
        EXPECT_FALSE(
            passvault::session::SessionManager::Instance()->IsUnlocked());
        EXPECT_EQ(passvault::session::SessionManager::Instance()->session_key(),
                  nullptr);
        EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kAbsent);
        EXPECT_EQ(cloud.bytes, original_cloud);
        EXPECT_EQ(cloud.version, original_version);
        EXPECT_EQ(cloud.reads + cloud.uploads + cloud.signouts, 0);
        UserInstanceLock competing(paths_.control_directory);
        EXPECT_FALSE(competing.TryLock());
        if (mode == 1) {
          done = true;
          dialog->reject();
          return;
        }
        dialog->findChild<QLineEdit*>("NewPasswordEdit")
            ->setText("fresh-master-123");
        dialog->findChild<QLineEdit*>("ConfirmPasswordEdit")
            ->setText("fresh-master-123");
        if (mode == 9 && !injected_setup_failure) {
          ASSERT_TRUE(QDir().mkpath(store_.file_path()));
          injected_setup_failure = true;
          stage = 10;
        } else {
          stage = 5;
        }
        dialog->findChild<QPushButton*>("PrimaryButton")->click();
        return;
      }
    }
    if (auto* input = qobject_cast<QInputDialog*>(modal)) {
      if (stage == 2) {
        if (mode == 5) {
          passvault::session::SessionManager::Instance()->Lock();
          stage = 9;
          input->reject();
          return;
        }
        input->setTextValue(mode == 3 ? "wrong-isolated-password"
                                      : "isolated-master-123");
        stage = 3;
        input->accept();
        return;
      }
    }
    if (auto* box = qobject_cast<QMessageBox*>(modal)) {
      if (stage == 12) {
        EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kAbsent);
        EXPECT_FALSE(old_window.isNull());
        EXPECT_FALSE(old_session.isNull());
        stage = 13;
        box->accept();
        return;
      }
      if (stage == 3) {
        saw_confirmation = true;
        EXPECT_EQ(box->defaultButton(), box->button(QMessageBox::Cancel));
        stage = (mode == 2 || mode == 11)                ? 8
                : (mode == 3 || mode == 4 || mode == 13) ? 7
                                                         : 4;
#ifdef _WIN32
        if (mode == 13) {
          held_master =
              CreateFileW(reinterpret_cast<LPCWSTR>(store_.file_path().utf16()),
                          GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
          ASSERT_NE(held_master, INVALID_HANDLE_VALUE);
        }
#endif
        box->button((mode == 2 || mode == 11) ? QMessageBox::Cancel
                                              : QMessageBox::Yes)
            ->click();
        return;
      }
      if ((mode == 7 || mode == 8 || mode == 10) && stage == 4) {
        saw_incomplete = true;
        EXPECT_TRUE(old_window.isNull());
        EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kOwned);
        EXPECT_EQ(cloud.reads + cloud.uploads + cloud.signouts, 0);
        box->button(QMessageBox::Retry)->click();
        return;
      }
      if (mode == 9 && stage == 10) {
        EXPECT_TRUE(QDir().rmdir(store_.file_path()));
        stage = 4;
        box->button(QMessageBox::Retry)->click();
        return;
      }
      if (stage == 7) {
#ifdef _WIN32
        if (held_master != INVALID_HANDLE_VALUE) {
          CloseHandle(held_master);
          held_master = INVALID_HANDLE_VALUE;
        }
#endif
        stage = 8;
        box->accept();
        return;
      }
      ADD_FAILURE() << "unexpected isolated runtime message";
      stage = 8;
      box->accept();
      return;
    }
    passvault::ui::MainWindow* window = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (auto* candidate = qobject_cast<passvault::ui::MainWindow*>(widget)) {
        if (candidate->isVisible()) window = candidate;
      }
    }
    if (!window) return;
    if (stage == 1) {
      old_window = window;
      old_session = passvault::session::SessionManager::Instance();
      old_editor = window->findChild<passvault::ui::EditorPanel*>();
      passvault::ui::ClipboardManager::Instance()->CopySensitive(
          "isolated-copy");
      QMetaObject::invokeMethod(window, "OnNewPassword", Qt::DirectConnection);
      if (mode == 11) {
        old_editor->findChild<QLineEdit*>("EditorTitleInput")
            ->setText("dirty-demo-entry");
        old_editor->ApplyGeneratedPassword("dirty-demo-password");
        old_editor->findChild<QPushButton*>("EditorSaveButton")->click();
        QMetaObject::invokeMethod(window, "OnNewPassword",
                                  Qt::DirectConnection);
      }
      old_editor->findChild<QLineEdit*>("EditorTitleInput")
          ->setText("unsaved-demo-draft");
      old_editor->ApplyGeneratedPassword("unsaved-demo-secret");
      if (mode == 12) {
        cloud.on_read = [&]() {
          stage = 12;
          QMetaObject::invokeMethod(old_window->preferences_page(),
                                    "ClearLocalDataRequested",
                                    Qt::DirectConnection);
        };
        QMetaObject::invokeMethod(window->preferences_page(),
                                  "SyncNowRequested", Qt::DirectConnection);
        cloud.on_read = {};
        return;
      }
      stage = 2;
      QMetaObject::invokeMethod(window->preferences_page(),
                                "ClearLocalDataRequested",
                                Qt::DirectConnection);
      return;
    }
    if (stage == 8) {
      EXPECT_EQ(window, old_window.data());
      EXPECT_EQ(passvault::session::SessionManager::Instance(),
                old_session.data());
      EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kAbsent);
      EXPECT_EQ(Read(store_.file_path()), master_bytes_);
      EXPECT_EQ(Read(paths_.token_file), QByteArray("isolated-demo"));
      EXPECT_TRUE(QSettings().value("ui/default_show_passwords").toBool());
      if (mode != 5) {
        EXPECT_TRUE(QApplication::clipboard()->text() == "isolated-copy");
        EXPECT_EQ(old_editor->Result().entry.title, "unsaved-demo-draft");
        EXPECT_TRUE(old_editor->Result().password == "unsaved-demo-secret");
      } else {
        EXPECT_TRUE(old_editor->Result().password.isEmpty());
      }
      EXPECT_TRUE(passvault::session::SessionManager::Instance()->IsUnlocked());
      EXPECT_EQ(cloud.reads + cloud.uploads + cloud.signouts, 0);
      if (mode == 11) {
        stage = 11;
        return;
      }
      done = true;
      QApplication::quit();
      return;
    }
    if (stage == 11 || stage == 13) {
      if (cloud.uploads == 0) return;
      EXPECT_EQ(cloud.uploads, 1);
      EXPECT_EQ(reset_.Inspect(), LocalReset::Intent::kAbsent);
      EXPECT_FALSE(old_window.isNull());
      done = true;
      QApplication::quit();
      return;
    }
    if (stage == 5) {
      EXPECT_TRUE(old_window.isNull());
      EXPECT_TRUE(old_session.isNull());
      auto* editor = window->findChild<passvault::ui::EditorPanel*>();
      EXPECT_TRUE(editor->Result().password.isEmpty());
      QMetaObject::invokeMethod(window, "OnNewPassword", Qt::DirectConnection);
      editor->findChild<QLineEdit*>("EditorTitleInput")->setText("fresh-entry");
      editor->findChild<QLineEdit*>("EditorUsernameInput")
          ->setText("demo-user");
      editor->ApplyGeneratedPassword("fresh-demo-password");
      editor->findChild<QPushButton*>("EditorSaveButton")->click();
      auto db = Database::Open(paths_.DataFile("passvault.db"));
      passvault::storage::PasswordDao passwords(*db);
      const auto rows = passwords.ReadAll();
      ASSERT_TRUE(rows);
      ASSERT_EQ(rows->size(), 1);
      EXPECT_EQ(rows->front().title, "fresh-entry");
      EXPECT_TRUE(passwords.ListDeletionMarkers()->empty());
      QMetaObject::invokeMethod(window->preferences_page(), "SyncNowRequested",
                                Qt::DirectConnection);
      EXPECT_GT(cloud.reads, 0);
      EXPECT_EQ(cloud.uploads, 1);
      EXPECT_NE(cloud.bytes, original_cloud);
      EXPECT_EQ(cloud.signouts, 0);
      done = true;
      QApplication::quit();
    }
  };
  if (mode == 6) {
    ASSERT_TRUE(Begin());
  }
  QTimer::singleShot(0, &driver_context, step);
  const bool previous_quit = QApplication::quitOnLastWindowClosed();
  QApplication::setQuitOnLastWindowClosed(true);
  {
    bool injected_io_failure = false;
    passvault::app::Application application(
        paths_, &cloud, [&](const QString& operation) {
          if (mode == 4 && operation == "intent") return false;
          const QString boundary = mode == 7   ? paths_.token_file
                                   : mode == 8 ? "marker"
                                               : "settings";
          if ((mode == 7 || mode == 8 || mode == 10) && operation == boundary &&
              !injected_io_failure) {
            injected_io_failure = true;
            return false;
          }
          return true;
        });
    EXPECT_EQ(application.Run(), 0);
  }
  QApplication::setQuitOnLastWindowClosed(previous_quit);
#ifdef _WIN32
  if (held_master != INVALID_HANDLE_VALUE) CloseHandle(held_master);
#endif
  EXPECT_TRUE(done);
  EXPECT_TRUE(unrelated.value("preserved").toBool());
  unrelated.clear();
  EXPECT_EQ(saw_setup, mode == 0 || mode == 1 || (mode >= 6 && mode <= 10));
  EXPECT_EQ(saw_incomplete, mode == 7 || mode == 8 || mode == 10);
  EXPECT_EQ(saw_confirmation, mode != 5 && mode != 6 && mode != 12);
  EXPECT_TRUE(QFile::exists(paths_.DataFile("keep.csv")));
  EXPECT_TRUE(QFile::exists(paths_.DataFile("unknown.dat")));
  QSettings().clear();
  passvault::ui::ClipboardManager::Instance()->ClearSensitive();
  passvault::ui::ThemeManager::Instance()->ApplyTheme(
      passvault::ui::Theme::kSystem);
}
INSTANTIATE_TEST_SUITE_P(RealApplication, LocalResetRuntimeTest,
                         testing::Range(0, 14));

}  // namespace

#include "local_reset_test.moc"
