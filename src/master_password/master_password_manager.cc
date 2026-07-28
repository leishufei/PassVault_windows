#include "master_password/master_password_manager.h"

#include <QByteArray>

#include <cstdint>
#include <cstring>

#include "crypto/key_derivation.h"
#include "crypto/random.h"
#include "crypto/secure_bytes.h"
#include "crypto/session_key.h"
#include "crypto/sha256.h"

namespace passvault::master_password {

namespace {

QByteArray HashHex(std::string_view password) {
    return QByteArray::fromStdString(crypto::Sha256HexLower(password));
}

std::optional<crypto::SessionKey> DeriveSessionKey(
    std::string_view password, const QByteArray& kdf_salt) {
    crypto::SecureBytes key = crypto::KeyDerivation::Pbkdf2HmacSha256(
        password,
        reinterpret_cast<const std::uint8_t*>(kdf_salt.constData()),
        static_cast<std::size_t>(kdf_salt.size()),
        MasterPasswordManager::kPbkdf2Iterations,
        crypto::SessionKey::kSize);
    return crypto::SessionKey::FromSecureBytes(std::move(key));
}

}  // namespace

MasterPasswordManager::MasterPasswordManager(MasterPasswordStore& store)
    : store_(store) {}

std::optional<UnlockPayload> MasterPasswordManager::WriteRecordAndDerive(
    std::string_view password, const QByteArray& kdf_salt) {
    MasterRecord record;
    record.password_hash = HashHex(password);
    record.kdf_salt = kdf_salt;
    if (!store_.Save(record)) {
        last_error_ = VerifyError::kIoError;
        return std::nullopt;
    }
    auto session_key = DeriveSessionKey(password, kdf_salt);
    if (!session_key.has_value()) {
        last_error_ = VerifyError::kInternalError;
        return std::nullopt;
    }
    crypto::SecureBytes master_password;
    master_password.AssignFromString(password);
    last_error_ = VerifyError::kNotInitialized;
    return UnlockPayload{std::move(*session_key), std::move(master_password)};
}

std::optional<UnlockPayload> MasterPasswordManager::SetInitial(
    std::string_view password) {
    if (store_.Exists()) {
        last_error_ = VerifyError::kInternalError;
        return std::nullopt;
    }
    const auto salt_vec =
        crypto::Random::Bytes(MasterPasswordStore::kKdfSaltSize);
    QByteArray salt(reinterpret_cast<const char*>(salt_vec.data()),
                    static_cast<int>(salt_vec.size()));
    return WriteRecordAndDerive(password, salt);
}

std::optional<UnlockPayload> MasterPasswordManager::VerifyLocal(
    std::string_view password) {
    if (!store_.Exists()) {
        last_error_ = VerifyError::kNotInitialized;
        return std::nullopt;
    }
    const auto record = store_.Load();
    if (!record.has_value()) {
        last_error_ = VerifyError::kIoError;
        return std::nullopt;
    }
    const auto candidate = HashHex(password);
    if (candidate.size() != record->password_hash.size()) {
        last_error_ = VerifyError::kWrongPassword;
        return std::nullopt;
    }
    volatile unsigned char diff = 0;
    for (int i = 0; i < candidate.size(); ++i) {
        diff |= static_cast<unsigned char>(candidate[i]) ^
                static_cast<unsigned char>(record->password_hash[i]);
    }
    if (diff != 0) {
        last_error_ = VerifyError::kWrongPassword;
        return std::nullopt;
    }
    auto session_key = DeriveSessionKey(password, record->kdf_salt);
    if (!session_key.has_value()) {
        last_error_ = VerifyError::kInternalError;
        return std::nullopt;
    }
    crypto::SecureBytes master_password;
    master_password.AssignFromString(password);
    last_error_ = VerifyError::kNotInitialized;
    return UnlockPayload{std::move(*session_key), std::move(master_password)};
}

std::optional<UnlockPayload> MasterPasswordManager::ChangePassword(
    std::string_view old_password, std::string_view new_password) {
    auto verify = VerifyLocal(old_password);
    if (!verify.has_value()) return std::nullopt;
    const auto salt_vec =
        crypto::Random::Bytes(MasterPasswordStore::kKdfSaltSize);
    QByteArray salt(reinterpret_cast<const char*>(salt_vec.data()),
                    static_cast<int>(salt_vec.size()));
    return WriteRecordAndDerive(new_password, salt);
}

bool MasterPasswordManager::Reset() {
    if (!store_.Clear()) {
        last_error_ = VerifyError::kIoError;
        return false;
    }
    last_error_ = VerifyError::kNotInitialized;
    return true;
}

}  // namespace passvault::master_password
