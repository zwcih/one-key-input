import { describe, it, expect } from "vitest";
import { defaultConfig, isFirstRun, AppConfig } from "../config";

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
    expect(c.polish.temperature).toBe(0.2);
    expect(c.polish.max_tokens).toBe(2000);
    expect(c.polish.use_context).toBe(true);
    expect(c.inject.mode).toBe("sendinput");
    expect(c.hotkey.key).toBe("f9");
    expect(c.hotkey.min_hold_ms).toBe(250);
    expect(c.sound.enabled).toBe(true);
    expect(c.autostart.enabled).toBe(true);
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
