#include "Config.h"
#include "../util/WinHelpers.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <stdexcept>
#include <sstream>

namespace onekey::config {

namespace {

std::filesystem::path s_last_loaded;

std::string ReadFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

template <typename T>
void GetOpt(const nlohmann::json& j, const char* key, T& out) {
    if (j.contains(key) && !j[key].is_null()) out = j[key].get<T>();
}

// Migrate legacy keys to the new shape: provider-specific settings into
// provider_options. We mutate `dst.provider_options` in place.
void MigrateLegacyAsr(const nlohmann::json& src, AsrConfig& dst) {
    if (!src.contains("speech")) return;
    const auto& s = src["speech"];
    if (s.contains("key"))      dst.provider_options["key"]      = s["key"];
    if (s.contains("region"))   dst.provider_options["region"]   = s["region"];
    if (s.contains("endpoint")) dst.provider_options["endpoint"] = s["endpoint"];
    if (s.contains("language")) dst.language                     = s["language"].get<std::string>();
}

void MigrateLegacyPolish(const nlohmann::json& src, PolishConfig& dst) {
    if (!src.contains("openai")) return;
    const auto& o = src["openai"];
    if (o.contains("endpoint"))    dst.provider_options["endpoint"]    = o["endpoint"];
    if (o.contains("key"))         dst.provider_options["key"]         = o["key"];
    if (o.contains("deployment"))  dst.provider_options["deployment"]  = o["deployment"];
    if (o.contains("api_version")) dst.provider_options["api_version"] = o["api_version"];
}

AppConfig Parse(const std::string& body) {
    auto j = nlohmann::json::parse(body);
    AppConfig c;

    // --- Legacy MVP fields first; explicit asr/polish blocks below override ---
    MigrateLegacyAsr(j, c.asr);
    MigrateLegacyPolish(j, c.polish);
    GetOpt(j, "polish_mode", c.polish.mode);
    if (j.contains("inject_mode")) {
        c.inject.mode = j["inject_mode"].get<std::string>();
    }
    if (j.contains("hotkey") && j["hotkey"].is_string()) {
        c.hotkey.key = j["hotkey"].get<std::string>();
    }

    // --- New explicit schema ---
    if (j.contains("asr") && j["asr"].is_object()) {
        const auto& a = j["asr"];
        GetOpt(a, "provider",    c.asr.provider);
        GetOpt(a, "language",    c.asr.language);
        GetOpt(a, "punctuation", c.asr.punctuation);
        if (a.contains("provider_options") && a["provider_options"].is_object()) {
            // Merge: explicit options win over migrated legacy values.
            c.asr.provider_options.merge_patch(a["provider_options"]);
        }
    }
    if (j.contains("polish") && j["polish"].is_object()) {
        const auto& p = j["polish"];
        GetOpt(p, "provider",    c.polish.provider);
        GetOpt(p, "mode",        c.polish.mode);
        GetOpt(p, "max_tokens",  c.polish.max_tokens);
        GetOpt(p, "use_context", c.polish.use_context);
        if (p.contains("provider_options") && p["provider_options"].is_object()) {
            c.polish.provider_options.merge_patch(p["provider_options"]);
        }
    }
    if (j.contains("inject") && j["inject"].is_object()) {
        GetOpt(j["inject"], "mode", c.inject.mode);
    }
    if (j.contains("hotkey") && j["hotkey"].is_object()) {
        const auto& h = j["hotkey"];
        GetOpt(h, "key",                c.hotkey.key);
        GetOpt(h, "min_hold_ms",        c.hotkey.min_hold_ms);
        GetOpt(h, "behavior",           c.hotkey.behavior);
        GetOpt(h, "smart_threshold_ms", c.hotkey.smart_threshold_ms);
        GetOpt(h, "max_duration_ms",    c.hotkey.max_duration_ms);
        // Normalize: any unknown behavior string falls back to push-to-talk
        // so a typo in config.json can't silently brick the hotkey.
        if (c.hotkey.behavior != "push_to_talk" &&
            c.hotkey.behavior != "toggle" &&
            c.hotkey.behavior != "smart") {
            spdlog::warn("[config] unknown hotkey.behavior '{}' — falling back to push_to_talk",
                         c.hotkey.behavior);
            c.hotkey.behavior = "push_to_talk";
        }
    }
    if (j.contains("sound") && j["sound"].is_object()) {
        GetOpt(j["sound"], "enabled", c.sound.enabled);
    }
    if (j.contains("autostart") && j["autostart"].is_object()) {
        GetOpt(j["autostart"], "enabled", c.autostart.enabled);
    }
    if (j.contains("translate") && j["translate"].is_object()) {
        const auto& t = j["translate"];
        GetOpt(t, "enabled",         c.translate.enabled);
        GetOpt(t, "hotkey",          c.translate.hotkey);
        GetOpt(t, "min_hold_ms",     c.translate.min_hold_ms);
        GetOpt(t, "target_language", c.translate.target_language);
        GetOpt(t, "smart_target",    c.translate.smart_target);
    }

    return c;
}

}  // namespace

AppConfig Load(const std::filesystem::path& override_path) {
    std::filesystem::path exe_dir(util::ExeDir());

    std::vector<std::filesystem::path> candidates;
    if (!override_path.empty()) candidates.push_back(override_path);
    candidates.push_back(exe_dir / L"config.json");
    candidates.push_back(exe_dir / L".." / L"config.json");
    // Running from build/default/bin/ during development:
    candidates.push_back(exe_dir / L".." / L".." / L".." / L"config.json");

    for (const auto& p : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) continue;
        s_last_loaded = std::filesystem::canonical(p, ec);
        if (ec) s_last_loaded = p;
        return Parse(ReadFile(p));
    }

