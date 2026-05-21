#include "OpenAIPolisher.h"
#include "TranslationPrompt.h"
#include "../net/HttpClient.h"
#include "../util/Strings.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstring>

namespace onekey::polish {

namespace {

constexpr const char* kPromptTidy =
    "你是一个口语整理助手。把用户口述的内容整理成通顺的书面表达："
    "去掉口头禅（嗯、啊、那个、然后这种）、修正明显口误、加合适的中英文标点。"
    "保留原意、保留原本的语气、不要扩写或增加新内容。"
    "只输出整理后的文字，不要任何解释或前后缀。";

constexpr const char* kPromptFormal =
    "你是一个文字润色助手。把用户口述的内容改写成正式书面语："
    "去除口语化表达、调整句序使其严谨、加合适的标点。"
    "保留原意，不增加新事实。只输出润色后的文字。";

const char* PromptFor(const std::string& mode) {
    if (mode == "tidy")   return kPromptTidy;
    if (mode == "formal") return kPromptFormal;
    return nullptr;  // raw == no prompt
}

bool IsAzureFlavor(const std::string& provider, const std::string& endpoint) {
    return provider == "openai-azure" ||
           endpoint.find("openai.azure.com") != std::string::npos;
}

}  // namespace

OpenAIPolisher::OpenAIPolisher(const config::PolishConfig& cfg)
    : provider_(cfg.provider),
      mode_(cfg.mode),
      temperature_(cfg.temperature),
      max_tokens_(cfg.max_tokens) {
    const auto& po = cfg.provider_options;
    endpoint_    = po.value("endpoint", "");
    key_         = po.value("key", "");
    deployment_  = po.value("deployment", "");
    api_version_ = po.value("api_version", "2024-08-01-preview");

    spdlog::info("[polish.openai] configured provider={} deploy={} mode={}",
                 provider_, deployment_, mode_);
}

void OpenAIPolisher::Polish(const std::wstring& raw,
                            const PolishContext& ctx,
                            std::function<void(std::wstring_view, bool)> on_token) {
    // Allow per-call style override via PolishContext — this lets the user
    // change polish mode at runtime (tray menu) without rebuilding the
    // polisher. Falls back to the construction-time mode.
    const std::string& effective_mode = ctx.style.empty() ? mode_ : ctx.style;

    // ---- Translation branch ----
    // The translation hotkey (F8) sets style="translate" and supplies
    // target_language. We swap the entire prompt for a structured
    // translator prompt; everything else (HTTP transport, streaming,
    // failure handling) is shared with polish.
    bool is_translate = (effective_mode == "translate");

    std::string system_combined;
    std::string user_utf8;

    if (is_translate) {
        TranslationPromptInput tin;
        tin.target_language        = ctx.target_language.empty()
                                       ? std::string("en")
                                       : ctx.target_language;
        tin.source_language        = ctx.source_language;
        tin.detected_peer_language = ctx.detected_peer_language;
        tin.app_label              = ctx.focus_app;
        tin.scene_hint             = ctx.scene_hint;
        tin.recent_text            = ctx.recent_text;
        tin.user_typed             = ctx.user_typed;
        tin.vocab_hints            = ctx.vocab_hints;
        tin.raw_transcript         = raw;
        // Borrow the polish style ladder so Formal -> formal translation,
        // Raw -> literal translation. Construction-time mode is the
        // user's currently-selected polish.mode.
        system_combined = BuildSystemPrompt(tin, mode_);
        user_utf8       = BuildUserMessage(tin);
    } else {
        const char* sys = PromptFor(effective_mode);
        if (!sys) {
            // raw mode — no LLM call.
            if (on_token) on_token(raw, true);
            return;
        }
        user_utf8 = util::WideToUtf8(raw);
        // If the session captured focus context (via FocusContext + extractor),
        // append it to the system message as a bounded reference block. We
        // intentionally do NOT put it in the user message — the LLM should
        // treat it as background, not as part of what to rewrite.
        system_combined = sys;
        if (!ctx.surrounding_text.empty()) {
            system_combined += "\n\n";
            system_combined += util::WideToUtf8(ctx.surrounding_text);
            spdlog::info("[polish.openai] system prompt augmented with focus context "
                         "(+{} bytes)", system_combined.size() - std::strlen(sys));
        }
    }

    bool azure_flavor = IsAzureFlavor(provider_, endpoint_);

    nlohmann::json body = {
        {"messages", nlohmann::json::array({
            { {"role", "system"}, {"content", system_combined} },
            { {"role", "user"},   {"content", user_utf8} }
        })},
        {"temperature", temperature_},
        {"max_tokens",  max_tokens_},
        {"stream",      true}
    };
    if (!azure_flavor) {
        body["model"] = deployment_;
    }

    // Resolve URL
    std::string url;
    if (azure_flavor) {
        std::string base = endpoint_;
        while (!base.empty() && base.back() == '/') base.pop_back();
        url = base + "/openai/deployments/" + deployment_ +
              "/chat/completions?api-version=" + api_version_;
    } else {
        std::string base = endpoint_.empty() ? std::string("https://api.openai.com") : endpoint_;
        while (!base.empty() && base.back() == '/') base.pop_back();
        if (base.find("/v1") == std::string::npos) base += "/v1";
        url = base + "/chat/completions";
    }

    cpr::Header headers{
        {"Content-Type", "application/json"},
        {"Accept", "text/event-stream"},
        {"Expect", ""},
    };
    if (azure_flavor) {
        headers["api-key"] = key_;
    } else if (!key_.empty()) {
        headers["Authorization"] = "Bearer " + key_;
    }

    spdlog::info("[polish.openai] POST {} mode={}", url, effective_mode);

    std::string sse_buf;
    auto t0 = std::chrono::steady_clock::now();
    bool first_token_logged = false;

    auto on_chunk = [&](std::string_view data) -> bool {
        sse_buf.append(data.data(), data.size());
        for (;;) {
            auto pos = sse_buf.find('\n');
            if (pos == std::string::npos) break;
            std::string line = sse_buf.substr(0, pos);
            sse_buf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == ':') continue;
            if (line.rfind("data:", 0) != 0) continue;

            std::string payload = line.substr(5);
            if (!payload.empty() && payload[0] == ' ') payload.erase(0, 1);
            if (payload == "[DONE]") continue;

            try {
                auto j = nlohmann::json::parse(payload);
                if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) continue;
                const auto& ch0 = j["choices"][0];
                if (!ch0.contains("delta")) continue;
                const auto& delta = ch0["delta"];
                if (!delta.contains("content") || delta["content"].is_null()) continue;
                std::string token = delta["content"].get<std::string>();
                if (token.empty()) continue;
                if (!first_token_logged) {
                    first_token_logged = true;
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0).count();
                    spdlog::info("[polish.openai] first token in {}ms", ms);
                }
                std::wstring w = util::Utf8ToWide(token);
                if (on_token) on_token(w, false);
            } catch (const std::exception& e) {
                spdlog::warn("[polish.openai] sse parse: {} payload={}", e.what(), payload);
            }
        }
        return true;
    };

    auto resp = net::PostJsonStream(url, headers, body.dump(), on_chunk, 60000);
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0).count();

    if (!resp.ok()) {
        std::string err = "polish http " + std::to_string(resp.status);
        if (!resp.error_msg.empty()) err += ": " + resp.error_msg;
        spdlog::error("[polish.openai] {} body_head={}", err, resp.body.substr(0, 200));
        if (on_token) on_token(L"", true);
        return;
    }
    spdlog::info("[polish.openai] done in {}ms", total_ms);
    if (on_token) on_token(L"", true);
}

}  // namespace onekey::polish
