#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "crypto/secure_bytes.h"
#include "crypto/session_key.h"
#include "master_password/master_password_store.h"
#include "master_password/password_policy.h"

namespace passvault::storage {
class PasswordDao;
}

namespace passvault::master_password {

enum class VerifyError {
    kNotInitialized,
    kWrongPassword,
    kPasswordTooShort,
    kIoError,
    kInternalError,
};

enum class ChangeError {
    kOk,
    kWrongPassword,
    kPasswordTooShort,
    kPasswordUnchanged,
    kDecryptFailed,
    kStoreFailed,
    kDatabaseFailed,
    kCloudFailed,
};

struct UnlockPayload {
    crypto::SessionKey session_key;
    crypto::SecureBytes master_password;
};

struct ChangeResult {
    bool success = false;
    ChangeError error = ChangeError::kOk;
    QString message;
    std::optional<UnlockPayload> payload;
};

class MasterPasswordManager {
 public:
    static constexpr int kPbkdf2Iterations = 600000;

    explicit MasterPasswordManager(MasterPasswordStore& store);

    // The reencryptor needs the password table; tests that only exercise the
    // local record can leave it null, in which case ChangePassword refuses.
    void set_password_dao(storage::PasswordDao* password_dao) {
        password_dao_ = password_dao;
    }

    bool IsInitialized() const { return store_.Exists(); }

    std::optional<UnlockPayload> SetInitial(std::string_view password);

    std::optional<UnlockPayload> VerifyLocal(std::string_view password);

    // TODO(task-18): VerifyAgainstCloud — 需要 sync/oauth 到位后接入
    // 云端 payload 的 sha256 hex 校验，用于新机器绑定或本地 master.dat 丢失
    // 时的恢复流程。

    // Step 1 of a master password change: verifies the old password,
    // re-encrypts every password row (tombstones included) inside one
    // transaction, and writes the new local record. On failure nothing is
    // written and the database still decrypts with the old password.
    ChangeResult PrepareChange(const QString& old_password,
                               const QString& new_password);

    // Step 2, only after the cloud backup has migrated: drops the rollback
    // snapshot. Safe to skip — the snapshot is freed with the manager.
    void CommitChange();

    // Undoes PrepareChange: restores the previous local record and the exact
    // ciphertext of every row it rewrote. Returns false if the rollback itself
    // could not be written, which leaves the caller to report a hard failure.
    bool RollBack();

    bool HasPendingChange() const { return !record_backup_.isEmpty(); }

    bool Reset();

    VerifyError last_error() const { return last_error_; }
    ChangeError last_change_error() const { return last_change_error_; }

 private:
    struct RowSnapshot {
        std::int64_t id = 0;
        QByteArray encrypted_password;
        QByteArray password_iv;
    };

    std::optional<UnlockPayload> WriteRecordAndDerive(
        std::string_view password, const QByteArray& kdf_salt);

    bool ReencryptRows(const crypto::SessionKey& old_key,
                       const crypto::SessionKey& new_key);

    bool RestoreRows();

    MasterPasswordStore& store_;
    storage::PasswordDao* password_dao_ = nullptr;
    QByteArray record_backup_;
    std::vector<RowSnapshot> row_backup_;
    VerifyError last_error_ = VerifyError::kNotInitialized;
    ChangeError last_change_error_ = ChangeError::kOk;
};

}  // namespace passvault::master_password
