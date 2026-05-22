#include <gtest/gtest.h>

#include "session/HotkeyBehaviorMachine.h"

#include <chrono>

using namespace onekey::session;
using ms = std::chrono::milliseconds;
using Clock = std::chrono::steady_clock;

namespace {

// Tiny helper so each test reads like a story.
struct Driver {
    HotkeyBehaviorMachine m;
    Clock::time_point t;

    explicit Driver(HotkeyBehavior b,
                    int threshold = 400,
                    int max_duration = 5000)
        : m(b, threshold, max_duration), t{} {}

    void Advance(int dt_ms) { t += ms(dt_ms); }
    HotkeyAction Down()   { return m.OnKeyDown(t); }
    HotkeyAction Up()     { return m.OnKeyUp(t); }
    HotkeyAction Esc()    { return m.OnEscape(); }
    HotkeyAction Tick()   { return m.OnTick(t); }
};

}  // namespace

// ===== ParseBehavior =====
TEST(HotkeyBehaviorMachine, ParseBehaviorTable) {
    EXPECT_EQ(ParseBehavior("push_to_talk"), HotkeyBehavior::PushToTalk);
    EXPECT_EQ(ParseBehavior("toggle"),       HotkeyBehavior::Toggle);
    EXPECT_EQ(ParseBehavior("smart"),        HotkeyBehavior::Smart);
    // Unknown / empty falls back to push-to-talk (legacy safe).
    EXPECT_EQ(ParseBehavior("nonsense"),     HotkeyBehavior::PushToTalk);
    EXPECT_EQ(ParseBehavior(""),             HotkeyBehavior::PushToTalk);
}

// ===== push_to_talk behavior =====
TEST(HotkeyBehaviorMachine, PushToTalkPressStartsReleaseStops) {
    Driver d(HotkeyBehavior::PushToTalk);
    EXPECT_EQ(d.Down(), HotkeyAction::StartRecording);
    EXPECT_EQ(d.m.State(), HotkeyMachineState::Recording);
    d.Advance(1500);
    EXPECT_EQ(d.Up(), HotkeyAction::StopAndProcess);
    EXPECT_EQ(d.m.State(), HotkeyMachineState::Idle);
}

TEST(HotkeyBehaviorMachine, PushToTalkRapidDoubleTapStillSane) {
    // Two start/stop cycles in a row — no sticky involvement.
    Driver d(HotkeyBehavior::PushToTalk);
    EXPECT_EQ(d.Down(), HotkeyAction::StartRecording);
    d.Advance(50);
    EXPECT_EQ(d.Up(),   HotkeyAction::StopAndProcess);
    d.Advance(50);
    EXPECT_EQ(d.Down(), HotkeyAction::StartRecording);
    EXPECT_EQ(d.Up(),   HotkeyAction::StopAndProcess);
}

// ===== toggle behavior =====
TEST(HotkeyBehaviorMachine, ToggleFirstTapStartsSecondTapStops) {
    Driver d(HotkeyBehavior::Toggle);
    EXPECT_EQ(d.Down(), HotkeyAction::StartRecording);
    EXPECT_EQ(d.Up(),   HotkeyAction::PromoteToSticky);
    EXPECT_EQ(d.m.State(), HotkeyMachineState::Sticky);
    // While sticky a stray KeyUp is ignored.
    EXPECT_EQ(d.Up(),   HotkeyAction::None);
    EXPECT_EQ(d.m.State(), HotkeyMachineState::Sticky);
    EXPECT_EQ(d.Down(), HotkeyAction::StopAndProcess);
    EXPECT_EQ(d.m.State(), HotkeyMachineState::Idle);
}

TEST(HotkeyBehaviorMachine, ToggleHoldingDoesNotMatter) {
    // Holding the key in toggle mode still promotes to sticky on release
    // (no smart-style threshold). Verify by holding 5s.
    Driver d(HotkeyBehavior::Toggle);
    EXPECT_EQ(d.Down(), HotkeyAction::StartRecording);
    d.Advance(2500);
    EXPECT_EQ(d.Up(),   HotkeyAction::PromoteToSticky);
}

