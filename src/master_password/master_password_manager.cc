#include "master_password/master_password_manager.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <cstring>
#include <utility>

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

bool MeetsNewPasswordPolicy(std::string_view password) {
    return QString::fromUtf8(password.data(),
                             static_cast<qsizetype>(password.size()))
               .size() >= kMinNewPasswordLength;
}

std::string_view ViewOf(const QByteArray& bytes) {
    return std::string_view(bytes.constData(),
                            static_cast<std::size_t>(bytes.size()));
}

QByteArray MakeSalt() {
    const auto salt_vec = crypto::Random::Bytes(MasterPasswordStore::kKdfSaltSize);
    return QByteArray(reinterpret_cast<const char*>(salt_vec.data()),
                      static_cast<int>(salt_vec.size()));
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
    if (!MeetsNewPasswordPolicy(password)) {
        last_error_ = VerifyError::kPasswordTooShort;
        return std::nullopt;
    }
    return WriteRecordAndDerive(password, MakeSalt());
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

ChangeResult MasterPasswordManager::BuildChange(
    const QString& old_password, const QString& new_password,
    QByteArray* protected_record) {
    auto old_bytes = old_password.toUtf8();
    auto verified = VerifyLocal(ViewOf(old_bytes));
    old_bytes.fill('\0');
    if (!verified) {
        const auto error = last_error_ == VerifyError::kWrongPassword
                               ? ChangeError::kWrongPassword
                               : ChangeError::kStoreFailed;
        return {false, error,
                error == ChangeError::kWrongPassword
                    ? QStringLiteral("当前主密码不正确。")
                    : QStringLiteral("读取主密码记录失败。"), std::nullopt};
    }
    if (old_password == new_password) {
        return {false, ChangeError::kPasswordUnchanged,
                QStringLiteral("新主密码不能与当前主密码相同。"), std::nullopt};
    }
    auto bytes = new_password.toUtf8();
    if (bytes.isEmpty()) {
        bytes.fill('\0');
        return {false, ChangeError::kPasswordTooShort,
                QStringLiteral("请输入新主密码。"),
                std::nullopt};
    }
    MasterRecord record{HashHex(ViewOf(bytes)), MakeSalt()};
    auto key = DeriveSessionKey(ViewOf(bytes), record.kdf_salt);
    auto blob = MasterPasswordStore::Encode(record);
    if (!key || !blob || !protected_record) {
        bytes.fill('\0');
        return {false, ChangeError::kStoreFailed,
                QStringLiteral("生成新主密码记录失败。"), std::nullopt};
    }
    crypto::SecureBytes password;
    password.AssignFromString(ViewOf(bytes));
    bytes.fill('\0');
    *protected_record = std::move(*blob);
    return {true, ChangeError::kOk, {},
            UnlockPayload{std::move(*key), std::move(password)}};
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
