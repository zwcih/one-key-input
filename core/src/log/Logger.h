#pragma once
#include <string>

namespace onekey::log {

void Init(const std::wstring& log_dir);
void Shutdown();

}  // namespace onekey::log
