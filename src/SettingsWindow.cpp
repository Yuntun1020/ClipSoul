#include "ClipSoul/SettingsWindow.h"

#include "ClipSoul/App.h"
#include "ClipSoul/Hotkey.h"
#include "ClipSoul/PopupLayout.h"
#include "ClipSoul/Version.h"
#include "ClipSoul/Win32Util.h"
#include "popup/IdleMemory.h"
#include "popup/InkGlyphs.h"

#include <ShObjIdl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwrite_1.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string_view>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace ClipSoul {
namespace {

constexpr wchar_t kSettingsClass[] = L"ClipSoul.SettingsWindow";
constexpr wchar_t kLimitEditOriginalProc[] = L"ClipSoul.SettingsWindow.LimitEditProc";
constexpr wchar_t kLimitEditHoverProp[] = L"ClipSoul.SettingsWindow.LimitEditHover";

// Ink Ledger 设置页几何（逻辑 px，与预览逐像素对齐）。
constexpr float kSettingsWidth = 340.0f;
constexpr float kSettingsHeight = 560.0f;
constexpr float kHeaderHeight = 44.0f;
constexpr float kFooterHeight = 56.0f;
constexpr float kBodyTop = kHeaderHeight;
constexpr float kBodyBottom = kSettingsHeight - kFooterHeight;
constexpr float kContentLeft = 18.0f;
constexpr float kContentRight = kSettingsWidth - 18.0f;
constexpr float kControlWidth = 156.0f;
constexpr float kControlHeight = 28.0f;
constexpr float kControlLeft = kContentRight - kControlWidth;
constexpr float kToggleWidth = 32.0f;
constexpr float kToggleHeight = 18.0f;
constexpr float kToggleLeft = kContentRight - kToggleWidth;
constexpr float kCloseLeft = kSettingsWidth - 36.0f;
constexpr float kCloseTop = 8.0f;
constexpr float kCloseSize = 28.0f;
constexpr float kCornerRadius = 14.0f;
constexpr float kBoxRadius = 7.0f;
constexpr float kSectionHeight = 16.0f;
constexpr float kLabelLineHeight = 18.0f;
constexpr float kSmallLineHeight = 14.0f;
constexpr float kContentBottom = 696.0f;
constexpr float kFooterButtonWidth = 52.0f;
constexpr float kFooterButtonHeight = 28.0f;

// 行几何：与预览的 .srow / .sec 累加结果一致。
struct SettingsRows {
    float records_label = 48.0f;
    float limit = 66.0f;
    float pause = 116.0f;
    float storage = 166.0f;
    float shortcuts_label = 265.0f;
    float hotkey = 283.0f;
    float continuous = 331.0f;
    float appearance_label = 394.0f;
    float theme = 412.0f;
    float resizable = 460.0f;
    float startup = 510.0f;
    float about_label = 572.0f;
    float project = 590.0f;
    float version = 638.0f;
    float footer_top = kBodyBottom;
};

constexpr float kLimitRowHeight = 50.0f;
constexpr float kPauseRowHeight = 50.0f;
constexpr float kStorageRowHeight = 85.0f;
constexpr float kHotkeyRowHeight = 48.0f;
constexpr float kContinuousRowHeight = 49.0f;
constexpr float kThemeRowHeight = 48.0f;
constexpr float kResizableRowHeight = 50.0f;
constexpr float kStartupRowHeight = 48.0f;
constexpr float kProjectRowHeight = 48.0f;
constexpr float kVersionRowHeight = 48.0f;

constexpr int kLimitEditId = 6101;
constexpr int kTargetClose = 1;
constexpr int kTargetPause = 3;
constexpr int kTargetStartup = 4;
constexpr int kTargetHotkey = 5;
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
constexpr int kTargetStorageReset = 20;
constexpr int kTargetSave = 100;
constexpr int kTargetCancel = 101;

// 子控件（历史上限的 EDIT）无法直接参与父窗口的命中测试，统一请求父窗口按光标位置重算 hover。
constexpr UINT kSettingsRefreshHoverMessage = WM_APP + 21;
constexpr UINT_PTR kSettingsAnimationTimer = 65;
constexpr UINT_PTR kSettingsIdleMemoryTimer = 64;
constexpr UINT_PTR kSettingsSaveCloseTimer = 66;
constexpr int kSettingsSaveCloseDelayMs = 700;

constexpr float kWindowTransformDurationMs = 220.0f;
constexpr float kWindowOpacityDurationMs = 160.0f;
constexpr float kToggleDurationMs = 220.0f;
constexpr float kThemeDurationMs = 220.0f;
constexpr float kHoverDurationMs = 70.0f;
constexpr float kBoxHoverDurationMs = 70.0f;
constexpr float kSizeResetDurationMs = 180.0f;
constexpr int kTooltipDwellMs = 450;
constexpr int kTooltipFadeMs = 120;

constexpr float kRowSeparatorAlphaLight = 0.09f;
constexpr float kRowSeparatorAlphaDark = 0.07f;
constexpr float kRingAlphaLight = 0.16f;
constexpr float kRingAlphaDark = 0.14f;

constexpr float kAnimationTickSeconds = 0.016f;

int DpiScale(HWND hwnd, int value) {
    const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
    return MulDiv(value, static_cast<int>(dpi ? dpi : 96), 96);
}

UiRect Rect(float left, float top, float right, float bottom) {
    return UiRect{left, top, right, bottom};
}

bool Contains(const UiRect& rect, float x, float y) {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

D2D1_COLOR_F ColorF(uint32_t rgb, float alpha = 1.0f) {
    return D2D1::ColorF(rgb, alpha);
}

uint32_t Brighten(uint32_t rgb, float factor) {
    auto channel = [factor](uint32_t value) {
        return static_cast<uint32_t>(std::min(255.0f, static_cast<float>(value) * factor));
    };
    return (channel((rgb >> 16) & 0xFF) << 16) | (channel((rgb >> 8) & 0xFF) << 8) | channel(rgb & 0xFF);
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

// CSS cubic-bezier(x1,y1,x2,y2)：先由 x 反解参数 t，再取 y。
float CubicBezier(float x1, float y1, float x2, float y2, float x) {
    const float target = Clamp01(x);
    if (target <= 0.0f) {
        return 0.0f;
    }
    if (target >= 1.0f) {
        return 1.0f;
    }
    const auto bezier = [](float t, float p1, float p2) {
        const float inv = 1.0f - t;
        return 3.0f * inv * inv * t * p1 + 3.0f * inv * t * t * p2 + t * t * t;
    };
    float low = 0.0f;
    float high = 1.0f;
    float t = target;
    for (int i = 0; i < 24; ++i) {
        const float value = bezier(t, x1, x2);
        if (std::fabs(value - target) < 1e-5f) {
            break;
        }
        if (value < target) {
            low = t;
        } else {
            high = t;
        }
        t = (low + high) * 0.5f;
    }
    return bezier(t, y1, y2);
}

float EasePanel(float progress) {
    return CubicBezier(0.2f, 0.8f, 0.2f, 1.0f, progress);
}

float EaseToggle(float progress) {
    return CubicBezier(0.3f, 1.4f, 0.5f, 1.0f, progress);
}

float EaseSegment(float progress) {
    // 分段控件的滑块不需要那么大的回弹：1.3 在 3 格控件上会冲过头再弹回来，看着像卡顿。
    return CubicBezier(0.32f, 1.12f, 0.5f, 1.0f, progress);
}

float EaseSolo(float progress) {
    return CubicBezier(0.3f, 1.5f, 0.5f, 1.0f, progress);
}

UiRect Inset(const UiRect& rect, float amount) {
    return Rect(rect.left + amount, rect.top + amount, rect.right - amount, rect.bottom - amount);
}

D2D1_ROUNDED_RECT RoundRect(const UiRect& rect, float radius) {
    return D2D1::RoundedRect(D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), radius, radius);
}

UiRect ToggleRect(float row_top, float row_height) {
    const float top = row_top + (row_height - kToggleHeight) * 0.5f - 0.5f;
    return Rect(kToggleLeft, top, kContentRight, top + kToggleHeight);
}

UiRect RowControlRect(float row_top, float row_height, float width = kControlWidth) {
    // 预览里行内容是 33.5px，控件居中后比几何中心高 0.5px。
    const float top = row_top + (row_height - kControlHeight) * 0.5f - 0.5f;
    return Rect(kContentRight - width, top, kContentRight, top + kControlHeight);
}

UiRect LimitBoxRect(const SettingsRows& rows) {
    return RowControlRect(rows.limit, kLimitRowHeight);
}

UiRect StorageBoxRect(const SettingsRows& rows) {
    const float top = rows.storage + 48.5f;
    return Rect(kContentLeft, top, kContentRight, top + kControlHeight);
}

UiRect HotkeyBoxRect(const SettingsRows& rows) {
    return RowControlRect(rows.hotkey, kHotkeyRowHeight);
}

UiRect ContinuousBoxRect(const SettingsRows& rows) {
    return RowControlRect(rows.continuous, kContinuousRowHeight);
}

UiRect ThemeBoxRect(const SettingsRows& rows) {
    return RowControlRect(rows.theme, kThemeRowHeight);
}

UiRect ProjectLinkRect(const SettingsRows& rows) {
    const float top = rows.project + (kProjectRowHeight - kControlHeight) * 0.5f - 0.5f;
    return Rect(kContentRight - 70.0f, top, kContentRight, top + kControlHeight);
}

UiRect SizeResetRect(const SettingsRows& rows) {
    const float center_y = rows.resizable + kResizableRowHeight * 0.5f;
    const float right = kToggleLeft - 8.0f;
    return Rect(right - 24.0f, center_y - 12.0f, right, center_y + 12.0f);
}

// 控件内右侧的图标按钮：24 × 22，右侧留 3px。
UiRect BoxActRect(const UiRect& box, int index_from_right) {
    const float center_y = (box.top + box.bottom) * 0.5f;
    const float right = box.right - 3.0f - static_cast<float>(index_from_right) * 25.0f;
    return Rect(right - 24.0f, center_y - 11.0f, right, center_y + 11.0f);
}

UiRect FooterButtonRect(const SettingsRows& rows, int index) {
    const float top = rows.footer_top + (kFooterHeight - kFooterButtonHeight) * 0.5f;
    const float right = kContentRight - static_cast<float>(1 - index) * (kFooterButtonWidth + 8.0f);
    return Rect(right - kFooterButtonWidth, top, right, top + kFooterButtonHeight);
}

int HitTestTarget(float x, float y, int scroll_offset, bool size_reset_visible) {
    if (Contains(Rect(kCloseLeft, kCloseTop, kCloseLeft + kCloseSize, kCloseTop + kCloseSize), x, y)) {
        return kTargetClose;
    }
    const auto rows = SettingsRows{};
    if (Contains(FooterButtonRect(rows, 1), x, y)) {
        return kTargetSave;
    }
    if (Contains(FooterButtonRect(rows, 0), x, y)) {
        return kTargetCancel;
    }
    if (y < kBodyTop || y >= kBodyBottom) {
        return 0;
    }
    const float body_y = y + static_cast<float>(scroll_offset);

    const auto limit_box = LimitBoxRect(rows);
    if (Contains(limit_box, x, body_y)) {
        return kTargetLimit;
    }
    if (Contains(ToggleRect(rows.pause, kPauseRowHeight), x, body_y)) {
        return kTargetPause;
    }
    if (Contains(ToggleRect(rows.startup, kStartupRowHeight), x, body_y)) {
        return kTargetStartup;
    }
    if (Contains(ToggleRect(rows.resizable, kResizableRowHeight), x, body_y)) {
        return kTargetPopupResizable;
    }
    const auto storage_box = StorageBoxRect(rows);
    if (Contains(storage_box, x, body_y)) {
        // 框内任意位置（含右侧图标）都算悬停这个框；图标本身仍返回各自的点击目标。
        if (Contains(BoxActRect(storage_box, 1), x, body_y)) {
            return kTargetStorageBrowse;
        }
        if (Contains(BoxActRect(storage_box, 0), x, body_y)) {
            return kTargetStorageReset;
        }
        return kTargetStorageBrowse;
    }
    const auto hotkey_box = HotkeyBoxRect(rows);
    if (Contains(hotkey_box, x, body_y)) {
        if (Contains(BoxActRect(hotkey_box, 0), x, body_y)) {
            return kTargetHotkeyReset;
        }
        return kTargetHotkey;
    }
    const auto continuous_box = ContinuousBoxRect(rows);
    if (Contains(continuous_box, x, body_y)) {
        if (Contains(BoxActRect(continuous_box, 0), x, body_y)) {
            return kTargetContinuousPasteHotkeyReset;
        }
        return kTargetContinuousPasteHotkey;
    }
    const auto theme_box = ThemeBoxRect(rows);
    if (Contains(theme_box, x, body_y)) {
        const float slot = theme_box.Width() / 3.0f;
        const int index = std::clamp(static_cast<int>((x - theme_box.left) / slot), 0, 2);
        return index == 0 ? kTargetThemeSystem : (index == 1 ? kTargetThemeLight : kTargetThemeDark);
    }
    if (Contains(ProjectLinkRect(rows), x, body_y)) {
        return kTargetProjectUrl;
    }
    if (size_reset_visible) {
        const auto size_reset = SizeResetRect(rows);
        if (Contains(size_reset, x, body_y)) {
            return kTargetPopupSizeReset;
        }
    }
    return 0;
}

bool IsInteractiveTarget(int target) {
    return target != 0;
}

std::wstring_view TooltipForTarget(int target) {
    switch (target) {
    case kTargetStorageBrowse: return L"浏览…";
    case kTargetStorageReset: return L"恢复默认";
    case kTargetHotkeyReset: return L"恢复默认";
    case kTargetContinuousPasteHotkeyReset: return L"恢复默认";
    case kTargetPopupSizeReset: return L"恢复默认大小";
    default: return L"";
    }
}

// 预览里热键显示为 "Alt + C"（加号两侧带空格），存储仍是紧凑形式。
std::wstring FormatHotkeyDisplay(unsigned modifiers, unsigned vk) {
    const std::wstring compact = FormatHotkey(modifiers, vk);
    std::wstring display;
    display.reserve(compact.size() + 8);
    for (const wchar_t ch : compact) {
        if (ch == L'+') {
            display += L" + ";
        } else {
            display += ch;
        }
    }
    return display;
}

LRESULT CALLBACK LimitEditWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    const auto original = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kLimitEditOriginalProc));
    if (message == WM_MOUSEWHEEL) {
        SendMessageW(GetParent(hwnd), message, wparam, lparam);
        return 0;
    }
    if (message == WM_MOUSEMOVE) {
        // EDIT 是独立子窗口，父窗口收不到它上面的移动/离开。
        // 进入时让 EDIT 自己跟踪 WM_MOUSELEAVE，并请父窗口按光标位置重算一次 hover。
        if (GetPropW(hwnd, kLimitEditHoverProp) == nullptr) {
            SetPropW(hwnd, kLimitEditHoverProp, reinterpret_cast<HANDLE>(1));
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            PostMessageW(GetParent(hwnd), kSettingsRefreshHoverMessage, 0, 0);
        }
        return original ? CallWindowProcW(original, hwnd, message, wparam, lparam)
                        : DefWindowProcW(hwnd, message, wparam, lparam);
    }
    if (message == WM_MOUSELEAVE) {
        RemovePropW(hwnd, kLimitEditHoverProp);
        PostMessageW(GetParent(hwnd), kSettingsRefreshHoverMessage, 0, 0);
        return original ? CallWindowProcW(original, hwnd, message, wparam, lparam)
                        : DefWindowProcW(hwnd, message, wparam, lparam);
    }
    if (message == WM_NCDESTROY) {
        RemovePropW(hwnd, kLimitEditOriginalProc);
        RemovePropW(hwnd, kLimitEditHoverProp);
    }
    return original ? CallWindowProcW(original, hwnd, message, wparam, lparam)
                    : DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace

