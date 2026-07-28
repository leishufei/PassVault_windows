#include "csv/csv_exporter.h"

#include <algorithm>
#include <tuple>

#include "csv/csv_validator.h"
#include "csv/csv_writer.h"

namespace passvault::csv {

namespace {

QByteArray Utf8Bom() {
    return QByteArray::fromRawData("\xEF\xBB\xBF", 3);
}

int CategorySortKey(const ExportEntry& entry) {
    if (entry.category_id == 0) return -1;
    return entry.category_sort_order;
}

}  // namespace

bool CsvExporter::Export(QIODevice* device, QList<ExportEntry> entries) {
    if (device == nullptr || !device->isOpen() || !device->isWritable()) {
        return false;
    }

    const QByteArray bom = Utf8Bom();
    if (device->write(bom) != bom.size()) return false;

    std::sort(entries.begin(), entries.end(),
              [](const ExportEntry& a, const ExportEntry& b) {
                  return std::tuple(CategorySortKey(a), a.category_id,
                                    a.created_at) <
                         std::tuple(CategorySortKey(b), b.category_id,
                                    b.created_at);
              });

    CsvWriter writer(device);
    if (!writer.WriteRow(CsvValidator::ExtendedHeaders())) return false;

    for (const auto& e : entries) {
        const QStringList row{
            e.uuid,      e.category_name, e.title,   e.username,
            e.password,  e.website,       e.notes,
        };
        if (!writer.WriteRow(row)) return false;
    }
    return true;
}

}  // namespace passvault::csv
