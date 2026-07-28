#include <gtest/gtest.h>

#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include <memory>

#include "hello/windows_hello_unlock.h"

namespace {

using passvault::hello::HelloError;
using passvault::hello::WindowsHelloUnlock;

class WindowsHelloUnlockTest : public ::testing::Test {
 protected:
    void SetUp() override {
        ASSERT_TRUE(temp_.isValid());
        path_ = temp_.path() + QStringLiteral("/hello_unlock.dat");
        unlock_ = std::make_unique<WindowsHelloUnlock>(path_);
    }

    QTemporaryDir temp_;
    QString path_;
    std::unique_ptr<WindowsHelloUnlock> unlock_;
};

TEST_F(WindowsHelloUnlockTest, UnavailableRejectsEnrollAndUnlock) {
    unlock_->set_availability_for_testing(false);
    unlock_->set_prompt_result_for_testing(true);

    EXPECT_FALSE(unlock_->IsAvailable());
    EXPECT_EQ(unlock_->last_error(), HelloError::kNotAvailable);

    EXPECT_FALSE(unlock_->Enroll(QStringLiteral("hunter2")));
    EXPECT_EQ(unlock_->last_error(), HelloError::kNotAvailable);
    EXPECT_FALSE(QFile::exists(path_));

    auto unlocked = unlock_->Unlock();
    EXPECT_FALSE(unlocked.has_value());
    EXPECT_EQ(unlock_->last_error(), HelloError::kNotEnrolled);
}

TEST_F(WindowsHelloUnlockTest, EnrollThenUnlockRoundTrip) {
    unlock_->set_availability_for_testing(true);
    unlock_->set_prompt_result_for_testing(true);

    ASSERT_TRUE(unlock_->Enroll(QStringLiteral("hunter2-ünicöde")));
    EXPECT_EQ(unlock_->last_error(), HelloError::kOk);
    EXPECT_TRUE(unlock_->IsEnrolled());
    EXPECT_TRUE(QFile::exists(path_));

    auto recovered = unlock_->Unlock();
    ASSERT_TRUE(recovered.has_value());
    EXPECT_EQ(*recovered, QStringLiteral("hunter2-ünicöde"));
    EXPECT_EQ(unlock_->last_error(), HelloError::kOk);
}

TEST_F(WindowsHelloUnlockTest, EnrollDeniedAtPrompt) {
    unlock_->set_availability_for_testing(true);
    unlock_->set_prompt_result_for_testing(false);

    EXPECT_FALSE(unlock_->Enroll(QStringLiteral("hunter2")));
    EXPECT_EQ(unlock_->last_error(), HelloError::kUserCancelled);
    EXPECT_FALSE(unlock_->IsEnrolled());
    EXPECT_FALSE(QFile::exists(path_));
}

TEST_F(WindowsHelloUnlockTest, UnlockDeniedAtPromptKeepsBlob) {
    unlock_->set_availability_for_testing(true);
    unlock_->set_prompt_result_for_testing(true);
    ASSERT_TRUE(unlock_->Enroll(QStringLiteral("hunter2")));
    ASSERT_TRUE(QFile::exists(path_));

    unlock_->set_prompt_result_for_testing(false);
    auto res = unlock_->Unlock();
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(unlock_->last_error(), HelloError::kUserCancelled);
    EXPECT_TRUE(QFile::exists(path_));  // blob must not be deleted on cancel
}

TEST_F(WindowsHelloUnlockTest, UnlockWithoutEnrollmentFailsFast) {
    unlock_->set_availability_for_testing(true);
    unlock_->set_prompt_result_for_testing(true);

    auto res = unlock_->Unlock();
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(unlock_->last_error(), HelloError::kNotEnrolled);
}

TEST_F(WindowsHelloUnlockTest, DisableClearsBlob) {
    unlock_->set_availability_for_testing(true);
    unlock_->set_prompt_result_for_testing(true);
    ASSERT_TRUE(unlock_->Enroll(QStringLiteral("hunter2")));
    ASSERT_TRUE(QFile::exists(path_));

    EXPECT_TRUE(unlock_->Disable());
    EXPECT_FALSE(unlock_->IsEnrolled());
    EXPECT_FALSE(QFile::exists(path_));

    // Idempotent: Disable when nothing to disable returns true.
    EXPECT_TRUE(unlock_->Disable());
    EXPECT_EQ(unlock_->last_error(), HelloError::kOk);
}

TEST_F(WindowsHelloUnlockTest, SecondInstanceCanUnlockPreviousBlob) {
    // Emulates process restart: same storage path, brand-new instance still
    // recovers the previously stored master password.
    unlock_->set_availability_for_testing(true);
    unlock_->set_prompt_result_for_testing(true);
    ASSERT_TRUE(unlock_->Enroll(QStringLiteral("across-restarts")));

    WindowsHelloUnlock next(path_);
    next.set_availability_for_testing(true);
    next.set_prompt_result_for_testing(true);
    EXPECT_TRUE(next.IsEnrolled());
    auto recovered = next.Unlock();
    ASSERT_TRUE(recovered.has_value());
    EXPECT_EQ(*recovered, QStringLiteral("across-restarts"));
}

TEST_F(WindowsHelloUnlockTest, EmptyMasterPasswordRoundTrips) {
    unlock_->set_availability_for_testing(true);
    unlock_->set_prompt_result_for_testing(true);
    ASSERT_TRUE(unlock_->Enroll(QString{}));
    auto recovered = unlock_->Unlock();
    ASSERT_TRUE(recovered.has_value());
    EXPECT_TRUE(recovered->isEmpty());
}

}  // namespace
