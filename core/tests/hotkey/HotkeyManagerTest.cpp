#include <gtest/gtest.h>

#include "hotkey/HotkeyManager.h"

using onekey::hotkey::HotkeyManager;

// HotkeyManager's key parser is in an anonymous namespace and the only
// public surface is Install(), which side-effects a global low-level
// keyboard hook on success. We only exercise the unknown-key branch here:
// Install() returns false *before* SetWindowsHookExW is called, giving
// us cheap coverage of the key parser's "unknown" path without touching
// the OS.

TEST(HotkeyManager, UnknownKeyNameFailsCleanly) {
    HotkeyManager mgr;
    EXPECT_FALSE(mgr.Install("definitely-not-a-key", 250));
}

TEST(HotkeyManager, EmptyKeyNameFailsCleanly) {
    HotkeyManager mgr;
    EXPECT_FALSE(mgr.Install("", 250));
}

TEST(HotkeyManager, MultiCharNonKeywordFails) {
    HotkeyManager mgr;
    // Not in the table and not a single character → ParseKey returns 0.
    EXPECT_FALSE(mgr.Install("foo", 250));
}

TEST(HotkeyManager, DestructorOnNotInstalledIsSafe) {
    // Just construct + destruct; should not call UnhookWindowsHookEx with
    // a null handle / dangling pointer.
    HotkeyManager mgr;
    EXPECT_NO_THROW(mgr.Uninstall());
}

TEST(HotkeyManager, InstallSecondaryWithoutPrimaryFails) {
    // Secondary registration depends on the LL hook the primary install
    // sets up — calling it first should fail cleanly.
    HotkeyManager mgr;
    EXPECT_FALSE(mgr.InstallSecondary("f8", 250));
}

TEST(HotkeyManager, InstallSecondaryUnknownKeyFails) {
    HotkeyManager mgr;
    // Even without a real hook, parser-level rejection comes first when the
    // key name is bogus.
    EXPECT_FALSE(mgr.InstallSecondary("definitely-not-a-key", 250));
}
