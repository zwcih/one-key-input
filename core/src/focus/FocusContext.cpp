#include "FocusContext.h"
#include "../util/Strings.h"

#include <spdlog/spdlog.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <oleauto.h>
#include <ObjBase.h>
#include <UIAutomationClient.h>
#include <wrl/client.h>
#include <psapi.h>

#include <chrono>
#include <thread>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "Psapi.lib")

using Microsoft::WRL::ComPtr;

namespace onekey::focus {

namespace {

// Hard caps so a giant document can't make us log megabytes or stall.
constexpr int kBeforeCaretChars = 200;
constexpr int kAfterCaretChars  = 50;
constexpr int kRegionMaxChars   = 800;
constexpr int kMaxRegions       = 20;
constexpr int kSnapshotBudgetMs = 800;  // walk aborts past this

std::wstring BstrToWString(BSTR b) {
    if (!b) return {};
    return std::wstring(b, ::SysStringLen(b));
}

std::wstring GetExeName(HWND hwnd) {
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return L"";
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    wchar_t path[MAX_PATH] = {0};
    DWORD n = MAX_PATH;
    std::wstring name;
    if (::QueryFullProcessImageNameW(h, 0, path, &n)) {
        std::wstring p(path, n);
        auto slash = p.find_last_of(L"\\/");
        name = (slash == std::wstring::npos) ? p : p.substr(slash + 1);
    }
    ::CloseHandle(h);
    return name;
}

std::wstring GetWindowTitleW(HWND hwnd) {
    int len = ::GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};
    std::wstring s(len + 1, L'\0');
    int got = ::GetWindowTextW(hwnd, s.data(), len + 1);
    if (got <= 0) return {};
    s.resize(got);
    return s;
}

std::wstring ControlTypeName(int ct) {
    // Just the ones we care about diagnostically — others fall to "T#".
    switch (ct) {
        case UIA_DocumentControlTypeId: return L"Document";
        case UIA_EditControlTypeId:     return L"Edit";
        case UIA_TextControlTypeId:     return L"Text";
        case UIA_CustomControlTypeId:   return L"Custom";
        case UIA_PaneControlTypeId:     return L"Pane";
        case UIA_GroupControlTypeId:    return L"Group";
        case UIA_ListControlTypeId:     return L"List";
        case UIA_ListItemControlTypeId: return L"ListItem";
        default: return L"T" + std::to_wstring(ct);
    }
}

// Read element name / automation id / control type into a TextRegion stub.
void FillCommonProps(IUIAutomationElement* el, TextRegion& r) {
    CONTROLTYPEID ct = 0;
    if (SUCCEEDED(el->get_CurrentControlType(&ct))) {
        r.control_type = ControlTypeName(ct);
    }
    BSTR name = nullptr;
    if (SUCCEEDED(el->get_CurrentName(&name)) && name) {
        r.name = BstrToWString(name);
        ::SysFreeString(name);
    }
    BSTR aid = nullptr;
    if (SUCCEEDED(el->get_CurrentAutomationId(&aid)) && aid) {
        r.automation_id = BstrToWString(aid);
        ::SysFreeString(aid);
    }
}

// Read up to `max_chars` of text from a TextPattern. Returns empty on failure.
std::wstring ReadAllText(IUIAutomationTextPattern* tp, int max_chars) {
    if (!tp) return {};
    ComPtr<IUIAutomationTextRange> range;
    if (FAILED(tp->get_DocumentRange(&range)) || !range) return {};
    BSTR text = nullptr;
    if (FAILED(range->GetText(max_chars, &text)) || !text) return {};
    std::wstring s = BstrToWString(text);
    ::SysFreeString(text);
    return s;
}

// For the focused element: try to split text around the caret (or selection).
void ReadAroundCaret(IUIAutomationTextPattern* tp, ContextSnapshot& out) {
    if (!tp) return;

    // Selection (if any) — gives us "selected_text" plus a known anchor.
    ComPtr<IUIAutomationTextRangeArray> sel_arr;
    if (SUCCEEDED(tp->GetSelection(&sel_arr)) && sel_arr) {
        int n = 0;
        sel_arr->get_Length(&n);
        if (n > 0) {
            ComPtr<IUIAutomationTextRange> sel;
            sel_arr->GetElement(0, &sel);
            if (sel) {
                BSTR sel_text = nullptr;
                if (SUCCEEDED(sel->GetText(2048, &sel_text)) && sel_text) {
                    out.selected_text = BstrToWString(sel_text);
                    ::SysFreeString(sel_text);
                }

                // Caret == end of selection (or start if collapsed).
                ComPtr<IUIAutomationTextRange> doc;
                if (SUCCEEDED(tp->get_DocumentRange(&doc)) && doc) {
                    ComPtr<IUIAutomationTextRange> before;
                    doc->Clone(&before);
                    if (before) {
                        // before = [doc.start ... sel.start]
                        before->MoveEndpointByRange(TextPatternRangeEndpoint_End,
                                                    sel.Get(),
                                                    TextPatternRangeEndpoint_Start);
                        BSTR tb = nullptr;
                        if (SUCCEEDED(before->GetText(kBeforeCaretChars * 4, &tb)) && tb) {
                            std::wstring s = BstrToWString(tb);
                            ::SysFreeString(tb);
                            if (s.size() > kBeforeCaretChars) {
                                s = s.substr(s.size() - kBeforeCaretChars);
                            }
                            out.before_caret = std::move(s);
                        }
                    }
                    ComPtr<IUIAutomationTextRange> after;
                    doc->Clone(&after);
                    if (after) {
                        // after = [sel.end ... doc.end]
                        after->MoveEndpointByRange(TextPatternRangeEndpoint_Start,
                                                   sel.Get(),
                                                   TextPatternRangeEndpoint_End);
                        BSTR ta = nullptr;
                        if (SUCCEEDED(after->GetText(kAfterCaretChars, &ta)) && ta) {
                            out.after_caret = BstrToWString(ta);
                            ::SysFreeString(ta);
                        }
                    }
                }
                return;
            }
        }
    }

    // No selection — just dump first N chars; many controls won't have a
    // separable caret position via UIA (Web fields, Slack composer).
    out.before_caret = ReadAllText(tp, kBeforeCaretChars);
}

// Walk the subtree below `root`, collecting any element exposing TextPattern.
// Bounded by kMaxRegions and a time budget.
void CollectTextRegions(IUIAutomation* uia,
                        IUIAutomationElement* root,
                        IUIAutomationElement* focused,
                        std::vector<TextRegion>& out,
                        std::chrono::steady_clock::time_point deadline) {
    // ControlView filters out raw containers; we still get edits/documents/lists.
    ComPtr<IUIAutomationCondition> cond;
    if (FAILED(uia->CreateTrueCondition(&cond))) return;

    ComPtr<IUIAutomationElementArray> found;
    if (FAILED(root->FindAll(TreeScope_Descendants, cond.Get(), &found)) || !found) {
        return;
    }
    int n = 0;
    found->get_Length(&n);

    for (int i = 0; i < n && (int)out.size() < kMaxRegions; ++i) {
        if (std::chrono::steady_clock::now() > deadline) break;

        ComPtr<IUIAutomationElement> el;
        if (FAILED(found->GetElement(i, &el)) || !el) continue;

        ComPtr<IUnknown> raw;
        if (FAILED(el->GetCurrentPattern(UIA_TextPatternId, &raw)) || !raw) continue;
        ComPtr<IUIAutomationTextPattern> tp;
        if (FAILED(raw.As(&tp)) || !tp) continue;

        std::wstring text = ReadAllText(tp.Get(), kRegionMaxChars);
        if (text.empty()) continue;

        TextRegion r;
        FillCommonProps(el.Get(), r);
        r.text = std::move(text);

        if (focused) {
            BOOL same = FALSE;
            if (SUCCEEDED(uia->CompareElements(el.Get(), focused, &same))) {
                r.is_focused = (same == TRUE);
            }
        }
        out.push_back(std::move(r));
    }
}

ContextSnapshot DoSnapshot(HWND hwnd) {
    auto t0 = std::chrono::steady_clock::now();
    ContextSnapshot snap;
    snap.hwnd = hwnd;
    if (!hwnd) {
        snap.error = L"no foreground hwnd";
        return snap;
    }

    snap.app_exe      = GetExeName(hwnd);
    snap.window_title = GetWindowTitleW(hwnd);

    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool com_inited = SUCCEEDED(hr);

    ComPtr<IUIAutomation> uia;
    hr = ::CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(&uia));
    if (FAILED(hr) || !uia) {
        snap.error = L"CoCreateInstance(CUIAutomation) failed";
        if (com_inited) ::CoUninitialize();
        return snap;
    }

