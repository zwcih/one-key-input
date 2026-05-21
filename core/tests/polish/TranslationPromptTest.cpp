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
    EXPECT_NE(sys.find("[TARGET LANGUAGE]"), std::string::npos);
    EXPECT_NE(sys.find("(en)"), std::string::npos);
}

TEST(TranslationPrompt, BuildSystemPromptOmitsEmptyContextBlocks) {
    TranslationPromptInput in;
    in.target_language = "ja";
    auto sys = BuildSystemPrompt(in, "tidy");
    // No app/scene/nearby/typed/vocab populated -> no [APP] [SCENE] etc.
    EXPECT_EQ(sys.find("[APP]"),    std::string::npos);
    EXPECT_EQ(sys.find("[SCENE]"),  std::string::npos);
    EXPECT_EQ(sys.find("[NEARBY]"), std::string::npos);
    EXPECT_EQ(sys.find("[TYPED]"),  std::string::npos);
    EXPECT_EQ(sys.find("[KEEP VERBATIM]"), std::string::npos);
    EXPECT_NE(sys.find("Japanese"), std::string::npos);
}

TEST(TranslationPrompt, BuildSystemPromptIncludesKeepVerbatimList) {
    TranslationPromptInput in;
    in.target_language = "en";
    in.vocab_hints = { L"parseConfig", L"OneKeyInput", L"F9" };
    auto sys = BuildSystemPrompt(in, "tidy");
    auto pos = sys.find("[KEEP VERBATIM]");
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
    EXPECT_NE(sys.find("PEER LANGUAGE"), std::string::npos);
    EXPECT_NE(sys.find("ja"), std::string::npos);
}

TEST(TranslationPrompt, BuildUserMessageContainsTranscriptAndRequest) {
    TranslationPromptInput in;
    in.target_language = "en";
    in.raw_transcript = L"你好，世界";
    auto user = BuildUserMessage(in);
    EXPECT_NE(user.find("[ORIGINAL]"), std::string::npos);
    EXPECT_NE(user.find("[REQUEST]"),  std::string::npos);
    EXPECT_NE(user.find("English"),    std::string::npos);
    // The transcript must round-trip through UTF-8.
    EXPECT_NE(user.find("\xe4\xbd\xa0"), std::string::npos);   // 你
}
