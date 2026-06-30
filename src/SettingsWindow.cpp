#include "ClipSoul/SettingsWindow.h"

#include "ClipSoul/App.h"
#include "ClipSoul/Hotkey.h"
#include "ClipSoul/PopupLayout.h"
#include "ClipSoul/Version.h"
#include "ClipSoul/Win32Util.h"

#include <ShObjIdl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")

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
constexpr int kProjectButtonBottom = 536;
constexpr int kLimitEditId = 6101;
constexpr int kLimitBoxLeft = 162;
constexpr int kLimitBoxTop = 78;
constexpr int kLimitBoxRight = 276;
constexpr int kLimitBoxBottom = 108;
constexpr int kLimitEditLeft = 170;
constexpr int kLimitEditTop = 83;
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
constexpr int kTargetThemeSystem = 17;
constexpr int kTargetThemeLight = 18;
constexpr int kTargetThemeDark = 19;
constexpr int kSettingsCornerRadius = 18;
constexpr int kTextVisualCenterOffset = 1;
constexpr UINT_PTR kSettingsHoverTimer = 61;
constexpr UINT_PTR kSettingsToggleTimer = 62;

bool InRect(int x, int y, int left, int top, int right, int bottom) {
    return x >= left && x <= right && y >= top && y <= bottom;
}

void EnsureGdiplusStarted() {
    static const ULONG_PTR token = [] {
        Gdiplus::GdiplusStartupInput input{};
        ULONG_PTR value = 0;
        Gdiplus::GdiplusStartup(&value, &input, nullptr);
        return value;
    }();
    (void)token;
}

