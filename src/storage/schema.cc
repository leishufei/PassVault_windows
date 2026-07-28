#include "storage/schema.h"

#include "storage/database.h"

#include <stdexcept>
#include <string>

namespace passvault::storage {

namespace {

constexpr const char* kCreatePasswordEntries = R"SQL(
CREATE TABLE IF NOT EXISTS password_entries (
    id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
    uuid TEXT NOT NULL DEFAULT '',
    title TEXT NOT NULL,
    username TEXT NOT NULL,
    encryptedPassword BLOB NOT NULL,
    passwordIv BLOB NOT NULL,
    website TEXT NOT NULL DEFAULT '',
    appPackageName TEXT NOT NULL DEFAULT '',
    notes TEXT NOT NULL DEFAULT '',
    isFavorite INTEGER NOT NULL DEFAULT 0,
    iconColor INTEGER NOT NULL DEFAULT 0,
    strength INTEGER NOT NULL DEFAULT 0,
    categoryId INTEGER NOT NULL DEFAULT 0,
    createdAt INTEGER NOT NULL,
    updatedAt INTEGER NOT NULL,
    isDeleted INTEGER NOT NULL DEFAULT 0
);
)SQL";

constexpr const char* kCreatePasswordEntriesUuidIndex = R"SQL(
CREATE UNIQUE INDEX IF NOT EXISTS index_password_entries_uuid
    ON password_entries(uuid);
)SQL";

constexpr const char* kCreateCategories = R"SQL(
CREATE TABLE IF NOT EXISTS categories (
    id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
    uuid TEXT NOT NULL DEFAULT '',
    name TEXT NOT NULL,
    color INTEGER NOT NULL,
    isDefault INTEGER NOT NULL DEFAULT 0,
    sortOrder INTEGER NOT NULL DEFAULT 0,
    createdAt INTEGER NOT NULL,
    updatedAt INTEGER NOT NULL,
    isDeleted INTEGER NOT NULL DEFAULT 0
);
)SQL";

void ApplyDdl(Database& db) {
    if (!db.Execute(kCreatePasswordEntries) ||
        !db.Execute(kCreatePasswordEntriesUuidIndex) ||
        !db.Execute(kCreateCategories)) {
        throw std::runtime_error("failed to create v8 schema");
    }
}

}  // namespace

void EnsureCurrentSchema(Database& db) {
    const int version = db.UserVersion();

    if (version == kCurrentSchemaVersion) {
        ApplyDdl(db);
        return;
    }

    if (version == 0) {
        Transaction tx(db);
        ApplyDdl(db);
        db.SetUserVersion(kCurrentSchemaVersion);
        if (!tx.Commit()) {
            throw std::runtime_error("failed to commit schema creation");
        }
        return;
    }

    throw std::runtime_error(
        "unsupported schema version " + std::to_string(version) +
        "; upgrade the Android app to schema " +
        std::to_string(kCurrentSchemaVersion) + " and re-sync first");
}

}  // namespace passvault::storage
