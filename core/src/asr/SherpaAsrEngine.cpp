#include "SherpaAsrEngine.h"
#include "../util/Strings.h"

#include <sherpa-onnx/c-api/c-api.h>
#include <spdlog/spdlog.h>

#include <windows.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace onekey::asr {

namespace {

// Resolve a model_dir possibly containing %LOCALAPPDATA% (or similar) into
// an absolute filesystem path. We deliberately don't pull in shlobj/SHGet*;
// the standard env-var expansion is enough for the documented config shape.
//
// A relative path is anchored at the directory holding the running exe
// (NOT current_path()): the default config ships `models\paraformer-...`
// next to the binaries, and autostart / shortcut launches set an unrelated
// CWD that would break a CWD-relative resolution.
std::filesystem::path ExeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

std::filesystem::path ExpandEnv(const std::string& raw) {
    std::filesystem::path p(util::Utf8ToWide(raw));
    if (raw.empty()) return p;
    std::wstring s = p.wstring();
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == L'%') {
            auto end = s.find(L'%', i + 1);
            if (end != std::wstring::npos) {
                std::wstring var = s.substr(i + 1, end - i - 1);
                wchar_t buf[32767];
                DWORD n = ::GetEnvironmentVariableW(var.c_str(), buf, 32767);
                if (n > 0 && n < 32767) {
                    out.append(buf, n);
                    i = end + 1;
                    continue;
                }
            }
        }
        out.push_back(s[i++]);
    }
    std::filesystem::path expanded(out);
    if (expanded.is_relative()) {
        auto base = ExeDir();
        if (!base.empty()) expanded = base / expanded;
    }
    return expanded;
}

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

}  // namespace

SherpaAsrEngine::SherpaAsrEngine(const config::AsrConfig& cfg) {
    const auto& po = cfg.provider_options;

    std::string model_dir_raw = po.value("model_dir", std::string{});
    if (model_dir_raw.empty()) {
        throw std::runtime_error(
            "sherpa-paraformer: provider_options.model_dir missing");
    }
    auto dir = ExpandEnv(model_dir_raw);
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        throw std::runtime_error(
            "sherpa-paraformer: model_dir does not exist or is not a directory: "
            + dir.string());
    }

    // The Paraformer-zh-streaming model packs four files. Tolerate the
    // common int8 naming too — users sometimes paste the raw file names
    // from the upstream tarball without renaming.
    auto pick = [&](std::initializer_list<const char*> names) -> std::string {
        for (const char* n : names) {
            auto p = dir / n;
            if (std::filesystem::exists(p, ec)) {
                return p.string();
            }
        }
        return {};
    };
    paths_.encoder = pick({"encoder.onnx", "encoder.int8.onnx"});
    paths_.decoder = pick({"decoder.onnx", "decoder.int8.onnx"});
    paths_.tokens  = pick({"tokens.txt"});
    if (paths_.encoder.empty() || paths_.decoder.empty() || paths_.tokens.empty()) {
        std::string missing;
        if (paths_.encoder.empty()) missing += " encoder.onnx";
        if (paths_.decoder.empty()) missing += " decoder.onnx";
        if (paths_.tokens.empty())  missing += " tokens.txt";
        throw std::runtime_error(
            "sherpa-paraformer: model_dir missing required files:" + missing
            + " (looked under " + dir.string() + ")");
    }

    num_threads_  = po.value("num_threads", 2);
    if (num_threads_ < 1) num_threads_ = 1;
    provider_str_ = po.value("provider", std::string{"cpu"});
    debug_        = po.value("debug", false);

    spdlog::info("[asr.sherpa] configured model_dir={} threads={} provider={}",
                 dir.string(), num_threads_, provider_str_);
}

SherpaAsrEngine::~SherpaAsrEngine() {
    // Make sure the worker is gone before we tear down sherpa handles.
    if (running_.load()) {
        // Stop() does the full teardown; call it defensively if the session
        // layer skipped it (e.g. shutdown during an error).
        try { Stop(); } catch (...) {}
    }
    if (stream_) {
        SherpaOnnxDestroyOnlineStream(stream_);
        stream_ = nullptr;
    }
    if (recognizer_) {
        SherpaOnnxDestroyOnlineRecognizer(recognizer_);
        recognizer_ = nullptr;
    }
}

