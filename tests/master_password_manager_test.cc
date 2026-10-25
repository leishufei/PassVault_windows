#include <gtest/gtest.h>

#include <QByteArray>
#include <QTemporaryDir>

#include <cstring>
#include <string>

#include "crypto/crypto_service.h"
#include "crypto/random.h"
#include "crypto/session_key.h"
#include "crypto/sha256.h"
#include "master_password/master_password_manager.h"
#include "master_password/master_password_store.h"
#include "model/password_entry.h"
#include "storage/database.h"
#include "storage/password_dao.h"
#include "storage/schema.h"

namespace {

using passvault::crypto::CryptoService;
using passvault::crypto::Sha256HexLower;
using passvault::master_password::ChangeError;
using passvault::master_password::MasterPasswordManager;
using passvault::master_password::MasterPasswordStore;
using passvault::master_password::VerifyError;

class MasterPasswordManagerTest : public ::testing::Test {
 protected:
    void SetUp() override {
        ASSERT_TRUE(dir_.isValid());
        store_ = std::make_unique<MasterPasswordStore>(
            dir_.filePath("master.dat"));
        db_ = passvault::storage::Database::OpenInMemory();
        ASSERT_TRUE(db_ != nullptr);
        passvault::storage::EnsureCurrentSchema(*db_);
        dao_ = std::make_unique<passvault::storage::PasswordDao>(*db_);
        manager_ = std::make_unique<MasterPasswordManager>(*store_);
        manager_->set_password_dao(dao_.get());
    }

    // Encrypts `plain` with `key` and inserts a row, mirroring the app's
    // own write path.
    std::int64_t InsertEncrypted(const passvault::crypto::SessionKey& key,
                                 const QString& plain,
                                 const QString& uuid = QStringLiteral(
                                     "uuid-1")) {
        const QByteArray pt = plain.toUtf8();
        const auto iv = passvault::crypto::Random::Bytes(CryptoService::kIvSize);
        const QByteArray ct = CryptoService::EncryptGcm(
            key.data(), key.size(), iv.data(), iv.size(),
            reinterpret_cast<const std::uint8_t*>(pt.constData()),
            static_cast<std::size_t>(pt.size()));
        passvault::model::PasswordEntry entry;
        entry.uuid = uuid;
        entry.title = QStringLiteral("t");
        entry.encrypted_password = ct;
        entry.password_iv =
            QByteArray(reinterpret_cast<const char*>(iv.data()),
                       static_cast<int>(iv.size()));
        return dao_->Insert(entry).value_or(-1);
    }

    QString DecryptWith(const passvault::crypto::SessionKey& key,
                        std::int64_t id) {
        const auto entry = dao_->FindById(id);
        if (!entry.has_value()) return QStringLiteral("<missing>");
        auto plain = CryptoService::DecryptGcm(
            key.data(), key.size(),
            reinterpret_cast<const std::uint8_t*>(
                entry->password_iv.constData()),
            static_cast<std::size_t>(entry->password_iv.size()),
            reinterpret_cast<const std::uint8_t*>(
                entry->encrypted_password.constData()),
            static_cast<std::size_t>(entry->encrypted_password.size()));
        if (!plain.has_value()) return QStringLiteral("<undecryptable>");
        return QString::fromUtf8(*plain);
    }

    QTemporaryDir dir_;
    std::unique_ptr<MasterPasswordStore> store_;
    std::unique_ptr<passvault::storage::Database> db_;
    std::unique_ptr<passvault::storage::PasswordDao> dao_;
    std::unique_ptr<MasterPasswordManager> manager_;
    const std::string initial_ = std::string(
        passvault::master_password::kMinNewPasswordLength, 'i');
    const std::string replacement_ = std::string(
        passvault::master_password::kMinNewPasswordLength, 'r');
};

}  // namespace

TEST_F(MasterPasswordManagerTest, IsInitializedTracksStore) {
    EXPECT_FALSE(manager_->IsInitialized());
    ASSERT_TRUE(manager_->SetInitial(initial_).has_value());
    EXPECT_TRUE(manager_->IsInitialized());
}

