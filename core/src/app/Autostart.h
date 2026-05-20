#pragma once

namespace onekey::app {

// Synchronizes the HKCU\Software\Microsoft\Windows\CurrentVersion\Run
// entry "OneKeyInput" with the requested state. Idempotent.
//   enabled = true  -> write Run value = "<this exe path>"
//   enabled = false -> delete the Run value if present
// Uses HKCU so no admin elevation is needed.
// Returns false on registry failure (logs the error itself).
bool SyncAutostart(bool enabled);

// Reports whether the Run entry currently points at this exe.
bool IsAutostartEnabled();

}  // namespace onekey::app
