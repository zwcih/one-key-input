#include "WasapiCapture.h"

#include <spdlog/spdlog.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <comdef.h>

#include <vector>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace onekey::audio {

namespace {

constexpr int kTargetSampleRate = 16000;

// Linear resampler producing int16 mono @ 16k from any float32 multi-channel input.
class ToMono16k {
public:
    void Configure(int src_rate, int src_channels, bool is_float, int src_bits) {
        src_rate_     = src_rate;
        src_channels_ = src_channels;
        is_float_     = is_float;
        src_bits_     = src_bits;
        ratio_        = static_cast<double>(src_rate_) / kTargetSampleRate;
        carry_pos_    = 0.0;
        residue_.clear();
    }

    // Convert a chunk to mono float in [-1,1].
    std::vector<float> ToMonoFloat(const BYTE* data, UINT32 frames) const {
        std::vector<float> mono(frames);
        if (is_float_ && src_bits_ == 32) {
            const float* src = reinterpret_cast<const float*>(data);
            for (UINT32 i = 0; i < frames; ++i) {
                float sum = 0.f;
                for (int c = 0; c < src_channels_; ++c) sum += src[i * src_channels_ + c];
                mono[i] = sum / static_cast<float>(src_channels_);
            }
        } else if (!is_float_ && src_bits_ == 16) {
            const int16_t* src = reinterpret_cast<const int16_t*>(data);
            for (UINT32 i = 0; i < frames; ++i) {
                int32_t sum = 0;
                for (int c = 0; c < src_channels_; ++c) sum += src[i * src_channels_ + c];
                int32_t avg = sum / src_channels_;
                mono[i] = static_cast<float>(avg) / 32768.f;
            }
        } else if (!is_float_ && src_bits_ == 32) {
            const int32_t* src = reinterpret_cast<const int32_t*>(data);
            for (UINT32 i = 0; i < frames; ++i) {
                int64_t sum = 0;
                for (int c = 0; c < src_channels_; ++c) sum += src[i * src_channels_ + c];
                int64_t avg = sum / src_channels_;
                mono[i] = static_cast<float>(avg) / 2147483648.f;
            }
        } else {
            // Unsupported, return silence
            std::fill(mono.begin(), mono.end(), 0.f);
        }
        return mono;
    }

    // Append new mono float samples, emit resampled int16 16k samples to `out`.
    void Resample(const std::vector<float>& mono, std::vector<int16_t>& out) {
        // Combine with leftover residue
        std::vector<float> buf;
        buf.reserve(residue_.size() + mono.size());
        buf.insert(buf.end(), residue_.begin(), residue_.end());
        buf.insert(buf.end(), mono.begin(),     mono.end());

        // Linear interpolate at positions carry_pos_, carry_pos_+ratio_, ...
        double pos = carry_pos_;
        while (pos + 1.0 < static_cast<double>(buf.size())) {
            size_t idx = static_cast<size_t>(pos);
            double frac = pos - static_cast<double>(idx);
            float s = buf[idx] * static_cast<float>(1.0 - frac) +
                      buf[idx + 1] * static_cast<float>(frac);
            int v = static_cast<int>(s * 32767.f);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out.push_back(static_cast<int16_t>(v));
            pos += ratio_;
        }
        // Keep tail residue (samples needed for next round of interpolation)
        size_t consumed = static_cast<size_t>(pos);
        if (consumed >= 1 && consumed <= buf.size()) {
            residue_.assign(buf.begin() + (consumed - 1), buf.end());
            carry_pos_ = pos - static_cast<double>(consumed - 1);
        } else {
            residue_.clear();
            carry_pos_ = 0.0;
        }
    }

private:
    int    src_rate_     = 0;
    int    src_channels_ = 0;
    bool   is_float_     = false;
    int    src_bits_     = 0;
    double ratio_        = 1.0;
    double carry_pos_    = 0.0;
    std::vector<float> residue_;
};

bool ParseFormat(const WAVEFORMATEX* fmt, bool& is_float, int& bits) {
    if (!fmt) return false;
    if (fmt->wFormatTag == WAVE_FORMAT_PCM) {
        is_float = false;
        bits = fmt->wBitsPerSample;
        return true;
    }
    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        is_float = true;
        bits = fmt->wBitsPerSample;
        return true;
    }
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
        bits = fmt->wBitsPerSample;
        if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            is_float = true;
            return true;
        }
        if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
            is_float = false;
            return true;
        }
    }
    return false;
}

}  // namespace

WasapiCapture::WasapiCapture() = default;

WasapiCapture::~WasapiCapture() {
    Stop();
}

bool WasapiCapture::Start(std::function<void(const int16_t*, size_t)> on_pcm) {
    if (running_.load()) return false;
    on_pcm_ = std::move(on_pcm);
    stop_requested_.store(false);
    running_.store(true);
    worker_ = std::thread([this] {
        this->RunCapture();
        running_.store(false);
    });
    return true;
}

void WasapiCapture::Stop() {
    stop_requested_.store(true);
    if (worker_.joinable()) worker_.join();
}

