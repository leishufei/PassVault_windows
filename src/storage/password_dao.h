#pragma once

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

#include "model/password_entry.h"

namespace passvault::storage {

class Database;

struct PasswordDeletionMarker {
    QString uuid;
    std::int64_t deleted_at = 0;
};

class PasswordDao {
 public:
    explicit PasswordDao(Database& db);

    Database& database() { return db_; }

    std::vector<model::PasswordEntry> ListActive();
    std::vector<model::PasswordEntry> ListIncludingDeleted();
    std::optional<std::vector<model::PasswordEntry>> ReadAll();
    std::vector<model::PasswordEntry> ListByCategory(std::int64_t category_id);
    std::vector<std::int64_t> ListIdsByCategory(std::int64_t category_id);

    std::optional<model::PasswordEntry> FindById(std::int64_t id);
    std::optional<model::PasswordEntry> FindByUuid(const QString& uuid);

    std::vector<model::PasswordEntry> Search(const QString& query);
    std::vector<model::PasswordEntry> FindByWebsite(const QString& website);
    std::vector<model::PasswordEntry> FindByPackageName(
        const QString& package_name);

    std::optional<std::int64_t> Insert(const model::PasswordEntry& entry);
    bool InsertMany(const std::vector<model::PasswordEntry>& entries);
    bool Update(const model::PasswordEntry& entry);
    bool SetFavorite(std::int64_t id, bool is_favorite);
    bool HardDelete(std::int64_t id);
    bool LogicalDelete(std::int64_t id, std::int64_t timestamp);
    bool LogicalDeleteByCategory(std::int64_t category_id,
                                 std::int64_t timestamp);
    bool DeleteAll();

    int CountDuplicate(const QString& title, const QString& username,
                       std::int64_t exclude_id);

    bool MigrateCategory(std::int64_t old_category_id,
                         std::int64_t new_category_id);
    bool BulkMigrateCategory(std::int64_t old_category_id,
                             std::int64_t new_category_id,
                             std::int64_t timestamp);
    bool BulkLogicalDelete(const std::vector<std::int64_t>& ids,
                           std::int64_t timestamp);
    bool BulkUpdateCategoryByIds(const std::vector<std::int64_t>& ids,
                                 std::int64_t new_category_id,
                                 std::int64_t timestamp);
    bool UpdateCategoryOf(std::int64_t id, std::int64_t new_category_id,
                          std::int64_t timestamp);

    std::optional<std::vector<std::int64_t>> ListTrashIds();
    std::optional<int> BulkRestore(const std::vector<std::int64_t>& ids,
                                   std::int64_t timestamp);
    std::optional<int> BulkPermanentlyDelete(
        const std::vector<std::int64_t>& ids, std::int64_t timestamp);
    std::optional<int> EmptyTrash(std::int64_t timestamp);
    std::optional<std::vector<PasswordDeletionMarker>> ListDeletionMarkers();
    bool ApplyDeletionMarker(const QString& uuid, std::int64_t timestamp);
    bool ApplySyncedEntry(const model::PasswordEntry& entry);

    bool DeleteOldTombstones(std::int64_t cutoff);

 private:
    Database& db_;
};

}  // namespace passvault::storage
