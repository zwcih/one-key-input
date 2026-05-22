// Mirror of core/src/config/Config.h. Optional fields here mean "absent in
// the JSON" — we preserve unknown keys during save by reading + merging.

export interface AppConfig {
  asr: {
    provider: string;
    language: string;
    punctuation?: boolean;
    provider_options: {
      key?: string;
      region?: string;
      endpoint?: string;
      // sherpa-paraformer (local):
      model_dir?: string;
      num_threads?: number;
      provider?: string;
      [k: string]: unknown;
    };
  };
  polish: {
    provider: string;
    mode: "raw" | "tidy" | "formal" | string;
    max_tokens?: number;
    use_context?: boolean;
    provider_options: {
      endpoint?: string;
      key?: string;
      deployment?: string;
      api_version?: string;
      [k: string]: unknown;
    };
  };
  inject: { mode: "sendinput" | "clipboard" | "auto" | string };
  hotkey: {
    key: string;
    min_hold_ms?: number;
    // Recording behavior. Mirrors C++ HotkeyConfig::behavior.
    //   push_to_talk: hold to record (legacy default for upgrades)
    //   toggle      : tap once to start, tap again to stop
    //   smart       : short tap = toggle, long press = push-to-talk
    behavior?: "push_to_talk" | "toggle" | "smart" | string;
    // Smart mode short/long press boundary in milliseconds. < threshold = toggle.
    smart_threshold_ms?: number;
    // Safety stop for toggle / smart-sticky in milliseconds. 0 disables.
    max_duration_ms?: number;
  };
  sound: { enabled: boolean };
  autostart: { enabled: boolean };
  // Translation mode (F8 by default). Reuses the polish pipeline but swaps
  // the LLM prompt for a structured translator prompt. Optional in the JSON
  // for back-compat: older configs predate this section.
  translate?: {
    enabled: boolean;
    hotkey: string;
    min_hold_ms?: number;
    target_language: string;   // "en" | "zh" | "ja" | "ko" | "de" | "fr" | "es" | ...
    smart_target: boolean;     // experimental — derive target from focus
  };
  [k: string]: unknown;  // preserve unknown top-level keys
}

export function defaultConfig(): AppConfig {
  return {
    asr: {
      provider: "azure-stream",
      language: "zh-CN",
      punctuation: true,
      provider_options: { key: "", region: "westus2" },
    },
    polish: {
      provider: "openai-azure",
      mode: "tidy",
      max_tokens: 2000,
      use_context: true,
      provider_options: {
        endpoint: "",
        key: "",
        deployment: "gpt-4o-mini",
        api_version: "2024-08-01-preview",
      },
    },
    inject: { mode: "sendinput" },
    hotkey: {
      key: "f9",
      min_hold_ms: 250,
      // Fresh installs default to smart (short tap toggles, long press =
      // push-to-talk). Existing on-disk configs without this field are
      // backfilled with "push_to_talk" by mergeWithDefaults() so upgrades
      // see no behavior regression.
      behavior: "smart",
      smart_threshold_ms: 400,
      max_duration_ms: 300000,
    },
    sound: { enabled: true },
    autostart: { enabled: true },
    translate: {
      enabled: true,
      hotkey: "f8",
      min_hold_ms: 250,
      target_language: "en",
      smart_target: false,
    },
  };
}

// True if any required credential is still a placeholder/empty.
export function isFirstRun(c: AppConfig): boolean {
  const azurePlaceholder = (s?: string) =>
    !s || s.startsWith("YOUR_");
  if (c.asr.provider.startsWith("azure")) {
    if (azurePlaceholder(c.asr.provider_options.key as string | undefined))
      return true;
  }
  // sherpa-paraformer (local ASR) requires a non-empty model_dir.
  if (c.asr.provider === "sherpa-paraformer") {
    const dir = c.asr.provider_options.model_dir as string | undefined;
    if (!dir) return true;
  }
  if (c.polish.provider.startsWith("openai")) {
    if (azurePlaceholder(c.polish.provider_options.key as string | undefined))
      return true;
  }
  return false;
}

// Backfill any top-level section the on-disk config is missing using the
// current schema's defaults. This is the upgrade-path safety net: a v0.1
// config.json predates the `translate` block, and the form would crash
// (or silently drop the section on save) if we didn't fill it in here.
//
// Defending every known top-level section — not just `translate` — means
// the *next* schema addition won't repeat the v0.1 → v0.2 white-screen
// regression. Returns a new object; the input is not mutated. Unknown
// top-level keys are preserved for forward compatibility.
export function mergeWithDefaults(c: AppConfig): AppConfig {
  const d = defaultConfig();
  // structuredClone so callers can't observe shared references between
  // the input, the defaults, and the result.
  const out = structuredClone(c) as AppConfig;
  if (out.asr === undefined) out.asr = d.asr;
  if (out.polish === undefined) out.polish = d.polish;
  if (out.inject === undefined) out.inject = d.inject;
  if (out.hotkey === undefined) {
    out.hotkey = d.hotkey;
  } else {
    // Per-field backfill for hotkey: a v0.1 config has `key` and
    // `min_hold_ms` but no behavior/threshold/max_duration. Default the
    // missing fields to *push_to_talk* — explicit zero-regression for
    // upgraders, even though new installs (defaultConfig) start on smart.
    if (out.hotkey.behavior === undefined) out.hotkey.behavior = "push_to_talk";
    if (out.hotkey.smart_threshold_ms === undefined) {
      out.hotkey.smart_threshold_ms = d.hotkey.smart_threshold_ms;
    }
    if (out.hotkey.max_duration_ms === undefined) {
      out.hotkey.max_duration_ms = d.hotkey.max_duration_ms;
    }
  }
  if (out.sound === undefined) out.sound = d.sound;
  if (out.autostart === undefined) out.autostart = d.autostart;
  if (out.translate === undefined) out.translate = d.translate;
  return out;
}
