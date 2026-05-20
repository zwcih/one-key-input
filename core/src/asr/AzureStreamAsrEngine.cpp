#include "AzureStreamAsrEngine.h"
#include "../util/Strings.h"

#include <spdlog/spdlog.h>

// Speech SDK is heavy; isolate to this TU only.
#include <speechapi_cxx.h>

#include <chrono>
#include <future>

namespace onekey::asr {

using namespace Microsoft::CognitiveServices::Speech;
using namespace Microsoft::CognitiveServices::Speech::Audio;

struct AzureStreamAsrEngine::Impl {
    std::shared_ptr<SpeechConfig>          cfg;
    std::shared_ptr<PushAudioInputStream>  push;
    std::shared_ptr<AudioConfig>           audio;
    std::shared_ptr<SpeechRecognizer>      reco;
};

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

}  // namespace

AzureStreamAsrEngine::AzureStreamAsrEngine(const config::AsrConfig& cfg)
    : impl_(std::make_unique<Impl>()) {
    language_ = cfg.language;
    const auto& po = cfg.provider_options;
    key_      = po.value("key", "");
    region_   = po.value("region", "");
    endpoint_ = po.value("endpoint", "");

    if (key_.empty()) {
        throw std::runtime_error("azure-stream: provider_options.key missing");
    }
    if (region_.empty() && endpoint_.empty()) {
        throw std::runtime_error("azure-stream: need provider_options.region or .endpoint");
    }

    // Build SpeechConfig once and reuse across Start/Stop cycles.
    if (!endpoint_.empty()) {
        impl_->cfg = SpeechConfig::FromEndpoint(endpoint_, key_);
    } else {
        impl_->cfg = SpeechConfig::FromSubscription(key_, region_);
    }
    impl_->cfg->SetSpeechRecognitionLanguage(language_);

    // Match Python parity: prefer punctuated "TrueText" display when available.
    impl_->cfg->SetProperty(
        PropertyId::SpeechServiceResponse_PostProcessingOption, "TrueText");

    spdlog::info("[asr.azure-stream] configured lang={} region={}",
                 language_, region_.empty() ? "(via endpoint)" : region_);
}

AzureStreamAsrEngine::~AzureStreamAsrEngine() {
    // Make sure any in-flight recognition is stopped before SDK objects die.
    if (running_.load() && impl_ && impl_->reco) {
        try { impl_->reco->StopContinuousRecognitionAsync().get(); }
        catch (...) {}
    }
}

void AzureStreamAsrEngine::Start() {
    {
        std::lock_guard<std::mutex> lk(text_mu_);
        accumulated_.clear();
    }

    // Fresh push stream + recognizer per session — simpler lifecycle than
    // trying to reuse: the SDK closes the stream when StopContinuousRecognition
    // finishes, and reopening cleanly is more reliable than re-arming.
    auto fmt = AudioStreamFormat::GetWaveFormatPCM(16000, 16, 1);
    impl_->push  = AudioInputStream::CreatePushStream(fmt);
    impl_->audio = AudioConfig::FromStreamInput(impl_->push);
    impl_->reco  = SpeechRecognizer::FromConfig(impl_->cfg, impl_->audio);

    // Recognizing — interim hypothesis. We surface this as Partial events
    // (for the overlay) but the session won't polish on them.
    impl_->reco->Recognizing.Connect(
        [this](const SpeechRecognitionEventArgs& e) {
            std::string t = e.Result->Text;
            if (t.empty()) return;
            AsrEvent ev;
            ev.kind = AsrEventKind::Partial;
            ev.text = util::Utf8ToWide(t);
            if (on_event) on_event(ev);
        });

    // Recognized — stable utterance. Emit SegmentFinal; accumulate for the
    // eventual SessionFinal we'll fire from Stop().
    impl_->reco->Recognized.Connect(
        [this](const SpeechRecognitionEventArgs& e) {
            if (e.Result->Reason != ResultReason::RecognizedSpeech) {
                spdlog::debug("[asr.azure-stream] Recognized reason={}",
                              int(e.Result->Reason));
                return;
            }
            std::string t = e.Result->Text;
            if (t.empty()) return;
            std::wstring w = util::Utf8ToWide(t);
            {
                std::lock_guard<std::mutex> lk(text_mu_);
                if (!accumulated_.empty()) accumulated_ += L" ";
                accumulated_ += w;
            }
            AsrEvent ev;
            ev.kind = AsrEventKind::SegmentFinal;
            ev.text = w;
            if (on_event) on_event(ev);
        });

    impl_->reco->Canceled.Connect(
        [this](const SpeechRecognitionCanceledEventArgs& e) {
            std::string reason = e.ErrorDetails;
            spdlog::warn("[asr.azure-stream] Canceled: reason={} details={}",
                         int(e.Reason), reason);
            if (e.Reason == CancellationReason::Error) {
                EmitError(on_event, util::Utf8ToWide(reason));
            }
        });

    spdlog::info("[asr.azure-stream] StartContinuousRecognitionAsync");
    try {
        impl_->reco->StartContinuousRecognitionAsync().get();
        running_.store(true);
    } catch (const std::exception& e) {
        spdlog::error("[asr.azure-stream] start failed: {}", e.what());
        EmitError(on_event, util::Utf8ToWide(e.what()));
        EmitFinal(on_event, L"");
    }
}

void AzureStreamAsrEngine::FeedAudio(const int16_t* pcm, size_t samples) {
    if (!pcm || samples == 0 || !running_.load() || !impl_->push) return;
    impl_->push->Write(reinterpret_cast<uint8_t*>(const_cast<int16_t*>(pcm)),
                       static_cast<uint32_t>(samples * sizeof(int16_t)));
}

void AzureStreamAsrEngine::Stop() {
    if (!running_.load()) {
        EmitFinal(on_event, L"");
        return;
    }
    running_.store(false);

    // Closing the push stream tells the SDK no more audio will arrive; it
    // then drains the recognizer naturally. StopContinuousRecognition blocks
    // until the final Recognized event has fired.
    try {
        if (impl_->push) impl_->push->Close();
    } catch (...) {}

    auto t0 = std::chrono::steady_clock::now();
    try {
        impl_->reco->StopContinuousRecognitionAsync().get();
    } catch (const std::exception& e) {
        spdlog::error("[asr.azure-stream] stop failed: {}", e.what());
        EmitError(on_event, util::Utf8ToWide(e.what()));
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();

    std::wstring final_text;
    {
        std::lock_guard<std::mutex> lk(text_mu_);
        final_text = accumulated_;
    }
    spdlog::info("[asr.azure-stream] stopped in {}ms, final_chars={}",
                 ms, final_text.size());

    EmitFinal(on_event, final_text);

    // Drop SDK objects so the next Start() rebuilds cleanly.
    impl_->reco.reset();
    impl_->audio.reset();
    impl_->push.reset();
}

}  // namespace onekey::asr
