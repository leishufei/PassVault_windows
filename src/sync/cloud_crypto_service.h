#pragma once

#include <QByteArray>
#include <optional>
#include <string_view>

#include "model/cloud_backup_format.h"

namespace passvault::sync {

class CloudCryptoService {
 public:
    static std::optional<model::CloudBackupFormat> EncryptForCloud(
        const QByteArray& payload_json,
        std::string_view master_password);

    static std::optional<QByteArray> DecryptFromCloud(
        const model::CloudBackupFormat& backup,
        std::string_view master_password);
};

}  // namespace passvault::sync
