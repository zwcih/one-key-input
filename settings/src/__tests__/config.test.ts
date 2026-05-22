import { describe, it, expect } from "vitest";
import { defaultConfig, isFirstRun, mergeWithDefaults, AppConfig } from "../config";

describe("defaultConfig", () => {
  it("returns the documented Settings UI defaults", () => {
    // Note: this is the *Settings UI* default, which intentionally
    // differs from the C++ Core's `AsrConfig::provider` default of
    // "azure-rest" — the UI seeds new users on streaming Azure for a
    // better demo experience. Don't tighten this assertion to assume
    // cross-project parity.
    const c = defaultConfig();
    expect(c.asr.provider).toBe("azure-stream");
    expect(c.asr.language).toBe("zh-CN");
    expect(c.asr.punctuation).toBe(true);
    expect(c.polish.provider).toBe("openai-azure");
    expect(c.polish.mode).toBe("tidy");
    expect(c.polish.max_tokens).toBe(2000);
    expect(c.polish.use_context).toBe(true);
    expect(c.inject.mode).toBe("sendinput");
    expect(c.hotkey.key).toBe("f9");
    expect(c.hotkey.min_hold_ms).toBe(250);
    // New installs default to smart so users get the recommended behavior
    // out of the box. Upgrades (no `behavior` field on disk) are
    // backfilled to push_to_talk by mergeWithDefaults — see below.
    expect(c.hotkey.behavior).toBe("smart");
    expect(c.hotkey.smart_threshold_ms).toBe(400);
    expect(c.hotkey.max_duration_ms).toBe(300000);
    expect(c.sound.enabled).toBe(true);
    expect(c.autostart.enabled).toBe(true);
    expect(c.translate?.enabled).toBe(true);
    expect(c.translate?.hotkey).toBe("f8");
    expect(c.translate?.target_language).toBe("en");
    expect(c.translate?.smart_target).toBe(false);
  });

  it("returns a fresh object each call (no shared mutable state)", () => {
    const a = defaultConfig();
    const b = defaultConfig();
    a.asr.provider_options.key = "mutated";
    expect(b.asr.provider_options.key).toBe("");
  });
});

describe("isFirstRun", () => {
  const filled = (): AppConfig => {
    const c = defaultConfig();
    c.asr.provider_options.key = "real-azure-key";
    c.polish.provider_options.key = "real-openai-key";
    return c;
  };

  it("is true when the Azure key is empty", () => {
    const c = defaultConfig();
    c.polish.provider_options.key = "real-openai-key";
    // azure key still ""
    expect(isFirstRun(c)).toBe(true);
  });

  it("is true when the Azure key is a YOUR_* placeholder", () => {
    const c = filled();
    c.asr.provider_options.key = "YOUR_AZURE_SPEECH_KEY";
    expect(isFirstRun(c)).toBe(true);
  });

  it("is true when the polish key is a YOUR_* placeholder", () => {
    const c = filled();
    c.polish.provider_options.key = "YOUR_OPENAI_KEY";
    expect(isFirstRun(c)).toBe(true);
  });

  it("is false when both keys are filled with real values", () => {
    expect(isFirstRun(filled())).toBe(false);
  });

  it("ignores ASR key for non-Azure ASR providers", () => {
    const c = filled();
    c.asr.provider = "whisper-local";
    c.asr.provider_options.key = "";  // would normally trigger firstRun
    expect(isFirstRun(c)).toBe(false);
  });

  it("ignores polish key for non-OpenAI polish providers", () => {
    const c = filled();
    c.polish.provider = "llamacpp";
    c.polish.provider_options.key = "";
    expect(isFirstRun(c)).toBe(false);
  });
});

describe("translate config", () => {
  it("defaults to F8 / English target / smart_target off", () => {
    const c = defaultConfig();
    expect(c.translate).toBeDefined();
    expect(c.translate!.hotkey).toBe("f8");
    expect(c.translate!.target_language).toBe("en");
    expect(c.translate!.smart_target).toBe(false);
    expect(c.translate!.enabled).toBe(true);
  });

  it("does NOT affect isFirstRun (translate has no credentials of its own)", () => {
    // translate borrows the polish provider; first-run is governed solely
    // by whether the polish key is filled.
    const c = defaultConfig();
    c.asr.provider_options.key = "real-azure-key";
    c.polish.provider_options.key = "real-openai-key";
    // Even with translate disabled or smart_target on, first-run stays false.
    c.translate!.enabled = false;
    expect(isFirstRun(c)).toBe(false);
    c.translate!.enabled = true;
    c.translate!.smart_target = true;
    expect(isFirstRun(c)).toBe(false);
  });
});

