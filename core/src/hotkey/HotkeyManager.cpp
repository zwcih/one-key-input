#include "HotkeyManager.h"

#include <spdlog/spdlog.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace onekey::hotkey {

namespace {

HotkeyManager* g_instance = nullptr;

int ParseKey(const std::string& s) {
    std::string k = s;
    std::transform(k.begin(), k.end(), k.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    static const std::unordered_map<std::string, int> map = {
        {"f1", VK_F1},  {"f2", VK_F2},  {"f3", VK_F3},  {"f4", VK_F4},
        {"f5", VK_F5},  {"f6", VK_F6},  {"f7", VK_F7},  {"f8", VK_F8},
        {"f9", VK_F9},  {"f10", VK_F10}, {"f11", VK_F11}, {"f12", VK_F12},
        {"caps lock", VK_CAPITAL}, {"capslock", VK_CAPITAL},
        {"right ctrl", VK_RCONTROL}, {"right shift", VK_RSHIFT},
        {"right alt", VK_RMENU},
        {"scroll lock", VK_SCROLL},
        {"pause", VK_PAUSE},
    };
    auto it = map.find(k);
    if (it != map.end()) return it->second;
    if (k.size() == 1) {
        char c = k[0];
        if (c >= 'a' && c <= 'z') return 'A' + (c - 'a');
        if (c >= '0' && c <= '9') return c;
    }
    return 0;
}

}  // namespace

HotkeyManager::HotkeyManager() = default;
HotkeyManager::~HotkeyManager() { Uninstall(); }

namespace {

LRESULT CALLBACK LowLevelHook(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_instance) {
        g_instance->OnHookEvent(static_cast<unsigned long>(wParam),
                                reinterpret_cast<const void*>(lParam));
    }
    return ::CallNextHookEx(nullptr, nCode, wParam, lParam);
}

}  // namespace

bool HotkeyManager::Install(const std::string& key_name, int min_hold_ms) {
    vk_ = ParseKey(key_name);
    if (vk_ == 0) {
        spdlog::error("[hotkey] unknown key: {}", key_name);
        return false;
    }
    min_hold_ms_ = min_hold_ms;
    g_instance = this;
    hook_ = ::SetWindowsHookExW(WH_KEYBOARD_LL, &LowLevelHook,
                                ::GetModuleHandleW(nullptr), 0);
    if (!hook_) {
        spdlog::error("[hotkey] SetWindowsHookExW failed err={}", ::GetLastError());
        g_instance = nullptr;
        return false;
    }
    spdlog::info("[hotkey] installed: key='{}' (vk=0x{:X}) min_hold={}ms",
                 key_name, vk_, min_hold_ms_);
    return true;
}

bool HotkeyManager::InstallSecondary(const std::string& key_name, int min_hold_ms) {
    int vk = ParseKey(key_name);
    if (vk == 0) {
        spdlog::error("[hotkey] secondary: unknown key: {}", key_name);
        return false;
    }
    if (vk == vk_) {
        spdlog::error("[hotkey] secondary key '{}' duplicates primary; ignoring",
                      key_name);
        return false;
    }
    if (!hook_) {
        spdlog::error("[hotkey] secondary requested before primary Install()");
        return false;
    }
    vk2_ = vk;
    min_hold_ms2_ = min_hold_ms;
    pressed2_ = false;
    press_tick2_ = 0;
    spdlog::info("[hotkey] secondary installed: key='{}' (vk=0x{:X}) min_hold={}ms",
                 key_name, vk2_, min_hold_ms2_);
    return true;
}

void HotkeyManager::Uninstall() {
    if (hook_) {
        ::UnhookWindowsHookEx(static_cast<HHOOK>(hook_));
        hook_ = nullptr;
    }
    if (g_instance == this) g_instance = nullptr;
}

void HotkeyManager::OnHookEvent(unsigned long wParam, const void* kbll) {
    const KBDLLHOOKSTRUCT* kb = static_cast<const KBDLLHOOKSTRUCT*>(kbll);
    bool is_down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool is_up   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);

    auto handle = [&](int vk, int min_hold, bool& pressed,
                      unsigned long long& press_tick,
                      const std::function<void()>& on_p,
                      const std::function<void()>& on_r,
                      const char* tag) {
        if (vk == 0) return;
        if (kb->vkCode != static_cast<DWORD>(vk)) return;
        if (is_down && !pressed) {
            pressed = true;
            press_tick = ::GetTickCount64();
            if (on_p) on_p();
        } else if (is_up && pressed) {
            pressed = false;
            unsigned long long held = ::GetTickCount64() - press_tick;
            if (static_cast<int>(held) < min_hold) {
                spdlog::info("[hotkey:{}] short press {}ms (< {}ms)",
                             tag, held, min_hold);
            }
            if (on_r) on_r();
        }
    };

    handle(vk_,  min_hold_ms_,  pressed_,  press_tick_,
           on_press,           on_release,           "primary");
    handle(vk2_, min_hold_ms2_, pressed2_, press_tick2_,
           on_press_secondary, on_release_secondary, "secondary");

    // Esc: fire on key-down so a sticky/toggle recording stops as soon as
    // the user starts pressing Esc rather than after release. We do not
    // mark the event handled — the LowLevelHook returns CallNextHookEx so
    // other apps still see Esc normally.
    if (is_down && kb->vkCode == VK_ESCAPE && on_escape) {
        on_escape();
    }
}

}  // namespace onekey::hotkey
