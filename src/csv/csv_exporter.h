#pragma once

#include <QIODevice>
#include <QList>
#include <QString>

#include <cstdint>

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
    static bool Export(QIODevice* device, QList<ExportEntry> entries);
};

}  // namespace passvault::csv
