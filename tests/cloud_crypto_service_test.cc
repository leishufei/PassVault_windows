#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>
#include <string>

#include "model/cloud_backup_format.h"
#include "model/sync_payload_v2.h"
#include "sync/cloud_crypto_service.h"

namespace {

using passvault::model::CategorySyncItemV2;
using passvault::model::CloudBackupFormat;
using passvault::model::PasswordSyncItemV2;
using passvault::model::SyncPayloadV2;
using passvault::sync::CloudCryptoService;

SyncPayloadV2 MakeSamplePayload() {
    SyncPayloadV2 payload;
    payload.version = 2;
    payload.sync_timestamp = 1723456789012LL;

    PasswordSyncItemV2 p1;
    p1.uuid = "11111111-1111-1111-1111-111111111111";
    p1.title = "GitHub";
    p1.username = "octocat";
    p1.password = "hunter2!@#";
    p1.website = "https://github.com";
    p1.notes = "primary dev account";
    p1.category_uuid = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
    p1.is_favorite = true;
    p1.icon_color = 0xff2196f3;
    p1.created_at = 1700000000000LL;
    p1.updated_at = 1720000000000LL;
    p1.is_deleted = false;

    PasswordSyncItemV2 p2;
    p2.uuid = "22222222-2222-2222-2222-222222222222";
    p2.title = QString::fromUtf8("邮箱");
    p2.username = "user@example.com";
    p2.password = QString::fromUtf8("p@ssw0rd 中文密码 🔐");
    p2.website = "https://mail.example.com";
    p2.notes = "";
    p2.category_uuid = "";  // uncategorized
    p2.is_favorite = false;
    p2.icon_color = 0;
    p2.created_at = 1710000000000LL;
    p2.updated_at = 1720000000000LL;
    p2.is_deleted = true;

    CategorySyncItemV2 c1;
    c1.uuid = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
    c1.name = "Work";
    c1.color = 0xff4caf50;
    c1.sort_order = 0;
    c1.created_at = 1699000000000LL;
    c1.updated_at = 1720000000000LL;
    c1.is_deleted = false;

    payload.passwords = {p1, p2};
    payload.categories = {c1};
    return payload;
}

}  // namespace

TEST(CloudCryptoService, RoundTrip_SamplePayload) {
    const SyncPayloadV2 original = MakeSamplePayload();
    const QByteArray plaintext_json = original.ToJsonBytes();
    ASSERT_FALSE(plaintext_json.isEmpty());

    const auto backup = CloudCryptoService::EncryptForCloud(plaintext_json, "correct horse battery staple");
    ASSERT_TRUE(backup.has_value());

    EXPECT_EQ(backup->version, CloudBackupFormat::kVersion);
    EXPECT_EQ(backup->iterations, CloudBackupFormat::kIterations);
    EXPECT_EQ(backup->kdf_algorithm, QString::fromLatin1(CloudBackupFormat::kKdfAlgorithm));
    EXPECT_EQ(backup->encryption_algorithm, QString::fromLatin1(CloudBackupFormat::kEncryptionAlgorithm));
    EXPECT_FALSE(backup->salt.isEmpty());
    EXPECT_FALSE(backup->iv.isEmpty());
    EXPECT_FALSE(backup->ciphertext.isEmpty());

    const auto recovered_json = CloudCryptoService::DecryptFromCloud(*backup, "correct horse battery staple");
    ASSERT_TRUE(recovered_json.has_value());
    EXPECT_EQ(*recovered_json, plaintext_json);

    const auto recovered = SyncPayloadV2::FromJsonBytes(*recovered_json);
    ASSERT_TRUE(recovered.has_value());
    EXPECT_EQ(recovered->version, original.version);
    EXPECT_EQ(recovered->sync_timestamp, original.sync_timestamp);
    ASSERT_EQ(recovered->passwords.size(), original.passwords.size());
    ASSERT_EQ(recovered->categories.size(), original.categories.size());
    EXPECT_EQ(recovered->passwords[0].title, original.passwords[0].title);
    EXPECT_EQ(recovered->passwords[0].password, original.passwords[0].password);
    EXPECT_EQ(recovered->passwords[1].password, original.passwords[1].password);
    EXPECT_EQ(recovered->passwords[1].is_deleted, original.passwords[1].is_deleted);
    EXPECT_EQ(recovered->categories[0].name, original.categories[0].name);
}

TEST(CloudCryptoService, WrongPassword_ReturnsNullopt) {
    const auto backup = CloudCryptoService::EncryptForCloud(
        QByteArray("{\"hello\":\"world\"}"),
        "master-password");
    ASSERT_TRUE(backup.has_value());

    const auto recovered = CloudCryptoService::DecryptFromCloud(*backup, "wrong-password");
    EXPECT_FALSE(recovered.has_value());
}

TEST(CloudCryptoService, UnsupportedKdfAlgorithm_ReturnsNullopt) {
    auto backup = CloudCryptoService::EncryptForCloud(
        QByteArray("{\"hello\":\"world\"}"),
        "master-password");
    ASSERT_TRUE(backup.has_value());

    backup->kdf_algorithm = "PBKDF2";
    const auto recovered = CloudCryptoService::DecryptFromCloud(*backup, "master-password");
    EXPECT_FALSE(recovered.has_value());
}

TEST(CloudCryptoService, UnsupportedEncryptionAlgorithm_ReturnsNullopt) {
    auto backup = CloudCryptoService::EncryptForCloud(
        QByteArray("{\"hello\":\"world\"}"),
        "master-password");
    ASSERT_TRUE(backup.has_value());

    backup->encryption_algorithm = "AES-256-CBC";
    const auto recovered = CloudCryptoService::DecryptFromCloud(*backup, "master-password");
    EXPECT_FALSE(recovered.has_value());
}

TEST(CloudCryptoService, TamperedCiphertext_ReturnsNullopt) {
    auto backup = CloudCryptoService::EncryptForCloud(
        QByteArray("payload-bytes-here"),
        "master-password");
    ASSERT_TRUE(backup.has_value());

    QByteArray raw = QByteArray::fromBase64(backup->ciphertext.toLatin1());
    ASSERT_FALSE(raw.isEmpty());
    raw[0] = static_cast<char>(raw[0] ^ 0x01);
    backup->ciphertext = QString::fromLatin1(raw.toBase64());

    const auto recovered = CloudCryptoService::DecryptFromCloud(*backup, "master-password");
    EXPECT_FALSE(recovered.has_value());
}

TEST(CloudCryptoService, BadSaltSize_ReturnsNullopt) {
    auto backup = CloudCryptoService::EncryptForCloud(
        QByteArray("payload"),
        "master-password");
    ASSERT_TRUE(backup.has_value());

    backup->salt = QString::fromLatin1(QByteArray(8, '\0').toBase64());  // 8 bytes, wrong size
    const auto recovered = CloudCryptoService::DecryptFromCloud(*backup, "master-password");
    EXPECT_FALSE(recovered.has_value());
}

TEST(CloudCryptoService, EmptyPayload_RoundTrips) {
    const QByteArray empty;
    const auto backup = CloudCryptoService::EncryptForCloud(empty, "pw");
    ASSERT_TRUE(backup.has_value());
    const auto recovered = CloudCryptoService::DecryptFromCloud(*backup, "pw");
    ASSERT_TRUE(recovered.has_value());
    EXPECT_EQ(*recovered, empty);
}
