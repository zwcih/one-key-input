#pragma once
#include <functional>
#include <string>
#include <vector>
#include <mutex>

namespace onekey::session {

// Phases of a single dictation. Subscribers can use these to drive UI
// (tray icon tooltip, cursor overlay animation, etc.) without knowing
// anything about ASR / polish / inject internals.
enum class Phase {
    Idle,             // nothing happening
    Recording,        // mic is open, user holding hotkey
    StickyRecording,  // mic is open in toggle/smart-sticky mode (no key held)
    Recognizing,      // hotkey released; ASR finalizing
    Polishing,        // ASR done; LLM streaming
    Injecting,        // LLM tokens arriving and being typed
    Done,             // injection complete; about to return to Idle
    Error             // something failed; will return to Idle
};

struct PhaseEvent {
    Phase       phase;
    std::wstring detail;   // optional human-readable detail (e.g. partial recognition text)
};

// Thread-safe pub/sub. Used by DictationSession to broadcast Phase changes
// to UI subscribers (tray, overlay). Callbacks run on whatever thread fires
// the event — subscribers must marshal to their own thread if they touch UI.
class EventBus {
public:
    using Callback = std::function<void(const PhaseEvent&)>;

    // Returns a token; pass to Unsubscribe to remove.
    int Subscribe(Callback cb);
    void Unsubscribe(int token);

    void Publish(const PhaseEvent& ev);

private:
    std::mutex                              mu_;
    std::vector<std::pair<int, Callback>>   subs_;
    int                                     next_token_ = 1;
};

const char* PhaseName(Phase p);

}  // namespace onekey::session
