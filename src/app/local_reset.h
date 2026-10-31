#pragma once

#include <QLockFile>
#include <QString>
#include <QStringList>
#include <functional>

class QSettings;

namespace passvault::app {

struct LocalResetPaths {
  QString installation;
  QString data_directory;
  QString control_directory;
  QString token_file;
  static LocalResetPaths Default();
  QString DataFile(const QString& name) const;
};

class UserInstanceLock {
 public:
  explicit UserInstanceLock(const QString& control_directory);
  bool TryLock();

 private:
  QString directory_;
  QLockFile lock_;
};

class LocalReset {
 public:
  enum class Intent { kAbsent, kOwned, kOtherInstallation, kInvalid };
  struct Eligibility {
    bool unlocked = false;
    bool password_change = false;
    bool sync_busy = false;
    bool oauth_busy = false;
  };
  explicit LocalReset(LocalResetPaths paths);
  Intent Inspect() const;
  bool Eligible(const Eligibility& state) const;
  bool Begin(const Eligibility& state, bool confirmed,
             const std::function<bool()>& verify);
  bool Resume(QSettings& settings,
              const std::function<bool()>& release_runtime);
  QString marker_path() const;
  QStringList Files() const;
  const LocalResetPaths& paths() const { return paths_; }
  // A false hook injects an I/O failure before the named durable operation.
  void set_io_check(std::function<bool(const QString&)> check) {
    io_check_ = std::move(check);
  }

 private:
  bool Check(const QString& operation) const;
  bool ValidPaths() const;
  QString Owner() const;
  LocalResetPaths paths_;
  std::function<bool(const QString&)> io_check_;
};

}  // namespace passvault::app
