#pragma once
#include <atomic>
#include <functional>
#include <string>

namespace onekey::hotkey {

// Press-to-talk hotkey via WH_KEYBOARD_LL.
// Must be installed and pumped on the main thread (GetMessage loop).
//
// Up to two independent press-to-talk bindings are supported (e.g. F9 for
// the polish pipeline and F8 for the translation pipeline). The same
// low-level hook handles both — there is no per-key OS hook overhead.
class HotkeyManager {
public:
    HotkeyManager();
    ~HotkeyManager();

    // key_name: "f9", "f12", "caps lock", etc. (case-insensitive)
    // min_hold_ms: callbacks ignored if release happens sooner than this.
    bool Install(const std::string& key_name, int min_hold_ms);

    // Register a second key. Must be called *after* Install() succeeds.
    // Returns false if key_name is unknown or duplicates the primary key.
    // Uses on_press_secondary / on_release_secondary as callbacks.
    bool InstallSecondary(const std::string& key_name, int min_hold_ms);

    void Uninstall();

    std::function<void()> on_press;
    std::function<void()> on_release;
    std::function<void()> on_press_secondary;
    std::function<void()> on_release_secondary;

    // Esc was pressed anywhere on the system. Fired so the session can
    // force-stop a sticky / toggle-mode recording. We do NOT swallow Esc
    // here — other apps still see it normally.
    std::function<void()> on_escape;

    // Called from the low-level keyboard hook. Public only so the C-style
    // hook callback can dispatch into it; treat as internal.
    void OnHookEvent(unsigned long wParam, const void* kbll);

private:
    int vk_ = 0;
    int min_hold_ms_ = 250;
    bool pressed_ = false;
    unsigned long long press_tick_ = 0;

    int vk2_ = 0;
    int min_hold_ms2_ = 250;
    bool pressed2_ = false;
    unsigned long long press_tick2_ = 0;

    void* hook_ = nullptr;  // HHOOK
};

}  // namespace onekey::hotkey
