#include "ClipSoul/Win32Util.h"

#include "ClipSoul/HistoryStore.h"

#include <ShlObj.h>
#include <dwmapi.h>

#include <filesystem>

namespace ClipSoul {

std::filesystem::path AppDataDir() {
    PWSTR path = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path))) {
        result = std::filesystem::path(path) / L"ClipSoul";
        CoTaskMemFree(path);
    } else {
        result = std::filesystem::temp_directory_path() / L"ClipSoul";
    }
    std::filesystem::create_directories(result);
    return result;
}

std::wstring FormatWin32Error(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                           FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring message = count ? std::wstring(buffer, count) : L"Unknown error";
    if (buffer) {
        LocalFree(buffer);
    }
    return message;
}

void SetModernWindowAttributes(HWND hwnd) {
    const BOOL dark = FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    const DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    const DWORD backdrop = 2; // DWMSBT_MAINWINDOW, available on supported Windows 11 builds.
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
}

bool SetStartWithWindows(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                        nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    bool ok = false;
    if (enabled) {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        ok = RegSetValueExW(key, L"ClipSoul", 0, REG_SZ, reinterpret_cast<const BYTE*>(path),
                            static_cast<DWORD>((wcslen(path) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        ok = RegDeleteValueW(key, L"ClipSoul") == ERROR_SUCCESS || GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return ok;
}

bool GetStartWithWindows() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    const auto result = RegQueryValueExW(key, L"ClipSoul", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return result;
}

std::wstring KindLabel(int kind) {
    switch (static_cast<ClipboardKind>(kind)) {
    case ClipboardKind::Text:
    case ClipboardKind::Html:
        return L"文本";
    case ClipboardKind::Image:
        return L"图片";
    case ClipboardKind::Files:
        return L"文件";
    case ClipboardKind::Link:
        return L"链接";
    }
    return L"文本";
}

} // namespace ClipSoul
