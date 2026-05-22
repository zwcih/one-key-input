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

TEST(AsrFactory, SherpaParaformerMissingModelDirThrows) {
    AsrConfig cfg;
    cfg.provider = "sherpa-paraformer";
    // provider_options empty: model_dir missing should fail fast at ctor.
    EXPECT_THROW({ (void)CreateAsrEngine(cfg); }, std::runtime_error);
}

TEST(AsrFactory, SherpaParaformerNonexistentDirThrows) {
    AsrConfig cfg;
    cfg.provider = "sherpa-paraformer";
    cfg.provider_options = {
        {"model_dir", "C:/this/path/should/never/exist/onekey-sherpa-test"},
    };
    EXPECT_THROW({ (void)CreateAsrEngine(cfg); }, std::runtime_error);
}

TEST(AsrFactory, UnknownProviderThrows) {
    AsrConfig cfg;
    cfg.provider = "wat-no-such";
    EXPECT_THROW({ (void)CreateAsrEngine(cfg); }, std::runtime_error);
}
