#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace passvault::csv {

enum class CsvFormat {
    kUnknown,
    kExtended,
    kLegacy,
};

enum class ImportAction {
    kInsert,
    kUpdate,
};

struct ExistingPasswordSnapshot {
    std::int64_t id = 0;
    QString uuid;
    QString title;
    QString username;
    QString password;
    QString website;
    QString notes;
    QString category_name;
};

struct FieldDiff {
    QString field;
    QString old_value;
    QString new_value;
};

struct ValidatedPasswordRow {
    int row_index = 0;
    ImportAction action = ImportAction::kInsert;
    QString title;
    QString username;
    QString password;
    QString website;
    QString notes;
    QString category_name;
    std::optional<std::int64_t> existing_id;
    QString existing_uuid;
    QList<FieldDiff> diffs;
};

struct InvalidRow {
    int row_index = 0;
    QStringList raw_data;
    QString error_message;
};

struct CsvValidationResult {
    CsvFormat format = CsvFormat::kUnknown;
    int total_rows = 0;
    QList<ValidatedPasswordRow> valid_rows;
    QList<InvalidRow> invalid_rows;
    QStringList new_category_names;

    int InsertCount() const;
    int UpdateCount() const;
};

}  // namespace passvault::csv
