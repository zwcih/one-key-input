// Simple typed i18n. Two dictionaries (zh, en) with the same keys.
// Adding a key in one dict and forgetting the other will TypeError at build.

export type Lang = "zh" | "en";

export type Dict = {
  appTitle: string;
  intro: string;
  firstRun: string;

  groupHotkey: string;
  groupHotkeyDesc: string;
  groupAsr: string;
  groupAsrDesc: string;
  groupPolish: string;
  groupPolishDesc: string;
  groupInject: string;
  groupInjectDesc: string;
  groupSound: string;
  groupSoundDesc: string;
  groupStartup: string;
  groupStartupDesc: string;
  groupTranslate: string;
  groupTranslateDesc: string;

  hotkeyKey: string;
  hotkeyHoldMs: string;
  hotkeyHoldHint: string;

  asrProvider: string;
  asrLanguage: string;
  asrAzureKey: string;
  asrAzureRegion: string;

  polishProvider: string;
  polishMode: string;
  polishModeRaw: string;
  polishModeTidy: string;
  polishModeFormal: string;
  polishEndpoint: string;
  polishKey: string;
  polishDeployment: string;
  polishApiVersion: string;
  polishUseContext: string;
  polishUseContextDesc: string;

  injectMode: string;
  injectModeSendinput: string;
  injectModeClipboard: string;
  injectModeAuto: string;

  soundEnabled: string;
  autostartEnabled: string;
  autostartHint: string;

  translateEnabled: string;
  translateHotkey: string;
  translateHotkeyHint: string;
  translateTargetLanguage: string;
  translateTargetLanguageHint: string;
  translateSmartTarget: string;
  translateSmartTargetHint: string;

  cancel: string;
  save: string;
  saving: string;
  testing: string;
  saved: string;
  startCore: string;
  needSave: string;
  saveFailed: string;
  loadFailed: string;
  coreStarted: string;
  coreStartFailed: string;
  testFailed: string;
  testFailedHint: string;
};

export const zh: Dict = {
  appTitle: "One-Key Input 设置",
  intro: "修改下方的设置后点击「保存」。保存后程序会自动重启以应用新的配置。",
  firstRun:
    "欢迎使用 One-Key Input！首次使用需要先填写下方的 Azure 语音服务和 Polish 模型的凭据，然后点击「保存」。保存成功后程序将自动启动。",

  groupHotkey: "快捷键",
  groupHotkeyDesc: "按住时录音，松开后转录。",
  groupAsr: "语音识别（ASR）",
  groupAsrDesc: "目前支持 Azure Speech。",
  groupPolish: "文本润色（Polish）",
  groupPolishDesc: "用 LLM 把识别结果整理或改写为可注入文本。",
  groupInject: "文本注入",
  groupInjectDesc: "把最终文本送进焦点窗口。",
  groupSound: "声音提示",
  groupSoundDesc: "录音开始/结束的提示音。",
  groupStartup: "启动",
  groupStartupDesc: "Windows 登录时是否自动启动。",
  groupTranslate: "翻译模式",
  groupTranslateDesc:
    "另一个热键，按住说母语 → 松开后输出目标语言。复用润色管线（同 ASR、同上下文、同 LLM），仅替换 prompt。润色风格沿用上方的「默认模式」（Raw/Tidy/Formal）。",

  hotkeyKey: "快捷键",
  hotkeyHoldMs: "最短按住时间 (ms)",
  hotkeyHoldHint: "防误触；少于这个时长视为误按。",

  asrProvider: "服务提供方",
  asrLanguage: "语言",
  asrAzureKey: "Azure Speech Key",
  asrAzureRegion: "Azure Region",

  polishProvider: "服务提供方",
  polishMode: "默认模式",
  polishModeRaw: "Raw（不润色）",
  polishModeTidy: "Tidy（默认）",
  polishModeFormal: "Formal（改写）",
  polishEndpoint: "Endpoint",
  polishKey: "API Key",
  polishDeployment: "Deployment",
  polishApiVersion: "API Version",
  polishUseContext: "上下文感知（推荐）",
  polishUseContextDesc:
    "按下热键时，将当前前台窗口的标题、应用名、光标周围文字等附加到润色 prompt，让结果更贴合场景。关闭后完全不抓取（节省 ~80-150ms 与少量 tokens）。",

  injectMode: "注入方式",
  injectModeSendinput: "SendInput",
  injectModeClipboard: "Clipboard",
  injectModeAuto: "Auto（首选 SendInput，失败回落 Clipboard）",

  soundEnabled: "启用提示音",
  autostartEnabled: "开机自动启动",
  autostartHint: "登录 Windows 后自动启动 One-Key Input。",

  translateEnabled: "启用翻译热键",
  translateHotkey: "翻译热键",
  translateHotkeyHint: "默认 F8，与上方的录音热键并存（不要重复）。",
  translateTargetLanguage: "目标语言",
  translateTargetLanguageHint:
    "ASR 识别出的语言不是这里选的目标语言时才会触发翻译。例如目标=英文：说中文翻英文，说英文不翻。",
  translateSmartTarget: "智能目标语言（实验性，默认关）",
  translateSmartTargetHint:
    "开启后将根据当前焦点窗口的内容自动判断目标语言（如对方语言），可能不准。准确性优先请关闭此项并固定上方的「目标语言」。",

  cancel: "取消",
  save: "保存",
  saving: "保存中...",
  testing: "验证凭据中...",
  saved: "已保存。Core 会在检测到变更后自动重启。",
  startCore: "保存并启动",
  needSave: "有未保存的修改",
  saveFailed: "保存失败：",
  loadFailed: "加载配置失败：",
  coreStarted: "已启动 Core。",
  coreStartFailed: "启动 Core 失败：",
  testFailed: "凭据验证失败，未保存：",
  testFailedHint: "请检查 key / region / endpoint，然后再次保存。",
};

