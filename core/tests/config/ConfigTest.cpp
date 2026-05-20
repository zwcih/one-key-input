#include <gtest/gtest.h>

#include "config/Config.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using nlohmann::json;
using namespace onekey::config;

namespace {

// Writes `body` to a unique temp file and returns its path. The file is
// kept on disk for the duration of the test process; we don't try to clean
// up per-test because Load() canonicalizes the path and SetPolishMode may
// rewrite it, and any leakage is into the OS tmp dir.
fs::path WriteTempConfig(const std::string& body, const char* tag) {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    auto dir = fs::temp_directory_path() / "onekey-tests";
    fs::create_directories(dir);
    fs::path p = dir / (std::string("cfg-") + tag + "-" +
                        std::to_string(dist(rng)) + ".json");
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << body;
    f.close();
    return p;
}

}  // namespace

TEST(Config, LoadAllDefaultsFromEmptyJson) {
    auto p = WriteTempConfig("{}", "defaults");
    AppConfig c = Load(p);
    // Spot-check the documented defaults from Config.h.
    EXPECT_EQ(c.asr.provider, "azure-rest");
    EXPECT_EQ(c.asr.language, "zh-CN");
    EXPECT_TRUE(c.asr.punctuation);
    EXPECT_EQ(c.polish.provider, "openai-azure");
    EXPECT_EQ(c.polish.mode, "tidy");
    EXPECT_DOUBLE_EQ(c.polish.temperature, 0.2);
    EXPECT_EQ(c.polish.max_tokens, 2000);
    EXPECT_TRUE(c.polish.use_context);
    EXPECT_EQ(c.inject.mode, "sendinput");
    EXPECT_EQ(c.hotkey.key, "f9");
    EXPECT_EQ(c.hotkey.min_hold_ms, 250);
    EXPECT_TRUE(c.sound.enabled);
    EXPECT_TRUE(c.autostart.enabled);
}

TEST(Config, LoadNewSchema) {
    json j = {
        {"asr", {
            {"provider", "azure-stream"},
            {"language", "en-US"},
            {"punctuation", false},
            {"provider_options", {{"key", "abc"}, {"region", "eastus"}}},
        }},
        {"polish", {
            {"provider", "openai"},
            {"mode", "formal"},
            {"temperature", 0.7},
            {"max_tokens", 512},
            {"use_context", false},
            {"provider_options", {{"key", "sk-xyz"}, {"deployment", "gpt-4o"}}},
        }},
        {"inject", {{"mode", "clipboard"}}},
        {"hotkey", {{"key", "f12"}, {"min_hold_ms", 500}}},
        {"sound", {{"enabled", false}}},
        {"autostart", {{"enabled", false}}},
    };
    auto p = WriteTempConfig(j.dump(), "new-schema");
    AppConfig c = Load(p);
    EXPECT_EQ(c.asr.provider, "azure-stream");
    EXPECT_EQ(c.asr.language, "en-US");
    EXPECT_FALSE(c.asr.punctuation);
    EXPECT_EQ(c.asr.provider_options.value("key", ""), "abc");
    EXPECT_EQ(c.asr.provider_options.value("region", ""), "eastus");
    EXPECT_EQ(c.polish.provider, "openai");
    EXPECT_EQ(c.polish.mode, "formal");
    EXPECT_DOUBLE_EQ(c.polish.temperature, 0.7);
    EXPECT_EQ(c.polish.max_tokens, 512);
    EXPECT_FALSE(c.polish.use_context);
    EXPECT_EQ(c.polish.provider_options.value("key", ""), "sk-xyz");
    EXPECT_EQ(c.polish.provider_options.value("deployment", ""), "gpt-4o");
    EXPECT_EQ(c.inject.mode, "clipboard");
    EXPECT_EQ(c.hotkey.key, "f12");
    EXPECT_EQ(c.hotkey.min_hold_ms, 500);
    EXPECT_FALSE(c.sound.enabled);
    EXPECT_FALSE(c.autostart.enabled);
}

TEST(Config, LoadLegacyPythonMvpSchema) {
    // Legacy keys documented in Config.h: speech.{key,region,language},
    // openai.{endpoint,key,deployment,api_version}, polish_mode, inject_mode,
    // hotkey as top-level string.
    json j = {
        {"speech", {
            {"key", "legacy-key"},
            {"region", "westus2"},
            {"language", "ja-JP"},
        }},
        {"openai", {
            {"endpoint", "https://r.openai.azure.com"},
            {"key", "legacy-oai"},
            {"deployment", "gpt-4o-mini"},
            {"api_version", "2024-01-01-preview"},
        }},
        {"polish_mode", "raw"},
        {"inject_mode", "clipboard"},
        {"hotkey", "f8"},
    };
    auto p = WriteTempConfig(j.dump(), "legacy");
    AppConfig c = Load(p);
    EXPECT_EQ(c.asr.language, "ja-JP");
    EXPECT_EQ(c.asr.provider_options.value("key", ""), "legacy-key");
    EXPECT_EQ(c.asr.provider_options.value("region", ""), "westus2");
    EXPECT_EQ(c.polish.provider_options.value("endpoint", ""),
              "https://r.openai.azure.com");
    EXPECT_EQ(c.polish.provider_options.value("key", ""), "legacy-oai");
    EXPECT_EQ(c.polish.provider_options.value("deployment", ""), "gpt-4o-mini");
    EXPECT_EQ(c.polish.provider_options.value("api_version", ""),
              "2024-01-01-preview");
    EXPECT_EQ(c.polish.mode, "raw");
    EXPECT_EQ(c.inject.mode, "clipboard");
    EXPECT_EQ(c.hotkey.key, "f8");
}

