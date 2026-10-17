#include "popup/CaretLocator.h"

#include "ClipSoul/PopupLayout.h"
#include "popup/PopupWin32Helpers.h"

#include <oleacc.h>
#include <oleauto.h>
#include <UIAutomationClient.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace ClipSoul::popup_detail {

constexpr DWORD kAttachParentProcess = static_cast<DWORD>(-1);

const wchar_t* TextAvoidSourceName(TextAvoidSource source) {
    switch (source) {
    case TextAvoidSource::GuiCaret:
        return L"gui-caret";
    case TextAvoidSource::MsaaCaret:
        return L"msaa-caret";
    case TextAvoidSource::ConsoleCursor:
        return L"console-cursor";
    case TextAvoidSource::JavaAccessBridgeCaret:
        return L"java-access-bridge-caret";
    case TextAvoidSource::VisualTerminalCaret:
        return L"visual-terminal-caret";
    case TextAvoidSource::AutomationText2Caret:
        return L"uia-text2-caret";
    case TextAvoidSource::AutomationText2Selection:
        return L"uia-text2-selection";
    case TextAvoidSource::AutomationTextSelection:
        return L"uia-text-selection";
    case TextAvoidSource::AutomationTerminalVisibleLine:
        return L"uia-terminal-visible-line";
    case TextAvoidSource::AutomationElementRect:
        return L"uia-element-rect";
    case TextAvoidSource::None:
    default:
        return L"none";
    }
}

bool TryGetGuiThreadInfo(DWORD thread_id, GUITHREADINFO& info) {
    if (thread_id == 0) {
        return false;
    }
    info = GUITHREADINFO{sizeof(info)};
    return GetGUIThreadInfo(thread_id, &info) == TRUE;
}

