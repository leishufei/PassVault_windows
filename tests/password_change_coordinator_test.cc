#include <gtest/gtest.h>

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <memory>
#include <limits>
#include <string>

#include "app/password_change_startup.h"
#include "crypto/crypto_service.h"
#include "session/session_manager.h"
#include "sync/sync_manager.h"
#include "crypto/random.h"
#include "hello/windows_hello_unlock.h"
#include "master_password/password_change_coordinator.h"
#include "model/cloud_backup_format.h"
#include "model/sync_payload_v2.h"
#include "storage/category_dao.h"
#include "storage/database.h"
#include "storage/password_dao.h"
#include "storage/schema.h"
#include "storage/statement.h"
#include "sync/cloud_crypto_service.h"
#include "sync/cloud_storage_provider.h"

namespace {

using passvault::master_password::CloudBinding;
using passvault::master_password::MasterPasswordManager;
using passvault::master_password::MasterPasswordStore;
using Coordinator = passvault::master_password::PasswordChangeCoordinator;
using Outcome = Coordinator::Outcome;
using Boundary = Coordinator::Boundary;
using passvault::sync::CloudReadResult;
using passvault::sync::CloudReadStatus;
using passvault::sync::UploadIfMatchStatus;
using passvault::model::SyncPayloadV2;
using passvault::crypto::CryptoService;

class MigrationProvider : public passvault::sync::CloudStorageProvider {
 public:
    QString ProviderName() const override { return QStringLiteral("isolated fake"); }
    bool IsAuthenticated() const override { return authenticated; }
    void SignOut() override { authenticated = false; }
    bool UploadBackup(const QByteArray&, const QString&, QString*) override { return false; }
    std::optional<QByteArray> DownloadBackup(const QString&, QString*) override {
        return std::nullopt;
    }
    std::optional<passvault::sync::CloudDownloadResult> DownloadBackupWithVersion(
        const QString& name, QString* error) override {
        auto result = ReadBackup(name, error);
        if (result.status != CloudReadStatus::kFound) return std::nullopt;
        return result.backup;
    }
    CloudReadResult ReadBackup(const QString&, QString*) override {
        ++reads;
        if (read_error || (uploads > 0 && fail_verification)) return {};
        if (missing) return {CloudReadStatus::kMissing, {}};
        return {CloudReadStatus::kFound, {body, version}};
    }
    UploadIfMatchStatus UploadBackupIfMatch(const QByteArray& candidate,
        const QString&, const QString& expected, QString*) override {
        ++uploads;
        expected_versions.push_back(expected);
        if (on_upload) on_upload();
        if (conflicts-- > 0 || expected != version) {
            version = QStringLiteral("2026-10-27T01:02:03.123456%1Z").arg(uploads);
            return UploadIfMatchStatus::kVersionMismatch;
        }
        if (reject_without_write) return UploadIfMatchStatus::kError;
        body = candidate;
        version = QStringLiteral("2026-10-27T01:02:04.123456789Z");
        return write_but_error ? UploadIfMatchStatus::kError : UploadIfMatchStatus::kSuccess;
    }
    bool authenticated = true;
    bool read_error = false;
    bool missing = false;
    bool fail_verification = false;
    bool reject_without_write = false;
    bool write_but_error = false;
    int reads = 0;
    int uploads = 0;
    int conflicts = 0;
    QByteArray body;
    QString version = QStringLiteral("2026-10-27T01:02:03.123456789Z");
    std::vector<QString> expected_versions;
    std::function<void()> on_upload;
};

class PasswordChangeTest : public testing::Test {
 protected:
    void SetUp() override {
        ASSERT_TRUE(dir_.isValid());
        Reopen();
        ASSERT_TRUE(manager_->SetInitial(old_password_));
        ASSERT_TRUE(coordinator_->SaveBinding(CloudBinding::kLocal));
    }

    void Reopen() {
        coordinator_.reset();
        manager_.reset();
        passwords_.reset();
        categories_.reset();
        db_.reset();
        store_ = std::make_unique<MasterPasswordStore>(dir_.filePath("master.dat"));
        db_ = passvault::storage::Database::Open(dir_.filePath("vault.db"));
        passvault::storage::EnsureCurrentSchema(*db_);
        passwords_ = std::make_unique<passvault::storage::PasswordDao>(*db_);
        categories_ = std::make_unique<passvault::storage::CategoryDao>(*db_);
        manager_ = std::make_unique<MasterPasswordManager>(*store_);
        coordinator_ = std::make_unique<Coordinator>(
            *store_, *manager_, *passwords_, *categories_, [this]() {
                ++hello_invalidations_;
                return hello_ok_;
            });
    }

    void AddRows() {
        auto unlocked = manager_->VerifyLocal(old_password_);
        ASSERT_TRUE(unlocked);
        passvault::model::Category category;
        category.uuid = "category-local";
        category.name = "Local";
        category.created_at = 101;
        category.updated_at = 202;
        auto id = categories_->Insert(category);
        ASSERT_TRUE(id);
        for (int i = 0; i < 3; ++i) {
            passvault::model::PasswordEntry p;
            p.uuid = QStringLiteral("local-%1").arg(i);
            p.title = "Isolated title";
            p.app_package_name = "isolated.package";
            p.category_id = *id;
            p.created_at = 111 + i;
            p.updated_at = 222 + i;
            p.is_deleted = i == 1;
            const auto iv = passvault::crypto::Random::Bytes(CryptoService::kIvSize);
            p.password_iv = QByteArray(reinterpret_cast<const char*>(iv.data()), iv.size());
            const QByteArray plain = i == 2 ? QByteArray{} : QByteArray("isolated-field");
            p.encrypted_password = CryptoService::EncryptGcm(
                unlocked->session_key.data(), unlocked->session_key.size(), iv.data(),
                iv.size(), reinterpret_cast<const std::uint8_t*>(plain.constData()), plain.size());
            ASSERT_TRUE(passwords_->Insert(p));
        }
        ASSERT_TRUE(passwords_->ApplyDeletionMarker("permanently-deleted", 333));
    }

    void SetCloud(const SyncPayloadV2& payload) {
        SetCloudJson(payload.ToJsonObject());
    }

    void SetCloudJson(const QJsonObject& payload) {
        auto encrypted = passvault::sync::CloudCryptoService::EncryptForCloud(
            QJsonDocument(payload).toJson(QJsonDocument::Compact), old_password_);
        ASSERT_TRUE(encrypted);
        provider_.body = encrypted->ToJsonBytes();
        ASSERT_TRUE(coordinator_->SaveBinding(CloudBinding::kBound));
    }