    // Root = the top-level window element. Going element-from-handle keeps
    // us in the right process scope.
    ComPtr<IUIAutomationElement> root;
    if (FAILED(uia->ElementFromHandle(hwnd, &root)) || !root) {
        snap.error = L"ElementFromHandle failed";
        if (com_inited) ::CoUninitialize();
        return snap;
    }

    // Focused element — may be the same as root, may be deep inside.
    ComPtr<IUIAutomationElement> focused;
    uia->GetFocusedElement(&focused);

    auto deadline = t0 + std::chrono::milliseconds(kSnapshotBudgetMs);

    // Focused element: type + caret-relative text.
    if (focused) {
        CONTROLTYPEID ct = 0;
        focused->get_CurrentControlType(&ct);
        snap.focused_control_type = ControlTypeName(ct);

        BSTR name = nullptr;
        if (SUCCEEDED(focused->get_CurrentName(&name)) && name) {
            snap.focused_name = BstrToWString(name);
            ::SysFreeString(name);
        }

        ComPtr<IUnknown> raw;
        if (SUCCEEDED(focused->GetCurrentPattern(UIA_TextPatternId, &raw)) && raw) {
            ComPtr<IUIAutomationTextPattern> tp;
            if (SUCCEEDED(raw.As(&tp)) && tp) {
                ReadAroundCaret(tp.Get(), snap);
            }
        }
    }

