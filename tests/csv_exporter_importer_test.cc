#include <gtest/gtest.h>

#include <QBuffer>
#include <QByteArray>
#include <QString>

#include <algorithm>
#include <memory>
#include <utility>

#include "crypto/crypto_service.h"
#include "crypto/secure_bytes.h"
#include "crypto/session_key.h"
#include "csv/csv_exporter.h"
#include "csv/csv_importer.h"
#include "csv/csv_models.h"
#include "csv/csv_reader.h"
#include "csv/csv_validator.h"
#include "model/category.h"
#include "storage/category_dao.h"
#include "storage/database.h"
#include "storage/password_dao.h"
#include "storage/schema.h"

using passvault::crypto::CryptoService;
using passvault::crypto::SecureBytes;
using passvault::crypto::SessionKey;
using passvault::csv::CsvExporter;
using passvault::csv::CsvImporter;
using passvault::csv::CsvReader;
using passvault::csv::CsvValidator;
using passvault::csv::ExistingPasswordSnapshot;
using passvault::csv::ExportEntry;
using passvault::csv::ImportAction;
using passvault::storage::CategoryDao;
using passvault::storage::Database;
using passvault::storage::EnsureCurrentSchema;
using passvault::storage::PasswordDao;

namespace {

SessionKey FixedKey() {
    SecureBytes bytes(SessionKey::kSize);
    for (std::size_t i = 0; i < SessionKey::kSize; ++i) {
        bytes.data()[i] = static_cast<std::uint8_t>(0x10 + i);
    }
    auto k = SessionKey::FromSecureBytes(std::move(bytes));
    return std::move(*k);
}

QString DecryptField(const SessionKey& key, const QByteArray& iv,
                     const QByteArray& ct_and_tag) {
    const auto pt = CryptoService::DecryptGcm(
        key.data(), key.size(),
        reinterpret_cast<const std::uint8_t*>(iv.constData()),
        static_cast<std::size_t>(iv.size()),
        reinterpret_cast<const std::uint8_t*>(ct_and_tag.constData()),
        static_cast<std::size_t>(ct_and_tag.size()));
    return pt.has_value() ? QString::fromUtf8(*pt) : QString();
}

class CsvRoundTripTest : public ::testing::Test {
 protected:
    void SetUp() override {
        db_ = Database::OpenInMemory();
        EnsureCurrentSchema(*db_);
        pwd_dao_ = std::make_unique<PasswordDao>(*db_);
        cat_dao_ = std::make_unique<CategoryDao>(*db_);
    }

    std::unique_ptr<Database> db_;
    std::unique_ptr<PasswordDao> pwd_dao_;
    std::unique_ptr<CategoryDao> cat_dao_;
};

}  // namespace

TEST_F(CsvRoundTripTest, ImportInsertsRowsAndCreatesCategories) {
    const QByteArray csv =
        "ID,分类,标题,用户名,密码,网站,备注\r\n"
        ",工作,Gmail,me@x.com,pw-gmail,g.com,note1\r\n"
        ",个人,Wifi,home,pw-wifi,,note2\r\n";
    const auto rows = CsvReader::ReadAll(csv);
    const auto validation = CsvValidator().Validate(rows, {}, {});
    ASSERT_EQ(validation.valid_rows.size(), 2);

    auto key = FixedKey();
    const auto summary = CsvImporter::Apply(validation, *pwd_dao_, *cat_dao_,
                                            key, 1'700'000'000'000LL);
    ASSERT_TRUE(summary.has_value());
    EXPECT_EQ(summary->inserted, 2);
    EXPECT_EQ(summary->updated, 0);
    EXPECT_EQ(summary->created_categories.size(), 2);

    const auto stored = pwd_dao_->ListActive();
    ASSERT_EQ(stored.size(), 2u);
    for (const auto& e : stored) {
        EXPECT_EQ(e.password_iv.size(), 12);
        EXPECT_NE(e.category_id, 0);
        const QString pt = DecryptField(key, e.password_iv, e.encrypted_password);
        EXPECT_TRUE(pt == QStringLiteral("pw-gmail") ||
                    pt == QStringLiteral("pw-wifi"));
    }
}

