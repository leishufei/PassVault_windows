#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <optional>

namespace passvault::model {

struct CloudBackupFormat {
    static constexpr int kVersion = 1;
    static constexpr int kIterations = 600000;
    static constexpr const char* kKdfAlgorithm = "PBKDF2WithHmacSHA256";
    static constexpr const char* kEncryptionAlgorithm = "AES-256-GCM";

    int version = kVersion;
    QString kdf_algorithm = QString::fromLatin1(kKdfAlgorithm);
    int iterations = kIterations;
    QString salt;
    QString encryption_algorithm = QString::fromLatin1(kEncryptionAlgorithm);
    QString iv;
    QString ciphertext;

    QJsonObject ToJsonObject() const;
    QByteArray ToJsonBytes() const;

    static std::optional<CloudBackupFormat> FromJsonBytes(const QByteArray& bytes);
    static std::optional<CloudBackupFormat> FromJsonObject(const QJsonObject& obj);
};

}  // namespace passvault::model
