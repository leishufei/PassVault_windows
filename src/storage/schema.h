#pragma once

namespace passvault::storage {

class Database;

inline constexpr int kCurrentSchemaVersion = 8;

void EnsureCurrentSchema(Database& db);

}  // namespace passvault::storage
