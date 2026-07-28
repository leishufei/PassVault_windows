#include "sync/sync_manager.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "crypto/crypto_service.h"
#include "crypto/random.h"
#include "crypto/secure_bytes.h"
#include "crypto/session_key.h"
#include "generator/password_strength.h"
#include "model/category.h"
#include "model/cloud_backup_format.h"
#include "model/password_entry.h"
#include "model/sync_payload_v2.h"
#include "session/session_manager.h"
#include "storage/category_dao.h"
#include "storage/password_dao.h"
#include "sync/cloud_crypto_service.h"
#include "sync/cloud_storage_provider.h"
#include "sync/merge_algorithm.h"

namespace passvault::sync {

namespace {

using model::Category;
using model::CategorySyncItemV2;
using model::CloudBackupFormat;
using model::PasswordEntry;
using model::PasswordSyncItemV2;
using model::SyncPayloadV2;

CategorySyncItemV2 CategoryToSyncItem(const Category& c) {
    CategorySyncItemV2 out;
    out.uuid = c.uuid;
    out.name = c.name;
    out.color = c.color;
    out.sort_order = c.sort_order;
    out.created_at = c.created_at;
    out.updated_at = c.updated_at;
    out.is_deleted = c.is_deleted;
    return out;
}

std::optional<QString> DecryptLocalPassword(const PasswordEntry& e,
                                            const crypto::SessionKey& key) {
    if (e.encrypted_password.isEmpty() && e.password_iv.isEmpty()) {
        return QString{};
    }
    if (static_cast<std::size_t>(e.password_iv.size()) !=
            crypto::CryptoService::kIvSize ||
        static_cast<std::size_t>(e.encrypted_password.size()) <
            crypto::CryptoService::kTagSize) {
        return std::nullopt;
    }
    auto plain = crypto::CryptoService::DecryptGcm(
        key.data(), key.size(),
        reinterpret_cast<const std::uint8_t*>(e.password_iv.constData()),
        static_cast<std::size_t>(e.password_iv.size()),
        reinterpret_cast<const std::uint8_t*>(e.encrypted_password.constData()),
        static_cast<std::size_t>(e.encrypted_password.size()));
    if (!plain) return std::nullopt;
    return QString::fromUtf8(*plain);
}

struct EncryptedField {
    QByteArray ciphertext;
    QByteArray iv;
};

EncryptedField EncryptLocalPassword(const QString& plain,
                                    const crypto::SessionKey& key) {
    const QByteArray plain_bytes = plain.toUtf8();
    const auto iv_bytes = crypto::Random::Bytes(crypto::CryptoService::kIvSize);
    const QByteArray ct = crypto::CryptoService::EncryptGcm(
        key.data(), key.size(), iv_bytes.data(), iv_bytes.size(),
        reinterpret_cast<const std::uint8_t*>(plain_bytes.constData()),
        static_cast<std::size_t>(plain_bytes.size()));
    return {ct, QByteArray(reinterpret_cast<const char*>(iv_bytes.data()),
                           static_cast<int>(iv_bytes.size()))};
}

std::optional<PasswordSyncItemV2> PasswordEntryToSyncItem(
    const PasswordEntry& e, const QHash<std::int64_t, QString>& cat_id_to_uuid,
    const crypto::SessionKey& key) {
    auto password = DecryptLocalPassword(e, key);
    if (!password) return std::nullopt;
    PasswordSyncItemV2 out;
    out.uuid = e.uuid;
    out.title = e.title;
    out.username = e.username;
    out.password = *password;
    out.website = e.website;
    out.notes = e.notes;
    out.category_uuid = cat_id_to_uuid.value(e.category_id, QString{});
    out.is_favorite = e.is_favorite;
    out.icon_color = e.icon_color;
    out.created_at = e.created_at;
    out.updated_at = e.updated_at;
    out.is_deleted = e.is_deleted;
    return out;
}

}  // namespace

SyncManager::SyncManager(storage::PasswordDao* pwd_dao,
                         storage::CategoryDao* cat_dao,
                         session::SessionManager* session, QObject* parent)
    : QObject(parent),
      pwd_dao_(pwd_dao),
      cat_dao_(cat_dao),
      session_(session) {}

SyncManager::Result SyncManager::PerformSync(const QString& remote_name,
                                              int max_retries) {
    emit SyncStarted();
    Result final_result;
    for (int i = 0; i < max_retries; ++i) {
        DoSyncOutcome outcome = DoSyncOnce(remote_name);
        if (outcome.status == DoSyncStatus::kSuccess) {
            final_result = {true, QString{}};
            emit SyncFinished(true, QString{});
            return final_result;
        }
        if (outcome.status == DoSyncStatus::kFatal) {
            final_result = {false, outcome.message};
            emit SyncFinished(false, outcome.message);
            return final_result;
        }
        // kVersionMismatch: fall through to retry
    }
    const QString msg =
        QStringLiteral("同步失败：多次重试后仍存在并发冲突");
    final_result = {false, msg};
    emit SyncFinished(false, msg);
    return final_result;
}

SyncManager::DoSyncOutcome SyncManager::DoSyncOnce(const QString& remote_name) {
    if (!provider_) {
        return {DoSyncStatus::kFatal, QStringLiteral("未配置云端 Provider")};
    }
    if (!session_ || !session_->IsUnlocked()) {
        return {DoSyncStatus::kFatal, QStringLiteral("会话已锁定")};
    }
    const crypto::SessionKey* session_key = session_->session_key();
    const crypto::SecureBytes* master_pwd = session_->master_password();
    if (!session_key || !master_pwd || master_pwd->empty()) {
        return {DoSyncStatus::kFatal, QStringLiteral("会话密钥不可用")};
    }
    const std::string_view master_pwd_view(
        reinterpret_cast<const char*>(master_pwd->data()), master_pwd->size());

    // 1. Local snapshot
    auto local_passwords = pwd_dao_->ListIncludingDeleted();
    auto local_categories = cat_dao_->ListIncludingDeleted();

    QHash<std::int64_t, QString> cat_id_to_uuid;
    cat_id_to_uuid.reserve(local_categories.size() + 1);
    for (const auto& c : local_categories) {
        cat_id_to_uuid.insert(c.id, c.uuid);
    }
    cat_id_to_uuid.insert(0, QString{});

    // 2. Download + decrypt cloud
    std::optional<SyncPayloadV2> cloud_payload;
    QString cloud_version;
    {
        QString err;
        auto download =
            provider_->DownloadBackupWithVersion(remote_name, &err);
        if (download) {
            cloud_version = download->version;
            auto backup = CloudBackupFormat::FromJsonBytes(download->data);
            if (!backup) {
                return {DoSyncStatus::kFatal,
                        QStringLiteral("云端备份 JSON 解析失败")};
            }
            auto plain =
                CloudCryptoService::DecryptFromCloud(*backup, master_pwd_view);
            if (!plain) {
                return {DoSyncStatus::kFatal,
                        QStringLiteral("云端解密失败：主密码不匹配或数据损坏")};
            }
            auto parsed = SyncPayloadV2::FromJsonBytes(*plain);
            if (!parsed || parsed->version != SyncPayloadV2::kVersion) {
                return {DoSyncStatus::kFatal,
                        QStringLiteral(
                            "云端为 V1 旧格式，请先在 Android 端同步升级到 V2")};
            }
            cloud_payload = std::move(parsed);
        } else {
            // No cloud file or transient network error: treat as first sync
            // and take the empty-cloud upload path.
            cloud_version.clear();
        }
    }

    // 3. Merge categories
    QHash<QString, CategorySyncItemV2> local_cat_map;
    local_cat_map.reserve(local_categories.size());
    for (const auto& c : local_categories) {
        local_cat_map.insert(c.uuid, CategoryToSyncItem(c));
    }
    QHash<QString, CategorySyncItemV2> cloud_cat_map;
    if (cloud_payload) {
        cloud_cat_map.reserve(cloud_payload->categories.size());
        for (const auto& c : cloud_payload->categories) {
            cloud_cat_map.insert(c.uuid, c);
        }
    }
    auto cat_merge = MergeByUuid(
        local_cat_map, cloud_cat_map,
        [](const CategorySyncItemV2& x) { return x.updated_at; });
    if (!cloud_payload) {
        cat_merge.has_local_changes = false;
    }

    // 4. Apply merged categories
    for (const auto& item : cat_merge.merged) {
        auto existing = cat_dao_->FindByUuid(item.uuid);
        Category c;
        c.id = existing ? existing->id : 0;
        c.uuid = item.uuid;
        c.name = item.name;
        c.color = item.color;
        c.is_default = existing ? existing->is_default : false;
        c.sort_order = item.sort_order;
        c.created_at = item.created_at;
        c.updated_at = item.updated_at;
        c.is_deleted = item.is_deleted;
        cat_dao_->Insert(c);
    }

    // Reload categories, rebuild uuid -> id map
    auto updated_categories = cat_dao_->ListIncludingDeleted();
    QHash<QString, std::int64_t> cat_uuid_to_id;
    cat_uuid_to_id.reserve(updated_categories.size() + 1);
    for (const auto& c : updated_categories) {
        cat_uuid_to_id.insert(c.uuid, c.id);
    }
    cat_uuid_to_id.insert(QString{}, 0);

    // 5. Build local password sync items using original mapping
    QHash<QString, PasswordSyncItemV2> local_pw_map;
    local_pw_map.reserve(local_passwords.size());
    for (const auto& p : local_passwords) {
        auto item = PasswordEntryToSyncItem(p, cat_id_to_uuid, *session_key);
        if (!item) {
            return {DoSyncStatus::kFatal,
                    QStringLiteral("本地密码解密失败，请检查会话密钥")};
        }
        local_pw_map.insert(item->uuid, std::move(*item));
    }
    QHash<QString, PasswordSyncItemV2> cloud_pw_map;
    if (cloud_payload) {
        cloud_pw_map.reserve(cloud_payload->passwords.size());
        for (const auto& p : cloud_payload->passwords) {
            cloud_pw_map.insert(p.uuid, p);
        }
    }
    auto pw_merge = MergeByUuid(
        local_pw_map, cloud_pw_map,
        [](const PasswordSyncItemV2& x) { return x.updated_at; });
    if (!cloud_payload) {
        pw_merge.has_local_changes = false;
    }

    // 6. Apply merged passwords
    for (const auto& sync_pw : pw_merge.merged) {
        auto existing = pwd_dao_->FindByUuid(sync_pw.uuid);

        if (!existing && !sync_pw.is_deleted &&
            (!sync_pw.title.isEmpty() || !sync_pw.username.isEmpty()) &&
            pwd_dao_->CountDuplicate(sync_pw.title, sync_pw.username, 0) > 0) {
            continue;  // Skip duplicate insert
        }

        EncryptedField enc = EncryptLocalPassword(sync_pw.password, *session_key);

        PasswordEntry entry;
        entry.id = existing ? existing->id : 0;
        entry.uuid = sync_pw.uuid;
        entry.title = sync_pw.title;
        entry.username = sync_pw.username;
        entry.encrypted_password = enc.ciphertext;
        entry.password_iv = enc.iv;
        entry.website = sync_pw.website;
        entry.app_package_name =
            existing ? existing->app_package_name : QString{};
        entry.notes = sync_pw.notes;
        entry.is_favorite = sync_pw.is_favorite;
        entry.icon_color = sync_pw.icon_color;
        entry.strength = generator::CalculatePasswordStrength(sync_pw.password);
        entry.category_id = cat_uuid_to_id.value(sync_pw.category_uuid, 0);
        entry.created_at = sync_pw.created_at;
        entry.updated_at = sync_pw.updated_at;
        entry.is_deleted = sync_pw.is_deleted;
        pwd_dao_->Insert(entry);
    }

    // 7. Dedup categories by name
    const std::int64_t now_ms = QDateTime::currentMSecsSinceEpoch();
    const bool dedup_changed = DedupCategoriesByName(now_ms);

    // 8. Reload final state and rebuild sync items
    auto final_categories = cat_dao_->ListIncludingDeleted();
    auto final_passwords = pwd_dao_->ListIncludingDeleted();
    QHash<std::int64_t, QString> final_cat_id_to_uuid;
    final_cat_id_to_uuid.reserve(final_categories.size() + 1);
    for (const auto& c : final_categories) {
        final_cat_id_to_uuid.insert(c.id, c.uuid);
    }
    final_cat_id_to_uuid.insert(0, QString{});

    std::vector<CategorySyncItemV2> final_cat_items;
    final_cat_items.reserve(final_categories.size());
    for (const auto& c : final_categories) {
        final_cat_items.push_back(CategoryToSyncItem(c));
    }
    std::vector<PasswordSyncItemV2> final_pw_items;
    final_pw_items.reserve(final_passwords.size());
    for (const auto& p : final_passwords) {
        auto item =
            PasswordEntryToSyncItem(p, final_cat_id_to_uuid, *session_key);
        if (!item) {
            return {DoSyncStatus::kFatal,
                    QStringLiteral("最终阶段本地密码解密失败")};
        }
        final_pw_items.push_back(std::move(*item));
    }

    // 9. Upload if needed
    const bool need_upload = cat_merge.has_local_changes ||
                             pw_merge.has_local_changes || dedup_changed ||
                             !cloud_payload;
    if (need_upload) {
        SyncPayloadV2 payload;
        payload.version = SyncPayloadV2::kVersion;
        payload.sync_timestamp = now_ms;
        payload.passwords = std::move(final_pw_items);
        payload.categories = std::move(final_cat_items);

        auto backup = CloudCryptoService::EncryptForCloud(
            payload.ToJsonBytes(), master_pwd_view);
        if (!backup) {
            return {DoSyncStatus::kFatal, QStringLiteral("云端加密失败")};
        }
        const QByteArray file_bytes = backup->ToJsonBytes();

        QString up_err;
        if (!cloud_version.isEmpty()) {
            auto status = provider_->UploadBackupIfMatch(
                file_bytes, remote_name, cloud_version, &up_err);
            if (status == UploadIfMatchStatus::kVersionMismatch) {
                return {DoSyncStatus::kVersionMismatch, up_err};
            }
            if (status != UploadIfMatchStatus::kSuccess) {
                return {DoSyncStatus::kFatal,
                        QStringLiteral("上传失败: ") + up_err};
            }
        } else {
            if (!provider_->UploadBackup(file_bytes, remote_name, &up_err)) {
                return {DoSyncStatus::kFatal,
                        QStringLiteral("上传失败: ") + up_err};
            }
        }
    }

    // 10. Cleanup old tombstones (30 days)
    const std::int64_t cutoff = now_ms - 30LL * 24 * 60 * 60 * 1000;
    pwd_dao_->DeleteOldTombstones(cutoff);
    cat_dao_->DeleteOldTombstones(cutoff);

    return {DoSyncStatus::kSuccess, QString{}};
}

bool SyncManager::DedupCategoriesByName(std::int64_t now_ms) {
    auto all = cat_dao_->ListIncludingDeleted();
    // Group non-deleted, non-default, non-uncategorized by trim+lowercase name
    QHash<QString, std::vector<Category>> groups;
    for (const auto& c : all) {
        if (c.is_deleted || c.is_default || c.id == 0) continue;
        const QString key = c.name.trimmed().toLower();
        groups[key].push_back(c);
    }

    bool changed = false;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        auto& dups = it.value();
        if (dups.size() <= 1) continue;
        auto winner_it = std::min_element(
            dups.begin(), dups.end(),
            [](const Category& a, const Category& b) {
                return a.created_at < b.created_at;
            });
        const std::int64_t winner_id = winner_it->id;
        for (const auto& loser : dups) {
            if (loser.id == winner_id) continue;
            pwd_dao_->BulkMigrateCategory(loser.id, winner_id, now_ms);
            cat_dao_->LogicalDelete(loser.id, now_ms);
            changed = true;
        }
    }
    return changed;
}

