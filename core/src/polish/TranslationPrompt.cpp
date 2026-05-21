#include "TranslationPrompt.h"
#include "../util/Strings.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace onekey::polish {

namespace {

// Keep just the primary subtag of a BCP-47 tag: "zh-CN" -> "zh", "en_US" -> "en".
// Lowercased so comparisons are case-insensitive.
std::string PrimarySubtag(std::string_view tag) {
    std::string out;
    out.reserve(tag.size());
    for (char c : tag) {
        if (c == '-' || c == '_') break;
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

}  // namespace

bool NeedsTranslation(std::string_view source_language,
                      std::string_view target_language) {
    auto src = PrimarySubtag(source_language);
    auto dst = PrimarySubtag(target_language);
    if (dst.empty()) return false;       // no target -> nothing to do
    if (src.empty()) return true;        // unknown source, defer to LLM
    return src != dst;
}

std::string PrettyLanguageName(std::string_view tag) {
    static const std::unordered_map<std::string, std::string> kNames = {
        {"en", "English"},
        {"zh", "Chinese"},
        {"ja", "Japanese"},
        {"ko", "Korean"},
        {"de", "German"},
        {"fr", "French"},
        {"es", "Spanish"},
        {"it", "Italian"},
        {"pt", "Portuguese"},
        {"ru", "Russian"},
        {"ar", "Arabic"},
        {"hi", "Hindi"},
        {"vi", "Vietnamese"},
        {"th", "Thai"},
        {"id", "Indonesian"},
        {"tr", "Turkish"},
        {"nl", "Dutch"},
        {"pl", "Polish"},
        {"sv", "Swedish"},
    };
    auto primary = PrimarySubtag(tag);
    auto it = kNames.find(primary);
    if (it == kNames.end()) return std::string(tag);
    return it->second;
}

namespace {

// Minimal structured-prompt rules. Phrased in English so the system prompt
// stays a manageable size across languages — the LLM is fluent in both
// directions regardless.
const char* kBaseRules =
    "You are a professional translator embedded in a press-to-talk dictation "
    "tool. The user dictates in their native language; your job is to render "
    "the dictation as natural, idiomatic text in the configured TARGET "
    "LANGUAGE so it can be injected verbatim into a chat box, code editor, "
    "email composer, or similar. "
    "Hard rules: "
    "(1) Output ONLY the translated text — no explanations, no quotes, no "
    "preface, no \"Here is the translation:\" prefix. "
    "(2) Preserve any technical identifiers (code symbols, product names, "
    "@handles, URLs) verbatim — never localize them. "
    "(3) Match the register and tone the OTHER PARTY in the surrounding "
    "context is using. If they wrote \"hi\" don't reply with \"Dear Sir\"; "
    "if they wrote \"Dear Mr. Zhang\" don't reply with \"yo\". "
    "(4) Disambiguate Chinese pronouns (他/她/它) using the surrounding "
    "context — pick he/she/they based on who is being referred to. "
    "(5) Localize numbers and times to natural target-language forms "
    "(\"两万\" -> \"20,000\", \"明天下午三点\" -> \"3 PM tomorrow\", "
    "not \"15:00\"). "
    "(6) If the dictation is already in the target language, return it "
    "essentially unchanged (light cleanup is OK). ";

// Per-style adjustment. Borrows the polish style ladder.
const char* StyleHint(std::string_view style) {
    if (style == "raw")    return "Style: raw — stay literal, do not embellish.";
    if (style == "formal") return "Style: formal — use polished, business-appropriate language.";
    return                        "Style: tidy — natural, clean, neither stiff nor sloppy.";
}

void AppendKv(std::ostringstream& o, const char* label, const std::string& v) {
    if (v.empty()) return;
    o << "[" << label << "] " << v << "\n";
}

}  // namespace

std::string BuildSystemPrompt(const TranslationPromptInput& in,
                              std::string_view style) {
    std::ostringstream o;
    o << kBaseRules << "\n\n";
    o << StyleHint(style) << "\n";
    o << "[TARGET LANGUAGE] " << PrettyLanguageName(in.target_language)
      << " (" << PrimarySubtag(in.target_language) << ")\n";
    if (!in.source_language.empty()) {
        o << "[SOURCE LANGUAGE (ASR)] " << in.source_language << "\n";
    }
    if (!in.detected_peer_language.empty()) {
        o << "[PEER LANGUAGE (detected)] " << in.detected_peer_language << "\n";
    }
    AppendKv(o, "APP",     util::WideToUtf8(in.app_label));
    AppendKv(o, "SCENE",   util::WideToUtf8(in.scene_hint));
    AppendKv(o, "NEARBY",  util::WideToUtf8(in.recent_text));
    AppendKv(o, "TYPED",   util::WideToUtf8(in.user_typed));

    if (!in.vocab_hints.empty()) {
        std::ostringstream terms;
        bool first = true;
        for (const auto& w : in.vocab_hints) {
            auto u = util::WideToUtf8(w);
            if (u.empty()) continue;
            if (!first) terms << ", ";
            terms << u;
            first = false;
        }
        std::string s = terms.str();
        if (!s.empty()) {
            o << "[KEEP VERBATIM] " << s << "\n";
        }
    }
    return o.str();
}

std::string BuildUserMessage(const TranslationPromptInput& in) {
    std::ostringstream o;
    o << "[ORIGINAL]\n" << util::WideToUtf8(in.raw_transcript) << "\n\n";
    o << "[REQUEST] Translate the [ORIGINAL] into "
      << PrettyLanguageName(in.target_language)
      << ". Keep technical identifiers verbatim. Match the surrounding "
         "register. Output translated text only.";
    return o.str();
}

}  // namespace onekey::polish
