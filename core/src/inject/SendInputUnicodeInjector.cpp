#include "SendInputUnicodeInjector.h"

#include <spdlog/spdlog.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <vector>

namespace onekey::inject {

namespace {

// Build a list of UNICODE keydown+keyup INPUT events for the given text.
// Surrogate pairs (chars > U+FFFF, e.g. emoji) are sent as two consecutive
// UTF-16 code units, which is the protocol expected by SendInput.
std::vector<INPUT> BuildUnicodeInputs(std::wstring_view text) {
    std::vector<INPUT> inputs;
    inputs.reserve(text.size() * 2);

    for (wchar_t ch : text) {
        // Newlines get split into VK_RETURN events so target widgets that
        // treat \n as "send" or "newline" behave correctly.
        if (ch == L'\n') {
            INPUT down{};
            down.type = INPUT_KEYBOARD;
            down.ki.wVk = VK_RETURN;
            INPUT up = down;
            up.ki.dwFlags = KEYEVENTF_KEYUP;
            inputs.push_back(down);
            inputs.push_back(up);
            continue;
        }
        if (ch == L'\r') {
            continue;  // ignore — Windows newlines come in as \r\n
        }

        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wScan = static_cast<WORD>(ch);
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        INPUT up = down;
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(down);
        inputs.push_back(up);
    }
    return inputs;
}

}  // namespace

InjectResult SendInputUnicodeInjector::InjectChunk(std::wstring_view text,
                                                   const InjectTarget& /*tgt*/) {
    if (text.empty()) return InjectResult::Ok;

    auto inputs = BuildUnicodeInputs(text);
    if (inputs.empty()) return InjectResult::Ok;

    // SendInput accepts UINT count. Chunk if the message exceeds reasonable size
    // to avoid hitting per-call OS limits (~5000 events is fine, we go 1000 to
    // give the target app a chance to keep up).
    constexpr UINT kBatch = 1000;
    UINT sent_total = 0;
    for (size_t off = 0; off < inputs.size(); off += kBatch) {
        UINT batch = static_cast<UINT>(std::min<size_t>(kBatch, inputs.size() - off));
        UINT sent = ::SendInput(batch, inputs.data() + off, sizeof(INPUT));
        sent_total += sent;
        if (sent != batch) {
            DWORD err = ::GetLastError();
            spdlog::warn("[inject.sendinput] partial send: {}/{} err={}",
                         sent_total, inputs.size(), err);
            return (sent_total == 0) ? InjectResult::FullFail : InjectResult::PartialFail;
        }
    }
    return InjectResult::Ok;
}

}  // namespace onekey::inject
