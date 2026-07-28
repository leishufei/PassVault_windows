#pragma once

#include <sqlite3.h>

#include <QString>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace passvault::storage {

class Database {
 public:
    static std::unique_ptr<Database> Open(const QString& file_path);
    static std::unique_ptr<Database> OpenInMemory();

    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) = delete;
    Database& operator=(Database&&) = delete;

    sqlite3* handle() const { return db_; }

    bool Execute(std::string_view sql);

    int UserVersion();
    void SetUserVersion(int version);

    std::int64_t LastInsertRowid() const;

 private:
    explicit Database(sqlite3* db);

    sqlite3* db_;
};

class Transaction {
 public:
    explicit Transaction(Database& db);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = delete;
    Transaction& operator=(Transaction&&) = delete;

    bool Commit();

 private:
    Database& db_;
    bool committed_ = false;
};

}  // namespace passvault::storage
