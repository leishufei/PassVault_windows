#pragma once

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

#include "model/category.h"

namespace passvault::storage {

class Database;

class CategoryDao {
 public:
    explicit CategoryDao(Database& db);

    std::vector<model::Category> ListActive();
    std::vector<model::Category> ListIncludingDeleted();

    std::optional<model::Category> FindById(std::int64_t id);
    std::optional<model::Category> FindByUuid(const QString& uuid);

    std::optional<std::int64_t> Insert(const model::Category& category);
    bool Update(const model::Category& category);
    bool HardDelete(std::int64_t id);
    bool DeleteAllCustom();

    int CountActive();

    bool UpdateSortOrder(std::int64_t id, int sort_order,
                         std::int64_t timestamp);
    bool LogicalDelete(std::int64_t id, std::int64_t timestamp);
    bool DeleteOldTombstones(std::int64_t cutoff);

 private:
    Database& db_;
};

}  // namespace passvault::storage
