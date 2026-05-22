// Settings UI backend. Commands:
//   - load_config:     read config.json (creating from config.example.json
//                      on first run), return parsed JSON
//   - save_config:     write the supplied JSON atomically; the Core
//                      process has a file watcher and self-restarts when
//                      it sees the change
//   - test_credentials: probe Azure Speech + the polish endpoint with the
//                      supplied JSON; returns a list of per-component
//                      pass/fail messages so the UI can refuse to save
//                      bad creds before Core ever sees them
//   - start_core:      spawn onekey-core.exe (for first-run flow)
//
// Search order for config.json + onekey-core.exe mirrors the C++ Core's
// logic: env override → sibling of the settings exe → dev fallback to
// build/default/bin.

use serde::Serialize;
use serde_json::Value;
use std::path::{Path, PathBuf};
use std::time::Duration;

#[cfg(windows)]
use std::os::windows::process::CommandExt;

// ----- path resolution -----

fn exe_dir() -> Option<PathBuf> {
    std::env::current_exe().ok().and_then(|p| p.parent().map(Path::to_path_buf))
}

/// Resolve config.json. Tries env, then a series of well-known locations.
fn locate_config() -> PathBuf {
    if let Ok(p) = std::env::var("ONEKEY_CONFIG") {
        return PathBuf::from(p);
    }
    let exe = exe_dir().unwrap_or_else(|| PathBuf::from("."));
    // Release install: config sits next to the Core exe, which is next to
    // the settings exe.
    let sibling = exe.join("config.json");
    if sibling.exists() { return sibling; }

    // Dev mode candidates (settings/src-tauri/target/release/onekey-settings.exe):
    let candidates = [
        // ../../../../config.json (repo root)
        exe.join("..").join("..").join("..").join("..").join("config.json"),
        // ../../../../build/default/bin/config.json (where CMake copies it)
        exe.join("..").join("..").join("..").join("..")
            .join("build").join("default").join("bin").join("config.json"),
    ];
    for c in candidates {
        if c.exists() {
            // canonicalize prettifies path in logs
            return std::fs::canonicalize(&c).unwrap_or(c);
        }
    }
    // If nothing exists yet, default to writing alongside the settings exe.
    sibling
}

fn locate_example_config() -> Option<PathBuf> {
    let exe = exe_dir()?;
    let candidates = [
        exe.join("config.example.json"),
        exe.join("..").join("..").join("..").join("..").join("config.example.json"),
    ];
    candidates.into_iter().find(|p| p.exists())
}

fn locate_core_exe() -> Option<PathBuf> {
    if let Ok(p) = std::env::var("ONEKEY_CORE_EXE") {
        let p = PathBuf::from(p);
        if p.exists() { return Some(p); }
    }
    let exe = exe_dir()?;
    let sibling = exe.join("onekey-core.exe");
    if sibling.exists() { return Some(sibling); }
    // Dev fallback.
    let dev = exe.join("..").join("..").join("..").join("..")
        .join("build").join("default").join("bin").join("onekey-core.exe");
    if dev.exists() {
        return std::fs::canonicalize(&dev).ok();
    }
    None
}

// ----- commands -----

#[tauri::command]
fn load_config() -> Result<Value, String> {
    let path = locate_config();
    eprintln!("[settings] load_config: {}", path.display());

    if !path.exists() {
        // Seed from the example if available, else minimal default.
        if let Some(example) = locate_example_config() {
            let body = std::fs::read_to_string(&example)
                .map_err(|e| format!("read example: {e}"))?;
            // Ensure parent exists before writing.
            if let Some(parent) = path.parent() {
                std::fs::create_dir_all(parent).ok();
            }
            std::fs::write(&path, &body).map_err(|e| format!("seed write: {e}"))?;
        } else {
            // Write a minimal stub the UI can edit; the UI will fill defaults
            // for missing keys anyway.
            if let Some(parent) = path.parent() {
                std::fs::create_dir_all(parent).ok();
            }
            std::fs::write(&path, "{}\n").map_err(|e| format!("init write: {e}"))?;
        }
    }
    let body = std::fs::read_to_string(&path).map_err(|e| format!("read: {e}"))?;
    let json: Value = serde_json::from_str(&body)
        .map_err(|e| format!("parse {}: {e}", path.display()))?;
    Ok(json)
}

