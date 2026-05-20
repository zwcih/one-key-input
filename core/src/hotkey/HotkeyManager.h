#pragma once
#include <atomic>
#include <functional>
#include <string>

namespace onekey::hotkey {

// Press-to-talk hotkey via WH_KEYBOARD_LL.
// Must be installed and pumped on the main thread (GetMessage loop).
class HotkeyManager {
public:
    HotkeyManager();
    ~HotkeyManager();

    // key_name: "f9", "f12", "caps lock", etc. (case-insensitive)
    // min_hold_ms: callbacks ignored if release happens sooner than this.
    bool Install(const std::string& key_name, int min_hold_ms);
    void Uninstall();

    std::function<void()> on_press;
    std::function<void()> on_release;

    // Called from the low-level keyboard hook. Public only so the C-style
    // hook callback can dispatch into it; treat as internal.
    void OnHookEvent(unsigned long wParam, const void* kbll);

private:
    int vk_ = 0;
    int min_hold_ms_ = 250;
    bool pressed_ = false;
    unsigned long long press_tick_ = 0;
    void* hook_ = nullptr;  // HHOOK
};

}  // namespace onekey::hotkey
