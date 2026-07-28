#include "model/cloud_backup_format.h"

#include <QJsonDocument>
#include <QJsonValue>

namespace passvault::model {

QJsonObject CloudBackupFormat::ToJsonObject() const {
    QJsonObject obj;
    obj.insert("version", version);
    obj.insert("kdfAlgorithm", kdf_algorithm);
    obj.insert("iterations", iterations);
    obj.insert("salt", salt);
    obj.insert("encryptionAlgorithm", encryption_algorithm);
    obj.insert("iv", iv);
    obj.insert("ciphertext", ciphertext);
    return obj;
}

QByteArray CloudBackupFormat::ToJsonBytes() const {
    return QJsonDocument(ToJsonObject()).toJson(QJsonDocument::Compact);
}

std::optional<CloudBackupFormat> CloudBackupFormat::FromJsonBytes(const QByteArray& bytes) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    return FromJsonObject(doc.object());
}

std::optional<CloudBackupFormat> CloudBackupFormat::FromJsonObject(const QJsonObject& obj) {
    CloudBackupFormat out;
    out.version = obj.value("version").toInt(kVersion);
    out.kdf_algorithm = obj.value("kdfAlgorithm").toString();
    out.iterations = obj.value("iterations").toInt(kIterations);
    out.salt = obj.value("salt").toString();
    out.encryption_algorithm = obj.value("encryptionAlgorithm").toString();
    out.iv = obj.value("iv").toString();
    out.ciphertext = obj.value("ciphertext").toString();

    if (out.salt.isEmpty() || out.iv.isEmpty() || out.ciphertext.isEmpty()) {
        return std::nullopt;
    }
    return out;
}

}  // namespace passvault::model