TEST(Config, NewSchemaProviderOptionsOverrideLegacy) {
    // When both legacy + new schema fields are present, new schema wins
    // (it's merged on top via merge_patch).
    json j = {
        {"speech", {{"key", "legacy"}, {"region", "old"}}},
        {"asr", {
            {"provider", "azure-rest"},
            {"provider_options", {{"key", "new"}}},
        }},
    };
    auto p = WriteTempConfig(j.dump(), "override");
    AppConfig c = Load(p);
    EXPECT_EQ(c.asr.provider_options.value("key", ""), "new");
    // Region only set in legacy block, so it's preserved.
    EXPECT_EQ(c.asr.provider_options.value("region", ""), "old");
}

TEST(Config, IsFirstRunTrueWhenAzureKeyMissing) {
    AppConfig c;
    c.asr.provider = "azure-rest";
    // Empty key -> placeholder
    EXPECT_TRUE(IsFirstRun(c));
}

TEST(Config, IsFirstRunTrueWhenYourPlaceholder) {
    AppConfig c;
    c.asr.provider = "azure-stream";
    c.asr.provider_options["key"] = "YOUR_AZURE_SPEECH_KEY";
    c.polish.provider = "openai";
    c.polish.provider_options["key"] = "real-openai-key";
    EXPECT_TRUE(IsFirstRun(c));
}

TEST(Config, IsFirstRunTrueWhenPolishKeyMissing) {
    AppConfig c;
    c.asr.provider = "azure-rest";
    c.asr.provider_options["key"] = "valid-azure";
    c.polish.provider = "openai-azure";
    c.polish.provider_options["key"] = "YOUR_OPENAI_KEY";
    EXPECT_TRUE(IsFirstRun(c));
}

TEST(Config, IsFirstRunFalseWhenAllKeysFilled) {
    AppConfig c;
    c.asr.provider = "azure-rest";
    c.asr.provider_options["key"] = "valid-azure";
    c.polish.provider = "openai-azure";
    c.polish.provider_options["key"] = "valid-oai";
    EXPECT_FALSE(IsFirstRun(c));
}

TEST(Config, IsFirstRunFalseForNonAzureNonOpenaiProviders) {
    AppConfig c;
    c.asr.provider = "windows-local";
    c.polish.provider = "ollama";  // doesn't start with "openai"
    EXPECT_FALSE(IsFirstRun(c));
}

TEST(Config, MalformedJsonThrows) {
    auto p = WriteTempConfig("{this is not json}", "bad");
    EXPECT_THROW({ (void)Load(p); }, std::exception);
}

TEST(Config, MissingFileThrows) {
    fs::path p = fs::temp_directory_path() / "onekey-tests" /
                 "definitely-does-not-exist-12345.json";
    fs::remove(p);
    EXPECT_THROW({ (void)Load(p); }, std::runtime_error);
}

TEST(Config, SetPolishModeWritesAndPreservesUnknownKeys) {
    json j = {
        {"polish", {{"mode", "tidy"}}},
        {"custom_user_key", "must-survive"},
        {"polish_mode", "tidy"},  // legacy duplicate
    };
    auto p = WriteTempConfig(j.dump(), "setmode");
    AppConfig c = Load(p);  // sets s_last_loaded
    (void)c;

    ASSERT_TRUE(SetPolishMode("formal"));

    // Read back from disk.
    std::ifstream f(p, std::ios::binary);
    json out = json::parse(f);
    EXPECT_EQ(out["polish"]["mode"], "formal");
    EXPECT_EQ(out["polish_mode"], "formal");  // legacy duplicated
    EXPECT_EQ(out["custom_user_key"], "must-survive");
}

TEST(Config, LastLoadedPathReflectsRecentLoad) {
    auto p = WriteTempConfig("{}", "lastpath");
    (void)Load(p);
    auto last = LastLoadedPath();
    EXPECT_FALSE(last.empty());
    // Should be canonical-equal to p (handle slash normalization).
    std::error_code ec;
    auto canon = fs::canonical(p, ec);
    if (!ec) EXPECT_EQ(last, canon);
}