bool TryGetCaretAnchorFromThread(DWORD thread_id, RECT& avoid, TextAvoidSource& source) {
    GUITHREADINFO info{};
    if (!TryGetGuiThreadInfo(thread_id, info) || !info.hwndCaret ||
        IsRectEmpty(&info.rcCaret) || (info.flags & GUI_CARETBLINKING) == 0) {
        return false;
    }
    POINT top_left{info.rcCaret.left, info.rcCaret.top};
    POINT bottom_right{info.rcCaret.right, info.rcCaret.bottom};
    if (!ClientToScreen(info.hwndCaret, &top_left) || !ClientToScreen(info.hwndCaret, &bottom_right)) {
        return false;
    }
    avoid = RECT{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
    source = TextAvoidSource::GuiCaret;
    return true;
}

bool TryGetCaretAnchor(HWND target, RECT& avoid, TextAvoidSource& source) {
    HWND foreground = GetForegroundWindow();
    if (foreground && TryGetCaretAnchorFromThread(GetWindowThreadProcessId(foreground, nullptr), avoid, source)) {
        return true;
    }
    if (target && target != foreground &&
        TryGetCaretAnchorFromThread(GetWindowThreadProcessId(target, nullptr), avoid, source)) {
        return true;
    }
    return false;
}

bool TryGetMsaaCaretAnchorFromWindow(HWND hwnd, RECT& avoid, TextAvoidSource& source) {
    if (!hwnd) {
        return false;
    }
    IAccessible* accessible = nullptr;
    if (FAILED(AccessibleObjectFromWindow(hwnd, OBJID_CARET, IID_IAccessible,
                                          reinterpret_cast<void**>(&accessible))) ||
        !accessible) {
        return false;
    }

    VARIANT child{};
    child.vt = VT_I4;
    child.lVal = CHILDID_SELF;
    long x = 0;
    long y = 0;
    long width = 0;
    long height = 0;
    const bool ok = SUCCEEDED(accessible->accLocation(&x, &y, &width, &height, child)) &&
                    width >= 0 && height > 0;
    ReleasePtr(accessible);
    if (!ok) {
        return false;
    }

    avoid = RECT{x, y, x + std::max<long>(1, width), y + height};
    source = TextAvoidSource::MsaaCaret;
    return PopupTextRangeRectUsable(avoid);
}

bool TryGetMsaaCaretAnchorFromThread(DWORD thread_id, RECT& avoid, TextAvoidSource& source) {
    GUITHREADINFO info{};
    if (!TryGetGuiThreadInfo(thread_id, info)) {
        return false;
    }
    if (info.hwndFocus && TryGetMsaaCaretAnchorFromWindow(info.hwndFocus, avoid, source)) {
        return true;
    }
    if (info.hwndCaret && info.hwndCaret != info.hwndFocus &&
        TryGetMsaaCaretAnchorFromWindow(info.hwndCaret, avoid, source)) {
        return true;
    }
    return false;
}

bool TryGetMsaaCaretAnchor(HWND target, RECT& avoid, TextAvoidSource& source) {
    HWND foreground = GetForegroundWindow();
    if (foreground &&
        (TryGetMsaaCaretAnchorFromThread(GetWindowThreadProcessId(foreground, nullptr), avoid, source) ||
         TryGetMsaaCaretAnchorFromWindow(foreground, avoid, source))) {
        return true;
    }
    if (target && target != foreground &&
        (TryGetMsaaCaretAnchorFromThread(GetWindowThreadProcessId(target, nullptr), avoid, source) ||
         TryGetMsaaCaretAnchorFromWindow(target, avoid, source))) {
        return true;
    }
    return false;
}

bool TryGetConsoleAnchor(HWND target, RECT& avoid, TextAvoidSource& source) {
    if (!target) {
        return false;
    }
    DWORD process_id = 0;
    GetWindowThreadProcessId(target, &process_id);
    if (process_id == 0) {
        return false;
    }

    bool attached = false;
    if (GetConsoleWindow() != target) {
        FreeConsole();
        attached = AttachConsole(process_id) == TRUE;
        if (!attached) {
            AttachConsole(kAttachParentProcess);
            return false;
        }
    }

    const HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    CONSOLE_SCREEN_BUFFER_INFO info{};
    CONSOLE_FONT_INFO font{};
    CONSOLE_SELECTION_INFO selection{};
    const bool has_selection = GetConsoleSelectionInfo(&selection) == TRUE;
    const bool ok = output != INVALID_HANDLE_VALUE &&
                    GetConsoleScreenBufferInfo(output, &info) &&
                    GetCurrentConsoleFont(output, FALSE, &font);
    if (ok) {
        COORD cell = PopupConsoleAnchorCell(has_selection ? selection : CONSOLE_SELECTION_INFO{},
                                            info.dwCursorPosition);
        if (info.dwSize.X > 0 && info.dwSize.Y > 0) {
            cell.X = std::clamp(cell.X, info.srWindow.Left, info.srWindow.Right);
            cell.Y = std::clamp(cell.Y, info.srWindow.Top, info.srWindow.Bottom);
        }
        POINT origin{0, 0};
        ClientToScreen(target, &origin);
        const POINT anchor = PopupConsoleCellAnchor(origin, cell, info.srWindow, font.dwFontSize);
        const int cell_width = std::max(1, static_cast<int>(font.dwFontSize.X));
        const int cell_height = std::max(1, static_cast<int>(font.dwFontSize.Y));
        avoid = RECT{anchor.x - cell_width, anchor.y, anchor.x, anchor.y + cell_height};
        source = TextAvoidSource::ConsoleCursor;
    }
    if (output != INVALID_HANDLE_VALUE) {
        CloseHandle(output);
    }

    if (attached) {
        FreeConsole();
        AttachConsole(kAttachParentProcess);
    }
    return ok;
}

constexpr int kJabShortStringSize = 256;
constexpr int kJabMaxStringSize = 1024;
using JabObject = long long;

struct JabAccessibleContextInfo {
    wchar_t name[kJabMaxStringSize];
    wchar_t description[kJabMaxStringSize];
    wchar_t role[kJabShortStringSize];
    wchar_t role_en_US[kJabShortStringSize];
    wchar_t states[kJabShortStringSize];
    wchar_t states_en_US[kJabShortStringSize];
    int indexInParent;
    int childrenCount;
    int x;
    int y;
    int width;
    int height;
    BOOL accessibleComponent;
    BOOL accessibleAction;
    BOOL accessibleSelection;
    BOOL accessibleText;
    BOOL accessibleInterfaces;
};

struct JabAccessibleTextInfo {
    int charCount;
    int caretIndex;
    int indexAtPoint;
};

struct JabAccessibleTextRectInfo {
    int x;
    int y;
    int width;
    int height;
};

struct JavaAccessBridgeApi {
    HMODULE module = nullptr;
    void(WINAPI* windows_run)() = nullptr;
    BOOL(WINAPI* is_java_window)(HWND) = nullptr;
    BOOL(WINAPI* get_context_with_focus)(HWND, long*, JabObject*) = nullptr;
    BOOL(WINAPI* get_context_info)(long, JabObject, JabAccessibleContextInfo*) = nullptr;
    BOOL(WINAPI* get_text_info)(long, JabObject, JabAccessibleTextInfo*, int, int) = nullptr;
    BOOL(WINAPI* get_caret_location)(long, JabObject, JabAccessibleTextRectInfo*, int) = nullptr;
    void(WINAPI* release_java_object)(long, JabObject) = nullptr;
    bool load_attempted = false;
    bool enable_attempted = false;
};

std::wstring JavaAccessBridgeDllName() {
    return L"WindowsAccessBridge-64.dll";
}

bool LoadJavaAccessBridgeApi(JavaAccessBridgeApi& api) {
    if (api.load_attempted) {
        return api.module && api.is_java_window && api.get_context_with_focus && api.get_context_info &&
               api.get_text_info && api.get_caret_location && api.release_java_object;
    }
    api.load_attempted = true;
    api.module = LoadLibraryW(JavaAccessBridgeDllName().c_str());
    if (!api.module) {
        return false;
    }
    api.windows_run = reinterpret_cast<decltype(api.windows_run)>(GetProcAddress(api.module, "Windows_run"));
    api.is_java_window = reinterpret_cast<decltype(api.is_java_window)>(GetProcAddress(api.module, "isJavaWindow"));
    api.get_context_with_focus =
        reinterpret_cast<decltype(api.get_context_with_focus)>(GetProcAddress(api.module, "getAccessibleContextWithFocus"));
    api.get_context_info =
        reinterpret_cast<decltype(api.get_context_info)>(GetProcAddress(api.module, "getAccessibleContextInfo"));
    api.get_text_info =
        reinterpret_cast<decltype(api.get_text_info)>(GetProcAddress(api.module, "getAccessibleTextInfo"));
    api.get_caret_location =
        reinterpret_cast<decltype(api.get_caret_location)>(GetProcAddress(api.module, "getCaretLocation"));
    api.release_java_object =
        reinterpret_cast<decltype(api.release_java_object)>(GetProcAddress(api.module, "releaseJavaObject"));
    if (api.windows_run) {
        api.windows_run();
    }
    return api.is_java_window && api.get_context_with_focus && api.get_context_info &&
           api.get_text_info && api.get_caret_location && api.release_java_object;
}

std::optional<std::filesystem::path> FindJavaAccessBridgeSwitch() {
    wchar_t path_buffer[32768]{};
    const DWORD length = SearchPathW(nullptr, L"jabswitch.exe", nullptr, static_cast<DWORD>(std::size(path_buffer)),
                                     path_buffer, nullptr);
    if (length > 0 && length < std::size(path_buffer)) {
        return std::filesystem::path(path_buffer);
    }

    wchar_t program_files[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"ProgramFiles", program_files, static_cast<DWORD>(std::size(program_files))) > 0) {
        std::error_code error;
        const std::filesystem::path java_root = std::filesystem::path(program_files) / L"Java";
        if (std::filesystem::exists(java_root, error)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     java_root, std::filesystem::directory_options::skip_permission_denied, error)) {
                if (!error && entry.is_regular_file(error) && entry.path().filename() == L"jabswitch.exe") {
                    return entry.path();
                }
                error.clear();
            }
        }
    }

    wchar_t java_home[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"JAVA_HOME", java_home, static_cast<DWORD>(std::size(java_home))) > 0) {
        const std::filesystem::path candidate = std::filesystem::path(java_home) / L"bin" / L"jabswitch.exe";
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::wstring QuoteCommandPath(const std::filesystem::path& path) {
    std::wstring value = L"\"";
    value += path.wstring();
    value += L"\"";
    return value;
}

bool TryEnableJavaAccessBridge() {
    const auto jabswitch = FindJavaAccessBridgeSwitch();
    if (!jabswitch) {
        return false;
    }
    std::wstring command = QuoteCommandPath(*jabswitch);
    command += L" /enable";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> command_buffer(command.begin(), command.end());
    command_buffer.push_back(L'\0');
    const bool started = CreateProcessW(nullptr, command_buffer.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) == TRUE;
    if (!started) {
        return false;
    }
    WaitForSingleObject(process.hProcess, 3000);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code == 0;
}

bool LooksLikeJavaWindow(HWND hwnd) {
    const std::wstring class_name = WindowClassName(hwnd);
    const std::wstring process_name = WindowProcessName(hwnd);
    return class_name == L"SunAwtFrame" || class_name == L"SunAwtDialog" ||
           class_name.find(L"SunAwt") != std::wstring::npos ||
           class_name.find(L"Swing") != std::wstring::npos ||
           process_name == L"java" || process_name == L"javaw";
}

JavaAccessBridgeApi& SharedJavaAccessBridgeApi() {
    static JavaAccessBridgeApi api;
    return api;
}

bool TryGetJavaAccessBridgeCaretAnchor(HWND target, RECT& avoid, TextAvoidSource& source, std::wstring& status) {
    if (!target) {
        status = L"java-access-bridge-unavailable";
        return false;
    }
    JavaAccessBridgeApi& api = SharedJavaAccessBridgeApi();
    if (!LoadJavaAccessBridgeApi(api)) {
        status = L"java-access-bridge-unavailable";
        return false;
    }
    if (!api.is_java_window(target)) {
        if (LooksLikeJavaWindow(target) && !api.enable_attempted) {
            api.enable_attempted = true;
            status = TryEnableJavaAccessBridge() ? L"java-access-bridge-enabled-restart-required"
                                                 : L"java-access-bridge-unavailable";
        } else {
            status = L"java-access-bridge-not-java-window";
        }
        return false;
    }

    long vm_id = 0;
    JabObject context = 0;
    if (!api.get_context_with_focus(target, &vm_id, &context) || vm_id == 0 || context == 0) {
        if (!api.enable_attempted) {
            api.enable_attempted = true;
            if (TryEnableJavaAccessBridge()) {
                status = L"java-access-bridge-enabled-restart-required";
            } else {
                status = L"java-access-bridge-no-focused-text";
            }
        } else {
            status = L"java-access-bridge-no-focused-text";
        }
        return false;
    }

    auto release_context = [&]() {
        if (context) {
            api.release_java_object(vm_id, context);
            context = 0;
        }
    };

    JabAccessibleContextInfo context_info{};
    if (!api.get_context_info(vm_id, context, &context_info) || !context_info.accessibleText) {
        release_context();
        status = L"java-access-bridge-no-focused-text";
        return false;
    }

    JabAccessibleTextInfo text_info{};
    if (!api.get_text_info(vm_id, context, &text_info, context_info.x, context_info.y) ||
        text_info.caretIndex < 0) {
        release_context();
        status = L"java-access-bridge-no-focused-text";
        return false;
    }

    JabAccessibleTextRectInfo caret{};
    int caret_index = text_info.caretIndex;
    if (text_info.charCount > 0) {
        caret_index = std::clamp(caret_index, 0, text_info.charCount - 1);
    }
    bool have_rect = api.get_caret_location(vm_id, context, &caret, text_info.caretIndex) == TRUE;
    if ((!have_rect || caret.width <= 0 || caret.height <= 0) && caret_index != text_info.caretIndex) {
        have_rect = api.get_caret_location(vm_id, context, &caret, caret_index) == TRUE;
    }
    release_context();
    if (!have_rect || caret.width <= 0 || caret.height <= 0) {
        status = L"java-access-bridge-no-focused-text";
        return false;
    }

    RECT window_rect{};
    if (!GetWindowRect(target, &window_rect)) {
        status = L"java-access-bridge-no-focused-text";
        return false;
    }
    const UINT dpi = DpiForWindowHandle(target);
    RECT rect{caret.x, caret.y, caret.x + caret.width, caret.y + caret.height};
    if (!PopupJavaCaretRectUsable(rect, window_rect, dpi)) {
        status = L"java-access-bridge-stale-caret";
        return false;
    }
    avoid = rect;
    source = TextAvoidSource::JavaAccessBridgeCaret;
    status = L"java-access-bridge-caret";
    return true;
}

struct WindowBitmapPixels {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> bgra;
};

bool CaptureWindowBitmap(HWND hwnd, WindowBitmapPixels& bitmap) {
    RECT rect{};
    if (!hwnd || !GetWindowRect(hwnd, &rect) || rect.right <= rect.left || rect.bottom <= rect.top) {
        return false;
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0 || width > 10000 || height > 10000) {
        return false;
    }

    HDC screen_dc = GetDC(nullptr);
    if (!screen_dc) {
        return false;
    }
    HDC memory_dc = CreateCompatibleDC(screen_dc);
    HBITMAP dib = nullptr;
    void* bits = nullptr;
    bool ok = false;
    if (memory_dc) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        dib = CreateDIBSection(screen_dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dib && bits) {
            HGDIOBJ old_bitmap = SelectObject(memory_dc, dib);
            if (BitBlt(memory_dc, 0, 0, width, height, screen_dc, rect.left, rect.top, SRCCOPY | CAPTUREBLT)) {
                bitmap.width = width;
                bitmap.height = height;
                bitmap.bgra.assign(static_cast<uint32_t*>(bits),
                                   static_cast<uint32_t*>(bits) + static_cast<size_t>(width) * height);
                ok = true;
            }
            SelectObject(memory_dc, old_bitmap);
        }
    }
    if (dib) {
        DeleteObject(dib);
    }
    if (memory_dc) {
        DeleteDC(memory_dc);
    }
    ReleaseDC(nullptr, screen_dc);
    return ok;
}

