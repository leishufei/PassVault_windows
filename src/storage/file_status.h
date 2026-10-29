#pragma once

#include <QString>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>

#include <cerrno>
#endif

namespace passvault::storage {

enum class FileStatus { kMissing, kFile, kError };

inline FileStatus LocalFileStatus(const QString& path) {
#ifdef _WIN32
  const auto attributes =
      GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const auto error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
               ? FileStatus::kMissing
               : FileStatus::kError;
  }
  if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
    return FileStatus::kError;
  }
  return FileStatus::kFile;
#else
  struct stat info{};
  if (lstat(path.toUtf8().constData(), &info) != 0) {
    return errno == ENOENT ? FileStatus::kMissing : FileStatus::kError;
  }
  return S_ISREG(info.st_mode) ? FileStatus::kFile : FileStatus::kError;
#endif
}

}  // namespace passvault::storage
