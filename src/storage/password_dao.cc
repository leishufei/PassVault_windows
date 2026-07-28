#include "storage/password_dao.h"

#include <QDebug>

#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "storage/database.h"
#include "storage/statement.h"

namespace passvault::storage {

namespace {

constexpr const char* kSelectAllColumns =
    "id, uuid, title, username, encryptedPassword, passwordIv, "
    "website, appPackageName, notes, isFavorite, iconColor, strength, "
    "categoryId, createdAt, updatedAt, isDeleted";

model::PasswordEntry ReadRow(const Statement& stmt) {
    model::PasswordEntry e;
    e.id = stmt.ColumnInt64(0);
    e.uuid = stmt.ColumnText(1);
    e.title = stmt.ColumnText(2);
    e.username = stmt.ColumnText(3);
    e.encrypted_password = stmt.ColumnBlob(4);
    e.password_iv = stmt.ColumnBlob(5);
    e.website = stmt.ColumnText(6);
    e.app_package_name = stmt.ColumnText(7);
    e.notes = stmt.ColumnText(8);
    e.is_favorite = stmt.ColumnBool(9);
    e.icon_color = stmt.ColumnInt(10);
    e.strength = stmt.ColumnInt(11);
    e.category_id = stmt.ColumnInt64(12);
    e.created_at = stmt.ColumnInt64(13);
    e.updated_at = stmt.ColumnInt64(14);
    e.is_deleted = stmt.ColumnBool(15);
    return e;
}

void BindFields(Statement& stmt, int start, const model::PasswordEntry& e) {
    stmt.BindText(start + 0, e.uuid);
    stmt.BindText(start + 1, e.title);
    stmt.BindText(start + 2, e.username);
    stmt.BindBlob(start + 3, e.encrypted_password);
    stmt.BindBlob(start + 4, e.password_iv);
    stmt.BindText(start + 5, e.website);
    stmt.BindText(start + 6, e.app_package_name);
    stmt.BindText(start + 7, e.notes);
    stmt.BindBool(start + 8, e.is_favorite);
    stmt.BindInt(start + 9, e.icon_color);
    stmt.BindInt(start + 10, e.strength);
    stmt.BindInt64(start + 11, e.category_id);
    stmt.BindInt64(start + 12, e.created_at);
    stmt.BindInt64(start + 13, e.updated_at);
    stmt.BindBool(start + 14, e.is_deleted);
}

std::int64_t InsertOne(Database& db, const model::PasswordEntry& entry) {
    const std::string sql =
        std::string("INSERT OR REPLACE INTO password_entries (") +
        kSelectAllColumns + ") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    Statement stmt(db.handle(), sql);
    if (entry.id == 0) {
        stmt.BindNull(1);
    } else {
        stmt.BindInt64(1, entry.id);
    }
    BindFields(stmt, 2, entry);
    stmt.StepDone();
    return db.LastInsertRowid();
}

std::string BuildInPlaceholders(std::size_t n) {
    std::string s;
    s.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        s += (i == 0) ? "?" : ",?";
    }
    return s;
}

std::vector<model::PasswordEntry> RunListQuery(Database& db,
                                               std::string_view sql) {
    std::vector<model::PasswordEntry> out;
    try {
        Statement stmt(db.handle(), sql);
        while (stmt.Step()) {
            out.push_back(ReadRow(stmt));
        }
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao list query failed:" << ex.what();
        out.clear();
    }
    return out;
}

}  // namespace

PasswordDao::PasswordDao(Database& db) : db_(db) {}

std::vector<model::PasswordEntry> PasswordDao::ListActive() {
    return RunListQuery(
        db_,
        std::string("SELECT ") + kSelectAllColumns +
            " FROM password_entries WHERE isDeleted = 0 "
            "ORDER BY createdAt DESC");
}

std::vector<model::PasswordEntry> PasswordDao::ListIncludingDeleted() {
    return RunListQuery(db_, std::string("SELECT ") + kSelectAllColumns +
                                 " FROM password_entries");
}

std::vector<model::PasswordEntry> PasswordDao::ListByCategory(
    std::int64_t category_id) {
    std::vector<model::PasswordEntry> out;
    try {
        const std::string sql =
            std::string("SELECT ") + kSelectAllColumns +
            " FROM password_entries WHERE categoryId = ? AND isDeleted = 0 "
            "ORDER BY createdAt DESC";
        Statement stmt(db_.handle(), sql);
        stmt.BindInt64(1, category_id);
        while (stmt.Step()) {
            out.push_back(ReadRow(stmt));
        }
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::ListByCategory failed:" << ex.what();
        out.clear();
    }
    return out;
}

std::vector<std::int64_t> PasswordDao::ListIdsByCategory(
    std::int64_t category_id) {
    std::vector<std::int64_t> out;
    try {
        Statement stmt(
            db_.handle(),
            "SELECT id FROM password_entries "
            "WHERE categoryId = ? AND isDeleted = 0");
        stmt.BindInt64(1, category_id);
        while (stmt.Step()) {
            out.push_back(stmt.ColumnInt64(0));
        }
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::ListIdsByCategory failed:" << ex.what();
        out.clear();
    }
    return out;
}

