#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

#include "csv/csv_models.h"

namespace passvault::crypto {
class SessionKey;
}

namespace passvault::storage {
class CategoryDao;
class PasswordDao;
}

namespace passvault::csv {

struct ImportSummary {
    int inserted = 0;
    int updated = 0;
    QStringList created_categories;
};

class CsvImporter {
 public:
    static std::optional<ImportSummary> Apply(
        const CsvValidationResult& validation,
        storage::PasswordDao& password_dao,
        storage::CategoryDao& category_dao,
        const crypto::SessionKey& session_key,
        std::int64_t now_ms);
};

}  // namespace passvault::csv
