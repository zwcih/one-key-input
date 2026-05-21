#include <gtest/gtest.h>

#include "polish/TranslationPrompt.h"

#include <string>

using namespace onekey::polish;

TEST(TranslationPrompt, NeedsTranslationDifferentPrimaryTags) {
    EXPECT_TRUE(NeedsTranslation("zh-CN", "en"));
    EXPECT_TRUE(NeedsTranslation("en-US", "zh"));
    EXPECT_TRUE(NeedsTranslation("ja-JP", "en"));
}

TEST(TranslationPrompt, NeedsTranslationSamePrimaryTagSkips) {
    // Same primary subtag — even with different region — means no
    // translation needed (transcript already in target language).
    EXPECT_FALSE(NeedsTranslation("zh-CN", "zh"));
    EXPECT_FALSE(NeedsTranslation("en-US", "en"));
    EXPECT_FALSE(NeedsTranslation("EN_GB", "en")); // case + underscore tolerant
}

TEST(TranslationPrompt, NeedsTranslationEmptyTargetIsFalse) {
    // No target ⇒ feature effectively off.
    EXPECT_FALSE(NeedsTranslation("zh-CN", ""));
}

TEST(TranslationPrompt, NeedsTranslationUnknownSourceDefersToLLM) {
    // Empty source ⇒ we don't know, return true so the LLM gets a chance.
    EXPECT_TRUE(NeedsTranslation("", "en"));
}

TEST(TranslationPrompt, PrettyLanguageNameKnownTags) {
    EXPECT_EQ(PrettyLanguageName("en"),    "English");
    EXPECT_EQ(PrettyLanguageName("zh"),    "Chinese");
    EXPECT_EQ(PrettyLanguageName("ja"),    "Japanese");
    EXPECT_EQ(PrettyLanguageName("ko"),    "Korean");
    EXPECT_EQ(PrettyLanguageName("zh-CN"), "Chinese");  // primary subtag
}

TEST(TranslationPrompt, PrettyLanguageNameUnknownTagPassesThrough) {
    // Don't invent — return whatever the caller passed so the LLM at least
    // sees the raw tag.
    EXPECT_EQ(PrettyLanguageName("xx"), "xx");
}

TEST(TranslationPrompt, BuildSystemPromptContainsTargetLanguage) {
    TranslationPromptInput in;
    in.target_language = "en";
    auto sys = BuildSystemPrompt(in, "tidy");
    EXPECT_NE(sys.find("English"), std::string::npos);
    EXPECT_NE(sys.find("en"), std::string::npos);  // primary subtag
}

TEST(TranslationPrompt, BuildSystemPromptOmitsEmptyContextBlocks) {
    TranslationPromptInput in;
    in.target_language = "ja";
    auto sys = BuildSystemPrompt(in, "tidy");
    // No app/scene/nearby/typed/vocab populated -> none of those labels
    // should appear in the system prompt.
    EXPECT_EQ(sys.find("应用："),       std::string::npos);
    EXPECT_EQ(sys.find("场景："),       std::string::npos);
    EXPECT_EQ(sys.find("周围内容："),   std::string::npos);
    EXPECT_EQ(sys.find("已输入："),     std::string::npos);
    EXPECT_EQ(sys.find("保持原样的词："), std::string::npos);
    EXPECT_NE(sys.find("Japanese"), std::string::npos);
}

TEST(TranslationPrompt, BuildSystemPromptIncludesKeepVerbatimList) {
    TranslationPromptInput in;
    in.target_language = "en";
    in.vocab_hints = { L"parseConfig", L"OneKeyInput", L"F9" };
    auto sys = BuildSystemPrompt(in, "tidy");
    auto pos = sys.find("保持原样的词：");
    ASSERT_NE(pos, std::string::npos);
    // Each term should appear in the list.
    EXPECT_NE(sys.find("parseConfig"), std::string::npos);
    EXPECT_NE(sys.find("OneKeyInput"), std::string::npos);
    EXPECT_NE(sys.find("F9"),          std::string::npos);
}

TEST(TranslationPrompt, BuildSystemPromptStyleHintAdapts) {
    TranslationPromptInput in;
    in.target_language = "en";
    auto raw    = BuildSystemPrompt(in, "raw");
    auto tidy   = BuildSystemPrompt(in, "tidy");
    auto formal = BuildSystemPrompt(in, "formal");
    EXPECT_NE(raw.find("raw"),       std::string::npos);
    EXPECT_NE(tidy.find("tidy"),     std::string::npos);
    EXPECT_NE(formal.find("formal"), std::string::npos);
    EXPECT_NE(raw,    tidy);
    EXPECT_NE(tidy,   formal);
    EXPECT_NE(formal, raw);
}

TEST(TranslationPrompt, BuildSystemPromptIncludesPeerLanguageWhenSet) {
    TranslationPromptInput in;
    in.target_language = "ja";
    in.detected_peer_language = "ja";
    auto sys = BuildSystemPrompt(in, "tidy");
    EXPECT_NE(sys.find("对方语言"), std::string::npos);
    EXPECT_NE(sys.find("ja"), std::string::npos);
}

TEST(TranslationPrompt, BuildUserMessageContainsTranscript) {
    TranslationPromptInput in;
    in.target_language = "en";
    in.raw_transcript = L"你好，世界";
    auto user = BuildUserMessage(in);
    // The user message is now just the transcript — no [ORIGINAL]/[REQUEST]
    // wrapper, since dual-channel instructions tripped Azure's jailbreak
    // classifier. The transcript must round-trip through UTF-8.
    EXPECT_NE(user.find("\xe4\xbd\xa0"), std::string::npos);   // 你
    EXPECT_EQ(user.find("[ORIGINAL]"), std::string::npos);
    EXPECT_EQ(user.find("[REQUEST]"),  std::string::npos);
}
