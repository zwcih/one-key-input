#include "SelfRestart.h"
#include "../util/WinHelpers.h"
#include "../util/Strings.h"

#include <spdlog/spdlog.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace onekey::app {

namespace {

std::wstring GetExePath() {
    wchar_t buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

bool FileExists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

std::optional<std::filesystem::path> FindSettingsExe() {
    // 1. Env override (dev convenience).
    wchar_t env[MAX_PATH * 2];
    DWORD n = ::GetEnvironmentVariableW(L"ONEKEY_SETTINGS_EXE", env, MAX_PATH * 2);
    if (n > 0 && n < MAX_PATH * 2) {
        std::filesystem::path p(env);
        if (FileExists(p)) return p;
    }

    std::filesystem::path exe_dir(util::ExeDir());

    // 2. Sibling install layout.
    auto sibling = exe_dir / L"onekey-settings.exe";
    if (FileExists(sibling)) return sibling;

    // 3. Dev fallback: from build/default/bin/, settings exe is at
    //    ../../../../settings/src-tauri/target/release/onekey-settings.exe
    auto dev = exe_dir / L".." / L".." / L".." / L".." / L"settings"
                       / L"src-tauri" / L"target" / L"release"
                       / L"onekey-settings.exe";
    if (FileExists(dev)) return std::filesystem::canonical(dev);
    return std::nullopt;
}

}  // namespace

bool LaunchSettings() {
    auto exe = FindSettingsExe();
    if (!exe) {
        spdlog::warn("[settings] onekey-settings.exe not found "
                     "(checked env, exe dir, dev path)");
        return false;
    }
    spdlog::info("[settings] launching {}", exe->string());

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask  = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = exe->c_str();
    info.nShow  = SW_SHOWNORMAL;
    if (!::ShellExecuteExW(&info)) {
        spdlog::error("[settings] ShellExecute failed err={}", ::GetLastError());
        return false;
    }
    if (info.hProcess) ::CloseHandle(info.hProcess);
    return true;
}

bool SelfRestart(unsigned long main_tid) {
    std::wstring exe = GetExePath();
    if (exe.empty()) {
        spdlog::error("[restart] GetModuleFileName failed");
        return false;
    }
    spdlog::info("[restart] respawning self: {}", util::WideToUtf8(exe));

    // Use CreateProcess so we can keep the new instance detached without
    // inheriting our hotkey hook / message queue handles.
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    // CommandLine must be writable per CreateProcess docs.
    std::wstring cmd = L"\"" + exe + L"\"";
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    if (!::CreateProcessW(exe.c_str(), cmd_buf.data(),
                          nullptr, nullptr, FALSE, flags,
                          nullptr, nullptr, &si, &pi)) {
        spdlog::error("[restart] CreateProcess failed err={}", ::GetLastError());
        return false;
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);

    // Tear down current process. Posting WM_QUIT lets RAII destructors fire.
    ::PostThreadMessageW(main_tid, WM_QUIT, 0, 0);
    return true;
}

}  // namespace onekey::app
