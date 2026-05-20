#pragma once
#include "IAsrEngine.h"
#include "../config/Config.h"
#include <memory>

namespace onekey::asr {

// Build an engine for the given config. Throws std::runtime_error if provider unknown
// or unimplemented (e.g. azure-stream / windows-local / whisper-local in Slice 1).
std::unique_ptr<IAsrEngine> CreateAsrEngine(const config::AsrConfig& cfg);

}  // namespace onekey::asr