TEST_F(MasterPasswordManagerTest, SetInitialReturnsSessionKeyAndPassword) {
    const auto out = manager_->SetInitial(initial_);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->session_key.size(), 32u);

    const auto* pwd = out->master_password.data();
    ASSERT_NE(pwd, nullptr);
    EXPECT_EQ(std::memcmp(pwd, initial_.data(), initial_.size()), 0);
}

TEST_F(MasterPasswordManagerTest, SetInitialRejectsPasswordBelowMinimum) {
    const std::string input(
        passvault::master_password::kMinNewPasswordLength - 1, 'i');
    EXPECT_FALSE(manager_->SetInitial(input).has_value());
    EXPECT_EQ(manager_->last_error(), VerifyError::kPasswordTooShort);
}

TEST_F(MasterPasswordManagerTest, SetInitialRejectsWhenAlreadyExists) {
    ASSERT_TRUE(manager_->SetInitial(initial_).has_value());
    EXPECT_FALSE(manager_->SetInitial(replacement_).has_value());
}

TEST_F(MasterPasswordManagerTest, VerifyLocalNotInitializedError) {
    EXPECT_FALSE(manager_->VerifyLocal(initial_).has_value());
    EXPECT_EQ(manager_->last_error(), VerifyError::kNotInitialized);
}

TEST_F(MasterPasswordManagerTest, VerifyLocalWrongPassword) {
    ASSERT_TRUE(manager_->SetInitial(initial_).has_value());
    EXPECT_FALSE(manager_->VerifyLocal("wrong").has_value());
    EXPECT_EQ(manager_->last_error(), VerifyError::kWrongPassword);
}

TEST_F(MasterPasswordManagerTest, VerifyLocalCorrectPasswordSucceedsAndKeyStable) {
    const auto first = manager_->SetInitial(initial_);
    ASSERT_TRUE(first.has_value());
    const auto second = manager_->VerifyLocal(initial_);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->session_key.size(), 32u);
    EXPECT_EQ(std::memcmp(first->session_key.data(),
                          second->session_key.data(), 32),
              0);
}

TEST_F(MasterPasswordManagerTest, HashMatchesAndroidFormat) {
    ASSERT_TRUE(manager_->SetInitial(initial_).has_value());
    const auto record = store_->Load();
    ASSERT_TRUE(record.has_value());
    const auto expected = QByteArray::fromStdString(Sha256HexLower(initial_));
    EXPECT_EQ(record->password_hash, expected);
}

TEST_F(MasterPasswordManagerTest, ChangeRewritesRecordAndChangesKey) {
    const auto initial = manager_->SetInitial(initial_);
    ASSERT_TRUE(initial.has_value());
    const auto rec_before = store_->Load();
    ASSERT_TRUE(rec_before.has_value());

    const auto result = manager_->PrepareChange(
        QString::fromStdString(initial_), QString::fromStdString(replacement_));
    ASSERT_TRUE(result.success) << result.message.toStdString();
    ASSERT_TRUE(result.payload.has_value());

    const auto rec_after = store_->Load();
    ASSERT_TRUE(rec_after.has_value());
    EXPECT_NE(rec_after->password_hash, rec_before->password_hash);
    EXPECT_NE(rec_after->kdf_salt, rec_before->kdf_salt);

    EXPECT_FALSE(manager_->VerifyLocal(initial_).has_value());
    EXPECT_TRUE(manager_->VerifyLocal(replacement_).has_value());
}

TEST_F(MasterPasswordManagerTest, ChangeRejectsBelowMinimum) {
    ASSERT_TRUE(manager_->SetInitial(initial_).has_value());
    const std::string input(
        passvault::master_password::kMinNewPasswordLength - 1, 'r');
    const auto result = manager_->PrepareChange(
        QString::fromStdString(initial_), QString::fromStdString(input));
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, ChangeError::kPasswordTooShort);
    EXPECT_TRUE(manager_->VerifyLocal(initial_).has_value());
}

TEST_F(MasterPasswordManagerTest, ChangeFailsOnWrongOld) {
    ASSERT_TRUE(manager_->SetInitial(initial_).has_value());
    const auto result = manager_->PrepareChange(
        QStringLiteral("wrong"), QString::fromStdString(replacement_));
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, ChangeError::kWrongPassword);
    EXPECT_TRUE(manager_->VerifyLocal(initial_).has_value());
}

