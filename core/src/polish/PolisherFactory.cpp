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
        throw std::runtime_error("polish provider 'llamacpp' not implemented yet (Slice 3)");
    }
    throw std::runtime_error("unknown polish provider: " + cfg.provider);
}

}  // namespace onekey::polish
