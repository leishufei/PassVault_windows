#include <gtest/gtest.h>
#include <QDateTime>

#include <QByteArray>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <cstdint>
#include <cstring>
#include <functional>
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
#include "sync/sync_scheduler.h"

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
        if (on_upload) std::exchange(on_upload, {})();
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

    passvault::sync::CloudReadResult ReadBackup(const QString& name, QString* error) override {
        if (on_read) std::exchange(on_read, {})();
        auto result = DownloadBackupWithVersion(name, error);
        if (!result) return {passvault::sync::CloudReadStatus::kMissing, {}};
        return {passvault::sync::CloudReadStatus::kFound, *result};
    }

    UploadIfMatchStatus UploadBackupIfMatch(const QByteArray& data,
                                            const QString&,
                                            const QString& expected_version,
                                            QString* out_error) override {
        if (on_upload) std::exchange(on_upload, {})();
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

    std::function<void()> on_read;
    std::function<void()> on_upload;

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

    void CheckPermanentDeletionDuringRead(bool old_cloud);
    void CheckFullReadFailure(const char* table, int read_to_deny,
                              int expected_category_scans,
                              bool delete_during_read = false);

    std::unique_ptr<Database> db_;
    std::unique_ptr<PasswordDao> pwd_dao_;
    std::unique_ptr<CategoryDao> cat_dao_;
    std::unique_ptr<SyncManager> manager_;
    FakeProvider provider_;
};

}  // namespace