TEST(HotkeyBehaviorMachine, ToggleEscapeStops) {
    Driver d(HotkeyBehavior::Toggle);
    d.Down(); d.Up();  // -> Sticky
    EXPECT_EQ(d.Esc(), HotkeyAction::StopAndProcess);
    EXPECT_EQ(d.m.State(), HotkeyMachineState::Idle);
}

// ===== smart behavior =====
TEST(HotkeyBehaviorMachine, SmartShortPressBelowThresholdGoesSticky) {
    Driver d(HotkeyBehavior::Smart, 400);
    EXPECT_EQ(d.Down(), HotkeyAction::StartRecording);
    d.Advance(200);   // < 400 ms -> short tap
    EXPECT_EQ(d.Up(),   HotkeyAction::PromoteToSticky);
    EXPECT_EQ(d.m.State(), HotkeyMachineState::Sticky);
    // Tap again to stop.
    EXPECT_EQ(d.Down(), HotkeyAction::StopAndProcess);
    EXPECT_EQ(d.m.State(), HotkeyMachineState::Idle);
}

TEST(HotkeyBehaviorMachine, SmartLongPressActsLikePushToTalk) {
    Driver d(HotkeyBehavior::Smart, 400);
    EXPECT_EQ(d.Down(), HotkeyAction::StartRecording);
    d.Advance(800);   // ≥ 400 ms -> long press
    EXPECT_EQ(d.Up(),   HotkeyAction::StopAndProcess);
    EXPECT_EQ(d.m.State(), HotkeyMachineState::Idle);
}

TEST(HotkeyBehaviorMachine, SmartThresholdBoundaryExactlyAtThresholdIsLong) {
    // Document the boundary: held == threshold counts as long press
    // (i.e. push-to-talk semantics) so the user always gets the more
    // conservative behavior right at the cutoff.
    Driver d(HotkeyBehavior::Smart, 400);
    d.Down();
    d.Advance(400);
    EXPECT_EQ(d.Up(), HotkeyAction::StopAndProcess);
}

TEST(HotkeyBehaviorMachine, SmartEscapeFromStickyStops) {
    Driver d(HotkeyBehavior::Smart, 400);
    d.Down();
    d.Advance(150);
    d.Up();  // -> Sticky
    EXPECT_EQ(d.Esc(), HotkeyAction::StopAndProcess);
}

TEST(HotkeyBehaviorMachine, SmartEscapeFromRecordingAlsoStops) {
    Driver d(HotkeyBehavior::Smart, 400);
    d.Down();
    EXPECT_EQ(d.Esc(), HotkeyAction::StopAndProcess);
    EXPECT_EQ(d.m.State(), HotkeyMachineState::Idle);
}

TEST(HotkeyBehaviorMachine, EscapeWhileIdleIsNoOp) {
    Driver d(HotkeyBehavior::Smart);
    EXPECT_EQ(d.Esc(), HotkeyAction::None);
}

// ===== max-duration watchdog =====
TEST(HotkeyBehaviorMachine, MaxDurationFiresStopForSticky) {
    Driver d(HotkeyBehavior::Smart, 400, /*max=*/3000);
    d.Down();
    d.Advance(150);
    d.Up();  // -> Sticky
    d.Advance(500);
    EXPECT_EQ(d.Tick(), HotkeyAction::None);
    d.Advance(2600);  // total >3000ms since press
    EXPECT_EQ(d.Tick(), HotkeyAction::StopAndProcess);
    EXPECT_EQ(d.m.State(), HotkeyMachineState::Idle);
}

TEST(HotkeyBehaviorMachine, MaxDurationZeroDisablesWatchdog) {
    Driver d(HotkeyBehavior::Smart, 400, /*max=*/0);
    d.Down();
    d.Advance(99999);
    EXPECT_EQ(d.Tick(), HotkeyAction::None);
}

TEST(HotkeyBehaviorMachine, OnTickIdleReturnsNone) {
    Driver d(HotkeyBehavior::PushToTalk);
    EXPECT_EQ(d.Tick(), HotkeyAction::None);
}
