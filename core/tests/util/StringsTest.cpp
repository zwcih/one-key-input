#include <gtest/gtest.h>

#include "util/Strings.h"

#include <string>

using onekey::util::Utf8ToWide;
using onekey::util::WideToUtf8;

TEST(Strings, EmptyRoundTrips) {
    EXPECT_TRUE(Utf8ToWide("").empty());
    EXPECT_TRUE(WideToUtf8(L"").empty());
}

TEST(Strings, AsciiRoundTrip) {
    const std::string  s8 = "hello, world!";
    const std::wstring s16 = L"hello, world!";
    EXPECT_EQ(Utf8ToWide(s8), s16);
    EXPECT_EQ(WideToUtf8(s16), s8);
}

TEST(Strings, CjkRoundTrip) {
    // "中文测试" — BMP code points each encoding to 3 UTF-8 bytes.
    const std::string  s8 = "\xE4\xB8\xAD\xE6\x96\x87\xE6\xB5\x8B\xE8\xAF\x95";
    const std::wstring s16 = L"\u4E2D\u6587\u6D4B\u8BD5";
    EXPECT_EQ(Utf8ToWide(s8), s16);
    EXPECT_EQ(WideToUtf8(s16), s8);
}

TEST(Strings, EmojiSurrogatePairRoundTrip) {
    // U+1F511 KEY emoji — encoded as 4 UTF-8 bytes and as a UTF-16
    // surrogate pair (D83D DD11).
    const std::string  s8 = "\xF0\x9F\x94\x91";
    const std::wstring s16 = L"\xD83D\xDD11";
    EXPECT_EQ(Utf8ToWide(s8), s16);
    EXPECT_EQ(WideToUtf8(s16), s8);
}

TEST(Strings, EmbeddedNulPreserved) {
    std::string  s8(1, '\0');
    s8 += "after";
    std::wstring s16(1, L'\0');
    s16 += L"after";
    EXPECT_EQ(Utf8ToWide(s8), s16);
    EXPECT_EQ(WideToUtf8(s16), s8);
    EXPECT_EQ(Utf8ToWide(s8).size(), s16.size());
}

TEST(Strings, InvalidUtf8DoesNotCrash) {
    // Lone continuation byte — not a valid UTF-8 start.
    // MultiByteToWideChar without MB_ERR_INVALID_CHARS will substitute
    // U+FFFD; we just check the call returns without crashing.
    std::string bad = "\xC0\xC0";
    auto w = Utf8ToWide(bad);
    EXPECT_NO_FATAL_FAILURE((void)w);
}

TEST(Strings, MixedAsciiAndCjk) {
    const std::string  s8 = "hi \xE4\xB8\xAD!";
    const std::wstring s16 = L"hi \u4E2D!";
    EXPECT_EQ(Utf8ToWide(s8), s16);
    EXPECT_EQ(WideToUtf8(s16), s8);
}
