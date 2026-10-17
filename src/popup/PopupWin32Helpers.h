#pragma once

// Small Win32 helpers shared by the popup window implementation files (internal, not public API).

#include <Windows.h>

#include <string>

namespace ClipSoul::popup_detail {

template <typename T>
void ReleasePtr(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

UINT DpiForWindowHandle(HWND hwnd);
std::wstring HexWindow(HWND hwnd);
std::wstring WindowClassName(HWND hwnd);
std::wstring WindowTitle(HWND hwnd);
std::wstring WindowProcessName(HWND hwnd);
std::wstring RectToLogString(RECT rect);

} // namespace ClipSoul::popup_detail
