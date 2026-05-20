#include "IconFactory.h"

#include <algorithm>
#include <vector>
#include <cmath>

namespace onekey::ui {

namespace {

struct Rgba { uint8_t b, g, r, a; };  // BGRA order for Windows DIB

constexpr Rgba kBgTransparent{0, 0, 0, 0};
constexpr Rgba kWhite        {245, 245, 245, 255};
constexpr Rgba kBlack        {18, 18, 24, 255};

Rgba TintFor(session::Phase phase) {
    using session::Phase;
    switch (phase) {
        case Phase::Recording:   return Rgba{ 60,  60, 220, 255}; // red (BGR)
        case Phase::Recognizing: return Rgba{ 60, 180, 240, 255}; // amber
        case Phase::Polishing:   return Rgba{240, 160,  80, 255}; // blue
        case Phase::Injecting:   return Rgba{120, 200,  80, 255}; // green
        case Phase::Error:       return Rgba{ 60,  60, 220, 255}; // red
        case Phase::Done:
        case Phase::Idle:
        default:                 return Rgba{180, 180, 180, 255}; // grey
    }
}

inline void Put(std::vector<Rgba>& px, int W, int H, int x, int y, Rgba c) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    px[y * W + x] = c;
}

inline void Blend(std::vector<Rgba>& px, int W, int H, int x, int y, Rgba c, double a) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    a = std::clamp(a, 0.0, 1.0);
    if (a <= 0) return;
    Rgba& d = px[y * W + x];
    auto mix = [&](uint8_t s, uint8_t dst){
        return uint8_t(s * a + dst * (1.0 - a));
    };
    if (d.a == 0) { d = {c.b, c.g, c.r, uint8_t(255 * a)}; return; }
    d.b = mix(c.b, d.b);
    d.g = mix(c.g, d.g);
    d.r = mix(c.r, d.r);
    d.a = std::max<uint8_t>(d.a, uint8_t(255 * a));
}

void FillRoundRect(std::vector<Rgba>& px, int W, int H,
                   int x0, int y0, int x1, int y1, int r, Rgba c) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            // Corner test
            int dx = 0, dy = 0;
            if (x < x0 + r) dx = (x0 + r) - x;
            else if (x > x1 - r) dx = x - (x1 - r);
            if (y < y0 + r) dy = (y0 + r) - y;
            else if (y > y1 - r) dy = y - (y1 - r);
            double d = std::sqrt(double(dx*dx + dy*dy));
            if (d <= r - 0.5) {
                Put(px, W, H, x, y, c);
            } else if (d < r + 0.5) {
                Blend(px, W, H, x, y, c, (r + 0.5 - d));
            }
        }
    }
}

void FillCircle(std::vector<Rgba>& px, int W, int H,
                double cx, double cy, double r, Rgba c) {
    int x0 = int(std::floor(cx - r - 1));
    int x1 = int(std::ceil (cx + r + 1));
    int y0 = int(std::floor(cy - r - 1));
    int y1 = int(std::ceil (cy + r + 1));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            double dx = (x + 0.5) - cx;
            double dy = (y + 0.5) - cy;
            double d  = std::sqrt(dx*dx + dy*dy);
            if (d <= r - 0.5)      Put(px, W, H, x, y, c);
            else if (d < r + 0.5)  Blend(px, W, H, x, y, c, (r + 0.5 - d));
        }
    }
}

void StrokeLine(std::vector<Rgba>& px, int W, int H,
                double x0, double y0, double x1, double y1, double thick, Rgba c) {
    int xa = int(std::floor(std::min(x0, x1) - thick - 1));
    int xb = int(std::ceil (std::max(x0, x1) + thick + 1));
    int ya = int(std::floor(std::min(y0, y1) - thick - 1));
    int yb = int(std::ceil (std::max(y0, y1) + thick + 1));
    double dx = x1 - x0, dy = y1 - y0;
    double len2 = dx*dx + dy*dy;
    if (len2 < 1e-6) return;
    for (int y = ya; y <= yb; ++y) {
        for (int x = xa; x <= xb; ++x) {
            double t = ((x + 0.5 - x0) * dx + (y + 0.5 - y0) * dy) / len2;
            t = std::clamp(t, 0.0, 1.0);
            double px_x = x0 + t * dx;
            double px_y = y0 + t * dy;
            double d = std::sqrt((x + 0.5 - px_x) * (x + 0.5 - px_x) +
                                 (y + 0.5 - px_y) * (y + 0.5 - px_y));
            if (d <= thick - 0.5)      Put(px, W, H, x, y, c);
            else if (d < thick + 0.5)  Blend(px, W, H, x, y, c, (thick + 0.5 - d));
        }
    }
}

