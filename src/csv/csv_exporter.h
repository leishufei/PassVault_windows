#pragma once

#include <QIODevice>
#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

namespace passvault::crypto {
class SessionKey;
}

namespace passvault::storage {
class CategoryDao;
class PasswordDao;
}

namespace passvault::csv {

struct ExportEntry {
    QString uuid;
    QString category_name;
    QString title;
    QString username;
    QString password;
    QString website;
    QString notes;
    int category_sort_order = 0;
    std::int64_t category_id = 0;
    std::int64_t created_at = 0;
};

class CsvExporter {
 public:
    static std::optional<QList<ExportEntry>> CollectEntries(
        storage::PasswordDao& password_dao,
        storage::CategoryDao& category_dao,
        const crypto::SessionKey& session_key);

    static bool Export(QIODevice* device, QList<ExportEntry> entries);
};

}  // namespace passvault::csv
