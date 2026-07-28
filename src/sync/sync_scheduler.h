#pragma once

#include <QObject>
#include <QString>

class QTimer;

namespace passvault::sync {

class SyncManager;

class SyncScheduler : public QObject {
    Q_OBJECT

 public:
    static constexpr int kDefaultDebounceMs = 5000;

    explicit SyncScheduler(SyncManager* sync_manager,
                           QObject* parent = nullptr);

    void set_debounce_ms(int ms) { debounce_ms_ = ms; }
    int debounce_ms() const { return debounce_ms_; }

    void MarkDirty();

    void SyncImmediately();

    bool IsPending() const;

 signals:
    void SyncScheduled();
    void SyncTriggered();

 private:
    void OnTimeout();

    SyncManager* sync_manager_;
    QTimer* timer_;
    int debounce_ms_ = kDefaultDebounceMs;
};

}  // namespace passvault::sync