void WasapiCapture::RunCapture() {
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool com_inited = SUCCEEDED(hr);

    ComPtr<IMMDeviceEnumerator> enumerator;
    hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                            CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        spdlog::error("[wasapi] CoCreateInstance MMDeviceEnumerator failed 0x{:X}",
                      static_cast<uint32_t>(hr));
        if (com_inited) ::CoUninitialize();
        return;
    }

    ComPtr<IMMDevice> device;
    // Use eConsole rather than eCommunications. Communications role triggers
    // the system "communications activity" ducking, which silences other
    // audio (including our own UI chirps) the instant we open the mic — that
    // swallowed the "start recording" beep. eConsole avoids the duck.
    hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
    if (FAILED(hr)) {
        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &device);
    }
    if (FAILED(hr)) {
        spdlog::error("[wasapi] GetDefaultAudioEndpoint failed 0x{:X}",
                      static_cast<uint32_t>(hr));
        if (com_inited) ::CoUninitialize();
        return;
    }

    ComPtr<IAudioClient> client;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
    if (FAILED(hr)) {
        spdlog::error("[wasapi] Activate IAudioClient failed 0x{:X}",
                      static_cast<uint32_t>(hr));
        if (com_inited) ::CoUninitialize();
        return;
    }

    WAVEFORMATEX* mix_format = nullptr;
    hr = client->GetMixFormat(&mix_format);
    if (FAILED(hr) || !mix_format) {
        spdlog::error("[wasapi] GetMixFormat failed 0x{:X}", static_cast<uint32_t>(hr));
        if (com_inited) ::CoUninitialize();
        return;
    }

    bool is_float = false;
    int  bits     = 0;
    if (!ParseFormat(mix_format, is_float, bits)) {
        spdlog::error("[wasapi] Unsupported mix format tag={}, bits={}",
                      mix_format->wFormatTag, mix_format->wBitsPerSample);
        ::CoTaskMemFree(mix_format);
        if (com_inited) ::CoUninitialize();
        return;
    }

    spdlog::info("[wasapi] mix format: {} Hz, {} ch, {} bits, {}",
                 mix_format->nSamplesPerSec, mix_format->nChannels,
                 bits, is_float ? "float" : "pcm");

    REFERENCE_TIME buffer_duration = 200 * 10000; // 200 ms
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            0,
                            buffer_duration, 0,
                            mix_format, nullptr);
    if (FAILED(hr)) {
        spdlog::error("[wasapi] Initialize failed 0x{:X}", static_cast<uint32_t>(hr));
        ::CoTaskMemFree(mix_format);
        if (com_inited) ::CoUninitialize();
        return;
    }

    ComPtr<IAudioCaptureClient> capture;
    hr = client->GetService(IID_PPV_ARGS(&capture));
    if (FAILED(hr)) {
        spdlog::error("[wasapi] GetService(IAudioCaptureClient) failed 0x{:X}",
                      static_cast<uint32_t>(hr));
        ::CoTaskMemFree(mix_format);
        if (com_inited) ::CoUninitialize();
        return;
    }

    ToMono16k converter;
    converter.Configure(static_cast<int>(mix_format->nSamplesPerSec),
                        static_cast<int>(mix_format->nChannels),
                        is_float, bits);

    hr = client->Start();
    if (FAILED(hr)) {
        spdlog::error("[wasapi] Start failed 0x{:X}", static_cast<uint32_t>(hr));
        ::CoTaskMemFree(mix_format);
        if (com_inited) ::CoUninitialize();
        return;
    }

    DWORD task_index = 0;
    HANDLE mmcss = ::AvSetMmThreadCharacteristicsW(L"Audio", &task_index);

    std::vector<int16_t> out_buf;
    out_buf.reserve(kTargetSampleRate / 5);

    while (!stop_requested_.load()) {
        UINT32 packet = 0;
        hr = capture->GetNextPacketSize(&packet);
        if (FAILED(hr)) {
            spdlog::warn("[wasapi] GetNextPacketSize 0x{:X}", static_cast<uint32_t>(hr));
            break;
        }
        if (packet == 0) {
            ::Sleep(10);
            continue;
        }

        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD  flags = 0;
        hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (FAILED(hr)) {
            spdlog::warn("[wasapi] GetBuffer 0x{:X}", static_cast<uint32_t>(hr));
            break;
        }

        if (frames > 0 && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
            auto mono = converter.ToMonoFloat(data, frames);
            out_buf.clear();
            converter.Resample(mono, out_buf);
            if (!out_buf.empty() && on_pcm_) {
                on_pcm_(out_buf.data(), out_buf.size());
            }
        } else if (frames > 0 && (flags & AUDCLNT_BUFFERFLAGS_SILENT) && on_pcm_) {
            // Emit silence so timing stays continuous.
            std::vector<float> mono(frames, 0.f);
            out_buf.clear();
            converter.Resample(mono, out_buf);
            if (!out_buf.empty()) on_pcm_(out_buf.data(), out_buf.size());
        }

        capture->ReleaseBuffer(frames);
    }

    client->Stop();
    if (mmcss) ::AvRevertMmThreadCharacteristics(mmcss);

    ::CoTaskMemFree(mix_format);
    if (com_inited) ::CoUninitialize();
}

}  // namespace onekey::audio