    std::optional<SyncPayloadV2> ReadCloud(const std::string& password) {
        auto backup = passvault::model::CloudBackupFormat::FromJsonBytes(provider_.body);
        if (!backup) return std::nullopt;
        auto plain = passvault::sync::CloudCryptoService::DecryptFromCloud(*backup, password);
        if (!plain) return std::nullopt;
        return SyncPayloadV2::FromJsonBytes(*plain);
    }

    Coordinator::Result Change() {
        return coordinator_->Change(QString::fromStdString(old_password_),
                                    QString::fromStdString(new_password_),
                                    QString::fromStdString(new_password_), &provider_);
    }

    QByteArray DatabaseHash() {
        sqlite3_int64 size = 0;
        auto* bytes = sqlite3_serialize(db_->handle(), "main", &size, 0);
        EXPECT_NE(bytes, nullptr);
        const auto hash = QCryptographicHash::hash(
            QByteArrayView(reinterpret_cast<const char*>(bytes), size),
            QCryptographicHash::Sha256);
        sqlite3_free(bytes);
        return hash;
    }

    int Synchronous() {
        passvault::storage::Statement stmt(db_->handle(), "PRAGMA main.synchronous");
        EXPECT_TRUE(stmt.Step());
        return stmt.ColumnInt(0);
    }

    void AssertReadable(const std::string& password) {
        auto unlocked = manager_->VerifyLocal(password);
        ASSERT_TRUE(unlocked);
        auto rows = passwords_->ReadAll();
        ASSERT_TRUE(rows);
        for (const auto& row : *rows) {
            auto plain = CryptoService::DecryptGcm(
                unlocked->session_key.data(), unlocked->session_key.size(),
                reinterpret_cast<const std::uint8_t*>(row.password_iv.constData()),
                row.password_iv.size(),
                reinterpret_cast<const std::uint8_t*>(row.encrypted_password.constData()),
                row.encrypted_password.size());
            ASSERT_TRUE(plain) << row.uuid.toStdString();
            EXPECT_EQ(*plain, row.uuid == "local-2" ? QByteArray{} : QByteArray("isolated-field"));
        }
        auto markers = passwords_->ListDeletionMarkers();
        ASSERT_TRUE(markers);
        ASSERT_EQ(markers->size(), 1);
        EXPECT_EQ(markers->front().uuid, "permanently-deleted");
        EXPECT_EQ(markers->front().deleted_at, 333);
    }

