#include <gtest/gtest.h>

#include <QByteArray>

#include "csv/csv_reader.h"

namespace {

using passvault::csv::CsvReader;

}  // namespace

TEST(CsvReader, EmptyInputReturnsEmpty) {
    const auto rows = CsvReader::ReadAll(QByteArray());
    EXPECT_TRUE(rows.isEmpty());
}

TEST(CsvReader, SingleLineNoTrailingNewline) {
    const auto rows = CsvReader::ReadAll("a,b,c");
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0], (QStringList{"a", "b", "c"}));
}

TEST(CsvReader, CrlfLineTerminatorSplitsRows) {
    const auto rows = CsvReader::ReadAll("a,b\r\nc,d\r\n");
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0], (QStringList{"a", "b"}));
    EXPECT_EQ(rows[1], (QStringList{"c", "d"}));
}

TEST(CsvReader, LfOnlyLineTerminatorAlsoSupported) {
    const auto rows = CsvReader::ReadAll("a,b\nc,d");
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0], (QStringList{"a", "b"}));
    EXPECT_EQ(rows[1], (QStringList{"c", "d"}));
}

TEST(CsvReader, QuotedFieldWithComma) {
    const auto rows = CsvReader::ReadAll("a,\"b,c\",d");
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0], (QStringList{"a", "b,c", "d"}));
}

TEST(CsvReader, QuotedFieldWithEscapedQuote) {
    const auto rows = CsvReader::ReadAll("\"say \"\"hi\"\"\"");
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0], (QStringList{"say \"hi\""}));
}

TEST(CsvReader, QuotedFieldWithNewline) {
    const auto rows = CsvReader::ReadAll("\"line1\r\nline2\"");
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0], (QStringList{"line1\r\nline2"}));
}

TEST(CsvReader, StripsUtf8Bom) {
    QByteArray data;
    data.append("\xEF\xBB\xBF");
    data.append("a,b\r\n");
    const auto rows = CsvReader::ReadAll(data);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0], (QStringList{"a", "b"}));
}

TEST(CsvReader, ChineseUtf8Decoded) {
    const auto rows =
        CsvReader::ReadAll(QString(QStringLiteral("分类,标题")).toUtf8());
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0], (QStringList{QStringLiteral("分类"),
                                     QStringLiteral("标题")}));
}

TEST(CsvReader, EmptyFieldsPreserved) {
    const auto rows = CsvReader::ReadAll(",,\r\n");
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0], (QStringList{"", "", ""}));
}
