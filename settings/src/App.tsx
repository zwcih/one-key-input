import { useEffect, useMemo, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { open as openDialog } from "@tauri-apps/plugin-dialog";
import { DICTS, Dict, Lang } from "./i18n/dicts";
import { AppConfig, defaultConfig, isFirstRun, mergeWithDefaults } from "./config";
import logoZh from "./assets/logo.png";
import logoEn from "./assets/logo-en.png";

const LANG_KEY = "onekey.lang";

type Status =
  | { kind: "idle" }
  | { kind: "testing" }
  | { kind: "saving" }
  | { kind: "ok"; text: string }
  | { kind: "error"; text: string };

interface TestResult {
  component: string;
  ok: boolean;
  message: string;
}

export default function App() {
  const [lang, setLang] = useState<Lang>(() => {
    const saved = localStorage.getItem(LANG_KEY);
    if (saved === "zh" || saved === "en") return saved;
    return navigator.language.toLowerCase().startsWith("zh") ? "zh" : "en";
  });
  const t = DICTS[lang];

  const [cfg, setCfg] = useState<AppConfig | null>(null);
  const [dirty, setDirty] = useState(false);
  const [status, setStatus] = useState<Status>({ kind: "idle" });
  const [firstRun, setFirstRun] = useState(false);

  // --- load on mount ---
  useEffect(() => {
    invoke<AppConfig>("load_config")
      .then((raw) => {
        // Backfill any sections the on-disk config predates (v0.1 → v0.2
        // upgraders are missing `translate`, for instance). Without this
        // the form crashes reading e.g. cfg.translate.enabled and Settings
        // shows a white window. See mergeWithDefaults() in ./config.
        const c = mergeWithDefaults(raw);
        setCfg(c);
        setFirstRun(isFirstRun(c));
      })
      .catch((e: unknown) =>
        setStatus({ kind: "error", text: t.loadFailed + String(e) }),
      );
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    localStorage.setItem(LANG_KEY, lang);
    document.title = t.appTitle;
  }, [lang, t.appTitle]);

  // --- helpers ---
  const update = (mutator: (draft: AppConfig) => void) => {
    if (!cfg) return;
    const next = structuredClone(cfg);
    mutator(next);
    setCfg(next);
    setDirty(true);
  };

  const onSave = async () => {
    if (!cfg) return;

    // Validate credentials against the real upstream services first. If any
    // probe fails, don't write the file — the user's existing config (if
    // any) keeps working, and we surface a useful error.
    setStatus({ kind: "testing" });
    try {
      const results = await invoke<TestResult[]>("test_credentials", { cfg });
      const failed = results.filter((r) => !r.ok);
      if (failed.length > 0) {
        const summary = failed
          .map((r) => `${r.component}: ${r.message}`)
          .join("; ");
        setStatus({
          kind: "error",
          text: t.testFailed + summary + " — " + t.testFailedHint,
        });
        return;
      }
    } catch (e) {
      setStatus({ kind: "error", text: t.testFailed + String(e) });
      return;
    }

    setStatus({ kind: "saving" });
    try {
      await invoke("save_config", { cfg });
      setDirty(false);
      // If we just escaped first-run, offer to start Core.
      if (firstRun && !isFirstRun(cfg)) {
        try {
          await invoke("start_core");
          setStatus({ kind: "ok", text: t.coreStarted });
          setFirstRun(false);
        } catch (e) {
          setStatus({ kind: "error", text: t.coreStartFailed + String(e) });
        }
      } else {
        setStatus({ kind: "ok", text: t.saved });
      }
    } catch (e) {
      setStatus({ kind: "error", text: t.saveFailed + String(e) });
    }
  };

  const onCancel = async () => {
    // Reload from disk, discarding edits.
    try {
      const fresh = mergeWithDefaults(await invoke<AppConfig>("load_config"));
      setCfg(fresh);
      setFirstRun(isFirstRun(fresh));
      setDirty(false);
      setStatus({ kind: "idle" });
    } catch (e) {
      setStatus({ kind: "error", text: t.loadFailed + String(e) });
    }
  };

  const logoSrc = lang === "zh" ? logoZh : logoEn;
  const introText = firstRun ? t.firstRun : t.intro;

  // --- render ---
  if (!cfg) {
    return (
      <div className="app">
        <div className="body">
          {status.kind === "error" ? (
            <p className="intro" style={{ color: "var(--danger)" }}>
              {status.text}
            </p>
          ) : (
            <p className="intro">…</p>
          )}
        </div>
      </div>
    );
  }

  return (
    <div className="app">
      <header className="header">
        <img className="header-logo" src={logoSrc} alt="One-Key Input" />
        <div className="header-actions">
          <button
            className={`lang-btn ${lang === "zh" ? "active" : ""}`}
            onClick={() => setLang("zh")}
          >
            中文
          </button>
          <button
            className={`lang-btn ${lang === "en" ? "active" : ""}`}
            onClick={() => setLang("en")}
          >
            EN
          </button>
        </div>
      </header>

      <main className="body">
        <p className={`intro ${firstRun ? "first-run" : ""}`}>{introText}</p>

        {/* Hotkey */}
        <section className="group">
          <h2>{t.groupHotkey}</h2>
          <p className="group-desc">{t.groupHotkeyDesc}</p>
          <div className="field">
            <label htmlFor="hk-key">{t.hotkeyKey}</label>
            <input
              id="hk-key"
              type="text"
              value={cfg.hotkey.key}
              onChange={(e) => update((d) => (d.hotkey.key = e.target.value))}
            />
          </div>
          <div className="field">
            <label htmlFor="hk-hold">
              {t.hotkeyHoldMs}
              <span className="hint">{t.hotkeyHoldHint}</span>
            </label>
            <input
              id="hk-hold"
              type="number"
              min={0}
              value={cfg.hotkey.min_hold_ms ?? 250}
              onChange={(e) =>
                update((d) => (d.hotkey.min_hold_ms = Number(e.target.value)))
              }
            />
          </div>
          <div className="field">
            <label>
              {t.hotkeyBehavior}
              <span className="hint">{t.hotkeyBehaviorHint}</span>
            </label>
            <div className="radio-row">
              {(
                [
                  ["smart", t.hotkeyBehaviorSmart],
                  ["push_to_talk", t.hotkeyBehaviorPushToTalk],
                  ["toggle", t.hotkeyBehaviorToggle],
                ] as const
              ).map(([val, label]) => (
                <label key={val}>
                  <input
                    type="radio"
                    name="hk-behavior"
                    checked={(cfg.hotkey.behavior ?? "push_to_talk") === val}
                    onChange={() =>
                      update((d) => {
                        d.hotkey.behavior = val;
                      })
                    }
                  />
                  {label}
                </label>
              ))}
            </div>
          </div>
          {(cfg.hotkey.behavior ?? "push_to_talk") === "smart" && (
            <div className="field">
              <label htmlFor="hk-smart-threshold">
                {t.hotkeySmartThresholdMs}
                <span className="hint">{t.hotkeySmartThresholdHint}</span>
              </label>
              <input
                id="hk-smart-threshold"
                type="number"
                min={50}
                max={2000}
                value={cfg.hotkey.smart_threshold_ms ?? 400}
                onChange={(e) =>
                  update(
                    (d) =>
                      (d.hotkey.smart_threshold_ms = Number(e.target.value)),
                  )
                }
              />
            </div>
          )}
          {((cfg.hotkey.behavior ?? "push_to_talk") === "smart" ||
            (cfg.hotkey.behavior ?? "push_to_talk") === "toggle") && (
            <div className="field">
              <label htmlFor="hk-max-duration">
                {t.hotkeyMaxDurationMin}
                <span className="hint">{t.hotkeyMaxDurationHint}</span>
              </label>
              <input
                id="hk-max-duration"
                type="number"
                min={0.5}
                max={30}
                step={0.5}
                value={((cfg.hotkey.max_duration_ms ?? 300000) / 60000)
                  .toString()}
                onChange={(e) =>
                  update(
                    (d) =>
                      (d.hotkey.max_duration_ms = Math.round(
                        Number(e.target.value) * 60000,
                      )),
                  )
                }
              />
            </div>
          )}
        </section>

        {/* ASR */}
        <section className="group">
          <h2>{t.groupAsr}</h2>
          <p className="group-desc">{t.groupAsrDesc}</p>
          <div className="field">
            <label>{t.asrProvider}</label>
            <select
              value={cfg.asr.provider}
              onChange={(e) =>
                update((d) => {
                  d.asr.provider = e.target.value;
                  // First-time switch to sherpa: pre-populate the canonical
                  // default model directory so the user just has to download +
                  // extract there. Issue #12 picks this path. We never
                  // overwrite an existing value.
                  if (
                    e.target.value === "sherpa-paraformer" &&
                    !d.asr.provider_options.model_dir
                  ) {
                    d.asr.provider_options.model_dir =
                      "models\\paraformer-zh-streaming";
                  }
                })
              }
            >
              <option value="azure-stream">{t.asrProviderOptionAzureStream}</option>
              <option value="azure-rest">{t.asrProviderOptionAzureRest}</option>
              <option value="sherpa-paraformer">{t.asrProviderOptionSherpaParaformer}</option>
            </select>
          </div>
          <div className="field">
            <label>{t.asrLanguage}</label>
            <select
              value={cfg.asr.language}
              onChange={(e) =>
                update((d) => (d.asr.language = e.target.value))
              }
            >
              <option value="zh-CN">zh-CN</option>
              <option value="en-US">en-US</option>
              <option value="ja-JP">ja-JP</option>
            </select>
          </div>
          {cfg.asr.provider === "sherpa-paraformer" ? (
            <SherpaAsrFields cfg={cfg} update={update} t={t} />
          ) : (
            <>
              <div className="field">
                <label>{t.asrAzureKey}</label>
                <input
                  type="password"
                  value={(cfg.asr.provider_options.key as string) ?? ""}
                  onChange={(e) =>
                    update((d) => (d.asr.provider_options.key = e.target.value))
                  }
                />
              </div>
              <div className="field">
                <label>{t.asrAzureRegion}</label>
                <input
                  type="text"
                  value={(cfg.asr.provider_options.region as string) ?? ""}
                  onChange={(e) =>
                    update((d) => (d.asr.provider_options.region = e.target.value))
                  }
                />
              </div>
            </>
          )}
        </section>

        {/* Polish */}
        <section className="group">
          <h2>{t.groupPolish}</h2>
          <p className="group-desc">{t.groupPolishDesc}</p>
          <div className="field">
            <label>{t.polishProvider}</label>
            <select
              value={cfg.polish.provider}
              onChange={(e) =>
                update((d) => (d.polish.provider = e.target.value))
              }
            >
              <option value="openai-azure">openai-azure</option>
              <option value="openai">openai</option>
            </select>
          </div>
          <div className="field">
            <label>{t.polishMode}</label>
            <div className="radio-row">
              {(["raw", "tidy", "formal"] as const).map((m) => (
                <label key={m}>
                  <input
                    type="radio"
                    name="polishMode"
                    checked={cfg.polish.mode === m}
                    onChange={() => update((d) => (d.polish.mode = m))}
                  />
                  {m === "raw"
                    ? t.polishModeRaw
                    : m === "tidy"
                      ? t.polishModeTidy
                      : t.polishModeFormal}
                </label>
              ))}
            </div>
          </div>
          <div className="field">
            <label>{t.polishEndpoint}</label>
            <input
              type="text"
              value={(cfg.polish.provider_options.endpoint as string) ?? ""}
              onChange={(e) =>
                update(
                  (d) => (d.polish.provider_options.endpoint = e.target.value),
                )
              }
            />
          </div>
          <div className="field">
            <label>{t.polishKey}</label>
            <input
              type="password"
              value={(cfg.polish.provider_options.key as string) ?? ""}
              onChange={(e) =>
                update((d) => (d.polish.provider_options.key = e.target.value))
              }
            />
          </div>
          <div className="field">
            <label>{t.polishDeployment}</label>
            <input
              type="text"
              value={(cfg.polish.provider_options.deployment as string) ?? ""}
              onChange={(e) =>
                update(
                  (d) =>
                    (d.polish.provider_options.deployment = e.target.value),
                )
              }
            />
          </div>
          <div className="field">
            <label>{t.polishApiVersion}</label>
            <input
              type="text"
              value={(cfg.polish.provider_options.api_version as string) ?? ""}
              onChange={(e) =>
                update(
                  (d) =>
                    (d.polish.provider_options.api_version = e.target.value),
                )
              }
            />
          </div>
          <div className="field">
            <label>{t.polishUseContext}</label>
            <input
              type="checkbox"
              checked={cfg.polish.use_context !== false}
              onChange={(e) =>
                update((d) => (d.polish.use_context = e.target.checked))
              }
            />
          </div>
          <p className="group-desc">{t.polishUseContextDesc}</p>
        </section>

        {/* Translate */}
        <section className="group">
          <h2>{t.groupTranslate}</h2>
          <p className="group-desc">{t.groupTranslateDesc}</p>
          <div className="field">
            <label htmlFor="tr-enabled">{t.translateEnabled}</label>
            <input
              id="tr-enabled"
              type="checkbox"
              checked={cfg.translate?.enabled !== false}
              onChange={(e) =>
                update((d) => {
                  if (!d.translate) {
                    // Backfill from defaults so older configs missing the
                    // translate block get a complete set in one toggle.
                    d.translate = defaultConfig().translate!;
                  }
                  d.translate.enabled = e.target.checked;
                })
              }
            />
          </div>
          <div className="field">
            <label htmlFor="tr-hotkey">
              {t.translateHotkey}
              <span className="hint">{t.translateHotkeyHint}</span>
            </label>
            <input
              id="tr-hotkey"
              type="text"
              value={cfg.translate?.hotkey ?? "f8"}
              onChange={(e) =>
                update((d) => {
                  if (!d.translate) return;
                  d.translate.hotkey = e.target.value;
                })
              }
            />
          </div>
          <div className="field">
            <label htmlFor="tr-target">
              {t.translateTargetLanguage}
              <span className="hint">{t.translateTargetLanguageHint}</span>
            </label>
            <select
              id="tr-target"
              value={cfg.translate?.target_language ?? "en"}
              onChange={(e) =>
                update((d) => {
                  if (!d.translate) return;
                  d.translate.target_language = e.target.value;
                })
              }
            >
              <option value="en">English (en)</option>
              <option value="zh">中文 (zh)</option>
              <option value="ja">日本語 (ja)</option>
              <option value="ko">한국어 (ko)</option>
              <option value="de">Deutsch (de)</option>
              <option value="fr">Français (fr)</option>
              <option value="es">Español (es)</option>
              <option value="it">Italiano (it)</option>
              <option value="pt">Português (pt)</option>
              <option value="ru">Русский (ru)</option>
            </select>
          </div>
          <div className="field">
            <label htmlFor="tr-smart">{t.translateSmartTarget}</label>
            <input
              id="tr-smart"
              type="checkbox"
              checked={cfg.translate?.smart_target === true}
              onChange={(e) =>
                update((d) => {
                  if (!d.translate) return;
                  d.translate.smart_target = e.target.checked;
                })
              }
            />
          </div>
          <p className="group-desc">{t.translateSmartTargetHint}</p>
        </section>

        {/* Inject */}
        <section className="group">
          <h2>{t.groupInject}</h2>
          <p className="group-desc">{t.groupInjectDesc}</p>
          <div className="field">
            <label>{t.injectMode}</label>
            <div className="radio-row">
              {(
                [
                  ["sendinput", t.injectModeSendinput],
                  ["clipboard", t.injectModeClipboard],
                  ["auto", t.injectModeAuto],
                ] as const
              ).map(([m, label]) => (
                <label key={m}>
                  <input
                    type="radio"
                    name="injectMode"
                    checked={cfg.inject.mode === m}
                    onChange={() => update((d) => (d.inject.mode = m))}
                  />
                  {label}
                </label>
              ))}
            </div>
          </div>
        </section>

        {/* Sound */}
        <section className="group">
          <h2>{t.groupSound}</h2>
          <p className="group-desc">{t.groupSoundDesc}</p>
          <div className="field">
            <label>{t.soundEnabled}</label>
            <input
              type="checkbox"
              checked={!!cfg.sound.enabled}
              onChange={(e) =>
                update((d) => (d.sound.enabled = e.target.checked))
              }
            />
          </div>
        </section>

        {/* Startup */}
        <section className="group">
          <h2>{t.groupStartup}</h2>
          <p className="group-desc">{t.groupStartupDesc}</p>
          <div className="field">
            <label>
              {t.autostartEnabled}
              <span className="hint">{t.autostartHint}</span>
            </label>
            <input
              type="checkbox"
              checked={!!cfg.autostart?.enabled}
              onChange={(e) =>
                update((d) => (d.autostart.enabled = e.target.checked))
              }
            />
          </div>
        </section>
      </main>

      <Footer
        t={t}
        status={status}
        dirty={dirty}
        firstRun={firstRun}
        onSave={onSave}
        onCancel={onCancel}
      />
    </div>
  );
}

// Local-ASR provider-specific fields. Lives next to App so it can directly
// mutate cfg via the parent's `update` callback. Test button calls into a
// Tauri command that probes whether sherpa-onnx can actually load the model.
function SherpaAsrFields(props: {
  cfg: AppConfig;
  update: (mutator: (draft: AppConfig) => void) => void;
  t: Dict;
}) {
  const { cfg, update, t } = props;
  const [testing, setTesting] = useState(false);
  const [result, setResult] = useState<
    | { kind: "none" }
    | { kind: "ok"; ms: number }
    | { kind: "err"; msg: string }
  >({ kind: "none" });

  const dir = (cfg.asr.provider_options.model_dir as string | undefined) ?? "";
  const threads =
    (cfg.asr.provider_options.num_threads as number | undefined) ?? 2;

  const onBrowse = async () => {
    try {
      const chosen = await openDialog({
        directory: true,
        multiple: false,
        defaultPath: dir || undefined,
      });
      if (typeof chosen === "string" && chosen.length > 0) {
        update((d) => (d.asr.provider_options.model_dir = chosen));
      }
    } catch (e) {
      // Dialog rejection (e.g. user cancel) is not an error worth surfacing.
      console.warn("sherpa model_dir browse failed:", e);
    }
  };

  const onTest = async () => {
    if (!dir) {
      setResult({ kind: "err", msg: t.asrSherpaModelDir });
      return;
    }
    setTesting(true);
    setResult({ kind: "none" });
    try {
      const ms = await invoke<number>("test_sherpa_model", {
        modelDir: dir,
        numThreads: threads,
      });
      setResult({ kind: "ok", ms });
    } catch (e) {
      setResult({ kind: "err", msg: String(e) });
    } finally {
      setTesting(false);
    }
  };

  return (
    <>
      <div className="field">
        <label>{t.asrSherpaModelDir}</label>
        <input
          type="text"
          value={dir}
          onChange={(e) =>
            update((d) => (d.asr.provider_options.model_dir = e.target.value))
          }
        />
        <button className="btn" type="button" onClick={onBrowse}>
          {t.asrSherpaBrowse}
        </button>
      </div>
      <p className="group-desc">{t.asrSherpaModelDirHint}</p>
      <div className="field">
        <label>{t.asrSherpaThreads}</label>
        <input
          type="number"
          min={1}
          max={16}
          value={threads}
          onChange={(e) =>
            update(
              (d) => (d.asr.provider_options.num_threads = Number(e.target.value)),
            )
          }
        />
      </div>
      <p className="group-desc">{t.asrSherpaThreadsHint}</p>
      <div className="field">
        <label />
        <button
          className="btn"
          type="button"
          onClick={onTest}
          disabled={testing}
        >
          {testing ? t.asrSherpaTesting : t.asrSherpaTest}
        </button>
        {result.kind === "ok" && (
          <span className="status ok">
            {t.asrSherpaTestOk}
            {result.ms}
            {"ms"}
          </span>
        )}
        {result.kind === "err" && (
          <span className="status error">
            {t.asrSherpaTestFail}
            {result.msg}
          </span>
        )}
      </div>
      <p className="group-desc">{t.asrSherpaSetupHelp}</p>
    </>
  );
}

function Footer(props: {
  t: Dict;
  status: Status;
  dirty: boolean;
  firstRun: boolean;
  onSave: () => void;
  onCancel: () => void;
}) {
  const { t, status, dirty, firstRun, onSave, onCancel } = props;
  const statusText = useMemo(() => {
    if (status.kind === "testing") return t.testing;
    if (status.kind === "saving") return t.saving;
    if (status.kind === "ok") return status.text;
    if (status.kind === "error") return status.text;
    return dirty ? t.needSave : "";
  }, [status, dirty, t]);
  const statusClass =
    status.kind === "error" ? "error" : status.kind === "ok" ? "ok" : "";
  const saveLabel = firstRun ? t.startCore : t.save;
  const busy = status.kind === "saving" || status.kind === "testing";

  return (
    <div className="footer">
      <div className={`status ${statusClass}`}>{statusText}</div>
      <button className="btn" onClick={onCancel} disabled={!dirty || busy}>
        {t.cancel}
      </button>
      <button
        className="btn btn-primary"
        onClick={onSave}
        disabled={busy}
      >
        {saveLabel}
      </button>
    </div>
  );
}