    QTemporaryDir dir_;
    std::unique_ptr<MasterPasswordStore> store_;
    std::unique_ptr<passvault::storage::Database> db_;
    std::unique_ptr<passvault::storage::PasswordDao> passwords_;
    std::unique_ptr<passvault::storage::CategoryDao> categories_;
    std::unique_ptr<MasterPasswordManager> manager_;
    std::unique_ptr<Coordinator> coordinator_;
    MigrationProvider provider_;
    int hello_invalidations_ = 0;
    bool hello_ok_ = true;
    const std::string old_password_ = "isolated-initial-master";
    const std::string new_password_ = "isolated-replacement-master";
};

TEST_F(PasswordChangeTest, MigratesActiveTrashEmptyPasswordAndMarkerWithoutChangingTimes) {
    AddRows();
    const auto before = passwords_->ReadAll();
    ASSERT_EQ(Change().outcome, Outcome::kCompleted);
    EXPECT_FALSE(coordinator_->NeedsRecovery());
    EXPECT_EQ(provider_.reads, 0);
    EXPECT_EQ(provider_.uploads, 0);
    Reopen();
    AssertReadable(new_password_);
    EXPECT_FALSE(manager_->VerifyLocal(old_password_));
    const auto after = passwords_->ReadAll();
    ASSERT_EQ(after->size(), before->size());
    for (std::size_t i = 0; i < after->size(); ++i) {
        EXPECT_EQ(after->at(i).id, before->at(i).id);
        EXPECT_EQ(after->at(i).uuid, before->at(i).uuid);
        EXPECT_EQ(after->at(i).created_at, before->at(i).created_at);
        EXPECT_EQ(after->at(i).updated_at, before->at(i).updated_at);
        EXPECT_EQ(after->at(i).app_package_name, before->at(i).app_package_name);
        EXPECT_EQ(after->at(i).is_deleted, before->at(i).is_deleted);
    }
}

TEST_F(PasswordChangeTest, RejectsWrongOldShortNewMismatchedConfirmationAndUnchanged) {
    AddRows();
    EXPECT_EQ(coordinator_->Change("wrong", "replacement", "replacement", nullptr).outcome,
              Outcome::kUnchanged);
    EXPECT_EQ(coordinator_->Change(QString::fromStdString(old_password_), "x", "x", nullptr).outcome,
              Outcome::kUnchanged);
    EXPECT_EQ(coordinator_->Change(QString::fromStdString(old_password_), "replacement", "other", nullptr).outcome,
              Outcome::kUnchanged);
    const auto old = QString::fromStdString(old_password_);
    EXPECT_EQ(coordinator_->Change(old, old, old, nullptr).outcome, Outcome::kUnchanged);
    EXPECT_FALSE(coordinator_->NeedsRecovery());
    AssertReadable(old_password_);
}

TEST_F(PasswordChangeTest, RejectsMalformedCiphertextAndEmptyOrdinaryFieldsWithoutWriting) {
    AddRows();
    const auto record = store_->LoadRaw();
    auto p = passwords_->FindByUuid("local-1");
    ASSERT_TRUE(p);
    const auto original = *p;
    for (int variant = 0; variant < 3; ++variant) {
        *p = original;
        if (variant == 0) p->encrypted_password[0] ^= 1;
        if (variant == 1) p->password_iv.clear();
        if (variant == 2) { p->encrypted_password.clear(); p->password_iv.clear(); }
        ASSERT_TRUE(passwords_->Update(*p));
        EXPECT_EQ(Change().outcome, Outcome::kUnchanged);
        EXPECT_EQ(store_->LoadRaw(), record);
        EXPECT_FALSE(coordinator_->NeedsRecovery());
    }
    ASSERT_TRUE(passwords_->Update(original));
    AssertReadable(old_password_);
}

int DenyTable(void* context, int action, const char* first, const char*, const char*, const char*) {
    if (action == SQLITE_READ && first && *static_cast<std::string*>(context) == first) {
        return SQLITE_DENY;
    }
    return SQLITE_OK;
}

TEST_F(PasswordChangeTest, StrictReadsDistinguishSqlFailureFromEmptyCollections) {
    AddRows();
    const auto before = store_->LoadRaw();
    for (std::string table : {"password_entries", "categories", "password_deletion_markers"}) {
        ASSERT_EQ(sqlite3_set_authorizer(db_->handle(), DenyTable, &table), SQLITE_OK);
        const auto result = Change();
        EXPECT_EQ(result.outcome, Outcome::kUnchanged);
        EXPECT_TRUE(result.message.contains(QStringLiteral("读取保险库失败")));
        sqlite3_set_authorizer(db_->handle(), nullptr, nullptr);
        EXPECT_EQ(store_->LoadRaw(), before);
    }
    AssertReadable(old_password_);
}

TEST_F(PasswordChangeTest, UnreadableMasterRecordIsIoFailureNotWrongPassword) {
    QFile file(store_->file_path());
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_GT(file.write("damaged protected blob"), 0);
    file.close();
    auto result = Change();
    EXPECT_EQ(result.outcome, Outcome::kUnchanged);
    EXPECT_TRUE(result.message.contains(QStringLiteral("读取主密码记录失败")));
}

TEST_F(PasswordChangeTest, MergesLocalCloudAndTimestampConflictsBeforeCloudMigration) {
    AddRows();
    SyncPayloadV2 cloud;
    passvault::model::PasswordSyncItemV2 item;
    item.uuid = "cloud-only";
    item.title = "Cloud title";
    item.password = "cloud-field";
    item.created_at = 10;
    item.updated_at = 30;
    cloud.passwords.push_back(item);
    item.uuid = "local-0";
    item.updated_at = 222;
    cloud.passwords.push_back(item);
    item.uuid = "local-1";
    item.updated_at = 999;
    item.is_deleted = true;
    cloud.passwords.push_back(item);
    SetCloud(cloud);
    const auto result = Change();
    ASSERT_EQ(result.outcome, Outcome::kCompleted);
    auto migrated = ReadCloud(new_password_);
    ASSERT_TRUE(migrated);
    EXPECT_FALSE(ReadCloud(old_password_));
    ASSERT_EQ(migrated->passwords.size(), 5);
    bool found_marker = false, found_local = false, found_cloud = false;
    for (const auto& p : migrated->passwords) {
        if (p.uuid == "permanently-deleted") found_marker = p.is_deleted && p.password.isEmpty();
        if (p.uuid == "local-0") found_local = p.password == "isolated-field" && p.updated_at == 222;
        if (p.uuid == "cloud-only") found_cloud = p.password == "cloud-field";
    }
    EXPECT_TRUE(found_marker);
    EXPECT_TRUE(found_local);
    EXPECT_TRUE(found_cloud);
    auto local = passwords_->FindByUuid("local-1");
    ASSERT_TRUE(local);
    EXPECT_EQ(local->updated_at, 999);
    EXPECT_TRUE(local->is_deleted);
    EXPECT_EQ(provider_.expected_versions.front(), "2026-10-27T01:02:03.123456789Z");
}

TEST_F(PasswordChangeTest, MissingBackupDiffersFromNetworkAuthenticationAndInvalidJson) {
    AddRows();
    SetCloud({});
    provider_.missing = true;
    auto result = Change();
    EXPECT_TRUE(result.message.contains(QStringLiteral("首次同步")));
    provider_.missing = false;
    provider_.read_error = true;
    result = Change();
    EXPECT_TRUE(result.message.contains(QStringLiteral("无法核对")));
    provider_.read_error = false;
    provider_.authenticated = false;
    result = Change();
    EXPECT_TRUE(result.message.contains(QStringLiteral("重新登录")));
    provider_.authenticated = true;
    provider_.body = "not json";
    result = Change();
    EXPECT_TRUE(result.message.contains(QStringLiteral("损坏")));
    EXPECT_EQ(provider_.uploads, 0);
    AssertReadable(old_password_);
}

TEST_F(PasswordChangeTest, RetriesFullFractionalVersionsAndStopsAtConfiguredLimit) {
    AddRows();
    SetCloud({});
    provider_.conflicts = 3;
    EXPECT_EQ(Change().outcome, Outcome::kUnchanged);
    EXPECT_EQ(provider_.uploads, 3);
    EXPECT_NE(provider_.expected_versions[0], provider_.expected_versions[1]);
    EXPECT_EQ(provider_.expected_versions[1], "2026-10-27T01:02:03.1234561Z");
    EXPECT_FALSE(coordinator_->NeedsRecovery());
    AssertReadable(old_password_);
    provider_.conflicts = 1;
    EXPECT_EQ(Change().outcome, Outcome::kCompleted);
}

TEST_F(PasswordChangeTest, ConflictReReadsAndReMergesNewCloudContent) {
    AddRows();
    SetCloud({});
    provider_.conflicts = 1;
    provider_.on_upload = [this]() {
        if (provider_.uploads != 1) return;
        SyncPayloadV2 newer;
        passvault::model::PasswordSyncItemV2 p;
        p.uuid = "arrived-during-conflict";
        p.title = "New remote";
        p.password = "remote-after-conflict";
        newer.passwords.push_back(p);
        SetCloud(newer);
    };
    ASSERT_EQ(Change().outcome, Outcome::kCompleted);
    EXPECT_TRUE(passwords_->FindByUuid("arrived-during-conflict"));
    auto cloud = ReadCloud(new_password_);
    ASSERT_TRUE(cloud);
    EXPECT_EQ(cloud->passwords.size(), 5);
}

TEST_F(PasswordChangeTest, ServerWriteThenConnectionErrorIsVerifiedAndCommitted) {
    AddRows();
    SetCloud({});
    provider_.write_but_error = true;
    ASSERT_EQ(Change().outcome, Outcome::kCompleted);
    Reopen();
    AssertReadable(new_password_);
}

TEST_F(PasswordChangeTest, ConfirmedUnchangedUploadFailureKeepsOldPassword) {
    AddRows();
    SetCloud({});
    provider_.reject_without_write = true;
    EXPECT_EQ(Change().outcome, Outcome::kUnchanged);
    EXPECT_FALSE(coordinator_->NeedsRecovery());
    AssertReadable(old_password_);
}

TEST_F(PasswordChangeTest, UnknownUploadStaysLockedAcrossRestartUntilVerificationWorks) {
    AddRows();
    SetCloud({});
    provider_.write_but_error = true;
    provider_.fail_verification = true;
    ASSERT_EQ(Change().outcome, Outcome::kLocked);
    Reopen();
    EXPECT_TRUE(coordinator_->NeedsRecovery());
    EXPECT_EQ(coordinator_->Recover(&provider_).outcome, Outcome::kLocked);
    EXPECT_EQ(Change().outcome, Outcome::kLocked);
    provider_.fail_verification = false;
    EXPECT_EQ(coordinator_->Recover(&provider_).outcome, Outcome::kCompleted);
    EXPECT_FALSE(coordinator_->NeedsRecovery());
    AssertReadable(new_password_);
}

TEST_F(PasswordChangeTest, BindingHistorySurvivesSignOutAndCannotDowngradeToLocal) {
    ASSERT_TRUE(coordinator_->SaveBinding(CloudBinding::kBound));
    provider_.SignOut();
    Reopen();
    EXPECT_EQ(coordinator_->ReadBinding(), CloudBinding::kBound);
    EXPECT_FALSE(coordinator_->SaveBinding(CloudBinding::kLocal));
    EXPECT_EQ(Change().outcome, Outcome::kUnchanged);
    EXPECT_EQ(provider_.uploads, 0);
}

TEST_F(PasswordChangeTest, LegacyUnknownRequiresExplicitPersistentChoice) {
    const QString path = dir_.filePath("legacy-master.dat");
    MasterPasswordStore legacy_store(path);
    MasterPasswordManager legacy_manager(legacy_store);
    ASSERT_TRUE(legacy_manager.SetInitial(old_password_));
    Coordinator legacy(legacy_store, legacy_manager, *passwords_, *categories_, []() { return true; });
    EXPECT_EQ(legacy.ReadBinding(), CloudBinding::kUnknown);
    EXPECT_EQ(legacy.Change(QString::fromStdString(old_password_),
        QString::fromStdString(new_password_), QString::fromStdString(new_password_), nullptr).outcome,
        Outcome::kUnchanged);
    ASSERT_TRUE(legacy.SaveBinding(CloudBinding::kLocal));
    Coordinator reopened(legacy_store, legacy_manager, *passwords_, *categories_, []() { return true; });
    EXPECT_EQ(reopened.ReadBinding(), CloudBinding::kLocal);
}

TEST_F(PasswordChangeTest, CorruptJournalBlocksRecoveryAndNewMigration) {
    QFile file(coordinator_->journal_path());
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_GT(file.write("damaged journal"), 0);
    file.close();
    Reopen();
    EXPECT_TRUE(coordinator_->NeedsRecovery());
    EXPECT_EQ(coordinator_->Recover(nullptr).outcome, Outcome::kLocked);
    EXPECT_EQ(Change().outcome, Outcome::kLocked);
}

TEST_F(PasswordChangeTest, CandidateJournalContainsNoUnprotectedMasterOrFieldPassword) {
    AddRows();
    coordinator_->set_checkpoint([](Boundary b) { return b != Boundary::kPrepared; });
    ASSERT_EQ(Change().outcome, Outcome::kLocked);
    QFile file(coordinator_->journal_path());
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto raw = file.readAll();
    EXPECT_FALSE(raw.contains(old_password_.c_str()));
    EXPECT_FALSE(raw.contains(new_password_.c_str()));
    EXPECT_FALSE(raw.contains("isolated-field"));
    const auto decoded = MasterPasswordStore::Unprotect(raw);
    ASSERT_TRUE(decoded);
    EXPECT_FALSE(decoded->contains(old_password_.c_str()));
    EXPECT_FALSE(decoded->contains(new_password_.c_str()));
    EXPECT_FALSE(decoded->contains("isolated-field"));
}

TEST_F(PasswordChangeTest, DatabaseWriteFailureRollsBackTransactionThenRecoversAfterRestart) {
    AddRows();
    SetCloud({});
    ASSERT_TRUE(db_->Execute("CREATE TRIGGER reject_migration BEFORE INSERT ON password_entries "
                            "BEGIN SELECT RAISE(ABORT, 'injected'); END"));
    EXPECT_EQ(Change().outcome, Outcome::kLocked);
    AssertReadable(old_password_);
    Reopen();
    EXPECT_EQ(coordinator_->Recover(&provider_).outcome, Outcome::kLocked);
    ASSERT_TRUE(db_->Execute("DROP TRIGGER reject_migration"));
    EXPECT_EQ(coordinator_->Recover(&provider_).outcome, Outcome::kCompleted);
    AssertReadable(new_password_);
}

TEST_F(PasswordChangeTest, AtomicRecordWriteFailureRemainsRecoverableWithoutMemoryKeys) {
    AddRows();
    bool injected = false;
    coordinator_->set_checkpoint([this, &injected](Boundary b) {
        if (b == Boundary::kBeforeRecord && !injected) {
            injected = true;
            EXPECT_TRUE(QFile::rename(store_->file_path(), dir_.filePath("record-backup")));
            EXPECT_TRUE(QDir().mkdir(store_->file_path()));
        }
        return true;
    });
    EXPECT_EQ(Change().outcome, Outcome::kLocked);
    Reopen();
    EXPECT_EQ(coordinator_->Recover(nullptr).outcome, Outcome::kLocked);
    ASSERT_TRUE(QDir().rename(store_->file_path(), dir_.filePath("record-blocker")));
    ASSERT_TRUE(QFile::rename(dir_.filePath("record-backup"), store_->file_path()));
    EXPECT_EQ(coordinator_->Recover(nullptr).outcome, Outcome::kCompleted);
    AssertReadable(new_password_);
}

TEST_F(PasswordChangeTest, HelloInvalidationFailureKeepsJournalUntilRetrySucceeds) {
    AddRows();
    hello_ok_ = false;
    EXPECT_EQ(Change().outcome, Outcome::kLocked);
    Reopen();
    hello_ok_ = true;
    EXPECT_EQ(coordinator_->Recover(nullptr).outcome, Outcome::kCompleted);
    AssertReadable(new_password_);
    EXPECT_GE(hello_invalidations_, 2);
}

class PasswordChangeRestartTest : public PasswordChangeTest,
    public testing::WithParamInterface<std::pair<bool, Boundary>> {};

TEST_P(PasswordChangeRestartTest, ReopensRealDatabaseAndRecordAtEveryDurableBoundary) {
    AddRows();
    const auto [cloud, boundary] = GetParam();
    if (cloud) SetCloud({});
    coordinator_->set_checkpoint([boundary](Boundary reached) { return reached != boundary; });
    ASSERT_EQ(Change().outcome, Outcome::kLocked);
    Reopen();
    ASSERT_TRUE(coordinator_->NeedsRecovery());
    const auto result = coordinator_->Recover(cloud ? &provider_ : nullptr);
    const bool old = boundary == Boundary::kPrepared || boundary == Boundary::kBeforeUpload;
    ASSERT_EQ(result.outcome, old ? Outcome::kUnchanged : Outcome::kCompleted);
    EXPECT_FALSE(coordinator_->NeedsRecovery());
    AssertReadable(old ? old_password_ : new_password_);
    EXPECT_EQ(coordinator_->Recover(cloud ? &provider_ : nullptr).outcome, Outcome::kUnchanged);
    AssertReadable(old ? old_password_ : new_password_);
}

INSTANTIATE_TEST_SUITE_P(DurableBoundaries, PasswordChangeRestartTest, testing::Values(
    std::make_pair(false, Boundary::kPrepared),
    std::make_pair(false, Boundary::kCloudConfirmed),
    std::make_pair(false, Boundary::kBeforeDatabase),
    std::make_pair(false, Boundary::kAfterDatabase),
    std::make_pair(false, Boundary::kBeforeRecord),
    std::make_pair(false, Boundary::kAfterRecord),
    std::make_pair(false, Boundary::kBeforeFinish),
    std::make_pair(true, Boundary::kPrepared),
    std::make_pair(true, Boundary::kBeforeUpload),
    std::make_pair(true, Boundary::kAfterUpload),
    std::make_pair(true, Boundary::kCloudConfirmed),
    std::make_pair(true, Boundary::kBeforeDatabase),
    std::make_pair(true, Boundary::kAfterDatabase),
    std::make_pair(true, Boundary::kBeforeRecord),
    std::make_pair(true, Boundary::kAfterRecord),
    std::make_pair(true, Boundary::kBeforeFinish)));

}  // namespace

