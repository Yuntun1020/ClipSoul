#include "popup/PopupWin32Helpers.h"

#include <filesystem>
#include <iterator>

namespace ClipSoul::popup_detail {

UINT DpiForWindowHandle(HWND hwnd) {
    if (hwnd) {
        if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
            using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
            auto* get_dpi_for_window = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
            if (get_dpi_for_window) {
                const UINT dpi = get_dpi_for_window(hwnd);
                if (dpi != 0) {
                    return dpi;
                }
            }
        }
    }
    const HDC screen = GetDC(nullptr);
    const UINT dpi = screen ? static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX)) : 96;
    if (screen) {
        ReleaseDC(nullptr, screen);
    }
    return dpi == 0 ? 96 : dpi;
}

std::wstring HexWindow(HWND hwnd) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"0x%p", hwnd);
    return buffer;
}

std::wstring WindowClassName(HWND hwnd) {
    wchar_t class_name[128]{};
    if (!hwnd || GetClassNameW(hwnd, class_name, static_cast<int>(std::size(class_name))) <= 0) {
        return L"";
    }
    return class_name;
}

std::wstring WindowTitle(HWND hwnd) {
    wchar_t title[128]{};
    if (!hwnd || GetWindowTextW(hwnd, title, static_cast<int>(std::size(title))) <= 0) {
        return L"";
    }
    return title;
}

std::wstring WindowProcessName(HWND hwnd) {
    DWORD process_id = 0;
    if (!hwnd || GetWindowThreadProcessId(hwnd, &process_id) == 0 || process_id == 0) {
        return L"";
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) {
        return L"";
    }
    wchar_t path[MAX_PATH]{};
    DWORD length = static_cast<DWORD>(std::size(path));
    std::wstring name;
    if (QueryFullProcessImageNameW(process, 0, path, &length) && length > 0) {
        name = std::filesystem::path(std::wstring(path, length)).stem().wstring();
    }
    CloseHandle(process);
    return name;
}

std::wstring RectToLogString(RECT rect) {
    std::wstring value = L"(";
    value += std::to_wstring(rect.left);
    value += L",";
    value += std::to_wstring(rect.top);
    value += L",";
    value += std::to_wstring(rect.right);
    value += L",";
    value += std::to_wstring(rect.bottom);
    value += L")";
    return value;
}

} // namespace ClipSoul::popup_detail
