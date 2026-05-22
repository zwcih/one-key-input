#include <gtest/gtest.h>

#include "asr/SherpaAsrEngine.h"
#include "config/Config.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using onekey::asr::SherpaAsrEngine;
using onekey::config::AsrConfig;

namespace {

// Helper: create a model_dir scratch space with whichever files we want
// present. The engine ctor is path-only validation (no model load), so
// touching empty files is enough to exercise the discovery logic.
class TempDir {
public:
    TempDir() {
        auto base = std::filesystem::temp_directory_path()
                    / "onekey-sherpa-tests";
        std::filesystem::create_directories(base);
        // Unique per-test subdir; mt-safe enough for serial test runs.
        for (int i = 0; i < 100; ++i) {
            auto candidate = base / ("d" + std::to_string(i) + "-" +
                                     std::to_string(::GetCurrentProcessId()));
            if (std::filesystem::create_directory(candidate)) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("TempDir: exhausted candidates");
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    const std::filesystem::path& path() const { return path_; }
    void touch(const char* name) {
        std::ofstream f(path_ / name, std::ios::binary);
        f << "stub";
    }
private:
    std::filesystem::path path_;
};

}  // namespace

TEST(SherpaAsrEngine, MissingModelDirThrows) {
    AsrConfig cfg;
    cfg.provider = "sherpa-paraformer";
    // No provider_options at all.
    EXPECT_THROW({ SherpaAsrEngine engine(cfg); }, std::runtime_error);
}

TEST(SherpaAsrEngine, EmptyModelDirStringThrows) {
    AsrConfig cfg;
    cfg.provider = "sherpa-paraformer";
    cfg.provider_options = {{"model_dir", ""}};
    EXPECT_THROW({ SherpaAsrEngine engine(cfg); }, std::runtime_error);
}

TEST(SherpaAsrEngine, NonexistentModelDirThrows) {
    AsrConfig cfg;
    cfg.provider = "sherpa-paraformer";
    cfg.provider_options = {
        {"model_dir", "C:/__definitely_does_not_exist_onekey__/x"}
    };
    EXPECT_THROW({ SherpaAsrEngine engine(cfg); }, std::runtime_error);
}

TEST(SherpaAsrEngine, ModelDirMissingFilesReportsWhich) {
    TempDir td;
    // Only tokens.txt present — encoder/decoder missing.
    td.touch("tokens.txt");
    AsrConfig cfg;
    cfg.provider = "sherpa-paraformer";
    cfg.provider_options = {{"model_dir", td.path().string()}};
    try {
        SherpaAsrEngine engine(cfg);
        FAIL() << "expected runtime_error";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("encoder.onnx"), std::string::npos) << msg;
        EXPECT_NE(msg.find("decoder.onnx"), std::string::npos) << msg;
    }
}

TEST(SherpaAsrEngine, FullModelLayoutValidates) {
    TempDir td;
    td.touch("encoder.onnx");
    td.touch("decoder.onnx");
    td.touch("tokens.txt");
    AsrConfig cfg;
    cfg.provider = "sherpa-paraformer";
    cfg.provider_options = {
        {"model_dir", td.path().string()},
        {"num_threads", 4},
        {"provider", "cpu"},
    };
    // Ctor validates the model dir layout; it does NOT load the models. So
    // even with stub files the construction must succeed.
    EXPECT_NO_THROW({ SherpaAsrEngine engine(cfg); });
}

TEST(SherpaAsrEngine, Int8ModelNamesAccepted) {
    // Some users paste the raw int8 file names from upstream tarballs
    // without renaming. The engine tolerates that.
    TempDir td;
    td.touch("encoder.int8.onnx");
    td.touch("decoder.int8.onnx");
    td.touch("tokens.txt");
    AsrConfig cfg;
    cfg.provider = "sherpa-paraformer";
    cfg.provider_options = {{"model_dir", td.path().string()}};
    EXPECT_NO_THROW({ SherpaAsrEngine engine(cfg); });
}

TEST(SherpaAsrEngine, EnvVarExpansionInModelDir) {
    TempDir td;
    td.touch("encoder.onnx");
    td.touch("decoder.onnx");
    td.touch("tokens.txt");
    ::SetEnvironmentVariableW(L"ONEKEY_SHERPA_TEST_ROOT",
                              td.path().wstring().c_str());
    AsrConfig cfg;
    cfg.provider = "sherpa-paraformer";
    cfg.provider_options = {{"model_dir", "%ONEKEY_SHERPA_TEST_ROOT%"}};
    EXPECT_NO_THROW({ SherpaAsrEngine engine(cfg); });
    ::SetEnvironmentVariableW(L"ONEKEY_SHERPA_TEST_ROOT", nullptr);
}

// Default config ships `models\paraformer-zh-streaming` as a relative path
// that lives next to onekey-core.exe. It must resolve against the exe
// directory — NOT current_path() — because autostart and shortcut launches
// set unrelated CWDs. We can't drop fake model files next to the running
// test binary, so we just confirm the failure message references the exe
// directory rather than CWD when the relative dir doesn't exist there.
TEST(SherpaAsrEngine, RelativeModelDirAnchoredAtExeDir) {
    namespace fs = std::filesystem;
    wchar_t exe_buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameW(nullptr, exe_buf, MAX_PATH);
    ASSERT_GT(n, 0u);
    ASSERT_LT(n, static_cast<DWORD>(MAX_PATH));
    fs::path exe_dir = fs::path(std::wstring(exe_buf, n)).parent_path();

    AsrConfig cfg;
    cfg.provider = "sherpa-paraformer";
    cfg.provider_options = {{"model_dir", "__onekey_nope_rel_dir__"}};
    try {
        SherpaAsrEngine engine(cfg);
        FAIL() << "expected throw — dir does not exist";
    } catch (const std::exception& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find(exe_dir.string()), std::string::npos)
            << "error should reference exe-dir-anchored path; got: " << msg;
    }
}

TEST(SherpaAsrEngine, CapabilitiesAreStreaming) {
    TempDir td;
    td.touch("encoder.onnx");
    td.touch("decoder.onnx");
    td.touch("tokens.txt");
    AsrConfig cfg;
    cfg.provider = "sherpa-paraformer";
    cfg.provider_options = {{"model_dir", td.path().string()}};
    SherpaAsrEngine engine(cfg);
    auto caps = engine.capabilities();
    EXPECT_TRUE(caps.is_streaming);
    EXPECT_TRUE(caps.emits_partials);
    EXPECT_TRUE(caps.emits_segment_finals);
}