TEST_F(PasswordChangeTest, StartupGateNeverUnlocksBeforeDurableRecoveryCompletes) {
    AddRows();
    SetCloud({});
    provider_.fail_verification = true;
    EXPECT_EQ(Change().outcome, Outcome::kLocked);
    Reopen();
    int unlocks = 0;
    const auto unlock = [this, &unlocks]() {
        ++unlocks;
        return manager_->VerifyLocal(new_password_).has_value();
    };
    EXPECT_FALSE(passvault::app::UnlockAfterPasswordRecovery(*coordinator_, &provider_,
        [](const QString&) { return false; }, unlock));
    EXPECT_EQ(unlocks, 0);
    provider_.fail_verification = false;
    EXPECT_TRUE(passvault::app::UnlockAfterPasswordRecovery(*coordinator_, &provider_,
        [](const QString&) { ADD_FAILURE(); return false; }, unlock));
    EXPECT_EQ(unlocks, 1);
}

TEST_F(PasswordChangeTest, ReentrantChangeIsRejectedWhileProviderRunsNestedEvents) {
    AddRows();
    SetCloud({});
    provider_.on_upload = [this]() {
        EXPECT_EQ(Change().outcome, Outcome::kLocked);
    };
    EXPECT_EQ(Change().outcome, Outcome::kCompleted);
    EXPECT_EQ(provider_.uploads, 1);
}

