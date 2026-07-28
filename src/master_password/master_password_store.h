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
    bool Clear() const;

 private:
    static QString DefaultFilePath();

    QString file_path_;
};

}  // namespace passvault::master_password
