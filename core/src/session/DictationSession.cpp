#include "DictationSession.h"
#include "../audio/WasapiCapture.h"
#include "../util/Strings.h"
#include "../focus/ContextExtractor.h"

#include <spdlog/spdlog.h>

#include <thread>
#include <unordered_set>

namespace onekey::session {

namespace {

const std::unordered_set<wchar_t>& FlushPunct() {
    static const std::unordered_set<wchar_t> s = {
        L'。', L'！', L'？', L'，', L'、', L'；', L'：',
        L',',  L'.',  L'!',  L'?',  L';',  L':',  L'\n'
    };
    return s;
}

bool ShouldFlush(const std::wstring& buf, bool force) {
    if (force) return !buf.empty();
    if (buf.empty()) return false;
    if (buf.size() >= 24) return true;
    if (buf.size() >= 6 && FlushPunct().count(buf.back())) return true;
    return false;
}

}  // namespace

DictationSession::DictationSession(const config::AppConfig& cfg,
                                   asr::IAsrEngine* asr,
                                   polish::IPolisher* polisher,
                                   inject::InjectorStrategy* injector,
                                   audio::WasapiCapture* capture,
                                   EventBus* bus)
    : cfg_(cfg), asr_(asr), polisher_(polisher),
      injector_(injector), capture_(capture), bus_(bus) {
    asr_->on_event = [this](const asr::AsrEvent& ev){
        switch (ev.kind) {
            case asr::AsrEventKind::Partial:
                // For partials: log + push to overlay via the Recording phase
                // with the partial text in detail. Polishing is NOT triggered.
                spdlog::debug("[session] asr.partial: {}", util::WideToUtf8(ev.text));
                if (bus_ && !ev.text.empty()) bus_->Publish({Phase::Recording, ev.text});
                break;
            case asr::AsrEventKind::SegmentFinal:
                // Per design discussion: even with a streaming engine we
                // currently wait for SessionFinal so the polisher sees the
                // whole utterance (better context). Just log for now.
                spdlog::debug("[session] asr.segment_final: {}", util::WideToUtf8(ev.text));
                break;
            case asr::AsrEventKind::SessionFinal: {
                std::lock_guard<std::mutex> lk(mu_);
                final_text_ = ev.text;
                break;
            }
            case asr::AsrEventKind::Error:
                spdlog::error("[session] asr.error: {}", util::WideToUtf8(ev.error));
                SetPhase(Phase::Error, ev.error);
                break;
        }
    };
}

void DictationSession::SetPhase(Phase p, std::wstring detail) {
    phase_.store(p);
    if (bus_) bus_->Publish({p, std::move(detail)});
}

void DictationSession::StartRecording() {
    Phase expected = Phase::Idle;
    if (!phase_.compare_exchange_strong(expected, Phase::Recording)) {
        spdlog::warn("[session] StartRecording ignored (phase={})",
                     PhaseName(phase_.load()));
        return;
    }
    t_press_ = std::chrono::steady_clock::now();
    spdlog::info("==> [session] start recording");
    if (bus_) bus_->Publish({Phase::Recording, L""});

    // Kick a UIA snapshot of the focus context in parallel with recording.
    // We freeze the foreground window at press time — if the user alt-tabs
    // mid-utterance, we still want context from where they *meant* to type.
    // Skipped entirely when polish.use_context is false (saves ~80-150ms
    // of UIA work + nothing reaches the LLM).
    if (cfg_.polish.use_context) {
        focus_future_ = focus::SnapshotAsync(::GetForegroundWindow());
    }

    {
        std::lock_guard<std::mutex> lk(mu_);
        final_text_.clear();
        inject_buf_.clear();
    }

    asr_->Start();

    bool ok = capture_->Start([this](const int16_t* pcm, size_t n){
        asr_->FeedAudio(pcm, n);
    });
    if (!ok) {
        spdlog::error("[session] WASAPI capture start failed");
        SetPhase(Phase::Error, L"mic open failed");
        asr_->Stop();
        SetPhase(Phase::Idle);
    }
}

void DictationSession::StopAndProcess() {
    Phase cur = phase_.load();
    if (cur != Phase::Recording) {
        spdlog::debug("[session] StopAndProcess ignored (phase={})", PhaseName(cur));
        return;
    }
    t_release_ = std::chrono::steady_clock::now();
    auto rec_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                     t_release_ - t_press_).count();
    spdlog::info("<== [session] stop recording, held {}ms", rec_ms);

