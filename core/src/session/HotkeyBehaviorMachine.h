#pragma once
#include <chrono>
#include <string>

namespace onekey::session {

// Behavior the user selected in Settings.
enum class HotkeyBehavior {
    PushToTalk,  // hold-to-record (legacy default)
    Toggle,      // tap to start, tap to stop
    Smart,       // short press => toggle, long press => push-to-talk
};

inline HotkeyBehavior ParseBehavior(const std::string& s) {
    if (s == "toggle") return HotkeyBehavior::Toggle;
    if (s == "smart")  return HotkeyBehavior::Smart;
    return HotkeyBehavior::PushToTalk;  // also covers typos / empty / legacy
}

// What the state machine asks its owner to do. The owner (DictationSession)
// translates these into device-level Start/Stop calls.
enum class HotkeyAction {
    None,
    StartRecording,      // begin a fresh recording (Idle -> Recording-ish)
    PromoteToSticky,     // already recording, key released within threshold:
                         //   keep mic open until user taps again or Esc
    StopAndProcess,      // finish recording and run the rest of the pipeline
};

// Logical state of the dictation session as far as hotkey behavior is concerned.
// Mirrors the state machine in the [Feature] Toggle 录音模式 issue:
//   Idle ──KeyDown──> Recording (start timer)
//   Recording:
//     ├─KeyUp ≤ THRESHOLD─> StickyRecording  (smart only; toggle: not used)
//     └─KeyUp > THRESHOLD─> Stopping
//   StickyRecording:
//     ├─KeyDown          ─> Stopping
//     ├─Esc anywhere     ─> Stopping
//     └─MAX_DURATION     ─> Stopping
enum class HotkeyMachineState {
    Idle,
    Recording,
    Sticky,
};

// A tiny, side-effect-free state machine. DictationSession owns one and
// drives Start/Stop based on the action it returns; the device layer never
// looks at HotkeyBehavior.
//
// Time-based decisions (short vs long press; max-duration safety stop) take
// a steady_clock::time_point so tests can pass synthetic times.
class HotkeyBehaviorMachine {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    explicit HotkeyBehaviorMachine(HotkeyBehavior b = HotkeyBehavior::PushToTalk,
                                   int smart_threshold_ms = 400,
                                   int max_duration_ms = 300000)
        : behavior_(b),
          smart_threshold_ms_(smart_threshold_ms),
          max_duration_ms_(max_duration_ms) {}

    HotkeyMachineState State() const { return state_; }
    HotkeyBehavior     Behavior() const { return behavior_; }
    int                SmartThresholdMs() const { return smart_threshold_ms_; }
    int                MaxDurationMs() const { return max_duration_ms_; }

    // Hotkey was pressed (KEYDOWN). `now` is "the time right now".
    HotkeyAction OnKeyDown(TimePoint now);

    // Hotkey was released (KEYUP). `now` is "the time right now".
    HotkeyAction OnKeyUp(TimePoint now);

    // Esc was pressed while we were recording. Ignored when idle.
    HotkeyAction OnEscape();

    // Periodic / lazy poll: returns StopAndProcess once max_duration has been
    // exceeded since the last KeyDown that started this recording. Owner can
    // call this from a timer thread or in response to other events.
    // `now` is "the time right now".
    HotkeyAction OnTick(TimePoint now);

private:
    HotkeyBehavior     behavior_;
    int                smart_threshold_ms_;
    int                max_duration_ms_;
    HotkeyMachineState state_ = HotkeyMachineState::Idle;
    TimePoint          t_press_{};  // when the current Recording started
};

}  // namespace onekey::session
