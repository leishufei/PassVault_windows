#pragma once

#include <QObject>
#include <QString>

class QTimer;

namespace passvault::ui {

// Copies sensitive text to the system clipboard and clears it after a timeout
// (default 30s). If the clipboard content has been replaced by another writer
// during the countdown, the clear is skipped to avoid nuking unrelated data.
class ClipboardManager : public QObject {
    Q_OBJECT

 public:
    static constexpr int kDefaultTimeoutMs = 30 * 1000;

    static ClipboardManager* Instance();

    void CopySensitive(const QString& text, int timeout_ms = kDefaultTimeoutMs);
    void CopyPlain(const QString& text);
    void CancelPendingClear();

    int timeout_ms() const { return default_timeout_ms_; }
    void set_default_timeout_ms(int ms) { default_timeout_ms_ = ms; }

 signals:
    void ClearScheduled(int timeout_ms);
    void Cleared();
    void ClearSkippedBecauseChanged();

 private:
    ClipboardManager();
    ~ClipboardManager() override;
    ClipboardManager(const ClipboardManager&) = delete;
    ClipboardManager& operator=(const ClipboardManager&) = delete;

    void OnTimeout();

    QTimer* timer_ = nullptr;
    QString expected_text_;
    int default_timeout_ms_ = kDefaultTimeoutMs;
};

}  // namespace passvault::ui