    capture_->Stop();
    SetPhase(Phase::Recognizing);

    std::thread([this]{ this->DoRecognizeAndPolish(); }).detach();
}

void DictationSession::DoRecognizeAndPolish() {
    asr_->Stop();  // synchronously triggers on_final for REST engine

    std::wstring raw;
    {
        std::lock_guard<std::mutex> lk(mu_);
        raw = final_text_;
    }
    if (raw.empty()) {
        spdlog::info("[session] empty transcription, abort");
        SetPhase(Phase::Idle);
        return;
    }
    spdlog::info("[session] raw: {}", util::WideToUtf8(raw));

    // Consume the focus snapshot we started at press time. Bounded wait —
    // if UIA hasn't finished by now (rare; budget is 800ms, recording is
    // usually longer), give up and polish without context.
    focus::EffectiveContext ectx;
    if (focus_future_.valid()) {
        auto status = focus_future_.wait_for(std::chrono::milliseconds(200));
        if (status == std::future_status::ready) {
            auto snap = focus_future_.get();
            spdlog::debug("[session] focus snapshot:{}",
                          util::WideToUtf8(focus::DebugDump(snap)));
            ectx = focus::Extract(snap);
            if (ectx.any()) {
                spdlog::info("[session] effective context: app=\"{}\" scene=\"{}\" "
                             "typed_chars={} nearby_chars={}",
                             util::WideToUtf8(ectx.app_label),
                             util::WideToUtf8(ectx.scene_hint),
                             ectx.user_typed.size(),
                             ectx.recent_text.size());
            } else {
                spdlog::info("[session] effective context: empty");
            }
        } else {
            spdlog::warn("[session] focus snapshot not ready after 200ms — dropped");
        }
    }

    inject::InjectTarget tgt;
    tgt.focused_hwnd = ::GetForegroundWindow();

    polish::PolishContext pctx;
    pctx.style = cfg_.polish.mode;
    pctx.focus_app        = ectx.app_label;
    pctx.surrounding_text = focus::AsPromptBlock(ectx);

    SetPhase(Phase::Polishing, raw);

    bool injecting_phase_announced = false;
    // Track whether the polisher ever produced output. Per the IPolisher
    // contract, polishers fire on_token(_, true) with an empty token on
    // failure — so "is_final fires with no prior tokens" means polish failed
    // and we should fall back to injecting the raw text instead of leaving
    // the user with nothing.
    bool any_token = false;

    polisher_->Polish(raw, pctx, [this, tgt, raw, &injecting_phase_announced,
                                  &any_token]
                                 (std::wstring_view token, bool is_final) {
        if (!token.empty()) any_token = true;
        if (!injecting_phase_announced && !token.empty()) {
            injecting_phase_announced = true;
            SetPhase(Phase::Injecting);
        }
        std::lock_guard<std::mutex> lk(mu_);
        if (!token.empty()) inject_buf_.append(token);
        bool force = is_final;
        if (is_final && !any_token) {
            // Polish failed — inject the raw transcript so the user keeps
            // their content. Announce the phase change late so the tray
            // briefly reflects "injecting".
            spdlog::warn("[session] polish produced no tokens; injecting raw");
            inject_buf_.assign(raw);
            if (!injecting_phase_announced) {
                injecting_phase_announced = true;
                SetPhase(Phase::Injecting);
            }
        }
        if (ShouldFlush(inject_buf_, force)) {
            spdlog::debug("[session] inject flush {} chars", inject_buf_.size());
            injector_->InjectChunk(inject_buf_, tgt);
            inject_buf_.clear();
        }
        if (is_final) {
            injector_->Commit(tgt);
            auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - t_press_).count();
            spdlog::info("[session] done total {}ms (polish_ok={})",
                         total_ms, any_token);
        }
    });

    // If polish failed, surface it. We still got text in front of the user
    // (the raw transcript), but the user should know polish needs fixing.
    if (!any_token) {
        SetPhase(Phase::Error, L"polish failed — injected raw text");
    }

    SetPhase(Phase::Done);
    SetPhase(Phase::Idle);
}

}  // namespace onekey::session
