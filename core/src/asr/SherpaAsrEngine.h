#pragma once
#include "IAsrEngine.h"
#include "../config/Config.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Forward-declare the sherpa-onnx opaque handles so the heavy C API header
// stays out of consumers — the .cpp does the actual sherpa includes.
struct SherpaOnnxOnlineRecognizer;
struct SherpaOnnxOnlineStream;

namespace onekey::asr {

// Local streaming ASR via sherpa-onnx (Paraformer-zh-streaming by default).
//
// provider_options:
//   model_dir   (required)  Directory containing encoder.onnx, decoder.onnx,
//                           and tokens.txt. The Paraformer streaming model
//                           layout from k2-fsa releases.
//   num_threads (optional)  ONNX Runtime threads, default 2.
//   provider    (optional)  ONNX execution provider, default "cpu". CUDA /
//                           DirectML are out of scope for Phase 1.
//   debug       (optional)  Boolean — enables sherpa's verbose logging.
//
// Capabilities mirror AzureStreamAsrEngine: partials + segment-finals fired
// from a background decode thread, terminated by exactly one SessionFinal
// from Stop(). Audio in is 16k mono int16 PCM via FeedAudio() — matches
// WasapiCapture's output so no resampling here.
class SherpaAsrEngine : public IAsrEngine {
public:
    explicit SherpaAsrEngine(const config::AsrConfig& cfg);
    ~SherpaAsrEngine() override;

    AsrCapabilities capabilities() const override {
        return AsrCapabilities{
            /*is_streaming=*/true,
            /*emits_partials=*/true,
            /*emits_segment_finals=*/true,
            // Endpoint detection is built in but conservative; the session
            // layer still drives Start/Stop via the hotkey.
            /*has_built_in_vad=*/true,
        };
    }

    void Start() override;
    void FeedAudio(const int16_t* pcm, size_t samples) override;
    void Stop() override;

private:
    // Background decode loop: dequeues PCM chunks, feeds the stream, calls
    // Decode while ready, and emits Partial / SegmentFinal events.
    void DecodeLoop();

    // Resolved paths for the Paraformer model files; validated in the ctor
    // so a misconfigured model_dir fails fast with a useful message.
    struct ModelPaths {
        std::string encoder;
        std::string decoder;
        std::string tokens;
    };
    ModelPaths paths_;

    int         num_threads_ = 2;
    std::string provider_str_ = "cpu";
    bool        debug_ = false;

    SherpaOnnxOnlineRecognizer* recognizer_ = nullptr;
    SherpaOnnxOnlineStream*     stream_     = nullptr;

    // Producer/consumer queue. WasapiCapture feeds from its capture thread;
    // DecodeLoop consumes on its worker thread. Audio is small (16-bit @ 16k,
    // a few hundred ms in flight), so we just copy into per-chunk vectors.
    std::mutex                          q_mu_;
    std::condition_variable             q_cv_;
    std::deque<std::vector<int16_t>>    q_;
    bool                                q_eof_ = false;

    std::thread       worker_;
    std::atomic<bool> running_{false};

    // Accumulator + last-partial bookkeeping shared between DecodeLoop and Stop.
    std::mutex   text_mu_;
    std::wstring accumulated_;     // all SegmentFinals concatenated
    std::wstring last_partial_;    // de-dupe filter for Partial emission
};

}  // namespace onekey::asr
