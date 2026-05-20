#include "WinHelpers.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <ctime>
#include <cstdio>

namespace onekey::util {

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0) return L".";
    std::wstring p(buf, n);
    auto pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return p.substr(0, pos);
}

std::wstring LocalTimestampForFilename() {
    SYSTEMTIME st;
    ::GetLocalTime(&st);
    wchar_t buf[64];
    std::swprintf(buf, 64, L"%04u%02u%02u",
                  st.wYear, st.wMonth, st.wDay);
    return std::wstring(buf);
}

}  // namespace onekey::util
