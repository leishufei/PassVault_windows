#include "model/sync_payload_v2.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace passvault::model {

namespace {

std::int64_t ReadInt64(const QJsonValue& v) {
    if (v.isDouble()) {
        return static_cast<std::int64_t>(v.toDouble());
    }
    if (v.isString()) {
        bool ok = false;
        const auto n = v.toString().toLongLong(&ok);
        return ok ? n : 0;
    }
    return 0;
}

}  // namespace

QJsonObject PasswordSyncItemV2::ToJsonObject() const {
    QJsonObject obj;
    obj.insert("uuid", uuid);
    obj.insert("title", title);
    obj.insert("username", username);
    obj.insert("password", password);
    obj.insert("website", website);
    obj.insert("notes", notes);
    obj.insert("categoryUuid", category_uuid);
    obj.insert("isFavorite", is_favorite);
    obj.insert("iconColor", icon_color);
    obj.insert("createdAt", static_cast<double>(created_at));
    obj.insert("updatedAt", static_cast<double>(updated_at));
    obj.insert("isDeleted", is_deleted);
    return obj;
}

std::optional<PasswordSyncItemV2> PasswordSyncItemV2::FromJsonObject(const QJsonObject& obj) {
    PasswordSyncItemV2 out;
    out.uuid = obj.value("uuid").toString();
    if (out.uuid.isEmpty()) return std::nullopt;
    out.title = obj.value("title").toString();
    out.username = obj.value("username").toString();
    out.password = obj.value("password").toString();
    out.website = obj.value("website").toString();
    out.notes = obj.value("notes").toString();
    out.category_uuid = obj.value("categoryUuid").toString();
    out.is_favorite = obj.value("isFavorite").toBool(false);
    out.icon_color = obj.value("iconColor").toInt(0);
    out.created_at = ReadInt64(obj.value("createdAt"));
    out.updated_at = ReadInt64(obj.value("updatedAt"));
    out.is_deleted = obj.value("isDeleted").toBool(false);
    return out;
}

QJsonObject CategorySyncItemV2::ToJsonObject() const {
    QJsonObject obj;
    obj.insert("uuid", uuid);
    obj.insert("name", name);
    obj.insert("color", color);
    obj.insert("sortOrder", sort_order);
    obj.insert("createdAt", static_cast<double>(created_at));
    obj.insert("updatedAt", static_cast<double>(updated_at));
    obj.insert("isDeleted", is_deleted);
    return obj;
}

std::optional<CategorySyncItemV2> CategorySyncItemV2::FromJsonObject(const QJsonObject& obj) {
    CategorySyncItemV2 out;
    out.uuid = obj.value("uuid").toString();
    if (out.uuid.isEmpty()) return std::nullopt;
    out.name = obj.value("name").toString();
    out.color = obj.value("color").toInt(0);
    out.sort_order = obj.value("sortOrder").toInt(0);
    out.created_at = ReadInt64(obj.value("createdAt"));
    out.updated_at = ReadInt64(obj.value("updatedAt"));
    out.is_deleted = obj.value("isDeleted").toBool(false);
    return out;
}

QJsonObject SyncPayloadV2::ToJsonObject() const {
    QJsonArray password_arr;
    for (const auto& p : passwords) {
        password_arr.append(p.ToJsonObject());
    }
    QJsonArray category_arr;
    for (const auto& c : categories) {
        category_arr.append(c.ToJsonObject());
    }
    QJsonObject obj;
    obj.insert("version", version);
    obj.insert("syncTimestamp", static_cast<double>(sync_timestamp));
    obj.insert("passwords", password_arr);
    obj.insert("categories", category_arr);
    return obj;
}

QByteArray SyncPayloadV2::ToJsonBytes() const {
    return QJsonDocument(ToJsonObject()).toJson(QJsonDocument::Compact);
}

std::optional<SyncPayloadV2> SyncPayloadV2::FromJsonBytes(const QByteArray& bytes) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    return FromJsonObject(doc.object());
}

std::optional<SyncPayloadV2> SyncPayloadV2::FromJsonObject(const QJsonObject& obj) {
    SyncPayloadV2 out;
    out.version = obj.value("version").toInt(kVersion);
    out.sync_timestamp = ReadInt64(obj.value("syncTimestamp"));

    const QJsonArray password_arr = obj.value("passwords").toArray();
    out.passwords.reserve(password_arr.size());
    for (const auto& v : password_arr) {
        auto item = PasswordSyncItemV2::FromJsonObject(v.toObject());
        if (item) out.passwords.push_back(std::move(*item));
    }

    const QJsonArray category_arr = obj.value("categories").toArray();
    out.categories.reserve(category_arr.size());
    for (const auto& v : category_arr) {
        auto item = CategorySyncItemV2::FromJsonObject(v.toObject());
        if (item) out.categories.push_back(std::move(*item));
    }
    return out;
}

}  // namespace passvault::model