    std::string searched;
    for (const auto& p : candidates) { searched += "\n  - " + p.string(); }
    throw std::runtime_error("no config.json found. Searched:" + searched +
                             "\nCopy config.example.json to config.json and fill in your keys.");
}

std::filesystem::path LastLoadedPath() { return s_last_loaded; }

namespace {
bool IsEmptyOrPlaceholder(const std::string& v) {
    if (v.empty()) return true;
    // Placeholder strings shipped in config.example.json all start with YOUR_.
    return v.rfind("YOUR_", 0) == 0;
}
}  // namespace

bool IsFirstRun(const AppConfig& cfg) {
    auto get = [](const nlohmann::json& o, const char* k) -> std::string {
        if (!o.contains(k) || !o[k].is_string()) return {};
        return o[k].get<std::string>();
    };
    // ASR: any provider that talks to Azure needs a key.
    if (cfg.asr.provider.rfind("azure", 0) == 0) {
        if (IsEmptyOrPlaceholder(get(cfg.asr.provider_options, "key"))) return true;
    }
    // Polish: openai-azure / openai both need a key.
    if (cfg.polish.provider.rfind("openai", 0) == 0) {
        if (IsEmptyOrPlaceholder(get(cfg.polish.provider_options, "key"))) return true;
    }
    return false;
}

bool SetPolishMode(const std::string& mode) {
    if (s_last_loaded.empty()) {
        spdlog::error("[config] SetPolishMode: no loaded path yet");
        return false;
    }
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(ReadFile(s_last_loaded));
    } catch (const std::exception& e) {
        spdlog::error("[config] SetPolishMode parse failed: {}", e.what());
        return false;
    }

    // Write the value into both the new schema location and the legacy top-level
    // key, so a file produced by either tool stays self-consistent.
    if (!j.contains("polish") || !j["polish"].is_object()) {
        j["polish"] = nlohmann::json::object();
    }
    j["polish"]["mode"] = mode;
    if (j.contains("polish_mode")) {
        j["polish_mode"] = mode;
    }

    // Atomic write: dump to tmp file in the same directory, then rename.
    auto tmp = s_last_loaded;
    tmp += ".tmp";
    try {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("open tmp");
        f << j.dump(2);
        f.close();
        std::error_code ec;
        std::filesystem::rename(tmp, s_last_loaded, ec);
        if (ec) {
            // rename may fail across the same volume in rare cases; try copy+remove.
            std::filesystem::copy_file(tmp, s_last_loaded,
                std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::remove(tmp);
            if (ec) throw std::runtime_error("rename: " + ec.message());
        }
    } catch (const std::exception& e) {
        spdlog::error("[config] SetPolishMode write failed: {}", e.what());
        return false;
    }
    spdlog::info("[config] wrote polish.mode={} to {}", mode, s_last_loaded.string());
    return true;
}

}  // namespace onekey::config