void SyncManagerTest::CheckFullReadFailure(
    const char* table, int read_to_deny, int expected_category_scans,
    bool delete_during_read) {
    manager_->set_provider(&provider_);
    const auto now = QDateTime::currentMSecsSinceEpoch();
    Category category;
    category.uuid = QStringLiteral("read-category");
    category.name = QStringLiteral("Read category");
    category.created_at = now;
    category.updated_at = now;
    const auto category_id = cat_dao_->Insert(category);
    ASSERT_TRUE(category_id);
    auto active = MakeEntry(71, QStringLiteral("synthetic"), now);
    active.category_id = *category_id;
    ASSERT_TRUE(pwd_dao_->Insert(active));
    auto trash = MakeEntry(72, QStringLiteral("synthetic"), now);
    trash.is_deleted = true;
    const auto trash_id = pwd_dao_->Insert(trash);
    ASSERT_TRUE(trash_id);
    SyncPayloadV2 cloud;
    for (const auto& entry : {active, trash}) {
        PasswordSyncItemV2 item;
        item.uuid = entry.uuid;
        item.title = entry.title;
        item.username = entry.username;
        item.password = QStringLiteral("synthetic");
        item.created_at = entry.created_at;
        item.updated_at = entry.updated_at;
        item.is_deleted = entry.is_deleted;
        cloud.passwords.push_back(item);
    }
    const auto backup = CloudCryptoService::EncryptForCloud(
        cloud.ToJsonBytes(), kMasterPassword);
    ASSERT_TRUE(backup);
    const auto original_cloud = backup->ToJsonBytes();
    provider_.SetCloudDirectly(original_cloud, QStringLiteral("original"));

    struct ReadFailure {
        const char* table;
        int target;
        int reads = 0;
        int denied = 0;
        int category_scans = 0;
        int scans_at_denial = -1;
        int password_uuid_reads = 0;
        int uuid_reads_at_denial = -1;
    } failure{table, read_to_deny};
    ASSERT_EQ(sqlite3_trace_v2(
        db_->handle(), SQLITE_TRACE_STMT,
        [](unsigned, void* context, void* statement, void*) {
            auto& state = *static_cast<ReadFailure*>(context);
            const std::string_view sql =
                sqlite3_sql(static_cast<sqlite3_stmt*>(statement));
            if (sql.find("SELECT ") == 0 &&
                sql.find(" FROM categories") != std::string_view::npos &&
                sql.find(" WHERE ") == std::string_view::npos) {
                ++state.category_scans;
            }
            if (sql.find("SELECT ") == 0 &&
                sql.find(" FROM password_entries WHERE uuid = ?") !=
                    std::string_view::npos) {
                ++state.password_uuid_reads;
            }
            return 0;
        }, &failure), SQLITE_OK);
    const auto install_failure = [&] {
        EXPECT_EQ(sqlite3_set_authorizer(
            db_->handle(),
            [](void* context, int action, const char* table_name,
               const char* column, const char*, const char*) {
                auto& state = *static_cast<ReadFailure*>(context);
                if (action == SQLITE_READ && table_name && column &&
                    std::strcmp(table_name, state.table) == 0 &&
                    std::strcmp(column, "id") == 0 &&
                    ++state.reads == state.target) {
                    ++state.denied;
                    state.scans_at_denial = state.category_scans;
                    state.uuid_reads_at_denial = state.password_uuid_reads;
                    return SQLITE_DENY;
                }
                return SQLITE_OK;
            }, &failure), SQLITE_OK);
    };
    bool cloud_read = false;
    bool deleted = false;
    provider_.on_read = [&] {
        cloud_read = true;
        if (delete_during_read) {
            EXPECT_EQ(pwd_dao_->BulkPermanentlyDelete({*trash_id}, now), 1);
            deleted = true;
            install_failure();
        }
    };
    if (!delete_during_read) install_failure();
    const auto result = manager_->PerformSync();
    EXPECT_EQ(sqlite3_set_authorizer(db_->handle(), nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(sqlite3_trace_v2(db_->handle(), 0, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(failure.denied, 1);
    EXPECT_EQ(failure.scans_at_denial, expected_category_scans);
    EXPECT_EQ(cloud_read, expected_category_scans != 0);
    if (delete_during_read) {
        EXPECT_TRUE(deleted);
        EXPECT_EQ(failure.uuid_reads_at_denial, 2);
        EXPECT_FALSE(pwd_dao_->FindByUuid(trash.uuid));
        const auto markers = pwd_dao_->ListDeletionMarkers();
        ASSERT_TRUE(markers);
        ASSERT_EQ(markers->size(), 1u);
        EXPECT_EQ(markers->front().uuid, trash.uuid);
        EXPECT_EQ(markers->front().deleted_at, now);
    }
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.kind, SyncManager::StatusKind::kError);
    EXPECT_EQ(provider_.upload_count(), 0);
    ASSERT_TRUE(provider_.data());
    EXPECT_TRUE(*provider_.data() == original_cloud);
    EXPECT_EQ(provider_.version(), QStringLiteral("original"));
    const auto preserved = pwd_dao_->FindByUuid(active.uuid);
    ASSERT_TRUE(preserved);
    EXPECT_TRUE(preserved->encrypted_password == active.encrypted_password);
    EXPECT_EQ(preserved->category_id, *category_id);
}

TEST_F(SyncManagerTest, FinalPasswordReadFailureDoesNotUploadPartialBackup) {
    CheckFullReadFailure("password_entries", 3, 4, true);
}

TEST_F(SyncManagerTest, FinalCategoryReadFailureDoesNotUploadPartialBackup) {
    CheckFullReadFailure("categories", 4, 3, true);
}

TEST_F(SyncManagerTest, InitialPasswordReadFailureStopsBeforeCloudRead) {
    CheckFullReadFailure("password_entries", 1, 0);
}

TEST_F(SyncManagerTest, InitialCategoryReadFailureStopsBeforeCloudRead) {
    CheckFullReadFailure("categories", 1, 0);
}

TEST_F(SyncManagerTest, MergedCategoryReadFailureStopsSync) {
    CheckFullReadFailure("categories", 3, 1);
}

TEST_F(SyncManagerTest, DedupCategoryReadFailureStopsSync) {
    CheckFullReadFailure("categories", 4, 2);
}

TEST_F(SyncManagerTest, LegitimatelyEmptyLocalAndCloudSyncSucceeds) {
    manager_->set_provider(&provider_);
    const auto passwords = pwd_dao_->ReadAll();
    ASSERT_TRUE(passwords);
    ASSERT_TRUE(passwords->empty());
    ASSERT_TRUE(db_->Execute("DELETE FROM categories"));
    ASSERT_TRUE(manager_->PerformSync().success);
    EXPECT_EQ(provider_.upload_count(), 1);
    const auto cloud = DecodeCloud(provider_);
    ASSERT_TRUE(cloud);
    EXPECT_TRUE(cloud->passwords.empty());
    EXPECT_TRUE(cloud->categories.empty());
}

TEST_F(SyncManagerTest, FullReadsPreserveCategoryReferenceInUploadedPayload) {
    manager_->set_provider(&provider_);
    Category category;
    category.uuid = QStringLiteral("category-reference");
    category.name = QStringLiteral("Reference");
    category.created_at = QDateTime::currentMSecsSinceEpoch();
    category.updated_at = category.created_at;
    const auto id = cat_dao_->Insert(category);
    ASSERT_TRUE(id);
    auto entry = MakeEntry(73, QStringLiteral("synthetic"), category.updated_at);
    entry.category_id = *id;
    ASSERT_TRUE(pwd_dao_->Insert(entry));
    ASSERT_TRUE(manager_->PerformSync().success);
    const auto cloud = DecodeCloud(provider_);
    ASSERT_TRUE(cloud);
    ASSERT_EQ(cloud->passwords.size(), 1u);
    EXPECT_EQ(cloud->passwords.front().category_uuid, category.uuid);
    bool found_category = false;
    for (const auto& item : cloud->categories) {
        if (item.uuid == category.uuid) {
            found_category = true;
            EXPECT_EQ(item.name, category.name);
        }
    }
    EXPECT_TRUE(found_category);
}

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

TEST_F(SyncManagerTest, KindIsSuccessOnCleanSync) {
    manager_->set_provider(&provider_);
    ASSERT_TRUE(pwd_dao_->Insert(MakeEntry(1, "pwd-1", 1710000000000LL)).has_value());

    auto result = manager_->PerformSync();
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.kind, SyncManager::StatusKind::kSuccess);
}

TEST_F(SyncManagerTest, KindIsErrorWithoutProvider) {
    auto result = manager_->PerformSync();
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.kind, SyncManager::StatusKind::kError);
}

TEST_F(SyncManagerTest, KindIsConflictAfterRetriesExhausted) {
    manager_->set_provider(&provider_);
    ASSERT_TRUE(pwd_dao_->Insert(MakeEntry(1, "pwd-1", 1710000000000LL)).has_value());
    ASSERT_TRUE(manager_->PerformSync().success);

    provider_.set_always_mismatch(true);
    ASSERT_TRUE(pwd_dao_->Insert(MakeEntry(2, "pwd-2", 1710000001000LL)).has_value());

    auto result = manager_->PerformSync(
        QString::fromLatin1(SyncManager::kDefaultRemoteFileName), 2);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.kind, SyncManager::StatusKind::kConflict);
}

