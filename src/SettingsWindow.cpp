#include "ClipSoul/SettingsWindow.h"

#include "ClipSoul/App.h"
#include "ClipSoul/PopupLayout.h"
#include "ClipSoul/Win32Util.h"

#include <ShObjIdl.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace ClipSoul {
namespace {
constexpr wchar_t kSettingsClass[] = L"ClipSoul.SettingsWindow";
constexpr int kSettingsWidth = 340;
constexpr int kSettingsHeight = 394;
constexpr int kCloseBoxLeft = 302;
constexpr int kCloseBoxTop = 14;
constexpr int kCloseBoxSize = 24;
constexpr int kTargetLimit = 9;
constexpr int kTargetHotkeyReset = 10;
constexpr int kTargetStorageBrowse = 11;
constexpr UINT_PTR kSettingsHoverTimer = 61;

bool InRect(int x, int y, int left, int top, int right, int bottom) {
    return x >= left && x <= right && y >= top && y <= bottom;
}

void DrawRoundRect(HDC dc, int left, int top, int right, int bottom, int radius,
                   COLORREF fill, COLORREF stroke) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, stroke);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, left, top, right, bottom, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

COLORREF RgbFromHex(uint32_t rgb) {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

COLORREF Mix(COLORREF from, COLORREF to, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    const auto mix_channel = [amount](int a, int b) {
        return static_cast<int>(std::lround(static_cast<float>(a) + static_cast<float>(b - a) * amount));
    };
    return RGB(mix_channel(GetRValue(from), GetRValue(to)),
               mix_channel(GetGValue(from), GetGValue(to)),
               mix_channel(GetBValue(from), GetBValue(to)));
}

void DrawGlowRect(HDC dc, int left, int top, int right, int bottom, int radius, COLORREF color, float progress) {
    progress = std::clamp(progress, 0.0f, 1.0f);
    if (progress <= 0.0f) {
        return;
    }
    for (int i = 4; i >= 0; --i) {
        const float t = static_cast<float>(i) / 4.0f;
        const int expand_x = 2 + static_cast<int>(std::lround(t * 8.0f));
        const int expand_y = 1 + static_cast<int>(std::lround(t * 5.0f));
        const COLORREF fill = Mix(RGB(255, 255, 255), color, (0.06f - t * 0.035f) * progress);
        DrawRoundRect(dc, left - expand_x, top - expand_y, right + expand_x, bottom + expand_y,
                      radius + expand_x, fill, fill);
    }
}

void DrawToggle(HDC dc, int x, int y, bool active, float hover, const PopupThemePalette& palette) {
    const COLORREF active_color = RgbFromHex(palette.accent);
    const COLORREF idle_fill = palette.dark ? RGB(51, 65, 85) : RGB(229, 236, 245);
    const COLORREF hover_fill = active ? Mix(active_color, RGB(45, 212, 191), hover)
                                       : Mix(idle_fill, palette.dark ? RGB(71, 85, 105) : RGB(244, 255, 253), hover);
    DrawRoundRect(dc, x, y, x + 38, y + 22, 11, active ? hover_fill : hover_fill,
                  active ? active_color : Mix(RgbFromHex(palette.border), RgbFromHex(palette.accent), hover * 0.45f));
    HBRUSH knob = CreateSolidBrush(RGB(255, 255, 255));
    HGDIOBJ old_brush = SelectObject(dc, knob);
    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    const int cx = active ? x + 27 : x + 11;
    Ellipse(dc, cx - 7, y + 4, cx + 7, y + 18);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(knob);
}

void DrawCenteredText(HDC dc, const RECT& rect, const wchar_t* text) {
    DrawTextW(dc, text, static_cast<int>(wcslen(text)), const_cast<RECT*>(&rect),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

std::wstring EllipsizeMiddle(HDC dc, std::wstring value, int max_width) {
    if (value.empty()) {
        return value;
    }
    SIZE size{};
    GetTextExtentPoint32W(dc, value.c_str(), static_cast<int>(value.size()), &size);
    if (size.cx <= max_width) {
        return value;
    }

    constexpr wchar_t ellipsis[] = L"...";
    while (value.size() > 4) {
        const size_t keep_left = (value.size() - 3) / 2;
        const size_t keep_right = value.size() - 3 - keep_left;
        std::wstring candidate = value.substr(0, keep_left) + ellipsis + value.substr(value.size() - keep_right);
        GetTextExtentPoint32W(dc, candidate.c_str(), static_cast<int>(candidate.size()), &size);
        if (size.cx <= max_width) {
            return candidate;
        }
        value.erase(value.begin() + static_cast<std::ptrdiff_t>(keep_left));
    }
    return ellipsis;
}

int HitTarget(int x, int y) {
    if (InRect(x, y, kCloseBoxLeft, kCloseBoxTop, kCloseBoxLeft + kCloseBoxSize, kCloseBoxTop + kCloseBoxSize)) return 1;
    if (InRect(x, y, 220, 346, 294, 374)) return 2;
    if (InRect(x, y, 162, 78, 276, 108)) return kTargetLimit;
    if (InRect(x, y, 222, 116, 288, 146)) return 3;
    if (InRect(x, y, 222, 150, 288, 180)) return 4;
    if (InRect(x, y, 150, 184, 276, 214)) return 5;
    if (InRect(x, y, 280, 184, 314, 214)) return kTargetHotkeyReset;
    if (InRect(x, y, 232, 252, 296, 282)) return kTargetStorageBrowse;
    return 0;
}

void FillSolid(HDC dc, const RECT& rect, COLORREF fill) {
    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}
} // namespace

SettingsWindow::SettingsWindow(HINSTANCE instance, HistoryStore& store, App& app)
    : instance_(instance),
      store_(store),
      app_(app) {}

bool SettingsWindow::Create(HWND owner) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = SettingsWindow::WindowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kSettingsClass;
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kSettingsClass, L"ClipSoul 设置",
                            WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, kSettingsWidth, kSettingsHeight,
                            owner, nullptr, instance_, this);
    if (!hwnd_) {
        return false;
    }

    ApplyBackdrop();
    LoadToControls();
    return true;
}

