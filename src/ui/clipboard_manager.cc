#include "ui/clipboard_manager.h"

#include <QApplication>
#include <QClipboard>
#include <QTimer>

namespace passvault::ui {

ClipboardManager* ClipboardManager::Instance() {
    static ClipboardManager* instance = new ClipboardManager();
    return instance;
}

ClipboardManager::ClipboardManager() {
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &ClipboardManager::OnTimeout);
}

ClipboardManager::~ClipboardManager() = default;

void ClipboardManager::CopySensitive(const QString& text, int timeout_ms) {
    QClipboard* clip = QGuiApplication::clipboard();
    if (!clip) return;
    clip->setText(text);
    expected_text_ = text;
    timer_->start(timeout_ms > 0 ? timeout_ms : default_timeout_ms_);
    emit ClearScheduled(timer_->interval());
}

void ClipboardManager::CopyPlain(const QString& text) {
    QClipboard* clip = QGuiApplication::clipboard();
    if (!clip) return;
    clip->setText(text);
    CancelPendingClear();
}

void ClipboardManager::CancelPendingClear() {
    timer_->stop();
    expected_text_.clear();
}

void ClipboardManager::OnTimeout() {
    QClipboard* clip = QGuiApplication::clipboard();
    if (!clip) return;
    if (clip->text() != expected_text_) {
        expected_text_.clear();
        emit ClearSkippedBecauseChanged();
        return;
    }
    clip->clear();
    expected_text_.clear();
    emit Cleared();
}

}  // namespace passvault::ui
