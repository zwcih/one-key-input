#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>
#include <mutex>

namespace onekey::audio {

// Captures default-input microphone at 16 kHz mono int16 PCM.
// Internally records at the device mix format and resamples in software.
class WasapiCapture {
public:
    WasapiCapture();
    ~WasapiCapture();

    // Start a new capture session. Blocks until the capture thread is up & running.
    // on_pcm fires from the capture thread with newly-captured 16k mono int16 samples.
    bool Start(std::function<void(const int16_t* pcm, size_t samples)> on_pcm);

    // Stop. Blocks until thread joined.
    void Stop();

    bool IsRunning() const { return running_.load(); }

private:
    void RunCapture();

    std::function<void(const int16_t*, size_t)> on_pcm_;
    std::thread                worker_;
    std::atomic<bool>          running_{false};
    std::atomic<bool>          stop_requested_{false};
};

}  // namespace onekey::audio