std::optional<model::PasswordEntry> PasswordDao::FindById(std::int64_t id) {
    try {
        const std::string sql = std::string("SELECT ") + kSelectAllColumns +
                                " FROM password_entries WHERE id = ?";
        Statement stmt(db_.handle(), sql);
        stmt.BindInt64(1, id);
        if (stmt.Step()) {
            return ReadRow(stmt);
        }
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::FindById failed:" << ex.what();
    }
    return std::nullopt;
}

std::optional<model::PasswordEntry> PasswordDao::FindByUuid(
    const QString& uuid) {
    try {
        const std::string sql = std::string("SELECT ") + kSelectAllColumns +
                                " FROM password_entries WHERE uuid = ?";
        Statement stmt(db_.handle(), sql);
        stmt.BindText(1, uuid);
        if (stmt.Step()) {
            return ReadRow(stmt);
        }
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::FindByUuid failed:" << ex.what();
    }
    return std::nullopt;
}

std::vector<model::PasswordEntry> PasswordDao::Search(const QString& query) {
    std::vector<model::PasswordEntry> out;
    try {
        const std::string sql =
            std::string("SELECT ") + kSelectAllColumns +
            " FROM password_entries WHERE isDeleted = 0 AND ("
            "title LIKE ? OR username LIKE ? OR website LIKE ?)";
        Statement stmt(db_.handle(), sql);
        const QString like = QStringLiteral("%") + query + QStringLiteral("%");
        stmt.BindText(1, like);
        stmt.BindText(2, like);
        stmt.BindText(3, like);
        while (stmt.Step()) {
            out.push_back(ReadRow(stmt));
        }
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::Search failed:" << ex.what();
        out.clear();
    }
    return out;
}

std::vector<model::PasswordEntry> PasswordDao::FindByWebsite(
    const QString& website) {
    std::vector<model::PasswordEntry> out;
    try {
        const std::string sql =
            std::string("SELECT ") + kSelectAllColumns +
            " FROM password_entries WHERE isDeleted = 0 AND website = ? "
            "ORDER BY updatedAt DESC";
        Statement stmt(db_.handle(), sql);
        stmt.BindText(1, website);
        while (stmt.Step()) {
            out.push_back(ReadRow(stmt));
        }
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::FindByWebsite failed:" << ex.what();
        out.clear();
    }
    return out;
}

std::vector<model::PasswordEntry> PasswordDao::FindByPackageName(
    const QString& package_name) {
    std::vector<model::PasswordEntry> out;
    try {
        const std::string sql =
            std::string("SELECT ") + kSelectAllColumns +
            " FROM password_entries WHERE isDeleted = 0 AND "
            "appPackageName = ? ORDER BY updatedAt DESC";
        Statement stmt(db_.handle(), sql);
        stmt.BindText(1, package_name);
        while (stmt.Step()) {
            out.push_back(ReadRow(stmt));
        }
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::FindByPackageName failed:" << ex.what();
        out.clear();
    }
    return out;
}

std::optional<std::int64_t> PasswordDao::Insert(
    const model::PasswordEntry& entry) {
    try {
        return InsertOne(db_, entry);
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::Insert failed:" << ex.what();
        return std::nullopt;
    }
}

bool PasswordDao::InsertMany(
    const std::vector<model::PasswordEntry>& entries) {
    if (entries.empty()) {
        return true;
    }
    try {
        Transaction tx(db_);
        for (const auto& e : entries) {
            InsertOne(db_, e);
        }
        return tx.Commit();
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::InsertMany failed:" << ex.what();
        return false;
    }
}

bool PasswordDao::Update(const model::PasswordEntry& entry) {
    try {
        Statement stmt(
            db_.handle(),
            "UPDATE password_entries SET "
            "uuid=?, title=?, username=?, encryptedPassword=?, passwordIv=?, "
            "website=?, appPackageName=?, notes=?, isFavorite=?, iconColor=?, "
            "strength=?, categoryId=?, createdAt=?, updatedAt=?, isDeleted=? "
            "WHERE id=?");
        BindFields(stmt, 1, entry);
        stmt.BindInt64(16, entry.id);
        return stmt.StepDone() > 0;
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::Update failed:" << ex.what();
        return false;
    }
}

bool PasswordDao::SetFavorite(std::int64_t id, bool is_favorite) {
    try {
        Statement stmt(
            db_.handle(),
            "UPDATE password_entries SET isFavorite = ? WHERE id = ?");
        stmt.BindBool(1, is_favorite);
        stmt.BindInt64(2, id);
        return stmt.StepDone() > 0;
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::SetFavorite failed:" << ex.what();
        return false;
    }
}

bool PasswordDao::HardDelete(std::int64_t id) {
    try {
        Statement stmt(db_.handle(),
                       "DELETE FROM password_entries WHERE id = ?");
        stmt.BindInt64(1, id);
        return stmt.StepDone() > 0;
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::HardDelete failed:" << ex.what();
        return false;
    }
}

