#include <gtest/gtest.h>

#include <sqlite3.h>

#include <QDir>
#include <QUuid>

#include <stdexcept>

#include "storage/database.h"
#include "storage/category_dao.h"
#include "storage/password_dao.h"
#include "storage/schema.h"
#include "storage/statement.h"

namespace {

using passvault::storage::Database;
using passvault::storage::EnsureCurrentSchema;
using passvault::storage::kCurrentSchemaVersion;
using passvault::storage::Statement;
using passvault::storage::Transaction;

int TableExists(Database& db, const char* name) {
    Statement stmt(db.handle(),
                   "SELECT COUNT(*) FROM sqlite_master "
                   "WHERE type = 'table' AND name = ?");
    stmt.BindText(1, std::string_view(name));
    stmt.Step();
    return stmt.ColumnInt(0);
}

int IndexExists(Database& db, const char* name) {
    Statement stmt(db.handle(),
                   "SELECT COUNT(*) FROM sqlite_master "
                   "WHERE type = 'index' AND name = ?");
    stmt.BindText(1, std::string_view(name));
    stmt.Step();
    return stmt.ColumnInt(0);
}

}  // namespace

TEST(Database, OpenInMemoryReturnsHandle) {
    auto db = Database::OpenInMemory();
    ASSERT_NE(db.get(), nullptr);
    EXPECT_NE(db->handle(), nullptr);
}

TEST(Database, ExecuteAndUserVersion) {
    auto db = Database::OpenInMemory();
    EXPECT_EQ(db->UserVersion(), 0);
    db->SetUserVersion(42);
    EXPECT_EQ(db->UserVersion(), 42);
    EXPECT_TRUE(db->Execute("CREATE TABLE t(a INTEGER)"));
    EXPECT_TRUE(db->Execute("INSERT INTO t VALUES (1)"));
    EXPECT_FALSE(db->Execute("INSERT INTO does_not_exist VALUES (1)"));
}

TEST(Database, TransactionCommit) {
    auto db = Database::OpenInMemory();
    ASSERT_TRUE(db->Execute("CREATE TABLE t(v INTEGER)"));
    {
        Transaction tx(*db);
        ASSERT_TRUE(db->Execute("INSERT INTO t VALUES (7)"));
        EXPECT_TRUE(tx.Commit());
    }
    Statement stmt(db->handle(), "SELECT v FROM t");
    ASSERT_TRUE(stmt.Step());
    EXPECT_EQ(stmt.ColumnInt(0), 7);
}

TEST(Database, TransactionRollbackOnScopeExit) {
    auto db = Database::OpenInMemory();
    ASSERT_TRUE(db->Execute("CREATE TABLE t(v INTEGER)"));
    {
        Transaction tx(*db);
        ASSERT_TRUE(db->Execute("INSERT INTO t VALUES (7)"));
        // No Commit — should ROLLBACK.
    }
    Statement stmt(db->handle(), "SELECT COUNT(*) FROM t");
    ASSERT_TRUE(stmt.Step());
    EXPECT_EQ(stmt.ColumnInt(0), 0);
}

TEST(Database, LastInsertRowid) {
    auto db = Database::OpenInMemory();
    ASSERT_TRUE(db->Execute("CREATE TABLE t(id INTEGER PRIMARY KEY, v INTEGER)"));
    ASSERT_TRUE(db->Execute("INSERT INTO t(v) VALUES (10)"));
    EXPECT_EQ(db->LastInsertRowid(), 1);
    ASSERT_TRUE(db->Execute("INSERT INTO t(v) VALUES (20)"));
    EXPECT_EQ(db->LastInsertRowid(), 2);
}

TEST(Schema, FreshDatabaseCreatesTablesAndIndex) {
    auto db = Database::OpenInMemory();
    EnsureCurrentSchema(*db);
    EXPECT_EQ(db->UserVersion(), kCurrentSchemaVersion);
    EXPECT_EQ(TableExists(*db, "password_entries"), 1);
    EXPECT_EQ(TableExists(*db, "categories"), 1);
    EXPECT_EQ(IndexExists(*db, "index_password_entries_uuid"), 1);
}

TEST(Schema, EnsureIsIdempotent) {
    auto db = Database::OpenInMemory();
    EnsureCurrentSchema(*db);
    EXPECT_NO_THROW(EnsureCurrentSchema(*db));
    EXPECT_EQ(db->UserVersion(), kCurrentSchemaVersion);
}

TEST(Schema, RejectsUnsupportedVersion) {
    auto db = Database::OpenInMemory();
    db->SetUserVersion(4);
    EXPECT_THROW(EnsureCurrentSchema(*db), std::runtime_error);
}

