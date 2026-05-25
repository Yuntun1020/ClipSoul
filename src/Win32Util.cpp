#include "ClipSoul/Win32Util.h"

#include "ClipSoul/HistoryStore.h"
#include "ClipSoul/StorageConfig.h"
#include "ClipSoul/TextUtil.h"

#include <ShlObj.h>
#include <dwmapi.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace ClipSoul {
namespace {
enum AccentState {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
};

struct AccentPolicy {
    int accent_state = 0;
    int accent_flags = 0;
    int gradient_color = 0;
    int animation_id = 0;
};

struct WindowCompositionAttributeData {
    int attribute = 0;
    void* data = nullptr;
    SIZE_T size_of_data = 0;
};

void EnableAcrylicBlur(HWND hwnd, bool dark) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) {
        return;
    }

    using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WindowCompositionAttributeData*);
    auto* set_window_composition_attribute = reinterpret_cast<SetWindowCompositionAttributeFn>(
        GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!set_window_composition_attribute) {
        return;
    }

    AccentPolicy accent;
    accent.accent_state = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    accent.accent_flags = 2;
    accent.gradient_color = dark ? 0x66100F0D : 0x33F8FBFF; // AABBGGRR.

    WindowCompositionAttributeData data;
    data.attribute = 19; // WCA_ACCENT_POLICY
    data.data = &accent;
    data.size_of_data = sizeof(accent);
    set_window_composition_attribute(hwnd, &data);
}
} // namespace

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

std::filesystem::path ExecutableDir() {
    std::vector<wchar_t> buffer(MAX_PATH);
    DWORD length = 0;
    for (;;) {
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::filesystem::current_path();
        }
        if (length < buffer.size() - 1) {
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

std::filesystem::path DefaultStorageDir() {
    return ExecutableDir();
}

std::filesystem::path StorageConfigPath() {
    return ExecutableDir() / L"clipsoul.storage";
}

std::filesystem::path LoadStorageDir() {
    std::string bytes;
    std::ifstream file(StorageConfigPath(), std::ios::binary);
    if (file) {
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    const std::wstring configured = Utf8ToWide(bytes);
    auto storage_dir = ResolveStorageDirectory(configured, DefaultStorageDir());
    std::filesystem::create_directories(storage_dir);
    return storage_dir;
}

bool SaveStorageDir(const std::filesystem::path& storage_dir) {
    try {
        std::filesystem::create_directories(storage_dir);
        std::ofstream file(StorageConfigPath(), std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        const auto bytes = WideToUtf8(SerializeStorageDirectory(storage_dir));
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return file.good();
    } catch (...) {
        return false;
    }
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

void EnableDpiAwareness() {
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto* set_context = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (set_context && set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return;
        }
    }
    SetProcessDPIAware();
}

void SetModernWindowAttributes(HWND hwnd) {
    const bool system_dark = IsSystemDarkTheme();
    EnableAcrylicBlur(hwnd, system_dark);

    const BOOL dark = system_dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    const DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    const DWORD backdrop = 3; // DWMSBT_TRANSIENTWINDOW, a lighter acrylic-like popup backdrop on Windows 11.
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
}

bool IsSystemDarkTheme() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD value = 1;
    DWORD size = sizeof(value);
    const bool ok = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                                     reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok && value == 0;
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