bool IsVisualCaretPixel(uint32_t bgra) {
    const int blue = static_cast<int>(bgra & 0xFF);
    const int green = static_cast<int>((bgra >> 8) & 0xFF);
    const int red = static_cast<int>((bgra >> 16) & 0xFF);
    const int max_channel = std::max({red, green, blue});
    const int min_channel = std::min({red, green, blue});
    const bool bright = max_channel >= 210 && min_channel >= 185;
    const bool terminal_green = green >= 160 && green >= red + 35 && green >= blue + 35;
    return bright || terminal_green;
}

bool IsDarkTerminalPixel(uint32_t bgra) {
    const int blue = static_cast<int>(bgra & 0xFF);
    const int green = static_cast<int>((bgra >> 8) & 0xFF);
    const int red = static_cast<int>((bgra >> 16) & 0xFF);
    return red <= 120 && green <= 150 && blue <= 170;
}

bool TryGetVisualTerminalCaretAnchor(HWND target, RECT& avoid, TextAvoidSource& source, std::wstring& status) {
    if (!target || !LooksLikeJavaWindow(target)) {
        status = L"visual-terminal-caret-not-java-window";
        return false;
    }
    RECT window_rect{};
    if (!GetWindowRect(target, &window_rect)) {
        status = L"visual-terminal-caret-capture-failed";
        return false;
    }
    WindowBitmapPixels bitmap;
    if (!CaptureWindowBitmap(target, bitmap) || bitmap.bgra.empty()) {
        status = L"visual-terminal-caret-capture-failed";
        return false;
    }

    const UINT dpi = DpiForWindowHandle(target);
    const int min_height = ScalePopupMetricForDpi(8, dpi);
    const int max_height = ScalePopupMetricForDpi(48, dpi);
    const int max_width = ScalePopupMetricForDpi(10, dpi);
    const int left_margin = ScalePopupMetricForDpi(140, dpi);
    const int top_margin = ScalePopupMetricForDpi(48, dpi);
    const int bottom_margin = ScalePopupMetricForDpi(120, dpi);
    int best_score = -1;
    RECT best{};

    for (int x = std::max(0, left_margin); x < bitmap.width; ++x) {
        int y = std::max(0, top_margin);
        const int y_end = std::max(y, bitmap.height - std::max(0, bottom_margin));
        while (y < y_end) {
            const uint32_t pixel = bitmap.bgra[static_cast<size_t>(y) * bitmap.width + x];
            if (!IsVisualCaretPixel(pixel)) {
                ++y;
                continue;
            }
            const int start_y = y;
            int end_y = y;
            while (end_y < y_end &&
                   IsVisualCaretPixel(bitmap.bgra[static_cast<size_t>(end_y) * bitmap.width + x])) {
                ++end_y;
            }
            const int height = end_y - start_y;
            if (height >= min_height && height <= max_height) {
                int left = x;
                int right = x + 1;
                while (left > 0) {
                    bool column_match = false;
                    for (int yy = start_y; yy < end_y && !column_match; ++yy) {
                        column_match =
                            IsVisualCaretPixel(bitmap.bgra[static_cast<size_t>(yy) * bitmap.width + left - 1]);
                    }
                    if (!column_match) {
                        break;
                    }
                    --left;
                }
                while (right < bitmap.width) {
                    bool column_match = false;
                    for (int yy = start_y; yy < end_y && !column_match; ++yy) {
                        column_match =
                            IsVisualCaretPixel(bitmap.bgra[static_cast<size_t>(yy) * bitmap.width + right]);
                    }
                    if (!column_match) {
                        break;
                    }
                    ++right;
                }
                const int width = right - left;
                int dark_neighbors = 0;
                const int neighbor_left = std::max(0, left - ScalePopupMetricForDpi(12, dpi));
                const int neighbor_right = std::min(bitmap.width - 1, right + ScalePopupMetricForDpi(12, dpi));
                for (int yy = start_y; yy < end_y; yy += std::max(1, height / 4)) {
                    if (IsDarkTerminalPixel(bitmap.bgra[static_cast<size_t>(yy) * bitmap.width + neighbor_left])) {
                        ++dark_neighbors;
                    }
                    if (IsDarkTerminalPixel(bitmap.bgra[static_cast<size_t>(yy) * bitmap.width + neighbor_right])) {
                        ++dark_neighbors;
                    }
                }
                RECT candidate{window_rect.left + left, window_rect.top + start_y,
                               window_rect.left + right, window_rect.top + end_y};
                if (width <= max_width && dark_neighbors >= 2 &&
                    PopupVisualCaretRectUsable(candidate, window_rect, dpi)) {
                    const int score = candidate.bottom * 10000 - candidate.left;
                    if (score > best_score) {
                        best_score = score;
                        best = candidate;
                    }
                }
                x = std::max(x, right - 1);
            }
            y = std::max(end_y, y + 1);
        }
    }

    if (best_score < 0) {
        status = L"visual-terminal-caret-not-found";
        return false;
    }
    avoid = best;
    source = TextAvoidSource::VisualTerminalCaret;
    status = L"visual-terminal-caret";
    return true;
}

