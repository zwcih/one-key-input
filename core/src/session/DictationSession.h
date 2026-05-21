#pragma once
#include "../asr/IAsrEngine.h"
#include "../polish/IPolisher.h"
#include "../inject/InjectorStrategy.h"
#include "../config/Config.h"
#include "../focus/FocusContext.h"
#include "EventBus.h"

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <chrono>

namespace onekey::audio { class WasapiCapture; }

namespace onekey::session {

// Owns the full record→recognize→polish→inject lifecycle.
// Press hotkey -> StartRecording(); Release -> StopAndProcess().
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

    enum class Mode {
        Polish,     // F9 — run polish.mode through OpenAIPolisher
        Translate,  // F8 — run translation prompt with translate.target_language
    };

    void StartRecording(Mode mode = Mode::Polish);
    void StopAndProcess();

    Phase CurrentPhase() const { return phase_.load(); }

private:
    void DoRecognizeAndPolish();
    void SetPhase(Phase p, std::wstring detail = {});

    const config::AppConfig& cfg_;
    asr::IAsrEngine*           asr_;
    polish::IPolisher*         polisher_;
    inject::InjectorStrategy*  injector_;
    audio::WasapiCapture*      capture_;
    EventBus*                  bus_;

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
};

}  // namespace onekey::session