TEST_F(PasswordChangeTest, RecoveredNewKeySupportsSubsequentCreateAndNormalSync) {
    AddRows();
    SetCloud({});
    coordinator_->set_checkpoint([](Boundary b) { return b != Boundary::kAfterDatabase; });
    ASSERT_EQ(Change().outcome, Outcome::kLocked);
    Reopen();
    ASSERT_EQ(coordinator_->Recover(&provider_).outcome, Outcome::kCompleted);
    auto unlocked = manager_->VerifyLocal(new_password_);
    ASSERT_TRUE(unlocked);
    auto* session = passvault::session::SessionManager::Instance();
    session->ResetForTests();
    session->Unlock(std::move(unlocked->session_key), std::move(unlocked->master_password));
    passvault::model::PasswordEntry p;
    p.uuid = "created-after-recovery";
    p.title = "Created after recovery";
    p.created_at = QDateTime::currentMSecsSinceEpoch();
    p.updated_at = p.created_at;
    const auto iv = passvault::crypto::Random::Bytes(CryptoService::kIvSize);
    p.password_iv = QByteArray(reinterpret_cast<const char*>(iv.data()), iv.size());
    const QByteArray plain("new isolated field");
    p.encrypted_password = CryptoService::EncryptGcm(
        session->session_key()->data(), session->session_key()->size(), iv.data(), iv.size(),
        reinterpret_cast<const std::uint8_t*>(plain.constData()), plain.size());
    ASSERT_TRUE(passwords_->Insert(p));
    passvault::sync::SyncManager sync(passwords_.get(), categories_.get(), session);
    sync.set_provider(&provider_);
    const auto result = sync.PerformSync();
    session->ResetForTests();
    ASSERT_TRUE(result.success) << result.message.toStdString();
    const auto cloud = ReadCloud(new_password_);
    ASSERT_TRUE(cloud);
    bool found = false;
    for (const auto& item : cloud->passwords) {
        if (item.uuid == p.uuid) found = item.password == QString::fromUtf8(plain);
    }
    EXPECT_TRUE(found);
}

TEST_F(PasswordChangeTest, OrdinaryEmptyTrashRowIsNotMistakenForAuxiliaryDeletionMarker) {
    AddRows();
    auto row = passwords_->FindByUuid("local-2");
    ASSERT_TRUE(row);
    row->title.clear();
    row->category_id = 0;
    row->created_at = 0;
    row->is_deleted = true;
    ASSERT_TRUE(passwords_->Update(*row));
    ASSERT_EQ(Change().outcome, Outcome::kCompleted);
    const auto after = passwords_->FindByUuid("local-2");
    ASSERT_TRUE(after);
    EXPECT_TRUE(after->is_deleted);
    EXPECT_EQ(after->encrypted_password.size(), CryptoService::kTagSize);
    AssertReadable(new_password_);
}

TEST_F(PasswordChangeTest, MergedDeletionMarkersSurviveLocalCommitInterruptionAndRestart) {
    AddRows();
    SyncPayloadV2 cloud;
    passvault::model::PasswordSyncItemV2 marker;
    marker.uuid = "local-0";
    marker.updated_at = 500;
    marker.is_deleted = true;
    cloud.passwords.push_back(marker);
    marker.uuid = "cloud-marker";
    marker.updated_at = 600;
    cloud.passwords.push_back(marker);
    passvault::model::PasswordSyncItemV2 stale;
    stale.uuid = "permanently-deleted";
    stale.title = "stale equal-time content";
    stale.password = "isolated stale field";
    stale.updated_at = 333;
    cloud.passwords.push_back(stale);
    SetCloud(cloud);
    coordinator_->set_checkpoint([](Boundary b) { return b != Boundary::kAfterDatabase; });
    ASSERT_EQ(Change().outcome, Outcome::kLocked);
    Reopen();
    ASSERT_EQ(coordinator_->Recover(&provider_).outcome, Outcome::kCompleted);
    EXPECT_FALSE(passwords_->FindByUuid("local-0"));
    EXPECT_FALSE(passwords_->FindByUuid("permanently-deleted"));
    const auto markers = passwords_->ListDeletionMarkers();
    ASSERT_TRUE(markers);
    EXPECT_EQ(markers->size(), 3);
    const auto migrated = ReadCloud(new_password_);
    ASSERT_TRUE(migrated);
    int count = 0;
    for (const auto& p : migrated->passwords) {
        if (p.is_deleted && p.title.isEmpty() && p.password.isEmpty()) ++count;
    }
    EXPECT_EQ(count, 3);
}

