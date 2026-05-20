#pragma once
#include "FocusContext.h"
#include <string>

namespace onekey::focus {

// Distilled, polish-ready summary of the user's surroundings.
// Built from a raw ContextSnapshot (today: UIA; tomorrow: maybe OCR or A11y).
//
// Designed for direct interpolation into an LLM system prompt — fields are
// already short, denoised, and human-readable. Empty fields should be
// omitted from the prompt entirely (no point telling the LLM "scene_hint:").
struct EffectiveContext {
    // One-line description of where the user is. Empty if unknown.
    // Example: "Chrome | window: 龙虾-找工作 - 通话 - NewBYR"
    std::wstring app_label;

    // Short description of the focused control (kind + accessible name).
    // Example: "chat composer (placeholder: 编写信息 ...)"
    // Empty when nothing useful is known.
    std::wstring scene_hint;

    // Text the user has already typed at the caret. NOT placeholder text.
    // Empty when we can't reliably distinguish from placeholder.
    std::wstring user_typed;

    // Most-recent ambient text around the focus — typically the tail of the
    // main Document region (chat history, document body). Capped to a few
    // hundred chars. Empty if no good region found.
    std::wstring recent_text;

    // True if at least one of the above fields is non-empty.
    bool any() const {
        return !app_label.empty() || !scene_hint.empty()
            || !user_typed.empty() || !recent_text.empty();
    }
};

// Pure function — no IO, no thread, no globals. Apply cleaning rules to
// a raw snapshot. Safe to call from anywhere.
EffectiveContext Extract(const ContextSnapshot& snap);

// Render the EffectiveContext as a fenced block suitable for pasting
// into an LLM system message. Returns empty string when nothing useful.
std::wstring AsPromptBlock(const EffectiveContext& ctx);

}  // namespace onekey::focus