export const en: Dict = {
  appTitle: "One-Key Input — Settings",
  intro:
    "Edit settings below and click Save. The Core will auto-restart to pick up your changes.",
  firstRun:
    "Welcome! First-time setup: fill in your Azure Speech credentials and your Polish LLM credentials below, then click Save. Core will launch automatically.",

  groupHotkey: "Hotkey",
  groupHotkeyDesc: "Hold to record, release to transcribe.",
  groupAsr: "Speech Recognition",
  groupAsrDesc: "Currently supports Azure Speech.",
  groupPolish: "Polish",
  groupPolishDesc: "LLM that tidies / rewrites the raw transcript.",
  groupInject: "Text Injection",
  groupInjectDesc: "How the final text reaches the focused window.",
  groupSound: "Sounds",
  groupSoundDesc: "Chirps when recording starts / stops.",
  groupStartup: "Startup",
  groupStartupDesc: "Whether to launch at Windows login.",
  groupTranslate: "Translation Mode",
  groupTranslateDesc:
    "A second hotkey: hold to speak in your native language, release to inject the target-language version into the focused window. Reuses the same ASR, context capture, and LLM — only the prompt changes. Polish style above (Raw/Tidy/Formal) carries over.",

  hotkeyKey: "Key",
  hotkeyHoldMs: "Min hold (ms)",
  hotkeyHoldHint: "Anti-mistap: shorter taps are ignored.",

  asrProvider: "Provider",
  asrLanguage: "Language",
  asrAzureKey: "Azure Speech Key",
  asrAzureRegion: "Azure Region",

  polishProvider: "Provider",
  polishMode: "Default mode",
  polishModeRaw: "Raw (no polish)",
  polishModeTidy: "Tidy (default)",
  polishModeFormal: "Formal (rewrite)",
  polishEndpoint: "Endpoint",
  polishKey: "API Key",
  polishDeployment: "Deployment",
  polishApiVersion: "API Version",
  polishUseContext: "Context-aware polish (recommended)",
  polishUseContextDesc:
    "On hotkey press, attach the focused window's title, app name, and text around the caret to the polish prompt — makes the result fit the situation. Disabling skips capture entirely (saves ~80-150ms and a few tokens).",

  injectMode: "Method",
  injectModeSendinput: "SendInput",
  injectModeClipboard: "Clipboard",
  injectModeAuto: "Auto (SendInput, fall back to Clipboard)",

  soundEnabled: "Enable sound cues",
  autostartEnabled: "Start at Windows login",
  autostartHint: "Launch One-Key Input automatically after you log in.",

  translateEnabled: "Enable the translation hotkey",
  translateHotkey: "Translate hotkey",
  translateHotkeyHint:
    "Defaults to F8. Coexists with the dictation hotkey above — do not pick the same key for both.",
  translateTargetLanguage: "Target language",
  translateTargetLanguageHint:
    "Translation triggers only when the recognized language differs from this. Example: target = English → Chinese gets translated, English stays as-is.",
  translateSmartTarget: "Smart target language (experimental, off by default)",
  translateSmartTargetHint:
    "Detects the language of the focused window's surrounding text and overrides the target. Best-effort: may be wrong. For consistent behavior, leave this off and pin the target language above.",

  cancel: "Cancel",
  save: "Save",
  saving: "Saving...",
  testing: "Verifying credentials...",
  saved: "Saved. Core will auto-restart shortly.",
  startCore: "Save and start",
  needSave: "Unsaved changes",
  saveFailed: "Save failed: ",
  loadFailed: "Failed to load config: ",
  coreStarted: "Core started.",
  coreStartFailed: "Failed to start Core: ",
  testFailed: "Credential check failed — not saved: ",
  testFailedHint: "Fix the key / region / endpoint and try saving again.",
};

export const DICTS: Record<Lang, Dict> = { zh, en };