TEST_F(PasswordChangeTest, CategoryNameMergeKeepsEarlierCategoryAndNormalBusinessTimes) {
    AddRows();
    SyncPayloadV2 cloud;
    passvault::model::CategorySyncItemV2 category;
    category.uuid = "earlier-cloud-category";
    category.name = "local";
    category.created_at = 50;
    category.updated_at = 100;
    cloud.categories.push_back(category);
    SetCloud(cloud);
    const auto before = QDateTime::currentMSecsSinceEpoch();
    ASSERT_EQ(Change().outcome, Outcome::kCompleted);
    const auto winner = categories_->FindByUuid(category.uuid);
    const auto loser = categories_->FindByUuid("category-local");
    ASSERT_TRUE(winner);
    ASSERT_TRUE(loser);
    EXPECT_FALSE(winner->is_deleted);
    EXPECT_TRUE(loser->is_deleted);
    EXPECT_GE(loser->updated_at, before);
    const auto active = passwords_->FindByUuid("local-0");
    const auto trash = passwords_->FindByUuid("local-1");
    ASSERT_TRUE(active);
    ASSERT_TRUE(trash);
    EXPECT_EQ(active->category_id, winner->id);
    EXPECT_EQ(active->created_at, 111);
    EXPECT_GE(active->updated_at, before);
    EXPECT_EQ(trash->category_id, loser->id);
    EXPECT_EQ(trash->updated_at, 223);
}

struct InvalidCloudCase {
    std::string name;
    QJsonObject payload;
};

QJsonObject CompleteCloudJson() {
    SyncPayloadV2 payload;
    passvault::model::CategorySyncItemV2 category;
    category.uuid = "remote-category";
    category.name = "Remote";
    payload.categories.push_back(category);
    passvault::model::PasswordSyncItemV2 password;
    password.uuid = "remote-password";
    password.category_uuid = category.uuid;
    payload.passwords.push_back(password);
    return payload.ToJsonObject();
}

std::vector<InvalidCloudCase> InvalidCloudCases() {
    std::vector<InvalidCloudCase> cases;
    const auto valid = CompleteCloudJson();
    auto add_root = [&](const char* name, const char* field, QJsonValue value) {
        auto object = valid;
        object.insert(field, value);
        cases.push_back({name, object});
    };
    add_root("VersionMissing", "version", QJsonValue::Undefined);
    add_root("VersionV1", "version", 1);
    add_root("VersionFuture", "version", 3);
    add_root("VersionFraction", "version", 2.5);
    add_root("VersionString", "version", "2");
    add_root("VersionBoolean", "version", true);
    add_root("VersionNull", "version", QJsonValue::Null);
    add_root("TimestampBoolean", "syncTimestamp", true);
    add_root("TimestampFraction", "syncTimestamp", 1.5);
    add_root("TimestampOverflow", "syncTimestamp", 9223372036854775808.0);
    add_root("TimestampBadString", "syncTimestamp", "not-an-integer");
    add_root("TimestampStringOverflow", "syncTimestamp", "9223372036854775808");
    for (const auto* collection : {"passwords", "categories"}) {
        auto object = valid;
        object.remove(collection);
        cases.push_back({std::string(collection) + "Missing", object});
        object.insert(collection, QJsonObject{});
        cases.push_back({std::string(collection) + "NotArray", object});
        auto array = valid.value(collection).toArray();
        array.append(123);
        object.insert(collection, array);
        cases.push_back({std::string(collection) + "NonObjectItem", object});
        const auto item = valid.value(collection).toArray().first().toObject();
        for (auto it = item.begin(); it != item.end(); ++it) {
            auto missing = item;
            missing.remove(it.key());
            object = valid;
            object.insert(collection, QJsonArray{missing});
            cases.push_back({std::string(collection) + "Missing" + it.key().toStdString(), object});
            auto wrong = item;
            wrong.insert(it.key(), it.value().isDouble() ? QJsonValue("wrong") : QJsonValue(123));
            object.insert(collection, QJsonArray{wrong});
            cases.push_back({std::string(collection) + "Wrong" + it.key().toStdString(), object});
        }
        for (const auto* field : {"createdAt", "updatedAt"}) {
            auto fractional = item;
            fractional.insert(field, 0.5);
            object = valid;
            object.insert(collection, QJsonArray{fractional});
            cases.push_back({std::string(collection) + "Fraction" + field, object});
        }
        const auto integer_fields = std::string(collection) == "passwords"
            ? std::vector<const char*>{"iconColor"}
            : std::vector<const char*>{"color", "sortOrder"};
        for (const auto* field : integer_fields) {
            auto fractional = item;
            fractional.insert(field, 0.5);
            object = valid;
            object.insert(collection, QJsonArray{fractional});
            cases.push_back({std::string(collection) + "Fraction" + field, object});
        }
        array = valid.value(collection).toArray();
        array.append(item);
        object = valid;
        object.insert(collection, array);
        cases.push_back({std::string(collection) + "DuplicateUuid", object});
        auto empty_uuid = item;
        empty_uuid.insert("uuid", "");
        object.insert(collection, QJsonArray{empty_uuid});
        cases.push_back({std::string(collection) + "EmptyUuid", object});
    }
    auto object = valid;
    auto password = object.value("passwords").toArray().first().toObject();
    password.insert("categoryUuid", "missing-category");
    object.insert("passwords", QJsonArray{password});
    cases.push_back({"MissingCategoryReference", object});
    password.insert("categoryUuid", "");
    password.insert("iconColor", 2147483648.0);
    object.insert("passwords", QJsonArray{password});
    cases.push_back({"ColorOverflow", object});
    return cases;
}

class PasswordChangeInvalidCloudTest
    : public PasswordChangeTest,
      public testing::WithParamInterface<InvalidCloudCase> {};

