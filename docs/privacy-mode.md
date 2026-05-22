# Privacy Mode — Local On-Device Inference (ASR + LLM)

> Status: **roadmap**. The `privacy.mode` config field and the
> `sherpa-onnx` / `llamacpp` provider names already exist as recognized
> values in the parser and factories, but selecting `local` (or any
> on-device provider) currently surfaces a "not implemented yet" error
> until the engines below are wired up.

Privacy is a core requirement for a non-trivial slice of our users —
they do not want their audio or polished text to leave their machine.
The current v1 stack (Azure Speech + Azure OpenAI) needs an equivalent
**fully-local** inference path as a peer option.

## Goals

- A single switch toggles **cloud / local / hybrid** (ASR-local + polish-cloud, or vice-versa).
- In local mode: audio and text **never leave the device** (verified by packet capture in CI).
- Experience on par with the cloud build: end-to-end on hotkey release → first injected character **< 1.5 s on CPU**, **< 1 s on NPU**.
- Chinese first, coverage first (an 8 GB laptop must run the default profile).

## Model selection (Chinese-first + coverage-first)

### ASR: sherpa-onnx + Paraformer-zh (streaming)

- Damo Academy's Chinese-tuned model; CER beats Whisper-large on Chinese in our test set.
- Runs on ONNX Runtime, real-time on CPU (an i3 is enough), hardware-agnostic.
- ~70 MB after int8 quantization → fast cold start.
- Streaming architecture, first-token latency < 300 ms.
- English for v1: Paraformer copes well enough; v2 will lazy-load `whisper-small` (466 MB) on demand.

### LLM polish: Qwen2.5-3B-Instruct (Q4_K_M, GGUF)

- SOTA Chinese instruction-following at this parameter size.
- ~2 GB after Q4_K_M quantization → fits the 8 GB laptop budget.
- llama.cpp backend; ~15–20 tok/s on an i5 CPU — good enough for streaming polish.
- 7B available as an optional "enthusiast" tier (requires 16 GB).
- Apache 2.0 — no commercial-use friction.

### Rejected alternatives

- **whisper.cpp / faster-whisper** — worse Chinese quality, larger.
- **Llama / Phi / Gemma** — weaker Chinese instruction following at the same size as Qwen2.5.
- **Q8 / FP16 quants** — double the footprint for < 2 % quality gain on the polish task.
- **7B as default** — 5 GB resident immediately disqualifies half of our 8 GB users.

## Hardware acceleration: NPU / GPU adapters

AI-PC shipments are climbing, and NPUs are an excellent match for our
"short text, low latency, always-resident" workload.

| Platform | Software stack | Expected speedup (vs CPU) |
|---|---|---|
| Intel Core Ultra (LNL/MTL) | OpenVINO GenAI | LLM ~3×, ¼ the power |
| Snapdragon X Elite/Plus | QNN SDK / ONNX Runtime QNN EP | LLM ~3×, requires native ARM64 build |
| AMD Ryzen AI (Strix+) | Ryzen AI SW / DirectML | LLM ~2× |
| Generic GPU fallback | DirectML / Vulkan (llama.cpp) | LLM 1.5–3× |

**Abstraction**: ONNX Runtime + Execution Provider gives us a uniform
interface; at startup we probe and degrade by priority down to CPU. v1
ships DirectML as the single fast path for broad coverage; v2 adds
OpenVINO and QNN as native backends.

### Estimated latency (Qwen2.5-3B Q4, ~50 token polish)

| Path | Latency |
|---|---|
| CPU (i7-1260P) | 1.5–2.0 s |
| Intel LNL NPU (OpenVINO INT4) | 0.6–0.9 s ✅ |
| Snapdragon X NPU (QNN) | 0.7–1.0 s ✅ |
| Discrete GPU (RTX 4060, llama.cpp CUDA) | 0.4–0.6 s ✅ |

## Engineering notes

1. **Streaming polish + streaming inject.** At ~20 tok/s the text trickling out actually feels better than a single-shot drop — same UX intuition as ChatGPT's streaming output.
2. **Resident-model strategy.** Keep the model in memory by default to avoid per-shot lazy-load cost. Offer a "low-memory mode" that unloads after 30 s of idle and lazy-reloads on the next hotkey (~2 s first-shot penalty).
3. **Power-saving mode.** When unplugged AND battery < 30 %, cap context window to 512, force temperature 0, and skip optional re-passes.
4. **Fallback is mandatory.** NPU drivers are a compatibility minefield; if probing fails, degrade silently to CPU.
5. **Keep prefill / decode on the same accelerator** to avoid cross-device scheduling overhead.

## Configuration matrix (target shipping defaults)

| Tier | ASR | LLM | Download | Resident memory |
|---|---|---|---|---|
| Entry (8 GB / iGPU) | Paraformer-zh int8 | Qwen2.5-3B Q4_K_M | ~2.2 GB | ~2.5 GB |
| Mainstream + AI PC | Paraformer (NPU) | Qwen2.5-3B INT4 (NPU) | ~2.5 GB | ~3 GB (faster, cooler) |
| Enthusiast (32 GB + dGPU) | as above | Qwen2.5-7B Q4_K_M (opt-in) | +5 GB | +5 GB |

## Rollout

- **Phase 1** — CPU default (llama.cpp + sherpa-onnx) with DirectML as the optional fast path. Covers all Windows devices.
- **Phase 2** — OpenVINO native backend (Intel Core Ultra has the largest install base of NPUs).
- **Phase 3** — QNN EP for Snapdragon X (Copilot+ marketing leverage + native ARM64 differentiation).
- **Phase 4** — DirectML refinements for older AMD devices.

## Acceptance criteria

- [ ] Packet-capture run in local mode shows zero outbound traffic.
- [ ] Mid-range laptop (i5 + 8 GB) end-to-end < 2 s.
- [ ] AI PC (Core Ultra / Snapdragon X) end-to-end < 1 s.
- [ ] Cloud / local / hybrid switch flips at runtime without a restart.
- [ ] Model manager UI ships (tracked under the model-distribution issue).

## Configuration

```jsonc
{
  "privacy": {
    // cloud  — both ASR and polish hit cloud providers (current default)
    // local  — both run on-device, zero outbound traffic
    // hybrid — mix; provider fields decide which side is local
    "mode": "cloud"
  },
  "asr":    { "provider": "sherpa-onnx" },   // when in local mode
  "polish": { "provider": "llamacpp" }       // when in local mode
}
```

## Out of scope for this document

- Model distribution / downloader UI — tracked separately.
- TSF and context-snapshot logic — unaffected; local LLMs use the same context-prompt block as the cloud path.