bool IsScreenPointNearWindow(POINT point, RECT window_rect, int tolerance) {
    return point.x >= window_rect.left - tolerance && point.x <= window_rect.right + tolerance &&
           point.y >= window_rect.top - tolerance && point.y <= window_rect.bottom + tolerance;
}

bool IsConsoleWindow(HWND hwnd) {
    wchar_t class_name[64]{};
    return hwnd && GetClassNameW(hwnd, class_name, static_cast<int>(std::size(class_name))) > 0 &&
           PopupTargetCanUseConsoleAnchor(class_name);
}

void ResetAutomationRectangles(SAFEARRAY*& rectangles) {
    if (rectangles) {
        SafeArrayDestroy(rectangles);
        rectangles = nullptr;
    }
}

bool RawRectFromSafeArray(SAFEARRAY* rectangles, RECT& rect) {
    if (!rectangles || SafeArrayGetDim(rectangles) != 1) {
        return false;
    }
    LONG lower = 0;
    LONG upper = -1;
    if (FAILED(SafeArrayGetLBound(rectangles, 1, &lower)) ||
        FAILED(SafeArrayGetUBound(rectangles, 1, &upper)) ||
        upper - lower + 1 < 4) {
        return false;
    }
    double values[4]{};
    for (LONG index = 0; index < 4; ++index) {
        LONG safe_index = lower + index;
        if (FAILED(SafeArrayGetElement(rectangles, &safe_index, &values[index]))) {
            return false;
        }
    }
    rect.left = static_cast<LONG>(std::lround(values[0]));
    rect.top = static_cast<LONG>(std::lround(values[1]));
    rect.right = static_cast<LONG>(std::lround(values[0] + values[2]));
    rect.bottom = static_cast<LONG>(std::lround(values[1] + values[3]));
    return rect.right > rect.left && rect.bottom > rect.top && rect.bottom > 1 &&
           (rect.left > 1 || rect.top > 1);
}