TEST_P(PasswordChangeInvalidCloudTest, RejectsWithoutWritingOrUploading) {
    AddRows();
    SetCloudJson(GetParam().payload);
    const auto database = DatabaseHash();
    const auto record = store_->LoadRaw();
    const auto cloud = provider_.body;
    EXPECT_EQ(Change().outcome, Outcome::kUnchanged);
    EXPECT_EQ(provider_.uploads, 0);
    EXPECT_TRUE(provider_.body == cloud);
    EXPECT_TRUE(store_->LoadRaw() == record);
    EXPECT_EQ(DatabaseHash(), database);
    EXPECT_FALSE(coordinator_->NeedsRecovery());
    EXPECT_FALSE(QFile::exists(coordinator_->journal_path()));
}

INSTANTIATE_TEST_SUITE_P(
    StrictMigration, PasswordChangeInvalidCloudTest,
    testing::ValuesIn(InvalidCloudCases()),
    [](const testing::TestParamInfo<InvalidCloudCase>& info) { return info.param.name; });

TEST_F(PasswordChangeTest, StrictV2AllowsEmptyPasswordsMarkersOptionalTimestampAndUnknownFields) {
    AddRows();
    auto object = CompleteCloudJson();
    object.remove("syncTimestamp");
    object.insert("extension", QJsonObject{{"opaque", QJsonArray{1, "kept"}}});
    auto category = object.value("categories").toArray().first().toObject();
    category.insert("extension", "category-extra");
    category.insert("createdAt", "9223372036854775807");
    category.insert("updatedAt", "-9223372036854775808");
    object.insert("categories", QJsonArray{category});
    auto password = object.value("passwords").toArray().first().toObject();
    password.insert("extension", QJsonArray{true, "password-extra"});
    passvault::model::PasswordSyncItemV2 marker;
    marker.uuid = "remote-marker";
    marker.updated_at = 444;
    marker.is_deleted = true;
    object.insert("passwords", QJsonArray{password, marker.ToJsonObject()});
    SetCloudJson(object);
    ASSERT_EQ(Change().outcome, Outcome::kCompleted);
    auto backup = passvault::model::CloudBackupFormat::FromJsonBytes(provider_.body);
    ASSERT_TRUE(backup);
    EXPECT_EQ(backup->version, 1);
    auto plain = passvault::sync::CloudCryptoService::DecryptFromCloud(*backup, new_password_);
    ASSERT_TRUE(plain);
    const auto migrated = QJsonDocument::fromJson(*plain).object();
    EXPECT_EQ(migrated.value("extension"), object.value("extension"));
    for (const auto& value : migrated.value("passwords").toArray()) {
        const auto item = value.toObject();
        if (item.value("uuid") == "remote-password") {
            EXPECT_EQ(item.value("password"), "");
            EXPECT_EQ(item.value("extension"), password.value("extension"));
        }
    }
    for (const auto& value : migrated.value("categories").toArray()) {
        const auto item = value.toObject();
        if (item.value("uuid") == "remote-category") {
            EXPECT_EQ(item.value("extension"), category.value("extension"));
            EXPECT_EQ(item.value("createdAt").toInteger(), std::numeric_limits<qint64>::max());
            EXPECT_EQ(item.value("updatedAt").toInteger(), std::numeric_limits<qint64>::min());
        }
    }
    const auto remote = passwords_->FindByUuid("remote-password");
    ASSERT_TRUE(remote);
    const auto unlocked = manager_->VerifyLocal(new_password_);
    ASSERT_TRUE(unlocked);
    const auto decrypted = CryptoService::DecryptGcm(
        unlocked->session_key.data(), unlocked->session_key.size(),
        reinterpret_cast<const std::uint8_t*>(remote->password_iv.constData()),
        remote->password_iv.size(),
        reinterpret_cast<const std::uint8_t*>(remote->encrypted_password.constData()),
        remote->encrypted_password.size());
    ASSERT_TRUE(decrypted);
    EXPECT_TRUE(decrypted->isEmpty());
    EXPECT_FALSE(passwords_->FindByUuid("remote-marker"));
    const auto markers = passwords_->ListDeletionMarkers();
    ASSERT_TRUE(markers);
    EXPECT_EQ(markers->size(), 2);
}

struct MigrationSqlControl {
    enum class Failure { kNone, kSet, kIgnoredSet, kReadback, kCommit, kRestore };
    Failure failure = Failure::kNone;
    int commits = 0;
    sqlite3* database = nullptr;
    bool setting_inside_transaction = false;
    std::vector<std::string> events;
};

int MigrationAuthorizer(void* context, int action, const char* first,
                        const char* second, const char*, const char*) {
    auto& control = *static_cast<MigrationSqlControl*>(context);
    if (action == SQLITE_PRAGMA && first && std::string(first) == "synchronous") {
        if (control.database && !sqlite3_get_autocommit(control.database)) {
            control.setting_inside_transaction = true;
        }
        const std::string value = second ? second : "read";
        control.events.push_back(value);
        if ((value == "FULL" || value == "2") &&
            control.failure == MigrationSqlControl::Failure::kSet) return SQLITE_DENY;
        if ((value == "FULL" || value == "2") &&
            control.failure == MigrationSqlControl::Failure::kIgnoredSet) return SQLITE_IGNORE;
        if (value == "read" && control.events.size() > 1 &&
            control.failure == MigrationSqlControl::Failure::kReadback) return SQLITE_DENY;
        if (value == "1" &&
            control.failure == MigrationSqlControl::Failure::kRestore) return SQLITE_DENY;
    }
    if (action == SQLITE_TRANSACTION && first) control.events.push_back(first);
    return SQLITE_OK;
}

int MigrationCommitHook(void* context) {
    auto& control = *static_cast<MigrationSqlControl*>(context);
    ++control.commits;
    return control.failure == MigrationSqlControl::Failure::kCommit;
}

class PasswordChangeDurabilityTest
    : public PasswordChangeTest,
      public testing::WithParamInterface<MigrationSqlControl::Failure> {};

