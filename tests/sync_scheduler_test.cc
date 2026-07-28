#include <gtest/gtest.h>

#include <QByteArray>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "crypto/secure_bytes.h"
#include "crypto/session_key.h"
#include "session/session_manager.h"
#include "storage/category_dao.h"
#include "storage/database.h"
#include "storage/password_dao.h"
#include "storage/schema.h"
#include "sync/cloud_storage_provider.h"
#include "sync/sync_manager.h"
#include "sync/sync_scheduler.h"

namespace {

using passvault::crypto::SecureBytes;
using passvault::crypto::SessionKey;
using passvault::session::SessionManager;
using passvault::storage::CategoryDao;
using passvault::storage::Database;
using passvault::storage::EnsureCurrentSchema;
using passvault::storage::PasswordDao;
using passvault::sync::CloudDownloadResult;
using passvault::sync::CloudStorageProvider;
using passvault::sync::SyncManager;
using passvault::sync::SyncScheduler;
using passvault::sync::UploadIfMatchStatus;

// Minimal provider used only for provider() != nullptr gating.
class NoopProvider : public CloudStorageProvider {
 public:
    QString ProviderName() const override { return QStringLiteral("noop"); }
    bool IsAuthenticated() const override { return false; }
    void SignOut() override {}
    bool UploadBackup(const QByteArray&, const QString&, QString*) override {
        return false;
    }
    std::optional<QByteArray> DownloadBackup(const QString&,
                                             QString*) override {
        return std::nullopt;
    }
    std::optional<CloudDownloadResult> DownloadBackupWithVersion(
        const QString&, QString*) override {
        return std::nullopt;
    }
    UploadIfMatchStatus UploadBackupIfMatch(const QByteArray&, const QString&,
                                            const QString&,
                                            QString*) override {
        return UploadIfMatchStatus::kError;
    }
};

SessionKey MakeKey() {
    SecureBytes bytes(SessionKey::kSize);
    for (std::size_t i = 0; i < SessionKey::kSize; ++i) {
        bytes.data()[i] = static_cast<std::uint8_t>(i + 1);
    }
    return std::move(*SessionKey::FromSecureBytes(std::move(bytes)));
}

SecureBytes MakePwd() {
    SecureBytes b;
    b.AssignFromString("hunter2");
    return b;
}

class SyncSchedulerTest : public ::testing::Test {
 protected:
    void SetUp() override {
        SessionManager::Instance()->ResetForTests();
        SessionManager::Instance()->Unlock(MakeKey(), MakePwd());
        db_ = Database::OpenInMemory();
        EnsureCurrentSchema(*db_);
        pwd_dao_ = std::make_unique<PasswordDao>(*db_);
        cat_dao_ = std::make_unique<CategoryDao>(*db_);
        sync_manager_ = std::make_unique<SyncManager>(
            pwd_dao_.get(), cat_dao_.get(), SessionManager::Instance());
        scheduler_ = std::make_unique<SyncScheduler>(sync_manager_.get());
        scheduler_->set_debounce_ms(60);
    }
    void TearDown() override { SessionManager::Instance()->ResetForTests(); }

    std::unique_ptr<Database> db_;
    std::unique_ptr<PasswordDao> pwd_dao_;
    std::unique_ptr<CategoryDao> cat_dao_;
    std::unique_ptr<SyncManager> sync_manager_;
    std::unique_ptr<SyncScheduler> scheduler_;
    NoopProvider provider_;
};

}  // namespace

TEST_F(SyncSchedulerTest, MarkDirtyIsNoopWithoutProvider) {
    QSignalSpy scheduled(scheduler_.get(), &SyncScheduler::SyncScheduled);
    scheduler_->MarkDirty();
    EXPECT_EQ(scheduled.count(), 0);
    EXPECT_FALSE(scheduler_->IsPending());
}

TEST_F(SyncSchedulerTest, MarkDirtyStartsDebounceTimer) {
    sync_manager_->set_provider(&provider_);
    QSignalSpy scheduled(scheduler_.get(), &SyncScheduler::SyncScheduled);
    scheduler_->MarkDirty();
    EXPECT_EQ(scheduled.count(), 1);
    EXPECT_TRUE(scheduler_->IsPending());
}

TEST_F(SyncSchedulerTest, RepeatedMarkDirtyResetsTimer) {
    sync_manager_->set_provider(&provider_);
    QSignalSpy triggered(scheduler_.get(), &SyncScheduler::SyncTriggered);
    scheduler_->set_debounce_ms(80);
    scheduler_->MarkDirty();
    QTest::qWait(40);
    EXPECT_EQ(triggered.count(), 0);
    scheduler_->MarkDirty();  // reset
    QTest::qWait(40);
    EXPECT_EQ(triggered.count(), 0);
    ASSERT_TRUE(triggered.wait(400));
    EXPECT_EQ(triggered.count(), 1);
}

TEST_F(SyncSchedulerTest, TimeoutTriggersSyncOnce) {
    sync_manager_->set_provider(&provider_);
    QSignalSpy triggered(scheduler_.get(), &SyncScheduler::SyncTriggered);
    QSignalSpy finished(sync_manager_.get(), &SyncManager::SyncFinished);
    scheduler_->MarkDirty();
    ASSERT_TRUE(triggered.wait(400));
    EXPECT_EQ(triggered.count(), 1);
    EXPECT_FALSE(scheduler_->IsPending());
    EXPECT_GE(finished.count(), 1);  // PerformSync fires SyncFinished (fail path here)
}

TEST_F(SyncSchedulerTest, SyncImmediatelyCancelsPendingAndRunsNow) {
    sync_manager_->set_provider(&provider_);
    QSignalSpy triggered(scheduler_.get(), &SyncScheduler::SyncTriggered);
    scheduler_->set_debounce_ms(500);
    scheduler_->MarkDirty();
    EXPECT_TRUE(scheduler_->IsPending());
    scheduler_->SyncImmediately();
    EXPECT_FALSE(scheduler_->IsPending());
    EXPECT_EQ(triggered.count(), 1);
}