TEST_F(SyncManagerTest, ConflictMessageHasNoFailurePrefix) {
    manager_->set_provider(&provider_);
    ASSERT_TRUE(pwd_dao_->Insert(MakeEntry(1, "pwd-1", 1710000000000LL)).has_value());
    ASSERT_TRUE(manager_->PerformSync().success);

    provider_.set_always_mismatch(true);
    ASSERT_TRUE(pwd_dao_->Insert(MakeEntry(2, "pwd-2", 1710000001000LL)).has_value());

    auto result = manager_->PerformSync(
        QString::fromLatin1(SyncManager::kDefaultRemoteFileName), 2);
    EXPECT_FALSE(result.message.startsWith(QStringLiteral("同步失败")));
    EXPECT_TRUE(result.message.contains(QStringLiteral("并发冲突")));
}

TEST_F(SyncManagerTest, SyncFinishedCarriesKind) {
    manager_->set_provider(&provider_);
    ASSERT_TRUE(pwd_dao_->Insert(MakeEntry(1, "pwd-1", 1710000000000LL)).has_value());

    QSignalSpy finished(manager_.get(), &SyncManager::SyncFinished);
    auto result = manager_->PerformSync();
    ASSERT_EQ(finished.count(), 1);
    const auto emitted =
        finished.at(0).at(0).value<SyncManager::StatusKind>();
    EXPECT_EQ(emitted, result.kind);
}

