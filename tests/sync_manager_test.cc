#include <gtest/gtest.h>

#include <QByteArray>
#include <QSignalSpy>
#include <QString>

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "crypto/crypto_service.h"
#include "crypto/random.h"
#include "crypto/secure_bytes.h"
#include "crypto/session_key.h"
#include "model/category.h"
#include "model/cloud_backup_format.h"
#include "model/password_entry.h"
#include "model/sync_payload_v2.h"
#include "session/session_manager.h"
#include "storage/category_dao.h"
#include "storage/database.h"
#include "storage/password_dao.h"
#include "storage/schema.h"
#include "sync/cloud_crypto_service.h"
#include "sync/cloud_storage_provider.h"
#include "sync/sync_manager.h"

namespace {

using passvault::crypto::CryptoService;
using passvault::crypto::Random;
using passvault::crypto::SecureBytes;
using passvault::crypto::SessionKey;
using passvault::model::Category;
using passvault::model::CloudBackupFormat;
using passvault::model::PasswordEntry;
using passvault::model::PasswordSyncItemV2;
using passvault::model::SyncPayloadV2;
using passvault::session::SessionManager;
using passvault::storage::CategoryDao;
using passvault::storage::Database;
using passvault::storage::EnsureCurrentSchema;
using passvault::storage::PasswordDao;
using passvault::sync::CloudCryptoService;
using passvault::sync::CloudDownloadResult;
using passvault::sync::CloudStorageProvider;
using passvault::sync::SyncManager;
using passvault::sync::UploadIfMatchStatus;

constexpr const char* kMasterPassword = "hunter2";

std::vector<std::uint8_t> FixedKeyBytes() {
    std::vector<std::uint8_t> bytes(SessionKey::kSize);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(i + 1);
    }
    return bytes;
}

SessionKey MakeFixedSessionKey() {
    SecureBytes bytes(SessionKey::kSize);
    const auto raw = FixedKeyBytes();
    std::memcpy(bytes.data(), raw.data(), raw.size());
    return std::move(*SessionKey::FromSecureBytes(std::move(bytes)));
}

SecureBytes MakeMasterPwdBytes() {
    SecureBytes b;
    b.AssignFromString(kMasterPassword);
    return b;
}

std::pair<QByteArray, QByteArray> EncryptWithFixedKey(const QString& plain) {
    const QByteArray pb = plain.toUtf8();
    const auto key = FixedKeyBytes();
    const auto iv = Random::Bytes(CryptoService::kIvSize);
    QByteArray ct = CryptoService::EncryptGcm(
        key.data(), key.size(), iv.data(), iv.size(),
        reinterpret_cast<const std::uint8_t*>(pb.constData()),
        static_cast<std::size_t>(pb.size()));
    return {ct, QByteArray(reinterpret_cast<const char*>(iv.data()),
                           static_cast<int>(iv.size()))};
}

PasswordEntry MakeEntry(int seed, const QString& plain_password,
                        std::int64_t updated_at) {
    PasswordEntry e;
    e.uuid = QStringLiteral("uuid-%1").arg(seed);
    e.title = QStringLiteral("title-%1").arg(seed);
    e.username = QStringLiteral("user-%1").arg(seed);
    auto ct_iv = EncryptWithFixedKey(plain_password);
    e.encrypted_password = ct_iv.first;
    e.password_iv = ct_iv.second;
    e.website = QStringLiteral("https://ex-%1.com").arg(seed);
    e.notes = QStringLiteral("notes-%1").arg(seed);
    e.is_favorite = false;
    e.icon_color = 0x300000;
    e.strength = 2;
    e.category_id = 0;
    e.created_at = 1700000000000LL + seed * 1000;
    e.updated_at = updated_at;
    e.is_deleted = false;
    return e;
}

// Test double: keeps the last uploaded blob and its version tag in-memory.
class FakeProvider : public CloudStorageProvider {
 public:
    QString ProviderName() const override { return QStringLiteral("fake"); }
    bool IsAuthenticated() const override { return true; }
    void SignOut() override {}

    bool UploadBackup(const QByteArray& data, const QString&,
                      QString*) override {
        ++upload_count_;
        data_ = data;
        version_ = NextVersion();
        return true;
    }

    std::optional<QByteArray> DownloadBackup(const QString&,
                                             QString* out_error) override {
        if (!data_) {
            if (out_error) *out_error = QStringLiteral("not found");
            return std::nullopt;
        }
        return *data_;
    }

    std::optional<CloudDownloadResult> DownloadBackupWithVersion(
        const QString&, QString* out_error) override {
        if (!data_) {
            if (out_error) *out_error = QStringLiteral("not found");
            return std::nullopt;
        }
        CloudDownloadResult r;
        r.data = *data_;
        r.version = version_;
        return r;
    }

