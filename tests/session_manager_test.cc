#include <gtest/gtest.h>

#include <QSignalSpy>

#include <utility>

#include "crypto/secure_bytes.h"
#include "crypto/session_key.h"
#include "session/session_manager.h"

namespace {

using passvault::crypto::SecureBytes;
using passvault::crypto::SessionKey;
using passvault::session::SessionManager;

SessionKey MakeKey(std::uint8_t seed) {
    SecureBytes bytes(SessionKey::kSize);
    for (std::size_t i = 0; i < SessionKey::kSize; ++i) {
        bytes.data()[i] = static_cast<std::uint8_t>(seed + i);
    }
    auto key = SessionKey::FromSecureBytes(std::move(bytes));
    return std::move(*key);
}

SecureBytes MakePwd(const char* s) {
    SecureBytes b;
    b.AssignFromString(s);
    return b;
}

class SessionManagerTest : public ::testing::Test {
 protected:
    void SetUp() override { SessionManager::Instance()->ResetForTests(); }
    void TearDown() override { SessionManager::Instance()->ResetForTests(); }
};

}  // namespace

TEST_F(SessionManagerTest, StartsLocked) {
    EXPECT_FALSE(SessionManager::Instance()->IsUnlocked());
    EXPECT_EQ(SessionManager::Instance()->session_key(), nullptr);
    EXPECT_EQ(SessionManager::Instance()->master_password(), nullptr);
}

TEST_F(SessionManagerTest, UnlockExposesKeyAndPassword) {
    auto* mgr = SessionManager::Instance();
    mgr->Unlock(MakeKey(1), MakePwd("hunter2"));
    ASSERT_TRUE(mgr->IsUnlocked());
    ASSERT_NE(mgr->session_key(), nullptr);
    EXPECT_EQ(mgr->session_key()->size(), 32u);
    EXPECT_EQ(mgr->session_key()->data()[0], 1);
    ASSERT_NE(mgr->master_password(), nullptr);
    EXPECT_EQ(mgr->master_password()->size(), 7u);
}

TEST_F(SessionManagerTest, LockClearsState) {
    auto* mgr = SessionManager::Instance();
    mgr->Unlock(MakeKey(1), MakePwd("hunter2"));
    mgr->Lock();
    EXPECT_FALSE(mgr->IsUnlocked());
    EXPECT_EQ(mgr->session_key(), nullptr);
    EXPECT_EQ(mgr->master_password(), nullptr);
}

TEST_F(SessionManagerTest, LockChangedSignalEmittedOnTransition) {
    auto* mgr = SessionManager::Instance();
    QSignalSpy spy(mgr, &SessionManager::LockChanged);

    mgr->Unlock(MakeKey(1), MakePwd("hunter2"));
    ASSERT_EQ(spy.count(), 1);
    EXPECT_FALSE(spy.at(0).at(0).toBool());

    mgr->Lock();
    ASSERT_EQ(spy.count(), 2);
    EXPECT_TRUE(spy.at(1).at(0).toBool());
}

TEST_F(SessionManagerTest, LockWhileAlreadyLockedNoSignal) {
    auto* mgr = SessionManager::Instance();
    QSignalSpy spy(mgr, &SessionManager::LockChanged);
    mgr->Lock();
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(SessionManagerTest, UnlockWhileUnlockedReplacesSilently) {
    auto* mgr = SessionManager::Instance();
    mgr->Unlock(MakeKey(1), MakePwd("first"));
    QSignalSpy spy(mgr, &SessionManager::LockChanged);
    mgr->Unlock(MakeKey(2), MakePwd("second"));
    EXPECT_EQ(spy.count(), 0);
    EXPECT_EQ(mgr->session_key()->data()[0], 2);
}