Gdiplus::Color GdiColor(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

void BuildRoundedPath(Gdiplus::GraphicsPath& path, float left, float top, float right, float bottom, float radius) {
    float diameter = radius * 2.0f;
    diameter = std::max(0.0f, std::min(diameter, std::min(right - left, bottom - top)));
    if (diameter <= 0.0f) {
        path.AddRectangle(Gdiplus::RectF(left, top, right - left, bottom - top));
        return;
    }
    path.AddArc(left, top, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(right - diameter, top, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(right - diameter, bottom - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(left, bottom - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

void DrawRoundRect(HDC dc, int left, int top, int right, int bottom, int radius,
                   COLORREF fill, COLORREF stroke) {
    EnsureGdiplusStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::GraphicsPath fill_path;
    BuildRoundedPath(fill_path, static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
                     static_cast<float>(bottom), static_cast<float>(radius));
    Gdiplus::SolidBrush fill_brush(GdiColor(fill));
    graphics.FillPath(&fill_brush, &fill_path);
    Gdiplus::GraphicsPath stroke_path;
    BuildRoundedPath(stroke_path, static_cast<float>(left) + 0.5f, static_cast<float>(top) + 0.5f,
                     static_cast<float>(right) - 0.5f, static_cast<float>(bottom) - 0.5f,
                     std::max(0.0f, static_cast<float>(radius) - 0.5f));
    Gdiplus::Pen pen(GdiColor(stroke), 1.0f);
    graphics.DrawPath(&pen, &stroke_path);
}

void DrawRoundRectFill(HDC dc, int left, int top, int right, int bottom, int radius, COLORREF fill) {
    EnsureGdiplusStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::GraphicsPath path;
    BuildRoundedPath(path, static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
                     static_cast<float>(bottom), static_cast<float>(radius));
    Gdiplus::SolidBrush brush(GdiColor(fill));
    graphics.FillPath(&brush, &path);
}

void DrawRoundRectOutline(HDC dc, int left, int top, int right, int bottom, int radius, COLORREF stroke) {
    EnsureGdiplusStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::GraphicsPath path;
    BuildRoundedPath(path, static_cast<float>(left) + 0.5f, static_cast<float>(top) + 0.5f,
                     static_cast<float>(right) - 0.5f, static_cast<float>(bottom) - 0.5f,
                     std::max(0.0f, static_cast<float>(radius) - 0.5f));
    Gdiplus::Pen pen(GdiColor(stroke), 1.0f);
    graphics.DrawPath(&pen, &path);
}

void DrawEllipse(HDC dc, float left, float top, float right, float bottom, COLORREF fill, COLORREF stroke) {
    EnsureGdiplusStarted();
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::SolidBrush brush(GdiColor(fill));
    graphics.FillEllipse(&brush, left, top, right - left, bottom - top);
    Gdiplus::Pen pen(GdiColor(stroke), 1.0f);
    graphics.DrawEllipse(&pen, left + 0.5f, top + 0.5f, right - left - 1.0f, bottom - top - 1.0f);
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

void DrawSectionFrame(HDC dc, int left, int top, int right, int bottom, const PopupThemePalette& palette) {
    const COLORREF fill =
        Mix(RgbFromHex(palette.panel_fill), RgbFromHex(palette.paper_hover), palette.dark ? 0.16f : 0.22f);
    DrawRoundRectFill(dc, left, top, right, bottom, 14, fill);
}

void DrawToggle(HDC dc, int x, int y, bool active, float hover, float knob_position,
                const PopupThemePalette& palette) {
    const COLORREF active_color = RgbFromHex(palette.accent);
    const COLORREF accent_hover = RgbFromHex(palette.accent_hover);
    const COLORREF idle_fill = RgbFromHex(palette.search_fill);
    const COLORREF idle_hover = RgbFromHex(palette.paper_hover);
    const COLORREF fill = active ? Mix(active_color, accent_hover, hover) : Mix(idle_fill, idle_hover, hover);
    const COLORREF stroke =
        active ? fill : Mix(RgbFromHex(palette.border_hover), RgbFromHex(palette.border_selected), hover);
    DrawRoundRect(dc, x, y, x + 38, y + 22, 11, fill, stroke);
    knob_position = std::clamp(knob_position, 0.0f, 1.0f);
    const float cx = static_cast<float>(x + 11) + 16.0f * knob_position;
    const COLORREF knob_fill = active ? RgbFromHex(palette.search_fill) : RgbFromHex(palette.muted);
    const COLORREF knob_stroke = active ? Mix(RgbFromHex(palette.search_fill), RgbFromHex(palette.border), 0.45f)
                                        : RgbFromHex(palette.border_selected);
    DrawEllipse(dc, cx - 6.5f, static_cast<float>(y) + 4.5f, cx + 6.5f, static_cast<float>(y) + 17.5f,
                knob_fill, knob_stroke);
}

COLORREF SettingsRowFill(const PopupThemePalette& palette) {
    return RgbFromHex(palette.card_fill);
}

COLORREF SettingsRowBorder(const PopupThemePalette& palette) {
    return RgbFromHex(palette.border);
}

COLORREF SettingsStrongBorder(const PopupThemePalette& palette) {
    return RgbFromHex(palette.border_selected);
}

COLORREF SettingsHoverFill(const PopupThemePalette& palette) {
    return RgbFromHex(palette.paper_hover);
}

COLORREF SettingsActiveFill(const PopupThemePalette& palette) {
    return RgbFromHex(palette.paper_selected);
}

void DrawCenteredText(HDC dc, const RECT& rect, const wchar_t* text) {
    RECT adjusted = rect;
    OffsetRect(&adjusted, 0, kTextVisualCenterOffset);
    DrawTextW(dc, text, static_cast<int>(wcslen(text)), &adjusted,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawVCenterText(HDC dc, const RECT& rect, const wchar_t* text) {
    RECT adjusted = rect;
    OffsetRect(&adjusted, 0, kTextVisualCenterOffset);
    DrawTextW(dc, text, static_cast<int>(wcslen(text)), &adjusted,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void DrawVCenterText(HDC dc, const RECT& rect, std::wstring_view text, UINT align = DT_LEFT) {
    RECT adjusted = rect;
    OffsetRect(&adjusted, 0, kTextVisualCenterOffset);
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &adjusted,
              align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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
    const auto theme_layout = SettingsWindowControlLayout();
    switch (HitTestSettingsThemeTarget(theme_layout, static_cast<float>(x), static_cast<float>(y))) {
    case SettingsThemeTarget::System:
        return kTargetThemeSystem;
    case SettingsThemeTarget::Light:
        return kTargetThemeLight;
    case SettingsThemeTarget::Dark:
        return kTargetThemeDark;
    case SettingsThemeTarget::None:
        break;
    }
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
    SetWindowTheme(limit_edit_, L"", L"");

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

void SettingsWindow::RefreshTheme() {
    ApplyBackdrop();
    if (limit_edit_brush_) {
        DeleteObject(limit_edit_brush_);
        limit_edit_brush_ = nullptr;
        limit_edit_brush_color_ = CLR_INVALID;
    }
    if (limit_edit_) {
        const BOOL redraw = FALSE;
        SendMessageW(limit_edit_, WM_SETREDRAW, redraw, 0);
        SetWindowTheme(limit_edit_, L"", L"");
        SendMessageW(limit_edit_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(limit_edit_, nullptr, TRUE);
        RedrawWindow(limit_edit_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
    }
    if (hwnd_) {
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void SettingsWindow::LoadToControls() {
    const auto settings = app_.Settings();
    limit_text_ = std::to_wstring(settings.history_limit);
    if (limit_edit_) {
        SetWindowTextW(limit_edit_, limit_text_.c_str());
        if (SettingsLimitEditShouldSelectAllOnLoad()) {
            SendMessageW(limit_edit_, EM_SETSEL, 0, -1);
        } else {
            SendMessageW(limit_edit_, EM_SETSEL, limit_text_.size(), limit_text_.size());
        }
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
    toggle_motion_target_ = 0;
    toggle_motion_progress_ = 1.0f;
    if (hwnd_) {
        KillTimer(hwnd_, kSettingsToggleTimer);
    }
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
    const bool from = paused_;
    paused_ = !paused_;
    StartToggleMotion(3, from, paused_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::ToggleStartup() {
    const bool from = startup_;
    startup_ = !startup_;
    StartToggleMotion(4, from, startup_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::TogglePopupResizable() {
    const bool from = popup_resizable_;
    popup_resizable_ = !popup_resizable_;
    StartToggleMotion(kTargetPopupResizable, from, popup_resizable_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::StartToggleMotion(int target, bool from_active, bool to_active) {
    toggle_motion_target_ = target;
    toggle_motion_from_active_ = from_active;
    toggle_motion_to_active_ = to_active;
    toggle_motion_progress_ = 0.0f;
    SetTimer(hwnd_, kSettingsToggleTimer, 16, nullptr);
}

float SettingsWindow::ToggleKnobPosition(int target, bool active) const {
    if (toggle_motion_target_ == target && toggle_motion_progress_ < 1.0f) {
        return PopupToggleKnobPosition(toggle_motion_from_active_, toggle_motion_to_active_,
                                       toggle_motion_progress_);
    }
    return active ? 1.0f : 0.0f;
}

void SettingsWindow::AdvanceToggleMotion() {
    toggle_motion_progress_ = std::min(1.0f, toggle_motion_progress_ + 0.18f);
    if (toggle_motion_progress_ >= 1.0f) {
        toggle_motion_progress_ = 1.0f;
        toggle_motion_target_ = 0;
        KillTimer(hwnd_, kSettingsToggleTimer);
    }
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
    const int next_mode = std::clamp(mode, 0, 2);
    if (!SettingsThemePreviewShouldRefreshNativeControls(theme_mode_, next_mode)) {
        return;
    }
    theme_mode_ = next_mode;
    RefreshTheme();
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
    const DWMNCRENDERINGPOLICY non_client_policy = DWMNCRP_DISABLED;
    DwmSetWindowAttribute(hwnd_, DWMWA_NCRENDERING_POLICY, &non_client_policy, sizeof(non_client_policy));

    const BOOL dark = ThemeModeResolvesDarkChrome(theme_mode_, IsSystemDarkTheme()) ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    const DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    const COLORREF border_color = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border_color, sizeof(border_color));
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
    const COLORREF window_fill = RgbFromHex(palette.window_tint);
    const COLORREF panel_fill = RgbFromHex(palette.panel_fill);
    const COLORREF search_fill = RgbFromHex(palette.search_fill);
    const COLORREF text_color = RgbFromHex(palette.text);
    const COLORREF muted_color = RgbFromHex(palette.muted);
    const COLORREF border_color = RgbFromHex(palette.border);
    const COLORREF strong_border = SettingsStrongBorder(palette);
    const COLORREF accent_color = RgbFromHex(palette.accent);
    const COLORREF row_fill = SettingsRowFill(palette);
    const COLORREF row_border = SettingsRowBorder(palette);
    const COLORREF hover_fill = SettingsHoverFill(palette);
    const COLORREF active_fill = SettingsActiveFill(palette);
    const auto settings_layout = SettingsWindowControlLayout();

    const COLORREF frame_color = Mix(row_border, strong_border, palette.dark ? 0.38f : 0.48f);
    FillSolid(dc, rc, window_fill);
    DrawRoundRectFill(dc, 0, 0, rc.right, rc.bottom, kSettingsCornerRadius, window_fill);
    DrawRoundRectOutline(dc, 1, 1, rc.right - 1, rc.bottom - 1, kSettingsCornerRadius - 1, frame_color);
    DrawRoundRectFill(dc, 24, 62, rc.right - 24, rc.bottom - 22, 12, panel_fill);
    DrawSectionFrame(dc, 30, 70, kContentRight + 2, 252, palette);
    DrawSectionFrame(dc, 30, 256, kContentRight + 2, 424, palette);
    DrawSectionFrame(dc, 30, 458, kContentRight + 2, 576, palette);

    const bool limit_focused = limit_edit_ && GetFocus() == limit_edit_;
    DrawRoundRect(dc, kLimitBoxLeft, kLimitBoxTop, kLimitBoxRight, kLimitBoxBottom, 10,
                  limit_focused ? Mix(search_fill, hover_fill, 0.65f)
                                : (hover_target_ == kTargetLimit
                                       ? Mix(search_fill, hover_fill, hover_progress_ * 0.45f)
                                       : search_fill),
                  limit_focused ? accent_color
                                : (hover_target_ == kTargetLimit
                                       ? Mix(border_color, strong_border, hover_progress_)
                                       : border_color));
    DrawRoundRect(dc, 150, 184, 276, 214, 10,
                  capturing_hotkey_ && !capturing_continuous_paste_hotkey_ ? active_fill
                                    : (hover_target_ == 5 ? Mix(search_fill, hover_fill, hover_progress_ * 0.45f)
                                                          : search_fill),
                  capturing_hotkey_ && !capturing_continuous_paste_hotkey_ ? accent_color
                                    : (hover_target_ == 5 ? Mix(border_color, strong_border, hover_progress_)
                                                          : border_color));
    DrawRoundRect(dc, 150, 218, 276, 248, 10,
                  capturing_hotkey_ && capturing_continuous_paste_hotkey_ ? active_fill
                                    : (hover_target_ == kTargetContinuousPasteHotkey
                                           ? Mix(search_fill, hover_fill, hover_progress_ * 0.45f)
                                           : search_fill),
                  capturing_hotkey_ && capturing_continuous_paste_hotkey_
                      ? accent_color
                      : (hover_target_ == kTargetContinuousPasteHotkey
                             ? Mix(border_color, strong_border, hover_progress_)
                             : border_color));
    DrawRoundRect(dc, 36, 284, kStorageBoxRight, 332, 10,
                  search_fill,
                  row_border);
    DrawRoundRect(dc, kStorageBrowseLeft, 294, kStorageBrowseRight, 324, 12,
                  hover_target_ == kTargetStorageBrowse
                      ? Mix(search_fill, hover_fill, hover_progress_ * 0.45f)
                      : search_fill,
                  hover_target_ == kTargetStorageBrowse ? Mix(border_color, strong_border, hover_progress_)
                                                        : border_color);
    DrawRoundRectFill(dc, 36, 354, kContentRight, 386, 14,
                      Mix(row_fill, panel_fill, palette.dark ? 0.35f : 0.52f));
    const auto draw_theme_button = [&](const UiRect& rect, int mode, int target, const wchar_t* label) {
        const bool active = theme_mode_ == mode;
        const bool hovered = hover_target_ == target;
        const COLORREF fill =
            active ? active_fill
                   : (hovered ? Mix(search_fill, hover_fill, hover_progress_ * 0.45f) : search_fill);
        const COLORREF stroke =
            active ? strong_border
                   : (hovered ? Mix(border_color, strong_border, hover_progress_) : border_color);
        DrawRoundRect(dc, static_cast<int>(rect.left), static_cast<int>(rect.top),
                      static_cast<int>(rect.right), static_cast<int>(rect.bottom), 10, fill, stroke);
        if (active) {
            DrawRoundRectFill(dc, static_cast<int>(rect.left + 8.0f), static_cast<int>(rect.top + 11.0f),
                              static_cast<int>(rect.left + 12.0f), static_cast<int>(rect.top + 17.0f),
                              2, accent_color);
        }
        SetTextColor(dc, active ? text_color : muted_color);
        RECT label_rect{static_cast<int>(rect.left + (active ? 14.0f : 0.0f)), static_cast<int>(rect.top),
                        static_cast<int>(rect.right), static_cast<int>(rect.bottom)};
        DrawCenteredText(dc, label_rect, label);
    };
    draw_theme_button(settings_layout.theme_system, 0, kTargetThemeSystem, L"\u8ddf\u968f\u7cfb\u7edf");
    draw_theme_button(settings_layout.theme_light, 1, kTargetThemeLight, L"\u6d45\u8272");
    draw_theme_button(settings_layout.theme_dark, 2, kTargetThemeDark, L"\u6df1\u8272");
    SetTextColor(dc, text_color);
    DrawRoundRect(dc, kProjectButtonLeft, kProjectButtonTop, kProjectButtonRight, kProjectButtonBottom, 10,
                  hover_target_ == kTargetProjectUrl
                      ? Mix(search_fill, hover_fill, hover_progress_ * 0.35f)
                      : search_fill,
                  border_color);
    DrawRoundRect(dc, kSaveLeft, kSaveTop, kSaveRight, kSaveBottom, 12,
                  hover_target_ == 2 ? Mix(accent_color, RgbFromHex(palette.accent_hover), hover_progress_) : accent_color,
                  accent_color);

    auto font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ old_font = SelectObject(dc, font);
    SetTextColor(dc, text_color);
    TextOutW(dc, 22, 20, L"ClipSoul 设置", 11);
    HPEN brand_pen = CreatePen(PS_SOLID, 2, accent_color);
    HGDIOBJ old_brand_pen = SelectObject(dc, brand_pen);
    MoveToEx(dc, 24, 48, nullptr);
    LineTo(dc, 74, 48);
    SelectObject(dc, old_brand_pen);
    DeleteObject(brand_pen);
    brand_pen = CreatePen(PS_SOLID, 1, strong_border);
    old_brand_pen = SelectObject(dc, brand_pen);
    MoveToEx(dc, 80, 48, nullptr);
    LineTo(dc, 108, 48);
    SelectObject(dc, old_brand_pen);
    DeleteObject(brand_pen);
    DrawVCenterText(dc, RECT{38, 78, 132, 108}, L"历史上限");
    DrawVCenterText(dc, RECT{38, 116, 132, 146}, L"暂停监听");
    DrawVCenterText(dc, RECT{38, 150, 132, 180}, L"开机自启");
    DrawVCenterText(dc, RECT{38, 184, 132, 214}, L"呼出热键");
    DrawVCenterText(dc, RECT{38, 218, 132, 248}, L"连续粘贴");
    DrawVCenterText(dc, RECT{38, 256, 132, 286}, L"\u5b58\u50a8\u4f4d\u7f6e");
    DrawVCenterText(dc, RECT{38, 328, 132, 354}, L"主题颜色");
    DrawVCenterText(dc, RECT{38, 390, 132, 420}, L"窗口缩放");
    const auto hotkey = capturing_hotkey_ && !capturing_continuous_paste_hotkey_
                            ? std::wstring(L"按下新热键")
                            : FormatHotkey(hotkey_modifiers_, hotkey_vk_);
    DrawVCenterText(dc, RECT{166, 184, 274, 214}, hotkey);
    RECT reset_hotkey_rect{280, 184, 326, 214};
    DrawCenteredText(dc, reset_hotkey_rect, L"默认");
    const auto continuous_hotkey = capturing_hotkey_ && capturing_continuous_paste_hotkey_
                                       ? std::wstring(L"按下新热键")
                                       : FormatHotkey(continuous_paste_hotkey_modifiers_,
                                                      continuous_paste_hotkey_vk_);
    DrawVCenterText(dc, RECT{166, 218, 274, 248}, continuous_hotkey);
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
    SetTextColor(dc, muted_color);
    DrawVCenterText(dc, RECT{282, 78, 320, 108}, L"条");
    RECT storage_notice_rect{38, kStorageNoticeTop, kSaveLeft - 12, kStorageNoticeBottom};
    DrawTextW(dc, L"\u4fee\u6539\u5b58\u50a8\u4f4d\u7f6e\u540e\u9700\u8981\u91cd\u542f\u751f\u6548", 13,
              &storage_notice_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (storage_save_failed_) {
        SetTextColor(dc, RgbFromHex(palette.danger));
        DrawVCenterText(dc, RECT{38, 440, kSaveLeft - 12, 458}, L"\u5b58\u50a8\u4f4d\u7f6e\u4fdd\u5b58\u5931\u8d25");
    }
    if (hotkey_conflict_) {
        SetTextColor(dc, RgbFromHex(palette.danger));
        TextOutW(dc, 150, 216, L"快捷键被占用", 6);
    }
    if (continuous_paste_hotkey_conflict_) {
        SetTextColor(dc, RgbFromHex(palette.danger));
        TextOutW(dc, 150, 250, L"快捷键被占用", 6);
    }

    DrawToggle(dc, 236, 120, paused_, hover_target_ == 3 ? hover_progress_ : 0.0f,
               ToggleKnobPosition(3, paused_), palette);
    DrawToggle(dc, 236, 154, startup_, hover_target_ == 4 ? hover_progress_ : 0.0f,
               ToggleKnobPosition(4, startup_), palette);
    DrawToggle(dc, 225, 394, popup_resizable_,
               hover_target_ == kTargetPopupResizable ? hover_progress_ : 0.0f,
               ToggleKnobPosition(kTargetPopupResizable, popup_resizable_), palette);
    SetTextColor(dc, text_color);
    RECT reset_size_rect{274, 390, 326, 420};
    DrawCenteredText(dc, reset_size_rect, L"默认");

    SetTextColor(dc, muted_color);
    DrawVCenterText(dc, RECT{54, 466, 150, 492}, L"\u9879\u76ee\u5730\u5740");
    SetTextColor(dc, text_color);
    DrawVCenterText(dc, RECT{184, 466, kContentRight - 14, 492}, kClipSoulProjectDisplayUrl);
    RECT project_url_rect{kProjectButtonLeft, kProjectButtonTop, kProjectButtonRight, kProjectButtonBottom};
    DrawCenteredText(dc, project_url_rect, L"\u6253\u5f00\u9879\u76ee\u4e3b\u9875");
    SetTextColor(dc, muted_color);
    DrawVCenterText(dc, RECT{54, 550, 150, 570}, L"\u5f53\u524d\u7248\u672c");
    SetTextColor(dc, text_color);
    DrawVCenterText(dc, RECT{184, 550, kContentRight - 14, 570}, kClipSoulVersion);

    SetTextColor(dc, text_color);
    RECT save_rect{kSaveLeft, kSaveTop, kSaveRight, kSaveBottom};
    DrawCenteredText(dc, save_rect, L"保存");

    if (hover_target_ == 1 && hover_progress_ > 0.0f) {
        DrawRoundRect(dc, kCloseBoxLeft, kCloseBoxTop, kCloseBoxLeft + kCloseBoxSize, kCloseBoxTop + kCloseBoxSize,
                      8, Mix(window_fill, RgbFromHex(palette.danger), 0.12f), RgbFromHex(palette.danger));
    }
    HPEN close_pen = CreatePen(PS_SOLID, 2, hover_target_ == 1 ? RgbFromHex(palette.danger) : text_color);
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
        if (wparam == kSettingsToggleTimer) {
            AdvanceToggleMotion();
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lparam) == limit_edit_) {
            const auto palette = ResolvePopupThemePalette(theme_mode_, IsSystemDarkTheme());
            const COLORREF edit_fill = RgbFromHex(palette.search_fill);
            SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
            SetBkColor(reinterpret_cast<HDC>(wparam), edit_fill);
            SetTextColor(reinterpret_cast<HDC>(wparam), RgbFromHex(palette.text));
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
        if (target == kTargetThemeSystem) {
            SetThemeMode(0);
            return 0;
        }
        if (target == kTargetThemeLight) {
            SetThemeMode(1);
            return 0;
        }
        if (target == kTargetThemeDark) {
            SetThemeMode(2);
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
