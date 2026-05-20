#pragma once
#include "../session/EventBus.h"

#include <functional>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace onekey::ui {

// Lightweight system tray icon. Owns a hidden message window so it can
// receive Shell_NotifyIcon callbacks. Must be created on the same thread
// that runs the message loop (the main thread).
//
// Menu actions are fixed for Slice 4-MVP — when we add real settings we'll
// either grow this or replace with a WebView2 settings window.
class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    // Returns false on hard failure.
    bool Create();
    void Destroy();

    // Tooltip text shown on hover. Safe to call from any thread (just posts).
    void SetTooltip(const std::wstring& text);

    // Show a balloon / toast next to the tray icon. Clicking the toast (or
    // the icon while the toast is up) fires on_open_settings. Safe to call
    // from any thread.
    void ShowToast(const std::wstring& title, const std::wstring& body);

    // Update icon graphic to reflect session phase + pause state.
    // Safe to call from any thread.
    void SetState(session::Phase phase, bool paused);

    // Wired by Application:
    std::function<void()> on_quit;
    std::function<void()> on_open_settings;
    std::function<void()> on_open_logs;
    std::function<void()> on_toggle_pause;     // pause/resume hotkey
    std::function<bool()> get_paused;          // ticks the menu item

    // Polish-mode submenu wiring.
    std::function<std::string()>               get_polish_mode; // returns "raw" | "tidy" | "formal"
    std::function<void(const std::string&)>    on_set_polish_mode;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void OnTrayMessage(WPARAM wParam, LPARAM lParam);
    void ShowContextMenu();
    void UpdateIcon();
    void Redraw();
    void EmitToast();   // marshaled to UI thread via WM_OK_TOAST

    HWND   hwnd_     = nullptr;
    HICON  hicon_    = nullptr;
    UINT   wm_taskbar_created_ = 0; // for explorer crash recovery
    bool   added_    = false;
    std::wstring     tooltip_;
    std::wstring     toast_title_;
    std::wstring     toast_body_;
    session::Phase   phase_  = session::Phase::Idle;
    bool             paused_ = false;
};

}  // namespace onekey::ui
