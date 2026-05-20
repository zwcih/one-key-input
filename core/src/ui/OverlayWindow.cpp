#include "OverlayWindow.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace onekey::ui {

namespace {

constexpr wchar_t kClassName[] = L"OneKey.OverlayWindow";

constexpr UINT WM_OK_PHASE     = WM_APP + 10;
constexpr UINT WM_OK_HIDE      = WM_APP + 11;
constexpr UINT_PTR ANIM_TIMER  = 0xB1;

constexpr int kWidth     = 140;
constexpr int kHeight    = 32;
constexpr int kDotR      = 6;
constexpr int kPadX      = 12;
constexpr int kAnimMs    = 50; // ~20 fps; overlay is tiny so this is cheap

const wchar_t* LabelFor(session::Phase p) {
    using session::Phase;
    switch (p) {
        case Phase::Recording:   return L"Recording";
        case Phase::Recognizing: return L"Recognizing";
        case Phase::Polishing:   return L"Polishing";
        case Phase::Injecting:   return L"Injecting";
        case Phase::Done:        return L"Done";
        case Phase::Error:       return L"Error";
        default:                 return L"";
    }
}

COLORREF DotColorFor(session::Phase p) {
    using session::Phase;
    switch (p) {
        case Phase::Recording:   return RGB(220,  60,  60);  // red
        case Phase::Recognizing: return RGB(240, 180,  60);  // amber
        case Phase::Polishing:   return RGB( 80, 160, 240);  // blue
        case Phase::Injecting:   return RGB( 80, 200, 120);  // green
        case Phase::Done:        return RGB(120, 200, 120);
        case Phase::Error:       return RGB(220,  60,  60);
        default:                 return RGB(160, 160, 160);
    }
}

}  // namespace

OverlayWindow::OverlayWindow() = default;
OverlayWindow::~OverlayWindow() { Destroy(); }

bool OverlayWindow::Create() {
    HINSTANCE hinst = ::GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &OverlayWindow::WndProc;
    wc.hInstance     = hinst;
    wc.lpszClassName = kClassName;
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    ::RegisterClassExW(&wc);

    DWORD ex = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
               WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
    hwnd_ = ::CreateWindowExW(ex, kClassName, L"OK.Overlay",
                              WS_POPUP,
                              0, 0, kWidth, kHeight,
                              nullptr, nullptr, hinst, nullptr);
    if (!hwnd_) {
        spdlog::error("[overlay] CreateWindowEx failed err={}", ::GetLastError());
        return false;
    }
    ::SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    font_ = ::CreateFontW(16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                          L"Segoe UI");

    spdlog::info("[overlay] created");
    return true;
}

void OverlayWindow::Destroy() {
    if (bus_ && bus_token_) {
        bus_->Unsubscribe(bus_token_);
        bus_token_ = 0;
        bus_ = nullptr;
    }
    if (anim_timer_ && hwnd_) {
        ::KillTimer(hwnd_, ANIM_TIMER);
        anim_timer_ = 0;
    }
    if (font_) { ::DeleteObject(font_); font_ = nullptr; }
    if (hwnd_) { ::DestroyWindow(hwnd_); hwnd_ = nullptr; }
}

void OverlayWindow::Attach(session::EventBus* bus) {
    bus_ = bus;
    bus_token_ = bus->Subscribe([this](const session::PhaseEvent& e){ this->OnPhase(e); });
}

void OverlayWindow::OnPhase(const session::PhaseEvent& ev) {
    // Called from arbitrary threads. Marshal to the UI thread via window message.
    // We pass a heap copy of the event for the WM handler to free.
    auto* heap = new session::PhaseEvent(ev);
    if (!::PostMessageW(hwnd_, WM_OK_PHASE,
                        reinterpret_cast<WPARAM>(heap), 0)) {
        delete heap;
    }
}

POINT OverlayWindow::ComputeAnchor() {
    // Try to anchor below the focused window's caret first.
    HWND fg = ::GetForegroundWindow();
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    if (fg && ::GetGUIThreadInfo(::GetWindowThreadProcessId(fg, nullptr), &gti)
        && gti.hwndCaret) {
        POINT p { gti.rcCaret.left, gti.rcCaret.bottom };
        if (::ClientToScreen(gti.hwndCaret, &p)) {
            p.y += 6;  // small gap below the caret
            return p;
        }
    }
    // Fallback: just below the mouse cursor.
    POINT cur;
    if (::GetCursorPos(&cur)) {
        cur.y += 20;
        return cur;
    }
    return POINT{ 100, 100 };
}