bool RectFromSafeArray(SAFEARRAY* rectangles, RECT& rect) {
    return RawRectFromSafeArray(rectangles, rect) && PopupTextRangeRectUsable(rect);
}

bool AvoidRectFromTextRange(IUIAutomationTextRange* range, RECT& avoid) {
    if (!range) {
        return false;
    }
    SAFEARRAY* rectangles = nullptr;
    bool ok = false;
    if (SUCCEEDED(range->GetBoundingRectangles(&rectangles))) {
        RECT rect{};
        if (RectFromSafeArray(rectangles, rect)) {
            avoid = rect;
            ok = true;
        }
    }
    ResetAutomationRectangles(rectangles);
    return ok;
}

bool CharacterRectFromTextRange(IUIAutomationTextRange* range, UINT dpi, RECT& rect) {
    if (!range) {
        return false;
    }
    SAFEARRAY* rectangles = nullptr;
    bool ok = false;
    if (SUCCEEDED(range->GetBoundingRectangles(&rectangles))) {
        RECT candidate{};
        if (RawRectFromSafeArray(rectangles, candidate) && PopupTextCharacterRectUsable(candidate, dpi)) {
            rect = candidate;
            ok = true;
        }
    }
    ResetAutomationRectangles(rectangles);
    return ok;
}

bool AvoidRectFromAdjacentTextRange(IUIAutomationTextRange* range, int count, bool after_character, UINT dpi,
                                    RECT& avoid) {
    if (!range) {
        return false;
    }
    IUIAutomationTextRange* moved = nullptr;
    if (FAILED(range->Clone(&moved)) || !moved) {
        return false;
    }

    int actual_moved = 0;
    RECT rect{};
    const bool ok = SUCCEEDED(moved->Move(TextUnit_Character, count, &actual_moved)) && actual_moved != 0 &&
                    SUCCEEDED(moved->ExpandToEnclosingUnit(TextUnit_Character)) &&
                    CharacterRectFromTextRange(moved, dpi, rect);
    if (ok) {
        avoid = PopupTextCharacterCaretRect(rect, after_character);
    }

    ReleasePtr(moved);
    return ok;
}

bool AvoidRectFromCollapsedTextRange(IUIAutomationTextRange* range, UINT dpi, RECT& avoid) {
    if (!range) {
        return false;
    }
    return AvoidRectFromAdjacentTextRange(range, 1, false, dpi, avoid) ||
           AvoidRectFromAdjacentTextRange(range, -1, true, dpi, avoid);
}