TEST_F(MasterPasswordManagerTest, ChangeRejectsUnchangedPassword) {
    ASSERT_TRUE(manager_->SetInitial(initial_).has_value());
    const auto result = manager_->PrepareChange(
        QString::fromStdString(initial_), QString::fromStdString(initial_));
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, ChangeError::kPasswordUnchanged);
}

TEST_F(MasterPasswordManagerTest, ChangeReencryptsEveryRowIncludingTombstones) {
    const auto initial = manager_->SetInitial(initial_);
    ASSERT_TRUE(initial.has_value());
    const std::int64_t active_id =
        InsertEncrypted(initial->session_key, QStringLiteral("secret-a"),
                        QStringLiteral("uuid-active"));
    const std::int64_t tombstone_id =
        InsertEncrypted(initial->session_key, QStringLiteral("secret-b"),
                        QStringLiteral("uuid-tombstone"));
    ASSERT_GT(active_id, 0);
    ASSERT_GT(tombstone_id, 0);
    ASSERT_TRUE(dao_->LogicalDelete(tombstone_id, 1));

    // A row with no ciphertext must survive the rewrite untouched.
    passvault::model::PasswordEntry empty;
    empty.uuid = QStringLiteral("uuid-empty");
    empty.title = QStringLiteral("empty");
    const auto empty_id = dao_->Insert(empty);
    ASSERT_TRUE(empty_id.has_value());

    const auto result = manager_->PrepareChange(
        QString::fromStdString(initial_), QString::fromStdString(replacement_));
    ASSERT_TRUE(result.success) << result.message.toStdString();
    ASSERT_TRUE(result.payload.has_value());

    EXPECT_EQ(DecryptWith(result.payload->session_key, active_id),
              QStringLiteral("secret-a"));
    EXPECT_EQ(DecryptWith(result.payload->session_key, tombstone_id),
              QStringLiteral("secret-b"));
    // The old key must no longer open either row.
    EXPECT_EQ(DecryptWith(initial->session_key, active_id),
              QStringLiteral("<undecryptable>"));
    EXPECT_EQ(DecryptWith(initial->session_key, tombstone_id),
              QStringLiteral("<undecryptable>"));

    const auto empty_row = dao_->FindById(*empty_id);
    ASSERT_TRUE(empty_row.has_value());
    EXPECT_TRUE(empty_row->encrypted_password.isEmpty());
    EXPECT_TRUE(empty_row->password_iv.isEmpty());
}

TEST_F(MasterPasswordManagerTest, ProbeSeqA) {
    const auto initial = manager_->SetInitial(initial_);
    ASSERT_TRUE(initial.has_value());
    const std::int64_t good_id =
        InsertEncrypted(initial->session_key, QStringLiteral("secret-a"));
    EXPECT_GT(good_id, 0);

    const auto foreign_bytes = passvault::crypto::Random::Bytes(32);
    const auto foreign = passvault::crypto::SessionKey::FromSecureBytes(
        passvault::crypto::SecureBytes(foreign_bytes.data(),
                                       foreign_bytes.size()));
    ASSERT_TRUE(foreign.has_value());
    const std::int64_t foreign_id =
        InsertEncrypted(*foreign, QStringLiteral("secret-foreign"));
    EXPECT_GT(foreign_id, 0);

    const auto result = manager_->PrepareChange(
        QString::fromStdString(initial_), QString::fromStdString(replacement_));
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, ChangeError::kDecryptFailed);
}