#[tauri::command]
fn save_config(cfg: Value) -> Result<(), String> {
    let path = locate_config();
    eprintln!("[settings] save_config: {}", path.display());

    // Preserve unknown top-level keys: merge our `cfg` into whatever's on
    // disk so legacy/extra keys aren't dropped.
    let merged = if path.exists() {
        match std::fs::read_to_string(&path) {
            Ok(body) => match serde_json::from_str::<Value>(&body) {
                Ok(mut on_disk) => { merge(&mut on_disk, &cfg); on_disk }
                Err(_) => cfg,  // unparseable on-disk file: overwrite
            },
            Err(_) => cfg,
        }
    } else {
        cfg
    };

    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent).map_err(|e| format!("mkdir: {e}"))?;
    }

    // Atomic: write to .tmp, rename.
    let tmp = path.with_extension("json.tmp");
    let pretty = serde_json::to_string_pretty(&merged)
        .map_err(|e| format!("serialize: {e}"))?;
    std::fs::write(&tmp, &pretty).map_err(|e| format!("write tmp: {e}"))?;
    std::fs::rename(&tmp, &path).map_err(|e| format!("rename: {e}"))?;
    Ok(())
}

// ----- credential probing -----

#[derive(Serialize)]
pub struct TestResult {
    /// Component name shown to the user, e.g. "Azure Speech".
    component: String,
    ok: bool,
    /// Human-readable status. On failure, includes HTTP code if any.
    message: String,
}

fn http_client() -> reqwest::Client {
    reqwest::Client::builder()
        .timeout(Duration::from_secs(8))
        .build()
        .expect("reqwest client")
}

/// Probe Azure Speech: POST /sts/v1.0/issuetoken returns a JWT if the key
/// + region are valid. 401 = bad key. DNS / 404 = bad region.
async fn probe_azure_speech(key: &str, region: &str) -> TestResult {
    let comp = "Azure Speech".to_string();
    if key.is_empty() || region.is_empty() {
        return TestResult {
            component: comp,
            ok: false,
            message: "key or region is empty".into(),
        };
    }
    let url = format!("https://{region}.api.cognitive.microsoft.com/sts/v1.0/issuetoken");
    let res = http_client()
        .post(&url)
        .header("Ocp-Apim-Subscription-Key", key)
        .header("Content-Length", "0")
        .send()
        .await;
    match res {
        Ok(r) if r.status().is_success() => TestResult {
            component: comp, ok: true, message: "OK".into(),
        },
        Ok(r) => {
            let status = r.status();
            let hint = match status.as_u16() {
                401 | 403 => "key rejected by Azure",
                404       => "endpoint not found — region likely wrong",
                _          => "unexpected response",
            };
            TestResult {
                component: comp,
                ok: false,
                message: format!("HTTP {} — {hint}", status.as_u16()),
            }
        }
        Err(e) => TestResult {
            component: comp,
            ok: false,
            // Reqwest's DNS errors land here when the region produces an
            // invalid hostname.
            message: format!("network error: {e}"),
        },
    }
}

