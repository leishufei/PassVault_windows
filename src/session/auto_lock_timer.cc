#include "session/auto_lock_timer.h"

namespace passvault::session {

AutoLockTimer::AutoLockTimer(QObject* parent) : QObject(parent), timer_(this) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &AutoLockTimer::TimedOut);
}

void AutoLockTimer::SetTimeoutMs(int timeout_ms) {
    if (timeout_ms <= 0) return;
    timeout_ms_ = timeout_ms;
    if (timer_.isActive()) timer_.start(timeout_ms_);
}

void AutoLockTimer::Start() { timer_.start(timeout_ms_); }

void AutoLockTimer::Stop() { timer_.stop(); }

void AutoLockTimer::NotifyActivity() {
    if (timer_.isActive()) timer_.start(timeout_ms_);
}

}  // namespace passvault::session