TEST_F(SyncManagerTest, PermanentMarkerWinsOlderAndEqualCloudButNewerCloudRestores) {
    manager_->set_provider(&provider_);
    const auto now = QDateTime::currentMSecsSinceEpoch();
    for (const auto offset : {-1, 0, 1}) {
        ASSERT_TRUE(pwd_dao_->ApplyDeletionMarker(QStringLiteral("uuid-1"), now));
        PasswordSyncItemV2 cloud;
        cloud.uuid = QStringLiteral("uuid-1");
        cloud.title = QStringLiteral("cloud");
        cloud.updated_at = now + offset;
        cloud.created_at = now - 100;
        SyncPayloadV2 payload;
        payload.passwords.push_back(cloud);
        auto backup = CloudCryptoService::EncryptForCloud(payload.ToJsonBytes(), kMasterPassword);
        ASSERT_TRUE(backup);
        provider_.SetCloudDirectly(backup->ToJsonBytes(), QStringLiteral("cloud-version"));
        const auto result = manager_->PerformSync();
        ASSERT_TRUE(result.success) << result.message.toStdString();
        const auto entry = pwd_dao_->FindByUuid(cloud.uuid);
        if (offset <= 0) {
            EXPECT_FALSE(entry);
            ASSERT_EQ(pwd_dao_->ListDeletionMarkers()->size(), 1u);
            const auto uploaded = DecodeCloud(provider_);
            ASSERT_TRUE(uploaded);
            ASSERT_EQ(uploaded->passwords.size(), 1u);
            EXPECT_TRUE(uploaded->passwords.front().is_deleted);
            EXPECT_TRUE(uploaded->passwords.front().title.isEmpty());
            EXPECT_EQ(uploaded->passwords.front().created_at, 0);
        } else {
            ASSERT_TRUE(entry);
            EXPECT_FALSE(entry->is_deleted);
            EXPECT_TRUE(pwd_dao_->ListDeletionMarkers()->empty());
            EXPECT_EQ(entry->encrypted_password.size(), CryptoService::kTagSize);
            EXPECT_EQ(entry->password_iv.size(), CryptoService::kIvSize);
        }
    }
}

TEST_F(SyncManagerTest, ReceivedContentlessTombstoneRemovesContentWithoutBlankTrashRow) {
    manager_->set_provider(&provider_);
    const auto now = QDateTime::currentMSecsSinceEpoch();
    ASSERT_TRUE(pwd_dao_->Insert(MakeEntry(1, QString(), now - 1)));
    SyncPayloadV2 payload;
    PasswordSyncItemV2 marker;
    marker.uuid = QStringLiteral("uuid-1");
    marker.updated_at = now;
    marker.is_deleted = true;
    payload.passwords.push_back(marker);
    auto backup = CloudCryptoService::EncryptForCloud(payload.ToJsonBytes(), kMasterPassword);
    ASSERT_TRUE(backup);
    provider_.SetCloudDirectly(backup->ToJsonBytes(), QStringLiteral("remote"));
    ASSERT_TRUE(manager_->PerformSync().success);
    EXPECT_TRUE(pwd_dao_->ListIncludingDeleted().empty());
    EXPECT_FALSE(pwd_dao_->FindByUuid(marker.uuid));
    EXPECT_TRUE(pwd_dao_->Search(QStringLiteral("title")).empty());
    ASSERT_EQ(pwd_dao_->ListDeletionMarkers()->size(), 1u);
    EXPECT_EQ(pwd_dao_->ListDeletionMarkers()->front().deleted_at, now);
}