/// Probe an Azure OpenAI deployment by making a real but minimal chat
/// completion call. We can't use the management-plane "get deployment"
/// API because data-plane inference keys aren't authorized for it (they'd
/// 401 even when the key actually works).
async fn probe_azure_openai(
    endpoint: &str, key: &str, deployment: &str, api_version: &str,
) -> TestResult {
    let comp = "Polish (Azure OpenAI)".to_string();
    for (name, val) in [
        ("endpoint", endpoint), ("key", key),
        ("deployment", deployment), ("api_version", api_version),
    ] {
        if val.is_empty() {
            return TestResult {
                component: comp, ok: false,
                message: format!("{name} is empty"),
            };
        }
    }
    let base = endpoint.trim_end_matches('/');
    let url = format!(
        "{base}/openai/deployments/{deployment}/chat/completions?api-version={api_version}"
    );
    let body = serde_json::json!({
        "messages": [{"role": "user", "content": "ping"}],
        "max_completion_tokens": 16,
    });
    let res = http_client()
        .post(&url)
        .header("api-key", key)
        .json(&body)
        .send()
        .await;
    match res {
        Ok(r) if r.status().is_success() => TestResult {
            component: comp, ok: true, message: "OK".into(),
        },
        Ok(r) => {
            let status = r.status();
            let hint = match status.as_u16() {
                401 | 403 => "key rejected by Azure OpenAI",
                404       => "deployment or endpoint not found",
                429       => "rate-limited (key likely valid)",
                _          => "unexpected response",
            };
            TestResult {
                component: comp, ok: false,
                message: format!("HTTP {} — {hint}", status.as_u16()),
            }
        }
        Err(e) => TestResult {
            component: comp, ok: false,
            message: format!("network error: {e}"),
        },
    }
}

/// Probe plain OpenAI: GET https://api.openai.com/v1/models with the
/// bearer token. 200 = valid, 401 = bad key.
async fn probe_openai(key: &str) -> TestResult {
    let comp = "Polish (OpenAI)".to_string();
    if key.is_empty() {
        return TestResult {
            component: comp, ok: false,
            message: "key is empty".into(),
        };
    }
    let res = http_client()
        .get("https://api.openai.com/v1/models")
        .bearer_auth(key)
        .send()
        .await;
    match res {
        Ok(r) if r.status().is_success() => TestResult {
            component: comp, ok: true, message: "OK".into(),
        },
        Ok(r) => TestResult {
            component: comp, ok: false,
            message: format!("HTTP {} — key rejected", r.status().as_u16()),
        },
        Err(e) => TestResult {
            component: comp, ok: false,
            message: format!("network error: {e}"),
        },
    }
}

#[tauri::command]
async fn test_credentials(cfg: Value) -> Result<Vec<TestResult>, String> {
    let mut results = Vec::new();

    // ASR probe.
    let asr_provider = cfg.pointer("/asr/provider")
        .and_then(|v| v.as_str()).unwrap_or("");
    if asr_provider.starts_with("azure") {
        let key = cfg.pointer("/asr/provider_options/key")
            .and_then(|v| v.as_str()).unwrap_or("");
        let region = cfg.pointer("/asr/provider_options/region")
            .and_then(|v| v.as_str()).unwrap_or("");
        results.push(probe_azure_speech(key, region).await);
    }

    // Polish probe.
    let polish_provider = cfg.pointer("/polish/provider")
        .and_then(|v| v.as_str()).unwrap_or("");
    match polish_provider {
        "openai-azure" => {
            let opts = cfg.pointer("/polish/provider_options").cloned().unwrap_or(Value::Null);
            let s = |k: &str| opts.get(k).and_then(|v| v.as_str()).unwrap_or("").to_string();
            results.push(
                probe_azure_openai(&s("endpoint"), &s("key"),
                                   &s("deployment"), &s("api_version")).await
            );
        }
        "openai" => {
            let key = cfg.pointer("/polish/provider_options/key")
                .and_then(|v| v.as_str()).unwrap_or("");
            results.push(probe_openai(key).await);
        }
        _ => {} // unknown provider — skip silently
    }

    Ok(results)
}

#[tauri::command]
fn start_core() -> Result<(), String> {
    let exe = locate_core_exe().ok_or_else(|| {
        "onekey-core.exe not found (checked env, sibling, dev path)".to_string()
    })?;
    eprintln!("[settings] start_core: {}", exe.display());

    let mut cmd = std::process::Command::new(&exe);
    // Detach: don't inherit our stdin/out, don't tie lifetime to us.
    cmd.stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());

    // CREATE_NO_WINDOW (0x08000000) so the console window doesn't flash.
    #[cfg(windows)]
    cmd.creation_flags(0x0800_0000);

    cmd.spawn().map_err(|e| format!("spawn: {e}"))?;
    Ok(())
}

