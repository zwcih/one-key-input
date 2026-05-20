#pragma once
#include <vector>
#include <cstdint>
#include <mutex>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <mmsystem.h>

namespace onekey::audio {

// Low-latency UI chirp player. Opens a HWAVEOUT once and keeps it open;
// each PlayStart/Stop/Error just submits a pre-prepared WAVEHDR via
// waveOutWrite, so latency is sub-frame (~5 ms) instead of the 50-100+ ms
// that PlaySound's per-call setup incurs.
//
// All buffers are 16-bit 22050 Hz mono — fixed format keeps device open
// across all three sounds.
class SoundCues {
public:
    SoundCues();
    ~SoundCues();

    void PlayStart();
    void PlayStop();
    void PlayError();

    void SetEnabled(bool on) { enabled_ = on; }
    bool Enabled() const     { return enabled_; }

    // No-op now (device is opened in the ctor); kept for source compat.
    void Prewarm() {}

private:
    struct Buffer {
        std::vector<int16_t> samples;
        WAVEHDR              hdr{};
        bool                 prepared = false;
    };

    void BuildToneInto(Buffer& b, double from_hz, double to_hz, int dur_ms);
    void PrepareAll();
    void Play(Buffer& b);

    HWAVEOUT  hwo_ = nullptr;
    Buffer    start_;
    Buffer    stop_;
    Buffer    error_;
    std::mutex mu_;
    bool      enabled_ = true;
};

}  // namespace onekey::audio
