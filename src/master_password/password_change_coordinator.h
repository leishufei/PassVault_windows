#pragma once

#include <QByteArray>
#include <QString>

#include <functional>
#include <optional>

#include "master_password/master_password_manager.h"

namespace passvault::storage {
class PasswordDao;
class CategoryDao;
}
namespace passvault::sync {
class CloudStorageProvider;
}

namespace passvault::master_password {

enum class CloudBinding { kUnknown, kLocal, kBound, kError };

class PasswordChangeCoordinator {
 public:
    enum class Outcome { kUnchanged, kCompleted, kLocked };
    enum class Boundary {
        kPrepared,
        kBeforeUpload,
        kAfterUpload,
        kCloudConfirmed,
        kBeforeDatabase,
        kAfterDatabase,
        kBeforeRecord,
        kAfterRecord,
        kBeforeFinish,
    };
    struct Result {
        Outcome outcome = Outcome::kUnchanged;
        QString message;
        std::optional<UnlockPayload> payload;
    };

    PasswordChangeCoordinator(MasterPasswordStore& store,
                              MasterPasswordManager& manager,
                              storage::PasswordDao& passwords,
                              storage::CategoryDao& categories,
                              std::function<bool()> invalidate_hello);

    CloudBinding ReadBinding() const;
    bool SaveBinding(CloudBinding binding) const;
    enum class RecoveryStatus { kIdle, kPending, kError };
    static RecoveryStatus InspectRecovery(const QString& journal_path);
    bool NeedsRecovery() const;
    Result Change(const QString& old_password, const QString& new_password,
                  const QString& confirmation,
                  sync::CloudStorageProvider* provider);
    Result Recover(sync::CloudStorageProvider* provider);

    // A false checkpoint simulates process termination at a durable boundary.
    void set_checkpoint(std::function<bool(Boundary)> checkpoint) {
        checkpoint_ = std::move(checkpoint);
    }
    QString journal_path() const;

 private:
    struct Journal;
    bool Save(const Journal& journal) const;
    std::optional<Journal> Load() const;
    static std::optional<Journal> LoadJournal(const QString& path);
    bool FinishJournal() const;
    bool Check(Boundary boundary) const;
    Result Commit(Journal& journal);
    Result ResolveUpload(Journal& journal,
                         sync::CloudStorageProvider* provider);

    MasterPasswordStore& store_;
    MasterPasswordManager& manager_;
    storage::PasswordDao& passwords_;
    storage::CategoryDao& categories_;
    std::function<bool()> invalidate_hello_;
    std::function<bool(Boundary)> checkpoint_;
    bool running_ = false;
};

}  // namespace passvault::master_password
