#include "PolisherFactory.h"
#include "OpenAIPolisher.h"

#include <spdlog/spdlog.h>
#include <stdexcept>

namespace onekey::polish {

std::unique_ptr<IPolisher> CreatePolisher(const config::PolishConfig& cfg) {
    spdlog::info("[polish.factory] provider={}", cfg.provider);
    if (cfg.provider == "openai-azure" || cfg.provider == "openai" || cfg.provider == "ollama") {
        return std::make_unique<OpenAIPolisher>(cfg);
    }
    if (cfg.provider == "llamacpp") {
        // Privacy-mode roadmap: llama.cpp + Qwen2.5-3B-Instruct (Q4_K_M).
        // See docs/privacy-mode.md for the model selection and rollout plan.
        throw std::runtime_error(
            "polish provider 'llamacpp' not implemented yet "
            "(privacy-mode roadmap; see docs/privacy-mode.md)");
    }
    throw std::runtime_error("unknown polish provider: " + cfg.provider);
}

}  // namespace onekey::polish
