#pragma once

#include <QList>
#include <QStringList>
#include <QVector>

#include "csv/csv_models.h"

namespace passvault::csv {

class CsvValidator {
 public:
    static constexpr int kExtendedColumnCount = 7;
    static constexpr int kLegacyColumnCount = 4;

    static const QStringList& ExtendedHeaders();
    static const QStringList& LegacyHeaders();
    static const QString& Uncategorized();

    CsvFormat DetectFormat(const QVector<QStringList>& records) const;

    CsvValidationResult Validate(
        const QVector<QStringList>& records,
        const QList<ExistingPasswordSnapshot>& existing_passwords,
        const QStringList& existing_category_names) const;
};

}  // namespace passvault::csv
