#pragma once

#include <QObject>
#include <QTimer>

namespace passvault::session {

class AutoLockTimer : public QObject {
    Q_OBJECT

 public:
    static constexpr int kDefaultTimeoutMs = 5 * 60 * 1000;

    explicit AutoLockTimer(QObject* parent = nullptr);

    int timeout_ms() const { return timeout_ms_; }
    void SetTimeoutMs(int timeout_ms);

    bool IsActive() const { return timer_.isActive(); }

 public slots:
    void Start();
    void Stop();
    void NotifyActivity();

 signals:
    void TimedOut();

 private:
    QTimer timer_;
    int timeout_ms_ = kDefaultTimeoutMs;
};

}  // namespace passvault::session
