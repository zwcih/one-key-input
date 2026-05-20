#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace onekey::asr {

// What kind of recognition events a given engine can produce.
enum class AsrEventKind {
    // Streaming-only: best-effort interim text; will be revised by later
    // events. UI may show it; the polisher should NOT consume partials.
    Partial,

    // Streaming-only: a stable, final transcription for a single utterance
    // segment (engine-defined; typically ends at silence / punctuation).
    // The session may already begin polishing this segment while later
    // segments continue to arrive.
    SegmentFinal,

    // Required from every engine on a successful Start->Stop cycle.
    // Batch engines emit exactly one SessionFinal (the whole utterance).
    // Streaming engines emit it as the last event after all SegmentFinals,
    // typically containing the concatenated final text.
    SessionFinal,

    // Recognition failed. Engine SHOULD still emit a SessionFinal afterward
    // (with empty text) so the session's state machine completes.
    Error,
};

struct AsrEvent {
    AsrEventKind kind = AsrEventKind::SessionFinal;
    std::wstring text;
    double       t_start_ms = 0.0;
    double       t_end_ms   = 0.0;
    double       confidence = 0.0;     // 0 = not provided
    std::wstring error;                 // Error events only
};

// Self-describing capability flags. Sessions can branch on these without
// guessing what backend is behind the interface. Adding a new provider
// declares its capabilities here once, and the rest of the codebase adapts.
struct AsrCapabilities {
    bool is_streaming      = false;  // true if engine ever emits Partial/SegmentFinal
    bool emits_partials    = false;  // streaming sub-bit: emits Partial events
    bool emits_segment_finals = false; // streaming sub-bit: emits SegmentFinal events
    bool has_built_in_vad  = false;  // engine slices utterances itself (Azure does)
};

// ASR engine contract.
//
// Construction:
//   Concrete engines take their full config (provider_options included) via
//   constructor or factory. The session layer never knows or cares which
//   backend it talks to — it only inspects capabilities() and listens to
//   on_event.
//
// Lifecycle per dictation:
//   Start()      — open resources / network / WebSocket.
//   FeedAudio()* — 16k mono int16 PCM, called repeatedly while user holds hotkey.
//   Stop()       — finalize. MUST be synchronous: by the time Stop() returns,
//                  a SessionFinal (or Error followed by SessionFinal) has fired
//                  on on_event. Streaming engines that have already fired
//                  Partials/SegmentFinals must still finish with a SessionFinal.
//
// Event ordering guarantee:
//   * Within a single Start/Stop cycle, events are delivered in
//     temporal order on a single thread context.
//   * Exactly one SessionFinal event terminates each cycle.
//   * After SessionFinal, on_event is not invoked again until the next Start.
class IAsrEngine {
public:
    virtual ~IAsrEngine() = default;

    virtual AsrCapabilities capabilities() const = 0;

    virtual void Start() = 0;
    virtual void FeedAudio(const int16_t* pcm, size_t samples) = 0;
    virtual void Stop() = 0;

    std::function<void(const AsrEvent&)> on_event;
};

const char* AsrEventKindName(AsrEventKind k);

}  // namespace onekey::asr
