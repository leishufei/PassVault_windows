#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace passvault::master_password {

struct MasterRecord {
    QByteArray password_hash;
    QByteArray kdf_salt;
};

class MasterPasswordStore {
 public:
    static constexpr int kKdfSaltSize = 16;
    static constexpr int kPasswordHashHexLen = 64;

    MasterPasswordStore();
    explicit MasterPasswordStore(QString file_path);

    const QString& file_path() const { return file_path_; }

    bool Exists() const;
    std::optional<MasterRecord> Load() const;
    bool Save(const MasterRecord& record) const;
    static std::optional<QByteArray> Encode(const MasterRecord& record);
    static std::optional<QByteArray> Protect(const QByteArray& bytes);
    static std::optional<QByteArray> Unprotect(const QByteArray& bytes);
    bool Clear() const;

    // Raw DPAPI blob, used to snapshot and restore the record around a
    // multi-step master password change. The blob is machine-bound, so a
    // byte-level copy is a complete rollback point.
    std::optional<QByteArray> LoadRaw() const;
    bool SaveRaw(const QByteArray& blob) const;

 private:
    static QString DefaultFilePath();

    QString file_path_;
};

}  // namespace passvault::master_password
