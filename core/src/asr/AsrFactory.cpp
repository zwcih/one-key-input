#include "AsrFactory.h"
#include "AzureRestAsrEngine.h"
#include "AzureStreamAsrEngine.h"
#include "SherpaAsrEngine.h"
#include "../util/Strings.h"

#include <spdlog/spdlog.h>
#include <stdexcept>

namespace onekey::asr {

std::unique_ptr<IAsrEngine> CreateAsrEngine(const config::AsrConfig& cfg) {
    spdlog::info("[asr.factory] provider={}", cfg.provider);
    if (cfg.provider == "azure-rest") {
        return std::make_unique<AzureRestAsrEngine>(cfg);
    }
    if (cfg.provider == "azure-stream") {
        return std::make_unique<AzureStreamAsrEngine>(cfg);
    }
    if (cfg.provider == "sherpa-paraformer") {
        return std::make_unique<SherpaAsrEngine>(cfg);
    }
    if (cfg.provider == "windows-local") {
        throw std::runtime_error("asr provider 'windows-local' not implemented yet");
    }
    if (cfg.provider == "whisper-local") {
        throw std::runtime_error("asr provider 'whisper-local' not implemented yet");
    }
    throw std::runtime_error("unknown asr provider: " + cfg.provider);
}

}  // namespace onekey::asr