TEST_F(MasterPasswordManagerTest, ChangeRefusesUndecryptableRowAndWritesNothing) {
    const auto initial = manager_->SetInitial(initial_);
    ASSERT_TRUE(initial.has_value());
    const std::int64_t good_id =
        InsertEncrypted(initial->session_key, QStringLiteral("secret-a"),
                        QStringLiteral("uuid-good"));

    // A row whose ciphertext was produced with some other key.
    const auto foreign_bytes = passvault::crypto::Random::Bytes(32);
    const auto foreign = passvault::crypto::SessionKey::FromSecureBytes(
        passvault::crypto::SecureBytes(foreign_bytes.data(),
                                       foreign_bytes.size()));
    ASSERT_TRUE(foreign.has_value());
    const std::int64_t foreign_id =
        InsertEncrypted(*foreign, QStringLiteral("secret-foreign"),
                        QStringLiteral("uuid-foreign"));

    const auto record_before = store_->Load();
    ASSERT_TRUE(record_before.has_value());

    const auto result = manager_->PrepareChange(
        QString::fromStdString(initial_), QString::fromStdString(replacement_));
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, ChangeError::kDecryptFailed);

    // Neither the record nor any row moved.
    const auto record_after = store_->Load();
    ASSERT_TRUE(record_after.has_value());
    EXPECT_EQ(record_after->password_hash, record_before->password_hash);
    EXPECT_EQ(record_after->kdf_salt, record_before->kdf_salt);
    EXPECT_EQ(DecryptWith(initial->session_key, good_id),
              QStringLiteral("secret-a"));
    EXPECT_EQ(DecryptWith(*foreign, foreign_id),
              QStringLiteral("secret-foreign"));
    EXPECT_TRUE(manager_->VerifyLocal(initial_).has_value());
}

TEST_F(MasterPasswordManagerTest, RollBackRestoresRecordAndRows) {
    const auto initial = manager_->SetInitial(initial_);
    ASSERT_TRUE(initial.has_value());
    const std::int64_t id =
        InsertEncrypted(initial->session_key, QStringLiteral("secret-a"));
    const auto record_before = store_->Load();
    ASSERT_TRUE(record_before.has_value());

    const auto result = manager_->PrepareChange(
        QString::fromStdString(initial_), QString::fromStdString(replacement_));
    ASSERT_TRUE(result.success) << result.message.toStdString();
    ASSERT_TRUE(manager_->HasPendingChange());

    ASSERT_TRUE(manager_->RollBack());
    EXPECT_FALSE(manager_->HasPendingChange());

    const auto record_after = store_->Load();
    ASSERT_TRUE(record_after.has_value());
    EXPECT_EQ(record_after->password_hash, record_before->password_hash);
    EXPECT_EQ(record_after->kdf_salt, record_before->kdf_salt);

    // The old password works again and still opens the untouched ciphertext.
    const auto reopened = manager_->VerifyLocal(initial_);
    ASSERT_TRUE(reopened.has_value());
    EXPECT_EQ(DecryptWith(reopened->session_key, id),
              QStringLiteral("secret-a"));
    EXPECT_FALSE(manager_->VerifyLocal(replacement_).has_value());
}

TEST_F(MasterPasswordManagerTest, CommitChangeDropsRollbackPoint) {
    const auto initial = manager_->SetInitial(initial_);
    ASSERT_TRUE(initial.has_value());
    const std::int64_t id =
        InsertEncrypted(initial->session_key, QStringLiteral("secret-a"));

    const auto result = manager_->PrepareChange(
        QString::fromStdString(initial_), QString::fromStdString(replacement_));
    ASSERT_TRUE(result.success) << result.message.toStdString();
    manager_->CommitChange();
    EXPECT_FALSE(manager_->HasPendingChange());

    // Rolling back after a commit is a no-op, so the new key stands.
    EXPECT_TRUE(manager_->RollBack());
    const auto reopened = manager_->VerifyLocal(replacement_);
    ASSERT_TRUE(reopened.has_value());
    EXPECT_EQ(DecryptWith(reopened->session_key, id),
              QStringLiteral("secret-a"));
}

TEST_F(MasterPasswordManagerTest, VerifyLocalAcceptsLegacyShortPassword) {
    const std::string legacy_input = "x";
    passvault::master_password::MasterRecord record;
    record.password_hash =
        QByteArray::fromStdString(Sha256HexLower(legacy_input));
    record.kdf_salt = QByteArray(MasterPasswordStore::kKdfSaltSize, 's');
    ASSERT_TRUE(store_->Save(record));
    EXPECT_TRUE(manager_->VerifyLocal(legacy_input).has_value());
}

TEST_F(MasterPasswordManagerTest, ResetRemovesFile) {
    ASSERT_TRUE(manager_->SetInitial(initial_).has_value());
    EXPECT_TRUE(manager_->Reset());
    EXPECT_FALSE(manager_->IsInitialized());
}
