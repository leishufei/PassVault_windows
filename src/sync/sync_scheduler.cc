#include "sync/sync_scheduler.h"

#include <QTimer>

#include "sync/sync_manager.h"

namespace passvault::sync {

SyncScheduler::SyncScheduler(SyncManager* sync_manager, QObject* parent)
    : QObject(parent), sync_manager_(sync_manager), timer_(new QTimer(this)) {
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &SyncScheduler::OnTimeout);
}

void SyncScheduler::SetPaused(bool paused) {
    paused_ = paused;
    if (sync_manager_) sync_manager_->set_paused(paused);
    if (paused) {
        dirty_ = dirty_ || timer_->isActive();
        timer_->stop();
    } else if (dirty_) {
        dirty_ = false;
        MarkDirty();
    }
}

void SyncScheduler::MarkDirty() {
    if (paused_) {
        dirty_ = true;
        return;
    }
    if (!sync_manager_ || !sync_manager_->provider()) return;
    dirty_ = true;
    timer_->start(debounce_ms_);
    emit SyncScheduled();
}

void SyncScheduler::SyncImmediately() {
    if (paused_) { dirty_ = true; return; }
    timer_->stop();
    if (!sync_manager_) return;
    if (sync_manager_->is_busy()) {
        MarkDirty();
        return;
    }
    dirty_ = false;
    emit SyncTriggered();
    sync_manager_->PerformSync();
}

bool SyncScheduler::IsPending() const {
    return timer_->isActive();
}

void SyncScheduler::OnTimeout() {
    if (paused_) return;
    if (!sync_manager_) return;
    if (sync_manager_->is_busy()) {
        MarkDirty();
        return;
    }
    dirty_ = false;
    emit SyncTriggered();
    sync_manager_->PerformSync();
}

}  // namespace passvault::sync
