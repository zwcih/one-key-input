#pragma once
#include "../asr/IAsrEngine.h"
#include "../polish/IPolisher.h"
#include "../inject/InjectorStrategy.h"
#include "../config/Config.h"
#include "../focus/FocusContext.h"
#include "EventBus.h"
#include "HotkeyBehaviorMachine.h"

#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>

namespace onekey::audio { class WasapiCapture; }

namespace onekey::session {

// Owns the full record→recognize→polish→inject lifecycle.
//
// Two ways to drive it:
//   - High-level press/release/escape dispatch via the embedded
//     HotkeyBehaviorMachine (push_to_talk / toggle / smart). This is the
//     path the Application wires the OS hotkey hook through.
//   - Low-level StartRecording/StopAndProcess for tests and for any caller
//     that doesn't want behavior interpretation.
//
// All public methods are safe to call from the hotkey thread.
//
// Phase changes are broadcast through the EventBus passed in; UI components
// (tray, overlay) subscribe to drive their visuals without coupling to ASR
// or polish details.
class DictationSession {
public:
    DictationSession(const config::AppConfig& cfg,
                     asr::IAsrEngine* asr,
                     polish::IPolisher* polisher,
                     inject::InjectorStrategy* injector,
                     audio::WasapiCapture* capture,
                     EventBus* bus);
    ~DictationSession();

    enum class Mode {
        Polish,     // F9 — run polish.mode through OpenAIPolisher
        Translate,  // F8 — run translation prompt with translate.target_language
    };

    // Behavior-aware hotkey dispatch. The session interprets the configured
    // hotkey.behavior (push_to_talk / toggle / smart) and decides whether
    // to start/stop/promote-to-sticky. Both F9 and F8 share the same
    // behavior config — the only difference is `mode`.
    void OnHotkeyPress(Mode mode = Mode::Polish);
    void OnHotkeyRelease();

    // Force-stop a sticky / toggle-mode recording. No-op when idle.
    void OnEscape();

    // Lower-level entry points. Used by callers (e.g. tests) that don't
    // want behavior interpretation.
    void StartRecording(Mode mode = Mode::Polish);
    void StopAndProcess();

    Phase CurrentPhase() const { return phase_.load(); }

private:
    void DoRecognizeAndPolish();
    void SetPhase(Phase p, std::wstring detail = {});
    void StartWatchdog();
    void StopWatchdog();

    const config::AppConfig& cfg_;
    asr::IAsrEngine*           asr_;
    polish::IPolisher*         polisher_;
    inject::InjectorStrategy*  injector_;
    audio::WasapiCapture*      capture_;
    EventBus*                  bus_;

    HotkeyBehaviorMachine      behavior_machine_;
    std::mutex                 machine_mu_;  // serializes hotkey dispatch

    std::atomic<Phase>          phase_{Phase::Idle};
    std::mutex                  mu_;
    std::wstring                final_text_;
    std::wstring                inject_buf_;

    std::chrono::steady_clock::time_point t_press_;
    std::chrono::steady_clock::time_point t_release_;

    // The mode chosen at StartRecording() time and consumed at polish time.
    // Defaults to Polish so the legacy single-hotkey flow is unchanged.
    Mode mode_ = Mode::Polish;

    // Captured at StartRecording() and consumed at SessionFinal. Held while
    // the user is dictating so the UIA walk overlaps the recording window
    // — by the time we need it, the future is typically already ready.
    std::future<focus::ContextSnapshot> focus_future_;

    // Max-duration watchdog. Only spun up while a sticky/toggle recording is
    // live so it doesn't sit on a thread idle when push-to-talk is in use.
    std::thread             watchdog_;
    std::condition_variable watchdog_cv_;
    std::mutex              watchdog_mu_;
    bool                    watchdog_stop_ = false;
};

}  // namespace onekey::session

