#include <gtest/gtest.h>

#include "polish/PolisherFactory.h"
#include "config/Config.h"

#include <stdexcept>

using namespace onekey::polish;
using onekey::config::PolishConfig;

TEST(PolisherFactory, OpenAiAzureConstructs) {
    PolishConfig cfg;
    cfg.provider = "openai-azure";
    cfg.provider_options = {
        {"endpoint", "https://r.openai.azure.com"},
        {"key", "fake"},
        {"deployment", "gpt-4o-mini"},
    };
    auto p = CreatePolisher(cfg);
    EXPECT_NE(p, nullptr);
}

TEST(PolisherFactory, OpenAiConstructs) {
    PolishConfig cfg;
    cfg.provider = "openai";
    cfg.provider_options = {{"key", "sk-fake"}, {"deployment", "gpt-4o"}};
    auto p = CreatePolisher(cfg);
    EXPECT_NE(p, nullptr);
}

TEST(PolisherFactory, OllamaConstructs) {
    PolishConfig cfg;
    cfg.provider = "ollama";
    cfg.provider_options = {
        {"endpoint", "http://localhost:11434/v1/chat/completions"},
        {"deployment", "llama3"},
    };
    auto p = CreatePolisher(cfg);
    EXPECT_NE(p, nullptr);
}

TEST(PolisherFactory, LlamacppNotImplemented) {
    PolishConfig cfg;
    cfg.provider = "llamacpp";
    EXPECT_THROW({ (void)CreatePolisher(cfg); }, std::runtime_error);
}

TEST(PolisherFactory, UnknownProviderThrows) {
    PolishConfig cfg;
    cfg.provider = "no-such-polish";
    EXPECT_THROW({ (void)CreatePolisher(cfg); }, std::runtime_error);
}