bool AvoidRectFromTextRangeArray(IUIAutomationTextRangeArray* ranges, RECT& avoid) {
    if (!ranges) {
        return false;
    }
    int length = 0;
    if (FAILED(ranges->get_Length(&length)) || length <= 0) {
        return false;
    }
    for (int index = 0; index < length; ++index) {
        IUIAutomationTextRange* range = nullptr;
        const bool ok = SUCCEEDED(ranges->GetElement(index, &range)) && range &&
                        AvoidRectFromTextRange(range, avoid);
        ReleasePtr(range);
        if (ok) {
            return true;
        }
    }
    return false;
}

bool AvoidRectFromCollapsedTextRangeArray(IUIAutomationTextRangeArray* ranges, UINT dpi, RECT& avoid) {
    if (!ranges) {
        return false;
    }
    int length = 0;
    if (FAILED(ranges->get_Length(&length)) || length <= 0) {
        return false;
    }
    for (int index = 0; index < length; ++index) {
        IUIAutomationTextRange* range = nullptr;
        const bool ok = SUCCEEDED(ranges->GetElement(index, &range)) && range &&
                        AvoidRectFromCollapsedTextRange(range, dpi, avoid);
        ReleasePtr(range);
        if (ok) {
            return true;
        }
    }
    return false;
}

bool AvoidRectFromTextPatternSelection(IUIAutomationTextPattern* text, RECT& avoid) {
    if (!text) {
        return false;
    }
    IUIAutomationTextRangeArray* ranges = nullptr;
    const bool ok = SUCCEEDED(text->GetSelection(&ranges)) && AvoidRectFromTextRangeArray(ranges, avoid);
    ReleasePtr(ranges);
    return ok;
}

bool AvoidRectFromTextPatternCollapsedSelection(IUIAutomationTextPattern* text, UINT dpi, RECT& avoid) {
    if (!text) {
        return false;
    }
    IUIAutomationTextRangeArray* ranges = nullptr;
    const bool ok =
        SUCCEEDED(text->GetSelection(&ranges)) && AvoidRectFromCollapsedTextRangeArray(ranges, dpi, avoid);
    ReleasePtr(ranges);
    return ok;
}

bool RectFromTextRangeVisibleLine(IUIAutomationTextRange* range, RECT& avoid) {
    if (!range) {
        return false;
    }
    SAFEARRAY* rectangles = nullptr;
    bool ok = false;
    if (SUCCEEDED(range->GetBoundingRectangles(&rectangles)) && rectangles && SafeArrayGetDim(rectangles) == 1) {
        LONG lower = 0;
        LONG upper = -1;
        if (SUCCEEDED(SafeArrayGetLBound(rectangles, 1, &lower)) &&
            SUCCEEDED(SafeArrayGetUBound(rectangles, 1, &upper)) &&
            upper - lower + 1 >= 4) {
            const LONG count = upper - lower + 1;
            const LONG start = lower + ((count - 4) / 4) * 4;
            double values[4]{};
            bool have_rect = true;
            for (LONG index = 0; index < 4; ++index) {
                LONG safe_index = start + index;
                if (FAILED(SafeArrayGetElement(rectangles, &safe_index, &values[index]))) {
                    have_rect = false;
                    break;
                }
            }
            if (have_rect) {
                RECT rect{
                    static_cast<LONG>(std::lround(values[0])),
                    static_cast<LONG>(std::lround(values[1])),
                    static_cast<LONG>(std::lround(values[0] + values[2])),
                    static_cast<LONG>(std::lround(values[1] + values[3])),
                };
                if (rect.right > rect.left && rect.bottom > rect.top) {
                    avoid = rect;
                    ok = true;
                }
            }
        }
    }
    ResetAutomationRectangles(rectangles);
    return ok;
}

bool AvoidRectFromTextPatternVisibleLine(IUIAutomationTextPattern* text, RECT& avoid) {
    if (!text) {
        return false;
    }
    IUIAutomationTextRangeArray* ranges = nullptr;
    bool ok = false;
    if (SUCCEEDED(text->GetVisibleRanges(&ranges)) && ranges) {
        int length = 0;
        if (SUCCEEDED(ranges->get_Length(&length)) && length > 0) {
            for (int index = length - 1; index >= 0 && !ok; --index) {
                IUIAutomationTextRange* range = nullptr;
                ok = SUCCEEDED(ranges->GetElement(index, &range)) && range &&
                     RectFromTextRangeVisibleLine(range, avoid);
                ReleasePtr(range);
            }
        }
    }
    ReleasePtr(ranges);
    return ok;
}

bool IsTerminalAutomationElement(IUIAutomationElement* element) {
    if (!element) {
        return false;
    }
    CONTROLTYPEID control_type = 0;
    if (FAILED(element->get_CurrentControlType(&control_type)) || control_type != UIA_TextControlTypeId) {
        return false;
    }
    BSTR class_name = nullptr;
    const bool terminal = SUCCEEDED(element->get_CurrentClassName(&class_name)) && class_name &&
                          wcscmp(class_name, L"TermControl") == 0;
    if (class_name) {
        SysFreeString(class_name);
    }
    return terminal;
}

bool TryGetAutomationElementTextAvoidRect(IUIAutomationElement* element, UINT dpi, bool allow_rect_fallback,
                                          RECT& avoid, TextAvoidSource& source);

bool AvoidRectFromAutomationElementRect(IUIAutomationElement* element, RECT& avoid, TextAvoidSource& source) {
    if (!element) {
        return false;
    }
    RECT rect{};
    if (FAILED(element->get_CurrentBoundingRectangle(&rect)) || !PopupTextInputRectUsable(rect)) {
        return false;
    }
    avoid = rect;
    source = TextAvoidSource::AutomationElementRect;
    return true;
}