SyncManager::Result SyncManager::ChangeCloudMasterPassword(
    const QString& old_password, const QString& new_password,
    const QString& remote_name) {
    if (!provider_) {
        return {false, QStringLiteral("未配置云端 Provider")};
    }
    const QByteArray old_pwd_bytes = old_password.toUtf8();
    const QByteArray new_pwd_bytes = new_password.toUtf8();
    const std::string_view old_view(old_pwd_bytes.constData(),
                                    static_cast<std::size_t>(old_pwd_bytes.size()));
    const std::string_view new_view(new_pwd_bytes.constData(),
                                    static_cast<std::size_t>(new_pwd_bytes.size()));

    for (int i = 0; i < kDefaultMaxRetries; ++i) {
        QString err;
        auto download = provider_->DownloadBackupWithVersion(remote_name, &err);
        if (!download) {
            return {false, QStringLiteral("下载云端备份失败: ") + err};
        }
        auto backup = CloudBackupFormat::FromJsonBytes(download->data);
        if (!backup) {
            return {false, QStringLiteral("云端备份 JSON 解析失败")};
        }
        auto plain = CloudCryptoService::DecryptFromCloud(*backup, old_view);
        if (!plain) {
            return {false,
                    QStringLiteral("旧主密码解密云端失败：主密码不匹配")};
        }
        auto new_backup = CloudCryptoService::EncryptForCloud(*plain, new_view);
        if (!new_backup) {
            return {false, QStringLiteral("新主密码加密云端失败")};
        }
        const QByteArray file_bytes = new_backup->ToJsonBytes();

        QString up_err;
        auto status = provider_->UploadBackupIfMatch(
            file_bytes, remote_name, download->version, &up_err);
        if (status == UploadIfMatchStatus::kSuccess) {
            return {true, QString{}};
        }
        if (status == UploadIfMatchStatus::kError) {
            return {false, QStringLiteral("上传失败: ") + up_err};
        }
        // kVersionMismatch: retry
    }
    return {false, QStringLiteral("改密失败：多次重试后仍存在并发冲突")};
}

}  // namespace passvault::sync
