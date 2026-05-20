#include "ClipboardInjector.h"

#include <spdlog/spdlog.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <chrono>
#include <thread>
#include <string>

namespace onekey::inject {

namespace {

bool SetClipboardWide(std::wstring_view text) {
    if (!::OpenClipboard(nullptr)) {
        spdlog::warn("[inject.clip] OpenClipboard failed err={}", ::GetLastError());
        return false;
    }
    ::EmptyClipboard();

    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        ::CloseClipboard();
        return false;
    }
    void* p = ::GlobalLock(mem);
    if (!p) {
        ::GlobalFree(mem);
        ::CloseClipboard();
        return false;
    }
    std::memcpy(p, text.data(), text.size() * sizeof(wchar_t));
    static_cast<wchar_t*>(p)[text.size()] = L'\0';
    ::GlobalUnlock(mem);

    if (!::SetClipboardData(CF_UNICODETEXT, mem)) {
        ::GlobalFree(mem);
        ::CloseClipboard();
        return false;
    }
    ::CloseClipboard();
    return true;
}

std::wstring GetClipboardWide() {
    std::wstring out;
    if (!::OpenClipboard(nullptr)) return out;
    HANDLE h = ::GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = static_cast<const wchar_t*>(::GlobalLock(h));
        if (p) {
            out.assign(p);
            ::GlobalUnlock(h);
        }
    }
    ::CloseClipboard();
    return out;
}

void SendCtrlV() {
    INPUT inputs[4] = {};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';

    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    ::SendInput(4, inputs, sizeof(INPUT));
}

}  // namespace

InjectResult ClipboardInjector::InjectChunk(std::wstring_view text, const InjectTarget& /*tgt*/) {
    if (text.empty()) return InjectResult::Ok;

    // Save & restore the user's clipboard so we don't pollute it.
    std::wstring saved = GetClipboardWide();

    if (!SetClipboardWide(text)) {
        spdlog::error("[inject.clip] SetClipboardWide failed");
        return InjectResult::FullFail;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    SendCtrlV();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    if (!saved.empty()) {
        // Best-effort restore; ignore failures.
        SetClipboardWide(saved);
    }
    return InjectResult::Ok;
}

}  // namespace onekey::inject
