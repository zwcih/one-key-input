#include "TrayIcon.h"
#include "IconFactory.h"
#include "../../resources/resource.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <shellapi.h>

namespace onekey::ui {

namespace {

constexpr UINT WM_OK_TRAY    = WM_APP + 1;
constexpr UINT WM_OK_RETIP   = WM_APP + 2;
constexpr UINT WM_OK_REDRAW  = WM_APP + 3;
constexpr UINT WM_OK_TOAST   = WM_APP + 4;
constexpr UINT TRAY_UID      = 0xA13C;

// Menu command IDs
constexpr UINT IDM_OPEN_SETTINGS = 1001;
constexpr UINT IDM_OPEN_LOGS    = 1002;
constexpr UINT IDM_PAUSE        = 1003;
constexpr UINT IDM_POLISH_RAW   = 1010;
constexpr UINT IDM_POLISH_TIDY  = 1011;
constexpr UINT IDM_POLISH_FORMAL= 1012;
constexpr UINT IDM_QUIT         = 1999;

constexpr wchar_t kClassName[] = L"OneKey.TrayWindow";

}  // namespace

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() { Destroy(); }

bool TrayIcon::Create() {
    HINSTANCE hinst = ::GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &TrayIcon::WndProc;
    wc.hInstance     = hinst;
    wc.lpszClassName = kClassName;
    ::RegisterClassExW(&wc); // ignore duplicate

    hwnd_ = ::CreateWindowExW(0, kClassName, L"OneKey",
                              0, 0, 0, 0, 0,
                              HWND_MESSAGE, nullptr, hinst, nullptr);
    if (!hwnd_) {
        spdlog::error("[tray] CreateWindowEx failed err={}", ::GetLastError());
        return false;
    }
    ::SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    wm_taskbar_created_ = ::RegisterWindowMessageW(L"TaskbarCreated");

    // Tray icon size depends on DPI; query the system small-icon size and
    // render a fresh tinted mic at that resolution. The factory icon is
    // owned by us (no LR_SHARED) — Destroy() must call DestroyIcon.
    int small_w = ::GetSystemMetrics(SM_CXSMICON);
    int small_h = ::GetSystemMetrics(SM_CYSMICON);
    int sz = std::max({16, small_w, small_h});
    hicon_ = CreateMicIcon(phase_, paused_, sz);
    hicon_owned_ = (hicon_ != nullptr);
    if (!hicon_) {
        // Fall back to the embedded resource so the tray entry still appears.
        hicon_ = static_cast<HICON>(::LoadImageW(hinst,
                                                 MAKEINTRESOURCEW(IDI_ONEKEY_TRAY),
                                                 IMAGE_ICON, sz, sz,
                                                 LR_DEFAULTCOLOR | LR_SHARED));
        if (!hicon_) hicon_ = ::LoadIconW(nullptr, IDI_APPLICATION);
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd_;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_OK_TRAY;
    nid.hIcon            = hicon_;
    wcsncpy_s(nid.szTip, L"One-Key Input — idle", _TRUNCATE);

    if (!::Shell_NotifyIconW(NIM_ADD, &nid)) {
        spdlog::error("[tray] Shell_NotifyIcon NIM_ADD failed");
        return false;
    }
    added_ = true;
    tooltip_ = L"One-Key Input — idle";
    spdlog::info("[tray] icon installed");
    return true;
}

void TrayIcon::Destroy() {
    if (added_ && hwnd_) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd   = hwnd_;
        nid.uID    = TRAY_UID;
        ::Shell_NotifyIconW(NIM_DELETE, &nid);
        added_ = false;
    }
    // Free the icon only when we allocated it ourselves (CreateMicIcon).
    // LR_SHARED + LoadIcon(IDI_APPLICATION) hand back system-owned handles
    // that DestroyIcon would log an error on.
    if (hicon_ && hicon_owned_) {
        ::DestroyIcon(hicon_);
    }
    hicon_ = nullptr;
    hicon_owned_ = false;
    if (hwnd_) {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void TrayIcon::SetState(session::Phase phase, bool paused) {
    // Cheap field update; actual icon rebuild happens on the UI thread.
    phase_  = phase;
    paused_ = paused;
    if (hwnd_) ::PostMessageW(hwnd_, WM_OK_REDRAW, 0, 0);
}

void TrayIcon::SetTooltip(const std::wstring& text) {
    if (!hwnd_) return;
    tooltip_ = text;
    // Cross-thread safe: post to the tray's own thread, the WndProc updates.
    ::PostMessageW(hwnd_, WM_OK_RETIP, 0, 0);
}

void TrayIcon::ShowToast(const std::wstring& title, const std::wstring& body) {
    if (!hwnd_) return;
    // Assignments race with EmitToast on the UI thread, but realistic call
    // rate is one-per-error so we don't bother with a mutex. The worst case
    // is the user sees a slightly-stale title/body for one toast.
    toast_title_ = title;
    toast_body_  = body;
    ::PostMessageW(hwnd_, WM_OK_TOAST, 0, 0);
}

void TrayIcon::UpdateIcon() {
    if (!added_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd_;
    nid.uID    = TRAY_UID;
    nid.uFlags = NIF_TIP;
    wcsncpy_s(nid.szTip, tooltip_.c_str(), _TRUNCATE);
    ::Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::Redraw() {
    if (!added_) return;
    // Build a fresh tinted icon for the current phase + pause state and
    // swap it into the tray entry. We always allocate a new handle so the
    // outgoing one stays valid for any in-flight shell paint.
    int small_w = ::GetSystemMetrics(SM_CXSMICON);
    int small_h = ::GetSystemMetrics(SM_CYSMICON);
    int sz = std::max({16, small_w, small_h});
    HICON next = CreateMicIcon(phase_, paused_, sz);
    if (!next) return;  // keep the old icon rather than show a blank entry

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd_;
    nid.uID    = TRAY_UID;
    nid.uFlags = NIF_ICON;
    nid.hIcon  = next;
    ::Shell_NotifyIconW(NIM_MODIFY, &nid);

    if (hicon_ && hicon_owned_) {
        ::DestroyIcon(hicon_);
    }
    hicon_ = next;
    hicon_owned_ = true;
}

void TrayIcon::EmitToast() {
    if (!added_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd_;
    nid.uID    = TRAY_UID;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
    // szInfoTitle is 64 wchars max, szInfo is 256 — truncate copy.
    wcsncpy_s(nid.szInfoTitle, toast_title_.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo,      toast_body_.c_str(),  _TRUNCATE);
    if (!::Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        spdlog::warn("[tray] toast show failed err={}", ::GetLastError());
    }
}

LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<TrayIcon*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (self && msg == self->wm_taskbar_created_) {
        // Explorer restarted — re-add the icon.
        self->added_ = false;
        self->Create();
        return 0;
    }

    switch (msg) {
        case WM_OK_TRAY:
            if (self) self->OnTrayMessage(wParam, lParam);
            return 0;
        case WM_OK_RETIP:
            if (self) self->UpdateIcon();
            return 0;
        case WM_OK_REDRAW:
            if (self) self->Redraw();
            return 0;
        case WM_OK_TOAST:
            if (self) self->EmitToast();
            return 0;
        case WM_COMMAND:
            if (self) {
                switch (LOWORD(wParam)) {
                    case IDM_OPEN_SETTINGS: if (self->on_open_settings) self->on_open_settings(); break;
                    case IDM_OPEN_LOGS:   if (self->on_open_logs)    self->on_open_logs();    break;
                    case IDM_PAUSE:       if (self->on_toggle_pause) self->on_toggle_pause(); break;
                    case IDM_POLISH_RAW:    if (self->on_set_polish_mode) self->on_set_polish_mode("raw");    break;
                    case IDM_POLISH_TIDY:   if (self->on_set_polish_mode) self->on_set_polish_mode("tidy");   break;
                    case IDM_POLISH_FORMAL: if (self->on_set_polish_mode) self->on_set_polish_mode("formal"); break;
                    case IDM_QUIT:        if (self->on_quit)         self->on_quit();         break;
                }
            }
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void TrayIcon::OnTrayMessage(WPARAM /*wParam*/, LPARAM lParam) {
    switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowContextMenu();
            break;
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            if (on_open_settings) on_open_settings();
            break;
        case NIN_BALLOONUSERCLICK:
            // User clicked the toast — take them to where they can fix it.
            if (on_open_settings) on_open_settings();
            break;
    }
}

void TrayIcon::ShowContextMenu() {
    POINT pt;
    ::GetCursorPos(&pt);

    HMENU menu = ::CreatePopupMenu();
    if (!menu) return;

    // Header (disabled, just informational). tooltip_ already contains
    // "One-Key Input — <phase>", so use it directly.
    ::AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, tooltip_.c_str());
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    bool paused = (get_paused && get_paused());
    UINT pause_flags = MF_STRING | (paused ? MF_CHECKED : MF_UNCHECKED);
    ::AppendMenuW(menu, pause_flags, IDM_PAUSE,
                  paused ? L"Resume hotkey" : L"Pause hotkey");

    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Polish-mode submenu — radio-style, current selection checked.
    if (on_set_polish_mode) {
        HMENU polish_menu = ::CreatePopupMenu();
        std::string current = get_polish_mode ? get_polish_mode() : "tidy";
        auto add_item = [&](UINT id, const wchar_t* label, const char* mode_id) {
            UINT flags = MF_STRING;
            if (current == mode_id) flags |= MF_CHECKED;
            ::AppendMenuW(polish_menu, flags, id, label);
        };
        add_item(IDM_POLISH_RAW,    L"Raw (no polish)",         "raw");
        add_item(IDM_POLISH_TIDY,   L"Tidy (default)",          "tidy");
        add_item(IDM_POLISH_FORMAL, L"Formal (rewrite)",        "formal");
        ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(polish_menu),
                      L"Polish mode");
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }

    ::AppendMenuW(menu, MF_STRING, IDM_OPEN_SETTINGS, L"Settings...");
    ::AppendMenuW(menu, MF_STRING, IDM_OPEN_LOGS,   L"Open Log Folder");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, IDM_QUIT, L"Quit");

    // Required so the menu dismisses correctly when user clicks elsewhere.
    ::SetForegroundWindow(hwnd_);
    ::TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN,
                     pt.x, pt.y, 0, hwnd_, nullptr);
    ::DestroyMenu(menu);
}

}  // namespace onekey::ui
