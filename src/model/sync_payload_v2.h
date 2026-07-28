#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <cstdint>
#include <optional>
#include <vector>

namespace passvault::model {

struct PasswordSyncItemV2 {
    QString uuid;
    QString title;
    QString username;
    QString password;
    QString website;
    QString notes;
    QString category_uuid;
    bool is_favorite = false;
    std::int32_t icon_color = 0;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
    bool is_deleted = false;

    QJsonObject ToJsonObject() const;
    static std::optional<PasswordSyncItemV2> FromJsonObject(const QJsonObject& obj);
};

struct CategorySyncItemV2 {
    QString uuid;
    QString name;
    std::int32_t color = 0;
    std::int32_t sort_order = 0;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
    bool is_deleted = false;

    QJsonObject ToJsonObject() const;
    static std::optional<CategorySyncItemV2> FromJsonObject(const QJsonObject& obj);
};

struct SyncPayloadV2 {
    static constexpr int kVersion = 2;

    int version = kVersion;
    std::int64_t sync_timestamp = 0;
    std::vector<PasswordSyncItemV2> passwords;
    std::vector<CategorySyncItemV2> categories;

    QJsonObject ToJsonObject() const;
    QByteArray ToJsonBytes() const;

    static std::optional<SyncPayloadV2> FromJsonBytes(const QByteArray& bytes);
    static std::optional<SyncPayloadV2> FromJsonObject(const QJsonObject& obj);
};

}  // namespace passvault::model
