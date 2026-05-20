#pragma once
#include "../session/EventBus.h"

#include <atomic>
#include <mutex>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace onekey::ui {

// Small status badge that appears near the user's caret/cursor while a
// dictation is in progress. Shows:
//   "● Recording"   (breathing red dot while mic open)
//   "● Recognizing"
//   "● Polishing"
//   "● Injecting"
//
// Implemented as a layered, top-most, non-activating tool window so it
// can't steal focus from the target editor. Visibility is driven by Phase
// events from the EventBus.
//
// Must be created on the same thread as the main message loop.
class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();

    bool Create();
    void Destroy();

    // Subscribe to phase events on the bus. Wires up everything needed.
    void Attach(session::EventBus* bus);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void OnPhase(const session::PhaseEvent& ev);
    void Show(session::Phase phase, const std::wstring& detail);
    void Hide();
    void Repaint();
    POINT ComputeAnchor();   // caret in screen coords, falls back to cursor

    HWND        hwnd_      = nullptr;
    HFONT       font_      = nullptr;
    UINT_PTR    anim_timer_ = 0;

    std::mutex          ui_mu_;
    session::Phase      phase_   = session::Phase::Idle;
    std::wstring        label_;
    int                 anim_tick_ = 0;   // for breathing
    bool                visible_   = false;

    session::EventBus*  bus_       = nullptr;
    int                 bus_token_ = 0;
};

}  // namespace onekey::ui
