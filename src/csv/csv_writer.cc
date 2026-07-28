#include "csv/csv_writer.h"

namespace passvault::csv {

namespace {

constexpr char kLineTerminator[] = "\r\n";
constexpr char kFieldSeparator = ',';
constexpr char kQuote = '"';

}  // namespace

CsvWriter::CsvWriter(QIODevice* device) : device_(device) {}

bool CsvWriter::WriteRow(const QStringList& fields) {
    if (device_ == nullptr || !device_->isOpen() || !device_->isWritable()) {
        return false;
    }
    QByteArray line;
    for (int i = 0; i < fields.size(); ++i) {
        if (i > 0) line.append(kFieldSeparator);
        line.append(EncodeField(fields.at(i)));
    }
    line.append(kLineTerminator);
    return device_->write(line) == line.size();
}

bool CsvWriter::WriteRows(const QVector<QStringList>& rows) {
    for (const auto& row : rows) {
        if (!WriteRow(row)) return false;
    }
    return true;
}

QByteArray CsvWriter::EncodeField(const QString& field) {
    const QByteArray utf8 = field.toUtf8();
    if (!NeedsQuoting(field)) return utf8;

    QByteArray out;
    out.reserve(utf8.size() + 2);
    out.append(kQuote);
    for (const char c : utf8) {
        if (c == kQuote) out.append(kQuote);
        out.append(c);
    }
    out.append(kQuote);
    return out;
}

bool CsvWriter::NeedsQuoting(const QString& field) {
    for (const QChar ch : field) {
        const char16_t c = ch.unicode();
        if (c == kFieldSeparator || c == kQuote || c == '\r' || c == '\n') {
            return true;
        }
    }
    return false;
}

}  // namespace passvault::csv
