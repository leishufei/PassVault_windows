#include "app/local_reset.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include "master_password/password_change_coordinator.h"
#include "oauth/token_store.h"
#include "storage/file_status.h"

namespace passvault::app {
namespace {

QString Canonical(const QString& path) {
  QString result = QFileInfo(path).canonicalFilePath();
#ifdef _WIN32
  result = result.toCaseFolded();
#endif
  return result;
}

bool RemoveFile(const QString& path) {
  const auto status = storage::LocalFileStatus(path);
  if (status == storage::FileStatus::kMissing) return true;
  return status == storage::FileStatus::kFile && QFile::remove(path);
}

}  // namespace

LocalResetPaths LocalResetPaths::Default() {
  return {
      QCoreApplication::applicationDirPath(),
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
          QStringLiteral("/control"),
      oauth::TokenStore::DefaultFilePath()};
}

QString LocalResetPaths::DataFile(const QString& name) const {
  return QDir(installation).filePath(QStringLiteral("data/") + name);
}

UserInstanceLock::UserInstanceLock(const QString& directory)
    : directory_(directory), lock_(QDir(directory).filePath("instance.lock")) {
  lock_.setStaleLockTime(0);
}

bool UserInstanceLock::TryLock() {
  return QDir().mkpath(directory_) && lock_.tryLock(0);
}

LocalReset::LocalReset(LocalResetPaths paths) : paths_(std::move(paths)) {}

QString LocalReset::marker_path() const {
  return QDir(paths_.control_directory).filePath("local-reset.json");
}

QString LocalReset::Owner() const {
  const auto canonical = Canonical(paths_.installation);
  if (canonical.isEmpty()) return {};
  return QString::fromLatin1(
      QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256)
          .toHex());
}

bool LocalReset::ValidPaths() const {
  if (Owner().isEmpty() || !QDir::isAbsolutePath(paths_.installation) ||
      !QDir::isAbsolutePath(paths_.control_directory) ||
      !QDir::isAbsolutePath(paths_.token_file))
    return false;
  const auto data = QDir(paths_.installation).filePath("data");
  if (QFileInfo::exists(data)) {
    QString expected =
        QDir::cleanPath(Canonical(paths_.installation) + "/data");
#ifdef _WIN32
    expected = expected.toCaseFolded();
#endif
    if (Canonical(data) != expected) return false;
  }
  return true;
}

LocalReset::Intent LocalReset::Inspect() const {
  if (!ValidPaths()) return Intent::kInvalid;
  const auto status = storage::LocalFileStatus(marker_path());
  if (status == storage::FileStatus::kMissing) return Intent::kAbsent;
  if (status != storage::FileStatus::kFile) return Intent::kInvalid;
  QFile file(marker_path());
  if (!file.open(QIODevice::ReadOnly) || file.size() > 4096)
    return Intent::kInvalid;
  const auto bytes = file.readAll();
  if (file.error() != QFileDevice::NoError) return Intent::kInvalid;
  const auto doc = QJsonDocument::fromJson(bytes);
  if (!doc.isObject()) return Intent::kInvalid;
  const auto object = doc.object();
  const auto owner = object.value("owner").toString();
  if (object.size() != 4 || object.value("version") != 1 ||
      object.value("scope") != "passvault-local-reset" ||
      object.value("state") != "pending" || owner.size() != 64) {
    return Intent::kInvalid;
  }
  for (const auto ch : owner) {
    if (!QStringLiteral("0123456789abcdef").contains(ch))
      return Intent::kInvalid;
  }
  return owner == Owner() ? Intent::kOwned : Intent::kOtherInstallation;
}

bool LocalReset::Eligible(const Eligibility& state) const {
  using Coordinator = master_password::PasswordChangeCoordinator;
  return state.unlocked && !state.password_change && !state.sync_busy &&
         !state.oauth_busy && Inspect() == Intent::kAbsent &&
         Coordinator::InspectRecovery(paths_.DataFile(
             "master.dat.migration")) == Coordinator::RecoveryStatus::kIdle;
}

bool LocalReset::Check(const QString& operation) const {
  return !io_check_ || io_check_(operation);
}

bool LocalReset::Begin(const Eligibility& state, bool confirmed,
                       const std::function<bool()>& verify) {
  if (!confirmed || !Eligible(state) || !verify || !verify() ||
      !Eligible(state) || !Check("intent") ||
      !QDir().mkpath(paths_.control_directory))
    return false;
  QSaveFile file(marker_path());
  const auto bytes =
      QJsonDocument(QJsonObject{{"version", 1},
                                {"scope", "passvault-local-reset"},
                                {"owner", Owner()},
                                {"state", "pending"}})
          .toJson(QJsonDocument::Compact);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() &&
         file.commit();
}

QStringList LocalReset::Files() const {
  QStringList files;
  for (const auto* name :
       {"passvault.db", "passvault.db-wal", "passvault.db-shm",
        "passvault.db-journal", "master.dat", "hello_unlock.dat",
        "master.dat.binding", "master.dat.migration"}) {
    files.append(paths_.DataFile(QString::fromLatin1(name)));
  }
  files.append(paths_.token_file);
  return files;
}

bool LocalReset::Resume(QSettings& settings,
                        const std::function<bool()>& release_runtime) {
  using Coordinator = master_password::PasswordChangeCoordinator;
  if (Inspect() != Intent::kOwned ||
      Coordinator::InspectRecovery(paths_.DataFile("master.dat.migration")) !=
          Coordinator::RecoveryStatus::kIdle ||
      !release_runtime || !release_runtime())
    return false;
  for (const auto& path : Files()) {
    if (!Check(path) || !RemoveFile(path)) return false;
  }
  if (!Check("settings")) return false;
  settings.setFallbacksEnabled(false);
  settings.clear();
  settings.sync();
  if (settings.status() != QSettings::NoError || !Check("marker")) return false;
  return RemoveFile(marker_path());
}

}  // namespace passvault::app