    UploadIfMatchStatus UploadBackupIfMatch(const QByteArray& data,
                                            const QString&,
                                            const QString& expected_version,
                                            QString* out_error) override {
        if (always_mismatch_) {
            if (out_error) *out_error = QStringLiteral("simulated mismatch");
            return UploadIfMatchStatus::kVersionMismatch;
        }
        if (!data_) {
            data_ = data;
            version_ = NextVersion();
            ++upload_count_;
            return UploadIfMatchStatus::kSuccess;
        }
        if (expected_version != version_) {
            if (out_error) *out_error = QStringLiteral("version mismatch");
            return UploadIfMatchStatus::kVersionMismatch;
        }
        data_ = data;
        version_ = NextVersion();
        ++upload_count_;
        return UploadIfMatchStatus::kSuccess;
    }

    const std::optional<QByteArray>& data() const { return data_; }
    QString version() const { return version_; }
    int upload_count() const { return upload_count_; }
    void set_always_mismatch(bool v) { always_mismatch_ = v; }
    void SetCloudDirectly(const QByteArray& data, const QString& version) {
        data_ = data;
        version_ = version;
    }

 private:
    QString NextVersion() {
        return QStringLiteral("v%1").arg(++version_counter_);
    }

    std::optional<QByteArray> data_;
    QString version_;
    int version_counter_ = 0;
    int upload_count_ = 0;
    bool always_mismatch_ = false;
};

class SyncManagerTest : public ::testing::Test {
 protected:
    void SetUp() override {
        SessionManager::Instance()->ResetForTests();
        SessionManager::Instance()->Unlock(MakeFixedSessionKey(),
                                            MakeMasterPwdBytes());
        db_ = Database::OpenInMemory();
        EnsureCurrentSchema(*db_);
        pwd_dao_ = std::make_unique<PasswordDao>(*db_);
        cat_dao_ = std::make_unique<CategoryDao>(*db_);
        manager_ = std::make_unique<SyncManager>(
            pwd_dao_.get(), cat_dao_.get(), SessionManager::Instance());
    }
    void TearDown() override { SessionManager::Instance()->ResetForTests(); }

    std::optional<SyncPayloadV2> DecodeCloud(const FakeProvider& p) {
        if (!p.data()) return std::nullopt;
        auto backup = CloudBackupFormat::FromJsonBytes(*p.data());
        if (!backup) return std::nullopt;
        auto plain = CloudCryptoService::DecryptFromCloud(
            *backup, std::string_view(kMasterPassword));
        if (!plain) return std::nullopt;
        return SyncPayloadV2::FromJsonBytes(*plain);
    }

    std::unique_ptr<Database> db_;
    std::unique_ptr<PasswordDao> pwd_dao_;
    std::unique_ptr<CategoryDao> cat_dao_;
    std::unique_ptr<SyncManager> manager_;
    FakeProvider provider_;
};

}  // namespace

TEST_F(SyncManagerTest, NoProviderFailsFast) {
    QSignalSpy started(manager_.get(), &SyncManager::SyncStarted);
    QSignalSpy finished(manager_.get(), &SyncManager::SyncFinished);
    auto result = manager_->PerformSync();
    EXPECT_FALSE(result.success);
    EXPECT_EQ(started.count(), 1);
    EXPECT_EQ(finished.count(), 1);
}

TEST_F(SyncManagerTest, LockedSessionFailsFast) {
    SessionManager::Instance()->Lock();
    manager_->set_provider(&provider_);
    auto result = manager_->PerformSync();
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(provider_.data().has_value());
}

TEST_F(SyncManagerTest, EmptyCloudFirstSyncUploadsLocalPayload) {
    manager_->set_provider(&provider_);
    ASSERT_TRUE(pwd_dao_->Insert(MakeEntry(1, "pwd-1", 1710000000000LL)).has_value());
    ASSERT_TRUE(pwd_dao_->Insert(MakeEntry(2, "pwd-2", 1710000001000LL)).has_value());

    auto result = manager_->PerformSync();
    ASSERT_TRUE(result.success) << result.message.toStdString();
    ASSERT_EQ(provider_.upload_count(), 1);

    auto payload = DecodeCloud(provider_);
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(payload->version, SyncPayloadV2::kVersion);
    ASSERT_EQ(payload->passwords.size(), 2u);
    QString titles;
    for (const auto& p : payload->passwords) titles += p.title + "|";
    EXPECT_TRUE(titles.contains("title-1"));
    EXPECT_TRUE(titles.contains("title-2"));
}

TEST_F(SyncManagerTest, SecondSyncWithNoChangesSkipsUpload) {
    manager_->set_provider(&provider_);
    ASSERT_TRUE(pwd_dao_->Insert(MakeEntry(1, "pwd-1", 1710000000000LL)).has_value());
    ASSERT_TRUE(manager_->PerformSync().success);
    const int uploads_after_first = provider_.upload_count();
    ASSERT_EQ(uploads_after_first, 1);

    // Second run: local mirrors cloud, dedup no-op, nothing to upload.
    auto result = manager_->PerformSync();
    ASSERT_TRUE(result.success) << result.message.toStdString();
    EXPECT_EQ(provider_.upload_count(), uploads_after_first);
}

