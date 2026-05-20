#include "Autostart.h"

#include <spdlog/spdlog.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <string>

namespace onekey::app {

namespace {

constexpr wchar_t kRunKey[]   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"OneKeyInput";

std::wstring CurrentExePath() {
    wchar_t buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

// Quote the path so spaces in the install dir survive CreateProcess parsing.
std::wstring QuotedExe() {
    return L"\"" + CurrentExePath() + L"\"";
}

}  // namespace

bool SyncAutostart(bool enabled) {
    HKEY hkey = nullptr;
    LONG rc = ::RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr,
                                REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_QUERY_VALUE,
                                nullptr, &hkey, nullptr);
    if (rc != ERROR_SUCCESS) {
        spdlog::error("[autostart] open Run key failed err={}", rc);
        return false;
    }

    bool ok = true;
    if (enabled) {
        std::wstring cmd = QuotedExe();
        rc = ::RegSetValueExW(hkey, kValueName, 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(cmd.c_str()),
                              static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
        if (rc != ERROR_SUCCESS) {
            spdlog::error("[autostart] write Run value failed err={}", rc);
            ok = false;
        } else {
            spdlog::info("[autostart] enabled: {}", std::string(cmd.begin(), cmd.end()));
        }
    } else {
        rc = ::RegDeleteValueW(hkey, kValueName);
        if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) {
            spdlog::error("[autostart] delete Run value failed err={}", rc);
            ok = false;
        } else {
            spdlog::info("[autostart] disabled");
        }
    }
    ::RegCloseKey(hkey);
    return ok;
}

bool IsAutostartEnabled() {
    HKEY hkey = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &hkey)
            != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    bool present = (::RegQueryValueExW(hkey, kValueName, nullptr, &type,
                                       nullptr, &bytes) == ERROR_SUCCESS
                    && type == REG_SZ);
    ::RegCloseKey(hkey);
    return present;
}

}  // namespace onekey::app
