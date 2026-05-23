#pragma once

#include <Windows.h>

#include <filesystem>
#include <string>

namespace ClipSoul {

std::filesystem::path AppDataDir();
std::wstring FormatWin32Error(DWORD error);
void SetModernWindowAttributes(HWND hwnd);
bool SetStartWithWindows(bool enabled);
bool GetStartWithWindows();
std::wstring KindLabel(int kind);

} // namespace ClipSoul
