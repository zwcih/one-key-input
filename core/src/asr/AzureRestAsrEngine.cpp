#include "AzureRestAsrEngine.h"
#include "../net/HttpClient.h"
#include "../util/Strings.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstring>
#include <stdexcept>

namespace onekey::asr {

namespace {

void EmitError(const std::function<void(const AsrEvent&)>& cb,
               const std::wstring& msg) {
    if (!cb) return;
    AsrEvent ev;
    ev.kind  = AsrEventKind::Error;
    ev.error = msg;
    cb(ev);
}

void EmitFinal(const std::function<void(const AsrEvent&)>& cb,
               const std::wstring& text) {
    if (!cb) return;
    AsrEvent ev;
    ev.kind = AsrEventKind::SessionFinal;
    ev.text = text;
    cb(ev);
}

std::vector<uint8_t> BuildWav(const std::vector<int16_t>& pcm) {
    constexpr int sample_rate = 16000;
    constexpr int channels    = 1;
    constexpr int bits        = 16;
    const uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const uint32_t fmt_size   = 16;
    const uint32_t riff_size  = 4 + (8 + fmt_size) + (8 + data_bytes);
    const uint32_t byte_rate  = sample_rate * channels * bits / 8;
    const uint16_t block_align = channels * bits / 8;

    std::vector<uint8_t> buf;
    buf.reserve(8 + riff_size);

    auto put32 = [&](uint32_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xff));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
        buf.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
        buf.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
    };
    auto put16 = [&](uint16_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xff));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    };
    auto puts = [&](const char* s) {
        for (size_t i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>(s[i]));
    };

    puts("RIFF"); put32(riff_size); puts("WAVE");
    puts("fmt "); put32(fmt_size);
    put16(1); put16(channels); put32(sample_rate); put32(byte_rate);
    put16(block_align); put16(bits);
    puts("data"); put32(data_bytes);

    const size_t prev = buf.size();
    buf.resize(prev + data_bytes);
    std::memcpy(buf.data() + prev, pcm.data(), data_bytes);
    return buf;
}

}  // namespace

AzureRestAsrEngine::AzureRestAsrEngine(const config::AsrConfig& cfg) {
    language_ = cfg.language;
    const auto& po = cfg.provider_options;
    key_      = po.value("key", "");
    region_   = po.value("region", "");
    endpoint_ = po.value("endpoint", "");

    if (key_.empty()) {
        throw std::runtime_error("azure-rest: provider_options.key missing");
    }
    if (region_.empty() && endpoint_.empty()) {
        throw std::runtime_error("azure-rest: need provider_options.region or .endpoint");
    }
    spdlog::info("[asr.azure-rest] configured lang={} region={}",
                 language_, region_.empty() ? "(via endpoint)" : region_);
}

void AzureRestAsrEngine::Start() {
    std::lock_guard<std::mutex> lk(mu_);
    pcm_.clear();
}

void AzureRestAsrEngine::FeedAudio(const int16_t* pcm, size_t samples) {
    if (!pcm || samples == 0) return;
    std::lock_guard<std::mutex> lk(mu_);
    pcm_.insert(pcm_.end(), pcm, pcm + samples);
}

void AzureRestAsrEngine::Stop() {
    std::vector<int16_t> snapshot;
    {
        std::lock_guard<std::mutex> lk(mu_);
        snapshot.swap(pcm_);
    }
    if (snapshot.empty()) {
        spdlog::warn("[asr.azure-rest] stop with no audio captured");
        EmitFinal(on_event, L"");
        return;
    }

    spdlog::info("[asr.azure-rest] uploading {} samples (~{:.1f}s)",
                 snapshot.size(), snapshot.size() / 16000.0);

    auto wav = BuildWav(snapshot);

    std::string host = endpoint_.empty()
        ? ("https://" + region_ + ".stt.speech.microsoft.com")
        : endpoint_;
    std::string url = host +
        "/speech/recognition/conversation/cognitiveservices/v1?language=" +
        language_ + "&format=detailed";

    cpr::Header headers{
        {"Ocp-Apim-Subscription-Key", key_},
        {"Content-Type", "audio/wav; codecs=audio/pcm; samplerate=16000"},
        {"Accept", "application/json"},
        {"Expect", ""},
    };

    auto t0 = std::chrono::steady_clock::now();
    auto resp = net::PostBytes(url, headers, wav, 30000);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0).count();
    spdlog::info("[asr.azure-rest] response in {}ms status={}", elapsed_ms, resp.status);

    if (!resp.ok()) {
        std::string err = "azure-rest http " + std::to_string(resp.status) +
                          (resp.error_msg.empty() ? "" : (": " + resp.error_msg));
        spdlog::error("[asr.azure-rest] {}", err);
        EmitError(on_event, util::Utf8ToWide(err));
        EmitFinal(on_event, L"");
        return;
    }

    std::wstring text;
    try {
        auto j = nlohmann::json::parse(resp.body);
        std::string status_str = j.value("RecognitionStatus", "");
        std::string display    = j.value("DisplayText", "");
        if (display.empty() && j.contains("NBest") && j["NBest"].is_array() && !j["NBest"].empty()) {
            display = j["NBest"][0].value("Display", "");
        }
        if (status_str == "Success") {
            text = util::Utf8ToWide(display);
        } else {
            spdlog::warn("[asr.azure-rest] non-success status={}", status_str);
        }
    } catch (const std::exception& e) {
        spdlog::error("[asr.azure-rest] parse failed: {}, body: {}", e.what(), resp.body);
        EmitError(on_event, util::Utf8ToWide(std::string("parse: ") + e.what()));
        EmitFinal(on_event, L"");
        return;
    }

    EmitFinal(on_event, text);
}

}  // namespace onekey::asr