TEST_F(SyncManagerTest, DeletedEmptyPasswordWithContentRemainsARealEncryptedTrashEntry) {
    manager_->set_provider(&provider_);
    SyncPayloadV2 payload;
    PasswordSyncItemV2 entry;
    entry.uuid = QStringLiteral("empty-password");
    entry.title = QStringLiteral("legitimate");
    entry.is_deleted = true;
    entry.created_at = QDateTime::currentMSecsSinceEpoch();
    entry.updated_at = entry.created_at;
    payload.passwords.push_back(entry);
    auto backup = CloudCryptoService::EncryptForCloud(payload.ToJsonBytes(), kMasterPassword);
    ASSERT_TRUE(backup);
    provider_.SetCloudDirectly(backup->ToJsonBytes(), QStringLiteral("remote"));
    ASSERT_TRUE(manager_->PerformSync().success);
    const auto stored = pwd_dao_->FindByUuid(entry.uuid);
    ASSERT_TRUE(stored);
    EXPECT_TRUE(stored->is_deleted);
    EXPECT_EQ(stored->encrypted_password.size(), CryptoService::kTagSize);
    EXPECT_EQ(stored->password_iv.size(), CryptoService::kIvSize);
    EXPECT_TRUE(pwd_dao_->ListDeletionMarkers()->empty());
}

TEST_F(SyncManagerTest, ReceivedMarkerWriteFailureKeepsOriginalContentAndReportsFailure) {
    manager_->set_provider(&provider_);
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto original = MakeEntry(1, QStringLiteral("synthetic"), now - 1);
    ASSERT_TRUE(pwd_dao_->Insert(original));
    ASSERT_TRUE(db_->Execute("CREATE TRIGGER fail_remove BEFORE DELETE ON password_entries "
                            "BEGIN SELECT RAISE(ABORT,'injected'); END"));
    SyncPayloadV2 payload;
    PasswordSyncItemV2 marker;
    marker.uuid = original.uuid;
    marker.is_deleted = true;
    marker.updated_at = now;
    payload.passwords.push_back(marker);
    const auto backup = CloudCryptoService::EncryptForCloud(payload.ToJsonBytes(), kMasterPassword);
    ASSERT_TRUE(backup);
    provider_.SetCloudDirectly(backup->ToJsonBytes(), QStringLiteral("remote"));
    EXPECT_FALSE(manager_->PerformSync().success);
    const auto preserved = pwd_dao_->FindByUuid(original.uuid);
    ASSERT_TRUE(preserved);
    EXPECT_EQ(preserved->encrypted_password, original.encrypted_password);
    EXPECT_TRUE(pwd_dao_->ListDeletionMarkers()->empty());
    EXPECT_EQ(provider_.upload_count(), 0);
}

