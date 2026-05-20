#pragma once
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace onekey::inject {

struct InjectTarget {
    HWND focused_hwnd = nullptr;
    std::wstring process_name;
    bool prefer_native = false;
};

enum class InjectResult { Ok, Refused, PartialFail, FullFail };

class IInjector {
public:
    virtual ~IInjector() = default;
    virtual InjectResult InjectChunk(std::wstring_view text, const InjectTarget& tgt) = 0;
    virtual void Commit(const InjectTarget& /*tgt*/) {}
};

}  // namespace onekey::inject
