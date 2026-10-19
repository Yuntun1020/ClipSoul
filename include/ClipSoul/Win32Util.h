#pragma once

#include <Windows.h>

#include <filesystem>
#include <string>

namespace ClipSoul {

std::filesystem::path ExecutableDir();
std::filesystem::path DefaultStorageDir();
std::filesystem::path LoadStorageDir();
bool SaveStorageDir(const std::filesystem::path& storage_dir);
std::wstring FormatWin32Error(DWORD error);
void EnableDpiAwareness();
void SetModernWindowAttributes(HWND hwnd);
void SetPopupWindowChromeAttributes(HWND hwnd);
void SetPopupWindowChromeAttributes(HWND hwnd, int theme_mode);
bool IsSystemDarkTheme();
// Drops the cached answer; call when the shell reports a personalization change.
void InvalidateSystemDarkTheme();
bool SetStartWithWindows(bool enabled);
bool GetStartWithWindows();
std::wstring KindLabel(int kind);

} // namespace ClipSoul
