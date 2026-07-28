#include "master_password/master_password_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtGlobal>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>
#endif

namespace passvault::master_password {

namespace {

constexpr const char* kJsonKeyPasswordHash = "password_hash";
constexpr const char* kJsonKeyKdfSalt = "kdf_salt";

std::optional<QByteArray> ProtectData(const QByteArray& plaintext) {
#ifdef _WIN32
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.constData()));
    in.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"passvault-master", nullptr, nullptr, nullptr,
                          0, &out)) {
        return std::nullopt;
    }
    QByteArray result(reinterpret_cast<const char*>(out.pbData),
                      static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return result;
#else
    Q_UNUSED(plaintext);
    return std::nullopt;
#endif
}

std::optional<QByteArray> UnprotectData(const QByteArray& ciphertext) {
#ifdef _WIN32
    DATA_BLOB in{};
    in.pbData =
        reinterpret_cast<BYTE*>(const_cast<char*>(ciphertext.constData()));
    in.cbData = static_cast<DWORD>(ciphertext.size());

    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        return std::nullopt;
    }
    QByteArray result(reinterpret_cast<const char*>(out.pbData),
                      static_cast<int>(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
#else
    Q_UNUSED(ciphertext);
    return std::nullopt;
#endif
}

QByteArray SerializeRecord(const MasterRecord& record) {
    QJsonObject obj;
    obj.insert(kJsonKeyPasswordHash, QString::fromLatin1(record.password_hash));
    obj.insert(kJsonKeyKdfSalt,
               QString::fromLatin1(record.kdf_salt.toBase64()));
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

std::optional<MasterRecord> DeserializeRecord(const QByteArray& json) {
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    const auto obj = doc.object();
    const auto hash_v = obj.value(kJsonKeyPasswordHash);
    const auto salt_v = obj.value(kJsonKeyKdfSalt);
    if (!hash_v.isString() || !salt_v.isString()) {
        return std::nullopt;
    }
    MasterRecord record;
    record.password_hash = hash_v.toString().toLatin1();
    record.kdf_salt =
        QByteArray::fromBase64(salt_v.toString().toLatin1(),
                               QByteArray::Base64Encoding);
    if (record.password_hash.size() !=
            MasterPasswordStore::kPasswordHashHexLen ||
        record.kdf_salt.size() != MasterPasswordStore::kKdfSaltSize) {
        return std::nullopt;
    }
    return record;
}

bool EnsureParentDir(const QString& file_path) {
    const QFileInfo info(file_path);
    const QDir parent = info.dir();
    if (parent.exists()) return true;
    return parent.mkpath(QStringLiteral("."));
}

}  // namespace

MasterPasswordStore::MasterPasswordStore() : file_path_(DefaultFilePath()) {}

MasterPasswordStore::MasterPasswordStore(QString file_path)
    : file_path_(std::move(file_path)) {}

QString MasterPasswordStore::DefaultFilePath() {
    const QString base = QCoreApplication::applicationDirPath();
    return base + QStringLiteral("/data/master.dat");
}

bool MasterPasswordStore::Exists() const {
    return QFile::exists(file_path_);
}

std::optional<MasterRecord> MasterPasswordStore::Load() const {
    QFile file(file_path_);
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    const QByteArray blob = file.readAll();
    file.close();

    auto plaintext = UnprotectData(blob);
    if (!plaintext.has_value()) return std::nullopt;

    auto record = DeserializeRecord(*plaintext);
    plaintext->fill('\0');
    return record;
}

bool MasterPasswordStore::Save(const MasterRecord& record) const {
    if (record.password_hash.size() != kPasswordHashHexLen) return false;
    if (record.kdf_salt.size() != kKdfSaltSize) return false;

    if (!EnsureParentDir(file_path_)) return false;

    QByteArray plaintext = SerializeRecord(record);
    auto ciphertext = ProtectData(plaintext);
    plaintext.fill('\0');
    if (!ciphertext.has_value()) return false;

    QSaveFile out(file_path_);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    if (out.write(*ciphertext) != ciphertext->size()) {
        out.cancelWriting();
        return false;
    }
    return out.commit();
}

bool MasterPasswordStore::Clear() const {
    if (!QFile::exists(file_path_)) return true;
    return QFile::remove(file_path_);
}

}  // namespace passvault::master_password
