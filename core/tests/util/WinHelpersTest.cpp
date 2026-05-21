#include <gtest/gtest.h>

#include "util/WinHelpers.h"

#include <regex>
#include <string>

using onekey::util::ExeDir;
using onekey::util::LocalTimestampForFilename;

TEST(WinHelpers, ExeDirIsNonEmpty) {
    auto dir = ExeDir();
    EXPECT_FALSE(dir.empty());
}

TEST(WinHelpers, LocalTimestampMatchesYYYYMMDD) {
    // Current implementation returns YYYYMMDD. If the implementation is
    // ever extended to include the time component (the header doc-comment
    // mentions YYYY-MM-DDTHH:MM:SS.mmm), update this regex accordingly.
    std::wstring ts = LocalTimestampForFilename();
    ASSERT_FALSE(ts.empty());
    std::wregex shape(L"^\\d{8}$");
    EXPECT_TRUE(std::regex_match(ts, shape))
        << "actual timestamp length=" << ts.size();
}

TEST(WinHelpers, LocalTimestampPlausibleYear) {
    auto ts = LocalTimestampForFilename();
    ASSERT_GE(ts.size(), 4u);
    int year = std::stoi(std::wstring(ts.substr(0, 4)));
    EXPECT_GE(year, 2024);
    EXPECT_LE(year, 2100);
}