TEST(Schema, UuidIndexIsUnique) {
    auto db = Database::OpenInMemory();
    EnsureCurrentSchema(*db);
    ASSERT_TRUE(db->Execute(
        "INSERT INTO password_entries(uuid, title, username, "
        "encryptedPassword, passwordIv, createdAt, updatedAt) "
        "VALUES ('same-uuid', 't1', 'u1', x'00', x'00', 1, 1)"));
    EXPECT_FALSE(db->Execute(
        "INSERT INTO password_entries(uuid, title, username, "
        "encryptedPassword, passwordIv, createdAt, updatedAt) "
        "VALUES ('same-uuid', 't2', 'u2', x'00', x'00', 1, 1)"));
}

TEST(Schema, HistoricalV8FileAddsMarkersWithoutChangingExistingRows) {
    const QString root = qEnvironmentVariable("PASSVAULT_TEST_ARTIFACT_DIR",
        QDir::currentPath() + QStringLiteral("/build/diagnostics/schema-upgrade"));
    ASSERT_TRUE(QDir().mkpath(root));
    const QString path = root + QStringLiteral("/historical-v8-") +
        QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".db");
    {
        auto db = Database::Open(path);
        ASSERT_TRUE(db->Execute(R"SQL(
CREATE TABLE password_entries (
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
CREATE UNIQUE INDEX index_password_entries_uuid ON password_entries(uuid);
CREATE TABLE categories (
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
INSERT INTO categories VALUES
    (7,'historical-category','demo category',123,0,3,1700000000000,1710000000000,0);
INSERT INTO password_entries VALUES
    (42,'historical-entry','demo title','demo user',x'000102ff',x'10203040',
     'https://example.test','demo.package','demo notes',1,456,2,7,
     1700000000100,1710000000100,1);
PRAGMA user_version = 8;
)SQL"));
        EXPECT_EQ(db->UserVersion(), 8);
        EXPECT_EQ(TableExists(*db, "password_deletion_markers"), 0);
        EXPECT_EQ(IndexExists(*db, "index_password_entries_uuid"), 1);
    }
    for (int reopen = 0; reopen < 2; ++reopen) {
        auto db = Database::Open(path);
        ASSERT_NO_THROW(EnsureCurrentSchema(*db));
        EXPECT_EQ(db->UserVersion(), 8);
        EXPECT_EQ(IndexExists(*db, "index_password_entries_uuid"), 1);
        passvault::storage::PasswordDao passwords(*db);
        passvault::storage::CategoryDao categories(*db);
        const auto entries = passwords.ReadAll();
        ASSERT_TRUE(entries);
        ASSERT_EQ(entries->size(), 1u);
        const auto& entry = entries->front();
        EXPECT_EQ(entry.id, 42);
        EXPECT_EQ(entry.uuid, QStringLiteral("historical-entry"));
        EXPECT_EQ(entry.title, QStringLiteral("demo title"));
        EXPECT_EQ(entry.username, QStringLiteral("demo user"));
        EXPECT_EQ(entry.encrypted_password, QByteArray::fromHex("000102ff"));
        EXPECT_EQ(entry.password_iv, QByteArray::fromHex("10203040"));
        EXPECT_EQ(entry.website, QStringLiteral("https://example.test"));
        EXPECT_EQ(entry.app_package_name, QStringLiteral("demo.package"));
        EXPECT_EQ(entry.notes, QStringLiteral("demo notes"));
        EXPECT_TRUE(entry.is_favorite);
        EXPECT_EQ(entry.icon_color, 456);
        EXPECT_EQ(entry.strength, 2);
        EXPECT_EQ(entry.category_id, 7);
        EXPECT_EQ(entry.created_at, 1700000000100LL);
        EXPECT_EQ(entry.updated_at, 1710000000100LL);
        EXPECT_TRUE(entry.is_deleted);
        const auto all_categories = categories.ListIncludingDeleted();
        ASSERT_EQ(all_categories.size(), 1u);
        const auto& category = all_categories.front();
        EXPECT_EQ(category.id, 7);
        EXPECT_EQ(category.uuid, QStringLiteral("historical-category"));
        EXPECT_EQ(category.name, QStringLiteral("demo category"));
        EXPECT_EQ(category.color, 123);
        EXPECT_FALSE(category.is_default);
        EXPECT_EQ(category.sort_order, 3);
        EXPECT_EQ(category.created_at, 1700000000000LL);
        EXPECT_EQ(category.updated_at, 1710000000000LL);
        EXPECT_FALSE(category.is_deleted);
        const auto markers = passwords.ListDeletionMarkers();
        ASSERT_TRUE(markers);
        ASSERT_EQ(markers->size(), reopen == 0 ? 0u : 1u);
        if (reopen == 0) {
            ASSERT_TRUE(passwords.ApplyDeletionMarker(
                QStringLiteral("new-marker"), 1800000000000LL));
        } else {
            EXPECT_EQ(markers->front().uuid, QStringLiteral("new-marker"));
            EXPECT_EQ(markers->front().deleted_at, 1800000000000LL);
        }
    }
}