TEST_P(PasswordChangeDurabilityTest, FailedBarrierRetainsConfirmedCandidateAcrossRestart) {
    AddRows();
    ASSERT_TRUE(db_->Execute("PRAGMA wal_autocheckpoint=0"));
    ASSERT_EQ(Synchronous(), 1);
    const auto before = DatabaseHash();
    const auto record = store_->LoadRaw();
    MigrationSqlControl control;
    control.failure = GetParam();
    control.database = db_->handle();
    coordinator_->set_checkpoint([&](Boundary boundary) {
        if (boundary == Boundary::kBeforeDatabase) {
            sqlite3_set_authorizer(db_->handle(), MigrationAuthorizer, &control);
            sqlite3_commit_hook(db_->handle(), MigrationCommitHook, &control);
        }
        return true;
    });
    const auto outcome = Change().outcome;
    sqlite3_set_authorizer(db_->handle(), nullptr, nullptr);
    sqlite3_commit_hook(db_->handle(), nullptr, nullptr);
    ASSERT_EQ(outcome, Outcome::kLocked);
    EXPECT_TRUE(coordinator_->NeedsRecovery());
    EXPECT_TRUE(store_->LoadRaw() == record);
    EXPECT_EQ(hello_invalidations_, 0);
    EXPECT_EQ(sqlite3_get_autocommit(db_->handle()), 1);
    EXPECT_FALSE(control.setting_inside_transaction);
    if (GetParam() != MigrationSqlControl::Failure::kRestore) {
        EXPECT_EQ(DatabaseHash(), before);
        EXPECT_EQ(Synchronous(), 1);
    } else {
        EXPECT_EQ(Synchronous(), 2);
    }
    EXPECT_TRUE(QFile::exists(dir_.filePath("vault.db-wal")));
    Reopen();
    ASSERT_TRUE(coordinator_->NeedsRecovery());
    ASSERT_EQ(coordinator_->Recover(nullptr).outcome, Outcome::kCompleted);
    AssertReadable(new_password_);
    EXPECT_FALSE(coordinator_->NeedsRecovery());
    EXPECT_EQ(coordinator_->Recover(nullptr).outcome, Outcome::kUnchanged);
    EXPECT_EQ(Synchronous(), 1);
}

INSTANTIATE_TEST_SUITE_P(
    DurableFailure, PasswordChangeDurabilityTest,
    testing::Values(MigrationSqlControl::Failure::kSet,
                    MigrationSqlControl::Failure::kIgnoredSet,
                    MigrationSqlControl::Failure::kReadback,
                    MigrationSqlControl::Failure::kCommit,
                    MigrationSqlControl::Failure::kRestore));

TEST_F(PasswordChangeTest, FullWalCommitAndSettingRestorePrecedeMasterAndJournalReplacement) {
    AddRows();
    MigrationSqlControl control;
    control.database = db_->handle();
    coordinator_->set_checkpoint([&](Boundary boundary) {
        if (boundary == Boundary::kBeforeDatabase) {
            EXPECT_EQ(sqlite3_get_autocommit(db_->handle()), 1);
            sqlite3_set_authorizer(db_->handle(), MigrationAuthorizer, &control);
            sqlite3_commit_hook(db_->handle(), MigrationCommitHook, &control);
        }
        if (boundary == Boundary::kBeforeRecord) {
            EXPECT_EQ(control.commits, 1);
            EXPECT_EQ(Synchronous(), 1);
            EXPECT_TRUE(coordinator_->NeedsRecovery());
            EXPECT_TRUE(manager_->VerifyLocal(old_password_));
            const std::vector<std::string> expected = {
                "read", "FULL", "read", "BEGIN", "COMMIT", "1", "read", "read"};
            EXPECT_EQ(control.events, expected);
        }
        return true;
    });
    const auto outcome = Change().outcome;
    sqlite3_set_authorizer(db_->handle(), nullptr, nullptr);
    sqlite3_commit_hook(db_->handle(), nullptr, nullptr);
    ASSERT_EQ(outcome, Outcome::kCompleted);
    EXPECT_FALSE(control.setting_inside_transaction);
    AssertReadable(new_password_);
    EXPECT_EQ(Synchronous(), 1);
}

TEST_F(PasswordChangeTest, BoundConfirmedRecoveryNeedsNoFurtherCloudWriteAfterBarrierFailure) {
    AddRows();
    SetCloud({});
    const auto record = store_->LoadRaw();
    const auto database = DatabaseHash();
    MigrationSqlControl control;
    control.failure = MigrationSqlControl::Failure::kSet;
    coordinator_->set_checkpoint([&](Boundary boundary) {
        if (boundary == Boundary::kBeforeDatabase) {
            sqlite3_set_authorizer(db_->handle(), MigrationAuthorizer, &control);
        }
        return true;
    });
    const auto outcome = Change().outcome;
    sqlite3_set_authorizer(db_->handle(), nullptr, nullptr);
    ASSERT_EQ(outcome, Outcome::kLocked);
    EXPECT_EQ(provider_.uploads, 1);
    const auto cloud = provider_.body;
    EXPECT_TRUE(ReadCloud(new_password_));
    EXPECT_TRUE(store_->LoadRaw() == record);
    EXPECT_EQ(DatabaseHash(), database);
    Reopen();
    ASSERT_EQ(coordinator_->Recover(nullptr).outcome, Outcome::kCompleted);
    EXPECT_EQ(provider_.uploads, 1);
    EXPECT_TRUE(provider_.body == cloud);
    AssertReadable(new_password_);
    EXPECT_EQ(coordinator_->Recover(nullptr).outcome, Outcome::kUnchanged);
}

TEST_F(PasswordChangeTest, RefusesActiveTransactionAndRetainsConfirmedSnapshot) {
    AddRows();
    const auto record = store_->LoadRaw();
    coordinator_->set_checkpoint([&](Boundary boundary) {
        if (boundary == Boundary::kBeforeDatabase) EXPECT_TRUE(db_->Execute("BEGIN IMMEDIATE"));
        return true;
    });
    const auto outcome = Change().outcome;
    ASSERT_TRUE(db_->Execute("ROLLBACK"));
    ASSERT_EQ(outcome, Outcome::kLocked);
    EXPECT_TRUE(store_->LoadRaw() == record);
    Reopen();
    ASSERT_EQ(coordinator_->Recover(nullptr).outcome, Outcome::kCompleted);
    AssertReadable(new_password_);
}

TEST_F(PasswordChangeTest, RefusesNonWalDurabilityAndRecoversAfterProductionReopen) {
    AddRows();
    ASSERT_TRUE(db_->Execute("PRAGMA journal_mode=DELETE"));
    const auto record = store_->LoadRaw();
    EXPECT_EQ(Change().outcome, Outcome::kLocked);
    EXPECT_TRUE(store_->LoadRaw() == record);
    Reopen();
    ASSERT_EQ(coordinator_->Recover(nullptr).outcome, Outcome::kCompleted);
    AssertReadable(new_password_);
}

TEST_F(PasswordChangeTest, RestoresOriginalExtraSynchronousPolicyAfterMigration) {
    AddRows();
    ASSERT_TRUE(db_->Execute("PRAGMA synchronous=EXTRA"));
    ASSERT_EQ(Synchronous(), 3);
    ASSERT_EQ(Change().outcome, Outcome::kCompleted);
    EXPECT_EQ(Synchronous(), 3);
    AssertReadable(new_password_);
}
