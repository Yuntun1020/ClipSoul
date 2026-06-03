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
bool IsSystemDarkTheme();
bool SetStartWithWindows(bool enabled);
bool GetStartWithWindows();
std::wstring KindLabel(int kind);

} // namespace ClipSoul
