#include <gtest/gtest.h>

#include <sqlite3.h>

#include <stdexcept>

#include "storage/database.h"
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
