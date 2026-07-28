#include "sync/sync_scheduler.h"

#include <QTimer>

#include "sync/sync_manager.h"

namespace passvault::sync {

SyncScheduler::SyncScheduler(SyncManager* sync_manager, QObject* parent)
    : QObject(parent), sync_manager_(sync_manager), timer_(new QTimer(this)) {
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &SyncScheduler::OnTimeout);
}

void SyncScheduler::MarkDirty() {
    if (!sync_manager_ || !sync_manager_->provider()) return;
    timer_->start(debounce_ms_);
    emit SyncScheduled();
}

void SyncScheduler::SyncImmediately() {
    timer_->stop();
    if (!sync_manager_) return;
    emit SyncTriggered();
    sync_manager_->PerformSync();
}

bool SyncScheduler::IsPending() const {
    return timer_->isActive();
}

void SyncScheduler::OnTimeout() {
    if (!sync_manager_) return;
    emit SyncTriggered();
    sync_manager_->PerformSync();
}

}  // namespace passvault::sync
