#include "SoundCues.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstring>

namespace onekey::audio {

namespace {

constexpr int    kSampleRate = 22050;
constexpr double kPi         = 3.14159265358979;

}  // namespace

void SoundCues::BuildToneInto(Buffer& b, double from_hz, double to_hz, int dur_ms) {
    const int n = kSampleRate * dur_ms / 1000;
    b.samples.assign(n, 0);
    const int attack  = kSampleRate *  4 / 1000;
    const int release = kSampleRate * 25 / 1000;
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        double k = (n > 1) ? double(i) / (n - 1) : 0.0;
        double f = from_hz + (to_hz - from_hz) * k;
        phase += 2.0 * kPi * f / kSampleRate;
        double env = 1.0;
        if (i < attack)             env = double(i) / attack;
        else if (i > n - release)   env = double(n - i) / release;
        if (env < 0) env = 0;
        double v = std::sin(phase) * env * 0.30;
        b.samples[i] = int16_t(v * 32767);
    }

    b.hdr = {};
    b.hdr.lpData         = reinterpret_cast<LPSTR>(b.samples.data());
    b.hdr.dwBufferLength = DWORD(b.samples.size() * sizeof(int16_t));
}

void SoundCues::PrepareAll() {
    auto prep = [this](Buffer& b){
        MMRESULT r = ::waveOutPrepareHeader(hwo_, &b.hdr, sizeof(WAVEHDR));
        if (r != MMSYSERR_NOERROR) {
            spdlog::warn("[sound] waveOutPrepareHeader failed: {}", r);
            return;
        }
        b.prepared = true;
    };
    prep(start_);
    prep(stop_);
    prep(error_);
}

SoundCues::SoundCues() {
    // Build PCM payloads.
    BuildToneInto(start_, 660.0, 990.0, 80);
    BuildToneInto(stop_,  990.0, 660.0, 80);
    BuildToneInto(error_, 220.0, 220.0, 160);

    WAVEFORMATEX wf{};
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = 1;
    wf.nSamplesPerSec  = kSampleRate;
    wf.wBitsPerSample  = 16;
    wf.nBlockAlign     = wf.nChannels * wf.wBitsPerSample / 8;
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    MMRESULT r = ::waveOutOpen(&hwo_, WAVE_MAPPER, &wf,
                               0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) {
        spdlog::error("[sound] waveOutOpen failed: {}", r);
        hwo_ = nullptr;
        return;
    }
    PrepareAll();
    spdlog::info("[sound] waveOut device open, buffers prepared");
}

SoundCues::~SoundCues() {
    if (!hwo_) return;
    ::waveOutReset(hwo_);
    auto unprep = [this](Buffer& b){
        if (b.prepared) {
            ::waveOutUnprepareHeader(hwo_, &b.hdr, sizeof(WAVEHDR));
            b.prepared = false;
        }
    };
    unprep(start_);
    unprep(stop_);
    unprep(error_);
    ::waveOutClose(hwo_);
    hwo_ = nullptr;
}

void SoundCues::Play(Buffer& b) {
    if (!enabled_ || !hwo_ || !b.prepared) return;
    std::lock_guard<std::mutex> lk(mu_);
    // If a previous play is still in flight on the same WAVEHDR, the second
    // write will be queued; that's fine for our 80 ms chirps but we reset the
    // dwFlags' WHDR_DONE bit explicitly so re-submission is safe.
    b.hdr.dwFlags &= ~WHDR_DONE;
    MMRESULT r = ::waveOutWrite(hwo_, &b.hdr, sizeof(WAVEHDR));
    if (r != MMSYSERR_NOERROR) {
        spdlog::warn("[sound] waveOutWrite failed: {}", r);
    }
}

void SoundCues::PlayStart() { Play(start_); }
void SoundCues::PlayStop()  { Play(stop_);  }
void SoundCues::PlayError() { Play(error_); }

}  // namespace onekey::audio
