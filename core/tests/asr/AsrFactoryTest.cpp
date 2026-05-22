#include <gtest/gtest.h>

#include "asr/AsrFactory.h"
#include "config/Config.h"

#include <stdexcept>

using namespace onekey::asr;
using onekey::config::AsrConfig;

TEST(AsrFactory, AzureRestNeedsKeyAndRegion) {
    AsrConfig cfg;
    cfg.provider = "azure-rest";
    EXPECT_THROW({ (void)CreateAsrEngine(cfg); }, std::runtime_error);
}

TEST(AsrFactory, AzureRestWithCredsConstructs) {
    AsrConfig cfg;
    cfg.provider = "azure-rest";
    cfg.provider_options = {{"key", "fake"}, {"region", "westus2"}};
    auto e = CreateAsrEngine(cfg);
    ASSERT_NE(e, nullptr);
    auto caps = e->capabilities();
    EXPECT_FALSE(caps.is_streaming);
}

TEST(AsrFactory, WindowsLocalNotImplemented) {
    AsrConfig cfg;
    cfg.provider = "windows-local";
    EXPECT_THROW({ (void)CreateAsrEngine(cfg); }, std::runtime_error);
}

TEST(AsrFactory, WhisperLocalNotImplemented) {
    AsrConfig cfg;
    cfg.provider = "whisper-local";
    EXPECT_THROW({ (void)CreateAsrEngine(cfg); }, std::runtime_error);
}

TEST(AsrFactory, SherpaOnnxNotImplemented) {
    // Privacy-mode roadmap: sherpa-onnx is a recognized provider name
    // but the engine is not wired up yet. Selecting it must surface a
    // clear "not implemented yet" error instead of "unknown provider".
    AsrConfig cfg;
    cfg.provider = "sherpa-onnx";
    EXPECT_THROW({ (void)CreateAsrEngine(cfg); }, std::runtime_error);
}

TEST(AsrFactory, UnknownProviderThrows) {
    AsrConfig cfg;
    cfg.provider = "wat-no-such";
    EXPECT_THROW({ (void)CreateAsrEngine(cfg); }, std::runtime_error);
}