bool AutomationElementRectNearAvoidRect(IUIAutomationElement* element, RECT avoid, UINT dpi) {
    if (!element) {
        return true;
    }
    RECT rect{};
    if (FAILED(element->get_CurrentBoundingRectangle(&rect)) || !PopupTextInputRectUsable(rect)) {
        return true;
    }
    const int tolerance = ScalePopupMetricForDpi(96, dpi);
    return rect.left - tolerance <= avoid.right && rect.right + tolerance >= avoid.left &&
           rect.top - tolerance <= avoid.bottom && rect.bottom + tolerance >= avoid.top;
}

bool AutomationElementHasPattern(IUIAutomationElement* element, PATTERNID pattern_id) {
    IUnknown* pattern = nullptr;
    const bool ok = element && SUCCEEDED(element->GetCurrentPattern(pattern_id, &pattern)) && pattern;
    ReleasePtr(pattern);
    return ok;
}

bool AutomationElementValueIsReadOnly(IUIAutomationElement* element) {
    IUIAutomationValuePattern* value = nullptr;
    BOOL read_only = FALSE;
    const bool ok = element &&
                    SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value &&
                    SUCCEEDED(value->get_CurrentIsReadOnly(&read_only));
    ReleasePtr(value);
    return ok && read_only;
}

FocusedTextContext GetFocusedTextContext() {
    FocusedTextContext context{};
    IUIAutomation* automation = nullptr;
    IUIAutomationElement* focused = nullptr;

    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) &&
        automation && SUCCEEDED(automation->GetFocusedElement(&focused)) && focused) {
        RECT rect{};
        if (SUCCEEDED(focused->get_CurrentBoundingRectangle(&rect)) && rect.right > rect.left &&
            rect.bottom > rect.top) {
            context.has_rect = true;
            context.rect = rect;
        }

        CONTROLTYPEID control_type = 0;
        const bool text_control =
            SUCCEEDED(focused->get_CurrentControlType(&control_type)) &&
            (control_type == UIA_EditControlTypeId || control_type == UIA_TextControlTypeId ||
             control_type == UIA_DocumentControlTypeId);
        const bool text_pattern = AutomationElementHasPattern(focused, UIA_TextPattern2Id) ||
                                  AutomationElementHasPattern(focused, UIA_TextPatternId);
        const bool value_pattern = AutomationElementHasPattern(focused, UIA_ValuePatternId);
        context.editable = (text_control || text_pattern || value_pattern) &&
                           !AutomationElementValueIsReadOnly(focused);
    }

    ReleasePtr(focused);
    ReleasePtr(automation);
    return context;
}

bool FocusedTextContextAllowsCaret(const FocusedTextContext& context, RECT avoid, UINT dpi) {
    return PopupCaretAnchorAllowedByFocusedText(context.editable, context.rect, context.has_rect, avoid, dpi);
}

bool TryGetAutomationCaretAvoidRect(UINT dpi, RECT& avoid, TextAvoidSource& source) {
    IUIAutomation* automation = nullptr;
    IUIAutomationElement* focused = nullptr;
    bool ok = false;

    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->GetFocusedElement(&focused)) && focused) {
        ok = TryGetAutomationElementTextAvoidRect(focused, dpi, true, avoid, source);
    }

    ReleasePtr(focused);
    ReleasePtr(automation);
    return ok;
}

std::wstring AutomationFocusedElementDebugInfo() {
    IUIAutomation* automation = nullptr;
    IUIAutomationElement* focused = nullptr;
    std::wstring info = L"uia-focused unavailable";
    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) &&
        automation && SUCCEEDED(automation->GetFocusedElement(&focused)) && focused) {
        CONTROLTYPEID control_type = 0;
        RECT rect{};
        BSTR class_name = nullptr;
        BSTR name = nullptr;
        focused->get_CurrentControlType(&control_type);
        focused->get_CurrentBoundingRectangle(&rect);
        focused->get_CurrentClassName(&class_name);
        focused->get_CurrentName(&name);
        info = L"uia-focused control=";
        info += std::to_wstring(control_type);
        info += L" class=";
        info += class_name ? class_name : L"";
        info += L" name=";
        info += name ? name : L"";
        info += L" rect=";
        info += RectToLogString(rect);
        info += L" text2=";
        info += AutomationElementHasPattern(focused, UIA_TextPattern2Id) ? L"1" : L"0";
        info += L" text=";
        info += AutomationElementHasPattern(focused, UIA_TextPatternId) ? L"1" : L"0";
        info += L" value=";
        info += AutomationElementHasPattern(focused, UIA_ValuePatternId) ? L"1" : L"0";
        info += L" readonly=";
        info += AutomationElementValueIsReadOnly(focused) ? L"1" : L"0";
        if (class_name) {
            SysFreeString(class_name);
        }
        if (name) {
            SysFreeString(name);
        }
    }
    ReleasePtr(focused);
    ReleasePtr(automation);
    return info;
}

bool TryGetExplicitAutomationTextCaretAvoidRect(UINT dpi, RECT& avoid, TextAvoidSource& source) {
    IUIAutomation* automation = nullptr;
    IUIAutomationElement* focused = nullptr;
    IUIAutomationTextPattern2* text2 = nullptr;
    IUIAutomationTextRange* range = nullptr;
    BOOL active = FALSE;
    bool ok = false;

    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->GetFocusedElement(&focused)) && focused &&
        SUCCEEDED(focused->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&text2))) && text2 &&
        SUCCEEDED(text2->GetCaretRange(&active, &range)) && active && range) {
        ok = AvoidRectFromTextRange(range, avoid);
        if (!ok) {
            ok = AvoidRectFromCollapsedTextRange(range, dpi, avoid);
        }
        if (ok && !AutomationElementRectNearAvoidRect(focused, avoid, dpi)) {
            ok = false;
        }
        if (ok) {
            source = TextAvoidSource::AutomationText2Caret;
        }
    }

    ReleasePtr(range);
    ReleasePtr(text2);
    ReleasePtr(focused);
    ReleasePtr(automation);
    return ok;
}

