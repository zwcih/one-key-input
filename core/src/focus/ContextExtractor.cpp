#include "ContextExtractor.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace onekey::focus {

namespace {

constexpr size_t kRecentTextMaxChars = 500;
constexpr size_t kMinRegionSignalChars = 20;  // <20 chars usually = button/label noise

std::wstring TrimWs(std::wstring s) {
    auto not_ws = [](wchar_t c){ return c != L' ' && c != L'\t' && c != L'\r' && c != L'\n'; };
    auto a = std::find_if(s.begin(), s.end(), not_ws);
    auto b = std::find_if(s.rbegin(), s.rend(), not_ws).base();
    if (a >= b) return {};
    return std::wstring(a, b);
}

// Friendly app name from the exe ("chrome.exe" -> "Chrome"). For unknown
// exes, just strip the extension.
std::wstring PrettyAppName(const std::wstring& exe) {
    auto lower = exe;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t c){ return (wchar_t)std::tolower((int)c); });
    if (lower == L"chrome.exe")    return L"Chrome";
    if (lower == L"msedge.exe")    return L"Edge";
    if (lower == L"firefox.exe")   return L"Firefox";
    if (lower == L"code.exe")      return L"VS Code";
    if (lower == L"cursor.exe")    return L"Cursor";
    if (lower == L"slack.exe")     return L"Slack";
    if (lower == L"teams.exe")     return L"Teams";
    if (lower == L"discord.exe")   return L"Discord";
    if (lower == L"wechat.exe")    return L"WeChat";
    if (lower == L"weixin.exe")    return L"WeChat";
    if (lower == L"feishu.exe")    return L"Feishu";
    if (lower == L"notepad.exe")   return L"Notepad";
    if (lower == L"winword.exe")   return L"Word";
    if (lower == L"outlook.exe")   return L"Outlook";
    if (lower == L"explorer.exe")  return L"Explorer";
    // Default: strip ".exe"
    if (lower.size() > 4 && lower.substr(lower.size() - 4) == L".exe") {
        return exe.substr(0, exe.size() - 4);
    }
    return exe;
}

// Detect when `before_caret` is actually the focused element's placeholder
// (UIA's TextPattern returns the placeholder when the editor is empty).
// Heuristic: before_caret == focused.name and after_caret is empty.
bool LooksLikePlaceholder(const ContextSnapshot& s) {
    if (s.before_caret.empty()) return false;
    if (s.before_caret != s.focused_name) return false;
    // If after_caret is empty, the field is effectively empty — what we
    // got is just the placeholder.
    return TrimWs(s.after_caret).empty();
}

// Score each region and pick the best one as our "recent_text" source.
// We bias toward: large region, Document type, NOT the focused element.
const TextRegion* PickBestAmbientRegion(const ContextSnapshot& s) {
    const TextRegion* best = nullptr;
    int best_score = 0;
    for (const auto& r : s.other_regions) {
        if (r.is_focused) continue;            // skip the input we're typing into
        if (r.text.size() < kMinRegionSignalChars) continue;
        // Skip regions whose text is just the window title (duplicate signal).
        if (r.text == s.window_title) continue;

        int score = static_cast<int>(std::min<size_t>(r.text.size(), 2000));
        if (r.control_type == L"Document") score += 200;
        if (r.control_type == L"Edit")     score -= 100;  // probably another input box
        if (r.control_type == L"Text" && r.text.size() < 60) score -= 50; // labelly

        if (score > best_score) {
            best_score = score;
            best = &r;
        }
    }
    return best;
}

// Take the tail of a long string (LLM cares about *recent* messages).
std::wstring Tail(const std::wstring& s, size_t max) {
    if (s.size() <= max) return s;
    return std::wstring(L"…") + s.substr(s.size() - max);
}

// Collapse runs of whitespace; strip control chars. Preserves Chinese/Unicode.
std::wstring Normalize(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    bool prev_ws = false;
    for (wchar_t c : in) {
        if (c == L'\r') continue;
        if (c == L'\n' || c == L'\t' || c == L' ') {
            if (!prev_ws && !out.empty()) {
                out.push_back(L' ');
                prev_ws = true;
            }
        } else if (c >= 0x20 || c == L'\n') {
            out.push_back(c);
            prev_ws = false;
        }
    }
    // trim
    return TrimWs(std::move(out));
}

}  // namespace

EffectiveContext Extract(const ContextSnapshot& snap) {
    EffectiveContext out;

    // ---- app_label ----
    if (!snap.app_exe.empty() || !snap.window_title.empty()) {
        std::wostringstream o;
        o << PrettyAppName(snap.app_exe);
        if (!snap.window_title.empty()) {
            std::wstring title = Normalize(snap.window_title);
            // Drop ad-suffixes like " - Google Chrome" / " — Mozilla Firefox"
            for (const auto* tail : { L" - Google Chrome", L" – Google Chrome",
                                       L" - Mozilla Firefox", L" - Microsoft Edge" }) {
                std::wstring t = tail;
                if (title.size() > t.size() &&
                    title.compare(title.size() - t.size(), t.size(), t) == 0) {
                    title.resize(title.size() - t.size());
                    break;
                }
            }
            if (!title.empty()) o << L" | window: " << title;
        }
        out.app_label = o.str();
    }

    // ---- scene_hint ----
    if (!snap.focused_control_type.empty()) {
        std::wostringstream o;
        o << L"focused " << snap.focused_control_type;
        if (!snap.focused_name.empty()) {
            // The focused name is often a label or placeholder — distinguish.
            if (LooksLikePlaceholder(snap)) {
                o << L" (placeholder: \"" << Normalize(snap.focused_name) << L"\")";
            } else {
                o << L" \"" << Normalize(snap.focused_name) << L"\"";
            }
        }
        out.scene_hint = o.str();
    }

    // ---- user_typed ----
    if (!LooksLikePlaceholder(snap)) {
        std::wstring t = Normalize(snap.before_caret);
        if (!t.empty()) out.user_typed = t;
    }
    // Selected text counts as "what the user is acting on" too.
    if (!snap.selected_text.empty()) {
        std::wstring s = Normalize(snap.selected_text);
        if (!s.empty()) {
            if (out.user_typed.empty()) out.user_typed = s;
            else out.user_typed += L" [selected: " + s + L"]";
        }
    }

    // ---- recent_text ----
    if (const TextRegion* best = PickBestAmbientRegion(snap)) {
        std::wstring t = Normalize(best->text);
        out.recent_text = Tail(t, kRecentTextMaxChars);
    }

    return out;
}

std::wstring AsPromptBlock(const EffectiveContext& ctx) {
    if (!ctx.any()) return {};
    std::wostringstream o;
    o << L"以下是用户当前所在应用的环境上下文，仅作背景参考，"
         L"用户的口述内容在 user 消息中，请优先按口述内容整理；"
         L"如果上下文能帮你判断称呼/术语/语气，可以利用。\n";
    o << L"```\n";
    if (!ctx.app_label.empty())   o << L"app: "      << ctx.app_label   << L"\n";
    if (!ctx.scene_hint.empty())  o << L"scene: "    << ctx.scene_hint  << L"\n";
    if (!ctx.user_typed.empty())  o << L"typed: \""  << ctx.user_typed  << L"\"\n";
    if (!ctx.recent_text.empty()) o << L"nearby: \"" << ctx.recent_text << L"\"\n";
    o << L"```";
    return o.str();
}

}  // namespace onekey::focus
