#include "ConfigWatcher.h"

#include <spdlog/spdlog.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <chrono>

namespace onekey::app {

ConfigWatcher::ConfigWatcher() = default;

ConfigWatcher::~ConfigWatcher() { Stop(); }

bool ConfigWatcher::Start(const std::filesystem::path& path,
                          std::function<void()> on_changed) {
    if (running_.load()) return false;

    path_ = path;
    dir_  = path.parent_path();
    filename_ = path.filename().wstring();
    // Lowercase compare so we tolerate notepad / VS Code casing quirks.
    std::transform(filename_.begin(), filename_.end(), filename_.begin(),
                   [](wchar_t c){ return static_cast<wchar_t>(::towlower(c)); });
    cb_ = std::move(on_changed);

    HANDLE h = ::CreateFileW(dir_.c_str(),
                             FILE_LIST_DIRECTORY,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr,
                             OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                             nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        spdlog::error("[watcher] CreateFile({}) failed err={}",
                      dir_.string(), ::GetLastError());
        return false;
    }
    dir_handle_ = h;
    stop_event_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event_) {
        ::CloseHandle(h);
        dir_handle_ = nullptr;
        return false;
    }

    running_.store(true);
    thread_ = std::thread([this]{ Run(); });
    spdlog::info("[watcher] watching {}", path_.string());
    return true;
}

void ConfigWatcher::Stop() {
    if (!running_.exchange(false)) return;
    if (stop_event_) ::SetEvent(static_cast<HANDLE>(stop_event_));
    if (dir_handle_) ::CancelIoEx(static_cast<HANDLE>(dir_handle_), nullptr);
    if (thread_.joinable()) thread_.join();
    if (dir_handle_) { ::CloseHandle(static_cast<HANDLE>(dir_handle_)); dir_handle_ = nullptr; }
    if (stop_event_) { ::CloseHandle(static_cast<HANDLE>(stop_event_)); stop_event_ = nullptr; }
}

void ConfigWatcher::Run() {
    HANDLE h_dir  = static_cast<HANDLE>(dir_handle_);
    HANDLE h_stop = static_cast<HANDLE>(stop_event_);

    // 4 KB buffer is more than enough for our directory: we only care about
    // a single file in a typically near-empty bin/ directory.
    alignas(DWORD) BYTE buf[4096];

    OVERLAPPED ov{};
    ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
        spdlog::error("[watcher] CreateEvent failed");
        return;
    }

    auto last_fire = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    while (running_.load()) {
        ::ResetEvent(ov.hEvent);
        DWORD bytes = 0;
        BOOL ok = ::ReadDirectoryChangesW(h_dir, buf, sizeof(buf), FALSE,
                                          FILE_NOTIFY_CHANGE_LAST_WRITE |
                                          FILE_NOTIFY_CHANGE_SIZE |
                                          FILE_NOTIFY_CHANGE_FILE_NAME,
                                          &bytes, &ov, nullptr);
        if (!ok) {
            DWORD err = ::GetLastError();
            if (err == ERROR_OPERATION_ABORTED) break;
            spdlog::error("[watcher] ReadDirectoryChanges failed err={}", err);
            break;
        }

        HANDLE waits[] = { ov.hEvent, h_stop };
        DWORD wait = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0) break;  // stop or error

        DWORD got = 0;
        if (!::GetOverlappedResult(h_dir, &ov, &got, FALSE) || got == 0) {
            continue;
        }

        // Walk the records, look for our file.
        bool matched = false;
        BYTE* p = buf;
        while (true) {
            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(p);
            std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
            std::transform(name.begin(), name.end(), name.begin(),
                           [](wchar_t c){ return static_cast<wchar_t>(::towlower(c)); });
            if (name == filename_) { matched = true; break; }
            if (info->NextEntryOffset == 0) break;
            p += info->NextEntryOffset;
        }

        if (matched) {
            // Editors (notepad, VS Code) often save via tmp+rename which
            // fires multiple events — coalesce within 500ms.
            auto now = std::chrono::steady_clock::now();
            if (now - last_fire > std::chrono::milliseconds(500)) {
                last_fire = now;
                // Give the editor a beat to finish flushing.
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                if (cb_) cb_();
            }
        }
    }

    ::CloseHandle(ov.hEvent);
}

}  // namespace onekey::app
