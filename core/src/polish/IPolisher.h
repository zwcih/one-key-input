#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace onekey::polish {

// Optional contextual hints for the polisher.
// Slice 1: only `style` is consumed. Other fields are reserved for M5 (focus context).
struct PolishContext {
    std::wstring focus_app;
    std::wstring window_title;
    std::wstring surrounding_text;
    std::vector<std::wstring> vocab_hints;
    std::string  style = "tidy"; // raw | tidy | formal | code-comment | im-chat
};

// Polisher contract.
//
// Construction:
//   Concrete polishers receive their full PolishConfig (including provider_options)
//   via constructor or factory. The session layer never reads provider-specific config.
//
// Polish(raw, ctx, on_token):
//   MUST be synchronous: by the time Polish() returns, every on_token call has
//   already fired AND the final on_token(_, is_final=true) has been delivered.
//   The session relies on this to know the dictation is fully injected.
//
//   Streaming providers (OpenAI SSE, llama.cpp token-by-token) fire on_token
//   many times. One-shot providers (e.g. a "raw" pass-through) may fire on_token
//   once with the whole text. In both cases the final call has is_final=true.
//
//   If polish fails, the implementation should still call on_token(_, true)
//   so the caller's flush logic completes (typically with empty token or the
//   raw text as fallback).
class IPolisher {
public:
    virtual ~IPolisher() = default;
    virtual void Polish(
        const std::wstring& raw,
        const PolishContext& ctx,
        std::function<void(std::wstring_view token, bool is_final)> on_token) = 0;
};

}  // namespace onekey::polish