describe("mergeWithDefaults", () => {
  // Regression guard for the v0.1 → v0.2 upgrade white-screen bug:
  // an older config.json predates the `translate` section, and the UI
  // crashed because translate was undefined when the form tried to read
  // it. mergeWithDefaults() must backfill every top-level section the
  // current schema knows about so the form has something to bind to.

  it("backfills the translate section for v0.1-shape configs", () => {
    // Simulate what `load_config` returns for an upgraded user: a
    // complete v0.1 config with no `translate` key at all.
    const v01: AppConfig = {
      asr: {
        provider: "azure-stream",
        language: "zh-CN",
        provider_options: { key: "real", region: "westus2" },
      },
      polish: {
        provider: "openai-azure",
        mode: "tidy",
        provider_options: { key: "real", endpoint: "" },
      },
      inject: { mode: "sendinput" },
      hotkey: { key: "f9", min_hold_ms: 250 },
      sound: { enabled: false },
      autostart: { enabled: false },
      // translate intentionally absent
    };
    const merged = mergeWithDefaults(v01);
    expect(merged.translate).toBeDefined();
    expect(merged.translate!.enabled).toBe(true);
    expect(merged.translate!.hotkey).toBe("f8");
    expect(merged.translate!.target_language).toBe("en");
    expect(merged.translate!.smart_target).toBe(false);
  });

  it("preserves user-set values in sections that already exist", () => {
    const v01: AppConfig = {
      asr: {
        provider: "azure-stream",
        language: "zh-CN",
        provider_options: { key: "user-key", region: "eastus" },
      },
      polish: {
        provider: "openai-azure",
        mode: "formal",
        provider_options: { key: "user-oai", endpoint: "https://x" },
      },
      inject: { mode: "clipboard" },
      hotkey: { key: "f10", min_hold_ms: 500 },
      sound: { enabled: false },
      autostart: { enabled: false },
    };
    const merged = mergeWithDefaults(v01);
    expect(merged.asr.provider_options.key).toBe("user-key");
    expect(merged.asr.provider_options.region).toBe("eastus");
    expect(merged.polish.mode).toBe("formal");
    expect(merged.polish.provider_options.endpoint).toBe("https://x");
    expect(merged.inject.mode).toBe("clipboard");
    expect(merged.hotkey.key).toBe("f10");
    expect(merged.hotkey.min_hold_ms).toBe(500);
    expect(merged.sound.enabled).toBe(false);
    expect(merged.autostart.enabled).toBe(false);
  });

  it("backfills autostart and sound when missing (pre-v0.1 shapes)", () => {
    // Even older shapes might lack autostart/sound. Belt-and-suspenders.
    const partial = {
      asr: defaultConfig().asr,
      polish: defaultConfig().polish,
      inject: defaultConfig().inject,
      hotkey: defaultConfig().hotkey,
    } as unknown as AppConfig;
    const merged = mergeWithDefaults(partial);
    expect(merged.autostart).toEqual({ enabled: true });
    expect(merged.sound).toEqual({ enabled: true });
    expect(merged.translate).toBeDefined();
  });

  it("backfills hotkey.behavior to push_to_talk for v0.1 hotkey blocks", () => {
    // Existing users had `{key, min_hold_ms}` only. The new behavior /
    // threshold / max_duration fields default to push_to_talk so a
    // running deployment sees zero behavior change after upgrade.
    const v01: AppConfig = {
      asr: defaultConfig().asr,
      polish: defaultConfig().polish,
      inject: defaultConfig().inject,
      hotkey: { key: "f9", min_hold_ms: 250 },
      sound: defaultConfig().sound,
      autostart: defaultConfig().autostart,
    };
    const merged = mergeWithDefaults(v01);
    expect(merged.hotkey.behavior).toBe("push_to_talk");
    expect(merged.hotkey.smart_threshold_ms).toBe(400);
    expect(merged.hotkey.max_duration_ms).toBe(300000);
    // User-set fields untouched.
    expect(merged.hotkey.key).toBe("f9");
    expect(merged.hotkey.min_hold_ms).toBe(250);
  });

  it("does not overwrite an explicit hotkey.behavior value", () => {
    const v02: AppConfig = {
      asr: defaultConfig().asr,
      polish: defaultConfig().polish,
      inject: defaultConfig().inject,
      hotkey: {
        key: "f9",
        min_hold_ms: 250,
        behavior: "toggle",
        smart_threshold_ms: 250,
        max_duration_ms: 60000,
      },
      sound: defaultConfig().sound,
      autostart: defaultConfig().autostart,
    };
    const merged = mergeWithDefaults(v02);
    expect(merged.hotkey.behavior).toBe("toggle");
    expect(merged.hotkey.smart_threshold_ms).toBe(250);
    expect(merged.hotkey.max_duration_ms).toBe(60000);
  });

  it("returns a new object and does not mutate the input", () => {
    const v01 = {
      asr: defaultConfig().asr,
      polish: defaultConfig().polish,
      inject: defaultConfig().inject,
      hotkey: defaultConfig().hotkey,
      sound: defaultConfig().sound,
      autostart: defaultConfig().autostart,
    } as AppConfig;
    expect(v01.translate).toBeUndefined();
    const merged = mergeWithDefaults(v01);
    expect(merged.translate).toBeDefined();
    // Input must remain untouched so callers can rely on the returned value.
    expect(v01.translate).toBeUndefined();
    expect(merged).not.toBe(v01);
  });

  it("preserves unknown top-level keys for forward compatibility", () => {
    const c = {
      ...defaultConfig(),
      experimental_future_section: { foo: "bar" },
    } as AppConfig;
    const merged = mergeWithDefaults(c);
    expect(merged.experimental_future_section).toEqual({ foo: "bar" });
  });
});
