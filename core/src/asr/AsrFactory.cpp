#include "AsrFactory.h"
#include "AzureRestAsrEngine.h"
#include "AzureStreamAsrEngine.h"
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
    if (cfg.provider == "windows-local") {
        throw std::runtime_error("asr provider 'windows-local' not implemented yet");
    }
    if (cfg.provider == "whisper-local") {
        throw std::runtime_error("asr provider 'whisper-local' not implemented yet");
    }
    if (cfg.provider == "sherpa-onnx") {
        // Privacy-mode roadmap: sherpa-onnx + Paraformer-zh streaming.
        // See docs/privacy-mode.md for the model selection and rollout plan.
        throw std::runtime_error(
            "asr provider 'sherpa-onnx' not implemented yet "
            "(privacy-mode roadmap; see docs/privacy-mode.md)");
    }
    throw std::runtime_error("unknown asr provider: " + cfg.provider);
}

}  // namespace onekey::asr