SettingsWindow::SettingsWindow(HINSTANCE instance, App& app)
    : instance_(instance),
      app_(app) {}

SettingsWindow::~SettingsWindow() {
    if (limit_edit_font_) {
        DeleteObject(limit_edit_font_);
        limit_edit_font_ = nullptr;
    }
    if (limit_edit_brush_) {
        DeleteObject(limit_edit_brush_);
        limit_edit_brush_ = nullptr;
    }
    DiscardDeviceResources();
    if (ink_glyph_cache_) {
        ink_glyph_cache_.reset();
    }
    for (auto** format : {&title_format_, &label_format_, &small_format_, &section_format_, &value_mono_format_,
                         &suffix_format_, &seg_format_, &seg_active_format_, &button_format_, &msg_format_,
                         &link_format_, &ver_format_, &tooltip_format_}) {
        if (*format) {
            (*format)->Release();
            *format = nullptr;
        }
    }
    if (ellipsis_sign_) {
        ellipsis_sign_->Release();
        ellipsis_sign_ = nullptr;
    }
    if (dwrite_factory_) {
        dwrite_factory_->Release();
        dwrite_factory_ = nullptr;
    }
    if (d2d_factory_) {
        d2d_factory_->Release();
        d2d_factory_ = nullptr;
    }
}