TEST_F(CsvRoundTripTest, ImportUpdatesByUuidAndEncryptsNewPassword) {
    const QString uuid = QStringLiteral("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");

    passvault::model::Category work;
    work.name = QStringLiteral("工作");
    work.uuid = QStringLiteral("cat-work");
    work.color = 1;
    work.sort_order = 0;
    work.created_at = 1'699'000'000'000LL;
    work.updated_at = 1'699'000'000'000LL;
    const auto work_id = cat_dao_->Insert(work);
    ASSERT_TRUE(work_id.has_value());

    auto key = FixedKey();
    const QByteArray iv_old = QByteArrayLiteral("\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B");
    const QByteArray pt_old = QByteArrayLiteral("oldpw");
    const QByteArray ct_old = CryptoService::EncryptGcm(
        key.data(), key.size(),
        reinterpret_cast<const std::uint8_t*>(iv_old.constData()),
        static_cast<std::size_t>(iv_old.size()),
        reinterpret_cast<const std::uint8_t*>(pt_old.constData()),
        static_cast<std::size_t>(pt_old.size()));
    ASSERT_FALSE(ct_old.isEmpty());

    passvault::model::PasswordEntry existing;
    existing.uuid = uuid;
    existing.title = QStringLiteral("Gmail");
    existing.username = QStringLiteral("me@x.com");
    existing.encrypted_password = ct_old;
    existing.password_iv = iv_old;
    existing.website = QStringLiteral("g.com");
    existing.notes = QStringLiteral("old-note");
    existing.category_id = *work_id;
    existing.created_at = 1'699'000'000'000LL;
    existing.updated_at = 1'699'000'000'000LL;
    const auto pwd_id = pwd_dao_->Insert(existing);
    ASSERT_TRUE(pwd_id.has_value());

    const QByteArray csv = QByteArray("ID,分类,标题,用户名,密码,网站,备注\r\n") +
                           uuid.toUtf8() +
                           ",工作,Gmail,me@x.com,newpw,g.com,new-note\r\n";
    const auto rows = CsvReader::ReadAll(csv);

    QList<ExistingPasswordSnapshot> snaps;
    ExistingPasswordSnapshot s;
    s.id = *pwd_id;
    s.uuid = uuid;
    s.title = existing.title;
    s.username = existing.username;
    s.password = QStringLiteral("oldpw");
    s.website = existing.website;
    s.notes = existing.notes;
    s.category_name = QStringLiteral("工作");
    snaps.append(s);

    const auto validation =
        CsvValidator().Validate(rows, snaps, {QStringLiteral("工作")});
    ASSERT_EQ(validation.valid_rows.size(), 1);
    EXPECT_EQ(validation.valid_rows[0].action, ImportAction::kUpdate);

    const auto summary = CsvImporter::Apply(validation, *pwd_dao_, *cat_dao_,
                                            key, 1'700'000'000'000LL);
    ASSERT_TRUE(summary.has_value());
    EXPECT_EQ(summary->updated, 1);
    EXPECT_EQ(summary->inserted, 0);
    EXPECT_TRUE(summary->created_categories.isEmpty());

    const auto after = pwd_dao_->FindById(*pwd_id);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->notes, QStringLiteral("new-note"));
    EXPECT_EQ(after->updated_at, 1'700'000'000'000LL);
    EXPECT_EQ(after->created_at, 1'699'000'000'000LL);
    const QString decrypted =
        DecryptField(key, after->password_iv, after->encrypted_password);
    EXPECT_EQ(decrypted, QStringLiteral("newpw"));
}

TEST(CsvExporter, WritesBomHeaderAndSortsRows) {
    QList<ExportEntry> entries;
    ExportEntry e1;
    e1.uuid = QStringLiteral("aaaa");
    e1.category_name = QStringLiteral("工作");
    e1.title = QStringLiteral("Gmail");
    e1.username = QStringLiteral("me");
    e1.password = QStringLiteral("pw1");
    e1.category_sort_order = 2;
    e1.category_id = 10;
    e1.created_at = 200;
    entries.append(e1);

    ExportEntry e2;
    e2.uuid = QStringLiteral("bbbb");
    e2.category_name = QStringLiteral("未分类");
    e2.title = QStringLiteral("Wifi");
    e2.username = QStringLiteral("home");
    e2.password = QStringLiteral("pw2");
    e2.category_sort_order = 0;
    e2.category_id = 0;
    e2.created_at = 100;
    entries.append(e2);

    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    ASSERT_TRUE(CsvExporter::Export(&buf, entries));
    buf.close();

    QByteArray data = buf.data();
    ASSERT_GE(data.size(), 3);
    EXPECT_EQ(static_cast<std::uint8_t>(data[0]), 0xEF);
    EXPECT_EQ(static_cast<std::uint8_t>(data[1]), 0xBB);
    EXPECT_EQ(static_cast<std::uint8_t>(data[2]), 0xBF);

    const auto rows = CsvReader::ReadAll(data);
    ASSERT_EQ(rows.size(), 3);
    EXPECT_EQ(rows[0], CsvValidator::ExtendedHeaders());
    EXPECT_EQ(rows[1][2], QStringLiteral("Wifi"));
    EXPECT_EQ(rows[2][2], QStringLiteral("Gmail"));
}
