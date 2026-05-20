#include "Application.h"
#include "Autostart.h"
#include "SelfRestart.h"
#include "../asr/AsrFactory.h"
#include "../polish/PolisherFactory.h"
#include "../log/Logger.h"
#include "../util/Strings.h"
#include "../util/WinHelpers.h"

#include <spdlog/spdlog.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>

#include <chrono>
#include <filesystem>

namespace onekey::app {

namespace {
// Custom WM_APP message used by the watcher thread to ask the main thread
// to do the actual reload/restart on its own thread.
constexpr UINT WM_APP_CONFIG_CHANGED = WM_APP + 50;

// Tracks whether the most recent file write was our own SetPolishMode call,
// so the watcher can ignore it. Crude but adequate: we set it just before
// the atomic rename and clear it after a short window.
std::chrono::steady_clock::time_point g_self_write_deadline{};
}  // namespace

Application::Application() = default;
Application::~Application() = default;

bool Application::Init() {
    main_tid_ = ::GetCurrentThreadId();

    try {
        cfg_ = config::Load();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[FATAL] config load failed: %s\n", e.what());
        return false;
    }

    std::wstring log_dir = util::ExeDir() + L"/logs";
    log::Init(log_dir);
    spdlog::info("==== onekey-core start ====");
    spdlog::info("config loaded from {}", config::LastLoadedPath().string());
    spdlog::info("asr={}/{}  polish={}/{} mode={}  inject={}  hotkey={}",
                 cfg_.asr.provider, cfg_.asr.language,
                 cfg_.polish.provider, cfg_.polish.provider_options.value("deployment", ""), cfg_.polish.mode,
                 cfg_.inject.mode, cfg_.hotkey.key);

    try {
        bus_      = std::make_unique<session::EventBus>();
        capture_  = std::make_unique<audio::WasapiCapture>();
        asr_      = asr::CreateAsrEngine(cfg_.asr);
        polisher_ = polish::CreatePolisher(cfg_.polish);
        injector_ = std::make_unique<inject::InjectorStrategy>(cfg_.inject);
        session_  = std::make_unique<session::DictationSession>(
            cfg_, asr_.get(), polisher_.get(), injector_.get(), capture_.get(),
            bus_.get());

        hotkey_ = std::make_unique<hotkey::HotkeyManager>();
        hotkey_->on_press   = [this]{
            if (paused_.load()) return;
            session_->StartRecording();
        };
        hotkey_->on_release = [this]{
            if (paused_.load()) return;
            session_->StopAndProcess();
        };
        if (!hotkey_->Install(cfg_.hotkey.key, cfg_.hotkey.min_hold_ms)) {
            spdlog::error("hotkey install failed");
            return false;
        }

        // UI — tray and overlay live on the main thread alongside the hotkey hook.
        tray_ = std::make_unique<ui::TrayIcon>();
        tray_->on_quit          = [this]{ OnQuit(); };
        tray_->on_open_settings = [this]{ OnOpenSettings(); };
        tray_->on_open_logs     = [this]{ OnOpenLogs(); };
        tray_->on_toggle_pause  = [this]{ OnTogglePause(); };
        tray_->get_paused       = [this]{ return paused_.load(); };
        tray_->get_polish_mode    = [this]{ return cfg_.polish.mode; };
        tray_->on_set_polish_mode = [this](const std::string& m){ OnSetPolishMode(m); };
        if (!tray_->Create()) {
            spdlog::warn("tray icon failed to create — continuing without it");
        }

        overlay_ = std::make_unique<ui::OverlayWindow>();
        if (overlay_->Create()) {
            overlay_->Attach(bus_.get());
        } else {
            spdlog::warn("overlay window failed — continuing without it");
        }

        // Tray tooltip + icon mirror session phase.
        bus_->Subscribe([this, last = std::make_shared<session::Phase>(session::Phase::Idle)]
                        (const session::PhaseEvent& e) mutable {
            if (!tray_) return;
            if (e.phase == *last) return;
            *last = e.phase;
            std::wstring tip = L"One-Key Input — ";
            tip += util::Utf8ToWide(session::PhaseName(e.phase));
            if (paused_.load()) tip = L"One-Key Input — paused";
            tray_->SetTooltip(tip);
            tray_->SetState(e.phase, paused_.load());
        });

        // Sound cues — start chirp on entry to Recording, stop chirp on entry
        // to Recognizing, error tone on entry to Error. Streaming ASR republishes
        // Phase::Recording on every partial, so we de-dupe by tracking last phase.
        sound_ = std::make_unique<audio::SoundCues>();
        sound_->SetEnabled(cfg_.sound.enabled);
        sound_->Prewarm();
        bus_->Subscribe([this, last = std::make_shared<session::Phase>(session::Phase::Idle)]
                        (const session::PhaseEvent& e) mutable {
            if (e.phase == *last) return;
            *last = e.phase;
            switch (e.phase) {
                case session::Phase::Recording:   sound_->PlayStart(); break;
                case session::Phase::Recognizing: sound_->PlayStop();  break;
                case session::Phase::Error:       sound_->PlayError(); break;
                default: break;
            }
        });

        // Toast on entry to Error so the user notices something failed
        // (otherwise they only hear a chirp + see "Error" in the tooltip
        // they don't read). Throttled — at most one toast per 10s — so
        // a flurry of failures doesn't carpet-bomb notifications.
        bus_->Subscribe([this,
                         last = std::make_shared<std::chrono::steady_clock::time_point>()]
                        (const session::PhaseEvent& e) mutable {
            if (!tray_) return;
            if (e.phase != session::Phase::Error) return;
            auto now = std::chrono::steady_clock::now();
            if (now - *last < std::chrono::seconds(10)) return;
            *last = now;
            std::wstring body = e.detail.empty()
                ? L"One-Key Input hit an error. Click to open Settings."
                : e.detail + L" — click to open Settings.";
            tray_->ShowToast(L"One-Key Input", body);
        });

        // Watch config.json — when the settings UI (or user) writes it, we
        // self-restart so all subsystems pick up the fresh values uniformly.
        watcher_ = std::make_unique<ConfigWatcher>();
        watcher_->Start(config::LastLoadedPath(), [this]{ OnConfigChanged(); });

        // Reconcile the HKCU\Run autostart entry with the user's preference
        // every launch. Cheap and idempotent — covers the case where the
        // user toggled the setting between sessions.
        SyncAutostart(cfg_.autostart.enabled);
    } catch (const std::exception& e) {
        spdlog::error("[FATAL] init failed: {}", e.what());
        return false;
    }

    spdlog::info("ready. press [{}] to talk. Right-click tray for menu. Ctrl+C to quit.",
                 cfg_.hotkey.key);
    return true;
}

int Application::Run() {
    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_APP_CONFIG_CHANGED) {
            // Reload triggered by file watcher. Most fields can only be
            // applied at startup (hotkey hook, ASR engine, etc.), so we
            // self-restart for a clean rebuild.
            spdlog::info("[app] config changed on disk — restarting");
            if (!SelfRestart(main_tid_)) {
                spdlog::error("[app] self-restart failed; staying on old config");
            }
            continue;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    spdlog::info("==== onekey-core shutdown ====");
    // Stop watcher before tearing down callbacks, then UI.
    if (watcher_) watcher_->Stop();
    overlay_.reset();
    tray_.reset();
    return static_cast<int>(msg.wParam);
}

void Application::OnQuit() {
    spdlog::info("[app] quit requested from tray");
    ::PostThreadMessageW(main_tid_, WM_QUIT, 0, 0);
}

void Application::OnOpenSettings() {
    spdlog::info("[app] opening settings UI");
    if (!LaunchSettings()) {
        // Fall back to opening config.json in notepad so the user can still
        // edit something.
        auto path = config::LastLoadedPath();
        if (!path.empty()) {
            spdlog::warn("[app] settings exe not found — falling back to notepad");
            ::ShellExecuteW(nullptr, L"open", L"notepad.exe",
                            path.wstring().c_str(), nullptr, SW_SHOWNORMAL);
        }
    }
}

void Application::OnOpenLogs() {
    std::wstring log_dir = util::ExeDir() + L"/logs";
    spdlog::info("[app] opening log dir: {}", util::WideToUtf8(log_dir));
    ::ShellExecuteW(nullptr, L"open", log_dir.c_str(),
                    nullptr, nullptr, SW_SHOWNORMAL);
}

void Application::OnTogglePause() {
    bool was = paused_.exchange(!paused_.load());
    spdlog::info("[app] hotkey {} -> {}", was ? "paused" : "active",
                                          was ? "active" : "paused");
    if (tray_) {
        tray_->SetTooltip(paused_.load() ? L"One-Key Input — paused"
                                         : L"One-Key Input — idle");
        tray_->SetState(session::Phase::Idle, paused_.load());
    }
}

void Application::OnSetPolishMode(const std::string& mode) {
    if (mode != "raw" && mode != "tidy" && mode != "formal") {
        spdlog::warn("[app] unknown polish mode requested: {}", mode);
        return;
    }
    spdlog::info("[app] polish mode {} -> {}", cfg_.polish.mode, mode);
    cfg_.polish.mode = mode;
    // Suppress the watcher's reaction to our own write (debounced window).
    g_self_write_deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(1500);
    if (!config::SetPolishMode(mode)) {
        spdlog::warn("[app] polish mode change applied in memory only "
                     "(write to config.json failed)");
    }
}

void Application::OnConfigChanged() {
    // Called from the watcher thread. Ignore self-writes (polish-mode tray click).
    if (std::chrono::steady_clock::now() < g_self_write_deadline) {
        spdlog::info("[watcher] ignored self-write");
        return;
    }
    spdlog::info("[watcher] config.json changed — scheduling restart");
    ::PostThreadMessageW(main_tid_, WM_APP_CONFIG_CHANGED, 0, 0);
}

}  // namespace onekey::app