TEST_F(SyncManagerTest, VersionMismatchRetriesUpToMaxThenFails) {
    manager_->set_provider(&provider_);
    ASSERT_TRUE(pwd_dao_->Insert(MakeEntry(1, "pwd-1", 1710000000000LL)).has_value());
    ASSERT_TRUE(manager_->PerformSync().success);

    // Simulate a persistent concurrent modifier on cloud.
    provider_.set_always_mismatch(true);
    ASSERT_TRUE(pwd_dao_->Insert(MakeEntry(2, "pwd-2", 1710000001000LL)).has_value());

    QSignalSpy finished(manager_.get(), &SyncManager::SyncFinished);
    auto result = manager_->PerformSync(
        QString::fromLatin1(SyncManager::kDefaultRemoteFileName), 2);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.message.contains(QStringLiteral("并发冲突")));
    EXPECT_EQ(finished.count(), 1);
}

TEST_F(SyncManagerTest, CloudOnlyItemLandsLocal) {
    manager_->set_provider(&provider_);

    // Pre-populate cloud with one item that doesn't exist locally.
    SyncPayloadV2 cloud;
    cloud.version = SyncPayloadV2::kVersion;
    cloud.sync_timestamp = 1710000000000LL;
    PasswordSyncItemV2 remote;
    remote.uuid = QStringLiteral("remote-uuid");
    remote.title = QStringLiteral("remote-title");
    remote.username = QStringLiteral("remote-user");
    remote.password = QStringLiteral("remote-pwd");
    remote.category_uuid = QString{};
    remote.icon_color = 0;
    remote.created_at = 1710000000000LL;
    remote.updated_at = 1710000000000LL;
    cloud.passwords.push_back(remote);
    auto backup = CloudCryptoService::EncryptForCloud(
        cloud.ToJsonBytes(), std::string_view(kMasterPassword));
    ASSERT_TRUE(backup.has_value());
    provider_.SetCloudDirectly(backup->ToJsonBytes(), "v0");

    auto result = manager_->PerformSync();
    ASSERT_TRUE(result.success) << result.message.toStdString();

    auto local = pwd_dao_->FindByUuid("remote-uuid");
    ASSERT_TRUE(local.has_value());
    EXPECT_EQ(local->title, QStringLiteral("remote-title"));

    // Decrypt local password field to verify round-trip.
    const auto key = FixedKeyBytes();
    ASSERT_EQ(static_cast<std::size_t>(local->password_iv.size()),
              CryptoService::kIvSize);
    auto plain = CryptoService::DecryptGcm(
        key.data(), key.size(),
        reinterpret_cast<const std::uint8_t*>(local->password_iv.constData()),
        static_cast<std::size_t>(local->password_iv.size()),
        reinterpret_cast<const std::uint8_t*>(local->encrypted_password.constData()),
        static_cast<std::size_t>(local->encrypted_password.size()));
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(QString::fromUtf8(*plain), QStringLiteral("remote-pwd"));
}

TEST_F(SyncManagerTest, DedupByNameCollapsesDuplicateCategories) {
    manager_->set_provider(&provider_);

    Category older;
    older.uuid = QStringLiteral("cat-older");
    older.name = QStringLiteral("Work");
    older.color = 0x111;
    older.sort_order = 0;
    older.created_at = 1;
    older.updated_at = 1;
    ASSERT_TRUE(cat_dao_->Insert(older).has_value());
    const std::int64_t older_id = cat_dao_->FindByUuid("cat-older")->id;

    Category newer;
    newer.uuid = QStringLiteral("cat-newer");
    newer.name = QStringLiteral(" work ");  // trim + lowercase should collide
    newer.color = 0x222;
    newer.sort_order = 1;
    newer.created_at = 100;
    newer.updated_at = 100;
    ASSERT_TRUE(cat_dao_->Insert(newer).has_value());
    const std::int64_t newer_id = cat_dao_->FindByUuid("cat-newer")->id;

    // Put a password under the newer (loser) category.
    auto entry = MakeEntry(9, "p9", 1710000000000LL);
    entry.category_id = newer_id;
    ASSERT_TRUE(pwd_dao_->Insert(entry).has_value());

    auto result = manager_->PerformSync();
    ASSERT_TRUE(result.success) << result.message.toStdString();

    auto winner = cat_dao_->FindByUuid("cat-older");
    auto loser = cat_dao_->FindByUuid("cat-newer");
    ASSERT_TRUE(winner.has_value());
    ASSERT_TRUE(loser.has_value());
    EXPECT_FALSE(winner->is_deleted);
    EXPECT_TRUE(loser->is_deleted);

    auto migrated = pwd_dao_->FindByUuid(entry.uuid);
    ASSERT_TRUE(migrated.has_value());
    EXPECT_EQ(migrated->category_id, older_id);
}
