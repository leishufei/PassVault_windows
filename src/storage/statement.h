#pragma once

#include <sqlite3.h>

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <string_view>

namespace passvault::storage {

class Statement {
 public:
    Statement(sqlite3* db, std::string_view sql);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&&) = delete;
    Statement& operator=(Statement&&) = delete;

    void BindInt(int index, int value);
    void BindInt64(int index, std::int64_t value);
    void BindBool(int index, bool value);
    void BindText(int index, const QString& value);
    void BindText(int index, std::string_view value);
    void BindBlob(int index, const QByteArray& value);
    void BindNull(int index);

    bool Step();
    int StepDone();

    int ColumnInt(int col) const;
    std::int64_t ColumnInt64(int col) const;
    bool ColumnBool(int col) const;
    QString ColumnText(int col) const;
    QByteArray ColumnBlob(int col) const;

    sqlite3_stmt* handle() const { return stmt_; }

 private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

}  // namespace passvault::storage
