#pragma once
#include "../session/EventBus.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace onekey::ui {

// Generates HICONs at runtime so we don't ship a .ico resource. Each icon
// is a stylized microphone, tinted to reflect the session phase:
//   Idle/Done   -> neutral grey
//   Recording   -> red
//   Recognizing -> amber
//   Polishing   -> blue
//   Injecting   -> green
//   Error       -> red with X overlay
//   Paused      -> grey with slash overlay
//
// Caller owns the returned HICON and must DestroyIcon() it.
HICON CreateMicIcon(session::Phase phase, bool paused, int size_px = 32);

}  // namespace onekey::ui
