#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QObject>

namespace passvault::ui {

// Wrapper around Win32 RegisterHotKey / WM_HOTKEY. Install into the
// QCoreApplication event filter chain before Register() is called.
class GlobalHotkey : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

 public:
    struct Modifiers {
        bool alt = false;
        bool ctrl = false;
        bool shift = false;
        bool win = false;
    };

    static GlobalHotkey* Instance();

    // Registers a hotkey identified by `id`. `key` is a Qt::Key (mapped to
    // Windows VK internally). Returns true on success. If `id` is already
    // registered it is silently replaced.
    bool Register(int id, const Modifiers& mods, int qt_key);

    // Unregisters a previously registered id. Safe to call for unknown ids.
    bool Unregister(int id);

    void UnregisterAll();

    bool nativeEventFilter(const QByteArray& event_type, void* message,
                           qintptr* result) override;

 signals:
    void HotkeyTriggered(int id);

 private:
    GlobalHotkey();
    ~GlobalHotkey() override;
    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;

    QHash<int, int> registered_ids_;  // id -> qt_key snapshot (for debug)
};

}  // namespace passvault::ui
