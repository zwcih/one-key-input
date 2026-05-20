#pragma once

namespace onekey::app {

// Spawn a fresh copy of this exe (same path, same args), then post WM_QUIT
// to the given thread so the message loop drains and the current process
// exits cleanly. The two processes overlap briefly during shutdown — this
// is fine for our use case (the hotkey hook in the old process is torn
// down before the new one tries to install its own).
//
// Returns false if spawning the new exe failed; in that case the caller
// stays alive.
bool SelfRestart(unsigned long main_tid);

// Launch onekey-settings.exe non-blockingly. Search order:
//   1. ONEKEY_SETTINGS_EXE env var
//   2. Sibling of the running exe
//   3. Dev fallback: ../../../../settings/src-tauri/target/release/onekey-settings.exe
// Returns false if not found.
bool LaunchSettings();

}  // namespace onekey::app
