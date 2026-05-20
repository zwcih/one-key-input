// onekey-core entry point.
//
// We're linked as a WINDOWS-subsystem app (so no console window pops up
// when launched from Explorer / autostart). Pass `--console` on the command
// line to attach/allocate a console and see stdout/stderr — handy when
// running from a terminal during development.

#include "app/Application.h"
#include "app/SelfRestart.h"
#include "config/Config.h"
#include "log/Logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <fcntl.h>
#include <io.h>

#include <atomic>
#include <cstdio>
#include <cwchar>

namespace {

std::atomic<DWORD> g_main_tid{0};

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl) {
    switch (ctrl) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT: {
            DWORD tid = g_main_tid.load();
            if (tid != 0) {
                ::PostThreadMessageW(tid, WM_QUIT, 0, 0);
            }
            return TRUE;
        }
        default:
            return FALSE;
    }
}

// Attach to the parent console if we have one (terminal launch), or allocate
// a fresh one otherwise. Then point the C stdio FILE* handles at it so
// printf / fwprintf etc. become visible.
void AttachOrAllocConsole() {
    if (!::AttachConsole(ATTACH_PARENT_PROCESS)) {
        ::AllocConsole();
    }
    // Re-bind C runtime stdio to the console.
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$",  "r", stdin);
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCtrlHandler(&ConsoleCtrlHandler, TRUE);
}

bool HasFlag(int argc, wchar_t** argv, const wchar_t* flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], flag) == 0) return true;
    }
    return false;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    g_main_tid.store(::GetCurrentThreadId());

    // No console by default; --console makes stdout/stderr visible and wires
    // up Ctrl+C handling.
    if (HasFlag(argc, argv, L"--console")) {
        AttachOrAllocConsole();
    }

    // First-run detection: if the config has no real credentials, the
    // engines would throw on init. Launch the settings UI to let the user
    // configure, then exit. The settings exe will spawn us back when it
    // writes a valid config.
    try {
        auto cfg = onekey::config::Load();
        if (onekey::config::IsFirstRun(cfg)) {
            std::fwprintf(stderr,
                L"[onekey] no credentials configured — opening Settings.\n"
                L"After saving, Settings will start the app for you.\n");
            onekey::app::LaunchSettings();
            return 0;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[onekey] config load failed: %s\n", e.what());
        onekey::app::LaunchSettings();
        return 1;
    }

    onekey::app::Application app;
    if (!app.Init()) {
        std::fwprintf(stderr, L"init failed\n");
        return 1;
    }
    int rc = app.Run();
    onekey::log::Shutdown();
    return rc;
}
