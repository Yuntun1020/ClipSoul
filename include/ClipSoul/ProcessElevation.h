#pragma once

#include <Windows.h>

namespace ClipSoul {

// True when the current process runs elevated (administrator).
//
// This matters because of UIPI (User Interface Privilege Isolation): a non-elevated process
// cannot send input to elevated windows. ClipSoul could not paste into, or reliably track
// modifier state for, an elevated game or admin tool. The popup also cannot take focus from a
// fullscreen game, which is what made Alt stick.
bool IsProcessElevated();

// Same check on an arbitrary token. Kept separate so it can be unit tested.
bool IsProcessElevatedForTest(HANDLE process_token);

} // namespace ClipSoul
