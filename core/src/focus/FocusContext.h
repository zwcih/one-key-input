#pragma once
#include <future>
#include <memory>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace onekey::focus {

// A single readable text region we found in the focused window.
struct TextRegion {
    std::wstring control_type;   // "Document", "Edit", "Text", "Custom", ...
    std::wstring name;           // accessible name (often the field label)
    std::wstring automation_id;  // app-defined ID, when present
    std::wstring text;           // the contents (may be long; caller decides truncation)
    bool         is_focused = false;
};

// Snapshot of what's on screen when the user starts dictating.
struct ContextSnapshot {
    // Top-level window:
    std::wstring app_exe;        // "Code.exe", "Teams.exe", ...
    std::wstring window_title;   // top-level window title
    HWND         hwnd = nullptr; // foreground window at snapshot time

    // Focused element (typically the edit control the user is about to type into):
    std::wstring focused_control_type;
    std::wstring focused_name;
    std::wstring before_caret;   // up to ~200 chars before the insertion point
    std::wstring after_caret;    // up to ~50 chars after
    std::wstring selected_text;  // if there's a selection, the selected text

    // Other text-bearing regions in the same window (chat history, doc body, etc.):
    std::vector<TextRegion> other_regions;

    // Diagnostics:
    long elapsed_ms = 0;
    std::wstring error;          // empty on success; otherwise reason snapshot failed/partial
};

// Asynchronous UIA snapshot.
//
// Call SnapshotAsync(focused_hwnd) the moment the user presses the hotkey,
// then either .get() or .wait_for(0ms) later when polish is about to run.
// All UIA / COM work happens on a private worker thread — the main thread
// is not touched. The future is always ready within a few hundred ms;
// timeouts inside the snapshot are bounded so it can't block recording.
std::future<ContextSnapshot> SnapshotAsync(HWND foreground_hwnd);

// Render a snapshot to a human-readable multi-line string for logging.
std::wstring DebugDump(const ContextSnapshot& s);

}  // namespace onekey::focus
