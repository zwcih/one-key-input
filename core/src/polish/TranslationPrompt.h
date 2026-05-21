#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace onekey::polish {

// Inputs needed to assemble a structured translation prompt.
//
// Pure data — no IO, no globals. Built by the session layer right before
// the LLM call from configuration + focus::EffectiveContext + ASR output.
struct TranslationPromptInput {
    // BCP-47-ish language tag the user wants the *output* in.
    // Examples: "en", "zh", "ja", "ko", "de", "fr", "es".
    std::string target_language = "en";

    // BCP-47-ish language of the ASR input (e.g. "zh-CN", "en-US").
    // Optional — when present we hint the LLM about the source side, which
    // matters for code-switched utterances ("修一下 parseConfig").
    std::string source_language;

    // Detected language of the focused window's surrounding text. Used by
    // the smart-target heuristic to decide whether to override
    // target_language. The prompt itself only renders [对方语言] when this
    // is non-empty.
    std::string detected_peer_language;

    // App / window / scene labels lifted from focus::EffectiveContext.
    // Kept as wstring because that's the form the focus extractor produces;
    // converted to UTF-8 internally.
    std::wstring app_label;       // "Chrome | window: GitHub - Issue #..."
    std::wstring scene_hint;      // "focused Edit (placeholder: ...)"
    std::wstring recent_text;     // last few hundred chars of nearby text
    std::wstring user_typed;      // what the user already typed at caret

    // Identifier-like tokens harvested from the focused window — code
    // symbols, product names, @handles. The LLM is told to keep these
    // verbatim. Empty list ⇒ no terminology block emitted.
    std::vector<std::wstring> vocab_hints;

    // The transcript to translate. Empty input ⇒ empty prompt body
    // (callers should short-circuit in that case).
    std::wstring raw_transcript;
};

// Decide whether the transcript actually needs translation.
//
// Logic per the design: if the (best-effort) detected language of `raw`
// already matches `target_language`, there's nothing to do — return false
// and the session will pass the transcript through (or run polish on it).
//
// `source_language` is the configured ASR language tag (e.g. "zh-CN").
// `target_language` is the user's translate.target_language ("en").
// We compare on the primary subtag only ("zh-CN" vs "en" → different).
bool NeedsTranslation(std::string_view source_language,
                      std::string_view target_language);

// Map "en"/"zh"/"ja"/... → human-readable name used in the prompt
// ("English" / "Chinese" / "Japanese"). Unknown codes pass through
// unchanged so the LLM at least sees the tag.
std::string PrettyLanguageName(std::string_view tag);

// Build the structured system prompt that goes into the LLM `system`
// message. Output is UTF-8. Empty fields are omitted from the result.
//
// The `style` argument is the polish style currently selected ("raw" /
// "tidy" / "formal") — translation borrows the style so that "Formal"
// produces a more formal translation while "Raw" stays close to the
// literal meaning. Per the issue: translation does NOT have its own
// independent style ladder.
std::string BuildSystemPrompt(const TranslationPromptInput& in,
                              std::string_view style);

// Build the user-message body — currently just `[原文] <transcript>`
// with a final `[要求]` line. Kept separate from the system prompt so the
// transcript stays in the `user` role (where the model expects it).
std::string BuildUserMessage(const TranslationPromptInput& in);

}  // namespace onekey::polish
