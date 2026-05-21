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

// Translator system prompt.
//
// Design history: earlier drafts used English with `[TARGET LANGUAGE]`,
// `[NEARBY]`, `[KEEP VERBATIM]` style uppercase tags and a numbered
// "Hard rules" block. That structure consistently triggered Azure
// OpenAI's `jailbreak` content-filter classifier on otherwise
// innocuous Chinese dictation — the shape of the prompt
// (uppercase bracket tags + dual-channel instructions in both
// system and user messages + dense imperative verbs) matches the
// statistical profile of prompt-injection templates.
//
// We now mirror the polish prompt: short Chinese narrative in the
// system message, plain transcript in the user message, no
// uppercase tags, no dual instructions.
const char* kBaseRulesZh =
    "你是一个翻译助手，嵌在按键说话的输入工具里。"
    "用户用母语口述一段话，你把它翻译成目标语言，"
    "结果会被直接注入到他正在打字的应用里（聊天、邮件、代码、论坛等）。"
    "请只输出译文本身，不要加引号、解释或前后缀。"
    "技术性专有名词、代码符号、产品名、@用户名、URL 保持原样。"
    "语气贴合上下文里对方所用的随意/正式程度。"
    "中文里的他/她/它根据上下文判断为 he/she/they。"
    "数字和时间用目标语言里自然的写法。"
    "如果用户已经说的是目标语言，就稍作整理后原样返回。";

// Per-style adjustment. Borrows the polish style ladder.
const char* StyleHint(std::string_view style) {
    if (style == "raw")    return "风格偏好：raw —— 直译，不润色。";
    if (style == "formal") return "风格偏好：formal —— 用正式书面语。";
    return                        "风格偏好：tidy —— 自然通顺，不僵硬也不松散。";
}

void AppendLine(std::ostringstream& o, const char* prefix, const std::string& v) {
    if (v.empty()) return;
    o << prefix << v << "\n";
}

}  // namespace

std::string BuildSystemPrompt(const TranslationPromptInput& in,
                              std::string_view style) {
    std::ostringstream o;
    o << kBaseRulesZh << "\n\n";
    o << StyleHint(style) << "\n";
    o << "目标语言：" << PrettyLanguageName(in.target_language)
      << "（" << PrimarySubtag(in.target_language) << "）\n";
    if (!in.source_language.empty()) {
        o << "识别语言：" << in.source_language << "\n";
    }
    if (!in.detected_peer_language.empty()) {
        o << "对方语言：" << in.detected_peer_language << "\n";
    }
    AppendLine(o, "应用：",     util::WideToUtf8(in.app_label));
    AppendLine(o, "场景：",     util::WideToUtf8(in.scene_hint));
    AppendLine(o, "周围内容：", util::WideToUtf8(in.recent_text));
    AppendLine(o, "已输入：",   util::WideToUtf8(in.user_typed));

    if (!in.vocab_hints.empty()) {
        std::ostringstream terms;
        bool first = true;
        for (const auto& w : in.vocab_hints) {
            auto u = util::WideToUtf8(w);
            if (u.empty()) continue;
            if (!first) terms << "、";
            terms << u;
            first = false;
        }
        std::string s = terms.str();
        if (!s.empty()) {
            o << "保持原样的词：" << s << "\n";
        }
    }
    return o.str();
}

std::string BuildUserMessage(const TranslationPromptInput& in) {
    // Plain transcript only. All instructions live in the system message;
    // sending a second `[REQUEST] Translate ... Output ... only` block in
    // the user channel matches prompt-injection templates and reliably
    // trips Azure's jailbreak classifier.
    return util::WideToUtf8(in.raw_transcript);
}

}  // namespace onekey::polish
