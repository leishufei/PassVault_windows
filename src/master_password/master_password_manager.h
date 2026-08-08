#pragma once

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

struct UnlockPayload {
    crypto::SessionKey session_key;
    crypto::SecureBytes master_password;
};

class MasterPasswordManager {
 public:
    static constexpr int kPbkdf2Iterations = 600000;

    explicit MasterPasswordManager(MasterPasswordStore& store);

    bool IsInitialized() const { return store_.Exists(); }

    std::optional<UnlockPayload> SetInitial(std::string_view password);

    std::optional<UnlockPayload> VerifyLocal(std::string_view password);

    // TODO(task-18): VerifyAgainstCloud — 需要 sync/oauth 到位后接入
    // 云端 payload 的 sha256 hex 校验，用于新机器绑定或本地 master.dat 丢失
    // 时的恢复流程。

    std::optional<UnlockPayload> ChangePassword(std::string_view old_password,
                                                std::string_view new_password);

    bool Reset();

    VerifyError last_error() const { return last_error_; }

 private:
    std::optional<UnlockPayload> WriteRecordAndDerive(
        std::string_view password, const QByteArray& kdf_salt);

    MasterPasswordStore& store_;
    VerifyError last_error_ = VerifyError::kNotInitialized;
};

}  // namespace passvault::master_password
