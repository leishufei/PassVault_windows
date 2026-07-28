#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>

namespace passvault::model {

struct PasswordEntry {
    std::int64_t id = 0;
    QString uuid;
    QString title;
    QString username;
    QByteArray encrypted_password;
    QByteArray password_iv;
    QString website;
    QString app_package_name;
    QString notes;
    bool is_favorite = false;
    std::int32_t icon_color = 0;
    std::int32_t strength = 0;
    std::int64_t category_id = 0;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
    bool is_deleted = false;
};

}  // namespace passvault::model