void SyncManagerTest::CheckPermanentDeletionDuringRead(bool old_cloud) {
    manager_->set_provider(&provider_);
    const auto now = QDateTime::currentMSecsSinceEpoch();
    auto entry = MakeEntry(old_cloud ? 2 : 1, QStringLiteral("synthetic"), now);
    entry.is_deleted = true;
    const auto id = pwd_dao_->Insert(entry);
    ASSERT_TRUE(id);
    SyncPayloadV2 payload;
    if (old_cloud) {
        PasswordSyncItemV2 cloud;
        cloud.uuid = entry.uuid;
        cloud.title = entry.title;
        cloud.password = QStringLiteral("synthetic");
        cloud.created_at = entry.created_at;
        cloud.updated_at = entry.updated_at;
        cloud.is_deleted = true;
        payload.passwords.push_back(cloud);
    }
    const auto backup = CloudCryptoService::EncryptForCloud(
        payload.ToJsonBytes(), kMasterPassword);
    ASSERT_TRUE(backup);
    provider_.SetCloudDirectly(backup->ToJsonBytes(), QStringLiteral("remote"));
    provider_.on_read = [&] {
        EXPECT_EQ(pwd_dao_->BulkPermanentlyDelete({*id}, now), 1);
    };
    for (int round = 0; round < 2; ++round) {
        ASSERT_TRUE(manager_->PerformSync().success);
        EXPECT_FALSE(pwd_dao_->FindByUuid(entry.uuid));
        EXPECT_TRUE(pwd_dao_->ListActive().empty());
        ASSERT_TRUE(pwd_dao_->ListTrashIds());
        EXPECT_TRUE(pwd_dao_->ListTrashIds()->empty());
        const auto markers = pwd_dao_->ListDeletionMarkers();
        ASSERT_TRUE(markers);
        ASSERT_EQ(markers->size(), 1u);
        for (const auto& marker : *markers) EXPECT_EQ(marker.deleted_at, now);
        const auto uploaded = DecodeCloud(provider_);
        ASSERT_TRUE(uploaded);
        ASSERT_EQ(uploaded->passwords.size(), markers->size());
        for (const auto& item : uploaded->passwords) {
            EXPECT_TRUE(item.is_deleted);
            EXPECT_TRUE(item.title.isEmpty());
            EXPECT_TRUE(item.username.isEmpty());
            EXPECT_TRUE(item.password.isEmpty());
            EXPECT_TRUE(item.website.isEmpty());
            EXPECT_TRUE(item.notes.isEmpty());
            EXPECT_TRUE(item.category_uuid.isEmpty());
            EXPECT_FALSE(item.is_favorite);
            EXPECT_EQ(item.icon_color, 0);
            EXPECT_EQ(item.created_at, 0);
            EXPECT_EQ(item.updated_at, now);
        }
    }
}

TEST_F(SyncManagerTest, PermanentDeletionDuringReadSurvivesEmptyCloud) {
    CheckPermanentDeletionDuringRead(false);
}

TEST_F(SyncManagerTest, PermanentDeletionDuringReadSurvivesMatchingOldCloud) {
    CheckPermanentDeletionDuringRead(true);
}

TEST_F(SyncManagerTest, PermanentDeletionDuringUploadRetainsDirtyAfterBusyTimeout) {
    manager_->set_provider(&provider_);
    passvault::sync::SyncScheduler scheduler(manager_.get());
    scheduler.set_debounce_ms(20);
    QSignalSpy triggered(&scheduler, &passvault::sync::SyncScheduler::SyncTriggered);
    const auto now = QDateTime::currentMSecsSinceEpoch();
    auto entry = MakeEntry(1, QStringLiteral("synthetic"), now);
    entry.is_deleted = true;
    const auto id = pwd_dao_->Insert(entry);
    ASSERT_TRUE(id);
    provider_.on_upload = [&] {
        EXPECT_TRUE(manager_->is_busy());
        EXPECT_EQ(pwd_dao_->BulkPermanentlyDelete({*id}, now), 1);
        scheduler.MarkDirty();
        QTest::qWait(80);
        EXPECT_EQ(triggered.count(), 1);
        EXPECT_TRUE(scheduler.IsPending());
        scheduler.SyncImmediately();
        EXPECT_EQ(triggered.count(), 1);
        EXPECT_TRUE(scheduler.IsPending());
    };
    scheduler.SyncImmediately();
    EXPECT_FALSE(pwd_dao_->FindByUuid(entry.uuid));
    QElapsedTimer wait;
    wait.start();
    while (provider_.upload_count() < 2 && wait.elapsed() < 3000) {
        QTest::qWait(20);
    }
    ASSERT_EQ(provider_.upload_count(), 2);
    EXPECT_FALSE(scheduler.IsPending());
    const auto markers = pwd_dao_->ListDeletionMarkers();
    ASSERT_TRUE(markers);
    ASSERT_EQ(markers->size(), 1u);
    EXPECT_EQ(markers->front().deleted_at, now);
    const auto uploaded = DecodeCloud(provider_);
    ASSERT_TRUE(uploaded);
    ASSERT_EQ(uploaded->passwords.size(), 1u);
    EXPECT_TRUE(uploaded->passwords.front().is_deleted);
    EXPECT_TRUE(uploaded->passwords.front().title.isEmpty());
    EXPECT_TRUE(uploaded->passwords.front().password.isEmpty());
    EXPECT_EQ(uploaded->passwords.front().updated_at, now);
    ASSERT_TRUE(manager_->PerformSync().success);
    EXPECT_FALSE(pwd_dao_->FindByUuid(entry.uuid));
    EXPECT_TRUE(pwd_dao_->ListTrashIds()->empty());
    EXPECT_EQ(provider_.upload_count(), 2);
}

