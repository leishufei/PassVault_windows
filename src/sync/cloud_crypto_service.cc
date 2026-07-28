#include "sync/cloud_crypto_service.h"

#include <QString>

#include <cstdint>
#include <vector>

#include "crypto/crypto_service.h"
#include "crypto/key_derivation.h"
#include "crypto/random.h"
#include "crypto/secure_bytes.h"

namespace passvault::sync {

namespace {

constexpr std::size_t kSaltSize = 16;
constexpr std::size_t kIvSize = 12;
constexpr int kCloudIterations = model::CloudBackupFormat::kIterations;

}  // namespace

std::optional<model::CloudBackupFormat> CloudCryptoService::EncryptForCloud(
    const QByteArray& payload_json,
    std::string_view master_password) {
    const std::vector<std::uint8_t> salt = crypto::Random::Bytes(kSaltSize);
    const std::vector<std::uint8_t> iv = crypto::Random::Bytes(kIvSize);

    crypto::SecureBytes key;
    try {
        key = crypto::KeyDerivation::Pbkdf2HmacSha256(
            master_password,
            salt.data(), salt.size(),
            kCloudIterations,
            crypto::CryptoService::kKeySize);
    } catch (...) {
        return std::nullopt;
    }

    const QByteArray ct_and_tag = crypto::CryptoService::EncryptGcm(
        key.data(), key.size(),
        iv.data(), iv.size(),
        reinterpret_cast<const std::uint8_t*>(payload_json.constData()),
        static_cast<std::size_t>(payload_json.size()));
    if (static_cast<std::size_t>(ct_and_tag.size()) !=
        static_cast<std::size_t>(payload_json.size()) + crypto::CryptoService::kTagSize) {
        return std::nullopt;
    }

    model::CloudBackupFormat out;
    out.version = model::CloudBackupFormat::kVersion;
    out.kdf_algorithm = QString::fromLatin1(model::CloudBackupFormat::kKdfAlgorithm);
    out.iterations = kCloudIterations;
    out.encryption_algorithm = QString::fromLatin1(model::CloudBackupFormat::kEncryptionAlgorithm);
    out.salt = QString::fromLatin1(
        QByteArray(reinterpret_cast<const char*>(salt.data()), static_cast<int>(salt.size())).toBase64());
    out.iv = QString::fromLatin1(
        QByteArray(reinterpret_cast<const char*>(iv.data()), static_cast<int>(iv.size())).toBase64());
    out.ciphertext = QString::fromLatin1(ct_and_tag.toBase64());
    return out;
}

std::optional<QByteArray> CloudCryptoService::DecryptFromCloud(
    const model::CloudBackupFormat& backup,
    std::string_view master_password) {
    if (backup.kdf_algorithm != QString::fromLatin1(model::CloudBackupFormat::kKdfAlgorithm)) {
        return std::nullopt;
    }
    if (backup.encryption_algorithm != QString::fromLatin1(model::CloudBackupFormat::kEncryptionAlgorithm)) {
        return std::nullopt;
    }
    if (backup.iterations <= 0) {
        return std::nullopt;
    }

    const QByteArray salt = QByteArray::fromBase64(backup.salt.toLatin1());
    const QByteArray iv = QByteArray::fromBase64(backup.iv.toLatin1());
    const QByteArray ct_and_tag = QByteArray::fromBase64(backup.ciphertext.toLatin1());

    if (static_cast<std::size_t>(salt.size()) != kSaltSize) return std::nullopt;
    if (static_cast<std::size_t>(iv.size()) != kIvSize) return std::nullopt;
    if (static_cast<std::size_t>(ct_and_tag.size()) < crypto::CryptoService::kTagSize) return std::nullopt;

    crypto::SecureBytes key;
    try {
        key = crypto::KeyDerivation::Pbkdf2HmacSha256(
            master_password,
            reinterpret_cast<const std::uint8_t*>(salt.constData()),
            static_cast<std::size_t>(salt.size()),
            backup.iterations,
            crypto::CryptoService::kKeySize);
    } catch (...) {
        return std::nullopt;
    }

    return crypto::CryptoService::DecryptGcm(
        key.data(), key.size(),
        reinterpret_cast<const std::uint8_t*>(iv.constData()),
        static_cast<std::size_t>(iv.size()),
        reinterpret_cast<const std::uint8_t*>(ct_and_tag.constData()),
        static_cast<std::size_t>(ct_and_tag.size()));
}

}  // namespace passvault::sync