void OverlayWindow::Show(session::Phase phase, const std::wstring& /*detail*/) {
    {
        std::lock_guard<std::mutex> lk(ui_mu_);
        phase_ = phase;
        label_ = LabelFor(phase);
        anim_tick_ = 0;
    }
    POINT a = ComputeAnchor();
    ::SetWindowPos(hwnd_, HWND_TOPMOST, a.x, a.y, kWidth, kHeight,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (!visible_) {
        visible_ = true;
        anim_timer_ = ::SetTimer(hwnd_, ANIM_TIMER, kAnimMs, nullptr);
    }
    Repaint();
}

void OverlayWindow::Hide() {
    if (!visible_) return;
    visible_ = false;
    if (anim_timer_) {
        ::KillTimer(hwnd_, ANIM_TIMER);
        anim_timer_ = 0;
    }
    ::ShowWindow(hwnd_, SW_HIDE);
}

void OverlayWindow::Repaint() {
    if (!hwnd_) return;

    HDC screen_dc = ::GetDC(nullptr);
    HDC mem_dc    = ::CreateCompatibleDC(screen_dc);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = kWidth;
    bi.bmiHeader.biHeight      = -kHeight;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = ::CreateDIBSection(screen_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old_bmp = ::SelectObject(mem_dc, dib);

    session::Phase phase;
    std::wstring   label;
    int            tick;
    {
        std::lock_guard<std::mutex> lk(ui_mu_);
        phase = phase_;
        label = label_;
        tick  = anim_tick_;
    }

    // Strategy: paint everything FULLY OPAQUE (alpha=255) and use a single
    // constant alpha for the whole window. This avoids GDI text routines
    // failing to write the alpha channel (which made text look fogged when
    // combined with per-pixel alpha + premultiplication).
    //
    // The animated dot pulse is achieved by RGB lerping toward background,
    // not by alpha changes.
    constexpr COLORREF bg_color     = RGB(28, 28, 32);
    constexpr COLORREF text_color   = RGB(235, 235, 240);
    constexpr BYTE     window_alpha = 230;  // overall translucency

    // Fill background (premultiplied = (R,G,B,255) since alpha=255).
    uint32_t* px = static_cast<uint32_t*>(bits);
    BYTE br = GetRValue(bg_color), bgg = GetGValue(bg_color), bb = GetBValue(bg_color);
    uint32_t bg = (uint32_t(0xFF) << 24) | (uint32_t(br) << 16) |
                  (uint32_t(bgg) << 8)   |  uint32_t(bb);
    for (int i = 0; i < kWidth * kHeight; ++i) px[i] = bg;

    // Pulsing dot — modulate dot RGB toward background instead of alpha,
    // so the whole bitmap stays alpha=255 and GDI text remains crisp.
    double t = tick * (kAnimMs / 1000.0);
    double pulse = 0.55 + 0.45 * std::sin(t * 4.5);
    if (phase == session::Phase::Recording) pulse = 0.55 + 0.45 * std::sin(t * 6.0);
    if (phase == session::Phase::Done) pulse = 1.0;
    pulse = std::clamp(pulse, 0.25, 1.0);

    COLORREF dotc = DotColorFor(phase);
    BYTE dr = GetRValue(dotc), dg = GetGValue(dotc), db = GetBValue(dotc);
    auto lerp = [](BYTE a, BYTE b, double k){
        return static_cast<BYTE>(b + (a - b) * k);
    };
    BYTE mr = lerp(dr, br,  pulse);
    BYTE mg = lerp(dg, bgg, pulse);
    BYTE mb = lerp(db, bb,  pulse);
    uint32_t dot_px = (uint32_t(0xFF) << 24) | (uint32_t(mr) << 16) |
                      (uint32_t(mg)   << 8)  |  uint32_t(mb);

    int cx = kPadX + kDotR;
    int cy = kHeight / 2;
    int rr = kDotR * kDotR;
    for (int y = cy - kDotR; y <= cy + kDotR; ++y) {
        if (y < 0 || y >= kHeight) continue;
        int dy = y - cy;
        for (int x = cx - kDotR; x <= cx + kDotR; ++x) {
            if (x < 0 || x >= kWidth) continue;
            int dx = x - cx;
            if (dx*dx + dy*dy <= rr) px[y * kWidth + x] = dot_px;
        }
    }

    // Text: GDI draws RGB only, but our background is fully opaque, so
    // ClearType blends correctly against the dark fill.
    ::SelectObject(mem_dc, font_);
    ::SetBkMode(mem_dc, TRANSPARENT);
    ::SetTextColor(mem_dc, text_color);
    RECT tr { kPadX + kDotR*2 + 8, 0, kWidth - 6, kHeight };
    ::DrawTextW(mem_dc, label.c_str(), -1, &tr,
                DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

    // Re-stamp alpha=255 on every pixel — GDI text writes 0 into the alpha
    // byte, which would create transparent holes under our premultiplied
    // BLENDFUNCTION. Force it back to 255 so the whole window is opaque,
    // then use SourceConstantAlpha for the overall translucency effect.
    for (int i = 0; i < kWidth * kHeight; ++i) {
        px[i] |= 0xFF000000u;
    }

    POINT src_pt { 0, 0 };
    SIZE  sz { kWidth, kHeight };
    BLENDFUNCTION bf{};
    bf.BlendOp             = AC_SRC_OVER;
    bf.SourceConstantAlpha = window_alpha;
    bf.AlphaFormat         = 0;  // no per-pixel alpha; use SourceConstantAlpha only
    ::UpdateLayeredWindow(hwnd_, screen_dc, nullptr, &sz, mem_dc, &src_pt,
                          0, &bf, ULW_ALPHA);

    ::SelectObject(mem_dc, old_bmp);
    ::DeleteObject(dib);
    ::DeleteDC(mem_dc);
    ::ReleaseDC(nullptr, screen_dc);
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<OverlayWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_OK_PHASE: {
            if (!self) return 0;
            auto* ev = reinterpret_cast<session::PhaseEvent*>(wParam);
            if (ev) {
                switch (ev->phase) {
                    case session::Phase::Idle:
                    case session::Phase::Done:
                        self->Hide();
                        break;
                    default:
                        self->Show(ev->phase, ev->detail);
                        break;
                }
                delete ev;
            }
            return 0;
        }
        case WM_TIMER:
            if (self && wParam == ANIM_TIMER && self->visible_) {
                std::lock_guard<std::mutex> lk(self->ui_mu_);
                self->anim_tick_++;
                // (Repaint outside the lock would be ideal, but cheap enough.)
                // Falls through to Repaint via the next line:
            }
            if (self && wParam == ANIM_TIMER && self->visible_) self->Repaint();
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace onekey::ui
