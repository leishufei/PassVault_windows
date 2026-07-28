#include <gtest/gtest.h>

#include <QList>
#include <QStringList>
#include <QVector>

#include "csv/csv_models.h"
#include "csv/csv_reader.h"
#include "csv/csv_validator.h"

using passvault::csv::CsvFormat;
using passvault::csv::CsvReader;
using passvault::csv::CsvValidator;
using passvault::csv::ExistingPasswordSnapshot;
using passvault::csv::ImportAction;

namespace {

QVector<QStringList> Parse(const QByteArray& data) {
    return CsvReader::ReadAll(data);
}

ExistingPasswordSnapshot MakeSnapshot(std::int64_t id, const QString& uuid,
                                      const QString& title,
                                      const QString& username,
                                      const QString& password,
                                      const QString& category = QStringLiteral("未分类")) {
    ExistingPasswordSnapshot s;
    s.id = id;
    s.uuid = uuid;
    s.title = title;
    s.username = username;
    s.password = password;
    s.category_name = category;
    return s;
}

}  // namespace

TEST(CsvValidator, DetectExtendedByIdHeader) {
    CsvValidator v;
    const auto rows = Parse("ID,分类,标题,用户名,密码,网站,备注\r\n");
    EXPECT_EQ(v.DetectFormat(rows), CsvFormat::kExtended);
}

TEST(CsvValidator, DetectExtendedByUuidFirstCell) {
    CsvValidator v;
    const auto rows = Parse(
        "11111111-2222-3333-4444-555555555555,cat,title,user,pw,site,notes");
    EXPECT_EQ(v.DetectFormat(rows), CsvFormat::kExtended);
}

TEST(CsvValidator, DetectLegacyByFourColumns) {
    CsvValidator v;
    const auto rows = Parse("分类,名称,用户名,密码");
    EXPECT_EQ(v.DetectFormat(rows), CsvFormat::kLegacy);
}

TEST(CsvValidator, DetectUnknownForTooFewColumns) {
    CsvValidator v;
    const auto rows = Parse("a,b,c");
    EXPECT_EQ(v.DetectFormat(rows), CsvFormat::kUnknown);
}

TEST(CsvValidator, HeaderRowSkipped) {
    CsvValidator v;
    QByteArray data;
    data.append("\xEF\xBB\xBF");
    data.append("ID,分类,标题,用户名,密码,网站,备注\r\n");
    data.append(",工作,Gmail,me@x.com,pw,g.com,note\r\n");
    const auto rows = Parse(data);
    const auto result = v.Validate(rows, {}, {});
    EXPECT_EQ(result.format, CsvFormat::kExtended);
    ASSERT_EQ(result.valid_rows.size(), 1);
    EXPECT_EQ(result.valid_rows[0].title, QStringLiteral("Gmail"));
    EXPECT_EQ(result.valid_rows[0].action, ImportAction::kInsert);
}

