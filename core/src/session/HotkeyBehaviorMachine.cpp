#include "HotkeyBehaviorMachine.h"

namespace onekey::session {

HotkeyAction HotkeyBehaviorMachine::OnKeyDown(TimePoint now) {
    switch (state_) {
        case HotkeyMachineState::Idle:
            // Always start recording on key down. The decision about whether
            // to release on KeyUp or stick is made later in OnKeyUp.
            state_   = HotkeyMachineState::Recording;
            t_press_ = now;
            return HotkeyAction::StartRecording;

        case HotkeyMachineState::Recording:
            // OS key-repeat in push_to_talk mode (we're already holding) —
            // ignore. For toggle/smart the key was already up before this
            // KeyDown, so we shouldn't be in Recording — but be defensive.
            return HotkeyAction::None;

        case HotkeyMachineState::Sticky:
            // Second tap in toggle / smart-sticky: stop recording.
            state_ = HotkeyMachineState::Idle;
            return HotkeyAction::StopAndProcess;
    }
    return HotkeyAction::None;
}

HotkeyAction HotkeyBehaviorMachine::OnKeyUp(TimePoint now) {
    if (state_ != HotkeyMachineState::Recording) {
        // KeyUp while Idle or Sticky has no meaning here. (Sticky is entered
        // *after* the previous KeyUp was already consumed; another stray
        // KeyUp arrives only if the OS dropped a key event.)
        return HotkeyAction::None;
    }

    switch (behavior_) {
        case HotkeyBehavior::PushToTalk:
            state_ = HotkeyMachineState::Idle;
            return HotkeyAction::StopAndProcess;

        case HotkeyBehavior::Toggle: {
            // Toggle uses press → start, next press → stop. The first KeyUp
            // after StartRecording promotes to Sticky regardless of how
            // long the user held the key.
            state_ = HotkeyMachineState::Sticky;
            return HotkeyAction::PromoteToSticky;
        }

        case HotkeyBehavior::Smart: {
            auto held = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - t_press_).count();
            if (held < smart_threshold_ms_) {
                // Short tap — sticky / toggle.
                state_ = HotkeyMachineState::Sticky;
                return HotkeyAction::PromoteToSticky;
            }
            // Long press — push-to-talk behavior.
            state_ = HotkeyMachineState::Idle;
            return HotkeyAction::StopAndProcess;
        }
    }
    return HotkeyAction::None;
}

HotkeyAction HotkeyBehaviorMachine::OnEscape() {
    if (state_ == HotkeyMachineState::Idle) return HotkeyAction::None;
    state_ = HotkeyMachineState::Idle;
    return HotkeyAction::StopAndProcess;
}

HotkeyAction HotkeyBehaviorMachine::OnTick(TimePoint now) {
    if (state_ == HotkeyMachineState::Idle) return HotkeyAction::None;
    if (max_duration_ms_ <= 0) return HotkeyAction::None;  // disabled
    auto held = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - t_press_).count();
    if (held >= max_duration_ms_) {
        state_ = HotkeyMachineState::Idle;
        return HotkeyAction::StopAndProcess;
    }
    return HotkeyAction::None;
}

}  // namespace onekey::session
