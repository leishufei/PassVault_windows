#pragma once

#include <QByteArray>
#include <QStringList>
#include <QVector>

namespace passvault::csv {

class CsvReader {
 public:
    static QVector<QStringList> ReadAll(const QByteArray& data);
};

}  // namespace passvault::csv
