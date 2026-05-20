#pragma once
#include "IAsrEngine.h"
#include "../config/Config.h"
#include <mutex>
#include <vector>
#include <string>

namespace onekey::asr {

// Azure Speech short-audio REST endpoint:
//   POST https://{region}.stt.speech.microsoft.com/speech/recognition/conversation/cognitiveservices/v1
//        ?language=zh-CN&format=detailed
//   Header: Ocp-Apim-Subscription-Key
//           Content-Type: audio/wav; codecs=audio/pcm; samplerate=16000
//   Body: 16k mono 16-bit PCM WAV
//
// provider_options:
//   key      (required)  Azure Speech subscription key
//   region   (required)  Azure region, e.g. "westus2"
//   endpoint (optional)  override the default https://{region}.stt.speech.microsoft.com host
//
// REST is non-streaming: PCM is buffered during FeedAudio() and the recognize
// call happens on Stop(). on_final fires synchronously before Stop() returns.
class AzureRestAsrEngine : public IAsrEngine {
public:
    explicit AzureRestAsrEngine(const config::AsrConfig& cfg);

    AsrCapabilities capabilities() const override {
        return AsrCapabilities{};  // all flags false — pure batch
    }

    void Start() override;
    void FeedAudio(const int16_t* pcm, size_t samples) override;
    void Stop() override;

private:
    // Resolved from provider_options at construction time.
    std::string  language_;     // e.g. "zh-CN"
    std::string  key_;
    std::string  region_;
    std::string  endpoint_;     // optional override

    std::mutex            mu_;
    std::vector<int16_t>  pcm_;
};

}  // namespace onekey::asr
