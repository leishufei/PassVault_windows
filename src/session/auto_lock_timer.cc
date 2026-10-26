#include "session/auto_lock_timer.h"

#include <QCoreApplication>
#include <QEvent>

namespace passvault::session {

AutoLockTimer::AutoLockTimer(QObject* parent) : QObject(parent), timer_(this) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &AutoLockTimer::TimedOut);
    if (auto* app = QCoreApplication::instance()) app->installEventFilter(this);
}

bool AutoLockTimer::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
            NotifyActivity();
            break;
        default:
            break;
    }
    return QObject::eventFilter(watched, event);
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
