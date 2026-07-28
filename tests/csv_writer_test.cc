#include <gtest/gtest.h>

#include <QBuffer>
#include <QString>
#include <QStringList>

#include "csv/csv_writer.h"

namespace {

using passvault::csv::CsvWriter;

QByteArray Write(const QVector<QStringList>& rows) {
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    CsvWriter writer(&buf);
    writer.WriteRows(rows);
    return buf.data();
}

}  // namespace

TEST(CsvWriter, PlainFieldsNoQuoting) {
    const auto out = Write({{"a", "b", "c"}});
    EXPECT_EQ(out, QByteArray("a,b,c\r\n"));
}

TEST(CsvWriter, QuotesFieldsWithComma) {
    const auto out = Write({{"a", "b,c", "d"}});
    EXPECT_EQ(out, QByteArray("a,\"b,c\",d\r\n"));
}

TEST(CsvWriter, EscapesInternalQuotes) {
    const auto out = Write({{"say \"hi\""}});
    EXPECT_EQ(out, QByteArray("\"say \"\"hi\"\"\"\r\n"));
}

TEST(CsvWriter, QuotesFieldsWithNewline) {
    const auto out = Write({{"line1\nline2"}});
    EXPECT_EQ(out, QByteArray("\"line1\nline2\"\r\n"));
}

TEST(CsvWriter, ChineseIsUtf8WithoutQuoting) {
    const auto out = Write({{QStringLiteral("分类"), QStringLiteral("标题")}});
    const QByteArray expected =
        QString(QStringLiteral("分类,标题\r\n")).toUtf8();
    EXPECT_EQ(out, expected);
}

TEST(CsvWriter, MultipleRowsCrlfSeparated) {
    const auto out = Write({{"a", "b"}, {"c", "d"}});
    EXPECT_EQ(out, QByteArray("a,b\r\nc,d\r\n"));
}

TEST(CsvWriter, EmptyFieldsProduceEmptyCells) {
    const auto out = Write({{"", "", ""}});
    EXPECT_EQ(out, QByteArray(",,\r\n"));
}
