#include "storage/database.h"

#include <QByteArray>
#include <QDebug>

#include <stdexcept>
#include <string>

namespace passvault::storage {

namespace {

sqlite3* OpenRaw(const char* utf8_path, int flags) {
    sqlite3* db = nullptr;
    const int rc = sqlite3_open_v2(utf8_path, &db, flags, nullptr);
    if (rc != SQLITE_OK) {
        const std::string msg = db ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
        if (db) {
            sqlite3_close(db);
        }
        throw std::runtime_error("sqlite3_open_v2 failed: " + msg);
    }
    return db;
}

void ExecOrThrow(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        sqlite3_close(db);
        throw std::runtime_error(std::string("sqlite3_exec failed: ") + sql +
                                 " - " + msg);
    }
}

}  // namespace

std::unique_ptr<Database> Database::Open(const QString& file_path) {
    const QByteArray utf8 = file_path.toUtf8();
    sqlite3* db = OpenRaw(utf8.constData(),
                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                              SQLITE_OPEN_URI);
    ExecOrThrow(db, "PRAGMA foreign_keys = ON;");
    ExecOrThrow(db, "PRAGMA journal_mode = WAL;");
    ExecOrThrow(db, "PRAGMA synchronous = NORMAL;");
    return std::unique_ptr<Database>(new Database(db));
}

std::unique_ptr<Database> Database::OpenInMemory() {
    sqlite3* db = OpenRaw(":memory:",
                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    ExecOrThrow(db, "PRAGMA foreign_keys = ON;");
    return std::unique_ptr<Database>(new Database(db));
}

Database::Database(sqlite3* db) : db_(db) {}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
    }
}

bool Database::Execute(std::string_view sql) {
    const std::string sql_z(sql);
    char* err = nullptr;
    const int rc = sqlite3_exec(db_, sql_z.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        qWarning() << "sqlite3_exec failed:" << (err ? err : "unknown");
        sqlite3_free(err);
        return false;
    }
    return true;
}

int Database::UserVersion() {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA user_version;", -1, &stmt, nullptr) !=
        SQLITE_OK) {
        throw std::runtime_error(
            std::string("PRAGMA user_version prepare failed: ") +
            sqlite3_errmsg(db_));
    }
    int version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return version;
}

void Database::SetUserVersion(int version) {
    const std::string sql =
        "PRAGMA user_version = " + std::to_string(version) + ";";
    if (!Execute(sql)) {
        throw std::runtime_error("SetUserVersion failed");
    }
}

std::int64_t Database::LastInsertRowid() const {
    return sqlite3_last_insert_rowid(db_);
}

Transaction::Transaction(Database& db) : db_(db) {
    if (!db_.Execute("BEGIN IMMEDIATE;")) {
        throw std::runtime_error("BEGIN IMMEDIATE failed");
    }
}

Transaction::~Transaction() {
    if (!committed_) {
        db_.Execute("ROLLBACK;");
    }
}

bool Transaction::Commit() {
    if (committed_) {
        return true;
    }
    if (db_.Execute("COMMIT;")) {
        committed_ = true;
        return true;
    }
    return false;
}

}  // namespace passvault::storage