TEST_F(SyncManagerTest, FavoriteSyncPreservesContentTimeAndContentChangesAdvanceIt) {
    const auto initial = MakeEntry(1, QStringLiteral("original password"), 1000);
    const auto id = pwd_dao_->Insert(initial);
    ASSERT_TRUE(id);
    manager_->set_provider(&provider_);
    ASSERT_TRUE(manager_->PerformSync().success);
    for (const bool favorite : {true, false}) {
        ASSERT_TRUE(pwd_dao_->SetFavorite(*id, favorite));
        const int uploads = provider_.upload_count();
        ASSERT_TRUE(manager_->PerformSync().success);
        EXPECT_EQ(provider_.upload_count(), uploads + 1);
        const auto cloud = DecodeCloud(provider_);
        ASSERT_TRUE(cloud);
        ASSERT_EQ(cloud->passwords.size(), 1);
        EXPECT_EQ(cloud->passwords.front().is_favorite, favorite);
        EXPECT_GT(cloud->passwords.front().updated_at, initial.updated_at);
        EXPECT_EQ(pwd_dao_->FindById(*id)->ContentUpdatedAt(), initial.updated_at);
    }
    auto cloud = DecodeCloud(provider_);
    ASSERT_TRUE(cloud);
    auto& remote = cloud->passwords.front();
    remote.is_favorite = true;
    ++remote.updated_at;
    auto backup = CloudCryptoService::EncryptForCloud(
        cloud->ToJsonBytes(), std::string_view(kMasterPassword));
    ASSERT_TRUE(backup);
    provider_.SetCloudDirectly(backup->ToJsonBytes(), QStringLiteral("remote-favorite"));
    ASSERT_TRUE(manager_->PerformSync().success);
    EXPECT_TRUE(pwd_dao_->FindById(*id)->is_favorite);
    EXPECT_EQ(pwd_dao_->FindById(*id)->ContentUpdatedAt(), initial.updated_at);
    remote.password = QStringLiteral("changed password");
    ++remote.updated_at;
    backup = CloudCryptoService::EncryptForCloud(
        cloud->ToJsonBytes(), std::string_view(kMasterPassword));
    ASSERT_TRUE(backup);
    provider_.SetCloudDirectly(backup->ToJsonBytes(), QStringLiteral("remote-content"));
    ASSERT_TRUE(manager_->PerformSync().success);
    const auto changed = pwd_dao_->FindById(*id);
    ASSERT_TRUE(changed);
    EXPECT_EQ(changed->ContentUpdatedAt(), remote.updated_at);
    EXPECT_EQ(changed->updated_at, remote.updated_at);
}