    // Other regions in the same window (chat history, doc body, sibling fields).
    CollectTextRegions(uia.Get(), root.Get(), focused.Get(),
                       snap.other_regions, deadline);

    if (com_inited) ::CoUninitialize();
    snap.elapsed_ms = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count());
    return snap;
}

}  // namespace

std::future<ContextSnapshot> SnapshotAsync(HWND hwnd) {
    // std::async with launch::async — each call gets its own thread; UIA
    // can run on any STA, and we tear it down when done. The promise is
    // satisfied even if DoSnapshot throws (it doesn't, but defensive).
    return std::async(std::launch::async, [hwnd]{
        try {
            return DoSnapshot(hwnd);
        } catch (const std::exception& e) {
            ContextSnapshot s;
            s.hwnd = hwnd;
            s.error = util::Utf8ToWide(e.what());
            return s;
        } catch (...) {
            ContextSnapshot s;
            s.hwnd = hwnd;
            s.error = L"unknown exception in DoSnapshot";
            return s;
        }
    });
}

std::wstring DebugDump(const ContextSnapshot& s) {
    auto trim_log = [](std::wstring v, size_t max) {
        // Strip newlines for log readability.
        for (auto& c : v) if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
        if (v.size() > max) v = v.substr(0, max) + L"…";
        return v;
    };

    std::wostringstream o;
    o << L"\n  app=" << s.app_exe
      << L" title=\"" << trim_log(s.window_title, 80) << L"\""
      << L" elapsed=" << s.elapsed_ms << L"ms";
    if (!s.error.empty()) {
        o << L" ERROR=" << s.error;
    }
    o << L"\n  focused: type=" << s.focused_control_type
      << L" name=\"" << trim_log(s.focused_name, 60) << L"\""
      << L" before=\"" << trim_log(s.before_caret, 80) << L"\""
      << L" after=\""  << trim_log(s.after_caret, 40)  << L"\"";
    if (!s.selected_text.empty()) {
        o << L"\n  selected=\"" << trim_log(s.selected_text, 100) << L"\"";
    }
    o << L"\n  other_regions=" << s.other_regions.size();
    int shown = 0;
    for (const auto& r : s.other_regions) {
        if (shown++ >= 8) {  // cap log noise
            o << L"\n    ... (" << (s.other_regions.size() - 8) << L" more)";
            break;
        }
        o << L"\n    [" << r.control_type << L"]"
          << (r.is_focused ? L" *focused*" : L"")
          << L" name=\"" << trim_log(r.name, 40) << L"\""
          << L" text=\"" << trim_log(r.text, 120) << L"\"";
    }
    return o.str();
}

}  // namespace onekey::focus
