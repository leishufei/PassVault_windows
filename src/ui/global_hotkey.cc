#include "ui/global_hotkey.h"

#include <QCoreApplication>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace passvault::ui {

namespace {

#ifdef _WIN32
UINT ToWinModifiers(const GlobalHotkey::Modifiers& m) {
    UINT out = 0;
    if (m.alt) out |= MOD_ALT;
    if (m.ctrl) out |= MOD_CONTROL;
    if (m.shift) out |= MOD_SHIFT;
    if (m.win) out |= MOD_WIN;
    out |= MOD_NOREPEAT;
    return out;
}

UINT QtKeyToVk(int qt_key) {
    if (qt_key >= 0x41 && qt_key <= 0x5A) return static_cast<UINT>(qt_key);
    if (qt_key >= 0x30 && qt_key <= 0x39) return static_cast<UINT>(qt_key);
    switch (qt_key) {
        case 0x01000030: return VK_F1;
        case 0x01000031: return VK_F2;
        case 0x01000032: return VK_F3;
        case 0x01000033: return VK_F4;
        case 0x01000034: return VK_F5;
        case 0x01000035: return VK_F6;
        case 0x01000036: return VK_F7;
        case 0x01000037: return VK_F8;
        case 0x01000038: return VK_F9;
        case 0x01000039: return VK_F10;
        case 0x0100003A: return VK_F11;
        case 0x0100003B: return VK_F12;
        case 0x01000020: return VK_SPACE;
        case 0x01000004: return VK_RETURN;
        default: return 0;
    }
}
#endif

}  // namespace

GlobalHotkey* GlobalHotkey::Instance() {
    static GlobalHotkey* instance = new GlobalHotkey();
    return instance;
}

GlobalHotkey::GlobalHotkey() {
    if (auto* app = QCoreApplication::instance()) {
        app->installNativeEventFilter(this);
    }
}

GlobalHotkey::~GlobalHotkey() { UnregisterAll(); }

bool GlobalHotkey::Register(int id, const Modifiers& mods, int qt_key) {
#ifdef _WIN32
    if (registered_ids_.contains(id)) {
        ::UnregisterHotKey(nullptr, id);
        registered_ids_.remove(id);
    }
    const UINT vk = QtKeyToVk(qt_key);
    if (vk == 0) return false;
    if (!::RegisterHotKey(nullptr, id, ToWinModifiers(mods), vk)) return false;
    registered_ids_.insert(id, qt_key);
    return true;
#else
    Q_UNUSED(id);
    Q_UNUSED(mods);
    Q_UNUSED(qt_key);
    return false;
#endif
}

bool GlobalHotkey::Unregister(int id) {
#ifdef _WIN32
    if (!registered_ids_.contains(id)) return false;
    const BOOL ok = ::UnregisterHotKey(nullptr, id);
    registered_ids_.remove(id);
    return ok != 0;
#else
    Q_UNUSED(id);
    return false;
#endif
}

void GlobalHotkey::UnregisterAll() {
#ifdef _WIN32
    for (auto it = registered_ids_.constBegin();
         it != registered_ids_.constEnd(); ++it) {
        ::UnregisterHotKey(nullptr, it.key());
    }
#endif
    registered_ids_.clear();
}

bool GlobalHotkey::nativeEventFilter(const QByteArray& event_type,
                                     void* message, qintptr* result) {
    Q_UNUSED(result);
#ifdef _WIN32
    if (event_type != "windows_generic_MSG" &&
        event_type != "windows_dispatcher_MSG") {
        return false;
    }
    auto* msg = static_cast<MSG*>(message);
    if (msg->message != WM_HOTKEY) return false;
    const int id = static_cast<int>(msg->wParam);
    if (!registered_ids_.contains(id)) return false;
    emit HotkeyTriggered(id);
    return true;
#else
    Q_UNUSED(event_type);
    Q_UNUSED(message);
    return false;
#endif
}

}  // namespace passvault::ui
