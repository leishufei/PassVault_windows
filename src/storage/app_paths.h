#pragma once

#include <QString>

namespace passvault::storage {

QString DefaultDataDirectory();
QString LegacyAppDataDirectory();
QString LegacyLocalAppDataDirectory();

bool MoveFileIfMissing(const QString& source, const QString& target);

}  // namespace passvault::storage
