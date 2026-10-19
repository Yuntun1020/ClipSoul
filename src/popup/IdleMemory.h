#pragma once

#include <Windows.h>

namespace ClipSoul::popup_detail {

// Once the UI has been closed, let Windows page out inactive font/rendering code.
// This changes residency, not committed memory, and never runs while a window is visible.
// The caller uses a single-shot timer so a quick reopen cancels the operation.
inline void TrimIdleWorkingSet(HWND window, UINT_PTR timer) {
    KillTimer(window, timer);
    bool visible = false;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND candidate, LPARAM state) -> BOOL {
        if (IsWindowVisible(candidate)) {
            *reinterpret_cast<bool*>(state) = true;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&visible));
    if (!visible) SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

} // namespace ClipSoul::popup_detail
