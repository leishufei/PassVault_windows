#pragma once

#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QVector>

namespace passvault::csv {

class CsvWriter {
 public:
    explicit CsvWriter(QIODevice* device);

    bool WriteRow(const QStringList& fields);
    bool WriteRows(const QVector<QStringList>& rows);

    static QByteArray EncodeField(const QString& field);

 private:
    static bool NeedsQuoting(const QString& field);

    QIODevice* device_;
};

}  // namespace passvault::csv
