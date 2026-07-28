#include "csv/csv_validator.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace passvault::csv {

int CsvValidationResult::InsertCount() const {
    int n = 0;
    for (const auto& row : valid_rows) {
        if (row.action == ImportAction::kInsert) ++n;
    }
    return n;
}

int CsvValidationResult::UpdateCount() const {
    int n = 0;
    for (const auto& row : valid_rows) {
        if (row.action == ImportAction::kUpdate && !row.diffs.isEmpty()) ++n;
    }
    return n;
}

const QStringList& CsvValidator::ExtendedHeaders() {
    static const QStringList kHeaders = {
        QStringLiteral("ID"),      QStringLiteral("分类"),
        QStringLiteral("标题"),    QStringLiteral("用户名"),
        QStringLiteral("密码"),    QStringLiteral("网站"),
        QStringLiteral("备注"),
    };
    return kHeaders;
}

const QStringList& CsvValidator::LegacyHeaders() {
    static const QStringList kHeaders = {
        QStringLiteral("分类"),
        QStringLiteral("名称"),
        QStringLiteral("用户名"),
        QStringLiteral("密码"),
    };
    return kHeaders;
}

const QString& CsvValidator::Uncategorized() {
    static const QString kName = QStringLiteral("未分类");
    return kName;
}

namespace {

const QRegularExpression& UuidPattern() {
    static const QRegularExpression kRe(
        QStringLiteral(
            "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"),
        QRegularExpression::CaseInsensitiveOption);
    return kRe;
}

bool LooksLikeUuid(const QString& s) {
    return UuidPattern().match(s).hasMatch();
}

struct ParsedRow {
    QString uuid;
    QString category_name;
    QString title;
    QString username;
    QString password;
    QString website;
    QString notes;
};

std::optional<ParsedRow> ParseRow(const QStringList& record, CsvFormat format) {
    ParsedRow out;
    if (format == CsvFormat::kExtended) {
        if (record.size() < CsvValidator::kExtendedColumnCount) return std::nullopt;
        const QString id_cell = record[0].trimmed();
        out.uuid = LooksLikeUuid(id_cell) ? id_cell : QString();
        out.category_name = record[1].trimmed();
        out.title = record[2].trimmed();
        out.username = record[3].trimmed();
        out.password = record[4].trimmed();
        out.website = record[5].trimmed();
        out.notes = record[6].trimmed();
        return out;
    }
    if (format == CsvFormat::kLegacy) {
        if (record.size() < CsvValidator::kLegacyColumnCount) return std::nullopt;
        out.uuid.clear();
        out.category_name = record[0].trimmed();
        out.title = record[1].trimmed();
        out.username = record[2].trimmed();
        out.password = record[3].trimmed();
        return out;
    }
    return std::nullopt;
}

bool IsHeaderRow(const QStringList& row, CsvFormat format) {
    if (format == CsvFormat::kExtended) {
        const QString first = row.value(0).trimmed();
        if (QString::compare(first, QStringLiteral("ID"), Qt::CaseInsensitive) == 0) {
            return true;
        }
        return row.value(2).contains(QStringLiteral("标题"));
    }
    if (format == CsvFormat::kLegacy) {
        return row.value(0).contains(QStringLiteral("分类")) ||
               row.value(1).contains(QStringLiteral("名称"));
    }
    return false;
}

QList<FieldDiff> ComputeDiffs(const ExistingPasswordSnapshot& existing,
                              const ParsedRow& parsed,
                              const QString& normalized_category) {
    QList<FieldDiff> diffs;
    auto add = [&](const QString& field, const QString& old_v,
                   const QString& new_v) {
        diffs.append(FieldDiff{field, old_v, new_v});
    };
    if (existing.title != parsed.title) add(QStringLiteral("标题"), existing.title, parsed.title);
    if (existing.username != parsed.username) add(QStringLiteral("用户名"), existing.username, parsed.username);
    if (existing.password != parsed.password) add(QStringLiteral("密码"), existing.password, parsed.password);
    if (existing.website != parsed.website) add(QStringLiteral("网站"), existing.website, parsed.website);
    if (existing.notes != parsed.notes) add(QStringLiteral("备注"), existing.notes, parsed.notes);
    if (existing.category_name != normalized_category) {
        add(QStringLiteral("分类"), existing.category_name, normalized_category);
    }
    return diffs;
}

QVector<QStringList> StripLeadingBom(const QVector<QStringList>& records) {
    if (records.isEmpty() || records.first().isEmpty()) return records;
    const QString& cell = records.first().first();
    if (!cell.startsWith(QChar(0xFEFF))) return records;
    QVector<QStringList> out = records;
    QStringList first_row = out.first();
    QString first_cell = first_row.first();
    first_cell.remove(0, 1);
    first_row[0] = first_cell;
    out[0] = first_row;
    return out;
}

}  // namespace

