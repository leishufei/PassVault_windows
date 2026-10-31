#include "storage/app_paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtGlobal>

namespace passvault::storage {

QString DefaultDataDirectory() {
  const QString app_data = qEnvironmentVariable("APPDATA");
  if (!app_data.isEmpty()) {
    return QDir(app_data).filePath(QStringLiteral("PassVault"));
  }
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString LegacyAppDataDirectory() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString LegacyLocalAppDataDirectory() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

bool MoveFileIfMissing(const QString& source, const QString& target) {
  if (source == target || QFile::exists(target)) return true;
  if (!QFile::exists(source)) return false;
  const QDir parent = QFileInfo(target).dir();
  if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) return false;
  if (QFile::rename(source, target)) return true;
  if (!QFile::copy(source, target)) return false;
  QFile::remove(source);
  return true;
}

}  // namespace passvault::storage