bool SettingsWindow::Create(HWND owner) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = SettingsWindow::WindowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kSettingsClass;
    RegisterClassW(&wc);

    const int window_width = DpiScale(owner, static_cast<int>(kSettingsWidth));
    const int window_height = DpiScale(owner, static_cast<int>(kSettingsHeight));
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, kSettingsClass, L"ClipSoul 设置",
                            WS_POPUP | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, window_width, window_height,
                            owner, nullptr, instance_, this);
    if (!hwnd_) {
        return false;
    }
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    const BOOL disable_transitions = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_TRANSITIONS_FORCEDISABLED,
                          &disable_transitions, sizeof(disable_transitions));

    const auto limit_box = LimitBoxRect(SettingsRows{});
    const int edit_x = DpiScale(hwnd_, static_cast<int>(limit_box.left) + 9);
    const int edit_y = DpiScale(hwnd_, static_cast<int>(limit_box.top) + 7);
    const int edit_w = DpiScale(hwnd_, static_cast<int>(limit_box.Width()) - 37);
    const int edit_h = DpiScale(hwnd_, 18);
    limit_edit_ = CreateWindowExW(0, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_LEFT | ES_AUTOHSCROLL,
                                  edit_x, edit_y, edit_w, edit_h, hwnd_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLimitEditId)), instance_, nullptr);
    if (!limit_edit_) {
        return false;
    }
    // 关掉视觉样式：让 EDIT 用 WM_CTLCOLOREDIT 返回的画刷填底，而不是按系统主题自绘。
    SetWindowTheme(limit_edit_, L"", L"");
    const auto original_edit_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        limit_edit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(LimitEditWindowProc)));
    SetPropW(limit_edit_, kLimitEditOriginalProc, reinterpret_cast<HANDLE>(original_edit_proc));
    SendMessageW(limit_edit_, EM_SETLIMITTEXT, 4, 0);
    SendMessageW(limit_edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
    limit_edit_font_ = CreateFontW(-DpiScale(hwnd_, 12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   FIXED_PITCH | FF_MODERN, L"Consolas");
    SendMessageW(limit_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(limit_edit_font_), TRUE);
    SetWindowTheme(limit_edit_, L"", L"");

    ApplyBackdrop();
    LoadToControls();
    UpdateLimitEditPosition();
    return true;
}

void SettingsWindow::Show() {
    LoadToControls();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const SIZE size{DpiScale(hwnd_, static_cast<int>(kSettingsWidth)),
                    DpiScale(hwnd_, static_cast<int>(kSettingsHeight))};
    const auto position = CenterWindowInWorkArea(size, work);
    scroll_offset_ = 0;
    window_transition_rect_ = RECT{position.x, position.y, position.x + size.cx, position.y + size.cy};
    SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
    UpdateLimitEditPosition();
    SetWindowPos(hwnd_, HWND_TOPMOST, position.x, position.y, size.cx, size.cy,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    StartWindowTransition(true);
}

void SettingsWindow::UpdateLimitEditPosition() {
    if (!limit_edit_ || !hwnd_) return;
    const auto box = LimitBoxRect(SettingsRows{});
    const int left = DpiScale(hwnd_, static_cast<int>(box.left) + 9);
    const int top = DpiScale(hwnd_, static_cast<int>(box.top) + 7) - DpiScale(hwnd_, scroll_offset_);
    const int width = DpiScale(hwnd_, static_cast<int>(box.Width()) - 37);
    const int height = DpiScale(hwnd_, 18);
    const int body_top = DpiScale(hwnd_, static_cast<int>(kBodyTop) + 1);
    const int body_bottom = DpiScale(hwnd_, static_cast<int>(kBodyBottom) - 1);
    const int visible_top = std::max(top, body_top);
    const int visible_bottom = std::min(top + height, body_bottom);
    const bool visible = window_transition_progress_ >= 1.0f || !window_transition_active_;
    if (visible_bottom > visible_top && visible) {
        SetWindowPos(limit_edit_, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        HRGN clip = CreateRectRgn(0, visible_top - top, width, visible_bottom - top);
        SetWindowRgn(limit_edit_, clip, TRUE);
    } else {
        SetWindowPos(limit_edit_, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
    }
}

void SettingsWindow::RefreshTheme() {
    RefreshTheme(false);
}

void SettingsWindow::RefreshTheme(bool live_preview) {
    if (!live_preview) {
        // 非客户区的 DWM 属性要同步往返一次 DWM；主题过渡的每一帧都做会明显卡顿，
        // 而且过渡期间整窗都被客户区盖住，等动画结束再刷一次即可。
        ApplyBackdrop();
        if (limit_edit_brush_) {
            DeleteObject(limit_edit_brush_);
            limit_edit_brush_ = nullptr;
            limit_edit_brush_color_ = CLR_INVALID;
        }
        if (limit_edit_) {
            SendMessageW(limit_edit_, WM_SETREDRAW, FALSE, 0);
            SetWindowTheme(limit_edit_, L"", L"");
            SendMessageW(limit_edit_, WM_SETREDRAW, TRUE, 0);
        }
    }
    if (!hwnd_) {
        return;
    }
    // 主题切换的每一帧都走这里：只失效、不强制同步重绘。原来每帧 RDW_UPDATENOW 会把整窗
    // （含子控件）同步画两遍，动画自然发抖。动画结束后由 EndThemeMotion 再做一次完整刷新。
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (limit_edit_) {
        // 原生 EDIT 是独立子窗口：父窗口重画不会带上它，必须自己失效一次，
        // 否则它会一直留着上一个主题的底色 —— 浅色主题下就是那块深色方块。
        // 用异步 InvalidateRect（不是 RDW_UPDATENOW），开销只有一个 119x18 的小控件。
        InvalidateRect(limit_edit_, nullptr, TRUE);
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
    save_message_ = L"修改后点击保存";
    save_message_ok_ = false;
    paused_ = settings.paused;
    startup_ = settings.start_with_windows;
    hotkey_modifiers_ = settings.hotkey_modifiers;
    hotkey_vk_ = settings.hotkey_vk;
    continuous_paste_hotkey_modifiers_ = settings.continuous_paste_hotkey_modifiers;
    continuous_paste_hotkey_vk_ = settings.continuous_paste_hotkey_vk;
    theme_mode_ = std::clamp(settings.theme_mode, 0, 2);
    theme_motion_from_ = theme_mode_;
    theme_motion_progress_ = 1.0f;
    popup_resizable_ = settings.popup_resizable;
    capturing_hotkey_ = false;
    capturing_continuous_paste_hotkey_ = false;
    hotkey_conflict_ = false;
    continuous_paste_hotkey_conflict_ = false;
    toggle_motion_target_ = 0;
    toggle_motion_progress_ = 1.0f;
    size_reset_target_visible_ = popup_resizable_;
    size_reset_progress_ = popup_resizable_ ? 1.0f : 0.0f;
    hover_target_ = 0;
    hover_fade_target_ = 0;
    pressed_target_ = 0;
    tooltip_target_ = 0;
    tooltip_progress_ = 0.0f;
    tooltip_dwell_ms_ = 0;
    if (hwnd_) {
        KillTimer(hwnd_, kSettingsSaveCloseTimer);
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
    StartToggleMotion(kTargetPause, from, paused_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::ToggleStartup() {
    const bool from = startup_;
    startup_ = !startup_;
    StartToggleMotion(kTargetStartup, from, startup_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::TogglePopupResizable() {
    const bool from = popup_resizable_;
    popup_resizable_ = !popup_resizable_;
    StartToggleMotion(kTargetPopupResizable, from, popup_resizable_);
    StartSizeResetMotion(popup_resizable_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::StartToggleMotion(int target, bool from_active, bool to_active) {
    toggle_motion_target_ = target;
    toggle_motion_from_active_ = from_active;
    toggle_motion_to_active_ = to_active;
    toggle_motion_progress_ = 0.0f;
    StartAnimationTimer();
}

float SettingsWindow::ToggleKnobPosition(int target, bool active) const {
    if (toggle_motion_target_ == target && toggle_motion_progress_ < 1.0f) {
        const float eased = EaseToggle(toggle_motion_progress_);
        const float from = toggle_motion_from_active_ ? 1.0f : 0.0f;
        const float to = toggle_motion_to_active_ ? 1.0f : 0.0f;
        return from + (to - from) * eased;
    }
    return active ? 1.0f : 0.0f;
}

void SettingsWindow::AdvanceToggleMotion(float elapsed_ms) {
    toggle_motion_progress_ = std::min(1.0f, toggle_motion_progress_ + elapsed_ms / kToggleDurationMs);
    if (toggle_motion_progress_ >= 1.0f) {
        toggle_motion_progress_ = 1.0f;
        toggle_motion_target_ = 0;
    }
}

void SettingsWindow::StartSizeResetMotion(bool visible) {
    size_reset_target_visible_ = visible;
    StartAnimationTimer();
}

void SettingsWindow::AdvanceSizeResetMotion(float elapsed_ms) {
    const float step = elapsed_ms / kSizeResetDurationMs;
    const float target = size_reset_target_visible_ ? 1.0f : 0.0f;
    if (size_reset_progress_ < target) {
        size_reset_progress_ = std::min(target, size_reset_progress_ + step);
    } else if (size_reset_progress_ > target) {
        size_reset_progress_ = std::max(target, size_reset_progress_ - step);
    }
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

PopupThemePalette SettingsWindow::CurrentPalette() const {
    const bool system_dark = IsSystemDarkTheme();
    const auto target = ResolvePopupThemePalette(theme_mode_, system_dark);
    if (theme_motion_progress_ >= 1.0f || theme_motion_from_ == theme_mode_) {
        return target;
    }
    const auto origin = ResolvePopupThemePalette(theme_motion_from_, system_dark);
    // 颜色过渡用不越界的缓动（EaseSegment 有回弹，用在颜色上会闪）。
    return LerpPopupThemePalette(origin, target, EasePanel(theme_motion_progress_));
}

void SettingsWindow::SetThemeMode(int mode) {
    const int next_mode = std::clamp(mode, 0, 2);
    if (!SettingsThemePreviewShouldRefreshNativeControls(theme_mode_, next_mode)) {
        return;
    }
    theme_motion_from_ = theme_mode_;
    theme_mode_ = next_mode;
    theme_motion_progress_ = 0.0f;
    // 原生 EDIT 是独立子窗口，父窗口的 D2D 重绘不会带上它：过渡期间它会一直停在
    // 第 0 帧的底色上，直到动画结束才统一 —— 看起来就是框里闪一块纯色方块。
    // 过渡期间把它藏起来（Paint 里会改用 D2D 画同一段文字），动画结束再放回来。
    if (limit_edit_ && GetFocus() != limit_edit_) {
        ShowWindow(limit_edit_, SW_HIDE);
    }
    StartAnimationTimer();
    // 预览帧：只失效重绘，刷新 DWM 边框/原生 EDIT 主题留到动画结束（见 AdvanceAnimations）。
    RefreshTheme(true);
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
    StartAnimationTimer();
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

void SettingsWindow::SetHoverTarget(int target) {
    // 只在目标真正变化时重启 hover 动画；同一目标反复设置不会让描边闪。
    if (target == hover_target_) {
        return;
    }
    hover_fade_target_ = hover_target_;
    hover_target_ = target;
    hover_progress_ = 0.0f;
    if (target != 0) {
        StartAnimationTimer();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::RefreshHoverFromCursor() {
    // 所有控件（含原生 EDIT）共用同一条判定：光标在哪，就高亮哪；离开就取消。
    if (!hwnd_) {
        return;
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(hwnd_, &cursor);
    // GetCursorPos/ScreenToClient 给的是物理像素，命中测试用的是逻辑 px。
    const int scale = DpiScale(hwnd_, 96);
    const int logical_x = MulDiv(cursor.x, 96, scale);
    const int logical_y = MulDiv(cursor.y, 96, scale);
    SetHoverTarget(HitTestTarget(static_cast<float>(logical_x), static_cast<float>(logical_y), scroll_offset_,
                                 size_reset_target_visible_));
}

bool SettingsWindow::PointerInsideRect(const UiRect& rect, int scroll_offset) const {
    if (!hwnd_) {
        return false;
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(hwnd_, &cursor);
    // 边框判定直接在物理像素上做半开区间 [left, right)，避免 DPI 换算取整带来 1px 的错判。
    const int scale = DpiScale(hwnd_, 96);
    const int scroll_px = DpiScale(hwnd_, scroll_offset);
    const int left = DpiScale(hwnd_, static_cast<int>(std::lround(rect.left)));
    const int top = DpiScale(hwnd_, static_cast<int>(std::lround(rect.top))) - scroll_px;
    const int right = DpiScale(hwnd_, static_cast<int>(std::lround(rect.right)));
    const int bottom = DpiScale(hwnd_, static_cast<int>(std::lround(rect.bottom))) - scroll_px;
    return cursor.x >= left && cursor.x < right && cursor.y >= top && cursor.y < bottom;
}

bool SettingsWindow::BoxHoverTarget(int index) const {
    const auto rows = SettingsRows{};
    switch (index) {
    case kBoxHoverLimit: return PointerInsideRect(LimitBoxRect(rows), scroll_offset_);
    case kBoxHoverStorage: return PointerInsideRect(StorageBoxRect(rows), scroll_offset_);
    case kBoxHoverHotkey: return PointerInsideRect(HotkeyBoxRect(rows), scroll_offset_);
    case kBoxHoverContinuous: return PointerInsideRect(ContinuousBoxRect(rows), scroll_offset_);
    default: return false;
    }
}

void SettingsWindow::AdvanceBoxHover(float elapsed_ms) {
    // 外框高亮：70ms 干净利落的淡入淡出（比预览的 120ms 更快）。
    const float step = elapsed_ms / kBoxHoverDurationMs;
    for (int index = 0; index < kBoxHoverCount; ++index) {
        const float target = BoxHoverTarget(index) ? 1.0f : 0.0f;
        float& progress = box_hover_[index];
        if (progress < target) {
            progress = std::min(target, progress + step);
        } else if (progress > target) {
            progress = std::max(target, progress - step);
        }
    }
}

void SettingsWindow::SetSaveMessage(std::wstring message, bool ok) {
    save_message_ = std::move(message);
    save_message_ok_ = ok;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::BeginHide() {
    if (!IsWindowVisible(hwnd_)) {
        return;
    }
    KillTimer(hwnd_, kSettingsSaveCloseTimer);
    StartWindowTransition(false);
}

void SettingsWindow::StartWindowTransition(bool opening) {
    if (!hwnd_) {
        return;
    }
    window_transition_active_ = true;
    window_transition_opening_ = opening;
    window_transition_progress_ = 0.0f;
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwnd_, 0, opening ? 0 : 255, LWA_ALPHA);
    if (!opening && !(window_transition_active_ && window_transition_opening_)) {
        RECT current{};
        GetWindowRect(hwnd_, &current);
        window_transition_rect_ = current;
    }
    UpdateLimitEditPosition();
    ApplyWindowTransitionFrame();
    StartAnimationTimer();
}

void SettingsWindow::ApplyWindowTransitionFrame() {
    if (!window_transition_active_) {
        return;
    }
    const float progress = Clamp01(window_transition_progress_);
    const float eased = EasePanel(progress);
    const float opacity_progress = std::min(1.0f, progress * kWindowTransformDurationMs / kWindowOpacityDurationMs);
    const float alpha = window_transition_opening_ ? opacity_progress : 1.0f - opacity_progress;
    const float scale = window_transition_opening_ ? 0.985f + 0.015f * eased : 1.0f - 0.015f * eased;
    const float y_offset = window_transition_opening_ ? 10.0f * (1.0f - eased) : 10.0f * eased;

    RECT final_rect = window_transition_rect_;
    const int final_w = final_rect.right - final_rect.left;
    const int final_h = final_rect.bottom - final_rect.top;
    if (final_w <= 0 || final_h <= 0) {
        return;
    }
    const int scaled_w = std::max(1, static_cast<int>(std::lround(static_cast<float>(final_w) * scale)));
    const int scaled_h = std::max(1, static_cast<int>(std::lround(static_cast<float>(final_h) * scale)));
    const int center_x = (final_rect.left + final_rect.right) / 2;
    const int center_y = (final_rect.top + final_rect.bottom) / 2;
    const int offset_y = DpiScale(hwnd_, static_cast<int>(std::lround(y_offset)));
    SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(std::lround(Clamp01(alpha) * 255.0f)), LWA_ALPHA);
    SetWindowPos(hwnd_, HWND_TOPMOST, center_x - scaled_w / 2, center_y - scaled_h / 2 + offset_y,
                 scaled_w, scaled_h, SWP_NOACTIVATE);
}

void SettingsWindow::AdvanceWindowTransition() {
    window_transition_progress_ = std::min(
        1.0f, window_transition_progress_ + kAnimationTickSeconds * 1000.0f / kWindowTransformDurationMs);
    ApplyWindowTransitionFrame();
    if (window_transition_progress_ >= 1.0f) {
        EndWindowTransition();
    }
}

void SettingsWindow::EndWindowTransition() {
    const bool was_opening = window_transition_opening_;
    window_transition_active_ = false;
    window_transition_progress_ = 1.0f;
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    if (was_opening) {
        SetWindowPos(hwnd_, HWND_TOPMOST, window_transition_rect_.left, window_transition_rect_.top,
                     window_transition_rect_.right - window_transition_rect_.left,
                     window_transition_rect_.bottom - window_transition_rect_.top, SWP_NOACTIVATE);
        UpdateLimitEditPosition();
        InvalidateRect(hwnd_, nullptr, FALSE);
    } else {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void SettingsWindow::StartAnimationTimer() {
    if (!hwnd_) {
        return;
    }
    if (animation_last_tick_ == 0) {
        animation_last_tick_ = GetTickCount64();
    }
    SetTimer(hwnd_, kSettingsAnimationTimer, 16, nullptr);
}

bool SettingsWindow::AnimationActive() const {
    for (int index = 0; index < kBoxHoverCount; ++index) {
        if (box_hover_[index] > 0.0f) {
            return true;
        }
    }
    const float size_reset_target = size_reset_target_visible_ ? 1.0f : 0.0f;
    const bool tooltip_fading = tooltip_progress_ > 0.0f && tooltip_progress_ < 1.0f;
    const bool tooltip_pending = !TooltipForTarget(hover_target_).empty() && tooltip_progress_ < 1.0f;
    return window_transition_active_ || toggle_motion_progress_ < 1.0f || theme_motion_progress_ < 1.0f ||
           std::fabs(size_reset_progress_ - size_reset_target) > 0.001f || hover_progress_ < 1.0f ||
           tooltip_fading || tooltip_pending || capturing_hotkey_;
}

void SettingsWindow::AdvanceAnimations() {
    if (!IsWindowVisible(hwnd_)) {
        KillTimer(hwnd_, kSettingsAnimationTimer);
        return;
    }
    // 按真实帧间隔推进，避免 SetTimer 抖动让动画忽快忽慢。
    const ULONGLONG now = GetTickCount64();
    const float elapsed_ms = animation_last_tick_ == 0
                                 ? kAnimationTickSeconds * 1000.0f
                                 : static_cast<float>(std::min<ULONGLONG>(64, now - animation_last_tick_));
    animation_last_tick_ = now;
    const bool was_window_transition = window_transition_active_;
    if (window_transition_active_) {
        AdvanceWindowTransition();
    }
    if (toggle_motion_progress_ < 1.0f) {
        AdvanceToggleMotion(elapsed_ms);
    }
    if (theme_motion_progress_ < 1.0f) {
        theme_motion_progress_ = std::min(1.0f, theme_motion_progress_ + elapsed_ms / kThemeDurationMs);
        if (theme_motion_progress_ >= 1.0f) {
            // 动画收尾：此时才把 DWM 边框色和原生 EDIT 的主题一次性刷到位。
            RefreshTheme(false);
            // 过渡期间被藏起来的原生 EDIT 在这里放回来（配色已经落到终态，不会再有色差）。
            UpdateLimitEditPosition();
        }
    }
    AdvanceSizeResetMotion(elapsed_ms);
    AdvanceBoxHover(elapsed_ms);

    // 悬停淡入 / 淡出（70ms）。
    if (hover_progress_ < 1.0f) {
        hover_progress_ = std::min(1.0f, hover_progress_ + elapsed_ms / kHoverDurationMs);
    }

    // 图标提示：悬停 450ms 后 120ms 淡入。
    const std::wstring_view tip = TooltipForTarget(hover_target_);
    if (!tip.empty()) {
        if (tooltip_target_ != hover_target_) {
            tooltip_target_ = hover_target_;
            tooltip_dwell_ms_ = 0;
            tooltip_progress_ = 0.0f;
        }
        tooltip_dwell_ms_ += static_cast<int>(std::lround(elapsed_ms));
        if (tooltip_dwell_ms_ >= kTooltipDwellMs) {
            tooltip_progress_ = std::min(1.0f, tooltip_progress_ + elapsed_ms / static_cast<float>(kTooltipFadeMs));
        }
    } else if (tooltip_progress_ > 0.0f) {
        tooltip_progress_ = std::max(0.0f, tooltip_progress_ - elapsed_ms / static_cast<float>(kTooltipFadeMs));
        if (tooltip_progress_ <= 0.0f) {
            tooltip_target_ = 0;
            tooltip_dwell_ms_ = 0;
        }
    }

    if (was_window_transition && !window_transition_active_) {
        UpdateLimitEditPosition();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (limit_edit_ && theme_motion_progress_ < 1.0f && IsWindowVisible(limit_edit_)) {
        // EDIT 正持有焦点、没被藏起来的兜底路径：让它陪着父窗口一起逐帧重画，
        // 否则它会停在第 0 帧的底色上，和正在插值的圆角框形成色差。
        InvalidateRect(limit_edit_, nullptr, TRUE);
    }

    if (!AnimationActive()) {
        KillTimer(hwnd_, kSettingsAnimationTimer);
        animation_last_tick_ = 0;
    }
}

void SettingsWindow::EnsureDeviceResources() {
    if (!d2d_factory_) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_);
    }
    if (!ink_glyph_cache_) {
        ink_glyph_cache_ = std::make_unique<popup_detail::InkGlyphCache>();
    }
    if (!dwrite_factory_) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(&dwrite_factory_));
        auto make_format = [&](const wchar_t* family, DWRITE_FONT_WEIGHT weight, float size,
                               IDWriteTextFormat** format) {
            dwrite_factory_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", format);
            if (*format) {
                (*format)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                (*format)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        };
        make_format(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 15.0f, &title_format_);
        make_format(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12.5f, &label_format_);
        make_format(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 10.5f, &small_format_);
        make_format(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 11.0f, &section_format_);
        make_format(L"Consolas", DWRITE_FONT_WEIGHT_NORMAL, 11.5f, &value_mono_format_);
        make_format(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 11.5f, &suffix_format_);
        make_format(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 11.5f, &seg_format_);
        make_format(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 11.5f, &seg_active_format_);
        make_format(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12.0f, &button_format_);
        make_format(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 11.0f, &msg_format_);
        make_format(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 11.5f, &link_format_);
        make_format(L"Consolas", DWRITE_FONT_WEIGHT_NORMAL, 11.5f, &ver_format_);
        make_format(L"Microsoft YaHei UI", DWRITE_FONT_WEIGHT_NORMAL, 11.0f, &tooltip_format_);
        if (button_format_) {
            DWRITE_TRIMMING trimming{};
            trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
            dwrite_factory_->CreateEllipsisTrimmingSign(button_format_, &ellipsis_sign_);
        }
        for (auto* format : {title_format_, label_format_, small_format_, section_format_, value_mono_format_,
                             suffix_format_, seg_format_, seg_active_format_, button_format_, msg_format_,
                             link_format_, ver_format_, tooltip_format_}) {
            if (format && ellipsis_sign_) {
                DWRITE_TRIMMING trimming{};
                trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
                format->SetTrimming(&trimming, ellipsis_sign_);
            }
        }
    }
    if (!render_target_) {
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        d2d_factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                         D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)),
            &render_target_);
        if (render_target_) {
            const float dpi = static_cast<float>(GetDpiForWindow(hwnd_));
            render_target_->SetDpi(dpi, dpi);
            render_target_->CreateSolidColorBrush(D2D1::ColorF(0, 1.0f), &brush_);
        }
    }
}

void SettingsWindow::DiscardDeviceResources() {
    if (render_target_) {
        render_target_->Release();
        render_target_ = nullptr;
    }
    if (brush_) {
        brush_->Release();
        brush_ = nullptr;
    }
    if (ink_glyph_cache_) {
        ink_glyph_cache_->Clear();
    }
    ClearTextLayoutCache();
}

void SettingsWindow::ClearTextLayoutCache() {
    for (auto& entry : text_layout_cache_) {
        if (entry.layout) {
            entry.layout->Release();
        }
    }
    text_layout_cache_.clear();
}

void SettingsWindow::Paint() {
    if (!IsWindowVisible(hwnd_)) {
        return;
    }
    EnsureDeviceResources();
    if (!render_target_ || !brush_) {
        return;
    }
    // 窗口在入场动画里会被缩放，渲染目标必须跟随客户端尺寸，否则内容会被拉伸。
    {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const D2D1_SIZE_U pixel_size = D2D1::SizeU(static_cast<UINT32>(std::max(0L, client.right - client.left)),
                                                   static_cast<UINT32>(std::max(0L, client.bottom - client.top)));
        const D2D1_SIZE_U current = render_target_->GetPixelSize();
        if (pixel_size.width > 0 && pixel_size.height > 0 &&
            (current.width != pixel_size.width || current.height != pixel_size.height)) {
            render_target_->Resize(pixel_size);
        }
    }
    // 主题切换时把「旧主题 → 新主题」的配色做插值：之前只有滑块在动、整块面板的颜色是瞬间跳变的，
    // 看起来就像闪了一下。插值之后背景、文字、分割线跟着滑块一起过渡。
    const auto palette = CurrentPalette();
    const auto rows = SettingsRows{};
    const uint32_t paper = palette.window_tint;
    const uint32_t raise = palette.panel_fill;
    const uint32_t deep = palette.card_fill;
    const uint32_t slip = palette.dark ? palette.paper_selected : palette.panel_fill;
    const uint32_t ink = palette.text;
    const uint32_t ink2 = palette.muted;
    const uint32_t ink3 = palette.text_tertiary;
    const uint32_t seal = palette.danger;
    const uint32_t ok = palette.dark ? 0x7FAE7A : 0x5F8C5B;
    const float separator_alpha = palette.dark ? kRowSeparatorAlphaDark : kRowSeparatorAlphaLight;
    const float ring_alpha = palette.dark ? kRingAlphaDark : kRingAlphaLight;

    render_target_->BeginDraw();
    render_target_->SetTransform(D2D1::Matrix3x2F::Identity());
    render_target_->Clear(ColorF(paper));

    auto fill_round = [&](const UiRect& rect, float radius, uint32_t color, float alpha) {
        brush_->SetColor(ColorF(color, alpha));
        render_target_->FillRoundedRectangle(RoundRect(rect, radius), brush_);
    };
    auto stroke_round = [&](const UiRect& rect, float radius, uint32_t color, float alpha, float width) {
        brush_->SetColor(ColorF(color, alpha));
        render_target_->DrawRoundedRectangle(RoundRect(rect, radius), brush_, width);
    };
    auto draw_text = [&](std::wstring_view text, IDWriteTextFormat* format, const UiRect& rect, uint32_t color,
                         float alpha, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
                         float letter_spacing = 0.0f) {
        if (!dwrite_factory_ || !format || text.empty() || rect.Width() <= 0.0f) {
            return;
        }
        // 同样的文案/字号/矩形每帧都重复出现；主题过渡时整窗每帧要重绘 40+ 次排版，
        // 缓存 layout 之后过渡期间只是换颜色重画，不再反复 reshape。
        CachedTextLayout* cached = nullptr;
        for (auto& entry : text_layout_cache_) {
            if (entry.format == format && entry.width == rect.Width() && entry.height == rect.Height() &&
                entry.align == align && entry.letter_spacing == letter_spacing && entry.text == text) {
                cached = &entry;
                break;
            }
        }
        if (!cached) {
            IDWriteTextLayout* layout = nullptr;
            if (FAILED(dwrite_factory_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format,
                                                         rect.Width(), rect.Height(), &layout)) ||
                !layout) {
                return;
            }
            layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            layout->SetTextAlignment(align);
            if (letter_spacing != 0.0f) {
                IDWriteTextLayout1* layout1 = nullptr;
                if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1),
                                                     reinterpret_cast<void**>(&layout1))) &&
                    layout1) {
                    layout1->SetCharacterSpacing(0.0f, letter_spacing, 0.0f,
                                                 DWRITE_TEXT_RANGE{0, static_cast<UINT32>(text.size())});
                    layout1->Release();
                }
            }
            if (text_layout_cache_.size() >= 256) {
                ClearTextLayoutCache();
            }
            text_layout_cache_.push_back(CachedTextLayout{
                std::wstring(text), format, rect.Width(), rect.Height(), align, letter_spacing, layout});
            cached = &text_layout_cache_.back();
        }
        brush_->SetColor(ColorF(color, alpha));
        render_target_->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), cached->layout, brush_);
    };
    auto text_width = [&](std::wstring_view text, IDWriteTextFormat* format) {
        if (!dwrite_factory_ || !format || text.empty()) {
            return 0.0f;
        }
        IDWriteTextLayout* layout = nullptr;
        if (FAILED(dwrite_factory_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format,
                                                     1000.0f, 100.0f, &layout)) ||
            !layout) {
            return 0.0f;
        }
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        layout->Release();
        return metrics.width;
    };
    auto draw_glyph = [&](popup_detail::InkGlyph glyph, const UiRect& rect, uint32_t color, float alpha) {
        brush_->SetColor(ColorF(color, alpha));
        popup_detail::DrawInkGlyph(render_target_, d2d_factory_, brush_, glyph, rect, ink_glyph_cache_.get());
    };
    auto hairline = [&](float left, float top, float right, float alpha = -1.0f) {
        brush_->SetColor(ColorF(ink, alpha < 0.0f ? separator_alpha : alpha));
        render_target_->DrawLine(D2D1::Point2F(left, top + 0.5f), D2D1::Point2F(right, top + 0.5f), brush_, 1.0f);
    };
    auto hover_alpha = [&](int target) {
        if (target == hover_target_) {
            return Clamp01(hover_progress_);
        }
        if (target == hover_fade_target_ && hover_progress_ < 1.0f) {
            return 1.0f - Clamp01(hover_progress_);
        }
        return 0.0f;
    };
    auto press_scale = [&](int target) { return pressed_target_ == target ? 0.97f : 1.0f; };
    auto scale_rect = [](const UiRect& rect, float scale) {
        const float cx = (rect.left + rect.right) * 0.5f;
        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float half_w = rect.Width() * 0.5f * scale;
        const float half_h = rect.Height() * 0.5f * scale;
        return Rect(cx - half_w, cy - half_h, cx + half_w, cy + half_h);
    };
    auto draw_box = [&](const UiRect& rect, int box_index, bool focused, bool recording, bool bad) {
        fill_round(rect, kBoxRadius, raise, 1.0f);
        const float hover = box_hover_[box_index];
        if (hover > 0.0f) {
            stroke_round(Inset(rect, 0.5f), kBoxRadius - 0.5f, ink3, hover, 1.0f);
        }
        if (recording || bad) {
            stroke_round(Inset(rect, 0.5f), kBoxRadius - 0.5f, seal, 1.0f, 1.2f);
        } else if (focused) {
            stroke_round(Inset(rect, 0.5f), kBoxRadius - 0.5f, ink, 1.0f, 1.2f);
        } else {
            stroke_round(Inset(rect, 0.5f), kBoxRadius - 0.5f, ink, ring_alpha * (1.0f - hover), 1.0f);
        }
    };
    auto draw_act = [&](const UiRect& rect, popup_detail::InkGlyph glyph, int target) {
        const float hover = hover_alpha(target);
        if (hover > 0.0f) {
            fill_round(rect, 5.0f, deep, hover);
        }
        const uint32_t color = hover > 0.0f ? ink : ink3;
        const UiRect icon = Rect((rect.left + rect.right) * 0.5f - 6.0f, (rect.top + rect.bottom) * 0.5f - 6.0f,
                                 (rect.left + rect.right) * 0.5f + 6.0f, (rect.top + rect.bottom) * 0.5f + 6.0f);
        draw_glyph(glyph, icon, color, 1.0f);
    };
    auto draw_toggle = [&](float row_top, float row_height, bool active, int target) {
        const auto rect = ToggleRect(row_top, row_height);
        const float knob = ToggleKnobPosition(target, active);
        // 轨道：关闭 = 16% 墨，开启 = 实墨；背景过渡 160ms。
        float bg_progress = 0.0f;
        if (toggle_motion_target_ == target && toggle_motion_progress_ < 1.0f) {
            const float raw = std::min(1.0f, toggle_motion_progress_ * kToggleDurationMs / 160.0f);
            bg_progress = toggle_motion_to_active_ ? raw : 1.0f - raw;
        } else {
            bg_progress = active ? 1.0f : 0.0f;
        }
        fill_round(rect, kToggleHeight * 0.5f, ink, ring_alpha);
        if (bg_progress > 0.0f) {
            fill_round(rect, kToggleHeight * 0.5f, ink, bg_progress);
        }
        const float knob_left = rect.left + 2.0f + 14.0f * knob;
        const float knob_top = rect.top + 2.0f;
        brush_->SetColor(D2D1::ColorF(0x000000, 0.16f));
        render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knob_left + 7.0f, knob_top + 8.2f), 7.0f, 7.0f), brush_);
        brush_->SetColor(ColorF(raise, 1.0f));
        render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knob_left + 7.0f, knob_top + 7.0f), 7.0f, 7.0f), brush_);
    };
    auto draw_row_label = [&](float row_top, float row_height, const wchar_t* label, const std::wstring& description,
                              bool error, float label_right) {
        const float label_top = description.empty() ? row_top + (row_height - kLabelLineHeight) * 0.5f
                                                    : row_top + 8.0f;
        draw_text(label, label_format_, Rect(kContentLeft, label_top, label_right, label_top + kLabelLineHeight),
                  ink, 1.0f);
        if (!description.empty()) {
            draw_text(description, small_format_,
                      Rect(kContentLeft, label_top + kLabelLineHeight + 1.0f, label_right,
                           label_top + kLabelLineHeight + 1.0f + kSmallLineHeight),
                      error ? seal : ink3, 1.0f);
        }
    };

    // ---------- 窗口 ----------
    fill_round(Rect(0.0f, 0.0f, kSettingsWidth, kSettingsHeight), kCornerRadius, paper, 1.0f);

    // ---------- 页眉 ----------
    draw_text(L"设置", title_format_, Rect(kContentLeft, 0.0f, kContentRight, kHeaderHeight), ink, 1.0f);
    {
        const auto close_rect = Rect(kCloseLeft, kCloseTop, kCloseLeft + kCloseSize, kCloseTop + kCloseSize);
        const float hover = hover_alpha(kTargetClose);
        if (hover > 0.0f) {
            fill_round(scale_rect(close_rect, press_scale(kTargetClose)), 7.0f, seal, 0.10f * hover);
        }
        const UiRect icon = Rect(close_rect.left + 6.5f, close_rect.top + 6.5f, close_rect.left + 21.5f,
                                 close_rect.top + 21.5f);
        draw_glyph(popup_detail::InkGlyph::Close, icon, hover > 0.0f ? seal : ink3, 1.0f);
    }

    // ---------- 主体 ----------
    render_target_->PushAxisAlignedClip(D2D1::RectF(0.0f, kBodyTop, kSettingsWidth, kBodyBottom),
                                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    render_target_->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -static_cast<float>(scroll_offset_)));

    // 记录
    draw_text(L"记录", section_format_,
              Rect(kContentLeft, rows.records_label, kContentRight, rows.records_label + kSectionHeight), ink3, 1.0f,
              DWRITE_TEXT_ALIGNMENT_LEADING, 1.0f);
    draw_row_label(rows.limit, kLimitRowHeight, L"历史上限", L"超出后移除最旧的记录", false, kControlLeft - 12.0f);
    draw_box(LimitBoxRect(rows), kBoxHoverLimit, limit_edit_ && GetFocus() == limit_edit_, false, false);
    {
        const auto box = LimitBoxRect(rows);
        if (window_transition_active_ || !limit_edit_ || !IsWindowVisible(limit_edit_)) {
            draw_text(limit_text_, value_mono_format_,
                      Rect(box.left + 10.0f, box.top, box.right - 28.0f, box.bottom), ink, 1.0f);
        }
        draw_text(L"条", suffix_format_, Rect(box.right - 26.0f, box.top, box.right - 10.0f, box.bottom), ink3, 1.0f,
                  DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    hairline(kContentLeft, rows.limit + kLimitRowHeight - 1.0f, kContentRight);

    draw_row_label(rows.pause, kPauseRowHeight, L"暂停监听", L"暂停期间复制的内容不会被记录", false, kToggleLeft - 12.0f);
    draw_toggle(rows.pause, kPauseRowHeight, paused_, kTargetPause);
    hairline(kContentLeft, rows.pause + kPauseRowHeight - 1.0f, kContentRight);

    draw_row_label(rows.storage, kStorageRowHeight, L"存储位置", L"修改后需要重启生效", false, kContentRight);
    {
        const auto box = StorageBoxRect(rows);
        draw_box(box, kBoxHoverStorage, false, false, false);
        draw_text(storage_path_, value_mono_format_,
                  Rect(box.left + 10.0f, box.top, BoxActRect(box, 1).left - 4.0f, box.bottom), ink, 1.0f);
        draw_act(BoxActRect(box, 1), popup_detail::InkGlyph::Folder, kTargetStorageBrowse);
        draw_act(BoxActRect(box, 0), popup_detail::InkGlyph::Reset, kTargetStorageReset);
    }

    // 快捷键
    draw_text(L"快捷键", section_format_,
              Rect(kContentLeft, rows.shortcuts_label, kContentRight, rows.shortcuts_label + kSectionHeight), ink3,
              1.0f, DWRITE_TEXT_ALIGNMENT_LEADING, 1.0f);
    {
        const bool recording_primary = capturing_hotkey_ && !capturing_continuous_paste_hotkey_;
        const auto box = HotkeyBoxRect(rows);
        draw_box(box, kBoxHoverHotkey, false, recording_primary, hotkey_conflict_);
        draw_row_label(rows.hotkey, kHotkeyRowHeight, L"呼出窗口", L"", false, kControlLeft - 12.0f);
        const auto value = recording_primary ? std::wstring(L"请按下新热键…")
                                              : FormatHotkeyDisplay(hotkey_modifiers_, hotkey_vk_);
        // 录制态：朱砂色 + 1s 循环闪烁（50% 处半透明），与预览的 blink 关键帧一致。
        float value_alpha = 1.0f;
        if (recording_primary) {
            const float phase = static_cast<float>(GetTickCount64() % 1000ULL) / 1000.0f;
            value_alpha = 0.75f + 0.25f * std::cos(phase * 6.28318530718f);
        }
        draw_text(value, value_mono_format_, Rect(box.left + 10.0f, box.top, BoxActRect(box, 0).left - 4.0f, box.bottom),
                  recording_primary ? seal : ink, value_alpha);
        draw_act(BoxActRect(box, 0), popup_detail::InkGlyph::Reset, kTargetHotkeyReset);
    }
    hairline(kContentLeft, rows.hotkey + kHotkeyRowHeight - 1.0f, kContentRight);
    {
        const bool recording_continuous = capturing_hotkey_ && capturing_continuous_paste_hotkey_;
        const bool conflict = hotkey_conflict_ || continuous_paste_hotkey_conflict_;
        const auto box = ContinuousBoxRect(rows);
        draw_box(box, kBoxHoverContinuous, false, recording_continuous, continuous_paste_hotkey_conflict_);
        draw_row_label(rows.continuous, kContinuousRowHeight, L"连续粘贴",
                       conflict ? L"快捷键被占用" : L"粘贴后自动选中下一条", conflict, kControlLeft - 12.0f);
        const auto value = recording_continuous
                               ? std::wstring(L"请按下新热键…")
                               : FormatHotkeyDisplay(continuous_paste_hotkey_modifiers_,
                                                     continuous_paste_hotkey_vk_);
        float value_alpha = 1.0f;
        if (recording_continuous) {
            const float phase = static_cast<float>(GetTickCount64() % 1000ULL) / 1000.0f;
            value_alpha = 0.75f + 0.25f * std::cos(phase * 6.28318530718f);
        }
        draw_text(value, value_mono_format_, Rect(box.left + 10.0f, box.top, BoxActRect(box, 0).left - 4.0f, box.bottom),
                  recording_continuous ? seal : ink, value_alpha);
        draw_act(BoxActRect(box, 0), popup_detail::InkGlyph::Reset, kTargetContinuousPasteHotkeyReset);
    }

    // 外观
    draw_text(L"外观", section_format_,
              Rect(kContentLeft, rows.appearance_label, kContentRight, rows.appearance_label + kSectionHeight), ink3,
              1.0f, DWRITE_TEXT_ALIGNMENT_LEADING, 1.0f);
    {
        const auto box = ThemeBoxRect(rows);
        draw_row_label(rows.theme, kThemeRowHeight, L"主题", L"", false, kControlLeft - 12.0f);
        fill_round(box, kBoxRadius, deep, 1.0f);
        const float slot = box.Width() / 3.0f;
        const float eased = theme_motion_progress_ >= 1.0f ? 1.0f : EaseSegment(theme_motion_progress_);
        // EaseSegment 带回弹（会略微超过 1），滑块 index 必须夹在 [0,2]：不然它会窜出轨道右缘，
        // 看着像「抖一下」。夹住之后回弹只表现为轻微过冲，滑块始终贴着格子走。
        const float index = std::clamp(static_cast<float>(theme_motion_from_) +
                                           (static_cast<float>(theme_mode_ - theme_motion_from_)) * eased,
                                       0.0f, 2.0f);
        const UiRect thumb = Rect(box.left + 2.0f + slot * index, box.top + 2.0f,
                                  box.left + 2.0f + slot * (index + 1.0f), box.bottom - 2.0f);
        brush_->SetColor(D2D1::ColorF(0x000000, 0.06f));
        render_target_->FillRoundedRectangle(
            RoundRect(Rect(thumb.left, thumb.top + 1.0f, thumb.right, thumb.bottom + 1.0f), 5.0f), brush_);
        fill_round(thumb, 5.0f, slip, 1.0f);
        stroke_round(Inset(thumb, 0.5f), 4.5f, ink, ring_alpha, 1.0f);
        constexpr const wchar_t* kThemeLabels[3] = {L"跟随系统", L"浅色", L"深色"};
        const bool theme_settled = theme_motion_progress_ >= 1.0f;
        for (int i = 0; i < 3; ++i) {
            const UiRect slot_rect = Rect(box.left + slot * static_cast<float>(i), box.top,
                                          box.left + slot * static_cast<float>(i + 1), box.bottom);
            if (theme_settled) {
                const bool active = theme_mode_ == i;
                draw_text(kThemeLabels[i], active ? seg_active_format_ : seg_format_, slot_rect, active ? ink : ink3,
                          1.0f, DWRITE_TEXT_ALIGNMENT_CENTER);
                continue;
            }
            // 动画期间不切换字重（会「啪」一下），而是按滑块离哪一格近来决定字的深浅。
            const float proximity = std::clamp(1.0f - std::fabs(index - static_cast<float>(i)), 0.0f, 1.0f);
            const uint32_t label_color = LerpPopupThemePalette(PopupThemePalette{.text_tertiary = ink3},
                                                               PopupThemePalette{.text_tertiary = ink}, proximity)
                                              .text_tertiary;
            draw_text(kThemeLabels[i], seg_format_, slot_rect, label_color, 1.0f, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
    hairline(kContentLeft, rows.theme + kThemeRowHeight - 1.0f, kContentRight);

    draw_row_label(rows.resizable, kResizableRowHeight, L"窗口缩放", L"允许拖动边缘调整弹窗大小", false,
                   kToggleLeft - 12.0f);
    {
        const auto solo = SizeResetRect(rows);
        if (size_reset_progress_ > 0.001f) {
            const float scale = 0.8f + 0.2f * EaseSolo(size_reset_progress_);
            const auto scaled = scale_rect(solo, scale);
            const float hover = hover_alpha(kTargetPopupSizeReset);
            if (hover > 0.0f) {
                fill_round(scaled, 6.0f, deep, hover * size_reset_progress_);
            }
            const float cx = (scaled.left + scaled.right) * 0.5f;
            const float cy = (scaled.top + scaled.bottom) * 0.5f;
            draw_glyph(popup_detail::InkGlyph::Reset, Rect(cx - 6.0f, cy - 6.0f, cx + 6.0f, cy + 6.0f),
                       hover > 0.0f ? ink : ink3, size_reset_progress_);
        }
        draw_toggle(rows.resizable, kResizableRowHeight, popup_resizable_, kTargetPopupResizable);
    }
    hairline(kContentLeft, rows.resizable + kResizableRowHeight - 1.0f, kContentRight);

    draw_row_label(rows.startup, kStartupRowHeight, L"开机自启", L"", false, kToggleLeft - 12.0f);
    draw_toggle(rows.startup, kStartupRowHeight, startup_, kTargetStartup);

    // 关于
    draw_text(L"关于", section_format_,
              Rect(kContentLeft, rows.about_label, kContentRight, rows.about_label + kSectionHeight), ink3, 1.0f,
              DWRITE_TEXT_ALIGNMENT_LEADING, 1.0f);
    draw_row_label(rows.project, kProjectRowHeight, L"项目主页", L"", false, ProjectLinkRect(rows).left - 12.0f);
    {
        const auto link = ProjectLinkRect(rows);
        const float hover = hover_alpha(kTargetProjectUrl);
        stroke_round(Inset(link, 0.5f), kBoxRadius - 0.5f, hover > 0.0f ? ink3 : ink, hover > 0.0f ? 1.0f : ring_alpha, 1.0f);
        draw_text(L"GitHub", link_format_, Rect(link.left + 10.0f, link.top, link.right - 16.0f, link.bottom),
                  hover > 0.0f ? ink : ink2, 1.0f);
        const float icon_cx = link.right - 15.0f;
        const float icon_cy = (link.top + link.bottom) * 0.5f;
        draw_glyph(popup_detail::InkGlyph::External, Rect(icon_cx - 5.0f, icon_cy - 5.0f, icon_cx + 5.0f, icon_cy + 5.0f),
                   hover > 0.0f ? ink : ink2, 1.0f);
    }
    hairline(kContentLeft, rows.project + kProjectRowHeight - 1.0f, kContentRight);

    draw_row_label(rows.version, kVersionRowHeight, L"当前版本", L"", false, kContentRight);
    {
        std::wstring version(kClipSoulVersion);
        if (!version.empty() && (version.front() == L'v' || version.front() == L'V')) {
            version.erase(version.begin());
        }
        draw_text(version, ver_format_,
                  Rect(kContentLeft, rows.version + (kVersionRowHeight - 28.0f) * 0.5f, kContentRight,
                       rows.version + (kVersionRowHeight + 28.0f) * 0.5f),
                  ink3, 1.0f, DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    render_target_->SetTransform(D2D1::Matrix3x2F::Identity());
    render_target_->PopAxisAlignedClip();

    // ---------- 底栏 ----------
    hairline(0.0f, kBodyBottom, kSettingsWidth);
    draw_text(save_message_, msg_format_,
              Rect(kContentLeft, rows.footer_top, FooterButtonRect(rows, 0).left - 12.0f,
                   rows.footer_top + kFooterHeight),
              storage_save_failed_ ? seal : (save_message_ok_ ? ok : ink3), 1.0f);
    {
        const auto cancel = FooterButtonRect(rows, 0);
        const float hover = hover_alpha(kTargetCancel);
        if (hover > 0.0f) {
            fill_round(scale_rect(cancel, press_scale(kTargetCancel)), kBoxRadius, deep, hover);
        }
        stroke_round(Inset(cancel, 0.5f), kBoxRadius - 0.5f, ink, ring_alpha, 1.0f);
        draw_text(L"取消", button_format_, cancel, hover > 0.0f ? ink : ink2, 1.0f, DWRITE_TEXT_ALIGNMENT_CENTER);

        const auto save = FooterButtonRect(rows, 1);
        const float save_hover = hover_alpha(kTargetSave);
        const auto save_scaled = scale_rect(save, press_scale(kTargetSave));
        fill_round(save_scaled, kBoxRadius, Brighten(ink, 1.0f + 0.3f * save_hover), 1.0f);
        draw_text(L"保存", button_format_, save_scaled, paper, 1.0f, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // ---------- 图标提示 ----------
    if (tooltip_progress_ > 0.001f && tooltip_target_ != 0) {
        const std::wstring_view tip = TooltipForTarget(tooltip_target_);
        if (!tip.empty()) {
            UiRect anchor{};
            switch (tooltip_target_) {
            case kTargetStorageBrowse: anchor = BoxActRect(StorageBoxRect(rows), 1); break;
            case kTargetStorageReset: anchor = BoxActRect(StorageBoxRect(rows), 0); break;
            case kTargetHotkeyReset: anchor = BoxActRect(HotkeyBoxRect(rows), 0); break;
            case kTargetContinuousPasteHotkeyReset: anchor = BoxActRect(ContinuousBoxRect(rows), 0); break;
            case kTargetPopupSizeReset: anchor = SizeResetRect(rows); break;
            default: break;
            }
            if (anchor.Width() > 0.0f) {
                const float width = text_width(tip, tooltip_format_) + 14.0f;
                const float height = 19.0f;
                float right = anchor.right;
                float top = anchor.bottom + 4.0f;
                right = std::min(right, kSettingsWidth - 4.0f);
                const UiRect tip_rect = Rect(right - width, top, right, top + height);
                fill_round(tip_rect, 4.0f, ink, 0.94f * tooltip_progress_);
                draw_text(tip, tooltip_format_, tip_rect, paper, tooltip_progress_, DWRITE_TEXT_ALIGNMENT_CENTER);
            }
        }
    }

    const HRESULT result = render_target_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
}

LRESULT SettingsWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT:
        if (IsWindowVisible(hwnd_)) {
            Paint();
        }
        ValidateRect(hwnd_, nullptr);
        return 0;
    case WM_SHOWWINDOW:
        KillTimer(hwnd_, kSettingsIdleMemoryTimer);
        if (!wparam) {
            SetTimer(hwnd_, kSettingsIdleMemoryTimer, 750, nullptr);
            KillTimer(hwnd_, kSettingsAnimationTimer);
            if (tracking_mouse_) {
                TRACKMOUSEEVENT track{sizeof(track), TME_CANCEL | TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&track);
            }
            tracking_mouse_ = false;
            hover_target_ = 0;
            hover_fade_target_ = 0;
            hover_progress_ = 1.0f;
            pressed_target_ = 0;
            tooltip_target_ = 0;
            tooltip_progress_ = 0.0f;
            tooltip_dwell_ms_ = 0;
            toggle_motion_target_ = 0;
            toggle_motion_progress_ = 1.0f;
            theme_motion_from_ = theme_mode_;
            theme_motion_progress_ = 1.0f;
            window_transition_active_ = false;
            window_transition_progress_ = 1.0f;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (capturing_hotkey_) {
            if (wparam == VK_ESCAPE) {
                capturing_hotkey_ = false;
                capturing_continuous_paste_hotkey_ = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            SetCapturedHotkey(wparam);
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            LoadToControls();
            BeginHide();
            return 0;
        }
        break;
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
        const int viewport_height = static_cast<int>(kBodyBottom - kBodyTop);
        const int maximum = std::max(0, static_cast<int>(kContentBottom - kBodyTop) - viewport_height);
        const int next = std::clamp(scroll_offset_ - (delta / WHEEL_DELTA) * 42, 0, maximum);
        if (next != scroll_offset_) {
            scroll_offset_ = next;
            UpdateLimitEditPosition();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_TIMER:
        if (wparam == kSettingsIdleMemoryTimer) {
            popup_detail::TrimIdleWorkingSet(hwnd_, kSettingsIdleMemoryTimer);
            return 0;
        }
        if (wparam == kSettingsSaveCloseTimer) {
            KillTimer(hwnd_, kSettingsSaveCloseTimer);
            BeginHide();
            return 0;
        }
        if (wparam == kSettingsAnimationTimer) {
            AdvanceAnimations();
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lparam) == limit_edit_) {
            // 用「当前正在显示」的配色（主题过渡期间是插值色），保证原生 EDIT 的底色和
            // 外面 D2D 画的那个圆角框完全一致，不会露出一块矩形色块。
            const auto palette = CurrentPalette();
            const COLORREF edit_fill =
                RGB((palette.panel_fill >> 16) & 0xFF, (palette.panel_fill >> 8) & 0xFF, palette.panel_fill & 0xFF);
            SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
            SetBkColor(reinterpret_cast<HDC>(wparam), edit_fill);
            SetTextColor(reinterpret_cast<HDC>(wparam),
                         RGB((palette.text >> 16) & 0xFF, (palette.text >> 8) & 0xFF, palette.text & 0xFF));
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
        RefreshHoverFromCursor();
        StartAnimationTimer();
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_mouse_ = false;
        RefreshHoverFromCursor();
        return 0;
    case kSettingsRefreshHoverMessage:
        RefreshHoverFromCursor();
        return 0;
    case WM_SETCURSOR: {
        if (LOWORD(lparam) != HTCLIENT) {
            break;
        }
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(hwnd_, &point);
        const int scale = DpiScale(hwnd_, 96);
        const int logical_x = MulDiv(point.x, 96, scale);
        const int logical_y = MulDiv(point.y, 96, scale);
        const int target = HitTestTarget(static_cast<float>(logical_x), static_cast<float>(logical_y), scroll_offset_, size_reset_target_visible_);
        const bool hand = IsInteractiveTarget(target) && target != kTargetLimit;
        SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        const int scale = DpiScale(hwnd_, 96);
        const int logical_x = MulDiv(GET_X_LPARAM(lparam), 96, scale);
        const int logical_y = MulDiv(GET_Y_LPARAM(lparam), 96, scale);
        const int target = HitTestTarget(static_cast<float>(logical_x), static_cast<float>(logical_y), scroll_offset_, size_reset_target_visible_);
        pressed_target_ = target;
        if (target == kTargetLimit) {
            SetFocus(limit_edit_);
            SendMessageW(limit_edit_, EM_SETSEL, 0, -1);
            return 0;
        }
        if (target != 0) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        SetFocus(hwnd_);
        return 0;
    }
    case WM_LBUTTONUP: {
        const int scale = DpiScale(hwnd_, 96);
        const int logical_x = MulDiv(GET_X_LPARAM(lparam), 96, scale);
        const int logical_y = MulDiv(GET_Y_LPARAM(lparam), 96, scale);
        const int target = HitTestTarget(static_cast<float>(logical_x), static_cast<float>(logical_y), scroll_offset_, size_reset_target_visible_);
        const int pressed = pressed_target_;
        pressed_target_ = 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (pressed == 0 || pressed != target) {
            return 0;
        }
        switch (target) {
        case kTargetClose:
        case kTargetCancel:
            LoadToControls();
            BeginHide();
            return 0;
        case kTargetSave:
            if (SaveFromControls()) {
                SetSaveMessage(L"✓ 已保存", true);
                SetTimer(hwnd_, kSettingsSaveCloseTimer, kSettingsSaveCloseDelayMs, nullptr);
            } else {
                SetSaveMessage(storage_save_failed_ ? L"存储位置保存失败" : L"快捷键被占用", false);
            }
            return 0;
        case kTargetPause: TogglePause(); return 0;
        case kTargetStartup: ToggleStartup(); return 0;
        case kTargetPopupResizable: TogglePopupResizable(); return 0;
        case kTargetPopupSizeReset: ResetPopupSize(); return 0;
        case kTargetHotkey: StartHotkeyCapture(false); return 0;
        case kTargetHotkeyReset: ResetHotkeyToDefault(); return 0;
        case kTargetContinuousPasteHotkey: StartHotkeyCapture(true); return 0;
        case kTargetContinuousPasteHotkeyReset: ResetContinuousPasteHotkeyToDefault(); return 0;
        case kTargetStorageBrowse: BrowseStorageDirectory(); return 0;
        case kTargetStorageReset:
            storage_path_ = app_.StorageDirectory().wstring();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case kTargetThemeSystem: SetThemeMode(0); return 0;
        case kTargetThemeLight: SetThemeMode(1); return 0;
        case kTargetThemeDark: SetThemeMode(2); return 0;
        case kTargetProjectUrl:
            ShellExecuteW(hwnd_, L"open", kClipSoulProjectUrl.data(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        default: return 0;
        }
    }
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &point);
        const int scale = DpiScale(hwnd_, 96);
        point.x = MulDiv(point.x, 96, scale);
        point.y = MulDiv(point.y, 96, scale);
        if (point.y >= 0 && point.y <= static_cast<int>(kHeaderHeight) && point.x < static_cast<int>(kCloseLeft)) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_CLOSE:
        BeginHide();
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
