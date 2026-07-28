#include <gtest/gtest.h>

#include <QSignalSpy>

#include "session/auto_lock_timer.h"

namespace {

using passvault::session::AutoLockTimer;

}  // namespace

TEST(AutoLockTimer, DefaultsToFiveMinutes) {
    AutoLockTimer timer;
    EXPECT_EQ(timer.timeout_ms(), 5 * 60 * 1000);
    EXPECT_FALSE(timer.IsActive());
}

TEST(AutoLockTimer, StartActivatesAndFiresOnce) {
    AutoLockTimer timer;
    timer.SetTimeoutMs(30);
    QSignalSpy spy(&timer, &AutoLockTimer::TimedOut);
    timer.Start();
    EXPECT_TRUE(timer.IsActive());
    ASSERT_TRUE(spy.wait(500));
    EXPECT_EQ(spy.count(), 1);
    EXPECT_FALSE(timer.IsActive());
}

TEST(AutoLockTimer, StopPreventsFire) {
    AutoLockTimer timer;
    timer.SetTimeoutMs(30);
    QSignalSpy spy(&timer, &AutoLockTimer::TimedOut);
    timer.Start();
    timer.Stop();
    EXPECT_FALSE(timer.IsActive());
    EXPECT_FALSE(spy.wait(80));
    EXPECT_EQ(spy.count(), 0);
}

TEST(AutoLockTimer, NotifyActivityResetsWhileActive) {
    AutoLockTimer timer;
    timer.SetTimeoutMs(80);
    QSignalSpy spy(&timer, &AutoLockTimer::TimedOut);
    timer.Start();

    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(spy.wait(40));
        timer.NotifyActivity();
    }
    EXPECT_EQ(spy.count(), 0);
    ASSERT_TRUE(spy.wait(400));
    EXPECT_EQ(spy.count(), 1);
}

TEST(AutoLockTimer, NotifyActivityIsNoopWhenStopped) {
    AutoLockTimer timer;
    timer.SetTimeoutMs(30);
    QSignalSpy spy(&timer, &AutoLockTimer::TimedOut);
    timer.NotifyActivity();
    EXPECT_FALSE(timer.IsActive());
    EXPECT_FALSE(spy.wait(80));
}

TEST(AutoLockTimer, SetTimeoutIgnoresNonPositive) {
    AutoLockTimer timer;
    timer.SetTimeoutMs(1234);
    timer.SetTimeoutMs(0);
    EXPECT_EQ(timer.timeout_ms(), 1234);
    timer.SetTimeoutMs(-1);
    EXPECT_EQ(timer.timeout_ms(), 1234);
}
