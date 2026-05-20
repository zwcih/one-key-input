#pragma once
#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace onekey::config {

// Provider-agnostic settings the dictation pipeline understands.
// Anything provider-specific lives in `provider_options`, an opaque
// JSON object that each engine implementation parses itself. The session
// layer never reads provider_options.
struct AsrConfig {
    std::string    provider = "azure-rest";  // azure-rest | azure-stream | windows-local | whisper-local | ...
    std::string    language = "zh-CN";
    bool           punctuation = true;
    nlohmann::json provider_options = nlohmann::json::object();
};

struct PolishConfig {
    std::string    provider   = "openai-azure"; // openai-azure | openai | ollama | llamacpp | ...
    std::string    mode       = "tidy";          // raw | tidy | formal | code-comment | im-chat
    double         temperature = 0.2;
    int            max_tokens  = 2000;
    bool           use_context = true;           // append focus context block to system prompt
    nlohmann::json provider_options = nlohmann::json::object();
};

struct InjectConfig {
    std::string mode = "sendinput"; // sendinput | clipboard | auto
};

struct HotkeyConfig {
    std::string key         = "f9";
    int         min_hold_ms = 250;
};

struct SoundConfig {
    bool enabled = true;   // play chirps on record start / stop
};

struct AutostartConfig {
    bool enabled = true;   // launch onekey-core at user login
};

struct AppConfig {
    AsrConfig       asr;
    PolishConfig    polish;
    InjectConfig    inject;
    HotkeyConfig    hotkey;
    SoundConfig     sound;
    AutostartConfig autostart;
};

// Loads config. Search order:
//   1. override_path (CLI flag)
//   2. <exe_dir>/config.json
//   3. <exe_dir>/../../../../config.json   (repo root when running from build/default/bin)
//   4. <exe_dir>/../config.json            (release layout)
// Throws std::runtime_error on hard failure.
//
// Accepts both the new schema (asr.provider_options.{...}) and the legacy Python MVP schema
// (speech.{key,region,language}, openai.{endpoint,key,deployment,api_version}). Legacy keys
// are migrated into provider_options at load time so engines see one shape.
AppConfig Load(const std::filesystem::path& override_path = {});

// Persists a polish-mode change back to whichever file LastLoadedPath()
// reports. Preserves the rest of the file (including legacy keys). Returns
// false on I/O failure; logs the underlying error itself.
bool SetPolishMode(const std::string& mode);

// Path the most recent Load() used. For diagnostics.
std::filesystem::path LastLoadedPath();

// Returns true if the config has empty/placeholder credentials such that
// the app cannot actually run — i.e. user has never configured it.
// Checks Azure speech key + (depending on polish provider) the polish key
// for emptiness or the known YOUR_* placeholder values shipped in
// config.example.json.
bool IsFirstRun(const AppConfig& cfg);

}  // namespace onekey::config
