#include "DictationSession.h"
#include "../audio/WasapiCapture.h"
#include "../util/Strings.h"
#include "../focus/ContextExtractor.h"

#include <spdlog/spdlog.h>

#include <algorithm>
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
      injector_(injector), capture_(capture), bus_(bus),
      behavior_machine_(ParseBehavior(cfg.hotkey.behavior),
                        cfg.hotkey.smart_threshold_ms,
                        cfg.hotkey.max_duration_ms) {
    asr_->on_event = [this](const asr::AsrEvent& ev){
        switch (ev.kind) {
            case asr::AsrEventKind::Partial:
                // For partials: log + push to overlay via the current
                // recording phase with the partial text in detail. We
                // mirror whichever recording sub-state we're in (regular
                // Recording vs. StickyRecording) so the tray icon doesn't
                // flap back to red while in sticky mode.
                spdlog::debug("[session] asr.partial: {}", util::WideToUtf8(ev.text));
                if (bus_ && !ev.text.empty()) {
                    Phase cur = phase_.load();
                    Phase echo = (cur == Phase::StickyRecording)
                                   ? Phase::StickyRecording
                                   : Phase::Recording;
                    bus_->Publish({echo, ev.text});
                }
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

DictationSession::~DictationSession() {
    StopWatchdog();
}

void DictationSession::OnHotkeyPress(Mode mode) {
    std::lock_guard<std::mutex> lk(machine_mu_);
    auto act = behavior_machine_.OnKeyDown(std::chrono::steady_clock::now());
    switch (act) {
        case HotkeyAction::StartRecording:
            StartRecording(mode);
            break;
        case HotkeyAction::StopAndProcess:
            // Second tap in toggle / smart-sticky.
            StopAndProcess();
            break;
        case HotkeyAction::PromoteToSticky:
        case HotkeyAction::None:
            break;
    }
}

void DictationSession::OnHotkeyRelease() {
    std::lock_guard<std::mutex> lk(machine_mu_);
    auto act = behavior_machine_.OnKeyUp(std::chrono::steady_clock::now());
    switch (act) {
        case HotkeyAction::StopAndProcess:
            StopAndProcess();
            break;
        case HotkeyAction::PromoteToSticky:
            // Mic stays open; flip the phase so the tray reflects the
            // hands-free state. The watchdog (started in StartRecording) is
            // already running and will enforce max_duration_ms.
            if (phase_.load() == Phase::Recording) {
                SetPhase(Phase::StickyRecording);
                spdlog::info("[session] promoted to sticky (hands-free) recording");
            }
            break;
        case HotkeyAction::StartRecording:
        case HotkeyAction::None:
            break;
    }
}

void DictationSession::OnEscape() {
    std::lock_guard<std::mutex> lk(machine_mu_);
    auto act = behavior_machine_.OnEscape();
    if (act == HotkeyAction::StopAndProcess) {
        spdlog::info("[session] Esc -> force stop");
        StopAndProcess();
    }
}

void DictationSession::StartRecording(Mode mode) {
    Phase expected = Phase::Idle;
    if (!phase_.compare_exchange_strong(expected, Phase::Recording)) {
        spdlog::warn("[session] StartRecording ignored (phase={})",
                     PhaseName(phase_.load()));
        return;
    }
    mode_ = mode;
    t_press_ = std::chrono::steady_clock::now();
    spdlog::info("==> [session] start recording (mode={})",
                 mode == Mode::Translate ? "translate" : "polish");
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
        return;
    }

    // Start the max-duration watchdog. Push-to-talk users never hit it
    // (the OS will deliver KeyUp first), but for toggle/smart it's the
    // safety net that prevents a forgotten mic from running forever.
    StartWatchdog();
}

void DictationSession::StopAndProcess() {
    Phase cur = phase_.load();
    if (cur != Phase::Recording && cur != Phase::StickyRecording) {
        spdlog::debug("[session] StopAndProcess ignored (phase={})", PhaseName(cur));
        return;
    }
    // Tear down the watchdog before we start the long-running pipeline so
    // it can't fire StopAndProcess again mid-recognize.
    StopWatchdog();
    t_release_ = std::chrono::steady_clock::now();
    auto rec_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                     t_release_ - t_press_).count();
    spdlog::info("<== [session] stop recording, held {}ms", rec_ms);

    capture_->Stop();
    SetPhase(Phase::Recognizing);

    std::thread([this]{ this->DoRecognizeAndPolish(); }).detach();
}

void DictationSession::StartWatchdog() {
    StopWatchdog();  // be safe
    if (cfg_.hotkey.max_duration_ms <= 0) return;
    {
        std::lock_guard<std::mutex> lk(watchdog_mu_);
        watchdog_stop_ = false;
    }
    watchdog_ = std::thread([this]{
        std::unique_lock<std::mutex> lk(watchdog_mu_);
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(cfg_.hotkey.max_duration_ms);
        // Loop on both the stop flag *and* the deadline so spurious
        // wakeups don't accidentally bail early, and so we re-evaluate
        // remaining time after each wakeup (otherwise the wait could
        // tick beyond the deadline by up to a full poll interval).
        while (!watchdog_stop_) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;
            auto remaining = std::min(
                std::chrono::milliseconds(500),
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
            watchdog_cv_.wait_for(lk, remaining);
        }
        if (watchdog_stop_) return;
        lk.unlock();
        spdlog::warn("[session] max_duration_ms ({}) hit — force stop",
                     cfg_.hotkey.max_duration_ms);
        // Drive through the same OnEscape path so the behavior machine's
        // internal state stays consistent (no half-states).
        OnEscape();
    });
}

void DictationSession::StopWatchdog() {
    {
        std::lock_guard<std::mutex> lk(watchdog_mu_);
        watchdog_stop_ = true;
    }
    watchdog_cv_.notify_all();
    // If the watchdog itself is calling this (via OnEscape→StopAndProcess),
    // joining would deadlock. In that case detach — the thread is about to
    // exit naturally after its callback returns anyway.
    if (watchdog_.joinable()) {
        if (watchdog_.get_id() == std::this_thread::get_id()) {
            watchdog_.detach();
        } else {
            watchdog_.join();
        }
    }
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
    pctx.focus_app        = ectx.app_label;
    pctx.scene_hint       = ectx.scene_hint;
    pctx.recent_text      = ectx.recent_text;
    pctx.user_typed       = ectx.user_typed;
    pctx.surrounding_text = focus::AsPromptBlock(ectx);

    if (mode_ == Mode::Translate) {
        // Translation borrows the polish style ladder via the polisher's
        // construction-time mode_ — we only flip the discriminator.
        pctx.style           = "translate";
        pctx.target_language = cfg_.translate.target_language;
        pctx.source_language = cfg_.asr.language;
        spdlog::info("[session] translate -> target='{}' source='{}'",
                     pctx.target_language, pctx.source_language);
    } else {
        pctx.style = cfg_.polish.mode;
    }

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
