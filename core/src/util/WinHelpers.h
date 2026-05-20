#pragma once
#include <string>

namespace onekey::util {

// Resolve current executable directory.
std::wstring ExeDir();

// Returns YYYY-MM-DDTHH:MM:SS.mmm in local time, for log filenames etc.
std::wstring LocalTimestampForFilename();

}  // namespace onekey::util
