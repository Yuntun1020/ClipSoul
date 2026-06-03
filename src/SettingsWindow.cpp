#include "ClipSoul/SettingsWindow.h"

#include "ClipSoul/App.h"
#include "ClipSoul/Hotkey.h"
#include "ClipSoul/PopupLayout.h"
#include "ClipSoul/Version.h"
#include "ClipSoul/Win32Util.h"

#include <ShObjIdl.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

namespace ClipSoul {
namespace {
constexpr wchar_t kSettingsClass[] = L"ClipSoul.SettingsWindow";
constexpr int kSettingsWidth = 640;
constexpr int kSettingsHeight = 638;
constexpr int kCloseBoxLeft = kSettingsWidth - 38;
constexpr int kCloseBoxTop = 14;
constexpr int kCloseBoxSize = 24;
constexpr int kContentRight = kSettingsWidth - 38;
constexpr int kStorageBoxRight = kSettingsWidth - 112;
constexpr int kStorageBrowseLeft = kSettingsWidth - 102;
constexpr int kStorageBrowseRight = kSettingsWidth - 38;
constexpr int kSaveLeft = kSettingsWidth - 114;
constexpr int kSaveRight = kSettingsWidth - 40;
constexpr int kSaveTop = 582;
constexpr int kSaveBottom = 610;
constexpr int kStorageNoticeTop = 426;
constexpr int kStorageNoticeBottom = 444;
constexpr int kProjectSectionLeft = 36;
constexpr int kProjectSectionTop = 458;
constexpr int kProjectSectionRight = kContentRight;
constexpr int kProjectSectionBottom = 574;
constexpr int kProjectButtonLeft = 54;
constexpr int kProjectButtonTop = 500;
constexpr int kProjectButtonRight = 222;
constexpr int kProjectButtonBottom = 544;
constexpr int kLimitEditId = 6101;
constexpr int kLimitBoxLeft = 162;
constexpr int kLimitBoxTop = 78;
constexpr int kLimitBoxRight = 276;
constexpr int kLimitBoxBottom = 108;
constexpr int kLimitEditLeft = 170;
constexpr int kLimitEditTop = 82;
constexpr int kLimitEditWidth = 98;
constexpr int kLimitEditHeight = 22;
constexpr int kTargetLimit = 9;
constexpr int kTargetHotkeyReset = 10;
constexpr int kTargetStorageBrowse = 11;
constexpr int kTargetContinuousPasteHotkey = 12;
constexpr int kTargetContinuousPasteHotkeyReset = 13;
constexpr int kTargetPopupResizable = 14;
constexpr int kTargetPopupSizeReset = 15;
constexpr int kTargetProjectUrl = 16;
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

int TextWidth(HDC dc, std::wstring_view value) {
    SIZE size{};
    GetTextExtentPoint32W(dc, value.data(), static_cast<int>(value.size()), &size);
    return size.cx;
}

std::vector<std::wstring> WrapPathText(HDC dc, std::wstring_view value, int max_width, int max_lines) {
    std::vector<std::wstring> lines;
    if (value.empty() || max_lines <= 0) {
        return lines;
    }

    size_t start = 0;
    while (start < value.size() && static_cast<int>(lines.size()) < max_lines) {
        size_t end = start + 1;
        size_t last_break = start;
        while (end <= value.size()) {
            const auto segment = value.substr(start, end - start);
            if (TextWidth(dc, segment) > max_width && end > start + 1) {
                break;
            }
            if (end < value.size()) {
                const wchar_t ch = value[end - 1];
                if (ch == L'\\' || ch == L'/' || ch == L'-' || ch == L'_' || ch == L' ') {
                    last_break = end;
                }
            }
            ++end;
        }

        size_t line_end = std::min(end - 1, value.size());
        if (line_end < value.size() && last_break > start) {
            line_end = last_break;
        }
        if (static_cast<int>(lines.size()) + 1 == max_lines) {
            line_end = value.size();
        }
        lines.emplace_back(value.substr(start, line_end - start));
        start = line_end;
        while (start < value.size() && value[start] == L' ') {
            ++start;
        }
    }
    return lines;
}

int HitTarget(int x, int y) {
    if (InRect(x, y, kCloseBoxLeft, kCloseBoxTop, kCloseBoxLeft + kCloseBoxSize, kCloseBoxTop + kCloseBoxSize)) return 1;
    if (InRect(x, y, kSaveLeft, kSaveTop, kSaveRight, kSaveBottom)) return 2;
    if (InRect(x, y, kLimitBoxLeft, kLimitBoxTop, kLimitBoxRight, kLimitBoxBottom)) return kTargetLimit;
    if (InRect(x, y, 222, 116, 288, 146)) return 3;
    if (InRect(x, y, 222, 150, 288, 180)) return 4;
    if (InRect(x, y, 150, 184, 276, 214)) return 5;
    if (InRect(x, y, 280, 184, 326, 214)) return kTargetHotkeyReset;
    if (InRect(x, y, 150, 218, 276, 248)) return kTargetContinuousPasteHotkey;
    if (InRect(x, y, 280, 218, 326, 248)) return kTargetContinuousPasteHotkeyReset;
    if (InRect(x, y, kStorageBrowseLeft, 294, kStorageBrowseRight, 324)) return kTargetStorageBrowse;
    if (InRect(x, y, 222, 390, 266, 420)) return kTargetPopupResizable;
    if (InRect(x, y, 274, 390, 326, 420)) return kTargetPopupSizeReset;
    if (InRect(x, y, kProjectButtonLeft, kProjectButtonTop, kProjectButtonRight, kProjectButtonBottom)) {
        return kTargetProjectUrl;
    }
    return 0;
}

void FillSolid(HDC dc, const RECT& rect, COLORREF fill) {
    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}
} // namespace

SettingsWindow::SettingsWindow(HINSTANCE instance, App& app)
    : instance_(instance),
      app_(app) {}

SettingsWindow::~SettingsWindow() {
    if (limit_edit_brush_) {
        DeleteObject(limit_edit_brush_);
        limit_edit_brush_ = nullptr;
    }
}

bool SettingsWindow::Create(HWND owner) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = SettingsWindow::WindowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kSettingsClass;
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kSettingsClass, L"ClipSoul 设置",
                            WS_POPUP | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, kSettingsWidth, kSettingsHeight,
                            owner, nullptr, instance_, this);
    if (!hwnd_) {
        return false;
    }

    limit_edit_ = CreateWindowExW(0, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL,
                                  kLimitEditLeft, kLimitEditTop, kLimitEditWidth, kLimitEditHeight,
                                  hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLimitEditId)),
                                  instance_, nullptr);
    if (!limit_edit_) {
        return false;
    }
    SendMessageW(limit_edit_, EM_SETLIMITTEXT, 4, 0);
    SendMessageW(limit_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

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
    if (limit_edit_) {
        SetWindowTextW(limit_edit_, limit_text_.c_str());
        SendMessageW(limit_edit_, EM_SETSEL, 0, -1);
    }
    storage_path_ = app_.StorageDirectory().wstring();
    storage_save_failed_ = false;
    paused_ = settings.paused;
    startup_ = settings.start_with_windows;
    hotkey_modifiers_ = settings.hotkey_modifiers;
    hotkey_vk_ = settings.hotkey_vk;
    continuous_paste_hotkey_modifiers_ = settings.continuous_paste_hotkey_modifiers;
    continuous_paste_hotkey_vk_ = settings.continuous_paste_hotkey_vk;
    theme_mode_ = std::clamp(settings.theme_mode, 0, 2);
    popup_resizable_ = settings.popup_resizable;
    capturing_hotkey_ = false;
    capturing_continuous_paste_hotkey_ = false;
    hotkey_conflict_ = false;
    continuous_paste_hotkey_conflict_ = false;
}

