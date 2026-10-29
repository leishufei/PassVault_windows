#pragma once

#include <QByteArray>
#include <QString>

#include <optional>
#include <string_view>

#include "crypto/secure_bytes.h"
#include "crypto/session_key.h"
#include "master_password/master_password_store.h"
#include "master_password/password_policy.h"

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

    bool IsInitialized() const { return store_.Exists(); }

    std::optional<UnlockPayload> SetInitial(std::string_view password);

    std::optional<UnlockPayload> VerifyLocal(std::string_view password);

    // Builds a new protected record without changing the store or database.
    ChangeResult BuildChange(const QString& old_password,
                             const QString& new_password,
                             QByteArray* protected_record);

    bool Reset();

    VerifyError last_error() const { return last_error_; }

 private:
    std::optional<UnlockPayload> WriteRecordAndDerive(
        std::string_view password, const QByteArray& kdf_salt);

    MasterPasswordStore& store_;
    VerifyError last_error_ = VerifyError::kNotInitialized;
};

}  // namespace passvault::master_password
