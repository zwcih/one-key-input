#pragma once
#include <string>
#include <string_view>

namespace onekey::util {

std::wstring Utf8ToWide(std::string_view utf8);
std::string  WideToUtf8(std::wstring_view wide);

}  // namespace onekey::util
