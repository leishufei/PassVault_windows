#include "storage/statement.h"

#include <stdexcept>
#include <string>

namespace passvault::storage {

namespace {

[[noreturn]] void ThrowSqliteError(sqlite3* db, const char* what) {
    throw std::runtime_error(std::string(what) + ": " + sqlite3_errmsg(db));
}

}  // namespace

Statement::Statement(sqlite3* db, std::string_view sql) : db_(db) {
    if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()),
                           &stmt_, nullptr) != SQLITE_OK) {
        stmt_ = nullptr;
        ThrowSqliteError(db_, "sqlite3_prepare_v2 failed");
    }
}

Statement::~Statement() {
    if (stmt_) {
        sqlite3_finalize(stmt_);
    }
}

void Statement::BindInt(int index, int value) {
    if (sqlite3_bind_int(stmt_, index, value) != SQLITE_OK) {
        ThrowSqliteError(db_, "sqlite3_bind_int failed");
    }
}

void Statement::BindInt64(int index, std::int64_t value) {
    if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) {
        ThrowSqliteError(db_, "sqlite3_bind_int64 failed");
    }
}

void Statement::BindBool(int index, bool value) {
    BindInt(index, value ? 1 : 0);
}

void Statement::BindText(int index, const QString& value) {
    const QByteArray utf8 = value.toUtf8();
    if (sqlite3_bind_text(stmt_, index, utf8.constData(), utf8.size(),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        ThrowSqliteError(db_, "sqlite3_bind_text failed");
    }
}

void Statement::BindText(int index, std::string_view value) {
    if (sqlite3_bind_text(stmt_, index, value.data(),
                          static_cast<int>(value.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        ThrowSqliteError(db_, "sqlite3_bind_text failed");
    }
}

void Statement::BindBlob(int index, const QByteArray& value) {
    if (sqlite3_bind_blob(stmt_, index, value.constData(), value.size(),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        ThrowSqliteError(db_, "sqlite3_bind_blob failed");
    }
}

void Statement::BindNull(int index) {
    if (sqlite3_bind_null(stmt_, index) != SQLITE_OK) {
        ThrowSqliteError(db_, "sqlite3_bind_null failed");
    }
}

bool Statement::Step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    ThrowSqliteError(db_, "sqlite3_step failed");
}

int Statement::StepDone() {
    const int rc = sqlite3_step(stmt_);
    if (rc != SQLITE_DONE) {
        ThrowSqliteError(db_, "sqlite3_step (expected DONE) failed");
    }
    return sqlite3_changes(db_);
}

int Statement::ColumnInt(int col) const {
    return sqlite3_column_int(stmt_, col);
}

std::int64_t Statement::ColumnInt64(int col) const {
    return sqlite3_column_int64(stmt_, col);
}

bool Statement::ColumnBool(int col) const {
    return sqlite3_column_int(stmt_, col) != 0;
}

QString Statement::ColumnText(int col) const {
    const auto* bytes =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt_, col));
    const int len = sqlite3_column_bytes(stmt_, col);
    if (!bytes || len <= 0) {
        return QString();
    }
    return QString::fromUtf8(bytes, len);
}

QByteArray Statement::ColumnBlob(int col) const {
    const void* data = sqlite3_column_blob(stmt_, col);
    const int len = sqlite3_column_bytes(stmt_, col);
    if (!data || len <= 0) {
        return QByteArray();
    }
    return QByteArray(static_cast<const char*>(data), len);
}

}  // namespace passvault::storage
