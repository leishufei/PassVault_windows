#pragma once

#include <QObject>
#include <QString>

#include <cstdint>

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
    struct Result {
        bool success = false;
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

    Result PerformSync(const QString& remote_name = QString::fromLatin1(
                           kDefaultRemoteFileName),
                       int max_retries = kDefaultMaxRetries);

    Result ChangeCloudMasterPassword(
        const QString& old_password, const QString& new_password,
        const QString& remote_name = QString::fromLatin1(
            kDefaultRemoteFileName));

 signals:
    void SyncStarted();
    void SyncFinished(bool success, const QString& message);

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

    bool DedupCategoriesByName(std::int64_t now_ms);

    storage::PasswordDao* pwd_dao_;
    storage::CategoryDao* cat_dao_;
    session::SessionManager* session_;
    CloudStorageProvider* provider_ = nullptr;
};

}  // namespace passvault::sync
