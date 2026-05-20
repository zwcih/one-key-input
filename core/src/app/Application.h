#pragma once
#include "../config/Config.h"
#include "../asr/IAsrEngine.h"
#include "../polish/IPolisher.h"
#include "../inject/InjectorStrategy.h"
#include "../audio/WasapiCapture.h"
#include "../audio/SoundCues.h"
#include "../hotkey/HotkeyManager.h"
#include "../session/DictationSession.h"
#include "../session/EventBus.h"
#include "../ui/TrayIcon.h"
#include "../ui/OverlayWindow.h"
#include "ConfigWatcher.h"

#include <atomic>
#include <memory>

namespace onekey::app {

class Application {
public:
    Application();
    ~Application();

    bool Init();
    int  Run();      // runs the Win32 message pump; returns exit code

private:
    void OnEditConfig();
    void OnOpenSettings();
    void OnOpenLogs();
    void OnTogglePause();
    void OnQuit();
    void OnSetPolishMode(const std::string& mode);
    void OnConfigChanged();    // fired by ConfigWatcher (background thread)

    config::AppConfig                           cfg_;
    std::unique_ptr<session::EventBus>          bus_;
    std::unique_ptr<audio::WasapiCapture>       capture_;
    std::unique_ptr<audio::SoundCues>           sound_;
    std::unique_ptr<asr::IAsrEngine>            asr_;
    std::unique_ptr<polish::IPolisher>          polisher_;
    std::unique_ptr<inject::InjectorStrategy>   injector_;
    std::unique_ptr<session::DictationSession>  session_;
    std::unique_ptr<hotkey::HotkeyManager>      hotkey_;
    std::unique_ptr<ui::TrayIcon>               tray_;
    std::unique_ptr<ui::OverlayWindow>          overlay_;
    std::unique_ptr<ConfigWatcher>              watcher_;

    std::atomic<bool>                           paused_{false};
    DWORD                                       main_tid_ = 0;
};

}  // namespace onekey::app