bool PasswordDao::LogicalDelete(std::int64_t id, std::int64_t timestamp) {
    try {
        Statement stmt(
            db_.handle(),
            "UPDATE password_entries SET isDeleted = 1, updatedAt = ? "
            "WHERE id = ?");
        stmt.BindInt64(1, timestamp);
        stmt.BindInt64(2, id);
        return stmt.StepDone() > 0;
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::LogicalDelete failed:" << ex.what();
        return false;
    }
}

bool PasswordDao::DeleteAll() {
    return db_.Execute("DELETE FROM password_entries");
}

int PasswordDao::CountDuplicate(const QString& title, const QString& username,
                                std::int64_t exclude_id) {
    try {
        Statement stmt(
            db_.handle(),
            "SELECT COUNT(*) FROM password_entries WHERE "
            "title = ? AND username = ? AND (title != '' OR username != '') "
            "AND isDeleted = 0 AND id != ?");
        stmt.BindText(1, title);
        stmt.BindText(2, username);
        stmt.BindInt64(3, exclude_id);
        if (stmt.Step()) {
            return stmt.ColumnInt(0);
        }
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::CountDuplicate failed:" << ex.what();
    }
    return 0;
}

bool PasswordDao::MigrateCategory(std::int64_t old_category_id,
                                  std::int64_t new_category_id) {
    try {
        Statement stmt(
            db_.handle(),
            "UPDATE password_entries SET categoryId = ? WHERE categoryId = ?");
        stmt.BindInt64(1, new_category_id);
        stmt.BindInt64(2, old_category_id);
        stmt.StepDone();
        return true;
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::MigrateCategory failed:" << ex.what();
        return false;
    }
}

bool PasswordDao::BulkMigrateCategory(std::int64_t old_category_id,
                                      std::int64_t new_category_id,
                                      std::int64_t timestamp) {
    try {
        Statement stmt(
            db_.handle(),
            "UPDATE password_entries SET categoryId = ?, updatedAt = ? "
            "WHERE categoryId = ? AND isDeleted = 0");
        stmt.BindInt64(1, new_category_id);
        stmt.BindInt64(2, timestamp);
        stmt.BindInt64(3, old_category_id);
        stmt.StepDone();
        return true;
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::BulkMigrateCategory failed:" << ex.what();
        return false;
    }
}

bool PasswordDao::BulkLogicalDelete(const std::vector<std::int64_t>& ids,
                                    std::int64_t timestamp) {
    if (ids.empty()) {
        return true;
    }
    try {
        const std::string sql =
            "UPDATE password_entries SET isDeleted = 1, updatedAt = ? "
            "WHERE id IN (" +
            BuildInPlaceholders(ids.size()) + ")";
        Statement stmt(db_.handle(), sql);
        stmt.BindInt64(1, timestamp);
        for (std::size_t i = 0; i < ids.size(); ++i) {
            stmt.BindInt64(static_cast<int>(i + 2), ids[i]);
        }
        stmt.StepDone();
        return true;
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::BulkLogicalDelete failed:" << ex.what();
        return false;
    }
}

bool PasswordDao::BulkUpdateCategoryByIds(
    const std::vector<std::int64_t>& ids, std::int64_t new_category_id,
    std::int64_t timestamp) {
    if (ids.empty()) {
        return true;
    }
    try {
        const std::string sql =
            "UPDATE password_entries SET categoryId = ?, updatedAt = ? "
            "WHERE id IN (" +
            BuildInPlaceholders(ids.size()) + ")";
        Statement stmt(db_.handle(), sql);
        stmt.BindInt64(1, new_category_id);
        stmt.BindInt64(2, timestamp);
        for (std::size_t i = 0; i < ids.size(); ++i) {
            stmt.BindInt64(static_cast<int>(i + 3), ids[i]);
        }
        stmt.StepDone();
        return true;
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::BulkUpdateCategoryByIds failed:"
                   << ex.what();
        return false;
    }
}

bool PasswordDao::UpdateCategoryOf(std::int64_t id,
                                   std::int64_t new_category_id,
                                   std::int64_t timestamp) {
    try {
        Statement stmt(
            db_.handle(),
            "UPDATE password_entries SET categoryId = ?, updatedAt = ? "
            "WHERE id = ?");
        stmt.BindInt64(1, new_category_id);
        stmt.BindInt64(2, timestamp);
        stmt.BindInt64(3, id);
        return stmt.StepDone() > 0;
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::UpdateCategoryOf failed:" << ex.what();
        return false;
    }
}

bool PasswordDao::DeleteOldTombstones(std::int64_t cutoff) {
    try {
        Statement stmt(
            db_.handle(),
            "DELETE FROM password_entries WHERE isDeleted = 1 AND "
            "updatedAt < ?");
        stmt.BindInt64(1, cutoff);
        stmt.StepDone();
        return true;
    } catch (const std::exception& ex) {
        qWarning() << "PasswordDao::DeleteOldTombstones failed:" << ex.what();
        return false;
    }
}

}  // namespace passvault::storage
