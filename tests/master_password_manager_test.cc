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
        manager_ = std::make_unique<MasterPasswordManager>(*store_);
    }

    QTemporaryDir dir_;
    std::unique_ptr<MasterPasswordStore> store_;
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

TEST_F(MasterPasswordManagerTest, ChangeRejectsEmptyPassword) {
    ASSERT_TRUE(manager_->SetInitial(initial_).has_value());
    const std::string input;
    QByteArray candidate;
    const auto result = manager_->BuildChange(
        QString::fromStdString(initial_), QString::fromStdString(input), &candidate);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, ChangeError::kPasswordTooShort);
    EXPECT_TRUE(manager_->VerifyLocal(initial_).has_value());
}

TEST_F(MasterPasswordManagerTest, ChangeAcceptsShortPasswordWithoutReplacingOldRecord) {
    ASSERT_TRUE(manager_->SetInitial(initial_).has_value());
    QByteArray candidate;
    const auto result = manager_->BuildChange(
        QString::fromStdString(initial_), QStringLiteral("x"), &candidate);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(candidate.isEmpty());
    EXPECT_TRUE(manager_->VerifyLocal(initial_).has_value());
}

TEST_F(MasterPasswordManagerTest, ChangeFailsOnWrongOld) {
    ASSERT_TRUE(manager_->SetInitial(initial_).has_value());
    QByteArray candidate;
    const auto result = manager_->BuildChange(
        QStringLiteral("wrong"), QString::fromStdString(replacement_), &candidate);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, ChangeError::kWrongPassword);
    EXPECT_TRUE(manager_->VerifyLocal(initial_).has_value());
}

TEST_F(MasterPasswordManagerTest, ChangeRejectsUnchangedPassword) {
    ASSERT_TRUE(manager_->SetInitial(initial_).has_value());
    QByteArray candidate;
    const auto result = manager_->BuildChange(
        QString::fromStdString(initial_), QString::fromStdString(initial_), &candidate);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, ChangeError::kPasswordUnchanged);
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

TEST_F(MasterPasswordManagerTest, BuildsProtectedCandidateWithoutMutatingStore) {
    ASSERT_TRUE(manager_->SetInitial(initial_));
    const auto before = store_->LoadRaw();
    QByteArray candidate;
    auto result = manager_->BuildChange(QString::fromStdString(initial_),
        QString::fromStdString(replacement_), &candidate);
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.payload);
    EXPECT_EQ(store_->LoadRaw(), before);
    ASSERT_TRUE(store_->SaveRaw(candidate));
    EXPECT_TRUE(manager_->VerifyLocal(replacement_));
    EXPECT_FALSE(manager_->VerifyLocal(initial_));
}
