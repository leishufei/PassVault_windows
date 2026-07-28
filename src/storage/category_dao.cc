#include "storage/category_dao.h"

#include <QDebug>

#include <exception>
#include <string>
#include <string_view>

#include "storage/database.h"
#include "storage/statement.h"

namespace passvault::storage {

namespace {

constexpr const char* kSelectAllColumns =
    "id, uuid, name, color, isDefault, sortOrder, "
    "createdAt, updatedAt, isDeleted";

model::Category ReadRow(const Statement& stmt) {
    model::Category c;
    c.id = stmt.ColumnInt64(0);
    c.uuid = stmt.ColumnText(1);
    c.name = stmt.ColumnText(2);
    c.color = stmt.ColumnInt(3);
    c.is_default = stmt.ColumnBool(4);
    c.sort_order = stmt.ColumnInt(5);
    c.created_at = stmt.ColumnInt64(6);
    c.updated_at = stmt.ColumnInt64(7);
    c.is_deleted = stmt.ColumnBool(8);
    return c;
}

void BindFields(Statement& stmt, int start, const model::Category& c) {
    stmt.BindText(start + 0, c.uuid);
    stmt.BindText(start + 1, c.name);
    stmt.BindInt(start + 2, c.color);
    stmt.BindBool(start + 3, c.is_default);
    stmt.BindInt(start + 4, c.sort_order);
    stmt.BindInt64(start + 5, c.created_at);
    stmt.BindInt64(start + 6, c.updated_at);
    stmt.BindBool(start + 7, c.is_deleted);
}

std::vector<model::Category> RunListQuery(Database& db,
                                          std::string_view sql) {
    std::vector<model::Category> out;
    try {
        Statement stmt(db.handle(), sql);
        while (stmt.Step()) {
            out.push_back(ReadRow(stmt));
        }
    } catch (const std::exception& ex) {
        qWarning() << "CategoryDao list query failed:" << ex.what();
        out.clear();
    }
    return out;
}

}  // namespace

CategoryDao::CategoryDao(Database& db) : db_(db) {}

std::vector<model::Category> CategoryDao::ListActive() {
    return RunListQuery(db_, std::string("SELECT ") + kSelectAllColumns +
                                 " FROM categories WHERE isDeleted = 0 "
                                 "ORDER BY sortOrder ASC, createdAt ASC");
}

std::vector<model::Category> CategoryDao::ListIncludingDeleted() {
    return RunListQuery(db_, std::string("SELECT ") + kSelectAllColumns +
                                 " FROM categories");
}

std::optional<model::Category> CategoryDao::FindById(std::int64_t id) {
    try {
        const std::string sql = std::string("SELECT ") + kSelectAllColumns +
                                " FROM categories WHERE id = ?";
        Statement stmt(db_.handle(), sql);
        stmt.BindInt64(1, id);
        if (stmt.Step()) {
            return ReadRow(stmt);
        }
    } catch (const std::exception& ex) {
        qWarning() << "CategoryDao::FindById failed:" << ex.what();
    }
    return std::nullopt;
}

std::optional<model::Category> CategoryDao::FindByUuid(const QString& uuid) {
    try {
        const std::string sql = std::string("SELECT ") + kSelectAllColumns +
                                " FROM categories WHERE uuid = ?";
        Statement stmt(db_.handle(), sql);
        stmt.BindText(1, uuid);
        if (stmt.Step()) {
            return ReadRow(stmt);
        }
    } catch (const std::exception& ex) {
        qWarning() << "CategoryDao::FindByUuid failed:" << ex.what();
    }
    return std::nullopt;
}

std::optional<std::int64_t> CategoryDao::Insert(
    const model::Category& category) {
    try {
        const std::string sql =
            std::string("INSERT OR REPLACE INTO categories (") +
            kSelectAllColumns + ") VALUES (?,?,?,?,?,?,?,?,?)";
        Statement stmt(db_.handle(), sql);
        if (category.id == 0) {
            stmt.BindNull(1);
        } else {
            stmt.BindInt64(1, category.id);
        }
        BindFields(stmt, 2, category);
        stmt.StepDone();
        return db_.LastInsertRowid();
    } catch (const std::exception& ex) {
        qWarning() << "CategoryDao::Insert failed:" << ex.what();
        return std::nullopt;
    }
}

bool CategoryDao::Update(const model::Category& category) {
    try {
        Statement stmt(
            db_.handle(),
            "UPDATE categories SET "
            "uuid=?, name=?, color=?, isDefault=?, sortOrder=?, "
            "createdAt=?, updatedAt=?, isDeleted=? WHERE id=?");
        BindFields(stmt, 1, category);
        stmt.BindInt64(9, category.id);
        return stmt.StepDone() > 0;
    } catch (const std::exception& ex) {
        qWarning() << "CategoryDao::Update failed:" << ex.what();
        return false;
    }
}

bool CategoryDao::HardDelete(std::int64_t id) {
    try {
        Statement stmt(db_.handle(), "DELETE FROM categories WHERE id = ?");
        stmt.BindInt64(1, id);
        return stmt.StepDone() > 0;
    } catch (const std::exception& ex) {
        qWarning() << "CategoryDao::HardDelete failed:" << ex.what();
        return false;
    }
}

bool CategoryDao::DeleteAllCustom() {
    return db_.Execute("DELETE FROM categories WHERE isDefault = 0");
}

int CategoryDao::CountActive() {
    try {
        Statement stmt(db_.handle(),
                       "SELECT COUNT(*) FROM categories WHERE isDeleted = 0");
        if (stmt.Step()) {
            return stmt.ColumnInt(0);
        }
    } catch (const std::exception& ex) {
        qWarning() << "CategoryDao::CountActive failed:" << ex.what();
    }
    return 0;
}

bool CategoryDao::UpdateSortOrder(std::int64_t id, int sort_order,
                                  std::int64_t timestamp) {
    try {
        Statement stmt(
            db_.handle(),
            "UPDATE categories SET sortOrder = ?, updatedAt = ? WHERE id = ?");
        stmt.BindInt(1, sort_order);
        stmt.BindInt64(2, timestamp);
        stmt.BindInt64(3, id);
        return stmt.StepDone() > 0;
    } catch (const std::exception& ex) {
        qWarning() << "CategoryDao::UpdateSortOrder failed:" << ex.what();
        return false;
    }
}

bool CategoryDao::LogicalDelete(std::int64_t id, std::int64_t timestamp) {
    try {
        Statement stmt(
            db_.handle(),
            "UPDATE categories SET isDeleted = 1, updatedAt = ? WHERE id = ?");
        stmt.BindInt64(1, timestamp);
        stmt.BindInt64(2, id);
        return stmt.StepDone() > 0;
    } catch (const std::exception& ex) {
        qWarning() << "CategoryDao::LogicalDelete failed:" << ex.what();
        return false;
    }
}

bool CategoryDao::DeleteOldTombstones(std::int64_t cutoff) {
    try {
        Statement stmt(
            db_.handle(),
            "DELETE FROM categories WHERE isDeleted = 1 AND updatedAt < ?");
        stmt.BindInt64(1, cutoff);
        stmt.StepDone();
        return true;
    } catch (const std::exception& ex) {
        qWarning() << "CategoryDao::DeleteOldTombstones failed:" << ex.what();
        return false;
    }
}

}  // namespace passvault::storage