/// Shallow merge: for each key in `src`, replace the value in `dst`.
/// Nested objects merge recursively; primitives + arrays are replaced.
fn merge(dst: &mut Value, src: &Value) {
    match (dst, src) {
        (Value::Object(d), Value::Object(s)) => {
            for (k, v) in s {
                merge(d.entry(k.clone()).or_insert(Value::Null), v);
            }
        }
        (d, s) => { *d = s.clone(); }
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![
            load_config,
            save_config,
            test_credentials,
            start_core
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

// ---------------------------------------------------------------------------
// Unit tests
//
// Only pure helpers are exercised here. Anything that touches the Tauri
// runtime (#[tauri::command] handlers, plugin init) is intentionally out of
// scope — they're integration-tested by running the app.
// ---------------------------------------------------------------------------
#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    // --- merge() ---

    #[test]
    fn merge_replaces_primitives() {
        let mut dst = json!({"a": 1, "b": "old"});
        let src = json!({"b": "new", "c": true});
        merge(&mut dst, &src);
        assert_eq!(dst, json!({"a": 1, "b": "new", "c": true}));
    }

    #[test]
    fn merge_recurses_into_objects() {
        let mut dst = json!({
            "asr": { "provider": "azure-rest", "language": "zh-CN" },
            "unknown": "keep-me",
        });
        let src = json!({
            "asr": { "language": "en-US" },
        });
        merge(&mut dst, &src);
        // Existing nested key preserved, src key overridden, unknown top-level
        // key untouched — this is the contract that lets save_config preserve
        // legacy schema fields.
        assert_eq!(dst["asr"]["provider"], json!("azure-rest"));
        assert_eq!(dst["asr"]["language"], json!("en-US"));
        assert_eq!(dst["unknown"], json!("keep-me"));
    }

    #[test]
    fn merge_replaces_arrays_wholesale() {
        let mut dst = json!({"xs": [1, 2, 3]});
        let src = json!({"xs": [9]});
        merge(&mut dst, &src);
        assert_eq!(dst, json!({"xs": [9]}));
    }

    #[test]
    fn merge_overwrites_primitive_with_object() {
        let mut dst = json!({"v": 1});
        let src = json!({"v": {"nested": true}});
        merge(&mut dst, &src);
        assert_eq!(dst, json!({"v": {"nested": true}}));
    }

    #[test]
    fn merge_inserts_new_object_into_null_dst_entry() {
        // Exercises the `entry().or_insert(Value::Null)` branch.
        let mut dst = json!({});
        let src = json!({"polish": {"mode": "tidy"}});
        merge(&mut dst, &src);
        assert_eq!(dst["polish"]["mode"], json!("tidy"));
    }

    // --- locate_config() env override ---

    #[test]
    fn locate_config_respects_env_override() {
        // SAFETY: tests run in the same process; `cargo test` runs them on
        // a thread pool but env vars are process-global. We restrict env
        // mutation to this single test and restore it after.
        let prev = std::env::var("ONEKEY_CONFIG").ok();
        std::env::set_var("ONEKEY_CONFIG", "/tmp/onekey-override.json");
        let p = locate_config();
        assert_eq!(p, std::path::PathBuf::from("/tmp/onekey-override.json"));
        match prev {
            Some(v) => std::env::set_var("ONEKEY_CONFIG", v),
            None    => std::env::remove_var("ONEKEY_CONFIG"),
        }
    }

    // --- exe_dir() ---

    #[test]
    fn exe_dir_resolves_to_some_path() {
        // `current_exe` is always available during `cargo test`.
        let d = exe_dir();
        assert!(d.is_some(), "exe_dir should return Some during cargo test");
        assert!(d.unwrap().is_absolute());
    }

    // --- TestResult Serialize roundtrip ---

    #[test]
    fn test_result_serializes_with_expected_field_names() {
        let r = TestResult {
            component: "Azure Speech".into(),
            ok: false,
            message: "HTTP 401 — key rejected".into(),
        };
        let s = serde_json::to_value(&r).unwrap();
        assert_eq!(s["component"], json!("Azure Speech"));
        assert_eq!(s["ok"], json!(false));
        assert!(s["message"].as_str().unwrap().contains("401"));
    }
}