bool SettingsWindow::SaveFromControls() {
    if (limit_edit_) {
        const int length = GetWindowTextLengthW(limit_edit_);
        std::wstring value(static_cast<size_t>(length) + 1, L'\0');
        if (length > 0) {
            GetWindowTextW(limit_edit_, value.data(), length + 1);
        }
        value.resize(static_cast<size_t>(length));
        limit_text_ = value;
    }
    auto settings = app_.Settings();
    hotkey_conflict_ = !HotkeyAvailableForSelection(hotkey_modifiers_, hotkey_vk_, false);
    continuous_paste_hotkey_conflict_ =
        !HotkeyAvailableForSelection(continuous_paste_hotkey_modifiers_, continuous_paste_hotkey_vk_, true);
    if (hotkey_conflict_ || continuous_paste_hotkey_conflict_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }
    settings.history_limit = std::max(1, _wtoi(limit_text_.c_str()));
    settings.paused = paused_;
    settings.start_with_windows = startup_;
    settings.hotkey_modifiers = hotkey_modifiers_;
    settings.hotkey_vk = hotkey_vk_;
    settings.continuous_paste_hotkey_modifiers = continuous_paste_hotkey_modifiers_;
    settings.continuous_paste_hotkey_vk = continuous_paste_hotkey_vk_;
    settings.theme_mode = theme_mode_;
    settings.popup_resizable = popup_resizable_;
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

void SettingsWindow::TogglePopupResizable() {
    popup_resizable_ = !popup_resizable_;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::ResetPopupSize() {
    app_.ResetPopupSize();
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
    hotkey_conflict_ = !HotkeyAvailableForSelection(hotkey_modifiers_, hotkey_vk_, false);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::ResetContinuousPasteHotkeyToDefault() {
    continuous_paste_hotkey_modifiers_ = kDefaultContinuousPasteHotkeyModifiers;
    continuous_paste_hotkey_vk_ = kDefaultContinuousPasteHotkeyVk;
    continuous_paste_hotkey_conflict_ =
        !HotkeyAvailableForSelection(continuous_paste_hotkey_modifiers_, continuous_paste_hotkey_vk_, true);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::StartHotkeyCapture(bool continuous_paste) {
    capturing_hotkey_ = true;
    capturing_continuous_paste_hotkey_ = continuous_paste;
    if (continuous_paste) {
        continuous_paste_hotkey_conflict_ = false;
    } else {
        hotkey_conflict_ = false;
    }
    SetFocus(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::SetCapturedHotkey(WPARAM vk) {
    const auto hotkey = BuildCapturedHotkey(static_cast<unsigned>(vk),
                                            (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                                            (GetKeyState(VK_MENU) & 0x8000) != 0,
                                            (GetKeyState(VK_SHIFT) & 0x8000) != 0,
                                            (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
                                                (GetKeyState(VK_RWIN) & 0x8000) != 0);
    if (!hotkey) {
        return;
    }
    if (capturing_continuous_paste_hotkey_) {
        continuous_paste_hotkey_modifiers_ = hotkey->modifiers;
        continuous_paste_hotkey_vk_ = hotkey->vk;
        continuous_paste_hotkey_conflict_ =
            !HotkeyAvailableForSelection(continuous_paste_hotkey_modifiers_, continuous_paste_hotkey_vk_, true);
    } else {
        hotkey_modifiers_ = hotkey->modifiers;
        hotkey_vk_ = hotkey->vk;
        hotkey_conflict_ = !HotkeyAvailableForSelection(hotkey_modifiers_, hotkey_vk_, false);
    }
    capturing_hotkey_ = false;
    capturing_continuous_paste_hotkey_ = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool SettingsWindow::HotkeyAvailableForSelection(unsigned modifiers, unsigned vk, bool continuous_paste) const {
    if (continuous_paste) {
        if (modifiers == hotkey_modifiers_ && vk == hotkey_vk_) {
            return false;
        }
    } else if (modifiers == continuous_paste_hotkey_modifiers_ && vk == continuous_paste_hotkey_vk_) {
        return false;
    }
    return app_.HotkeyAvailable(modifiers, vk);
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
        DrawRoundRect(dc, left, top, right, bottom, radius,
                      Mix(panel_fill, dark ? RGB(54, 63, 77) : RGB(244, 255, 253), hover * 0.45f),
                      Mix(border_color, accent_color, hover * 0.38f));
    };

    const bool limit_focused = limit_edit_ && GetFocus() == limit_edit_;
    DrawRoundRect(dc, kLimitBoxLeft, kLimitBoxTop, kLimitBoxRight, kLimitBoxBottom, 10,
                  limit_focused ? Mix(search_fill, dark ? RGB(54, 63, 77) : RGB(244, 255, 253), 0.65f)
                                : (hover_target_ == kTargetLimit
                                       ? Mix(search_fill, dark ? RGB(54, 63, 77) : RGB(244, 255, 253),
                                             hover_progress_ * 0.45f)
                                       : search_fill),
                  limit_focused ? accent_color
                                : (hover_target_ == kTargetLimit
                                       ? Mix(border_color, accent_color, hover_progress_ * 0.38f)
                                       : border_color));
    hover_rect(3, 222, 116, 288, 146, 15);
    hover_rect(4, 222, 150, 288, 180, 15);
    hover_rect(kTargetPopupResizable, 222, 390, 266, 420, 15);
    hover_rect(kTargetPopupSizeReset, 274, 390, 326, 420, 12);
    DrawRoundRect(dc, 150, 184, 276, 214, 10,
                  capturing_hotkey_ && !capturing_continuous_paste_hotkey_ ? RGB(221, 252, 248)
                                    : (hover_target_ == 5 ? Mix(search_fill, dark ? RGB(54, 63, 77) : RGB(244, 255, 253), hover_progress_ * 0.45f)
                                                          : search_fill),
                  capturing_hotkey_ && !capturing_continuous_paste_hotkey_ ? accent_color
                                    : (hover_target_ == 5 ? Mix(border_color, accent_color, hover_progress_ * 0.38f)
                                                          : border_color));
    DrawRoundRect(dc, 150, 218, 276, 248, 10,
                  capturing_hotkey_ && capturing_continuous_paste_hotkey_ ? RGB(221, 252, 248)
                                    : (hover_target_ == kTargetContinuousPasteHotkey
                                           ? Mix(search_fill, dark ? RGB(54, 63, 77) : RGB(244, 255, 253),
                                                 hover_progress_ * 0.45f)
                                           : search_fill),
                  capturing_hotkey_ && capturing_continuous_paste_hotkey_
                      ? accent_color
                      : (hover_target_ == kTargetContinuousPasteHotkey
                             ? Mix(border_color, accent_color, hover_progress_ * 0.38f)
                             : border_color));
    DrawRoundRect(dc, 36, 284, kStorageBoxRight, 332, 10,
                  dark ? RGB(34, 40, 51) : RGB(244, 248, 252),
                  dark ? RGB(62, 70, 84) : RGB(220, 232, 244));
    DrawRoundRect(dc, kStorageBrowseLeft, 294, kStorageBrowseRight, 324, 12,
                  hover_target_ == kTargetStorageBrowse
                      ? Mix(search_fill, dark ? RGB(54, 63, 77) : RGB(244, 255, 253), hover_progress_ * 0.45f)
                      : search_fill,
                  hover_target_ == kTargetStorageBrowse ? Mix(border_color, accent_color, hover_progress_ * 0.38f)
                                                        : border_color);
    DrawRoundRect(dc, 36, 354, kContentRight, 386, 14,
                  dark ? RGB(34, 40, 51) : RGB(244, 248, 252),
                  dark ? RGB(62, 70, 84) : RGB(220, 232, 244));
    DrawRoundRect(dc, kProjectSectionLeft, kProjectSectionTop, kProjectSectionRight, kProjectSectionBottom, 14,
                  dark ? RGB(34, 40, 51) : RGB(244, 248, 252),
                  dark ? RGB(62, 70, 84) : RGB(220, 232, 244));
    DrawRoundRect(dc, kProjectButtonLeft, kProjectButtonTop, kProjectButtonRight, kProjectButtonBottom, 12,
                  hover_target_ == kTargetProjectUrl
                      ? Mix(search_fill, dark ? RGB(54, 63, 77) : RGB(244, 255, 253), hover_progress_ * 0.45f)
                      : search_fill,
                  hover_target_ == kTargetProjectUrl ? Mix(border_color, accent_color, hover_progress_ * 0.38f)
                                                     : border_color);
    DrawRoundRect(dc, kSaveLeft, kSaveTop, kSaveRight, kSaveBottom, 12, accent_color, accent_color);

    auto font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ old_font = SelectObject(dc, font);
    SetTextColor(dc, text_color);
    TextOutW(dc, 22, 20, L"ClipSoul 设置", 11);
    TextOutW(dc, 38, 84, L"历史上限", 4);
    TextOutW(dc, 38, 124, L"暂停监听", 4);
    TextOutW(dc, 38, 158, L"开机自启", 4);
    TextOutW(dc, 38, 190, L"呼出热键", 4);
    TextOutW(dc, 38, 224, L"连续粘贴", 4);
    TextOutW(dc, 38, 264, L"\u5b58\u50a8\u4f4d\u7f6e", 4);
    TextOutW(dc, 38, 334, L"主题颜色", 4);
    TextOutW(dc, 38, 394, L"窗口缩放", 4);
    const auto hotkey = capturing_hotkey_ && !capturing_continuous_paste_hotkey_
                            ? std::wstring(L"按下新热键")
                            : FormatHotkey(hotkey_modifiers_, hotkey_vk_);
    TextOutW(dc, 166, 190, hotkey.c_str(), static_cast<int>(hotkey.size()));
    RECT reset_hotkey_rect{280, 184, 326, 214};
    DrawCenteredText(dc, reset_hotkey_rect, L"默认");
    const auto continuous_hotkey = capturing_hotkey_ && capturing_continuous_paste_hotkey_
                                       ? std::wstring(L"按下新热键")
                                       : FormatHotkey(continuous_paste_hotkey_modifiers_,
                                                      continuous_paste_hotkey_vk_);
    TextOutW(dc, 166, 224, continuous_hotkey.c_str(), static_cast<int>(continuous_hotkey.size()));
    RECT reset_continuous_hotkey_rect{280, 218, 326, 248};
    DrawCenteredText(dc, reset_continuous_hotkey_rect, L"默认");
    SetTextColor(dc, muted_color);
    const int storage_text_width = kStorageBoxRight - 54;
    const auto storage_lines = WrapPathText(dc, storage_path_, storage_text_width, 2);
    for (size_t line_index = 0; line_index < storage_lines.size(); ++line_index) {
        RECT storage_rect{46, 290 + static_cast<int>(line_index) * 18, kStorageBoxRight - 8,
                          308 + static_cast<int>(line_index) * 18};
        DrawTextW(dc, storage_lines[line_index].c_str(), static_cast<int>(storage_lines[line_index].size()),
                  &storage_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    SetTextColor(dc, text_color);
    RECT browse_rect{kStorageBrowseLeft, 294, kStorageBrowseRight, 324};
    DrawCenteredText(dc, browse_rect, L"\u6d4f\u89c8");
    RECT theme_rect{38, 356, kContentRight - 2, 384};
    DrawCenteredText(dc, theme_rect, L"\u6b63\u5728\u5f00\u53d1\u4e2d");
    SetTextColor(dc, muted_color);
    TextOutW(dc, 282, 85, L"条", 1);
    RECT storage_notice_rect{38, kStorageNoticeTop, kSaveLeft - 12, kStorageNoticeBottom};
    DrawTextW(dc, L"\u4fee\u6539\u5b58\u50a8\u4f4d\u7f6e\u540e\u9700\u8981\u91cd\u542f\u751f\u6548", 13,
              &storage_notice_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (storage_save_failed_) {
        SetTextColor(dc, RGB(229, 72, 77));
        TextOutW(dc, 38, 448, L"\u5b58\u50a8\u4f4d\u7f6e\u4fdd\u5b58\u5931\u8d25", 8);
    }
    if (hotkey_conflict_) {
        SetTextColor(dc, RGB(229, 72, 77));
        TextOutW(dc, 150, 216, L"快捷键被占用", 6);
    }
    if (continuous_paste_hotkey_conflict_) {
        SetTextColor(dc, RGB(229, 72, 77));
        TextOutW(dc, 150, 250, L"快捷键被占用", 6);
    }

    DrawToggle(dc, 236, 120, paused_, hover_target_ == 3 ? hover_progress_ : 0.0f, palette);
    DrawToggle(dc, 236, 154, startup_, hover_target_ == 4 ? hover_progress_ : 0.0f, palette);
    DrawToggle(dc, 225, 394, popup_resizable_,
               hover_target_ == kTargetPopupResizable ? hover_progress_ : 0.0f, palette);
    SetTextColor(dc, text_color);
    RECT reset_size_rect{274, 390, 326, 420};
    DrawCenteredText(dc, reset_size_rect, L"默认");

    SetTextColor(dc, muted_color);
    TextOutW(dc, 54, 472, L"\u9879\u76ee\u5730\u5740", 4);
    SetTextColor(dc, text_color);
    TextOutW(dc, 184, 472, kClipSoulProjectDisplayUrl.data(), static_cast<int>(kClipSoulProjectDisplayUrl.size()));
    RECT project_url_rect{kProjectButtonLeft, kProjectButtonTop, kProjectButtonRight, kProjectButtonBottom};
    DrawCenteredText(dc, project_url_rect, L"\u6253\u5f00\u9879\u76ee\u4e3b\u9875");
    SetTextColor(dc, muted_color);
    TextOutW(dc, 54, 552, L"\u5f53\u524d\u7248\u672c", 4);
    SetTextColor(dc, text_color);
    TextOutW(dc, 184, 552, kClipSoulVersion.data(), static_cast<int>(kClipSoulVersion.size()));

    SetTextColor(dc, RGB(255, 255, 255));
    RECT save_rect{kSaveLeft, kSaveTop, kSaveRight, kSaveBottom};
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
    case WM_KEYDOWN:
        if (capturing_hotkey_) {
            SetCapturedHotkey(wparam);
            return 0;
        }
        break;
    case WM_SYSKEYDOWN:
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
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lparam) == limit_edit_) {
            const auto palette = ResolvePopupThemePalette(theme_mode_, IsSystemDarkTheme());
            const COLORREF edit_fill = palette.dark ? RGB(38, 46, 60) : RGB(247, 251, 255);
            SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
            SetBkColor(reinterpret_cast<HDC>(wparam), edit_fill);
            SetTextColor(reinterpret_cast<HDC>(wparam), palette.dark ? RGB(238, 242, 247) : RgbFromHex(palette.text));
            if (!limit_edit_brush_ || limit_edit_brush_color_ != edit_fill) {
                if (limit_edit_brush_) {
                    DeleteObject(limit_edit_brush_);
                }
                limit_edit_brush_ = CreateSolidBrush(edit_fill);
                limit_edit_brush_color_ = edit_fill;
            }
            return reinterpret_cast<LRESULT>(limit_edit_brush_);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wparam) == kLimitEditId && reinterpret_cast<HWND>(lparam) == limit_edit_) {
            if (HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS) {
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            if (HIWORD(wparam) == EN_CHANGE) {
                const int length = GetWindowTextLengthW(limit_edit_);
                std::wstring value(static_cast<size_t>(length) + 1, L'\0');
                if (length > 0) {
                    GetWindowTextW(limit_edit_, value.data(), length + 1);
                }
                value.resize(static_cast<size_t>(length));
                limit_text_ = value;
                return 0;
            }
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
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);
        const int target = HitTarget(x, y);
        if (target == kTargetLimit) {
            SetFocus(limit_edit_);
            SendMessageW(limit_edit_, EM_SETSEL, 0, -1);
            return 0;
        }
        SetFocus(hwnd_);
        if (target == 1) {
            ShowWindow(hwnd_, SW_HIDE);
            return 0;
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
        if (target == kTargetPopupResizable) {
            TogglePopupResizable();
            return 0;
        }
        if (target == kTargetPopupSizeReset) {
            ResetPopupSize();
            return 0;
        }
        if (target == 5) {
            StartHotkeyCapture(false);
            return 0;
        }
        if (target == kTargetHotkeyReset) {
            ResetHotkeyToDefault();
            return 0;
        }
        if (target == kTargetContinuousPasteHotkey) {
            StartHotkeyCapture(true);
            return 0;
        }
        if (target == kTargetContinuousPasteHotkeyReset) {
            ResetContinuousPasteHotkeyToDefault();
            return 0;
        }
        if (target == kTargetStorageBrowse) {
            BrowseStorageDirectory();
            return 0;
        }
        if (target == kTargetProjectUrl) {
            ShellExecuteW(hwnd_, L"open", kClipSoulProjectUrl.data(), nullptr, nullptr, SW_SHOWNORMAL);
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
