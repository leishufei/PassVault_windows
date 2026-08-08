#include <gtest/gtest.h>

#include <QByteArray>
#include <QTemporaryDir>

#include <cstring>
#include <string>

#include "crypto/sha256.h"
#include "master_password/master_password_manager.h"
#include "master_password/master_password_store.h"

namespace {

using passvault::crypto::Sha256HexLower;
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

TEST_F(MasterPasswordManagerTest, ChangePasswordRewritesRecordAndChangesKey) {
    const auto initial = manager_->SetInitial(initial_);
    ASSERT_TRUE(initial.has_value());
    const auto rec_before = store_->Load();
    ASSERT_TRUE(rec_before.has_value());

    const auto changed = manager_->ChangePassword(initial_, replacement_);
    ASSERT_TRUE(changed.has_value());

    const auto rec_after = store_->Load();
    ASSERT_TRUE(rec_after.has_value());
    EXPECT_NE(rec_after->password_hash, rec_before->password_hash);
    EXPECT_NE(rec_after->kdf_salt, rec_before->kdf_salt);

    EXPECT_FALSE(manager_->VerifyLocal(initial_).has_value());
    EXPECT_TRUE(manager_->VerifyLocal(replacement_).has_value());
}

TEST_F(MasterPasswordManagerTest, ChangePasswordRejectsBelowMinimum) {
    ASSERT_TRUE(manager_->SetInitial(initial_).has_value());
    const std::string input(
        passvault::master_password::kMinNewPasswordLength - 1, 'r');
    EXPECT_FALSE(manager_->ChangePassword(initial_, input).has_value());
    EXPECT_EQ(manager_->last_error(), VerifyError::kPasswordTooShort);
    EXPECT_TRUE(manager_->VerifyLocal(initial_).has_value());
}

TEST_F(MasterPasswordManagerTest, ChangePasswordFailsOnWrongOld) {
    ASSERT_TRUE(manager_->SetInitial(initial_).has_value());
    EXPECT_FALSE(
        manager_->ChangePassword("wrong", replacement_).has_value());
    EXPECT_TRUE(manager_->VerifyLocal(initial_).has_value());
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
