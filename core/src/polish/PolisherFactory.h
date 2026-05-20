#pragma once
#include "IPolisher.h"
#include "../config/Config.h"
#include <memory>

namespace onekey::polish {

std::unique_ptr<IPolisher> CreatePolisher(const config::PolishConfig& cfg);

}  // namespace onekey::polish