void SherpaAsrEngine::Start() {
    {
        std::lock_guard<std::mutex> lk(text_mu_);
        accumulated_.clear();
        last_partial_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(q_mu_);
        q_.clear();
        q_eof_ = false;
    }

    // Lazy-create the recognizer once and reuse it across Start/Stop cycles.
    // Model load is the expensive part (~hundreds of ms for Paraformer int8),
    // and the C API supports replacing the stream cheaply via destroy+create.
    if (!recognizer_) {
        SherpaOnnxOnlineRecognizerConfig rcfg{};
        rcfg.feat_config.sample_rate  = 16000;
        rcfg.feat_config.feature_dim  = 80;
        rcfg.model_config.paraformer.encoder = paths_.encoder.c_str();
        rcfg.model_config.paraformer.decoder = paths_.decoder.c_str();
        rcfg.model_config.tokens             = paths_.tokens.c_str();
        rcfg.model_config.num_threads        = num_threads_;
        rcfg.model_config.provider           = provider_str_.c_str();
        rcfg.model_config.debug              = debug_ ? 1 : 0;
        rcfg.decoding_method                 = "greedy_search";
        rcfg.enable_endpoint                 = 1;
        rcfg.rule1_min_trailing_silence      = 2.4f;
        rcfg.rule2_min_trailing_silence      = 1.2f;
        rcfg.rule3_min_utterance_length      = 20.0f;

        auto t0 = std::chrono::steady_clock::now();
        recognizer_ = const_cast<SherpaOnnxOnlineRecognizer*>(
            SherpaOnnxCreateOnlineRecognizer(&rcfg));
        if (!recognizer_) {
            EmitError(on_event,
                L"sherpa-onnx: SherpaOnnxCreateOnlineRecognizer returned null "
                L"(check model files match Paraformer-streaming layout)");
            EmitFinal(on_event, L"");
            return;
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
        spdlog::info("[asr.sherpa] recognizer loaded in {}ms", ms);
    }

    // Fresh per-session stream — Reset() exists but a brand-new stream is
    // the simpler lifecycle and avoids state leaks between dictations.
    stream_ = const_cast<SherpaOnnxOnlineStream*>(
        SherpaOnnxCreateOnlineStream(recognizer_));
    if (!stream_) {
        EmitError(on_event, L"sherpa-onnx: SherpaOnnxCreateOnlineStream null");
        EmitFinal(on_event, L"");
        return;
    }

    running_.store(true);
    worker_ = std::thread([this] { DecodeLoop(); });
}

void SherpaAsrEngine::FeedAudio(const int16_t* pcm, size_t samples) {
    if (!pcm || samples == 0 || !running_.load()) return;
    std::vector<int16_t> chunk(pcm, pcm + samples);
    {
        std::lock_guard<std::mutex> lk(q_mu_);
        q_.emplace_back(std::move(chunk));
    }
    q_cv_.notify_one();
}

void SherpaAsrEngine::Stop() {
    if (!running_.load()) {
        EmitFinal(on_event, L"");
        return;
    }

    // Drain: tell the worker no more audio is coming, then wait for it to
    // finish decoding what's already in the queue. The worker is responsible
    // for flushing the tail via the "is_final" stream option.
    {
        std::lock_guard<std::mutex> lk(q_mu_);
        q_eof_ = true;
    }
    q_cv_.notify_all();

    if (worker_.joinable()) worker_.join();
    running_.store(false);

    if (stream_) {
        SherpaOnnxDestroyOnlineStream(stream_);
        stream_ = nullptr;
    }

    std::wstring final_text;
    {
        std::lock_guard<std::mutex> lk(text_mu_);
        final_text = accumulated_;
        // If endpoint detection never fired (short utterance, single segment),
        // accumulated_ is empty — fall back to the most recent partial.
        if (final_text.empty()) final_text = last_partial_;
    }
    EmitFinal(on_event, final_text);
}

void SherpaAsrEngine::DecodeLoop() {
    constexpr int kSampleRate = 16000;

    while (true) {
        std::vector<int16_t> chunk;
        bool eof = false;
        {
            std::unique_lock<std::mutex> lk(q_mu_);
            q_cv_.wait(lk, [this] { return !q_.empty() || q_eof_; });
            if (!q_.empty()) {
                chunk = std::move(q_.front());
                q_.pop_front();
            }
            eof = q_eof_ && q_.empty();
        }

        if (!chunk.empty()) {
            // sherpa wants float samples in [-1, 1].
            std::vector<float> f(chunk.size());
            constexpr float kScale = 1.0f / 32768.0f;
            for (size_t i = 0; i < chunk.size(); ++i) f[i] = chunk[i] * kScale;
            SherpaOnnxOnlineStreamAcceptWaveform(
                stream_, kSampleRate, f.data(), static_cast<int32_t>(f.size()));
        }

        if (eof) {
            // Tell the recognizer the stream is finalized so it flushes any
            // tail audio still in its feature buffer.
            //
            // InputFinished() is the contract — required for every streaming
            // recognizer to flush the tail.
            //
            // The "is_final" stream option is the additional Paraformer-streaming
            // signal that nudges the decoder to emit a finalized token sequence
            // without waiting for an endpoint timeout. It is a no-op for
            // Transducer-style models (Zipformer etc.), but Paraformer needs it.
            //
            // We probe HasOption() once and log if it's gone: a future upstream
            // rename would otherwise be a silent regression — tail text just
            // arrives late or not at all, and tests pass because they don't
            // measure latency.
            SherpaOnnxOnlineStreamInputFinished(stream_);
            if (SherpaOnnxOnlineStreamHasOption(stream_, "is_final")) {
                SherpaOnnxOnlineStreamSetOption(stream_, "is_final", "1");
            } else if (!warned_missing_is_final_) {
                warned_missing_is_final_ = true;
                spdlog::warn("[asr.sherpa] 'is_final' stream option not "
                             "recognized by this sherpa-onnx build; "
                             "Paraformer tail flush may be delayed. Check "
                             "upstream c-api.h for the new option name.");
            }
        }

        while (SherpaOnnxIsOnlineStreamReady(recognizer_, stream_)) {
            SherpaOnnxDecodeOnlineStream(recognizer_, stream_);
        }

        // Emit a Partial event whenever the visible text changed. The C API
        // returns a heap-allocated result struct we must free.
        const SherpaOnnxOnlineRecognizerResult* r =
            SherpaOnnxGetOnlineStreamResult(recognizer_, stream_);
        if (r && r->text && r->text[0] != '\0') {
            std::wstring w = util::Utf8ToWide(r->text);
            bool changed = false;
            {
                std::lock_guard<std::mutex> lk(text_mu_);
                if (w != last_partial_) {
                    last_partial_ = w;
                    changed = true;
                }
            }
            if (changed && on_event) {
                AsrEvent ev;
                ev.kind = AsrEventKind::Partial;
                ev.text = w;
                on_event(ev);
            }
        }
        if (r) SherpaOnnxDestroyOnlineRecognizerResult(r);

        // Endpoint? Flush as a SegmentFinal and reset for the next utterance.
        if (SherpaOnnxOnlineStreamIsEndpoint(recognizer_, stream_)) {
            std::wstring seg;
            {
                std::lock_guard<std::mutex> lk(text_mu_);
                seg = last_partial_;
                last_partial_.clear();
                if (!seg.empty()) {
                    if (!accumulated_.empty()) accumulated_ += L' ';
                    accumulated_ += seg;
                }
            }
            if (!seg.empty() && on_event) {
                AsrEvent ev;
                ev.kind = AsrEventKind::SegmentFinal;
                ev.text = seg;
                on_event(ev);
            }
            SherpaOnnxOnlineStreamReset(recognizer_, stream_);
        }

        if (eof) {
            // After is_final flush we've emitted whatever we can; promote
            // the residual partial into accumulated_ so Stop() picks it up
            // even if no endpoint fired for the tail.
            std::lock_guard<std::mutex> lk(text_mu_);
            if (!last_partial_.empty()) {
                if (!accumulated_.empty()) accumulated_ += L' ';
                accumulated_ += last_partial_;
                last_partial_.clear();
            }
            return;
        }
    }
}

}  // namespace onekey::asr
