#include "master_password/master_password_manager.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <cstdint>
#include <cstring>
#include <utility>

#include "crypto/crypto_service.h"
#include "crypto/key_derivation.h"
#include "crypto/random.h"
#include "crypto/secure_bytes.h"
#include "crypto/session_key.h"
#include "crypto/sha256.h"
#include "model/password_entry.h"
#include "storage/database.h"
#include "storage/password_dao.h"

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

// An entry that carries no ciphertext is a placeholder and is copied through
// unchanged instead of being treated as a decrypt failure.
bool HasCiphertext(const model::PasswordEntry& entry) {
    return !entry.encrypted_password.isEmpty() || !entry.password_iv.isEmpty();
}

bool ReencryptEntry(const model::PasswordEntry& entry,
                    const crypto::SessionKey& old_key,
                    const crypto::SessionKey& new_key,
                    model::PasswordEntry* out) {
    if (static_cast<std::size_t>(entry.password_iv.size()) !=
            crypto::CryptoService::kIvSize ||
        static_cast<std::size_t>(entry.encrypted_password.size()) <
            crypto::CryptoService::kTagSize) {
        return false;
    }
    auto plain = crypto::CryptoService::DecryptGcm(
        old_key.data(), old_key.size(),
        reinterpret_cast<const std::uint8_t*>(entry.password_iv.constData()),
        static_cast<std::size_t>(entry.password_iv.size()),
        reinterpret_cast<const std::uint8_t*>(entry.encrypted_password.constData()),
        static_cast<std::size_t>(entry.encrypted_password.size()));
    if (!plain) return false;

    const auto iv_bytes = crypto::Random::Bytes(crypto::CryptoService::kIvSize);
    const QByteArray ct = crypto::CryptoService::EncryptGcm(
        new_key.data(), new_key.size(), iv_bytes.data(), iv_bytes.size(),
        reinterpret_cast<const std::uint8_t*>(plain->constData()),
        static_cast<std::size_t>(plain->size()));
    if (ct.isEmpty() && !plain->isEmpty()) return false;

    *out = entry;
    out->encrypted_password = ct;
    out->password_iv = QByteArray(reinterpret_cast<const char*>(iv_bytes.data()),
                                  static_cast<int>(iv_bytes.size()));
    return true;
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

bool MasterPasswordManager::ReencryptRows(const crypto::SessionKey& old_key,
                                          const crypto::SessionKey& new_key) {
    if (password_dao_ == nullptr) return true;

    auto entries = password_dao_->ListIncludingDeleted();
    if (entries.empty()) return true;

    storage::Transaction txn(password_dao_->database());
    row_backup_.clear();
    row_backup_.reserve(entries.size());

    for (const auto& entry : entries) {
        if (!HasCiphertext(entry)) continue;

        model::PasswordEntry updated;
        if (!ReencryptEntry(entry, old_key, new_key, &updated)) {
            last_change_error_ = ChangeError::kDecryptFailed;
            return false;
        }
        if (!password_dao_->Update(updated)) {
            last_change_error_ = ChangeError::kDatabaseFailed;
            return false;
        }
        row_backup_.push_back(
            RowSnapshot{entry.id, entry.encrypted_password, entry.password_iv});
    }

    if (!txn.Commit()) {
        last_change_error_ = ChangeError::kDatabaseFailed;
        return false;
    }
    return true;
}

bool MasterPasswordManager::RestoreRows() {
    if (password_dao_ == nullptr || row_backup_.empty()) return true;

    storage::Transaction txn(password_dao_->database());
    for (const auto& snapshot : row_backup_) {
        auto entry = password_dao_->FindById(snapshot.id);
        if (!entry.has_value()) return false;
        entry->encrypted_password = snapshot.encrypted_password;
        entry->password_iv = snapshot.password_iv;
        if (!password_dao_->Update(*entry)) return false;
    }
    return txn.Commit();
}

ChangeResult MasterPasswordManager::PrepareChange(const QString& old_password,
                                                  const QString& new_password) {
    const QByteArray old_bytes = old_password.toUtf8();
    const QByteArray new_bytes = new_password.toUtf8();
    const std::string_view old_view = ViewOf(old_bytes);
    const std::string_view new_view = ViewOf(new_bytes);

    if (password_dao_ == nullptr) {
        last_change_error_ = ChangeError::kDatabaseFailed;
        return {false, ChangeError::kDatabaseFailed,
                QStringLiteral("密码库不可用，未修改主密码。"), std::nullopt};
    }
    if (old_password == new_password) {
        last_change_error_ = ChangeError::kPasswordUnchanged;
        return {false, ChangeError::kPasswordUnchanged,
                QStringLiteral("新主密码不能与当前主密码相同。"), std::nullopt};
    }

    auto verified = VerifyLocal(old_view);
    if (!verified.has_value()) {
        const ChangeError error =
            last_error_ == VerifyError::kPasswordTooShort
                ? ChangeError::kPasswordTooShort
                : ChangeError::kWrongPassword;
        last_change_error_ = error;
        return {false, error, QStringLiteral("当前主密码不正确。"), std::nullopt};
    }
    if (!MeetsNewPasswordPolicy(new_view)) {
        last_change_error_ = ChangeError::kPasswordTooShort;
        return {false, ChangeError::kPasswordTooShort,
                QStringLiteral("主密码至少 %1 位。").arg(kMinNewPasswordLength),
                std::nullopt};
    }

    const QByteArray new_salt = MakeSalt();
    auto new_key = DeriveSessionKey(new_view, new_salt);
    if (!new_key.has_value()) {
        last_change_error_ = ChangeError::kStoreFailed;
        return {false, ChangeError::kStoreFailed,
                QStringLiteral("无法派生新的加密密钥。"), std::nullopt};
    }

    // A caller may legitimately retry after a rolled-back attempt; drop the
    // stale snapshot before taking a new one.
    row_backup_.clear();
    record_backup_.clear();
    last_change_error_ = ChangeError::kOk;

    if (!ReencryptRows(verified->session_key, *new_key)) {
        const ChangeError error = last_change_error_;
        row_backup_.clear();
        return {false, error,
                error == ChangeError::kDecryptFailed
                    ? QStringLiteral("存在无法解密的密码，未修改主密码。")
                    : QStringLiteral("重写本地密码失败，未修改主密码。"),
                std::nullopt};
    }

    auto record_backup = store_.LoadRaw();
    if (!record_backup.has_value()) {
        // Without a rollback point the cloud migration could strand the
        // database on a key no record points at, so refuse to proceed.
        const bool rows_restored = RestoreRows();
        row_backup_.clear();
        last_change_error_ = ChangeError::kStoreFailed;
        return {false, ChangeError::kStoreFailed,
                rows_restored
                    ? QStringLiteral("无法读取主密码记录，未修改主密码。")
                    : QStringLiteral("无法读取主密码记录，且本地数据未完全还原。"),
                std::nullopt};
    }
    record_backup_ = std::move(*record_backup);

    auto payload = WriteRecordAndDerive(new_view, new_salt);
    if (!payload.has_value()) {
        const bool rows_restored = RestoreRows();
        const bool record_restored = store_.SaveRaw(record_backup_);
        if (rows_restored) row_backup_.clear();
        if (record_restored) record_backup_.clear();
        last_change_error_ = ChangeError::kStoreFailed;
        return {false, ChangeError::kStoreFailed,
                (rows_restored && record_restored)
                    ? QStringLiteral("无法写入新的主密码记录，已回滚。")
                    : QStringLiteral("无法写入新的主密码记录，且已回滚失败。"),
                std::nullopt};
    }

    last_change_error_ = ChangeError::kOk;
    return {true, ChangeError::kOk, QString(), std::move(payload)};
}

void MasterPasswordManager::CommitChange() {
    row_backup_.clear();
    record_backup_.clear();
    last_change_error_ = ChangeError::kOk;
}

bool MasterPasswordManager::RollBack() {
    bool ok = true;
    if (!record_backup_.isEmpty()) {
        if (!store_.SaveRaw(record_backup_)) {
            ok = false;
        } else {
            record_backup_.clear();
        }
    }
    if (!RestoreRows()) {
        ok = false;
    } else {
        row_backup_.clear();
    }
    last_change_error_ = ok ? ChangeError::kOk : ChangeError::kStoreFailed;
    return ok;
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