TEST(CsvValidator, MatchesExistingByUuid) {
    CsvValidator v;
    const QString uuid = QStringLiteral("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
    const QByteArray csv = QByteArray("ID,分类,标题,用户��,密码,网站,备注\r\n") +
                           uuid.toUtf8() +
                           ",工作,GmailNew,me@x.com,newpw,g.com,note\r\n";
    const auto rows = Parse(csv);
    const QList<ExistingPasswordSnapshot> existing = {MakeSnapshot(
        42, uuid, QStringLiteral("GmailOld"), QStringLiteral("me@x.com"),
        QStringLiteral("oldpw"), QStringLiteral("工作"))};
    const auto result = v.Validate(rows, existing, {QStringLiteral("工作")});
    ASSERT_EQ(result.valid_rows.size(), 1);
    EXPECT_EQ(result.valid_rows[0].action, ImportAction::kUpdate);
    EXPECT_EQ(result.valid_rows[0].existing_id.value_or(-1), 42);
    EXPECT_FALSE(result.valid_rows[0].diffs.isEmpty());
}

TEST(CsvValidator, FallbackMatchByTitleAndUsername) {
    CsvValidator v;
    const QByteArray csv =
        "ID,分类,标题,用户名,密码,网站,备注\r\n"
        ",工作,Gmail,me@x.com,newpw,g.com,\r\n";
    const auto rows = Parse(csv);
    const QList<ExistingPasswordSnapshot> existing = {
        MakeSnapshot(7, QStringLiteral(""), QStringLiteral("Gmail"),
                     QStringLiteral("me@x.com"), QStringLiteral("oldpw"),
                     QStringLiteral("工作"))};
    const auto result = v.Validate(rows, existing, {QStringLiteral("工作")});
    ASSERT_EQ(result.valid_rows.size(), 1);
    EXPECT_EQ(result.valid_rows[0].action, ImportAction::kUpdate);
    EXPECT_EQ(result.valid_rows[0].existing_id.value_or(-1), 7);
}

TEST(CsvValidator, EmptyTitleIsInvalid) {
    CsvValidator v;
    const auto rows = Parse(
        "ID,分类,标题,用户名,密码,网站,备注\r\n"
        ",cat,,user,pw,site,notes\r\n");
    const auto result = v.Validate(rows, {}, {});
    ASSERT_EQ(result.invalid_rows.size(), 1);
    EXPECT_EQ(result.invalid_rows[0].error_message,
              QStringLiteral("标题不能为空"));
}

TEST(CsvValidator, EmptyUsernameAndPasswordIsInvalid) {
    CsvValidator v;
    const auto rows = Parse(
        "ID,分类,标题,用户名,密码,网站,备注\r\n"
        ",cat,Gmail,,,site,notes\r\n");
    const auto result = v.Validate(rows, {}, {});
    ASSERT_EQ(result.invalid_rows.size(), 1);
    EXPECT_EQ(result.invalid_rows[0].error_message,
              QStringLiteral("用户名和密码不能同时为空"));
}

TEST(CsvValidator, TracksNewCategoriesExcludingUncategorized) {
    CsvValidator v;
    const auto rows = Parse(
        "ID,分类,标题,用户名,密码,网站,备注\r\n"
        ",工作,A,u1,p1,,\r\n"
        ",Uncategorized,B,u2,p2,,\r\n"
        ",,C,u3,p3,,\r\n");
    const auto result = v.Validate(rows, {}, {});
    EXPECT_EQ(result.new_category_names.size(), 2);
    EXPECT_TRUE(result.new_category_names.contains(QStringLiteral("工作")));
    EXPECT_TRUE(
        result.new_category_names.contains(QStringLiteral("Uncategorized")));
}

TEST(CsvValidator, LegacyFourColumnsInsertOnly) {
    CsvValidator v;
    const auto rows = Parse("工作,Gmail,me@x.com,pw");
    const auto result = v.Validate(rows, {}, {});
    EXPECT_EQ(result.format, CsvFormat::kLegacy);
    ASSERT_EQ(result.valid_rows.size(), 1);
    EXPECT_EQ(result.valid_rows[0].action, ImportAction::kInsert);
    EXPECT_EQ(result.valid_rows[0].category_name, QStringLiteral("工作"));
    EXPECT_TRUE(result.valid_rows[0].website.isEmpty());
    EXPECT_TRUE(result.valid_rows[0].notes.isEmpty());
}

TEST(CsvValidator, DiffOnlyChangedFields) {
    CsvValidator v;
    const QString uuid = QStringLiteral("11111111-2222-3333-4444-555555555555");
    const QByteArray csv = QByteArray("ID,分类,标题,用户名,密码,网站,备注\r\n") +
                           uuid.toUtf8() +
                           ",工作,Gmail,me@x.com,newpw,g.com,note\r\n";
    const auto rows = Parse(csv);
    const QList<ExistingPasswordSnapshot> existing = {MakeSnapshot(
        1, uuid, QStringLiteral("Gmail"), QStringLiteral("me@x.com"),
        QStringLiteral("oldpw"), QStringLiteral("工作"))};
    const auto result = v.Validate(rows, existing, {QStringLiteral("工作")});
    ASSERT_EQ(result.valid_rows.size(), 1);
    const auto& row = result.valid_rows[0];
    ASSERT_EQ(row.diffs.size(), 3);
    QStringList fields;
    for (const auto& d : row.diffs) fields << d.field;
    EXPECT_TRUE(fields.contains(QStringLiteral("密码")));
    EXPECT_TRUE(fields.contains(QStringLiteral("网站")));
    EXPECT_TRUE(fields.contains(QStringLiteral("备注")));
}