bool TryGetAutomationElementTextAvoidRect(IUIAutomationElement* element, UINT dpi, bool allow_rect_fallback,
                                          RECT& avoid, TextAvoidSource& source) {
    if (!element) {
        return false;
    }
    IUIAutomationTextPattern2* text2 = nullptr;
    IUIAutomationTextPattern* text = nullptr;
    IUIAutomationTextRange* range = nullptr;
    BOOL active = FALSE;
    bool ok = false;

    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&text2))) && text2 &&
        SUCCEEDED(text2->GetCaretRange(&active, &range)) && active && range) {
        ok = AvoidRectFromTextRange(range, avoid);
        if (ok) {
            source = TextAvoidSource::AutomationText2Caret;
        }
        if (!ok) {
            ok = AvoidRectFromCollapsedTextRange(range, dpi, avoid);
            if (ok) {
                source = TextAvoidSource::AutomationText2Caret;
            }
        }
    }
    if (!ok && text2) {
        ok = AvoidRectFromTextPatternSelection(text2, avoid);
        if (ok) {
            source = TextAvoidSource::AutomationText2Selection;
        }
        if (!ok) {
            ok = AvoidRectFromTextPatternCollapsedSelection(text2, dpi, avoid);
            if (ok) {
                source = TextAvoidSource::AutomationText2Selection;
            }
        }
    }
    if (!ok && SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text))) && text) {
        ok = AvoidRectFromTextPatternSelection(text, avoid);
        if (ok) {
            source = TextAvoidSource::AutomationTextSelection;
        }
        if (!ok) {
            ok = AvoidRectFromTextPatternCollapsedSelection(text, dpi, avoid);
            if (ok) {
                source = TextAvoidSource::AutomationTextSelection;
            }
        }
    }
    if (ok && !AutomationElementRectNearAvoidRect(element, avoid, dpi)) {
        ok = false;
    }
    if (!ok && allow_rect_fallback) {
        ok = AvoidRectFromAutomationElementRect(element, avoid, source);
    }

    ReleasePtr(range);
    ReleasePtr(text);
    ReleasePtr(text2);
    return ok;
}

bool TryFindAutomationTextAvoidRectInSubtree(IUIAutomation* automation, IUIAutomationElement* root, UINT dpi,
                                             RECT& avoid, TextAvoidSource& source) {
    if (!automation || !root) {
        return false;
    }

    IUIAutomationCondition* keyboard_focus_condition = nullptr;
    IUIAutomationElement* candidate = nullptr;
    bool ok = false;

    VARIANT focus_value{};
    focus_value.vt = VT_BOOL;
    focus_value.boolVal = VARIANT_TRUE;
    if (SUCCEEDED(automation->CreatePropertyCondition(UIA_HasKeyboardFocusPropertyId, focus_value,
                                                      &keyboard_focus_condition)) &&
        keyboard_focus_condition &&
        SUCCEEDED(root->FindFirst(TreeScope_Subtree, keyboard_focus_condition, &candidate)) && candidate) {
            ok = TryGetAutomationElementTextAvoidRect(candidate, dpi, true, avoid, source);
    }

    ReleasePtr(candidate);
    ReleasePtr(keyboard_focus_condition);
    return ok;
}

bool TryGetAutomationAvoidRectFromWindow(HWND target, UINT dpi, RECT& avoid, TextAvoidSource& source) {
    if (!target) {
        return false;
    }
    IUIAutomation* automation = nullptr;
    IUIAutomationElement* root = nullptr;
    IUIAutomationElement* focused = nullptr;
    IUIAutomationCondition* keyboard_focus_condition = nullptr;
    IUIAutomationElement* candidate = nullptr;
    bool ok = false;

    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) &&
        automation) {
        if (SUCCEEDED(automation->GetFocusedElement(&focused)) && focused) {
            ok = TryGetAutomationElementTextAvoidRect(focused, dpi, true, avoid, source);
        }
        if (SUCCEEDED(automation->ElementFromHandle(target, &root)) && root) {
            VARIANT focus_value{};
            focus_value.vt = VT_BOOL;
            focus_value.boolVal = VARIANT_TRUE;
            if (!ok &&
                SUCCEEDED(automation->CreatePropertyCondition(UIA_HasKeyboardFocusPropertyId, focus_value,
                                                              &keyboard_focus_condition)) &&
                keyboard_focus_condition &&
                SUCCEEDED(root->FindFirst(TreeScope_Subtree, keyboard_focus_condition, &candidate)) && candidate) {
                ok = TryGetAutomationElementTextAvoidRect(candidate, dpi, true, avoid, source);
            }
            ReleasePtr(candidate);

            if (!ok) {
                ok = TryFindAutomationTextAvoidRectInSubtree(automation, root, dpi, avoid, source);
            }
            if (!ok) {
                ok = TryGetAutomationElementTextAvoidRect(root, dpi, false, avoid, source);
            }
        }
    }

    ReleasePtr(candidate);
    ReleasePtr(keyboard_focus_condition);
    ReleasePtr(focused);
    ReleasePtr(root);
    ReleasePtr(automation);
    return ok;
}

} // namespace ClipSoul::popup_detail