void SettingsWindow::Show() {
    LoadToControls();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const auto position = CenterWindowInWorkArea(SIZE{kSettingsWidth, kSettingsHeight}, work);
    SetWindowPos(hwnd_, HWND_TOPMOST, position.x, position.y, kSettingsWidth, kSettingsHeight, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::LoadToControls() {
    const auto settings = app_.Settings();
    limit_text_ = std::to_wstring(settings.history_limit);
    storage_path_ = app_.StorageDirectory().wstring();
    storage_save_failed_ = false;
    paused_ = settings.paused;
    startup_ = settings.start_with_windows;
    hotkey_modifiers_ = settings.hotkey_modifiers;
    hotkey_vk_ = settings.hotkey_vk;
    theme_mode_ = std::clamp(settings.theme_mode, 0, 2);
    capturing_hotkey_ = false;
    editing_limit_ = false;
    replace_limit_on_next_digit_ = false;
    hotkey_conflict_ = false;
}

bool SettingsWindow::SaveFromControls() {
    auto settings = app_.Settings();
    settings.history_limit = std::max(1, _wtoi(limit_text_.c_str()));
    settings.paused = paused_;
    settings.start_with_windows = startup_;
    settings.hotkey_modifiers = hotkey_modifiers_;
    settings.hotkey_vk = hotkey_vk_;
    settings.theme_mode = theme_mode_;
    app_.SaveSettings(settings);
    const auto storage_dir = std::filesystem::path(storage_path_);
    if (storage_dir.empty() || !app_.SaveStorageDirectory(storage_dir)) {
        storage_save_failed_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }
    storage_save_failed_ = false;
    return true;
}

void SettingsWindow::TogglePause() {
    paused_ = !paused_;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::ToggleStartup() {
    startup_ = !startup_;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::BrowseStorageDirectory() {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return;
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    dialog->SetTitle(L"\u9009\u62e9\u5b58\u50a8\u4f4d\u7f6e");

    IShellItem* current_folder = nullptr;
    if (!storage_path_.empty() &&
        SUCCEEDED(SHCreateItemFromParsingName(storage_path_.c_str(), nullptr, IID_PPV_ARGS(&current_folder)))) {
        dialog->SetFolder(current_folder);
        current_folder->Release();
    }

    if (SUCCEEDED(dialog->Show(hwnd_))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                storage_path_ = path;
                storage_save_failed_ = false;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::SetThemeMode(int mode) {
    theme_mode_ = std::clamp(mode, 0, 2);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::ResetHotkeyToDefault() {
    hotkey_modifiers_ = kDefaultHotkeyModifiers;
    hotkey_vk_ = kDefaultHotkeyVk;
    hotkey_conflict_ = !app_.HotkeyAvailable(hotkey_modifiers_, hotkey_vk_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::StartHotkeyCapture() {
    capturing_hotkey_ = true;
    hotkey_conflict_ = false;
    SetFocus(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::SetCapturedHotkey(WPARAM vk) {
    if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN) {
        return;
    }
    unsigned modifiers = 0;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= MOD_CONTROL;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) modifiers |= MOD_ALT;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= MOD_SHIFT;
    if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) modifiers |= MOD_WIN;
    if (modifiers == 0) {
        modifiers = MOD_ALT;
    }
    hotkey_modifiers_ = modifiers;
    hotkey_vk_ = static_cast<unsigned>(vk);
    capturing_hotkey_ = false;
    hotkey_conflict_ = !app_.HotkeyAvailable(hotkey_modifiers_, hotkey_vk_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::ApplyBackdrop() {
    const DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    HRGN region = CreateRoundRectRgn(0, 0, kSettingsWidth + 1, kSettingsHeight + 1, 18, 18);
    SetWindowRgn(hwnd_, region, TRUE);
}

void SettingsWindow::Paint() {
    PAINTSTRUCT ps{};
    HDC paint_dc = BeginPaint(hwnd_, &ps);
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    HDC dc = CreateCompatibleDC(paint_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(paint_dc, rc.right - rc.left, rc.bottom - rc.top);
    HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
    SetBkMode(dc, TRANSPARENT);
    const auto palette = ResolvePopupThemePalette(theme_mode_, IsSystemDarkTheme());
    const bool dark = palette.dark;
    const COLORREF window_fill = dark ? RGB(15, 18, 24) : RGB(242, 248, 253);
    const COLORREF panel_fill = dark ? RGB(27, 33, 44) : RGB(253, 255, 255);
    const COLORREF search_fill = dark ? RGB(38, 46, 60) : RGB(247, 251, 255);
    const COLORREF text_color = dark ? RGB(238, 242, 247) : RgbFromHex(palette.text);
    const COLORREF muted_color = dark ? RGB(156, 166, 180) : RgbFromHex(palette.muted);
    const COLORREF border_color = dark ? RGB(76, 86, 101) : RgbFromHex(palette.border);
    const COLORREF accent_color = dark ? RGB(118, 128, 144) : RgbFromHex(palette.accent);

    FillSolid(dc, rc, window_fill);
    DrawRoundRect(dc, 0, 0, rc.right - 1, rc.bottom - 1, 18, window_fill,
                  dark ? RGB(56, 65, 78) : RGB(210, 224, 238));
    DrawRoundRect(dc, 12, 52, rc.right - 12, rc.bottom - 12, 18,
                  dark ? RGB(20, 25, 34) : RGB(235, 246, 251),
                  dark ? RGB(56, 65, 78) : RGB(222, 235, 246));
    DrawRoundRect(dc, 18, 58, rc.right - 18, rc.bottom - 18, 16, panel_fill,
                  dark ? RGB(58, 68, 82) : RGB(220, 232, 244));

    const auto hover_rect = [&](int target, int left, int top, int right, int bottom, int radius) {
        const float hover = hover_target_ == target ? hover_progress_ : 0.0f;
        if (hover <= 0.0f) {
            return;
        }
        DrawGlowRect(dc, left, top, right, bottom, radius, accent_color, hover);
        DrawRoundRect(dc, left, top, right, bottom, radius,
                      Mix(panel_fill, dark ? RGB(54, 63, 77) : RGB(244, 255, 253), hover * 0.45f),
                      Mix(border_color, accent_color, hover * 0.38f));
    };

    DrawGlowRect(dc, 162, 78, 276, 108, 10, accent_color, hover_target_ == kTargetLimit ? hover_progress_ : 0.0f);
    DrawRoundRect(dc, 162, 78, 276, 108, 10,
                  editing_limit_ ? Mix(search_fill, dark ? RGB(54, 63, 77) : RGB(244, 255, 253), 0.65f)
                                 : (hover_target_ == kTargetLimit
                                        ? Mix(search_fill, dark ? RGB(54, 63, 77) : RGB(244, 255, 253),
                                              hover_progress_ * 0.45f)
                                        : search_fill),
                  editing_limit_ ? accent_color
                                 : (hover_target_ == kTargetLimit
                                        ? Mix(border_color, accent_color, hover_progress_ * 0.38f)
                                        : border_color));
    hover_rect(3, 222, 116, 288, 146, 15);
    hover_rect(4, 222, 150, 288, 180, 15);
    DrawRoundRect(dc, 150, 184, 276, 214, 10,
                  capturing_hotkey_ ? RGB(221, 252, 248)
                                    : (hover_target_ == 5 ? Mix(search_fill, dark ? RGB(54, 63, 77) : RGB(244, 255, 253), hover_progress_ * 0.45f)
                                                          : search_fill),
                  capturing_hotkey_ ? accent_color
                                    : (hover_target_ == 5 ? Mix(border_color, accent_color, hover_progress_ * 0.38f)
                                                          : border_color));
    DrawRoundRect(dc, 36, 250, 224, 282, 10,
                  dark ? RGB(34, 40, 51) : RGB(244, 248, 252),
                  dark ? RGB(62, 70, 84) : RGB(220, 232, 244));
    hover_rect(kTargetStorageBrowse, 232, 252, 296, 282, 12);
    DrawRoundRect(dc, 232, 252, 296, 282, 12,
                  hover_target_ == kTargetStorageBrowse
                      ? Mix(search_fill, dark ? RGB(54, 63, 77) : RGB(244, 255, 253), hover_progress_ * 0.45f)
                      : search_fill,
                  hover_target_ == kTargetStorageBrowse ? Mix(border_color, accent_color, hover_progress_ * 0.38f)
                                                        : border_color);
    DrawRoundRect(dc, 36, 304, 298, 336, 14,
                  dark ? RGB(34, 40, 51) : RGB(244, 248, 252),
                  dark ? RGB(62, 70, 84) : RGB(220, 232, 244));
    DrawRoundRect(dc, 220, 346, 294, 374, 12,
                  hover_target_ == 2 ? Mix(accent_color, RGB(13, 148, 145), hover_progress_) : accent_color,
                  accent_color);

    auto font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ old_font = SelectObject(dc, font);
    SetTextColor(dc, text_color);
    TextOutW(dc, 22, 20, L"ClipSoul 设置", 11);
    TextOutW(dc, 38, 84, L"历史上限", 4);
    TextOutW(dc, 38, 124, L"暂停监听", 4);
    TextOutW(dc, 38, 158, L"开机自启", 4);
    TextOutW(dc, 38, 190, L"呼出热键", 4);
    TextOutW(dc, 38, 230, L"\u5b58\u50a8\u4f4d\u7f6e", 4);
    TextOutW(dc, 38, 284, L"主题颜色", 4);
    TextOutW(dc, 188, 85, limit_text_.c_str(), static_cast<int>(limit_text_.size()));
    const auto hotkey = capturing_hotkey_ ? std::wstring(L"按下新热键") : FormatHotkey(hotkey_modifiers_, hotkey_vk_);
    TextOutW(dc, 166, 190, hotkey.c_str(), static_cast<int>(hotkey.size()));
    RECT reset_hotkey_rect{280, 184, 314, 214};
    DrawCenteredText(dc, reset_hotkey_rect, L"默认");
    RECT storage_rect{46, 256, 218, 276};
    const auto storage_display = EllipsizeMiddle(dc, storage_path_, storage_rect.right - storage_rect.left);
    SetTextColor(dc, muted_color);
    DrawTextW(dc, storage_display.c_str(), static_cast<int>(storage_display.size()), &storage_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(dc, text_color);
    RECT browse_rect{232, 252, 296, 282};
    DrawCenteredText(dc, browse_rect, L"\u6d4f\u89c8");
    RECT theme_rect{38, 306, 296, 334};
    DrawCenteredText(dc, theme_rect, L"\u6b63\u5728\u5f00\u53d1\u4e2d");
    SetTextColor(dc, muted_color);
    TextOutW(dc, 282, 85, L"条", 1);
    TextOutW(dc, 38, 338, L"\u4fee\u6539\u5b58\u50a8\u4f4d\u7f6e\u540e\u9700\u8981\u91cd\u542f\u751f\u6548", 13);
    if (storage_save_failed_) {
        SetTextColor(dc, RGB(229, 72, 77));
        TextOutW(dc, 166, 338, L"\u5b58\u50a8\u4f4d\u7f6e\u4fdd\u5b58\u5931\u8d25", 8);
    }
    if (hotkey_conflict_) {
        SetTextColor(dc, RGB(229, 72, 77));
        TextOutW(dc, 150, 216, L"快捷键被占用", 6);
    }

    DrawToggle(dc, 236, 120, paused_, hover_target_ == 3 ? hover_progress_ : 0.0f, palette);
    DrawToggle(dc, 236, 154, startup_, hover_target_ == 4 ? hover_progress_ : 0.0f, palette);

    SetTextColor(dc, RGB(255, 255, 255));
    RECT save_rect{220, 346, 294, 374};
    DrawCenteredText(dc, save_rect, L"保存");

    if (hover_target_ == 1 && hover_progress_ > 0.0f) {
        DrawRoundRect(dc, kCloseBoxLeft, kCloseBoxTop, kCloseBoxLeft + kCloseBoxSize, kCloseBoxTop + kCloseBoxSize,
                      8, RGB(255, 241, 242), RGB(242, 85, 90));
    }
    HPEN close_pen = CreatePen(PS_SOLID, 2, hover_target_ == 1 ? RGB(229, 72, 77) : text_color);
    HGDIOBJ old_pen = SelectObject(dc, close_pen);
    MoveToEx(dc, kCloseBoxLeft + 8, kCloseBoxTop + 8, nullptr);
    LineTo(dc, kCloseBoxLeft + 16, kCloseBoxTop + 16);
    MoveToEx(dc, kCloseBoxLeft + 16, kCloseBoxTop + 8, nullptr);
    LineTo(dc, kCloseBoxLeft + 8, kCloseBoxTop + 16);
    SelectObject(dc, old_pen);
    DeleteObject(close_pen);

    SelectObject(dc, old_font);
    BitBlt(paint_dc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(hwnd_, &ps);
}

LRESULT SettingsWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT:
        Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CHAR:
        if (capturing_hotkey_) {
            return 0;
        }
        if (!editing_limit_) {
            return 0;
        }
        if (wparam == VK_BACK) {
            if (!limit_text_.empty()) {
                limit_text_.pop_back();
            }
        } else if (wparam >= L'0' && wparam <= L'9' && limit_text_.size() < 4) {
            if (replace_limit_on_next_digit_) {
                limit_text_.clear();
                replace_limit_on_next_digit_ = false;
            }
            limit_text_.push_back(static_cast<wchar_t>(wparam));
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (capturing_hotkey_) {
            SetCapturedHotkey(wparam);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wparam == kSettingsHoverTimer) {
            hover_progress_ = std::min(1.0f, hover_progress_ + 0.18f);
            if (hover_progress_ >= 1.0f) KillTimer(hwnd_, kSettingsHoverTimer);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        if (!tracking_mouse_) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&track);
            tracking_mouse_ = true;
        }
        const int target = HitTarget(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (target != hover_target_) {
            hover_target_ = target;
            hover_progress_ = 0.0f;
            SetTimer(hwnd_, kSettingsHoverTimer, 16, nullptr);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_mouse_ = false;
        hover_target_ = 0;
        hover_progress_ = 0.0f;
        KillTimer(hwnd_, kSettingsHoverTimer);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd_);
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);
        const int target = HitTarget(x, y);
        if (target == 1) {
            ShowWindow(hwnd_, SW_HIDE);
            return 0;
        }
        editing_limit_ = target == kTargetLimit;
        if (editing_limit_) {
            replace_limit_on_next_digit_ = true;
        }
        if (target == 2) {
            if (SaveFromControls()) {
                ShowWindow(hwnd_, SW_HIDE);
            }
            return 0;
        }
        if (target == 3) {
            TogglePause();
            return 0;
        }
        if (target == 4) {
            ToggleStartup();
            return 0;
        }
        if (target == 5) {
            StartHotkeyCapture();
            return 0;
        }
        if (target == kTargetHotkeyReset) {
            ResetHotkeyToDefault();
            return 0;
        }
        if (target == kTargetStorageBrowse) {
            BrowseStorageDirectory();
            return 0;
        }
        return 0;
    }
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &point);
        if (point.y >= 0 && point.y <= 54 && point.x < kCloseBoxLeft) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_CLOSE:
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

LRESULT CALLBACK SettingsWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    SettingsWindow* window = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        window = static_cast<SettingsWindow*>(create->lpCreateParams);
        window->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    return window ? window->HandleMessage(message, wparam, lparam)
                  : DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace ClipSoul
