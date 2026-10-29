#pragma once

#include <QObject>
#include <QString>

#include <cstdint>
#include <optional>

namespace passvault::storage {
class PasswordDao;
class CategoryDao;
}  // namespace passvault::storage

namespace passvault::session {
class SessionManager;
}  // namespace passvault::session

namespace passvault::sync {

class CloudStorageProvider;

class SyncManager : public QObject {
    Q_OBJECT

 public:
    enum class StatusKind {
        kSuccess,
        kConflict,
        kError,
    };
    Q_ENUM(StatusKind)

    struct Result {
        bool success = false;
        StatusKind kind = StatusKind::kError;
        QString message;
    };

    static constexpr int kDefaultMaxRetries = 3;
    static constexpr const char* kDefaultRemoteFileName =
        "PassVault_Cloud_Backup.json";

    SyncManager(storage::PasswordDao* pwd_dao,
                storage::CategoryDao* cat_dao,
                session::SessionManager* session,
                QObject* parent = nullptr);

    void set_provider(CloudStorageProvider* provider) { provider_ = provider; }
    CloudStorageProvider* provider() const { return provider_; }

    void set_paused(bool paused) { paused_ = paused; }
    bool is_busy() const { return busy_; }

    Result PerformSync(const QString& remote_name = QString::fromLatin1(
                           kDefaultRemoteFileName),
                       int max_retries = kDefaultMaxRetries);

 signals:
    void SyncStarted();
    void SyncFinished(StatusKind kind, const QString& message);

 private:
    enum class DoSyncStatus {
        kSuccess,
        kVersionMismatch,
        kFatal,
    };

    struct DoSyncOutcome {
        DoSyncStatus status = DoSyncStatus::kFatal;
        QString message;
    };

    DoSyncOutcome DoSyncOnce(const QString& remote_name);

    std::optional<bool> DedupCategoriesByName(std::int64_t now_ms);

    storage::PasswordDao* pwd_dao_;
    storage::CategoryDao* cat_dao_;
    session::SessionManager* session_;
    CloudStorageProvider* provider_ = nullptr;
    bool paused_ = false;
    bool busy_ = false;
};

}  // namespace passvault::sync