// Draw a microphone (capsule body + stand) into the given pixel grid.
void DrawMic(std::vector<Rgba>& px, int W, int H, Rgba body_color) {
    // Mic body: rounded rect centered horizontally, upper 60% of icon.
    int body_w   = W * 9 / 20;          // ~14 of 32
    int body_h   = H * 11 / 20;         // ~18 of 32
    int body_x0  = (W - body_w) / 2;
    int body_y0  = H * 2 / 20;
    int body_x1  = body_x0 + body_w - 1;
    int body_y1  = body_y0 + body_h - 1;
    int body_r   = body_w / 2;

    FillRoundRect(px, W, H, body_x0, body_y0, body_x1, body_y1, body_r, body_color);

    // U-shaped stand arc under the body (approximate with thick semi-circle outline).
    double cx     = W / 2.0;
    double cy     = body_y1 - body_r + 0.5;
    double arc_r  = body_w * 0.85;
    double thick  = std::max(1.5, W / 18.0);
    // Draw arc by stamping short line segments around bottom half.
    int steps = 48;
    for (int i = 0; i < steps; ++i) {
        double a0 = 3.14159 * (1.0 + double(i)     / steps);
        double a1 = 3.14159 * (1.0 + double(i + 1) / steps);
        double x0 = cx + arc_r * std::cos(a0);
        double y0 = cy + arc_r * std::sin(a0);
        double x1 = cx + arc_r * std::cos(a1);
        double y1 = cy + arc_r * std::sin(a1);
        StrokeLine(px, W, H, x0, y0, x1, y1, thick * 0.5, body_color);
    }

    // Stem from body bottom down to base.
    double stem_x0 = cx;
    double stem_y0 = cy + arc_r;
    double stem_y1 = H - H / 10.0;
    StrokeLine(px, W, H, stem_x0, stem_y0, stem_x0, stem_y1, thick * 0.55, body_color);

    // Base bar at bottom.
    double base_w  = W * 0.40;
    StrokeLine(px, W, H, cx - base_w/2, stem_y1, cx + base_w/2, stem_y1,
               thick * 0.55, body_color);
}

void OverlayPausedSlash(std::vector<Rgba>& px, int W, int H) {
    // Diagonal line top-right to bottom-left, dark stroke with white halo.
    double thick = std::max(1.5, W / 14.0);
    StrokeLine(px, W, H, W - 2, 2, 2, H - 2, thick * 0.9, kWhite);
    StrokeLine(px, W, H, W - 2, 2, 2, H - 2, thick * 0.45, kBlack);
}

void OverlayErrorX(std::vector<Rgba>& px, int W, int H) {
    double thick = std::max(1.5, W / 14.0);
    StrokeLine(px, W, H, 2, 2, W - 2, H - 2, thick * 0.9, kWhite);
    StrokeLine(px, W, H, W - 2, 2, 2, H - 2, thick * 0.9, kWhite);
}

HICON BuildHIcon(const std::vector<Rgba>& px, int W, int H) {
    // Color DIB
    BITMAPV5HEADER bi{};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = W;
    bi.bV5Height      = -H;        // top-down
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    HDC hdc = ::GetDC(nullptr);
    void* color_bits = nullptr;
    HBITMAP color_bmp = ::CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi),
                                           DIB_RGB_COLORS, &color_bits, nullptr, 0);
    ::ReleaseDC(nullptr, hdc);
    if (!color_bmp) return nullptr;
    std::memcpy(color_bits, px.data(), W * H * 4);

    // Mask bitmap (1bpp). Pixels with alpha==0 → mask=1 (transparent).
    HBITMAP mask_bmp = ::CreateBitmap(W, H, 1, 1, nullptr);

    ICONINFO ii{};
    ii.fIcon    = TRUE;
    ii.hbmColor = color_bmp;
    ii.hbmMask  = mask_bmp;
    HICON icon  = ::CreateIconIndirect(&ii);

    ::DeleteObject(color_bmp);
    ::DeleteObject(mask_bmp);
    return icon;
}

}  // namespace

HICON CreateMicIcon(session::Phase phase, bool paused, int size_px) {
    const int W = size_px;
    const int H = size_px;
    std::vector<Rgba> px(W * H, kBgTransparent);

    Rgba body = paused ? Rgba{160, 160, 160, 255} : TintFor(phase);
    DrawMic(px, W, H, body);

    if (phase == session::Phase::Error)  OverlayErrorX(px, W, H);
    if (paused)                          OverlayPausedSlash(px, W, H);

    return BuildHIcon(px, W, H);
}

}  // namespace onekey::ui
