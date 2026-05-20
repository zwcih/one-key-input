#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <thread>

namespace onekey::app {

// Watches a single file's containing directory and invokes a callback when
// that file is modified. Uses ReadDirectoryChangesW (overlapped) so we can
// stop cleanly via Stop().
//
// The callback fires on a worker thread; marshal to the UI/main thread if
// you need to interact with windows.
class ConfigWatcher {
public:
    ConfigWatcher();
    ~ConfigWatcher();

    // Begin watching `path`. Returns false on hard failure (e.g. directory
    // can't be opened). Safe to call once per instance.
    bool Start(const std::filesystem::path& path, std::function<void()> on_changed);
    void Stop();

private:
    void Run();

    std::filesystem::path        path_;
    std::filesystem::path        dir_;
    std::wstring                 filename_;          // basename, for filtering
    std::function<void()>        cb_;
    std::thread                  thread_;
    void*                        dir_handle_ = nullptr;  // HANDLE
    void*                        stop_event_ = nullptr;  // HANDLE — manual-reset
    std::atomic<bool>            running_{false};
};

}  // namespace onekey::app
