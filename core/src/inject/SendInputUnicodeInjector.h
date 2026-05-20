#pragma once
#include "IInjector.h"

namespace onekey::inject {

// Send text as Unicode keystrokes via SendInput + KEYEVENTF_UNICODE (VK_PACKET).
// Does not touch the clipboard. Works in most modern apps; can fail in:
//   - Elevated apps (when the injector is not elevated) — UIPI blocks SendInput
//   - Some old Win32 controls that read raw VK codes only (IME compatible mode)
//   - DirectX exclusive-mode windows
// Caller is expected to wrap this in a strategy that can fall back.
class SendInputUnicodeInjector : public IInjector {
public:
    InjectResult InjectChunk(std::wstring_view text, const InjectTarget& tgt) override;
};

}  // namespace onekey::inject
