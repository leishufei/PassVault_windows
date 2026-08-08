#include "csv/csv_exporter.h"

#include <QHash>

#include <algorithm>
#include <tuple>
#include <utility>

#include "crypto/crypto_service.h"
#include "crypto/session_key.h"
#include "csv/csv_validator.h"
#include "csv/csv_writer.h"
#include "model/category.h"
#include "storage/category_dao.h"
#include "storage/password_dao.h"

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

std::optional<QList<ExportEntry>> CsvExporter::CollectEntries(
    storage::PasswordDao& password_dao, storage::CategoryDao& category_dao,
    const crypto::SessionKey& session_key) {
    QHash<std::int64_t, model::Category> categories;
    for (const auto& category : category_dao.ListIncludingDeleted()) {
        categories.insert(category.id, category);
    }

    QList<ExportEntry> entries;
    for (const auto& password : password_dao.ListActive()) {
        auto plaintext = crypto::CryptoService::DecryptGcm(
            session_key.data(), session_key.size(),
            reinterpret_cast<const std::uint8_t*>(
                password.password_iv.constData()),
            static_cast<std::size_t>(password.password_iv.size()),
            reinterpret_cast<const std::uint8_t*>(
                password.encrypted_password.constData()),
            static_cast<std::size_t>(password.encrypted_password.size()));
        if (!plaintext.has_value()) return std::nullopt;

        ExportEntry entry;
        entry.uuid = password.uuid;
        entry.title = password.title;
        entry.username = password.username;
        entry.password = QString::fromUtf8(*plaintext);
        plaintext->fill('\0');
        entry.website = password.website;
        entry.notes = password.notes;
        entry.category_id = password.category_id;
        entry.created_at = password.created_at;

        if (password.category_id == 0) {
            entry.category_name = CsvValidator::Uncategorized();
        } else {
            const auto category = categories.constFind(password.category_id);
            if (category != categories.cend()) {
                entry.category_name = category->name;
                entry.category_sort_order = category->sort_order;
            } else {
                return std::nullopt;
            }
        }
        entries.append(std::move(entry));
    }
    return entries;
}

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
