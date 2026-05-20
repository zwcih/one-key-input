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
      [k: string]: unknown;
    };
  };
  polish: {
    provider: string;
    mode: "raw" | "tidy" | "formal" | string;
    temperature?: number;
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
  hotkey: { key: string; min_hold_ms?: number };
  sound: { enabled: boolean };
  autostart: { enabled: boolean };
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
      temperature: 0.2,
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
    hotkey: { key: "f9", min_hold_ms: 250 },
    sound: { enabled: true },
    autostart: { enabled: true },
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
  if (c.polish.provider.startsWith("openai")) {
    if (azurePlaceholder(c.polish.provider_options.key as string | undefined))
      return true;
  }
  return false;
}
