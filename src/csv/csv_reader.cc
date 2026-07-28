#include "csv/csv_reader.h"

#include <QString>

namespace passvault::csv {

namespace {

constexpr char kBomByte0 = static_cast<char>(0xEF);
constexpr char kBomByte1 = static_cast<char>(0xBB);
constexpr char kBomByte2 = static_cast<char>(0xBF);

QByteArray StripBom(const QByteArray& data) {
    if (data.size() >= 3 && data[0] == kBomByte0 && data[1] == kBomByte1 &&
        data[2] == kBomByte2) {
        return data.mid(3);
    }
    return data;
}

}  // namespace

QVector<QStringList> CsvReader::ReadAll(const QByteArray& data) {
    QVector<QStringList> rows;
    const QByteArray stripped = StripBom(data);
    if (stripped.isEmpty()) return rows;

    QStringList current_row;
    QByteArray current_field;
    bool in_quotes = false;
    bool row_has_content = false;

    const int n = stripped.size();
    int i = 0;
    while (i < n) {
        const char c = stripped[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < n && stripped[i + 1] == '"') {
                    current_field.append('"');
                    i += 2;
                    continue;
                }
                in_quotes = false;
                ++i;
                continue;
            }
            current_field.append(c);
            ++i;
            continue;
        }

        if (c == '"') {
            in_quotes = true;
            row_has_content = true;
            ++i;
            continue;
        }
        if (c == ',') {
            current_row.append(QString::fromUtf8(current_field));
            current_field.clear();
            row_has_content = true;
            ++i;
            continue;
        }
        if (c == '\r' || c == '\n') {
            current_row.append(QString::fromUtf8(current_field));
            current_field.clear();
            if (row_has_content || !current_row.isEmpty()) {
                rows.append(current_row);
            }
            current_row.clear();
            row_has_content = false;
            if (c == '\r' && i + 1 < n && stripped[i + 1] == '\n') {
                i += 2;
            } else {
                ++i;
            }
            continue;
        }

        current_field.append(c);
        row_has_content = true;
        ++i;
    }

    if (row_has_content || !current_field.isEmpty() || !current_row.isEmpty()) {
        current_row.append(QString::fromUtf8(current_field));
        rows.append(current_row);
    }

    return rows;
}

}  // namespace passvault::csv
