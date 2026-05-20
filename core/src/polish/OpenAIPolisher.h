#pragma once
#include "IPolisher.h"
#include "../config/Config.h"
#include <string>

namespace onekey::polish {

// OpenAI-compatible streaming Chat Completions client. Compatible with:
//   - Azure OpenAI:  https://{resource}.openai.azure.com/openai/deployments/{deploy}/chat/completions?api-version=...
//   - OpenAI:        https://api.openai.com/v1/chat/completions
//   - Ollama:        http://localhost:11434/v1/chat/completions
//
// provider_options:
//   endpoint    (required for openai-azure / ollama; defaults to api.openai.com for openai)
//   key         (required except for keyless local Ollama)
//   deployment  (Azure: the deployment name in the URL; others: the model name)
//   api_version (Azure only)
class OpenAIPolisher : public IPolisher {
public:
    explicit OpenAIPolisher(const config::PolishConfig& cfg);

    void Polish(const std::wstring& raw,
                const PolishContext& ctx,
                std::function<void(std::wstring_view token, bool is_final)> on_token) override;

private:
    std::string provider_;     // "openai-azure" | "openai" | "ollama"
    std::string mode_;
    double      temperature_;
    int         max_tokens_;
    std::string endpoint_;
    std::string key_;
    std::string deployment_;   // also doubles as model name for non-Azure
    std::string api_version_;
};

}  // namespace onekey::polish