CsvFormat CsvValidator::DetectFormat(const QVector<QStringList>& records) const {
    if (records.isEmpty()) return CsvFormat::kUnknown;
    const QStringList& first_row = records.first();

    if (first_row.size() >= kExtendedColumnCount) {
        const QString first_cell = first_row.value(0).trimmed();
        const bool has_id_header =
            QString::compare(first_cell, QStringLiteral("ID"),
                             Qt::CaseInsensitive) == 0;
        const bool looks_like_uuid = LooksLikeUuid(first_cell);
        const bool has_extended_header =
            first_row.value(2).contains(QStringLiteral("标题")) ||
            first_row.value(5).contains(QStringLiteral("网站"));
        if (has_id_header || looks_like_uuid || has_extended_header) {
            return CsvFormat::kExtended;
        }
    }

    if (first_row.size() >= kLegacyColumnCount &&
        first_row.size() < kExtendedColumnCount) {
        return CsvFormat::kLegacy;
    }
    return CsvFormat::kUnknown;
}

CsvValidationResult CsvValidator::Validate(
    const QVector<QStringList>& records,
    const QList<ExistingPasswordSnapshot>& existing_passwords,
    const QStringList& existing_category_names) const {
    const QVector<QStringList> sanitized = StripLeadingBom(records);
    const CsvFormat format = DetectFormat(sanitized);

    CsvValidationResult result;
    result.format = format;
    if (format == CsvFormat::kUnknown || sanitized.isEmpty()) return result;

    const int start_idx = IsHeaderRow(sanitized.first(), format) ? 1 : 0;

    QSet<QString> existing_category_lower;
    for (const auto& name : existing_category_names) {
        existing_category_lower.insert(name.toLower());
    }

    QHash<QString, const ExistingPasswordSnapshot*> by_uuid;
    QHash<QString, const ExistingPasswordSnapshot*> by_title_user;
    for (const auto& snap : existing_passwords) {
        if (!snap.uuid.isEmpty()) by_uuid.insert(snap.uuid, &snap);
        const QString key = snap.title.toLower() + QChar('|') + snap.username.toLower();
        by_title_user.insert(key, &snap);
    }

    QSet<QString> new_category_set;

    for (int idx = start_idx; idx < sanitized.size(); ++idx) {
        const QStringList& record = sanitized[idx];
        const int display_row = idx + 1;
        const auto parsed_opt = ParseRow(record, format);
        if (!parsed_opt.has_value()) {
            result.invalid_rows.append(
                InvalidRow{display_row, record, QStringLiteral("字段数量不足")});
            continue;
        }
        const ParsedRow& parsed = *parsed_opt;
        if (parsed.title.isEmpty()) {
            result.invalid_rows.append(
                InvalidRow{display_row, record, QStringLiteral("标题不能为空")});
            continue;
        }
        if (parsed.username.isEmpty() && parsed.password.isEmpty()) {
            result.invalid_rows.append(InvalidRow{
                display_row, record,
                QStringLiteral("用户名和密码不能同时为空")});
            continue;
        }

        const ExistingPasswordSnapshot* existing = nullptr;
        if (format == CsvFormat::kExtended) {
            if (!parsed.uuid.isEmpty()) {
                existing = by_uuid.value(parsed.uuid, nullptr);
            }
            if (existing == nullptr) {
                const QString key = parsed.title.toLower() + QChar('|') +
                                    parsed.username.toLower();
                existing = by_title_user.value(key, nullptr);
            }
        } else {
            const QString key = parsed.title.toLower() + QChar('|') +
                                parsed.username.toLower();
            existing = by_title_user.value(key, nullptr);
        }

        const QString normalized_category =
            parsed.category_name.isEmpty() ? Uncategorized() : parsed.category_name;
        if (normalized_category != Uncategorized() &&
            !existing_category_lower.contains(normalized_category.toLower())) {
            new_category_set.insert(normalized_category);
        }

        ValidatedPasswordRow row;
        row.row_index = display_row;
        row.title = parsed.title;
        row.username = parsed.username;
        row.password = parsed.password;
        row.website = parsed.website;
        row.notes = parsed.notes;
        row.category_name = normalized_category;
        if (existing != nullptr) {
            row.action = ImportAction::kUpdate;
            row.existing_id = existing->id;
            row.existing_uuid = existing->uuid;
            row.diffs = ComputeDiffs(*existing, parsed, normalized_category);
        } else {
            row.action = ImportAction::kInsert;
            row.existing_uuid = parsed.uuid;
        }
        result.valid_rows.append(row);
    }

    result.total_rows = sanitized.size() - start_idx;
    for (const auto& name : new_category_set) {
        result.new_category_names.append(name);
    }
    return result;
}

}  // namespace passvault::csv
