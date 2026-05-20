#pragma once
#include "IAsrEngine.h"
#include "../config/Config.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

// Forward-declare SDK types so consumers don't drag the SDK headers.
// All real SDK usage lives in the .cpp.
namespace Microsoft::CognitiveServices::Speech {
    class SpeechRecognizer;
    namespace Audio {
        class PushAudioInputStream;
        class AudioConfig;
    }
}

namespace onekey::asr {

// Streaming Azure Speech recognizer via the official Speech SDK.
//
// provider_options:
//   key      (required)  Azure Speech subscription key
//   region   (required)  Azure region, e.g. "westus2"
//   endpoint (optional)  explicit endpoint URL (overrides region)
//
// Capabilities:
//   is_streaming=true, emits_partials=true, emits_segment_finals=true,
//   has_built_in_vad=true (SDK does its own utterance segmentation).
//
// Audio in: 16k mono int16 PCM via FeedAudio() — pushed into a SDK
// PushAudioInputStream. The SDK runs its own recognition thread that
// invokes our Recognizing / Recognized callbacks.
//
// Stop() is synchronous: it calls StopContinuousRecognition (returns when
// the SDK has flushed all in-flight recognition), then emits SessionFinal
// with the concatenated final text.
class AzureStreamAsrEngine : public IAsrEngine {
public:
    explicit AzureStreamAsrEngine(const config::AsrConfig& cfg);
    ~AzureStreamAsrEngine() override;

    AsrCapabilities capabilities() const override {
        return AsrCapabilities{
            /*is_streaming=*/true,
            /*emits_partials=*/true,
            /*emits_segment_finals=*/true,
            /*has_built_in_vad=*/true,
        };
    }

    void Start() override;
    void FeedAudio(const int16_t* pcm, size_t samples) override;
    void Stop() override;

private:
    // SDK objects are pImpl'd to keep heavy headers out of consumers.
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::string language_;
    std::string key_;
    std::string region_;
    std::string endpoint_;

    std::mutex      text_mu_;
    std::wstring    accumulated_;  // concat of all SegmentFinal text
    std::atomic<bool> running_{false};
};

}  // namespace onekey::asr
