#include "ClipSoul/PopupWindow.h"

#include "ClipSoul/PasteController.h"
#include "ClipSoul/PasteModel.h"
#include "ClipSoul/PopupLayout.h"
#include "ClipSoul/ResourceIds.h"
#include "ClipSoul/TextUtil.h"
#include "ClipSoul/Version.h"
#include "ClipSoul/Win32Util.h"
#include "popup/CaretLocator.h"
#include "popup/InkGlyphs.h"
#include "popup/IdleMemory.h"
#include "popup/PromptDialogs.h"
#include "popup/PopupWin32Helpers.h"

#include <commctrl.h>
#include <imm.h>
#include <oleacc.h>
#include <oleauto.h>
#include <UIAutomationClient.h>
#include <windowsx.h>

#include <algorithm>
#include <unordered_set>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ClipSoul {
using namespace popup_detail;
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr wchar_t kPopupClass[] = L"ClipSoul.PopupWindow";
constexpr UINT_PTR kAnimationTimer = 42;
// 1:1 复刻预览的 60fps 过渡：WM_TIMER 实际最小粒度约 10~15ms，
// 用 10ms 请求值才能让 170ms 的 winIn 落下十几帧。
constexpr UINT kAnimationTimerMs = 10;
constexpr UINT_PTR kHoverTimer = 43;
constexpr UINT_PTR kSearchCaretTimer = 44;
constexpr UINT_PTR kOutsideClickTimer = 45;
constexpr UINT_PTR kItemLongPressTimer = 46;
constexpr UINT_PTR kSuppressInactiveHideTimer = 47;
constexpr UINT_PTR kShellTopmostRaiseTimer = 48;
constexpr UINT_PTR kIdleMemoryTimer = 49;
// 搜索输入防抖：每敲一个字符就重新查库 + 数一次总数太贵（一次约 5 条 SQL）。
constexpr UINT_PTR kSearchReloadTimer = 50;
// 分批粘贴：每写完一段要给目标程序（Office、IDE）一点时间消费，才不会把后一段抢先覆盖。
constexpr UINT_PTR kMultiPasteTimer = 51;
constexpr UINT kMultiPasteDelayMs = 70;
constexpr UINT kSearchReloadDelayMs = 90;
constexpr DWORD kTransientHideSuppressMs = 650;
constexpr DWORD kShellTopmostRaiseMs = 700;
constexpr int kPopupResizeLeft = 0x1;
constexpr int kPopupResizeRight = 0x2;
constexpr int kPopupResizeTop = 0x4;
constexpr int kPopupResizeBottom = 0x8;
constexpr int kPopupResizeGrip = 8;
constexpr int kMinPopupWidth = 300;
constexpr int kMinPopupHeight = 360;
constexpr int kMaxPopupWidth = 760;
constexpr int kMaxPopupHeight = 980;
constexpr int kContextDelete = 3001;
constexpr int kContextPin = 3002;
constexpr int kContextFavorite = 3003;
constexpr int kContextSetNote = 3004;
constexpr int kContextMoveUp = 3005;
constexpr int kContextMoveDown = 3006;
constexpr int kContextFavoriteGroupBase = 3100;
constexpr int kContextFavoriteGroupMax = 3199;
constexpr DWORD kChildWindowExitMs = 90;
constexpr DWORD kPopupBandShellSurface = 16;
constexpr int kFilterPresetCustom = 4;
constexpr int kFilterPresetCount = 5;

using CreateWindowInBandProc = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                             LPVOID, DWORD);

struct ScopedD2DLayer {
    ID2D1RenderTarget* target = nullptr;
    ID2D1Layer* layer = nullptr;
    bool active = false;

    ~ScopedD2DLayer() {
        if (active && target) {
            target->PopLayer();
        }
        ReleasePtr(layer);
    }
};

D2D1_RECT_F Rect(float left, float top, float right, float bottom) {
    return D2D1::RectF(left, top, right, bottom);
}

D2D1_RECT_F Rect(const UiRect& rect) {
    return D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom);
}

D2D1_ROUNDED_RECT RoundRect(float left, float top, float right, float bottom, float radius) {
    return D2D1::RoundedRect(Rect(left, top, right, bottom), radius, radius);
}

D2D1_ROUNDED_RECT RoundRect(const UiRect& rect, float radius) {
    return D2D1::RoundedRect(Rect(rect), radius, radius);
}

UiRect CenteredRect(const UiRect& rect, float width, float height) {
    const float x = rect.left + (rect.Width() - width) * 0.5f;
    const float y = rect.top + (rect.Height() - height) * 0.5f;
    return UiRect{x, y, x + width, y + height};
}

UiRect ShrinkRect(const UiRect& rect, float dx, float dy) {
    return UiRect{rect.left + dx, rect.top + dy, rect.right - dx, rect.bottom - dy};
}

D2D1_POINT_2F RotatePoint(float x, float y, float center_x, float center_y, float radians) {
    const float s = std::sin(radians);
    const float c = std::cos(radians);
    const float dx = x - center_x;
    const float dy = y - center_y;
    return D2D1::Point2F(center_x + dx * c - dy * s, center_y + dx * s + dy * c);
}

bool Contains(const UiRect& rect, POINT value) {
    return value.x >= rect.left && value.x <= rect.right && value.y >= rect.top && value.y <= rect.bottom;
}

bool ContainsWindow(HWND parent, HWND candidate) {
    if (!parent || !candidate) {
        return false;
    }
    return parent == candidate || IsChild(parent, candidate);
}

bool ScreenPointInsideWindow(HWND hwnd, POINT point) {
    RECT rect{};
    return hwnd && GetWindowRect(hwnd, &rect) && PtInRect(&rect, point);
}

void ActivatePopupWindow(HWND hwnd) {
    if (!hwnd) {
        return;
    }
    const DWORD current_thread = GetCurrentThreadId();
    DWORD foreground_process = 0;
    const DWORD foreground_thread = GetWindowThreadProcessId(GetForegroundWindow(), &foreground_process);
    const bool attach = foreground_thread != 0 && foreground_thread != current_thread;
    if (attach) {
        AttachThreadInput(current_thread, foreground_thread, TRUE);
    }
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    if (attach) {
        AttachThreadInput(current_thread, foreground_thread, FALSE);
    }
}

CreateWindowInBandProc ResolveCreateWindowInBand() {
    static const auto proc = reinterpret_cast<CreateWindowInBandProc>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "CreateWindowInBand"));
    return proc;
}

void FocusSearchEdit(HWND search_edit) {
    if (!search_edit) {
        return;
    }
    SetFocus(search_edit);
    HideCaret(search_edit);
}

void HideNativeSearchCaret(HWND search_edit) {
    if (search_edit && GetFocus() == search_edit) {
        HideCaret(search_edit);
    }
}

UiRect FavoriteGroupDeleteConfirmPanelRect(int logical_width = 0) {
    const auto& metrics = PopupMetrics();
    const float kWidth = static_cast<float>(logical_width > 0 ? logical_width : metrics.width) - 40.0f;
    constexpr float kHeight = 150.0f;
    const float surface_width = logical_width > 0 ? static_cast<float>(logical_width) : static_cast<float>(metrics.width);
    const float left = (surface_width - kWidth) * 0.5f;
    const float top = 104.0f;
    return UiRect{left, top, left + kWidth, top + kHeight};
}

UiRect FavoriteGroupDeleteConfirmDeleteRect(const UiRect& panel) {
    return UiRect{panel.right - 68.0f, panel.bottom - 42.0f, panel.right - 16.0f, panel.bottom - 14.0f};
}

UiRect FavoriteGroupDeleteConfirmCancelRect(const UiRect& panel) {
    return UiRect{panel.right - 126.0f, panel.bottom - 42.0f, panel.right - 74.0f, panel.bottom - 14.0f};
}

PopupFavoriteGroupDeleteConfirmTarget HitTestFavoriteGroupDeleteConfirm(POINT point, int logical_width = 0) {
    const auto panel = FavoriteGroupDeleteConfirmPanelRect(logical_width);
    if (!Contains(panel, point)) {
        return PopupFavoriteGroupDeleteConfirmTarget::None;
    }
    if (Contains(FavoriteGroupDeleteConfirmDeleteRect(panel), point)) {
        return PopupFavoriteGroupDeleteConfirmTarget::Delete;
    }
    if (Contains(FavoriteGroupDeleteConfirmCancelRect(panel), point)) {
        return PopupFavoriteGroupDeleteConfirmTarget::Cancel;
    }
    return PopupFavoriteGroupDeleteConfirmTarget::Panel;
}

UINT DpiForMonitorHandle(HMONITOR monitor, UINT fallback_dpi) {
    if (monitor) {
        if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
            using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
            auto* get_dpi_for_monitor = reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor"));
            if (get_dpi_for_monitor) {
                UINT dpi_x = 0;
                UINT dpi_y = 0;
                constexpr int kMdtEffectiveDpi = 0;
                if (SUCCEEDED(get_dpi_for_monitor(monitor, kMdtEffectiveDpi, &dpi_x, &dpi_y)) && dpi_x != 0) {
                    FreeLibrary(shcore);
                    return dpi_x;
                }
            }
            FreeLibrary(shcore);
        }
    }
    return fallback_dpi == 0 ? 96 : fallback_dpi;
}

HMONITOR MonitorForPopupTarget(HWND target, HWND popup) {
    if (target) {
        return MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
    }
    if (popup) {
        return MonitorFromWindow(popup, MONITOR_DEFAULTTONEAREST);
    }
    return MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
}

RECT WorkAreaForMonitor(HMONITOR monitor, RECT fallback_work) {
    MONITORINFO info{sizeof(info)};
    if (monitor && GetMonitorInfoW(monitor, &info)) {
        return info.rcWork;
    }
    return fallback_work;
}

RECT MonitorRectForMonitor(HMONITOR monitor, RECT fallback_monitor) {
    MONITORINFO info{sizeof(info)};
    if (monitor && GetMonitorInfoW(monitor, &info)) {
        return info.rcMonitor;
    }
    return fallback_monitor;
}

RECT VirtualScreenRect() {
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width > 0 && height > 0) {
        return RECT{left, top, left + width, top + height};
    }
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    return work;
}

std::wstring ItemTitle(const HistoryItem& item) {
    if (item.kind == ClipboardKind::Files && !item.files.empty()) {
        return FileNameFromPath(item.files.front());
    }
    if (item.kind == ClipboardKind::Image) {
        return L"图片";
    }
    return item.preview.empty() ? item.search_text : item.preview;
}

std::wstring ItemMeta(const HistoryItem& item) {
    std::wstring meta = KindLabel(static_cast<int>(item.kind));
    if (item.is_pinned) meta += L" · 置顶";
    if (item.is_favorite) meta += L" · 收藏";
    if (!item.note.empty()) meta += L" · " + PopupNotePreviewText(item.note);
    return meta;
}

std::wstring ItemMetaSummary(const HistoryItem& item) {
    // Ink Ledger 副行：类型 · 字数/数量。备注单独绘制（带笔形小图标）。
    std::wstring meta = item.is_phrase ? L"常用语" : KindLabel(static_cast<int>(item.kind));
    switch (item.kind) {
    case ClipboardKind::Files:
        meta += L" · " + std::to_wstring(item.files.size()) + L" 个文件";
        break;
    case ClipboardKind::Image:
        break;
    case ClipboardKind::Link: {
        const auto& url = !item.text.empty() ? item.text : item.search_text;
        const auto scheme = url.find(L"://");
        const auto begin = scheme == std::wstring::npos ? 0 : scheme + 3;
        const auto end = url.find_first_of(L"/?#", begin);
        meta += L" · " + url.substr(begin, end == std::wstring::npos ? end : end - begin);
        break;
    }
    default: {
        const auto& body = !item.text.empty() ? item.text : item.search_text;
        meta += L" · " + std::to_wstring(body.size()) + L" 字";
        break;
    }
    }
    return meta;
}

std::vector<std::pair<size_t, size_t>> FindSearchMatches(std::wstring_view text, std::wstring_view needle) {
    std::vector<std::pair<size_t, size_t>> matches;
    if (needle.empty() || text.empty() || needle.size() > text.size()) {
        return matches;
    }
    const auto fold = [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); };
    for (size_t index = 0; index + needle.size() <= text.size();) {
        bool hit = true;
        for (size_t offset = 0; offset < needle.size(); ++offset) {
            if (fold(text[index + offset]) != fold(needle[offset])) {
                hit = false;
                break;
            }
        }
        if (hit) {
            matches.emplace_back(index, needle.size());
            index += needle.size();
        } else {
            ++index;
        }
    }
    return matches;
}

std::wstring ItemDetailText(const HistoryItem& item) {
    std::wstring detail;
    if (!item.note.empty()) {
        detail = L"\u5907\u6ce8\uff1a";
        detail += item.note;
    }
    auto append_content = [&](std::wstring value) {
        if (value.empty()) {
            return detail;
        }
        if (!detail.empty()) {
            detail += L"\n\n";
        }
        detail += value;
        return detail;
    };

    if (item.kind == ClipboardKind::Files) {
        std::wstringstream stream;
        for (size_t index = 0; index < item.files.size(); ++index) {
            if (index > 0) {
                stream << L"\n";
            }
            stream << item.files[index];
        }
        return append_content(stream.str());
    }
    if (item.kind == ClipboardKind::Link) {
        return append_content(!item.text.empty() ? item.text : item.search_text);
    }
    if (item.kind == ClipboardKind::Image) {
        return append_content(item.payload_path.wstring());
    }
    return append_content(!item.text.empty() ? item.text : item.search_text);
}

// "Now" and "today" are resolved once per paint and passed in: this runs for every visible row,
// and each ItemTime() call used to do two localtime_s() calls.
struct TimeLabelContext {
    std::time_t now = 0;
    std::tm today{};
    bool valid = false;

    static TimeLabelContext Now() {
        TimeLabelContext context;
        context.now = std::time(nullptr);
        context.valid = localtime_s(&context.today, &context.now) == 0;
        return context;
    }
};

std::wstring ItemTime(const HistoryItem& item, const TimeLabelContext& context) {
    const std::time_t value = static_cast<std::time_t>(item.created_at_unix);
    std::tm local_time{};
    if (localtime_s(&local_time, &value) != 0) {
        return {};
    }
    std::tm today{};
    std::time_t now = context.now;
    if (context.valid) {
        today = context.today;
    } else {
        now = std::time(nullptr);
        if (localtime_s(&today, &now) != 0) {
            today = local_time;
        }
    }
    wchar_t buffer[16]{};
    if (local_time.tm_year == today.tm_year && local_time.tm_yday == today.tm_yday)
        wcsftime(buffer, std::size(buffer), L"%H:%M", &local_time);
    else if (now - value < 48 * 60 * 60 && (local_time.tm_yday + 1 == today.tm_yday || today.tm_yday == 0))
        return L"昨天";
    else swprintf_s(buffer, L"%d/%d", local_time.tm_mon + 1, local_time.tm_mday);
    return buffer;
}

D2D1_COLOR_F ColorWithAlpha(uint32_t rgb, float alpha) {
    return D2D1::ColorF(rgb, alpha);
}

uint32_t PopupNeutralHoverFill(const PopupThemePalette& palette) {
    return palette.paper_hover;
}

int64_t LocalDateUnix(PopupCalendarDate date, bool end_of_day) {
    std::tm value{};
    value.tm_year = date.year - 1900;
    value.tm_mon = date.month - 1;
    value.tm_mday = date.day;
    value.tm_hour = end_of_day ? 23 : 0;
    value.tm_min = end_of_day ? 59 : 0;
    value.tm_sec = end_of_day ? 59 : 0;
    value.tm_isdst = -1;
    return static_cast<int64_t>(std::mktime(&value));
}

std::wstring FormatDateValue(const std::optional<PopupCalendarDate>& date) {
    if (!date) {
        return L"未选择";
    }
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"%04d/%02d/%02d", date->year, date->month, date->day);
    return buffer;
}

std::wstring FormatMonthTitle(int year, int month) {
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"%04d\u5e74%d\u6708", year, month);
    return buffer;
}

std::wstring ForegroundWindowDebugInfo() {
    HWND foreground = GetForegroundWindow();
    std::wstring info = HexWindow(foreground);
    info += L" class=";
    info += WindowClassName(foreground);
    info += L" process=";
    info += WindowProcessName(foreground);
    return info;
}

void ShiftMonth(int& year, int& month, int delta) {
    month += delta;
    while (month < 1) {
        month += 12;
        --year;
    }
    while (month > 12) {
        month -= 12;
        ++year;
    }
}

PopupCalendarDate TodayDate() {
    const std::time_t now = std::time(nullptr);
    std::tm local_time{};
    if (localtime_s(&local_time, &now) != 0) {
        return PopupCalendarDate{2026, 5, 24};
    }
    return PopupCalendarDate{local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday};
}
bool LoadWicFrameFromFile(IWICImagingFactory* factory, const std::filesystem::path& path, IWICBitmapDecoder** decoder,
                          IWICBitmapFrameDecode** frame) {
    return factory && decoder && frame &&
           SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                        WICDecodeMetadataCacheOnLoad, decoder)) &&
           SUCCEEDED((*decoder)->GetFrame(0, frame));
}

IWICBitmapSource* CreateScaledPreviewSource(IWICImagingFactory* factory, IWICBitmapFrameDecode* frame) {
    if (!factory || !frame) {
        return nullptr;
    }
    UINT width = 0;
    UINT height = 0;
    if (FAILED(frame->GetSize(&width, &height))) {
        return nullptr;
    }
    const SIZE target = PopupPreviewDecodeSize(width, height, PopupImageFilePreviewDecodePixelLimit());
    if (target.cx <= 0 || target.cy <= 0) {
        return nullptr;
    }
    if (target.cx == static_cast<LONG>(width) && target.cy == static_cast<LONG>(height)) {
        frame->AddRef();
        return frame;
    }

    IWICBitmapScaler* scaler = nullptr;
    if (FAILED(factory->CreateBitmapScaler(&scaler))) {
        return nullptr;
    }
    if (FAILED(scaler->Initialize(frame, static_cast<UINT>(target.cx), static_cast<UINT>(target.cy),
                                  WICBitmapInterpolationModeFant))) {
        ReleasePtr(scaler);
        return nullptr;
    }
    return scaler;
}

} // namespace

PopupWindow::PopupWindow(HINSTANCE instance, HistoryStore& store, PasteController& paste_controller)
    : instance_(instance),
      store_(store),
      paste_controller_(paste_controller) {
    ink_glyph_cache_=std::make_unique<InkGlyphCache>();
    const auto today = TodayDate();
    calendar_year_ = today.year;
    calendar_month_ = today.month;
}

PopupWindow::~PopupWindow() {
    DiscardDeviceResources();
    if (search_edit_brush_) {
        DeleteObject(search_edit_brush_);
        search_edit_brush_ = nullptr;
    }
    DiscardTextResources();
    ink_glyph_cache_.reset();
    ReleasePtr(d2d_factory_);
}

void PopupWindow::SetDebugLogger(std::function<void(std::wstring_view)> logger) {
    debug_logger_ = std::move(logger);
}

void PopupWindow::SetKeyboardInvocation(bool keyboard_invocation) {
    keyboard_invocation_ = keyboard_invocation;
}

void PopupWindow::DebugLog(std::wstring_view message) const {
    if (debug_logger_) {
        debug_logger_(message);
    }
}

bool PopupWindow::Create(HWND owner) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = PopupWindow::WindowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kPopupClass;
    RegisterClassW(&wc);

    const UINT dpi = DpiForWindowHandle(owner);
    const SIZE size = PhysicalPopupSize(dpi);
    const DWORD popup_ex_style = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED |
                                 (PopupWindowShouldUseNoActivateStyle() ? WS_EX_NOACTIVATE : 0);
    if (PopupShouldCreateInShellWindowBand()) {
        if (const auto create_window_in_band = ResolveCreateWindowInBand()) {
            hwnd_ = create_window_in_band(popup_ex_style, kPopupClass, L"ClipSoul", WS_POPUP | WS_CLIPCHILDREN,
                                          CW_USEDEFAULT, CW_USEDEFAULT, size.cx, size.cy, owner, nullptr, instance_,
                                          this, kPopupBandShellSurface);
        }
    }
    if (!hwnd_) {
        hwnd_ = CreateWindowExW(popup_ex_style, kPopupClass, L"ClipSoul",
                                WS_POPUP | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, size.cx, size.cy, owner,
                                nullptr, instance_, this);
    }
    if (!hwnd_) {
        return false;
    }
    // 预览里的窗口淡入淡出是 CSS opacity：整窗（含 DWM 阴影）一起变透明。
    // 分层窗口 + LWA_ALPHA 是 Win32 里唯一能做到这一点的做法；不透明度 255 时
    // 像素与改动前完全一致，只在 170ms / 120ms 的过渡里降到 255 以下。
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    search_edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
                                                    ES_AUTOHSCROLL | ES_LEFT,
                                   0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchEditId)),
                                   instance_, nullptr);
    if (search_edit_) {
                auto font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessageW(search_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(search_edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
        const WPARAM show_when_focused = PopupNativeSearchCueBannerShowsWhenFocused() ? TRUE : FALSE;
                SendMessageW(search_edit_, EM_SETCUEBANNER, show_when_focused,
                     reinterpret_cast<LPARAM>(L"\u641c\u7d22\u5386\u53f2\u8bb0\u5f55"));
        SetWindowSubclass(search_edit_, SearchEditSubclassProc, 0,
                          reinterpret_cast<DWORD_PTR>(this));
        UpdateSearchEditBounds();
    }
    ApplyBackdrop();
    ApplyWindowRegion();
    SetTimer(hwnd_,kIdleMemoryTimer,750,nullptr);
    return true;
}

void PopupWindow::Show(HWND target) {
    KillTimer(hwnd_,kIdleMemoryTimer);
    SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, 0);
    paste_target_ = target;
    moving_window_ = false;
    resizing_window_ = false;
    resize_edges_ = 0;
    mouse_down_started_inside_popup_ = false;
    suppress_inactive_hide_ = false;
    suppress_inactive_hide_until_ = 0;
    left_button_was_down_ = false;
    KillTimer(hwnd_, kSuppressInactiveHideTimer);
    KillTimer(hwnd_, kShellTopmostRaiseTimer);
    KillTimer(hwnd_, kHoverTimer);
    hover_progress_ = 1.0f;
    hover_action_ = UiAction::None;
    hover_item_index_ = -1;
    pin_motion_progress_ = 1.0f;
    pin_motion_from_ = pinned_open_;
    pin_motion_to_ = pinned_open_;
    const std::wstring target_class = WindowClassName(target);
    const std::wstring target_title = WindowTitle(target);
    const std::wstring target_process = WindowProcessName(target);
    shell_topmost_raise_ = PopupTargetNeedsShellTopmostRaise(target_class, target_title, target_process);
    shell_topmost_raise_until_ = shell_topmost_raise_ ? GetTickCount() + kShellTopmostRaiseMs : 0;
    if (shell_topmost_raise_ && target && IsWindow(target)) {
        SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(target));
    }
    ReloadItems();
    // 每次呼出都从「最近」分组的第一条开始高亮：置顶区只是快捷区，不参与默认选中，
    // 也不恢复上一次停在哪条旧记录上（那会让回车粘贴到一条很旧的记录）。
    selected_index_ = ClampPopupSelectedIndex(static_cast<int>(items_.size()),
                                              PopupDefaultSelectedIndex(CurrentSectionPlan()));
    ClampScrollToCurrentPopupHeight();
    RefreshFavoriteGroupCounts();
    UpdatePopupLogicalSize();
    UpdateBehaviorFromSettings();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    HMONITOR target_monitor = MonitorForPopupTarget(target, hwnd_);
    const RECT monitor_rect = MonitorRectForMonitor(target_monitor, work);
    work = WorkAreaForMonitor(target_monitor, work);
    const UINT dpi = DpiForMonitorHandle(target_monitor, CurrentDpi());
    const SIZE size = PhysicalPopupSize(dpi, static_cast<int>(items_.size()));
    const bool activate_on_show = PopupShowShouldActivate(keyboard_invocation_, shell_topmost_raise_);
    const bool use_no_activate = PopupWindowShouldUseNoActivateStyle(activate_on_show);
    const LONG_PTR current_ex_style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    const bool has_no_activate = (current_ex_style & WS_EX_NOACTIVATE) != 0;
    bool no_activate_style_changed = false;
    if (has_no_activate != use_no_activate) {
        LONG_PTR next_ex_style = current_ex_style;
        if (use_no_activate) {
            next_ex_style |= WS_EX_NOACTIVATE;
        } else {
            next_ex_style &= ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
        }
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, next_ex_style);
        no_activate_style_changed = true;
    }
    UINT flags = SWP_SHOWWINDOW;
    if (PopupSetWindowPosShouldUseNoActivate(activate_on_show)) {
        flags |= SWP_NOACTIVATE;
    }
    if (no_activate_style_changed) {
        flags |= SWP_FRAMECHANGED;
    }
    int x = 0;
    int y = 0;
    const POINT position = ResolvePopupPosition(target, size, monitor_rect, work, dpi);
    x = position.x;
    y = position.y;
    // 预览 winIn 的 from 是 opacity:0。必须在窗口可见之前就把 alpha 降到 0，
    // 否则 SWP_SHOWWINDOW 到首个 WM_PAINT/WM_TIMER 之间会闪一下完全不透明的窗口。
    window_entering_ = animations_enabled_;
    window_leaving_ = false;
    window_motion_progress_ = animations_enabled_ ? 0.0f : 1.0f;
    ApplyWindowAlpha();
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, size.cx, size.cy, flags);
    ApplyWindowRegion();
    UpdatePopupLogicalSize();
    scroll_offset_ = ScrollOffsetToRevealSelection(scroll_offset_, selected_index_);
    RECT shown_rect{};
    GetWindowRect(hwnd_, &shown_rect);
    const LONG_PTR shown_ex_style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (DebugEnabled()) {
    std::wstring show_log = L"popup show shell_surface=";
    show_log += shell_topmost_raise_ ? L"1" : L"0";
    show_log += L" activate=";
    show_log += activate_on_show ? L"1" : L"0";
    show_log += L" no_activate=";
    show_log += use_no_activate ? L"1" : L"0";
    show_log += L" visible=";
    show_log += IsWindowVisible(hwnd_) ? L"1" : L"0";
    show_log += L" ex=";
    show_log += std::to_wstring(shown_ex_style);
    show_log += L" rect=";
    show_log += RectToLogString(shown_rect);
    show_log += L" class=";
    show_log += target_class;
    show_log += L" process=";
    show_log += target_process;
    DebugLog(show_log);
    }
    if (shell_topmost_raise_) {
        SetTimer(hwnd_, kShellTopmostRaiseTimer, 50, nullptr);
    }
    if (activate_on_show) {
        ActivatePopupWindow(hwnd_);
        if (DebugEnabled()) {
            DebugLog(L"popup activate foreground=" + ForegroundWindowDebugInfo());
        }
    }
    UpdateSearchEditBounds();
    SyncSearchEdit();
    SetTimer(hwnd_, kOutsideClickTimer, 50, nullptr);
    if (PopupShouldAutoFocusSearchOnShow(search_edit_ != nullptr)) {
        search_focused_ = true;
        search_caret_on_ = true;
        // 预览启动就是 q.focus()，搜索框下的墨线在呼出时从左展开（200ms）。
        // 这里不能只碰 search_focused_：进度必须从 0 推到 1 才有动画。
        search_focus_from_ = 0.0f;
        search_focus_to_ = 1.0f;
        search_focus_start_ms_ = static_cast<double>(GetTickCount64());
        SetFocus(search_edit_);
        SetTimer(hwnd_, kSearchCaretTimer, GetCaretBlinkTime(), nullptr);
    } else {
        search_focused_ = false;
        search_caret_on_ = false;
        search_focus_from_ = 0.0f;
        search_focus_to_ = 0.0f;
        KillTimer(hwnd_, kSearchCaretTimer);
    }
    KillTimer(hwnd_, kAnimationTimer);
    motion_target_ = PopupMotionTarget::None;
    motion_progress_ = 1.0f;
    closing_filter_panel_ = false;
    closing_favorite_group_menu_ = false;
    closing_delete_confirm_ = false;
    closing_delete_confirm_snapshot_ = PendingFavoriteGroupDelete{};
    context_menu_open_ = context_submenu_open_ = closing_context_menu_ = false;
    clear_confirm_open_ = false;
    window_entering_ = rows_entering_ = animations_enabled_;
    window_leaving_ = false;
    window_motion_start_ms_ = rows_entrance_start_ms_ = static_cast<double>(GetTickCount64());
    // 搜索墨线的展开与窗口入场同一时刻起算（预览里 q.focus() 也在启动时立即执行）。
    if (search_focus_to_ > 0.0f) {
        search_focus_start_ms_ = window_motion_start_ms_;
    }
    window_motion_progress_ = animations_enabled_ ? 0.0f : 1.0f;
    // 首帧就要是 opacity:0（预览 winIn 的 from）；已在 SetWindowPos 前设过一次，
    // 这里只为了让起点时间与真正上屏的时刻对齐。
    ApplyWindowAlpha();
    if (animations_enabled_) SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool PopupWindow::IsVisible() const {
    return hwnd_ && IsWindowVisible(hwnd_);
}

std::optional<int64_t> PopupWindow::SelectedItemId() const {
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(items_.size())) {
        return items_[selected_index_].id;
    }
    return hidden_selected_item_id_;
}

void PopupWindow::SetSelectedItemId(std::optional<int64_t> id) {
    if (!id || items_.empty()) {
        return;
    }
    const auto found = std::find_if(items_.begin(), items_.end(), [id](const HistoryItem& item) {
        return item.id == *id;
    });
    if (found == items_.end()) {
        return;
    }
    selected_index_ = static_cast<int>(std::distance(items_.begin(), found));
    scroll_offset_ = ScrollOffsetToRevealSelection(scroll_offset_, selected_index_);
    KickSlipAnimation();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::Hide() {
    Hide(L"unspecified");
}

void PopupWindow::Hide(std::wstring_view reason) {
    if (!completing_window_hide_ && animations_enabled_ && IsVisible() &&
        (reason == L"escape" || reason == L"close-button" || reason == L"paste")) {
        if (!window_leaving_) {
            window_entering_ = false;
            window_leaving_ = true;
            window_motion_start_ms_ = static_cast<double>(GetTickCount64());
            window_motion_progress_ = 0.0f;
            // 退出也从当前帧（opacity 1）开始，下一帧起按 winOut 曲线降。
            ApplyWindowAlpha();
            SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
        }
        return;
    }
    window_entering_ = window_leaving_ = rows_entering_ = false;
    // 下次 Show 会重新从 0 开始淡入；这里复位到 255，避免淡出末帧的 0 被留在窗口上。
    window_motion_progress_ = 1.0f;
    ApplyWindowAlpha();
    context_menu_open_ = context_submenu_open_ = closing_context_menu_ = false;
    clear_confirm_open_ = false;
    row_transitions_.clear();
    detail_transitions_.clear();
    // 隐藏后不应再保留未完成的回顶部滚动，下次 Show 从干净状态开始。
    smooth_scroll_active_ = false;
    smooth_scroll_from_ = smooth_scroll_to_ = 0.0f;
    if (DebugEnabled()) {
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        std::wstring log = L"popup hide reason=";
        log += reason;
        log += L" rect=";
        log += RectToLogString(rect);
        log += L" foreground=";
        log += ForegroundWindowDebugInfo();
        DebugLog(log);
    }
    KillTimer(hwnd_, kOutsideClickTimer);
    KillTimer(hwnd_, kSuppressInactiveHideTimer);
    KillTimer(hwnd_, kShellTopmostRaiseTimer);
    KillTimer(hwnd_, kAnimationTimer);
    KillTimer(hwnd_, kHoverTimer);
    KillTimer(hwnd_, kItemLongPressTimer);
    hover_progress_ = 1.0f;
    context_motion_progress_ = 1.0f;
    badge_transitions_.clear();
    stamp_item_id_.reset();
    toast_text_.clear();
    motion_target_ = PopupMotionTarget::None;
    motion_progress_ = 1.0f;
    closing_filter_panel_ = false;
    closing_favorite_group_menu_ = false;
    closing_delete_confirm_ = false;
    closing_delete_confirm_snapshot_ = PendingFavoriteGroupDelete{};
    CancelItemPress();
    if (moving_window_ || resizing_window_) {
        ReleaseCapture();
    }
    moving_window_ = false;
    resizing_window_ = false;
    resize_edges_ = 0;
    mouse_down_started_inside_popup_ = false;
    suppress_inactive_hide_ = false;
    suppress_inactive_hide_until_ = 0;
    left_button_was_down_ = false;
    shell_topmost_raise_ = false;
    shell_topmost_raise_until_ = 0;
    SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, 0);
    search_focused_ = false;
    search_caret_on_ = false;
    search_selecting_ = false;
    KillTimer(hwnd_, kSearchCaretTimer);
    KillTimer(hwnd_, kSearchReloadTimer);
    KillTimer(hwnd_, kMultiPasteTimer);
    multi_paste_operations_.clear();
    hidden_selected_item_id_ = SelectedItemId();
    ShowWindow(hwnd_, SW_HIDE);
    // The tray listener does not need GPU surfaces or copies of clipboard payloads.
    // Recreate them on the next Show, preserving only the continuous-paste anchor.
    DiscardDeviceResources();
    std::vector<HistoryItem>().swap(items_);
    std::vector<FavoriteGroup>().swap(favorite_groups_);
    favorite_group_counts_.clear();
    detail_height_cache_.clear();
    SetTimer(hwnd_,kIdleMemoryTimer,750,nullptr);
}

void PopupWindow::Refresh() {
    if(!IsVisible()) {
        if(hwnd_)SetTimer(hwnd_,kIdleMemoryTimer,750,nullptr);
        return;
    }
    ReloadItems();
    if (hwnd_) {
        ResizeToCurrentItems();
        SyncSearchEdit();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PopupWindow::RefreshTheme() {
    UpdateThemeFromSettings(true);
    ApplyBackdrop();
    if (search_edit_) {
        InvalidateRect(search_edit_, nullptr, TRUE);
    }
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PopupWindow::ReloadItems() {
    UpdateThemeFromSettings();
    favorite_groups_ = store_.FavoriteGroups();
    if (view_mode_ == ViewMode::Favorites && active_favorite_group_id_) {
        const auto id = *active_favorite_group_id_;
        const auto found = std::find_if(favorite_groups_.begin(), favorite_groups_.end(),
                                        [id](const FavoriteGroup& group) { return group.id == id; });
        if (found == favorite_groups_.end()) {
            active_favorite_group_id_.reset();
        }
    }
    const bool was_populated = !items_.empty();
    std::set<int64_t> previous_ids;
    for (const auto& item : items_) previous_ids.insert(item.id);
    items_ = store_.Query(BuildQuery());
    detail_height_cache_.clear();
    row_metrics_dirty_ = true;
    if (animations_enabled_ && IsVisible() && was_populated && !rows_entering_) {
        const double now = static_cast<double>(GetTickCount64());
        for (const auto& item : items_) {
            if (!previous_ids.contains(item.id)) row_transitions_[item.id] = {now, false};
        }
        if (!row_transitions_.empty()) SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
    }
    HistoryQuery count_query;
    count_query.include_favorites = true;
    history_count_ = store_.Count(count_query);
    // 列表内容变了：保持同一个「行号」。新复制进来的条目排在最上面，所以高亮会一直停在
    // 最新一条上，而不是钉在某条旧记录上、随着新条目插入慢慢往下沉。
    selected_index_ = ClampPopupSelectedIndex(static_cast<int>(items_.size()), selected_index_);
    ClampScrollToCurrentPopupHeight();
    PruneImagePreviewCache();
    // 列表换了：纸条直接落到新位置，不做跨列表飞行。
    slip_y_ = PopupSpringState{SlipTargetTop() < 0.0f ? 0.0f : SlipTargetTop(), 0.0f};
    slip_h_ = PopupSpringState{SlipTargetHeight(), 0.0f};
    slip_last_ms_ = static_cast<double>(GetTickCount64());
}

void PopupWindow::PruneImagePreviewCache() {
    // Decoded preview bitmaps are keyed by file path. Without pruning, every image ever shown
    // (including deleted / rotated-out history items) stays in GPU/system memory until the
    // render target is recreated. Keep only the bitmaps referenced by the current item list.
    if (image_preview_cache_.empty()) {
        return;
    }
    std::unordered_set<std::wstring> live_keys;
    for (const auto& item : items_) {
        if (!item.payload_path.empty()) {
            live_keys.insert(item.payload_path.wstring());
        }
        if (item.kind == ClipboardKind::Files && !item.files.empty()) {
            live_keys.insert(item.files.front());
        }
    }
    for (auto it = image_preview_cache_.begin(); it != image_preview_cache_.end();) {
        if (live_keys.count(it->first) == 0) {
            ReleasePtr(it->second);
            it = image_preview_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void PopupWindow::ResizeToCurrentItems() {
    if (!hwnd_) {
        return;
    }
    UpdatePopupLogicalSize();
    if (manual_popup_size_) {
        ClampScrollToCurrentPopupHeight();
        UpdateSearchEditBounds();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    RECT current{};
    GetWindowRect(hwnd_, &current);
    const SIZE size = PhysicalPopupSize(CurrentDpi(), static_cast<int>(items_.size()));
    SetWindowPos(hwnd_, nullptr, current.left, current.top, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
    UpdateSearchEditBounds();
}

void PopupWindow::SyncSearchEdit() {
    if (!search_edit_) {
        return;
    }
    if (GetFocus() == search_edit_) {
        return;
    }
    const int length = GetWindowTextLengthW(search_edit_);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) {
        GetWindowTextW(search_edit_, value.data(), length + 1);
    }
    value.resize(static_cast<size_t>(length));
    if (value != query_) {
        SetWindowTextW(search_edit_, query_.c_str());
        SendMessageW(search_edit_, EM_SETSEL, query_.size(), query_.size());
    }
    SyncSearchSelectionFromEdit();
}

void PopupWindow::UpdateSearchEditBounds() {
    if (!search_edit_) {
        return;
    }
    const UINT dpi = CurrentDpi();
    const auto search = PopupNativeSearchEditRect(BuildPopupSearchLayoutForWidth(popup_logical_width_));
    const int left = MulDiv(static_cast<int>(std::lround(search.left)), static_cast<int>(dpi), 96);
    const int top = MulDiv(static_cast<int>(std::lround(search.top)), static_cast<int>(dpi), 96);
    const int width = std::max(1, MulDiv(static_cast<int>(std::lround(search.Width())), static_cast<int>(dpi), 96));
    const int height =
        std::max(1, MulDiv(static_cast<int>(std::lround(search.Height())), static_cast<int>(dpi), 96));
    SetWindowPos(search_edit_, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

float PopupWindow::SearchTextOffsetDips(size_t text_index, bool trailing) const {
    if (query_.empty() || !dwrite_factory_ || !small_format_) {
        return 0.0f;
    }

    const auto search_layout = BuildPopupSearchLayoutForWidth(popup_logical_width_);
    IDWriteTextLayout* text_layout = nullptr;
    float offset = 0.0f;
    const size_t clamped_index = std::min(text_index, query_.size());
    // The caret must be measured with the same face/format the text is drawn with (search_format_,
    // 14pt); using small_format_ (11pt) here made the caret, selection and hit-testing drift left.
    IDWriteTextFormat* caret_format = search_format_ ? search_format_ : small_format_;
    if (SUCCEEDED(dwrite_factory_->CreateTextLayout(query_.c_str(), static_cast<UINT32>(query_.size()),
                                                    caret_format, search_layout.text.Width(),
                                                    search_layout.text.Height(), &text_layout)) &&
        text_layout) {
        FLOAT caret_offset = 0.0f;
        FLOAT caret_y = 0.0f;
        DWRITE_HIT_TEST_METRICS hit_metrics{};
        const UINT32 text_position = static_cast<UINT32>(clamped_index);
        if (SUCCEEDED(text_layout->HitTestTextPosition(text_position, trailing ? TRUE : FALSE,
                                                       &caret_offset, &caret_y, &hit_metrics))) {
            offset = caret_offset;
        } else if (text_position > 0 &&
                   SUCCEEDED(text_layout->HitTestTextPosition(text_position - 1, TRUE, &caret_offset, &caret_y,
                                                              &hit_metrics))) {
            offset = caret_offset;
        } else {
            DWRITE_TEXT_METRICS metrics_text{};
            if (SUCCEEDED(text_layout->GetMetrics(&metrics_text))) {
                offset = metrics_text.widthIncludingTrailingWhitespace;
            }
        }
    }
    ReleasePtr(text_layout);
    return offset;
}

float PopupWindow::SearchCaretOffsetDips() const {
    return SearchTextOffsetDips(search_selection_caret_, false);
}

size_t PopupWindow::SearchTextIndexFromPoint(POINT point) const {
    if (query_.empty() || !dwrite_factory_ || !small_format_) {
        return 0;
    }

    const auto search_layout = BuildPopupSearchLayoutForWidth(popup_logical_width_);
    const float x = std::clamp(static_cast<float>(point.x) - search_layout.text.left, 0.0f,
                               std::max(0.0f, search_layout.text.Width()));
    const float y = std::clamp(static_cast<float>(point.y) - search_layout.text.top, 0.0f,
                               std::max(0.0f, search_layout.text.Height()));
    IDWriteTextLayout* text_layout = nullptr;
    size_t index = query_.size();
    IDWriteTextFormat* caret_format = search_format_ ? search_format_ : small_format_;
    if (SUCCEEDED(dwrite_factory_->CreateTextLayout(query_.c_str(), static_cast<UINT32>(query_.size()),
                                                    caret_format, search_layout.text.Width(),
                                                    search_layout.text.Height(), &text_layout)) &&
        text_layout) {
        BOOL trailing_hit = FALSE;
        BOOL inside = FALSE;
        DWRITE_HIT_TEST_METRICS hit_metrics{};
        if (SUCCEEDED(text_layout->HitTestPoint(x, y, &trailing_hit, &inside, &hit_metrics))) {
            index = static_cast<size_t>(hit_metrics.textPosition) + (trailing_hit ? 1u : 0u);
        }
    } else if (search_layout.text.Width() > 0.0f) {
        index = static_cast<size_t>(std::lround((x / search_layout.text.Width()) * query_.size()));
    }
    ReleasePtr(text_layout);
    return std::min(index, query_.size());
}

PopupSearchSelectionRange PopupWindow::CurrentSearchSelection() const {
    return NormalizePopupSearchSelection(search_selection_anchor_, search_selection_caret_, query_.size());
}

void PopupWindow::SyncSearchSelectionFromEdit() {
    if (!search_edit_) {
        search_selection_anchor_ = std::min(search_selection_anchor_, query_.size());
        search_selection_caret_ = std::min(search_selection_caret_, query_.size());
        return;
    }
    DWORD start = 0;
    DWORD end = 0;
    SendMessageW(search_edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    search_selection_anchor_ = std::min<size_t>(start, query_.size());
    search_selection_caret_ = std::min<size_t>(end, query_.size());
}

void PopupWindow::SetSearchSelection(size_t anchor, size_t caret) {
    search_selection_anchor_ = std::min(anchor, query_.size());
    search_selection_caret_ = std::min(caret, query_.size());
    if (search_edit_) {
        const auto selection = CurrentSearchSelection();
        SendMessageW(search_edit_, EM_SETSEL, static_cast<WPARAM>(selection.start),
                     static_cast<LPARAM>(selection.end));
    }
    search_caret_on_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::EndSearchSelectionDrag() {
    if (!search_selecting_) {
        return;
    }
    search_selecting_ = false;
    ReleaseCapture();
    EndTransientHideSuppressionSoon();
    SetTimer(hwnd_, kOutsideClickTimer, 50, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::ReloadSettingsCache() {
    settings_cache_ = store_.LoadSettings();
    settings_cache_valid_ = true;
    history_limit_ = settings_cache_.history_limit;
}

void PopupWindow::UpdateThemeFromSettings(bool force) {
    // ReloadItems() 会在每次输入/过滤时调用到这里，之前每次都整表读 settings 并查一次系统设置，
    // 现在改成用缓存，只有真正保存设置时才重新读取（见 InvalidateSettingsCache 的调用点）。
    if (!settings_cache_valid_) {
        ReloadSettingsCache();
    }
    const auto& settings = settings_cache_;
    const int next_theme = std::clamp(settings.theme_mode, 0, 2);
    const bool resize_changed = popup_resizable_ != settings.popup_resizable;
    popup_resizable_ = settings.popup_resizable;
    paused_ = settings.paused;
    UpdateAnimationPreferences();
    if (!force && next_theme == theme_mode_ && text_brush_) {
        if (resize_changed && !popup_resizable_) {
            manual_popup_size_ = false;
            ResizeToCurrentItems();
        }
        return;
    }
    theme_mode_ = next_theme;
    if (search_edit_brush_) {
        DeleteObject(search_edit_brush_);
        search_edit_brush_ = nullptr;
    }
    if (text_brush_) {
        const auto palette = ResolvePopupThemePalette(theme_mode_, IsSystemDarkTheme());
        text_brush_->SetColor(D2D1::ColorF(palette.text, 0.94f));
        muted_brush_->SetColor(D2D1::ColorF(palette.muted, 0.86f));
        accent_brush_->SetColor(D2D1::ColorF(palette.accent, 0.90f));
    }
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PopupWindow::UpdateBehaviorFromSettings() {
    if (!settings_cache_valid_) {
        ReloadSettingsCache();
    }
    const bool next_resizable = settings_cache_.popup_resizable;
    const bool changed = popup_resizable_ != next_resizable;
    popup_resizable_ = next_resizable;
    if (!popup_resizable_) {
        manual_popup_size_ = false;
        UpdatePopupLogicalSize();
        if (changed && hwnd_ && IsVisible()) {
            ResizeToCurrentItems();
        }
    }
}

void PopupWindow::ResetManualSize() {
    manual_popup_size_ = false;
    UpdatePopupLogicalSize();
    if (hwnd_ && IsVisible()) {
        ResizeToCurrentItems();
    } else {
        UpdateSearchEditBounds();
    }
}


HistoryQuery PopupWindow::BuildQuery() const {
    HistoryQuery query;
    query.limit = std::max(60, history_limit_);
    query.include_favorites = true;
    query.text = query_;
    query.kinds = filter_kinds_;
    query.favorites_only = view_mode_ == ViewMode::Favorites;
    if (query.favorites_only) {
        query.favorite_group_id = active_favorite_group_id_;
    }
    if (date_filter_.start) {
        query.start_unix = LocalDateUnix(*date_filter_.start, false);
    }
    if (date_filter_.end) {
        query.end_unix = LocalDateUnix(*date_filter_.end, true);
    }
    return query;
}

std::wstring PopupWindow::ActiveFavoriteGroupLabel() const {
    if (!active_favorite_group_id_) {
        return L"全部收藏";
    }
    return FavoriteGroupName(active_favorite_group_id_);
}

std::wstring PopupWindow::FavoriteGroupName(std::optional<int64_t> group_id) const {
    if (!group_id) {
        return L"全部收藏";
    }
    const auto id = *group_id;
    const auto found = std::find_if(favorite_groups_.begin(), favorite_groups_.end(),
                                    [id](const FavoriteGroup& group) { return group.id == id; });
    return found == favorite_groups_.end() ? L"\u6536\u85cf\u5939" : found->name;
}

void PopupWindow::MoveSelection(int delta) {
    if (items_.empty()) return;
    CancelSmoothScroll();
    selected_index_ = ClampPopupSelectedIndex(static_cast<int>(items_.size()), selected_index_ + delta);
    scroll_offset_ = ScrollOffsetToRevealSelection(scroll_offset_, selected_index_);
    KickSlipAnimation();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::AdvanceSelectionAfterContinuousPaste() {
    if(!IsVisible())return;
    if (items_.empty()) {
        return;
    }
    selected_index_ =
        PopupContinuousPasteSelectionStep(static_cast<int>(items_.size()), selected_index_).next_selected_index;
    scroll_offset_ = ScrollOffsetToRevealSelection(scroll_offset_, selected_index_);
    KickSlipAnimation();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool PopupWindow::PasteSelectedForContinuousPaste() {
    if (items_.empty()) {
        return false;
    }
    const auto step = PopupContinuousPasteSelectionStep(static_cast<int>(items_.size()), selected_index_);
    selected_index_ = step.paste_index;
    const auto item = items_[step.paste_index];
    auto load_full = [this](int64_t id) { return store_.Get(id); };
    if (!paste_controller_.RestoreToClipboard(item, hwnd_, load_full)) {
        return false;
    }
    BeginTransientHideSuppression();
    paste_controller_.SendPaste(paste_target_, PasteShortcutOptions{false});
    EndTransientHideSuppressionSoon();
    AdvanceSelectionAfterContinuousPaste();
    stamp_item_id_ = item.id;
    stamp_start_ms_ = static_cast<double>(GetTickCount64());
    SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
    if (ShouldHidePopupAfterContinuousPaste(pinned_open_)) {
        Hide(L"continuous-paste");
    }
    return true;
}

void PopupWindow::BeginItemPress(int item_index, POINT point) {
    if (item_index < 0 || item_index >= static_cast<int>(items_.size())) {
        return;
    }
    pressed_item_index_ = item_index;
    long_press_selected_ = false;
    press_point_ = point;
    SetCapture(hwnd_);
    SetTimer(hwnd_, kItemLongPressTimer, PopupItemLongPressMilliseconds(), nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::CancelItemPress() {
    if (pressed_item_index_ < 0) {
        return;
    }
    pressed_item_index_ = -1;
    long_press_selected_ = false;
    press_point_ = POINT{-1, -1};
    KillTimer(hwnd_, kItemLongPressTimer);
    if (!dragging_scrollbar_) {
        ReleaseCapture();
    }
}

void PopupWindow::HandleLongPressTimer() {
    if (pressed_item_index_ < 0 || pressed_item_index_ >= static_cast<int>(items_.size())) {
        CancelItemPress();
        return;
    }
    KillTimer(hwnd_, kItemLongPressTimer);
    selected_index_ = pressed_item_index_;
    scroll_offset_ = ScrollOffsetToRevealSelection(scroll_offset_, selected_index_);
    KickSlipAnimation();
    long_press_selected_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::CompleteItemPress(POINT point) {
    const int pressed = pressed_item_index_;
    if (pressed < 0) {
        return;
    }
    const int released = HitTestItem(point);
    const bool long_press = long_press_selected_;
    InvalidateRect(hwnd_, nullptr, FALSE);
    CancelItemPress();
    switch (PopupItemPressReleaseActionFor(pressed == released, long_press)) {
    case PopupItemPressReleaseAction::Paste:
        selected_index_ = pressed;
        ActivateSelection();
        break;
    case PopupItemPressReleaseAction::SelectOnly:
        selected_index_ = pressed;
        scroll_offset_ = ScrollOffsetToRevealSelection(scroll_offset_, selected_index_);
        KickSlipAnimation();
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
    case PopupItemPressReleaseAction::None:
        break;
    }
}

void PopupWindow::CloseFilterPanelWithMotion() {
    if (!filter_open_) {
        return;
    }
    filter_open_ = false;
    closing_filter_panel_ = true;
    hover_filter_target_ = PopupFilterTarget::None;
    hover_filter_date_.reset();
    StartMotion(PopupMotionTarget::FilterPanel, -1);
}

void PopupWindow::CloseFavoriteGroupMenuWithMotion() {
    if (!favorite_group_menu_open_) {
        return;
    }
    favorite_group_menu_open_ = false;
    closing_favorite_group_menu_ = true;
    hover_favorite_group_menu_hit_ = {};
    StartMotion(PopupMotionTarget::FavoriteMenu, -1);
}

void PopupWindow::CloseFavoriteGroupDeleteConfirmWithMotion() {
    if (!pending_favorite_group_delete_) {
        return;
    }
    closing_delete_confirm_snapshot_ = *pending_favorite_group_delete_;
    pending_favorite_group_delete_.reset();
    closing_delete_confirm_ = true;
    hover_delete_confirm_target_ = PopupFavoriteGroupDeleteConfirmTarget::None;
    StartMotion(PopupMotionTarget::DeleteConfirm, -1);
}

void PopupWindow::ActivateSelection() {
    if (items_.empty()) return;
    const auto& item = items_[selected_index_];
    if(row_transitions_.contains(item.id) && row_transitions_.at(item.id).removing)return;
    if (multi_select_) {
        selection_.Toggle(item.id);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    auto load_full = [this](int64_t id) { return store_.Get(id); };
    if (paste_controller_.RestoreToClipboard(item, hwnd_, load_full)) {
        stamp_item_id_ = item.id;
        stamp_start_ms_ = static_cast<double>(GetTickCount64());
        SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
        if (ShouldHidePopupAfterPaste(pinned_open_)) {
            Hide(L"paste");
        }
        paste_controller_.SendPaste(paste_target_);
    }
}

void PopupWindow::ToggleMultiSelect() {
    multi_select_ = !multi_select_;
    InvalidateRowMetrics();
    selection_.Clear();
    if (multi_select_) {
        filter_open_ = false;
        favorite_group_menu_open_ = false;
    }
    StartMotion(PopupMotionTarget::MultiSelect, multi_select_ ? 1 : -1);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::ToggleFavorite(int64_t id) {
    if (auto item = store_.Get(id)) {
        if(animations_enabled_)badge_transitions_[id]={static_cast<double>(GetTickCount64()),false,item->is_favorite,!item->is_favorite};
        store_.SetFavorite(id, !item->is_favorite);
        ReloadItems();
        RefreshFavoriteGroupCounts();
        if(animations_enabled_)SetTimer(hwnd_,kAnimationTimer,kAnimationTimerMs,nullptr);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PopupWindow::TogglePinned(int64_t id) {
    auto item = store_.Get(id);
    if (!item) {
        return;
    }
    const bool pinning = !item->is_pinned;
    if (animations_enabled_) {
        badge_transitions_[id] = {static_cast<double>(GetTickCount64()), true, item->is_pinned, pinning};
    }
    // 纸条现在的位置先记下来：重新排版后让它滑过去，而不是瞬移。
    const PopupSpringState slip_before = slip_y_;
    const PopupSpringState slip_height_before = slip_h_;
    store_.SetPinned(id, pinning);
    ReloadItems();
    if (pinning) {
        // 置顶项会移到顶部置顶区，跟着它保持选中（就在视野里）。
        SetSelectedItemId(id);
    } else {
        // 取消置顶后条目回到它原本的时间位置（通常落在列表中部），高亮不能跟着它「沉」下去；
        // 回到最近分组的第一条，与呼出弹窗时的默认项保持一致。
        selected_index_ = ClampPopupSelectedIndex(static_cast<int>(items_.size()),
                                                  PopupDefaultSelectedIndex(CurrentSectionPlan()));
        scroll_offset_ = ScrollOffsetToRevealSelection(scroll_offset_, selected_index_);
    }
    if (animations_enabled_) {
        slip_y_ = slip_before;
        slip_h_ = slip_height_before;
        KickSlipAnimation();
        SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::DeleteItem(int64_t id) {
    BeginRemoval({id});
}

void PopupWindow::MoveItemInCurrentList(int item_index, int delta) {
    const int target_index = item_index + delta;
    if (item_index < 0 || item_index >= static_cast<int>(items_.size()) || target_index < 0 ||
        target_index >= static_cast<int>(items_.size())) {
        return;
    }

    const auto moved_id = items_[item_index].id;
    const auto neighbor_id = items_[target_index].id;
    if (store_.SwapSortOrder(moved_id, neighbor_id)) {
        ReloadItems();
        SetSelectedItemId(moved_id);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PopupWindow::SelectAllVisible() {
    if (items_.empty()) {
        return;
    }
    bool all_selected = true;
    for (const auto& item : items_) {
        if (!selection_.IsSelected(item.id)) {
            all_selected = false;
            break;
        }
    }
    if (all_selected) {
        selection_.Clear();
    } else {
        for (const auto& item : items_) {
            if (!selection_.IsSelected(item.id)) {
                selection_.Toggle(item.id);
            }
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::PasteSelected() {
    std::vector<HistoryItem> selected_items;
    selected_items.reserve(items_.size());
    for (const auto& item : items_) {
        if (selection_.IsSelected(item.id)) {
            selected_items.push_back(item);
        }
    }
    if (selected_items.empty()) {
        return;
    }

    StartMultiPaste(BuildMultiPasteOperations(selected_items));
}

void PopupWindow::StartMultiPaste(std::vector<MultiPasteOperation> operations) {
    multi_paste_operations_ = std::move(operations);
    selection_.Clear();
    multi_select_ = false;
    AdvanceMultiPaste();
}

void PopupWindow::AdvanceMultiPaste() {
    // 每段之间用定时器等待，而不是 Sleep() 把整个消息泵冻住（粘贴长列表时 UI 完全无响应）。
    while (!multi_paste_operations_.empty()) {
        auto operation = std::move(multi_paste_operations_.front());
        multi_paste_operations_.erase(multi_paste_operations_.begin());
        if (!paste_controller_.RestoreMultipleToClipboard(operation.items, hwnd_)) {
            continue;
        }
        paste_controller_.SendPaste(paste_target_);
        if (!multi_paste_operations_.empty()) {
            SetTimer(hwnd_, kMultiPasteTimer, kMultiPasteDelayMs, nullptr);
            return;
        }
    }
    if (ShouldHidePopupAfterPaste(pinned_open_)) {
        Hide(L"multi-paste");
    } else {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PopupWindow::PromptCreateFavoriteGroup() {
    prompt_open_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE); UpdateWindow(hwnd_);
    BeginTransientHideSuppression();
    if (const auto name = PromptText(hwnd_, instance_, L"\u65b0\u5efa\u6536\u85cf\u5939", L"\u8f93\u5165\u5206\u7ec4\u540d\u79f0", L"", theme_mode_)) {
        active_favorite_group_id_ = store_.EnsureFavoriteGroup(*name);
        view_mode_ = ViewMode::Favorites;
        filter_open_ = false;
        favorite_group_menu_open_ = false;
        multi_select_ = false;
        selection_.Clear();
        scroll_offset_ = 0;
        ReloadItems();
        ResizeToCurrentItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    prompt_open_ = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
    EndTransientHideSuppressionSoon();
    SetTimer(hwnd_, kOutsideClickTimer, 50, nullptr);
}

void PopupWindow::PromptEditNote(int64_t id) {
    const auto item = store_.Get(id);
    if (!item) {
        return;
    }
    prompt_open_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE); UpdateWindow(hwnd_);
    BeginTransientHideSuppression();
    if (const auto note = PromptText(hwnd_, instance_, L"编辑备注", L"备注会显示在内容下方，也可以被搜索到", item->note, theme_mode_)) {
        store_.SetNote(id, *note);
        ReloadItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    prompt_open_ = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
    EndTransientHideSuppressionSoon();
}

void PopupWindow::PromptAddFavoritePhrase() {
    prompt_open_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE); UpdateWindow(hwnd_);
    BeginTransientHideSuppression();
    if (const auto phrase = PromptFavoritePhrase(hwnd_, instance_, theme_mode_)) {
        if (store_.AddFavoritePhrase(phrase->text, phrase->note, active_favorite_group_id_)) {
            view_mode_ = ViewMode::Favorites;
            filter_open_ = false;
            favorite_group_menu_open_ = false;
            multi_select_ = false;
            selection_.Clear();
            ReloadItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    prompt_open_ = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
    EndTransientHideSuppressionSoon();
}

void PopupWindow::ToggleFavoriteGroupMenu() {
    if (!favorite_group_menu_open_) {
        RefreshFavoriteGroupCounts();
    }
    if (favorite_group_menu_open_) {
        CloseFavoriteGroupMenuWithMotion();
    } else {
        favorite_group_menu_open_ = true;
        closing_favorite_group_menu_ = false;
        filter_open_ = false;
        closing_filter_panel_ = false;
        hover_filter_target_ = PopupFilterTarget::None;
        hover_filter_date_.reset();
        StartMotion(PopupMotionTarget::FavoriteMenu);
    }
    hover_progress_ = 0.0f;
    SetTimer(hwnd_, kHoverTimer, 16, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::ToggleExpanded(int64_t id) {
    const double now = static_cast<double>(GetTickCount64());
    const auto previous = expanded_item_id_;
    float old_height = 0.0f;
    for (const auto& item : items_) {
        if (item.id == id) old_height = AnimatedExpandedHeight(item);
        if (previous && item.id == *previous && item.id != id && animations_enabled_)
            detail_transitions_[item.id] = {AnimatedExpandedHeight(item), 0.0f, now};
    }
    if (expanded_item_id_ && *expanded_item_id_ == id) {
        expanded_item_id_.reset();
    } else {
        expanded_item_id_ = id;
    }
    if (animations_enabled_) {
        for (const auto& item : items_) if (item.id == id) {
            detail_transitions_[id] = {old_height, expanded_item_id_ && *expanded_item_id_ == id
                ? ExpandedExtraHeightForItem(item) : 0.0f, now};
        }
    }
    detail_scroll_=0.0f;
    InvalidateRowMetrics();
    arrow_motion_item_id_ = id;
    arrow_motion_progress_ = animations_enabled_ ? 0.0f : 1.0f;
    arrow_motion_start_ms_ = static_cast<double>(GetTickCount64());
    if (animations_enabled_) {
        SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
    }
    ResizeToCurrentItems();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

PopupFilterLayout PopupWindow::CurrentFilterLayout() const {
    const bool custom = filter_preset_ == kFilterPresetCustom;
    const int rows=PopupCalendarRowCount(calendar_year_,calendar_month_);
    auto layout=BuildPopupFilterLayout(custom,rows,popup_logical_width_);
    if(animations_enabled_ && calendar_motion_from_!=calendar_motion_to_) {
        const float p=PopupMotionProgress(PopupTimedMotionProgress(static_cast<double>(GetTickCount64())-calendar_motion_start_ms_,240.0,true));
        if(p<1.0f) {
            const auto compact=BuildPopupFilterLayout(false,rows,popup_logical_width_);
            const auto full=BuildPopupFilterLayout(true,rows,popup_logical_width_);
            const float fraction=calendar_motion_from_+(calendar_motion_to_-calendar_motion_from_)*p;
            const float delta=(full.panel.bottom-compact.panel.bottom)*fraction;
            const float current_delta=layout.panel.bottom-compact.panel.bottom;
            const float shift=delta-current_delta;
            for(auto* rect:{&layout.footer_divider,&layout.done,&layout.count_text}){rect->top+=shift;rect->bottom+=shift;}
            layout.panel.bottom+=shift;
            if(!custom) {
                layout.calendar=full.calendar;layout.calendar_grid=full.calendar_grid;layout.calendar_title=full.calendar_title;
                layout.calendar_prev=full.calendar_prev;layout.calendar_prev_year=full.calendar_prev_year;
                layout.calendar_next=full.calendar_next;layout.calendar_next_year=full.calendar_next_year;layout.custom_range=true;
            }
        }
    }
    return layout;
}

void PopupWindow::RefreshFavoriteGroupCounts() {
    favorite_group_counts_ = store_.FavoriteGroupCounts();
    HistoryQuery query;
    query.favorites_only = true;
    favorite_group_counts_total_ = store_.Count(query);
}

bool PopupWindow::HasActiveFilter() const {
    return !filter_kinds_.empty() || date_filter_.start.has_value() || date_filter_.end.has_value() ||
           filter_preset_ != 0;
}

int PopupWindow::FavoriteGroupItemCount(int64_t group_id) const {
    const auto found = favorite_group_counts_.find(group_id);
    return found == favorite_group_counts_.end() ? 0 : found->second;
}

void PopupWindow::ApplyFilterPreset(int preset_index) {
    const int clamped = std::clamp(preset_index, 0, kFilterPresetCustom);
    const float current=filter_preset_==kFilterPresetCustom?1.0f:0.0f;
    if((clamped==kFilterPresetCustom)!=(filter_preset_==kFilterPresetCustom)){
        calendar_motion_from_=current;calendar_motion_to_=clamped==kFilterPresetCustom?1.0f:0.0f;
        calendar_motion_start_ms_=static_cast<double>(GetTickCount64());
        if(animations_enabled_)SetTimer(hwnd_,kAnimationTimer,kAnimationTimerMs,nullptr);
    }
    filter_preset_ = clamped;
    if (clamped != kFilterPresetCustom) {
        const auto range = PopupPresetDateRangeFor(clamped, TodayDate());
        date_filter_.start = range.start;
        date_filter_.end = range.end;
        date_filter_.active_field = PopupDateRangeField::Start;
    } else if (date_filter_.start) {
        calendar_year_ = date_filter_.start->year;
        calendar_month_ = date_filter_.start->month;
    }
    scroll_offset_ = 0;
    ReloadItems();
    ResizeToCurrentItems();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

UINT PopupWindow::CurrentDpi() const {
    return DpiForWindowHandle(hwnd_);
}

SIZE PopupWindow::PhysicalPopupSize(UINT dpi, int visible_items) const {
    if (manual_popup_size_) {
        return SIZE{ScalePopupMetricForDpi(popup_logical_width_, dpi), ScalePopupMetricForDpi(popup_logical_height_, dpi)};
    }
    const auto& metrics = PopupMetrics();
    const int height = std::max(PopupHeightForVisibleItems(visible_items), DesiredPopupLogicalHeight());
    return SIZE{ScalePopupMetricForDpi(metrics.width, dpi), ScalePopupMetricForDpi(height, dpi)};
}

POINT PopupWindow::ResolvePopupPosition(HWND target, SIZE size, RECT monitor, RECT work, UINT dpi) const {
    const std::wstring target_class = WindowClassName(target);
    const std::wstring target_title = WindowTitle(target);
    const std::wstring target_process = WindowProcessName(target);
    if (DebugEnabled()) {
        std::wstring log = L"popup position target=";
        log += HexWindow(target);
        log += L" class=";
        log += target_class;
        log += L" process=";
        log += target_process;
        log += L" keyboard=";
        log += keyboard_invocation_ ? L"1" : L"0";
        DebugLog(log);
    }
    RECT avoid{};
    TextAvoidSource avoid_source = TextAvoidSource::None;
    const auto text_position = [&](const wchar_t* path, TextAvoidSource source, RECT resolved_avoid) {
        HMONITOR avoid_monitor = MonitorFromRect(&resolved_avoid, MONITOR_DEFAULTTONEAREST);
        const RECT avoid_work = WorkAreaForMonitor(avoid_monitor, work);
        const RECT avoid_monitor_rect = MonitorRectForMonitor(avoid_monitor, monitor);
        const UINT avoid_dpi = DpiForMonitorHandle(avoid_monitor, dpi);
        const POINT position =
            PopupKeyboardInvocationPosition(true, resolved_avoid, size, avoid_monitor_rect, avoid_work, avoid_dpi);
        if (!DebugEnabled()) {
            return position;
        }
        std::wstring position_log = L"popup position: ";
        position_log += path;
        position_log += L" source=";
        position_log += TextAvoidSourceName(source);
        position_log += L" avoid=(";
        position_log += std::to_wstring(resolved_avoid.left);
        position_log += L",";
        position_log += std::to_wstring(resolved_avoid.top);
        position_log += L",";
        position_log += std::to_wstring(resolved_avoid.right);
        position_log += L",";
        position_log += std::to_wstring(resolved_avoid.bottom);
        position_log += L") pos=(";
        position_log += std::to_wstring(position.x);
        position_log += L",";
        position_log += std::to_wstring(position.y);
        position_log += L") work=(";
        position_log += std::to_wstring(avoid_work.left);
        position_log += L",";
        position_log += std::to_wstring(avoid_work.top);
        position_log += L",";
        position_log += std::to_wstring(avoid_work.right);
        position_log += L",";
        position_log += std::to_wstring(avoid_work.bottom);
        position_log += L")";
        DebugLog(position_log);
        return position;
    };
    const FocusedTextContext focused_text = GetFocusedTextContext();
    const auto trusted_text_position = [&](const wchar_t* path, TextAvoidSource source, RECT resolved_avoid,
                                           bool require_focused_text_gate) -> std::optional<POINT> {
        if (require_focused_text_gate && !FocusedTextContextAllowsCaret(focused_text, resolved_avoid, dpi)) {
            std::wstring reject_log = L"popup position: caret rejected source=";
            reject_log += TextAvoidSourceName(source);
            reject_log += focused_text.editable ? L" reason=stale-caret" : L" reason=focused element is not editable text";
            reject_log += L" avoid=";
            reject_log += RectToLogString(resolved_avoid);
            DebugLog(reject_log);
            return std::nullopt;
        }
        if (PopupShouldUseTextAvoidForTarget(true, target_class, target_title)) {
            return text_position(path, source, resolved_avoid);
        }
        return std::nullopt;
    };
    if (TryGetCaretAnchor(target, avoid, avoid_source)) {
        if (const auto position = trusted_text_position(L"caret avoid", avoid_source, avoid, true)) {
            return *position;
        }
    }
    if (TryGetMsaaCaretAnchor(target, avoid, avoid_source)) {
        if (const auto position = trusted_text_position(L"msaa caret avoid", avoid_source, avoid, true)) {
            return *position;
        }
    }
    if (PopupTargetCanUseConsoleAnchor(target_class) &&
        TryGetConsoleAnchor(target, avoid, avoid_source) &&
        PopupShouldUseTextAvoidForTarget(true, target_class, target_title)) {
        return text_position(L"console avoid", avoid_source, avoid);
    }
    std::wstring java_access_bridge_status;
    if (TryGetJavaAccessBridgeCaretAnchor(target, avoid, avoid_source, java_access_bridge_status) &&
        PopupShouldUseTextAvoidForTarget(true, target_class, target_title)) {
        return text_position(L"java access bridge avoid", avoid_source, avoid);
    }
    if (!java_access_bridge_status.empty() &&
        java_access_bridge_status != L"java-access-bridge-not-java-window") {
        DebugLog(L"popup position: " + java_access_bridge_status);
    }
    std::wstring visual_caret_status;
    if (TryGetVisualTerminalCaretAnchor(target, avoid, avoid_source, visual_caret_status) &&
        PopupShouldUseTextAvoidForTarget(true, target_class, target_title)) {
        return text_position(L"visual terminal caret avoid", avoid_source, avoid);
    }
    if (!visual_caret_status.empty() &&
        visual_caret_status != L"visual-terminal-caret-not-java-window") {
        DebugLog(L"popup position: " + visual_caret_status);
    }
    if (TryGetAutomationCaretAvoidRect(dpi, avoid, avoid_source) &&
        PopupShouldUseTextAvoidForTarget(true, target_class, target_title)) {
        return text_position(L"automation focus avoid", avoid_source, avoid);
    }
    if (TryGetAutomationAvoidRectFromWindow(target, dpi, avoid, avoid_source) &&
        PopupShouldUseTextAvoidForTarget(true, target_class, target_title)) {
        return text_position(L"automation window avoid", avoid_source, avoid);
    }
    if (DebugEnabled() && PopupTargetNeedsShellTopmostRaise(target_class, target_title, target_process)) {
        // UIA tree dump is expensive; only do it when logging.
        DebugLog(L"popup position: automation unavailable " + AutomationFocusedElementDebugInfo());
    }
    const POINT fallback = keyboard_invocation_ ? PopupKeyboardInvocationPosition(false, RECT{}, size, monitor, work, dpi)
                                                : PopupBottomRightFallback(size, work, dpi);
    std::wstring fallback_log =
        keyboard_invocation_ ? L"popup position: windows clipboard dock fallback pos=("
                             : L"popup position: bottom right fallback pos=(";
    fallback_log += std::to_wstring(fallback.x);
    fallback_log += L",";
    fallback_log += std::to_wstring(fallback.y);
    fallback_log += L")";
    DebugLog(fallback_log);
    return fallback;
}

void PopupWindow::HideIfInactive(HWND next_active) {
    const bool left_button_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool pointer_interaction = PopupPointerInteractionSuppressesInactiveHide(
        moving_window_, resizing_window_, mouse_down_started_inside_popup_, left_button_was_down_);
    if (left_button_down && pointer_interaction) {
        left_button_was_down_ = true;
    }
    if (ShouldHidePopupAfterInactive(pinned_open_, prompt_open_, pointer_interaction,
                                     IsTransientHideSuppressed(), IsVisible(),
                                     ContainsWindow(hwnd_, next_active), shell_topmost_raise_)) {
        if (DebugEnabled()) {
            DebugLog(L"popup inactive hide next=" + HexWindow(next_active) + L" next_class=" +
                     WindowClassName(next_active) + L" next_process=" + WindowProcessName(next_active));
        }
        Hide(L"inactive");
    }
}

void PopupWindow::RaiseAboveShellSurface() {
    if (!shell_topmost_raise_ || !IsVisible()) {
        KillTimer(hwnd_, kShellTopmostRaiseTimer);
        return;
    }
    const bool activate_on_raise = PopupShowShouldActivate(keyboard_invocation_, shell_topmost_raise_);
    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW;
    if (PopupSetWindowPosShouldUseNoActivate(activate_on_raise)) {
        flags |= SWP_NOACTIVATE;
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, flags);
    RECT popup_rect{};
    GetWindowRect(hwnd_, &popup_rect);
    const LONG_PTR ex_style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    std::wstring log = L"popup shell raise rect=";
    log += RectToLogString(popup_rect);
    log += L" visible=";
    log += IsWindowVisible(hwnd_) ? L"1" : L"0";
    log += L" topmost=";
    log += (ex_style & WS_EX_TOPMOST) ? L"1" : L"0";
    DebugLog(log);
    if (activate_on_raise) {
        ActivatePopupWindow(hwnd_);
    }
    const LONG remaining_ms = static_cast<LONG>(shell_topmost_raise_until_ - GetTickCount());
    if (PopupShellTopmostRaiseShouldExpire(shell_topmost_raise_, remaining_ms)) {
        shell_topmost_raise_ = false;
        shell_topmost_raise_until_ = 0;
        KillTimer(hwnd_, kShellTopmostRaiseTimer);
    }
}

void PopupWindow::BeginTransientHideSuppression() {
    suppress_inactive_hide_ = true;
    suppress_inactive_hide_until_ = GetTickCount() + kTransientHideSuppressMs;
    SetTimer(hwnd_, kSuppressInactiveHideTimer, 50, nullptr);
}

void PopupWindow::EndTransientHideSuppressionSoon() {
    suppress_inactive_hide_ = true;
    suppress_inactive_hide_until_ = GetTickCount() + kTransientHideSuppressMs;
    SetTimer(hwnd_, kSuppressInactiveHideTimer, 50, nullptr);
}

bool PopupWindow::IsTransientHideSuppressed() const {
    if (!suppress_inactive_hide_) {
        return false;
    }
    return static_cast<LONG>(suppress_inactive_hide_until_ - GetTickCount()) > 0;
}

void PopupWindow::UpdatePopupLogicalSize() {
    RECT client{};
    const auto& metrics = PopupMetrics();
    if (!hwnd_ || !GetClientRect(hwnd_, &client)) {
        popup_logical_width_ = metrics.width;
        popup_logical_height_ = metrics.height;
        return;
    }
    const UINT dpi = CurrentDpi();
    popup_logical_width_ = std::clamp(MulDiv(client.right - client.left, 96, static_cast<int>(dpi)),
                                      kMinPopupWidth, kMaxPopupWidth);
    popup_logical_height_ = std::clamp(MulDiv(client.bottom - client.top, 96, static_cast<int>(dpi)),
                                       kMinPopupHeight, kMaxPopupHeight);
    if (!PopupShouldUseCurrentWindowWidthForLayout(popup_resizable_, manual_popup_size_,
                                                   popup_logical_width_, metrics.width)) {
        popup_logical_width_ = metrics.width;
        popup_logical_height_ = metrics.height;
    }
}

void PopupWindow::ClampScrollToCurrentPopupHeight() {
    scroll_offset_ = ClampSmoothScrollOffset(scroll_offset_);
}

int PopupWindow::DesiredPopupLogicalHeight() const {
    const auto& metrics = PopupMetrics();
    int desired = metrics.height;
    if (expanded_item_id_) {
        const auto found = std::find_if(items_.begin(), items_.end(), [&](const HistoryItem& item) {
            return item.id == *expanded_item_id_;
        });
        if (found != items_.end()) {
            // 页脚也要算进去，否则展开长备注会被页脚盖住。
            desired = static_cast<int>(std::ceil(PopupListTop() + metrics.card_height +
                                                 ExpandedExtraHeightForItem(*found) + metrics.card_gap +
                                                 PopupFooterHeight() + 8.0f));
        }
    }
    return std::clamp(desired, kMinPopupHeight, kMaxPopupHeight);
}

float PopupWindow::ExpandedExtraHeightForItem(const HistoryItem& item) const {
    const auto& metrics = PopupMetrics();
    const float detail_width = std::max(40.0f, static_cast<float>(popup_logical_width_ - 140 - (multi_select_ ? 28 : 0)));
    if (item.kind == ClipboardKind::Image && !item.payload_path.empty()) return 128.0f;
    if(const auto cached=detail_height_cache_.find(item.id);cached!=detail_height_cache_.end() &&
        cached->second.first==detail_width)return cached->second.second;
    const float measured_height=MeasureDetailTextHeight(ItemDetailText(item),detail_width);
    const float height=8.0f+std::min(150.0f,std::max(18.0f,measured_height)+16.0f);
    if(detail_format_)detail_height_cache_[item.id]={detail_width,height};
    return height;
}

float PopupWindow::MeasureDetailTextHeight(std::wstring_view text, float width) const {
    if (!dwrite_factory_ || !detail_format_ || text.empty() || width <= 0.0f) {
        return 0.0f;
    }

    IDWriteTextLayout* layout = nullptr;
    constexpr float kMaxLayoutHeight = 8192.0f;
    const HRESULT hr =
        dwrite_factory_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), detail_format_, width,
                                          kMaxLayoutHeight, &layout);
    if (FAILED(hr) || !layout) {
        return 0.0f;
    }

    DWRITE_TEXT_METRICS metrics_text{};
    float height = 0.0f;
    if (SUCCEEDED(layout->GetMetrics(&metrics_text))) {
        height = metrics_text.height;
    }
    layout->Release();
    return height;
}

float PopupWindow::ItemScrollTop(int item_index) const {
    return ItemRowTop(item_index);
}

PopupSectionPlan PopupWindow::CurrentSectionPlan() const {
    size_t pinned = 0;
    for (const auto& item : items_) {
        if (item.is_pinned) {
            ++pinned;
        } else {
            break;
        }
    }
    return PopupSectionPlanFor(items_.size(), pinned);
}

bool PopupWindow::RowMetricsAreAnimating() const {
    // 展开/收起和删除过渡会连续改变行高，缓存在这段时间里每帧都无效。
    return !detail_transitions_.empty() || !row_transitions_.empty();
}

void PopupWindow::RebuildRowMetrics() {
    // 一次 O(n) 前缀和，之后绘制/命中/滚动都按索引 O(1) 取值。
    // 之前 ItemRowTop() 每次都从 0 累加到 index，被行循环调用后整个绘制是 O(n^2)。
    const auto& metrics = PopupMetrics();
    const size_t count = items_.size();
    const auto plan = CurrentSectionPlan();
    const float header = PopupSectionHeaderHeight();
    const float gap = static_cast<float>(metrics.card_gap);

    row_heights_.resize(count);
    row_tops_.resize(count);
    float top = plan.show_pinned_header ? header : 0.0f;
    for (size_t index = 0; index < count; ++index) {
        if (plan.show_recent_header && index == plan.pinned_count) {
            top += header;
        }
        row_tops_[index] = top;
        const float height = ItemScrollHeight(static_cast<int>(index));
        row_heights_[index] = height;
        top += height;
        if (index + 1 < count) {
            top += gap;
        }
    }
    total_scroll_height_ = count == 0 ? 0.0f : top + 12.0f;
    row_metrics_dirty_ = false;
}

float PopupWindow::RowTopForIndex(int item_index) const {
    if (row_metrics_dirty_ || RowMetricsAreAnimating()) {
        const_cast<PopupWindow*>(this)->RebuildRowMetrics();
    }
    if (item_index < 0 || item_index >= static_cast<int>(row_tops_.size())) {
        return 0.0f;
    }
    return row_tops_[static_cast<size_t>(item_index)];
}

float PopupWindow::ItemRowTop(int item_index) const {
    return RowTopForIndex(item_index);
}

void PopupWindow::InvalidateRowMetrics() {
    row_metrics_dirty_ = true;
}

float PopupWindow::ItemScrollHeight(int item_index) const {
    const auto& metrics = PopupMetrics();
    if (item_index < 0 || item_index >= static_cast<int>(items_.size())) {
        return 0.0f;
    }
    const auto& item = items_[item_index];
    const bool expanded = expanded_item_id_ && *expanded_item_id_ == item.id;
    float height = static_cast<float>(metrics.card_height) + AnimatedExpandedHeight(item);
    if (const auto motion = row_transitions_.find(item.id); motion != row_transitions_.end()) {
        const float progress = RowMotionProgress(item.id);
        height *= motion->second.removing ? 1.0f - PopupMotionProgress(progress) : PopupMotionProgress(progress);
    }
    return height;
}

float PopupWindow::TotalScrollHeight() const {
    if (row_metrics_dirty_ || RowMetricsAreAnimating()) {
        const_cast<PopupWindow*>(this)->RebuildRowMetrics();
    }
    return total_scroll_height_;
}

float PopupWindow::ViewportScrollHeight() const {
    return std::max(0.0f, PopupListBottomForHeight(popup_logical_height_) - PopupListTop());
}

float PopupWindow::ClampSmoothScrollOffset(float requested_offset) const {
    const float max_offset = std::max(0.0f, TotalScrollHeight() - ViewportScrollHeight());
    return std::clamp(requested_offset, 0.0f, max_offset);
}

float PopupWindow::ScrollOffsetToRevealSelection(float requested_offset, int selected_index) const {
    if (items_.empty()) {
        return 0.0f;
    }
    const int clamped_selection = ClampPopupSelectedIndex(static_cast<int>(items_.size()), selected_index);
    const float selected_top = ItemScrollTop(clamped_selection);
    const float selected_bottom = selected_top + ItemScrollHeight(clamped_selection);
    return PopupScrollOffsetToRevealRange(requested_offset, selected_top, selected_bottom,
                                          TotalScrollHeight(), ViewportScrollHeight());
}

void PopupWindow::StartSmoothScrollToTop() {
    // 预览：list.scrollTo({top: 0, behavior: 'smooth'})，滚动位上滑而不是瞬跳。
    if (!animations_enabled_) {
        CancelSmoothScroll();
        scroll_offset_ = 0.0f;
        return;
    }
    smooth_scroll_from_ = scroll_offset_;
    smooth_scroll_to_ = 0.0f;
    smooth_scroll_start_ms_ = static_cast<double>(GetTickCount64());
    smooth_scroll_active_ = std::abs(smooth_scroll_from_ - smooth_scroll_to_) > 0.01f;
    if (smooth_scroll_active_) {
        SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
    }
}

void PopupWindow::CancelSmoothScroll() {
    smooth_scroll_active_ = false;
    smooth_scroll_from_ = smooth_scroll_to_ = scroll_offset_;
}

bool PopupWindow::SmoothScrollNeedsAnimation() const {
    return smooth_scroll_active_ && std::abs(scroll_offset_ - smooth_scroll_to_) > 0.01f;
}

void PopupWindow::AdvanceSmoothScroll() {
    if (!smooth_scroll_active_) {
        return;
    }
    // Chromium smooth scroll 约 300ms；超出后吸附到目标位置。
    constexpr double kSmoothScrollMs = 300.0;
    const double elapsed = static_cast<double>(GetTickCount64()) - smooth_scroll_start_ms_;
    const float raw = static_cast<float>(std::clamp(elapsed / kSmoothScrollMs, 0.0, 1.0));
    scroll_offset_ = ClampSmoothScrollOffset(
        PopupSmoothScrollOffset(smooth_scroll_from_, smooth_scroll_to_, raw));
    if (raw >= 1.0f) {
        scroll_offset_ = smooth_scroll_to_;
        smooth_scroll_active_ = false;
    }
}

int PopupWindow::FirstVisibleItemIndex() const {
    if (row_metrics_dirty_ || RowMetricsAreAnimating()) {
        const_cast<PopupWindow*>(this)->RebuildRowMetrics();
    }
    if (items_.empty()) {
        return 0;
    }
    // 前缀和上的二分：滚动到列表尾部时不用再从头遍历。
    return PopupFirstVisibleRowIndex(row_tops_, scroll_offset_);
}

int PopupWindow::ResizeHitTest(POINT point) const {
    if (!popup_resizable_) {
        return 0;
    }
    int edges = 0;
    if (point.x <= kPopupResizeGrip) {
        edges |= kPopupResizeLeft;
    } else if (point.x >= popup_logical_width_ - kPopupResizeGrip) {
        edges |= kPopupResizeRight;
    }
    if (point.y <= kPopupResizeGrip) {
        edges |= kPopupResizeTop;
    } else if (point.y >= popup_logical_height_ - kPopupResizeGrip) {
        edges |= kPopupResizeBottom;
    }
    return edges;
}

HCURSOR PopupWindow::CursorForPoint(POINT point) const {
    if (context_menu_open_) {
        const int row = HitTestContextMenu(point);
        const bool sub = context_submenu_open_ && HitTestContextMenu(point, true) >= 0;
        return LoadCursorW(nullptr, sub || ContextRowEnabled(row) ? IDC_HAND : IDC_ARROW);
    }
    if (clear_confirm_open_) {
        const auto hit = HitTestFavoriteGroupDeleteConfirm(point, popup_logical_width_);
        return LoadCursorW(nullptr, hit == PopupFavoriteGroupDeleteConfirmTarget::Delete || hit == PopupFavoriteGroupDeleteConfirmTarget::Cancel ? IDC_HAND : IDC_ARROW);
    }
    if (const int resize_edges = ResizeHitTest(point)) {
        if ((resize_edges & kPopupResizeLeft && resize_edges & kPopupResizeTop) ||
            (resize_edges & kPopupResizeRight && resize_edges & kPopupResizeBottom)) {
            return LoadCursorW(nullptr, IDC_SIZENWSE);
        }
        if ((resize_edges & kPopupResizeRight && resize_edges & kPopupResizeTop) ||
            (resize_edges & kPopupResizeLeft && resize_edges & kPopupResizeBottom)) {
            return LoadCursorW(nullptr, IDC_SIZENESW);
        }
        if (resize_edges & (kPopupResizeLeft | kPopupResizeRight)) {
            return LoadCursorW(nullptr, IDC_SIZEWE);
        }
        if (resize_edges & (kPopupResizeTop | kPopupResizeBottom)) {
            return LoadCursorW(nullptr, IDC_SIZENS);
        }
    }

    const auto search_layout = BuildPopupSearchLayoutForWidth(popup_logical_width_);
    if (Contains(search_layout.box, point)) {
        return LoadCursorW(nullptr, PopupSearchClearButtonHitTest(search_layout, !query_.empty(), point)
                                        ? IDC_HAND
                                        : IDC_IBEAM);
    }

    if (pending_favorite_group_delete_ || closing_delete_confirm_) {
        const auto target = HitTestFavoriteGroupDeleteConfirm(point, popup_logical_width_);
        if (target == PopupFavoriteGroupDeleteConfirmTarget::Delete ||
            target == PopupFavoriteGroupDeleteConfirmTarget::Cancel) {
            return LoadCursorW(nullptr, IDC_HAND);
        }
        return LoadCursorW(nullptr, IDC_ARROW);
    }

    if (filter_open_) {
        const auto target = HitTestPopupFilterTarget(CurrentFilterLayout(), calendar_year_, calendar_month_,
                                                     static_cast<float>(point.x), static_cast<float>(point.y));
        if (target != PopupFilterTarget::None && target != PopupFilterTarget::Panel) {
            return LoadCursorW(nullptr, IDC_HAND);
        }
    }

    if (favorite_group_menu_open_) {
        const auto hit = HitTestPopupFavoriteGroupMenu(BuildPopupFavoriteGroupMenuLayout(favorite_groups_.size()),
                                                       favorite_groups_.size(), static_cast<float>(point.x),
                                                       static_cast<float>(point.y));
        if (hit.target != PopupFavoriteGroupMenuTarget::None &&
            hit.target != PopupFavoriteGroupMenuTarget::Panel) {
            return LoadCursorW(nullptr, IDC_HAND);
        }
    }

    switch (HitTestAction(point)) {
    case UiAction::None:
    case UiAction::Search:
        return LoadCursorW(nullptr, IDC_ARROW);
    case UiAction::ClearSearch:
    case UiAction::Filter:
    case UiAction::MultiSelect:
    case UiAction::ClearAll:
    case UiAction::SelectAll:
    case UiAction::DeleteSelected:
    case UiAction::PasteSelected:
    case UiAction::HistoryTab:
    case UiAction::FavoritesTab:
    case UiAction::FavoriteGroupMenu:
    case UiAction::AddFavoritePhrase:
    case UiAction::ExpandItem:
    case UiAction::Scrollbar:
    case UiAction::ScrollToTop:
    case UiAction::Pin:
    case UiAction::Close:
        return LoadCursorW(nullptr, IDC_HAND);
    }
    return LoadCursorW(nullptr, IDC_ARROW);
}

void PopupWindow::ApplyCursorForPoint(POINT point) const {
    SetCursor(CursorForPoint(point));
}

void PopupWindow::BeginWindowMove() {
    if (!hwnd_) {
        return;
    }
    moving_window_ = true;
    resizing_window_ = false;
    resize_edges_ = 0;
    mouse_down_started_inside_popup_ = true;
    left_button_was_down_ = true;
    GetCursorPos(&drag_start_screen_);
    GetWindowRect(hwnd_, &drag_start_rect_);
    SetCapture(hwnd_);
    BeginTransientHideSuppression();
    KillTimer(hwnd_, kOutsideClickTimer);
}

void PopupWindow::BeginWindowResize(int edges) {
    if (!hwnd_ || edges == 0) {
        return;
    }
    resizing_window_ = true;
    moving_window_ = false;
    resize_edges_ = edges;
    hover_progress_ = 1.0f;
    mouse_down_started_inside_popup_ = true;
    left_button_was_down_ = true;
    GetCursorPos(&drag_start_screen_);
    GetWindowRect(hwnd_, &drag_start_rect_);
    SetCapture(hwnd_);
    BeginTransientHideSuppression();
    KillTimer(hwnd_, kAnimationTimer);
    KillTimer(hwnd_, kHoverTimer);
    KillTimer(hwnd_, kOutsideClickTimer);
}

void PopupWindow::UpdateWindowMoveOrResize() {
    if (!hwnd_ || (!moving_window_ && !resizing_window_)) {
        return;
    }
    RECT current{};
    GetWindowRect(hwnd_, &current);
    POINT cursor{};
    GetCursorPos(&cursor);
    const int dx = cursor.x - drag_start_screen_.x;
    const int dy = cursor.y - drag_start_screen_.y;
    RECT next = drag_start_rect_;
    if (moving_window_) {
        OffsetRect(&next, dx, dy);
    } else {
        const UINT dpi = CurrentDpi();
        const int min_width = ScalePopupMetricForDpi(kMinPopupWidth, dpi);
        const int min_height = ScalePopupMetricForDpi(kMinPopupHeight, dpi);
        const int max_width = ScalePopupMetricForDpi(kMaxPopupWidth, dpi);
        const int max_height = ScalePopupMetricForDpi(kMaxPopupHeight, dpi);
        if (resize_edges_ & kPopupResizeLeft) {
            next.left = std::clamp(next.left + dx, next.right - max_width, next.right - min_width);
        }
        if (resize_edges_ & kPopupResizeRight) {
            next.right = std::clamp(next.right + dx, next.left + min_width, next.left + max_width);
        }
        if (resize_edges_ & kPopupResizeTop) {
            next.top = std::clamp(next.top + dy, next.bottom - max_height, next.bottom - min_height);
        }
        if (resize_edges_ & kPopupResizeBottom) {
            next.bottom = std::clamp(next.bottom + dy, next.top + min_height, next.top + max_height);
        }
        manual_popup_size_ = true;
    }
    if (PopupShouldApplyWindowRect(current, next)) {
        SetWindowPos(hwnd_, nullptr, next.left, next.top, next.right - next.left, next.bottom - next.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (PopupShouldFlushPaintDuringLiveResize(resizing_window_, true)) {
            UpdateWindow(hwnd_);
        }
    }
}

void PopupWindow::EndWindowMoveOrResize() {
    if (moving_window_ || resizing_window_) {
        ReleaseCapture();
    }
    moving_window_ = false;
    resizing_window_ = false;
    resize_edges_ = 0;
    custom_position_ = true;
    left_button_was_down_ = false;
    mouse_down_started_inside_popup_ = false;
    UpdatePopupLogicalSize();
    ClampScrollToCurrentPopupHeight();
    UpdateSearchEditBounds();
    EndTransientHideSuppressionSoon();
    SetTimer(hwnd_, kOutsideClickTimer, 50, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::UpdateScrollDrag(POINT point) {
    const auto track = PopupScrollbarTrackRectForSize(popup_logical_width_, popup_logical_height_);
    const float content_height = TotalScrollHeight();
    const float viewport_height = ViewportScrollHeight();
    if (content_height <= viewport_height) {
        CancelSmoothScroll();
        scroll_offset_ = 0.0f;
        return;
    }
    const float thumb_height = std::max(32.0f, track.Height() * viewport_height / content_height);
    const float travel = std::max(1.0f, track.Height() - thumb_height);
    const float thumb_top =
        std::clamp(static_cast<float>(point.y) - thumb_height * 0.5f, track.top, track.bottom - thumb_height);
    const float max_offset = std::max(0.0f, content_height - viewport_height);
    const float next_offset = ClampSmoothScrollOffset((thumb_top - track.top) * max_offset / travel);
    if (next_offset != scroll_offset_) {
        CancelSmoothScroll();
        scroll_offset_ = next_offset;
        KickSlipAnimation();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

POINT PopupWindow::ClientPointToDips(POINT point) const {
    const UINT dpi = CurrentDpi();
    return POINT{MulDiv(point.x, 96, static_cast<int>(dpi)),
                 MulDiv(point.y, 96, static_cast<int>(dpi))};
}

void PopupWindow::EnsureDeviceResources() {
    if (!d2d_factory_) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_);
    }
    if (!dwrite_factory_) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(&dwrite_factory_));
        // Ink Ledger 字体：品牌 Georgia 斜体，正文/次要 Segoe UI，数字与时间 Consolas。
        dwrite_factory_->CreateTextFormat(L"Georgia", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                          DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL,
                                          17.0f, L"zh-CN", &brand_format_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          13.0f, L"zh-CN", &label_format_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          13.0f, L"zh-CN", &title_format_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          12.0f, L"zh-CN", &button_format_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          14.0f, L"zh-CN", &search_format_);
        dwrite_factory_->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          10.5f, L"zh-CN", &mono_format_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          13.0f, L"zh-CN", &body_format_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          11.0f, L"zh-CN", &small_format_);
        dwrite_factory_->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,12.5f,L"zh-CN",&code_format_);
        dwrite_factory_->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,7.5f,L"en-US",&extension_format_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          12.0f, L"zh-CN", &detail_format_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          12.0f, L"zh-CN", &centered_small_format_);
        dwrite_factory_->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          11.0f, L"zh-CN", &date_value_format_);
        if (centered_small_format_) {
            centered_small_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            centered_small_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (date_value_format_) {
            date_value_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        for (auto* format : {brand_format_, label_format_, title_format_, button_format_, search_format_,
                             mono_format_, body_format_, small_format_, date_value_format_, code_format_, extension_format_}) {
            if (format) {
                format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }
        DWRITE_TRIMMING trimming{};
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        if (body_format_) {
            dwrite_factory_->CreateEllipsisTrimmingSign(body_format_, &ellipsis_trimming_sign_);
        }
        for (auto* format : {brand_format_, label_format_, title_format_, button_format_, search_format_,
                             mono_format_, body_format_, small_format_, centered_small_format_,
                             date_value_format_, code_format_, extension_format_}) {
            if (format) {
                format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                format->SetTrimming(&trimming, ellipsis_trimming_sign_);
            }
        }
        if (detail_format_) {
            detail_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_CHARACTER);
            detail_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
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
        const float dpi = static_cast<float>(CurrentDpi());
        render_target_->SetDpi(dpi, dpi);
        const auto palette = ResolvePopupThemePalette(theme_mode_, IsSystemDarkTheme());
        render_target_->CreateSolidColorBrush(D2D1::ColorF(palette.text, 0.94f), &text_brush_);
        render_target_->CreateSolidColorBrush(D2D1::ColorF(palette.muted, 0.84f), &muted_brush_);
        render_target_->CreateSolidColorBrush(D2D1::ColorF(palette.accent, 0.90f), &accent_brush_);
        UpdateThemeFromSettings();
    }
}

void PopupWindow::DiscardTextResources() {
    // 字体格式即将重建，之前量好的宽度既失效也会按悬垂的 format 指针命中。
    text_width_cache_.clear();
    ReleasePtr(brand_format_);
    ReleasePtr(label_format_);
    ReleasePtr(button_format_);
    ReleasePtr(mono_format_);
    ReleasePtr(search_format_);
    ReleasePtr(title_format_);
    ReleasePtr(body_format_);
    ReleasePtr(code_format_);
    ReleasePtr(extension_format_);
    ReleasePtr(small_format_);
    ReleasePtr(detail_format_);
    ReleasePtr(centered_small_format_);
    ReleasePtr(date_value_format_);
    ReleasePtr(ellipsis_trimming_sign_);
    ReleasePtr(wic_factory_);
    ReleasePtr(dwrite_factory_);
}

void PopupWindow::DiscardDeviceResources() {
    for (auto& [_, bitmap] : image_preview_cache_) {
        ReleasePtr(bitmap);
    }
    image_preview_cache_.clear();
    ReleasePtr(text_brush_);
    ReleasePtr(muted_brush_);
    ReleasePtr(accent_brush_);
    ReleasePtr(paint_brush_);
    ReleasePtr(ribbon_notch_left_);
    ReleasePtr(ribbon_notch_right_);
    ReleasePtr(render_target_);
}

ID2D1Bitmap* PopupWindow::LoadImagePreviewBitmap(const std::filesystem::path& path) {
    if (path.empty() || !render_target_) {
        return nullptr;
    }
    const std::wstring key = path.wstring();
    if (const auto found = image_preview_cache_.find(key); found != image_preview_cache_.end()) {
        return found->second;
    }

    ID2D1Bitmap* bitmap = nullptr;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        image_preview_cache_[key] = nullptr;
        return nullptr;
    }
    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    BITMAPINFOHEADER bitmap_header{};
    file.read(reinterpret_cast<char*>(&bitmap_header),sizeof(bitmap_header));
    const auto* header=&bitmap_header;
    if (!file || size < static_cast<std::streamoff>(sizeof(BITMAPINFOHEADER)) ||
        header->biSize < sizeof(BITMAPINFOHEADER) || header->biWidth <= 0 || header->biHeight == 0 ||
        header->biHeight == LONG_MIN || header->biPlanes != 1 ||
        (header->biBitCount != 32 && header->biBitCount != 24)) {
        image_preview_cache_[key] = nullptr;
        return nullptr;
    }

    const int width = header->biWidth;
    const int height = std::abs(header->biHeight);
    const bool top_down = header->biHeight < 0;
    const size_t header_size = static_cast<size_t>(header->biSize);
    const size_t row_stride = ((static_cast<size_t>(width) * header->biBitCount + 31u) / 32u) * 4u;
    const size_t pixel_offset = header_size;
    const size_t required = pixel_offset + row_stride * static_cast<size_t>(height);
    if (required > static_cast<size_t>(size)) {
        image_preview_cache_[key] = nullptr;
        return nullptr;
    }

    const SIZE target = PopupPreviewDecodeSize(static_cast<unsigned>(width), static_cast<unsigned>(height),
                                               PopupImagePreviewDecodePixelLimit());
    if (target.cx <= 0 || target.cy <= 0) {
        image_preview_cache_[key] = nullptr;
        return nullptr;
    }

    std::vector<uint32_t> pixels(static_cast<size_t>(target.cx) * static_cast<size_t>(target.cy));
    // Read only sampled scanlines, rather than duplicating a full-resolution DIB.
    std::vector<uint8_t> scanline(row_stride);
    for (int y = 0; y < target.cy; ++y) {
        const int sampled_y = std::min(height - 1, static_cast<int>((static_cast<int64_t>(y) * height) / target.cy));
        const int src_y = top_down ? sampled_y : height - 1 - sampled_y;
        file.seekg(static_cast<std::streamoff>(pixel_offset + row_stride * static_cast<size_t>(src_y)));
        file.read(reinterpret_cast<char*>(scanline.data()),static_cast<std::streamsize>(scanline.size()));
        if(!file){image_preview_cache_[key]=nullptr;return nullptr;}
        const auto* src = scanline.data();
        auto* dst = pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(target.cx);
        for (int x = 0; x < target.cx; ++x) {
            const int sampled_x = std::min(width - 1, static_cast<int>((static_cast<int64_t>(x) * width) / target.cx));
            const uint8_t b = src[sampled_x * (header->biBitCount / 8) + 0];
            const uint8_t g = src[sampled_x * (header->biBitCount / 8) + 1];
            const uint8_t r = src[sampled_x * (header->biBitCount / 8) + 2];
            dst[x] = 0xFF000000u | (static_cast<uint32_t>(r) << 16u) | (static_cast<uint32_t>(g) << 8u) |
                     static_cast<uint32_t>(b);
        }
    }

    const auto props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    render_target_->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(target.cx), static_cast<UINT32>(target.cy)),
                                 pixels.data(), static_cast<UINT32>(target.cx * sizeof(uint32_t)), props, &bitmap);
    // Sixteen entries cover the visible rows even at the largest supported height.
    while(image_preview_cache_.size()>=16){auto it=image_preview_cache_.begin();ReleasePtr(it->second);image_preview_cache_.erase(it);}
    image_preview_cache_[key] = bitmap;
    return bitmap;
}

ID2D1Bitmap* PopupWindow::LoadImageFilePreviewBitmap(const std::filesystem::path& path) {
    if(!wic_factory_)CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic_factory_));
    if (path.empty() || !wic_factory_ || !render_target_) {
        return nullptr;
    }
    const std::wstring key = path.wstring();
    if (const auto found = image_preview_cache_.find(key); found != image_preview_cache_.end()) {
        return found->second;
    }

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapSource* preview_source = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID2D1Bitmap* bitmap = nullptr;
    if (LoadWicFrameFromFile(wic_factory_, path, &decoder, &frame) &&
        (preview_source = CreateScaledPreviewSource(wic_factory_, frame)) != nullptr &&
        SUCCEEDED(wic_factory_->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(preview_source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeMedianCut))) {
        render_target_->CreateBitmapFromWicBitmap(converter, nullptr, &bitmap);
    }
    ReleasePtr(converter);
    ReleasePtr(preview_source);
    ReleasePtr(frame);
    ReleasePtr(decoder);
    while(image_preview_cache_.size()>=16){auto it=image_preview_cache_.begin();ReleasePtr(it->second);image_preview_cache_.erase(it);}
    image_preview_cache_[key] = bitmap;
    return bitmap;
}

ID2D1Bitmap* PopupWindow::CachedImagePreviewBitmap(const std::filesystem::path& path) const {
    if (path.empty()) {
        return nullptr;
    }
    if (const auto found = image_preview_cache_.find(path.wstring()); found != image_preview_cache_.end()) {
        return found->second;
    }
    return nullptr;
}

PopupWindow::IconId PopupWindow::ToWindowIcon(PopupIconAssetSlot slot) const {
    switch (slot) {
    case PopupIconAssetSlot::Pin:
        return IconId::Pin;
    case PopupIconAssetSlot::PinActive:
        return IconId::PinActive;
    case PopupIconAssetSlot::Close:
        return IconId::Close;
    case PopupIconAssetSlot::Filter:
        return IconId::Filter;
    case PopupIconAssetSlot::FilterActive:
        return IconId::FilterActive;
    case PopupIconAssetSlot::MultiSelect:
        return IconId::MultiSelect;
    case PopupIconAssetSlot::MultiSelectActive:
        return IconId::MultiSelectActive;
    case PopupIconAssetSlot::Trash:
        return IconId::Trash;
    }
    return IconId::Filter;
}

void PopupWindow::DrawIcon(IconId icon, const UiRect& rect, float opacity) {
    const auto palette = ResolvePopupThemePalette(theme_mode_, IsSystemDarkTheme());
    InkGlyph glyph = InkGlyph::Plus;
    switch (icon) {
    case IconId::Pin: case IconId::PinActive: glyph = InkGlyph::Pin; break;
    case IconId::Close: glyph = InkGlyph::Close; break;
    case IconId::Filter: case IconId::FilterActive: glyph = InkGlyph::Filter; break;
    case IconId::MultiSelect: case IconId::MultiSelectActive: glyph = InkGlyph::Multi; break;
    case IconId::Trash: glyph = InkGlyph::Trash; break;
    case IconId::TextKind: glyph = InkGlyph::Text; break;
    case IconId::LinkKind: glyph = InkGlyph::Link; break;
    default: break;
    }
    const bool content=icon==IconId::TextKind || icon==IconId::LinkKind;
    if(!text_brush_)return;
    const auto saved=text_brush_->GetColor();
    text_brush_->SetColor(D2D1::ColorF(content?palette.muted:palette.text_tertiary,opacity));
    DrawInkGlyph(render_target_,d2d_factory_,text_brush_,glyph,rect,ink_glyph_cache_.get());
    text_brush_->SetColor(saved);
}

void PopupWindow::StartMotion(PopupMotionTarget target, int direction) {
    if (!hwnd_ || moving_window_ || resizing_window_) {
        return;
    }
    motion_target_ = target;
    motion_progress_ = 0.0f;
    motion_start_ms_ = static_cast<double>(GetTickCount64());
    motion_direction_ = direction < 0 ? -1 : 1;
    if (!animations_enabled_) {
        motion_progress_ = 1.0f;
        FinishMotionIfSettled();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::UpdateAnimationPreferences() {
    // 系统关闭"显示动画"时，所有进度直接取 1，弹簧直接跳到目标位置。
    BOOL enabled = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) {
        enabled = TRUE;
    }
    animations_enabled_ = enabled != FALSE;
    if (!animations_enabled_) {
        motion_progress_ = 1.0f;
        arrow_motion_progress_ = 1.0f;
        hover_progress_ = 1.0f;
    }
}

void PopupWindow::FinishMotionIfSettled() {
    if (motion_progress_ >= 1.0f) {
        motion_progress_ = 1.0f;
        if (motion_target_ == PopupMotionTarget::FilterPanel) {
            closing_filter_panel_ = false;
        } else if (motion_target_ == PopupMotionTarget::FavoriteMenu) {
            closing_favorite_group_menu_ = false;
        } else if (motion_target_ == PopupMotionTarget::DeleteConfirm) {
            closing_delete_confirm_ = false;
            closing_delete_confirm_snapshot_ = PendingFavoriteGroupDelete{};
        }
        motion_target_ = PopupMotionTarget::None;
    }
}

PopupMotionKind PopupWindow::MotionKindForTarget(PopupMotionTarget target) {
    switch (target) {
    case PopupMotionTarget::ViewSwitch:
        return PopupMotionKind::ViewSwitch;
    case PopupMotionTarget::MultiSelect:
        return PopupMotionKind::MultiSelect;
    default:
        return PopupMotionKind::Panel;
    }
}

BYTE PopupWindow::WindowAlphaForFrame() const {
    if (!window_entering_ && !window_leaving_) {
        return 255;
    }
    const PopupWindowMotionFrame frame = window_leaving_ ? PopupWindowLeaveFrame(window_motion_progress_)
                                                        : PopupWindowEnterFrame(window_motion_progress_);
    const float clamped = std::clamp(frame.opacity, 0.0f, 1.0f);
    return static_cast<BYTE>(std::lround(clamped * 255.0f));
}

void PopupWindow::ApplyWindowAlpha() {
    if (!hwnd_) {
        return;
    }
    // 预览是整窗 opacity；分层窗口的 LWA_ALPHA 是唯一能连 DWM 阴影一起淡出的做法。
    SetLayeredWindowAttributes(hwnd_, 0, WindowAlphaForFrame(), LWA_ALPHA);
}

void PopupWindow::AdvanceMotion() {
    if(!IsVisible()){KillTimer(hwnd_,kAnimationTimer);return;}
    const double now_ms = static_cast<double>(GetTickCount64());
    // 只有真的有进度在推进时才整窗重绘：之前每帧无条件 InvalidateRect，导致 hover 收敛后
    // 定时器还要空转 ~580ms、每 10ms 重绘一整张软件渲染的画布。
    bool needs_repaint = false;
    if (window_entering_ || window_leaving_) {
        window_motion_progress_ = PopupTimedMotionProgress(now_ms - window_motion_start_ms_, window_leaving_ ? 120.0 : 170.0, animations_enabled_);
        ApplyWindowAlpha();
        needs_repaint = true;
        if (window_motion_progress_ >= 1.0f) {
            if (window_leaving_) {
                completing_window_hide_ = true;
                Hide(L"animation-complete");
                completing_window_hide_ = false;
                return;
            }
            window_entering_ = false;
            ApplyWindowAlpha();
        }
    }
    // 预览 rowRise：40 + i*18ms 错峰延迟，每行 220ms；最后一行结束于 422ms。
    if (rows_entering_) {
        if (now_ms - rows_entrance_start_ms_ >= PopupRowEntranceTotalMs()) rows_entering_ = false;
        needs_repaint = true;
    }
    if (context_menu_open_ || closing_context_menu_) {
        context_motion_progress_ = PopupTimedMotionProgress(now_ms - context_motion_start_ms_, 200.0, animations_enabled_);
        if (context_motion_progress_ >= 1.0f) closing_context_menu_ = false;
        needs_repaint = true;
    }
    bool removed = false;
    for (auto it = row_transitions_.begin(); it != row_transitions_.end();) {
        needs_repaint = true;
        if (RowMotionProgress(it->first) >= 1.0f) {
            removed |= it->second.removing;
            it = row_transitions_.erase(it);
        } else ++it;
    }
    if (removed) { ReloadItems(); RefreshFavoriteGroupCounts(); }
    for (auto it = detail_transitions_.begin(); it != detail_transitions_.end();) {
        needs_repaint = true;
        if (now_ms - it->second.start_ms >= 240.0 || !animations_enabled_) it = detail_transitions_.erase(it);
        else ++it;
    }
    for(auto it=badge_transitions_.begin();it!=badge_transitions_.end();) {
        needs_repaint = true;
        if(now_ms-it->second.start_ms>=280.0)it=badge_transitions_.erase(it);else ++it;
    }
    if (stamp_item_id_) {
        if (now_ms - stamp_start_ms_ >= 960.0) {
            stamp_item_id_.reset();
        } else {
            needs_repaint = true;
        }
    }
    if (!toast_text_.empty()) {
        if (now_ms - toast_start_ms_ >= 1600.0) {
            toast_text_.clear();
        } else {
            needs_repaint = true;
        }
    }
    if (motion_target_ != PopupMotionTarget::None) {
        const auto kind = MotionKindForTarget(motion_target_);
        motion_progress_ = PopupTimedMotionProgress(now_ms - motion_start_ms_,
                                                    PopupMotionDurationMs(kind), animations_enabled_);
        FinishMotionIfSettled();
        needs_repaint = true;
    }
    if (pin_motion_progress_ < 1.0f) {
        pin_motion_progress_ = PopupTimedMotionProgress(now_ms - pin_motion_start_ms_, 260.0, animations_enabled_);
        if (pin_motion_progress_ >= 1.0f) pin_motion_progress_ = 1.0f;
        needs_repaint = true;
    }
    if (arrow_motion_progress_ < 1.0f) {
        arrow_motion_progress_ =
            PopupTimedMotionProgress(now_ms - arrow_motion_start_ms_,
                                     PopupMotionDurationMs(PopupMotionKind::Arrow), animations_enabled_);
        if (arrow_motion_progress_ >= 1.0f) {
            arrow_motion_progress_ = 1.0f;
            arrow_motion_item_id_.reset();
        }
        needs_repaint = true;
    }
    if (hover_progress_ < 1.0f) {
        hover_progress_ = PopupTimedMotionProgress(now_ms - hover_motion_start_ms_,
                                                   PopupMotionDurationMs(PopupMotionKind::Hover),
                                                   animations_enabled_);
        if (hover_progress_ >= 1.0f) {
            KillTimer(hwnd_, kHoverTimer);
        }
        needs_repaint = true;
    }
    // 弹簧/平滑滚动只在还需要推进时才重绘，收敛之后不再白白整窗重画。
    if (SlipNeedsAnimation() || SmoothScrollNeedsAnimation()) {
        needs_repaint = true;
    }
    AdvanceSlipSpring();
    AdvanceSmoothScroll();
    // 悬停提示（450ms 后出现）也靠这个定时器驱动；它由 hover_action_ 决定，不靠进度值。
    if (hover_action_ != UiAction::None) {
        needs_repaint = true;
    }
    // 日历/搜索框过渡是纯时间驱动的绘制（Paint 里按 GetTickCount64 求值）。
    if (now_ms - calendar_motion_start_ms_ < 240.0 || now_ms - search_focus_start_ms_ < 200.0) {
        needs_repaint = true;
    }
    if (motion_target_ == PopupMotionTarget::None && arrow_motion_progress_ >= 1.0f &&
        hover_progress_ >= 1.0f && pin_motion_progress_ >= 1.0f && !SlipNeedsAnimation() && !window_entering_ && !window_leaving_ &&
        !rows_entering_ && row_transitions_.empty() && detail_transitions_.empty() && badge_transitions_.empty() && !closing_context_menu_ &&
        context_motion_progress_ >= 1.0f && toast_text_.empty() && !stamp_item_id_ && !SmoothScrollNeedsAnimation() &&
        now_ms-calendar_motion_start_ms_>=240.0 && now_ms - search_focus_start_ms_ >= 200.0 &&
        (hover_action_==UiAction::None || now_ms-hover_motion_start_ms_>=580.0)) {
        KillTimer(hwnd_, kAnimationTimer);
    }
    if (needs_repaint) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

float PopupWindow::SlipTargetTop() const {
    if (items_.empty() || selected_index_ < 0 || selected_index_ >= static_cast<int>(items_.size())) {
        return -1.0f;
    }
    return ItemRowTop(selected_index_) - scroll_offset_;
}

float PopupWindow::SlipTargetHeight() const {
    if (items_.empty() || selected_index_ < 0 || selected_index_ >= static_cast<int>(items_.size())) {
        return 0.0f;
    }
    return ItemScrollHeight(selected_index_);
}

bool PopupWindow::SlipNeedsAnimation() const {
    const float target_top = SlipTargetTop();
    if (target_top < 0.0f) {
        return false;
    }
    return std::abs(slip_y_.position - target_top) > 0.5f || std::abs(slip_y_.velocity) > 2.0f ||
           std::abs(slip_h_.position - SlipTargetHeight()) > 0.5f || std::abs(slip_h_.velocity) > 2.0f;
}

void PopupWindow::KickSlipAnimation() {
    // 选中项或滚动位置变了：纸条要跟着动，必须保证 kAnimationTimer 在跑。
    if (!hwnd_ || !IsVisible() || !animations_enabled_ || !SlipNeedsAnimation()) {
        return;
    }
    slip_last_ms_ = 0.0;
    SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
}

void PopupWindow::AdvanceSlipSpring() {
    const double now_ms = static_cast<double>(GetTickCount64());
    const double elapsed_ms = slip_last_ms_ > 0.0 ? now_ms - slip_last_ms_ : 16.0;
    slip_last_ms_ = now_ms;
    const float target_top = SlipTargetTop();
    if (target_top < 0.0f || !animations_enabled_) {
        slip_y_ = PopupSpringState{target_top < 0.0f ? 0.0f : target_top, 0.0f};
        slip_h_ = PopupSpringState{SlipTargetHeight(), 0.0f};
        return;
    }
    const float dt = static_cast<float>(elapsed_ms / 1000.0);
    slip_y_ = PopupSpringStep(slip_y_.position, slip_y_.velocity, target_top, dt);
    slip_h_ = PopupSpringStep(slip_h_.position, slip_h_.velocity, SlipTargetHeight(), dt);
}

void PopupWindow::Paint() {
    if(!IsVisible())return;
    EnsureDeviceResources();
    if(!render_target_)return;
    const auto& metrics = PopupMetrics();
    if (PopupPaintShouldUpdateLayout()) {
        UpdatePopupLogicalSize();
        ClampScrollToCurrentPopupHeight();
    }
    if (slip_last_ms_ <= 0.0) {
        slip_last_ms_ = static_cast<double>(GetTickCount64());
    }
    const auto palette = ResolvePopupThemePalette(theme_mode_, IsSystemDarkTheme());
    const float surface_width = static_cast<float>(popup_logical_width_);
    const float surface_height = static_cast<float>(popup_logical_height_);
    // Ink Ledger 内容边线：左 18 / 右 10（页眉、搜索、标签栏同宽）。
    const float content_left = 18.0f;
    const float content_right = surface_width - 10.0f;
    const auto motionProgressFor = [&](PopupMotionTarget target) {
        if (motion_target_ != target) {
            return 1.0f;
        }
        const float progress = PopupMotionProgress(motion_progress_);
        return motion_direction_ < 0 ? 1.0f - progress : progress;
    };
    const bool view_switch_active = motion_target_ == PopupMotionTarget::ViewSwitch;

    render_target_->BeginDraw();
    // 预览里窗口是 border-radius:14px 的一张纸，圆角外是桌面。
    // 这里用透明清屏 + 自绘圆角矩形，圆角外一格像素都不涂：窗口级 alpha 才能整窗淡入。
    render_target_->Clear(D2D1::ColorF(0, 0.0f));

    if (!paint_brush_ && render_target_) {
        render_target_->CreateSolidColorBrush(ColorWithAlpha(palette.window_tint, palette.window_opacity), &paint_brush_);
    }
    ID2D1SolidColorBrush* brush = paint_brush_;
    if (!brush) {
        return;
    }
    const bool drawing_fast_interaction = moving_window_ || resizing_window_;
    auto drawEdgeShadow = [&](const UiRect& rect, float radius, float strength = 1.0f, float y_offset = 2.0f) {
        if (!PopupShouldDrawDecorativeShadows(drawing_fast_interaction)) {
            return;
        }
        const float amount = std::clamp(strength,0.0f,1.0f);
        const int steps=radius>=11?12:7;
        for(int i=steps;i>0;--i) {
            const float spread=static_cast<float>(i);
            brush->SetColor(D2D1::ColorF(palette.dark?0x000000:0x282014,
                (palette.dark?0.025f:0.012f)*amount*(1.0f-spread/static_cast<float>(steps+2))));
            render_target_->FillRoundedRectangle(RoundRect(rect.left-spread,rect.top+y_offset-spread*0.4f,
                rect.right+spread,rect.bottom+y_offset+spread,radius+spread*0.5f),brush);
        }
    };
    auto drawInkPanelFrame = [&](const UiRect& rect, float radius, float opacity = 0.74f, bool strong = false) {
        brush->SetColor(D2D1::ColorF(strong ? palette.border_selected : palette.border,
                                     palette.dark ? opacity * 0.78f : opacity));
        render_target_->DrawRoundedRectangle(RoundRect(rect, radius), brush, strong ? 1.25f : 1.0f);
    };
    // 发丝线：墨色 9%（加强版 16%）。
    auto hairline = [&](float left, float top, float right, bool strong = false) {
        brush->SetColor(D2D1::ColorF(palette.text, strong ? 0.16f : 0.09f));
        render_target_->DrawLine(D2D1::Point2F(left, top + 0.5f), D2D1::Point2F(right, top + 0.5f), brush, 1.0f);
    };
    // 收藏书签底部的 V 形缺口：两条斜切三角在单位空间里各建一次，之后只做平移 + 缩放。
    // 之前每帧、每个收藏行都要 CreatePathGeometry 两次（并立刻 Release）。
    auto ribbonNotchGeometry = [&](bool mirrored) -> ID2D1PathGeometry* {
        ID2D1PathGeometry*& slot = mirrored ? ribbon_notch_right_ : ribbon_notch_left_;
        if (slot || !d2d_factory_) {
            return slot;
        }
        ID2D1PathGeometry* geometry = nullptr;
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(d2d_factory_->CreatePathGeometry(&geometry)) && geometry &&
            SUCCEEDED(geometry->Open(&sink)) && sink) {
            sink->BeginFigure(D2D1::Point2F(0.0f, 0.0f), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(mirrored ? 1.0f : 0.0f, 1.0f));
            sink->AddLine(D2D1::Point2F(mirrored ? 0.0f : 1.0f, 0.0f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            slot = geometry;
            return geometry;
        }
        ReleasePtr(geometry);
        return nullptr;
    };
    // 在 (x, y) 处画一个宽 w、高 h 的斜切缺口。
    auto fillRibbonNotch = [&](float x, float y, float w, float h, bool mirrored, uint32_t color, float alpha) {
        ID2D1PathGeometry* geometry = ribbonNotchGeometry(mirrored);
        if (!geometry) {
            return;
        }
        D2D1_MATRIX_3X2_F saved{};
        render_target_->GetTransform(&saved);
        render_target_->SetTransform(D2D1::Matrix3x2F::Scale(w, h) * D2D1::Matrix3x2F::Translation(x, y) * saved);
        brush->SetColor(D2D1::ColorF(color, alpha));
        render_target_->FillGeometry(geometry, brush);
        render_target_->SetTransform(saved);
    };
    auto drawText = [&](std::wstring_view text, IDWriteTextFormat* format, const UiRect& rect, uint32_t color,
                        float alpha) {
        if (text.empty() || !format) {
            return;
        }
        brush->SetColor(D2D1::ColorF(color, alpha));
        render_target_->DrawTextW(text.data(), static_cast<UINT32>(text.size()), format, Rect(rect), brush);
    };
    auto drawChevron = [&](float cx, float cy, float size, bool up, uint32_t color, float alpha, float thickness) {
        brush->SetColor(D2D1::ColorF(color, alpha));
        const float half = size * 0.5f;
        if (up) {
            render_target_->DrawLine(D2D1::Point2F(cx - half, cy + half * 0.5f), D2D1::Point2F(cx, cy - half * 0.5f),
                                     brush, thickness);
            render_target_->DrawLine(D2D1::Point2F(cx + half, cy + half * 0.5f), D2D1::Point2F(cx, cy - half * 0.5f),
                                     brush, thickness);
        } else {
            render_target_->DrawLine(D2D1::Point2F(cx - half, cy - half * 0.5f), D2D1::Point2F(cx, cy + half * 0.5f),
                                     brush, thickness);
            render_target_->DrawLine(D2D1::Point2F(cx + half, cy - half * 0.5f), D2D1::Point2F(cx, cy + half * 0.5f),
                                     brush, thickness);
        }
    };

    auto drawAlignedText = [&](std::wstring_view text, IDWriteTextFormat* format, const UiRect& rect,
                               uint32_t color, float alpha, DWRITE_TEXT_ALIGNMENT alignment) {
        if (!format) return;
        const auto previous = format->GetTextAlignment();
        format->SetTextAlignment(alignment);
        drawText(text, format, rect, color, alpha);
        format->SetTextAlignment(previous);
    };
    auto textWidth = [&](std::wstring_view text, IDWriteTextFormat* format) {
        if (!format || text.empty()) {
            return 0.0f;
        }
        const auto key = std::make_pair(std::wstring(text), static_cast<const void*>(format));
        if (const auto cached = text_width_cache_.find(key); cached != text_width_cache_.end()) {
            return cached->second;
        }
        IDWriteTextLayout* layout = nullptr;
        float width = 0.0f;
        if (SUCCEEDED(dwrite_factory_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                format, 4096.0f, 100.0f, &layout))) {
            DWRITE_TEXT_METRICS tm{}; layout->GetMetrics(&tm); width = tm.widthIncludingTrailingWhitespace;
            layout->Release();
        }
        // 固定文案（品牌名、"匹配 "、提示语）不需要每帧重新排版；数量变化时条目仍然有限。
        if (text_width_cache_.size() < 256) {
            text_width_cache_[key] = width;
        }
        return width;
    };
    auto glyph = [&](InkGlyph icon, const UiRect& rect, uint32_t color, float alpha = 1.0f) {
        brush->SetColor(D2D1::ColorF(color, alpha));
        DrawInkGlyph(render_target_,d2d_factory_,brush,icon,rect,ink_glyph_cache_.get());
    };
    // 预览 #winIn/#winOut 的 opacity / translateY / scale，按真实时间逐帧求值。
    // opacity 只由窗口级 LWA_ALPHA 施加（见 ApplyWindowAlpha），这里再叠一层 D2D
    // 图层透明度会变成 opacity²，和预览对不上。
    const PopupWindowMotionFrame window_frame =
        window_leaving_ ? PopupWindowLeaveFrame(window_motion_progress_)
                        : (window_entering_ ? PopupWindowEnterFrame(window_motion_progress_)
                                            : PopupWindowMotionFrame{});
    const float window_scale = window_frame.scale;
    if (window_entering_ || window_leaving_) {
        render_target_->SetTransform(D2D1::Matrix3x2F::Scale(window_scale, window_scale,
            D2D1::Point2F(surface_width / 2.0f, surface_height / 2.0f)) *
            D2D1::Matrix3x2F::Translation(0, window_frame.translate_y));
    }
    {
        const UiRect backdrop_rect{0.0f, 0.0f, surface_width, surface_height};
        brush->SetColor(ColorWithAlpha(palette.window_tint, palette.window_opacity));
        render_target_->FillRoundedRectangle(RoundRect(backdrop_rect, PopupWindowEdgeCornerRadius()), brush);
    }
    // 注意：透明度完全由窗口级 LWA_ALPHA 负责；这里再 PushLayer 半透明会变成 opacity²。

    // ---------- 窗口底 ----------
    // 底色已在 window_layer 之前铺好（见上），这里不再重复铺一层。
    const UiRect window_surface_rect{0.0f, 0.0f, surface_width, surface_height};

    // ---------- A1 页眉：Georgia 斜体品牌 + 朱砂方点 + 等宽版本号 ----------
    const auto header = BuildPopupHeaderLayoutForWidth(popup_logical_width_);
    const UiRect brand_rect{content_left, 0.0f, content_left + 96.0f, 44.0f};
    drawText(L"ClipSoul", brand_format_, brand_rect, palette.text, 0.96f);
    brush->SetColor(D2D1::ColorF(palette.danger, 0.94f));
    const float brand_width = textWidth(L"ClipSoul", brand_format_);
    render_target_->FillRoundedRectangle(
        RoundRect(content_left + brand_width + 3.0f, 21.0f, content_left + brand_width + 8.0f, 26.0f, 1.0f), brush);
    drawText(kClipSoulVersion, mono_format_,
             UiRect{content_left + brand_width + 16.0f, 3.0f, content_left + brand_width + 78.0f, 44.0f}, palette.text_tertiary, 1.0f);

    auto drawHeaderButton = [&](const UiRect& rect, bool active, UiAction action) {
        const bool hovered = hover_action_ == action;
        const float hover = hovered ? hover_progress_ : 0.0f;
        if (hovered) {
            brush->SetColor(D2D1::ColorF(action == UiAction::Close ? palette.danger : palette.paper_hover,
                                         action == UiAction::Close ? 0.12f * hover : 0.92f * hover));
            render_target_->FillRoundedRectangle(RoundRect(rect, 7), brush);
        }
        if (action == UiAction::Pin && active) {
            // 「已固定」小圆点跟随旋转进度淡入。
            const float badge = pin_motion_progress_ < 1.0f
                                    ? (pin_motion_to_ ? pin_motion_progress_ : 1.0f - pin_motion_progress_)
                                    : (pinned_open_ ? 1.0f : 0.0f);
            if (badge > 0.0f) {
                brush->SetColor(D2D1::ColorF(palette.danger, 0.96f * badge));
                render_target_->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(rect.right - 6.5f, rect.top + 6.5f), 2.5f * badge, 2.5f * badge),
                    brush);
            }
        }
        return hover;
    };
    drawHeaderButton(header.pin, pinned_open_, UiAction::Pin);
    {
        // 图钉旋转：未固定 0° → 固定后 -40°，260ms 弹性缓动（预览 #pinWin）。
        const float opacity = palette.dark ? (pinned_open_ ? 0.96f : 0.72f) : (pinned_open_ ? 0.88f : 0.55f);
        const UiRect pin_icon = BuildPopupHeaderPinIconRect(header);
        const float angle = PopupPinRotationDegrees(pin_motion_to_, pin_motion_progress_);
        if (std::fabs(angle) > 0.01f) {
            const float cx = (pin_icon.left + pin_icon.right) * 0.5f;
            const float cy = (pin_icon.top + pin_icon.bottom) * 0.5f;
            D2D1_MATRIX_3X2_F saved_transform{};
            render_target_->GetTransform(&saved_transform);
            render_target_->SetTransform(D2D1::Matrix3x2F::Rotation(angle, D2D1::Point2F(cx, cy)) * saved_transform);
            DrawIcon(ToWindowIcon(PopupPinIconSlot(pin_motion_to_)), pin_icon, opacity);
            render_target_->SetTransform(saved_transform);
        } else {
            DrawIcon(ToWindowIcon(PopupPinIconSlot(pin_motion_to_)), pin_icon, opacity);
        }
    }
    const float close_hover = drawHeaderButton(header.close, false, UiAction::Close);
    if (close_hover > 0.0f) {
        brush->SetColor(D2D1::ColorF(palette.danger, 0.30f + 0.66f * close_hover));
        const auto icon_rect = BuildPopupHeaderCloseIconRect(header);
        const float cx = (icon_rect.left + icon_rect.right) * 0.5f;
        const float cy = (icon_rect.top + icon_rect.bottom) * 0.5f;
        render_target_->DrawLine(D2D1::Point2F(cx - 4.5f, cy - 4.5f), D2D1::Point2F(cx + 4.5f, cy + 4.5f), brush,
                                 1.4f);
        render_target_->DrawLine(D2D1::Point2F(cx + 4.5f, cy - 4.5f), D2D1::Point2F(cx - 4.5f, cy + 4.5f), brush,
                                 1.4f);
    } else {
        DrawIcon(IconId::Close, BuildPopupHeaderCloseIconRect(header), palette.dark ? 0.62f : 0.42f);
    }

    // ---------- A2 搜索：无框、底部发丝线、聚焦时墨线从左展开 ----------
    const auto search_layout = BuildPopupSearchLayoutForWidth(popup_logical_width_);
    const bool search_hovered = hover_action_ == UiAction::Search || Contains(search_layout.box, hover_point_);
    const bool search_active = search_focused_ && GetFocus() == search_edit_;
    const float search_focus_p = PopupMotionProgress(PopupTimedMotionProgress(
        static_cast<double>(GetTickCount64()) - search_focus_start_ms_, 200.0, animations_enabled_));
    const float search_focus = search_focus_from_ + (search_focus_to_ - search_focus_from_) * search_focus_p;
    const float search_line_y = search_layout.box.bottom - 1.0f;
    hairline(search_layout.box.left, search_line_y, search_layout.box.right, true);
    if (search_focus > 0.0f) {
        const float line_right =
            search_layout.box.left + (search_layout.box.right - search_layout.box.left) * search_focus;
        brush->SetColor(D2D1::ColorF(palette.text, 0.92f));
        render_target_->DrawLine(D2D1::Point2F(search_layout.box.left, search_line_y),
                                 D2D1::Point2F(line_right, search_line_y), brush, 1.5f);
    }
    const float search_icon_x = search_layout.icon.left + search_layout.icon.Width() * 0.5f;
    const float search_icon_y = (search_layout.icon.top + search_layout.icon.bottom) * 0.5f;
    brush->SetColor(D2D1::ColorF(search_active ? palette.text : palette.text_tertiary, search_active ? 0.92f : 0.76f));
    render_target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(search_icon_x, search_icon_y - 1.0f), 4.6f, 4.6f), brush,
                                1.4f);
    render_target_->DrawLine(D2D1::Point2F(search_icon_x + 3.4f, search_icon_y + 2.6f),
                             D2D1::Point2F(search_icon_x + 6.6f, search_icon_y + 5.8f), brush, 1.4f);

    const bool has_composition = PopupSearchHasComposition(composing_, composition_text_);
    const auto search_text = PopupSearchCompositionDisplayText(query_, composing_, composition_text_);
    const auto search_selection = CurrentSearchSelection();
    if (PopupSearchShouldDrawSelection(search_active, !query_.empty(), search_selection)) {
        const float selection_left =
            ClampPopupSearchCaretX(search_layout, SearchTextOffsetDips(search_selection.start, false));
        const float selection_right =
            ClampPopupSearchCaretX(search_layout, SearchTextOffsetDips(search_selection.end, false));
        if (selection_right > selection_left) {
            brush->SetColor(D2D1::ColorF(palette.text, palette.dark ? 0.22f : 0.10f));
            render_target_->FillRoundedRectangle(
                RoundRect(selection_left, search_layout.text.top + 2.0f, selection_right,
                          search_layout.text.bottom - 2.0f, 3.0f),
                brush);
        }
    }
    const bool composition_colored = PopupSearchCompositionTextColor(has_composition, query_);
    if (has_composition && dwrite_factory_ && search_format_) {
        IDWriteTextLayout* text_layout = nullptr;
        if (SUCCEEDED(dwrite_factory_->CreateTextLayout(search_text.data(), static_cast<UINT32>(search_text.size()),
                                                        search_format_, search_layout.text.Width(),
                                                        search_layout.text.Height(), &text_layout)) &&
            text_layout) {
            DWRITE_TEXT_RANGE range{static_cast<UINT32>(query_.size()),
                                    static_cast<UINT32>(composition_text_.size())};
            text_layout->SetUnderline(TRUE, range);
            brush->SetColor(D2D1::ColorF(palette.text, 0.94f));
            render_target_->DrawTextLayout(D2D1::Point2F(search_layout.text.left, search_layout.text.top),
                                           text_layout, brush);
            ReleasePtr(text_layout);
        }
    } else {
        drawText(search_text, search_format_, search_layout.text,
                 composition_colored ? palette.text : palette.text_tertiary, composition_colored ? 0.94f : 0.62f);
    }
    if (PopupSearchCaretVisible(search_active, search_caret_on_) && !PopupSearchHasSelection(search_selection) &&
        PopupSearchCaretVisibleDuringComposition(has_composition)) {
        const float caret_x = ClampPopupSearchCaretX(search_layout, SearchCaretOffsetDips());
        brush->SetColor(D2D1::ColorF(palette.text, 0.86f));
        render_target_->DrawLine(D2D1::Point2F(caret_x, search_layout.text.top + 2.0f),
                                 D2D1::Point2F(caret_x, search_layout.text.bottom - 2.0f), brush, 1.0f);
    }
    if (!query_.empty()) {
        const bool clear_hovered = hover_action_ == UiAction::ClearSearch ||
                                   PopupSearchClearButtonHitTest(search_layout, true, hover_point_);
        const auto center = PopupSearchClearButtonCenterDips(search_layout);
        if (clear_hovered) {
            brush->SetColor(D2D1::ColorF(palette.paper_hover, 0.94f));
            render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<float>(center.x),
                                                                    static_cast<float>(center.y)),
                                                      9.0f, 9.0f),
                                        brush);
        }
        brush->SetColor(D2D1::ColorF(clear_hovered ? palette.text : palette.text_tertiary,
                                     clear_hovered ? 0.92f : 0.72f));
        render_target_->DrawLine(D2D1::Point2F(center.x - 3.5f, center.y - 3.5f),
                                 D2D1::Point2F(center.x + 3.5f, center.y + 3.5f), brush, 1.3f);
        render_target_->DrawLine(D2D1::Point2F(center.x + 3.5f, center.y - 3.5f),
                                 D2D1::Point2F(center.x - 3.5f, center.y + 3.5f), brush, 1.3f);
    }

    // ---------- A3 标签栏 + 工具按钮（同栏靠右） ----------
    const auto tabs = BuildPopupTabsLayoutForWidth(view_mode_ == ViewMode::Favorites, popup_logical_width_);
    const bool multi_motion_active = motion_target_ == PopupMotionTarget::MultiSelect;
    const float multi_motion = multi_motion_active ? PopupMotionProgress(motion_progress_) : 1.0f;
    const bool multi_entering = multi_select_;
    const float tab_text_top = tabs.history.top;

    auto drawTabLabel = [&](const UiRect& rect, const wchar_t* label, int count, bool active) {
        drawText(label, active ? title_format_ : label_format_,
                 UiRect{rect.left, tab_text_top, rect.left + 27.0f, rect.bottom},
                 active ? palette.text : palette.text_tertiary, active ? 0.98f : 0.86f);
        drawText(std::to_wstring(count), mono_format_,
                 UiRect{rect.left + 30.0f, tab_text_top + 2.0f, rect.right, rect.bottom},
                 palette.text_tertiary, 0.82f);
    };
    drawTabLabel(tabs.history, L"历史", history_count_, view_mode_ == ViewMode::History);
    drawTabLabel(tabs.favorites, L"收藏", favorite_group_counts_total_, view_mode_ == ViewMode::Favorites);

    // 选中标签下的 1.5px 墨线（滑动动画）
    UiRect indicator = tabs.active_indicator;
    if (view_switch_active) {
        const auto previous_tabs =
            BuildPopupTabsLayoutForWidth(motion_direction_ > 0 ? false : true, popup_logical_width_);
        indicator = PopupTabIndicatorRectForMotion(previous_tabs.active_indicator, tabs.active_indicator,
                                                   PopupMotionProgress(motion_progress_));
    }
    brush->SetColor(D2D1::ColorF(palette.text, 0.92f));
    render_target_->FillRectangle(Rect(indicator.left, indicator.bottom - 1.5f, indicator.right, indicator.bottom),
                                  brush);


    if (view_mode_ == ViewMode::Favorites) {
        // 分组按钮（描边小按钮：组名 + ▾）
        const bool group_hovered = hover_action_ == UiAction::FavoriteGroupMenu;
        const float group_hover = group_hovered ? hover_progress_ : 0.0f;
        if (group_hovered || favorite_group_menu_open_) {
            brush->SetColor(D2D1::ColorF(palette.paper_hover, 0.94f * (favorite_group_menu_open_ ? 1.0f : group_hover)));
            render_target_->FillRoundedRectangle(RoundRect(tabs.favorite_group, 6), brush);
        }
        brush->SetColor(D2D1::ColorF(palette.text, 0.16f));
        render_target_->DrawRoundedRectangle(RoundRect(tabs.favorite_group, 6), brush, 1.0f);
        drawText(ActiveFavoriteGroupLabel(), button_format_,
                 UiRect{tabs.favorite_group.left + 8.0f, tabs.favorite_group.top,
                        tabs.favorite_group.right - 15.0f, tabs.favorite_group.bottom},
                 palette.muted, 0.94f);
        drawChevron(tabs.favorite_group.right - 9.0f,
                    (tabs.favorite_group.top + tabs.favorite_group.bottom) * 0.5f, 8.0f,
                    favorite_group_menu_open_, palette.text_tertiary, 1.0f, 1.6f);
        // ＋ 添加常用语
        const bool add_hovered = hover_action_ == UiAction::AddFavoritePhrase;
        const float add_cx = (tabs.add_favorite_phrase.left + tabs.add_favorite_phrase.right) * 0.5f;
        const float add_cy = (tabs.add_favorite_phrase.top + tabs.add_favorite_phrase.bottom) * 0.5f;
        if (add_hovered) {
            brush->SetColor(D2D1::ColorF(palette.paper_hover, 0.94f * hover_progress_));
            render_target_->FillRoundedRectangle(RoundRect(tabs.add_favorite_phrase, 7.0f), brush);
        }
        brush->SetColor(D2D1::ColorF(palette.muted, 0.92f));
        render_target_->DrawLine(D2D1::Point2F(add_cx - 4.0f, add_cy), D2D1::Point2F(add_cx + 4.0f, add_cy), brush,
                                 1.3f);
        render_target_->DrawLine(D2D1::Point2F(add_cx, add_cy - 4.0f), D2D1::Point2F(add_cx, add_cy + 4.0f), brush,
                                 1.3f);
    }

    const auto toolbar = BuildPopupToolbarLayoutForWidth(multi_select_, popup_logical_width_);
    D2D1_MATRIX_3X2_F toolbar_transform{};
    render_target_->GetTransform(&toolbar_transform);
    if (multi_motion_active) {
        render_target_->SetTransform(
            D2D1::Matrix3x2F::Translation(PopupMultiSelectToolbarOffset(multi_motion, multi_entering), 0.0f));
    }
    auto drawIconButton = [&](const UiRect& rect, IconId icon, UiAction action, bool active, bool show_dot) {
        const bool hovered = hover_action_ == action;
        const float hover = hovered ? hover_progress_ : 0.0f;
        if (hovered) {
            brush->SetColor(D2D1::ColorF(palette.paper_hover, 0.94f * hover));
            render_target_->FillRoundedRectangle(RoundRect(rect, 7), brush);
        }
        DrawIcon(icon, CenteredRect(rect, 15.0f, 15.0f),
                 palette.dark ? (active ? 0.96f : (hovered ? 0.92f : 0.66f))
                              : (active ? 0.90f : (hovered ? 0.86f : 0.58f)));
        if (show_dot) {
            // 有筛选条件时，筛选按钮右上角朱砂圆点
            brush->SetColor(D2D1::ColorF(palette.danger, 0.96f));
            render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rect.right - 5.0f, rect.top + 5.0f), 2.2f, 2.2f),
                                        brush);
        }
    };
    auto drawTextButton = [&](const UiRect& rect, const std::wstring& label, UiAction action, bool danger,
                              bool solid) {
        const bool hovered = hover_action_ == action;
        const float hover = hovered ? hover_progress_ : 0.0f;
        if (solid) {
            brush->SetColor(D2D1::ColorF(palette.text, 0.94f));
            render_target_->FillRoundedRectangle(RoundRect(rect, 6), brush);
            drawAlignedText(label, button_format_, rect, palette.window_tint, selection_.SelectedIds().empty() ? 0.35f : 1.0f, DWRITE_TEXT_ALIGNMENT_CENTER);
            return;
        }
        if (hovered) {
            brush->SetColor(D2D1::ColorF(danger ? palette.danger : palette.paper_hover,
                                         danger ? 0.12f * hover : 0.94f * hover));
            render_target_->FillRoundedRectangle(RoundRect(rect, 6), brush);
        }
        const bool disabled = danger && selection_.SelectedIds().empty();
        drawAlignedText(label, button_format_, rect, danger && hovered ? palette.danger : palette.muted,
                        disabled ? 0.35f : 1.0f, DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    if (multi_select_) {
        const size_t selected_size = selection_.Count();
        const std::wstring selected_count = std::to_wstring(selected_size);
        drawTextButton(toolbar.cancel_multi_select, L"取消", UiAction::MultiSelect, false, false);
        drawTextButton(toolbar.select_all, !items_.empty() && selected_size == items_.size() ? L"全不选" : L"全选", UiAction::SelectAll, false, false);
        drawTextButton(toolbar.delete_selected, L"删除", UiAction::DeleteSelected, true, false);
        drawTextButton(toolbar.paste_selected, L"粘贴 " + selected_count, UiAction::PasteSelected, false, true);
    } else {
        drawIconButton(toolbar.filter, ToWindowIcon(PopupFilterIconSlot(filter_open_)), UiAction::Filter, filter_open_,
                       HasActiveFilter());
        drawIconButton(toolbar.multi_select, ToWindowIcon(PopupMultiSelectIconSlot(false)), UiAction::MultiSelect,
                       false, false);
        drawIconButton(toolbar.clear_all, IconId::Trash, UiAction::ClearAll, false, false);
    }
    if (multi_motion_active) {
        render_target_->SetTransform(toolbar_transform);
    }

    // ---------- A4/A5/A6 列表：发丝线分行，当前项是抽起的纸条 ----------
    const auto list_clip = PopupListClipRectForHeight(popup_logical_width_, popup_logical_height_);
    render_target_->PushAxisAlignedClip(Rect(list_clip), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    D2D1_MATRIX_3X2_F list_transform{};
    render_target_->GetTransform(&list_transform);
    if (view_switch_active) {
        render_target_->SetTransform(D2D1::Matrix3x2F::Translation(
            PopupViewSwitchListOffset(motion_direction_, motion_progress_, 18.0f), 0.0f));
    }

    const auto plan = CurrentSectionPlan();
    const float section_header_height = PopupSectionHeaderHeight();

    // 当前项纸条：弹簧跟随，画在内容之下
    const float slip_target_top = SlipTargetTop();
    if (slip_target_top >= 0.0f && !multi_select_) {
        const float slip_top = PopupListTop() + slip_y_.position;
        const float slip_height = std::max(20.0f, slip_h_.position);
        const UiRect slip{8.0f, slip_top, surface_width - 8.0f, slip_top + slip_height};
        drawEdgeShadow(slip, 9.0f, 0.34f, 1.6f);
        brush->SetColor(D2D1::ColorF(palette.paper_selected, palette.dark ? 0.94f : 0.99f));
        render_target_->FillRoundedRectangle(RoundRect(slip, 9), brush);
        brush->SetColor(D2D1::ColorF(palette.text, 0.10f));
        render_target_->DrawRoundedRectangle(RoundRect(slip, 9), brush, 1.0f);
    }

    const TimeLabelContext time_context = TimeLabelContext::Now();
    const int first_visible_index = FirstVisibleItemIndex();
    // 预览 JS 的 --i：sect 取当前计数（不递增），row 取 Math.min(++i, 9)。
    // 所以 sect 的 --i = 已渲染行数，行 k 的 --i = k + 1。
    const double entrance_elapsed_ms =
        rows_entering_ ? static_cast<double>(GetTickCount64()) - rows_entrance_start_ms_ : 1.0e9;
    const auto entrance_index_for_item = [](int item_index) { return std::min(item_index + 1, 9); };
    for (int item_index = first_visible_index; item_index < static_cast<int>(items_.size()); ++item_index) {
        const float row_top = PopupListTop() + ItemRowTop(item_index) - scroll_offset_;
        if (row_top > static_cast<float>(popup_logical_height_)) {
            break;
        }
        // A5 分区标题：置顶 N（朱砂）/ 最近 N（收藏视图叫"其他"）
        if (plan.show_pinned_header && item_index == 0) {
            const float header_top = row_top - section_header_height;
            if (header_top + section_header_height > PopupListTop()) {
                // 置顶分区标题排在首行之前：预览里此时 --i 仍是 0。
                const auto entrance = PopupRowEntranceFrameAt(entrance_elapsed_ms, 0);
                D2D1_MATRIX_3X2_F saved{};
                render_target_->GetTransform(&saved);
                render_target_->SetTransform(D2D1::Matrix3x2F::Translation(0, entrance.translate_y) * saved);
                const float pin_y = header_top + 16.0f;
                glyph(InkGlyph::PinFill,{content_left,pin_y-5.5f,content_left+11,pin_y+5.5f},palette.danger,
                      entrance.opacity);
                drawText(L"置顶 " + std::to_wstring(plan.pinned_count), small_format_,
                         UiRect{content_left + 16.0f, header_top+8, content_left + 66.0f,
                                header_top + 24.0f}, palette.danger, entrance.opacity);
                brush->SetColor(D2D1::ColorF(palette.danger, 0.10f * entrance.opacity));
                render_target_->DrawLine(D2D1::Point2F(content_left + 54.0f, pin_y),
                                         D2D1::Point2F(surface_width - 18.0f, pin_y), brush, 1.0f);
                render_target_->SetTransform(saved);
            }
        }
        if (plan.show_recent_header && static_cast<size_t>(item_index) == plan.pinned_count) {
            const float header_top = row_top - section_header_height;
            if (header_top + section_header_height > PopupListTop()) {
                // 预览里 --i 用的是当前计数（已渲染行数），最近分区标题 = pinned_count。
                const auto entrance = PopupRowEntranceFrameAt(
                    entrance_elapsed_ms, std::min(static_cast<int>(plan.pinned_count), 9));
                D2D1_MATRIX_3X2_F saved{};
                render_target_->GetTransform(&saved);
                render_target_->SetTransform(D2D1::Matrix3x2F::Translation(0, entrance.translate_y) * saved);
                const wchar_t* label = view_mode_ == ViewMode::Favorites ? L"其他" : L"最近";
                drawText(std::wstring(label) + L" " + std::to_wstring(plan.recent_count), small_format_,
                         UiRect{content_left, header_top+8, content_left + 58.0f, header_top+24},
                         palette.text_tertiary, entrance.opacity);
                brush->SetColor(D2D1::ColorF(palette.text, 0.09f * entrance.opacity));
                render_target_->DrawLine(D2D1::Point2F(content_left + 45.0f, header_top + 16.0f),
                                         D2D1::Point2F(surface_width - 18.0f, header_top + 16.0f),
                                         brush, 1.0f);
                render_target_->SetTransform(saved);
            }
        }

        const auto& item = items_[item_index];
        const bool selected = item_index == selected_index_;
        const bool hovered = item_index == hover_item_index_;
        const float hover = hovered ? hover_progress_ : 0.0f;
        const bool checked = selection_.IsSelected(item.id);
        const bool expanded = expanded_item_id_ && *expanded_item_id_ == item.id;
        const float expanded_height = AnimatedExpandedHeight(item);
        D2D1_MATRIX_3X2_F row_transform{};
        render_target_->GetTransform(&row_transform);
        float row_opacity = 1.0f, row_dx = 0.0f, row_dy = 0.0f;
        if (rows_entering_) {
            const auto entrance = PopupRowEntranceFrameAt(entrance_elapsed_ms, entrance_index_for_item(item_index));
            row_opacity = entrance.opacity;
            row_dy = entrance.translate_y;
        }
        if (auto motion = row_transitions_.find(item.id); motion != row_transitions_.end()) {
            const float p = RowMotionProgress(item.id);
            if (motion->second.removing) { row_opacity *= 1.0f - std::min(1.0f, p / 0.4f); row_dx = -10.0f * PopupMotionProgress(p); }
            else { row_opacity *= PopupMotionProgress(p); row_dy -= 6.0f * (1.0f - PopupMotionProgress(p)); }
        }
        float press_scale = pressed_item_index_ == item_index ? (long_press_selected_ ? 0.985f : 0.99f) : 1.0f;
        render_target_->SetTransform(D2D1::Matrix3x2F::Scale(press_scale, press_scale,
            D2D1::Point2F(surface_width / 2, row_top + ItemScrollHeight(item_index) / 2)) *
            D2D1::Matrix3x2F::Translation(row_dx, row_dy) * row_transform);
        ID2D1Layer* row_layer = nullptr;
        const bool row_layer_active = row_opacity < 0.999f && SUCCEEDED(render_target_->CreateLayer(nullptr, &row_layer));
        if (row_layer_active) render_target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::Matrix3x2F::Identity(), row_opacity), row_layer);
        auto card = BuildPopupCardLayout(multi_select_ || multi_motion_active, row_top);
        if (multi_motion_active) {
            const auto single_card = BuildPopupCardLayout(false, row_top);
            const auto multi_card = BuildPopupCardLayout(true, row_top);
            const float layout_progress = multi_select_ ? multi_motion : 1.0f - multi_motion;
            const auto blendRect = [layout_progress](const UiRect& from, const UiRect& to) {
                const auto blend = [layout_progress](float a, float b) { return a + (b - a) * layout_progress; };
                return UiRect{blend(from.left, to.left), blend(from.top, to.top), blend(from.right, to.right),
                              blend(from.bottom, to.bottom)};
            };
            card.image_preview = blendRect(single_card.image_preview, multi_card.image_preview);
            card.file_icon = blendRect(single_card.file_icon, multi_card.file_icon);
            card.kind_badge = blendRect(single_card.kind_badge,multi_card.kind_badge);
            card.title = blendRect(single_card.title, multi_card.title);
            card.meta = blendRect(single_card.meta, multi_card.meta);
            card.checkbox = multi_card.checkbox;
        }
        const float width_delta = surface_width - static_cast<float>(metrics.width);
        card.card.right += width_delta;
        card.title.right += width_delta;
        card.meta.right += width_delta;
        card.time.left += width_delta;
        card.time.right += width_delta;
        card.menu.left += width_delta;
        card.menu.right += width_delta;
        card.expand.left += width_delta;
        card.expand.right += width_delta;
        card.card.bottom += expanded_height;

        render_target_->PushAxisAlignedClip(Rect(card.card.left,card.card.top,card.card.right,
            card.card.top+ItemScrollHeight(item_index)),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        // 悬停：55% 深纸（当前项不铺）
        if (hovered && !selected) {
            brush->SetColor(D2D1::ColorF(palette.paper_hover, 0.55f * hover));
            render_target_->FillRoundedRectangle(RoundRect(card.card, 9), brush);
        }
        // 行间发丝线：左端从 52px 起；当前行、其上一行、末行不画
        const bool hide_line = selected || (item_index + 1 == selected_index_) ||
                               (item_index + 1 == static_cast<int>(items_.size()));
        if (!hide_line) {
            hairline(card.card.left + 52.0f, card.card.bottom, card.card.right - 10.0f);
        }

        // A6 多选勾选框：15px 圆角方框，整列从左侧滑入
        if (multi_select_ || multi_motion_active) {
            const float checkbox_opacity =
                multi_motion_active ? PopupMultiSelectCheckboxOpacity(multi_motion, multi_entering) : 1.0f;
            const float checkbox_offset =
                multi_motion_active ? PopupMultiSelectCheckboxOffset(multi_motion, multi_entering) : 0.0f;
            const UiRect checkbox{card.checkbox.left + checkbox_offset, card.checkbox.top,
                                  card.checkbox.right + checkbox_offset, card.checkbox.bottom};
            brush->SetOpacity(checkbox_opacity);
            brush->SetColor(D2D1::ColorF(checked ? palette.text : palette.panel_fill,
                                         checked ? 0.94f : (palette.dark ? 0.30f : 0.92f)));
            render_target_->FillRoundedRectangle(RoundRect(checkbox, 4), brush);
            brush->SetColor(D2D1::ColorF(checked ? palette.text : palette.text_tertiary, checked ? 0.94f : 0.66f));
            render_target_->DrawRoundedRectangle(RoundRect(checkbox, 4), brush, 1.2f);
            if (checked) {
                brush->SetColor(D2D1::ColorF(palette.window_tint, 0.98f));
                render_target_->DrawLine(D2D1::Point2F(checkbox.left + 3.2f, checkbox.top + 7.6f),
                                         D2D1::Point2F(checkbox.left + 6.2f, checkbox.top + 10.6f), brush, 1.7f);
                render_target_->DrawLine(D2D1::Point2F(checkbox.left + 6.2f, checkbox.top + 10.6f),
                                         D2D1::Point2F(checkbox.right - 3.2f, checkbox.top + 4.4f), brush, 1.7f);
            }
            brush->SetOpacity(1.0f);
        }

        // 媒体列：30×30 类型徽章 / 34px 缩略图 / 文件图标
        const UiRect media_slot = card.image_preview;
        const bool fast_interaction =
            PopupIsFastMediaInteraction(moving_window_, resizing_window_, view_switch_active);
        const bool load_preview = PopupShouldLoadImagePreview(fast_interaction);
        const auto drawImagePreview = [&](const std::filesystem::path& path, const UiRect& bounds) {
            brush->SetColor(D2D1::ColorF(palette.panel_fill, palette.dark ? 0.94f : 0.99f));
            render_target_->FillRoundedRectangle(RoundRect(bounds, 6), brush);
            brush->SetColor(D2D1::ColorF(palette.text, 0.16f));
            render_target_->DrawRoundedRectangle(RoundRect(bounds, 6), brush, 1.0f);
            ID2D1Bitmap* bitmap = load_preview ? (item.kind==ClipboardKind::Files
                ? LoadImageFilePreviewBitmap(path) : LoadImagePreviewBitmap(path)) : CachedImagePreviewBitmap(path);
            if (PopupShouldDrawCachedMediaDuringFastInteraction(fast_interaction, bitmap != nullptr) && bitmap) {
                const auto size = bitmap->GetSize();
                const auto fitted = FitImageRectToBounds(size.width, size.height, ShrinkRect(bounds, 3.0f, 3.0f));
                render_target_->DrawBitmap(bitmap, Rect(fitted), 0.98f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        };
        if (item.kind == ClipboardKind::Image) {
            drawImagePreview(item.payload_path, CenteredRect(media_slot, 34.0f, 34.0f));
        } else if (item.kind == ClipboardKind::Files && !item.files.empty()) {
            const auto& first_file = item.files.front();
            if (PopupFileCanUseImagePreview(first_file)) {
                drawImagePreview(first_file, CenteredRect(media_slot, 34.0f, 34.0f));
            } else {
                // 文件：带折角的纸片图标
                const UiRect sheet = CenteredRect(media_slot, 27.0f, 33.0f);
                brush->SetColor(D2D1::ColorF(palette.panel_fill, palette.dark ? 0.94f : 0.99f));
                render_target_->FillRoundedRectangle(
                    RoundRect(sheet.left, sheet.top, sheet.right, sheet.bottom, 3.0f), brush);
                brush->SetColor(D2D1::ColorF(palette.text, 0.16f));
                render_target_->DrawRoundedRectangle(
                    RoundRect(sheet.left, sheet.top, sheet.right, sheet.bottom, 3.0f), brush, 1.0f);
                brush->SetColor(D2D1::ColorF(palette.paper_hover));
                render_target_->FillRoundedRectangle(RoundRect(sheet.right-9,sheet.top,sheet.right,sheet.top+9,2),brush);
                brush->SetColor(D2D1::ColorF(palette.text,0.16f));
                render_target_->DrawLine(D2D1::Point2F(sheet.right-9,sheet.top),D2D1::Point2F(sheet.right-9,sheet.top+9),brush,1);
                render_target_->DrawLine(D2D1::Point2F(sheet.right-9,sheet.top+9),D2D1::Point2F(sheet.right,sheet.top+9),brush,1);
                const auto extension = std::filesystem::path(first_file).extension().wstring();
                std::wstring label = extension.size() > 1 ? extension.substr(1,3) : std::wstring(L"FILE");
                for (auto& ch : label) ch=static_cast<wchar_t>(towupper(ch));
                drawAlignedText(label,extension_format_,{sheet.left,sheet.bottom-14,sheet.right,sheet.bottom-2},palette.muted,1,DWRITE_TEXT_ALIGNMENT_CENTER);
            }
        } else {
            // 文本 / 链接：30×30 深纸徽章 + 原型线性图标
            const UiRect badge = card.kind_badge;
            brush->SetColor(D2D1::ColorF(selected && !multi_select_ ? palette.window_tint : palette.paper_hover, 1.0f));
            render_target_->FillRoundedRectangle(RoundRect(badge, 8), brush);
            DrawIcon(item.kind == ClipboardKind::Link ? IconId::LinkKind : IconId::TextKind,
                     CenteredRect(badge, 14.0f, 14.0f), 1.0f);
        }

        // 置顶：类型图标右上角朱砂实心圆 + 白色图钉
        const auto badge_motion=badge_transitions_.find(item.id);
        const bool pin_anim=badge_motion!=badge_transitions_.end() && badge_motion->second.pin;
        const bool fav_anim=badge_motion!=badge_transitions_.end() && !badge_motion->second.pin;
        if (item.is_pinned || pin_anim) {
            D2D1_MATRIX_3X2_F pin_transform{};render_target_->GetTransform(&pin_transform);
            const UiRect badge = CenteredRect(media_slot, 30.0f, 30.0f);
            const float pin_cx = card.card.left + 41.0f + (multi_select_ ? 28.0f : 0.0f);
            const float pin_cy = card.card.top + 13.0f;
            if(pin_anim) {
                const float raw=PopupTimedMotionProgress(static_cast<double>(GetTickCount64())-badge_motion->second.start_ms,280.0,animations_enabled_);
                const float p=PopupMotionProgress(raw);
                const float scale=badge_motion->second.to?p:1-p;
                render_target_->SetTransform(D2D1::Matrix3x2F::Scale(scale,scale,D2D1::Point2F(pin_cx,pin_cy))*
                    D2D1::Matrix3x2F::Rotation(-60*(1-p),D2D1::Point2F(pin_cx,pin_cy))*pin_transform);
            }
            brush->SetColor(D2D1::ColorF(selected?palette.paper_selected:palette.window_tint, 1.0f));
            render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(pin_cx, pin_cy), 9.0f, 9.0f), brush);
            brush->SetColor(D2D1::ColorF(palette.danger, 0.98f));
            render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(pin_cx, pin_cy), 8.0f, 8.0f), brush);
            glyph(InkGlyph::PinFill,{pin_cx-4.5f,pin_cy-4.5f,pin_cx+4.5f,pin_cy+4.5f},0xFFFFFF);
            render_target_->SetTransform(pin_transform);
        }

        // 标题：搜索命中处加粗 + 朱砂下划线
        const auto title = ItemTitle(item);
        const bool is_code = item.kind == ClipboardKind::Text && (title.starts_with(L"cmake ") || title.starts_with(L"npm ") ||
            title.starts_with(L"git ") || title.starts_with(L"SELECT ") || title.starts_with(L"ctest ") || title.starts_with(L"const "));
        auto* row_format = is_code ? code_format_ : body_format_;
        const UiRect title_rect = card.title;
        const auto matches = query_.empty() ? std::vector<std::pair<size_t, size_t>>{}
                                            : FindSearchMatches(title, query_);
        if (!matches.empty() && dwrite_factory_ && title_format_) {
            IDWriteTextLayout* text_layout = nullptr;
            if (SUCCEEDED(dwrite_factory_->CreateTextLayout(title.c_str(), static_cast<UINT32>(title.size()),
                                                            row_format, title_rect.Width(), title_rect.Height(),
                                                            &text_layout)) &&
                text_layout) {
                for (const auto& [start, length] : matches) {
                    text_layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD,
                                               DWRITE_TEXT_RANGE{static_cast<UINT32>(start),
                                                                 static_cast<UINT32>(length)});
                }
                brush->SetColor(D2D1::ColorF(palette.text, selected ? 0.99f : 0.94f));
                render_target_->DrawTextLayout(D2D1::Point2F(title_rect.left, title_rect.top), text_layout, brush);
                for (const auto& [start, length] : matches) {
                    UINT32 count = 0;
                    text_layout->HitTestTextRange(static_cast<UINT32>(start), static_cast<UINT32>(length), 0.0f, 0.0f,
                                                  nullptr, 0, &count);
                    if (count == 0 || count > 16) {
                        continue;
                    }
                    DWRITE_HIT_TEST_METRICS hit[16]{};
                    if (SUCCEEDED(text_layout->HitTestTextRange(static_cast<UINT32>(start),
                                                                static_cast<UINT32>(length), 0.0f, 0.0f, hit, count,
                                                                &count))) {
                        brush->SetColor(D2D1::ColorF(palette.danger, 0.92f));
                        for (UINT32 index = 0; index < count; ++index) {
                            const float underline_y = title_rect.top + hit[index].top + hit[index].height - 1.0f;
                            render_target_->DrawLine(
                                D2D1::Point2F(title_rect.left + hit[index].left, underline_y),
                                D2D1::Point2F(title_rect.left + hit[index].left + hit[index].width, underline_y),
                                brush, 1.5f);
                        }
                    }
                }
                ReleasePtr(text_layout);
            }
        } else {
            drawText(title, row_format, title_rect, palette.text, 1.0f);
        }

        // 副行：类型 · 字数（备注带笔形小图标）
        const auto meta = ItemMetaSummary(item);
        const float meta_width = std::min(textWidth(meta, small_format_), card.meta.Width());
        drawText(meta, small_format_, UiRect{card.meta.left,card.meta.top,card.meta.left+meta_width,card.meta.bottom}, palette.text_tertiary, 1.0f);
        if (!item.note.empty()) {
            const auto note_text = PopupNotePreviewText(item.note);
            const float note_x = card.meta.left + meta_width + 14.0f;
            const float pen_y = card.meta.top + 5.0f;
            brush->SetColor(D2D1::ColorF(palette.text_tertiary, 0.86f));
            render_target_->DrawLine(D2D1::Point2F(note_x, pen_y + 6.0f), D2D1::Point2F(note_x + 5.0f, pen_y + 1.0f),
                                     brush, 1.1f);
            render_target_->DrawLine(D2D1::Point2F(note_x - 0.6f, pen_y + 6.6f),
                                     D2D1::Point2F(note_x + 1.0f, pen_y + 5.0f), brush, 1.1f);
            drawText(note_text, small_format_,
                     UiRect{note_x + 8.0f, card.meta.top, card.meta.right, card.meta.bottom}, palette.muted, 0.90f);
        }

        // 时间（等宽）+ 展开箭头；收藏项给书签带留位置
        const auto time = ItemTime(item, time_context);
        const float time_shift = item.is_favorite ? 14.0f : 0.0f;
        drawAlignedText(time, mono_format_,
                 UiRect{card.time.left - time_shift, card.time.top, card.time.right - time_shift, card.time.bottom},
                 palette.text_tertiary, 1.0f, DWRITE_TEXT_ALIGNMENT_TRAILING);
        float arrow_progress = 1.0f;
        if (arrow_motion_item_id_ && *arrow_motion_item_id_ == item.id) {
            arrow_progress = PopupMotionProgress(arrow_motion_progress_);
        }
        const float ex = (card.expand.left + card.expand.right) * 0.5f;
        const float ey = (card.expand.top + card.expand.bottom) * 0.5f;
        // 箭头旋转：展开时从 0° 连续转到 180°（而不是在 90° 处突然翻面）。
        const float arrow_radians = kPi * (expanded ? arrow_progress : 1.0f - arrow_progress);
        const auto arrow_left = RotatePoint(ex - 4.5f, ey - 2.5f, ex, ey, arrow_radians);
        const auto arrow_mid = RotatePoint(ex, ey + 2.5f, ex, ey, arrow_radians);
        const auto arrow_right = RotatePoint(ex + 4.5f, ey - 2.5f, ex, ey, arrow_radians);
        brush->SetColor(D2D1::ColorF(expanded ? palette.text : palette.text_tertiary,
                                     expanded ? 0.92f : 0.70f));
        render_target_->DrawLine(arrow_left, arrow_mid, brush, 1.4f);
        render_target_->DrawLine(arrow_right, arrow_mid, brush, 1.4f);

        // 收藏：右上角朱砂书签带（宽 8、高 13、底边 V 形缺口）
        if (item.is_favorite || fav_anim) {
            float ribbon_height=13;
            if(fav_anim) {
                const float p=PopupMotionProgress(PopupTimedMotionProgress(static_cast<double>(GetTickCount64())-badge_motion->second.start_ms,280.0,animations_enabled_));
                ribbon_height*=badge_motion->second.to?p:1-p;
            }
            const float ribbon_right = card.card.right - 14.0f;
            const float ribbon_left = ribbon_right - 8.0f;
            const float ribbon_bottom = card.card.top + std::max(3.0f,ribbon_height);
            brush->SetColor(D2D1::ColorF(palette.danger, 0.96f));
            render_target_->FillRectangle(Rect(ribbon_left, card.card.top, ribbon_right, ribbon_bottom - 3.0f), brush);
            // 左半：斜边从左上角接到右下；右半是它的镜像。
            const float notch_width = (ribbon_right - ribbon_left) * 0.5f;
            fillRibbonNotch(ribbon_left, ribbon_bottom - 3.0f, notch_width, 3.0f, false, palette.danger, 0.96f);
            fillRibbonNotch(ribbon_right - notch_width, ribbon_bottom - 3.0f, notch_width, 3.0f, true,
                            palette.danger, 0.96f);
        }

        // 展开详情：深纸圆角块；图片显示 120px 大图
        if (expanded_height > 0.5f) {
            render_target_->PushAxisAlignedClip(Rect(card.title.left, card.meta.bottom, card.card.right - 12.0f,
                card.card.bottom - 4.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            const UiRect detail_block{card.title.left, card.meta.bottom + 8.0f, card.title.right,
                                      card.meta.bottom + 8.0f + std::max(0.0f,expanded_height - 8.0f)};
            brush->SetColor(D2D1::ColorF(selected ? palette.window_tint : palette.paper_hover, 1.0f));
            render_target_->FillRoundedRectangle(RoundRect(detail_block, 6), brush);
            const bool image_item = item.kind == ClipboardKind::Image && !item.payload_path.empty();
            const bool image_file_item = false;
            const auto detail = ItemDetailText(item);
            if (image_item || image_file_item) {
                const float image_size = 120.0f;
                const UiRect image_rect{detail_block.left, detail_block.top,
                                        detail_block.left + image_size,
                                        detail_block.top + image_size};
                const auto& path = image_item ? item.payload_path : std::filesystem::path(item.files.front());
                brush->SetColor(D2D1::ColorF(palette.panel_fill, palette.dark ? 0.94f : 0.99f));
                render_target_->FillRoundedRectangle(RoundRect(image_rect, 7), brush);
                brush->SetColor(D2D1::ColorF(palette.text, 0.16f));
                render_target_->DrawRoundedRectangle(RoundRect(image_rect, 7), brush, 1.0f);
                ID2D1Bitmap* bitmap = nullptr;
                if (image_item) {
                    bitmap = load_preview ? LoadImagePreviewBitmap(path) : CachedImagePreviewBitmap(path);
                } else {
                    bitmap = load_preview ? LoadImageFilePreviewBitmap(item.files.front())
                                          : CachedImagePreviewBitmap(item.files.front());
                }
                if (PopupShouldDrawCachedMediaDuringFastInteraction(fast_interaction, bitmap != nullptr) && bitmap) {
                    const auto size = bitmap->GetSize();
                    const auto fitted =
                        FitImageRectToBounds(size.width, size.height, ShrinkRect(image_rect, 4.0f, 4.0f));
                    render_target_->DrawBitmap(bitmap, Rect(fitted), 0.98f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                }

            } else {
                render_target_->PushAxisAlignedClip(Rect(detail_block.left+10,detail_block.top+8,detail_block.right-10,detail_block.bottom-6),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                drawText(detail, detail_format_,
                         UiRect{detail_block.left + 10.0f, detail_block.top + 8.0f-detail_scroll_, detail_block.right - 10.0f,
                                detail_block.top + 8192.0f-detail_scroll_}, palette.muted, 1.0f);
                render_target_->PopAxisAlignedClip();
            }
            render_target_->PopAxisAlignedClip();
        }
        if (stamp_item_id_ && *stamp_item_id_ == item.id) {
            const float elapsed = static_cast<float>(GetTickCount64() - stamp_start_ms_);
            const float appear = PopupMotionProgress(std::clamp(elapsed / 260.0f, 0.0f, 1.0f));
            const float alpha = elapsed > 700.0f ? 1.0f - std::clamp((elapsed - 700.0f) / 260.0f, 0.0f, 1.0f) : appear;
            const UiRect stamp{card.card.right - 90.0f, card.card.top + 11.0f, card.card.right - 42.0f, card.card.top + 34.0f};
            D2D1_MATRIX_3X2_F saved{}; render_target_->GetTransform(&saved);
            const auto center = D2D1::Point2F((stamp.left + stamp.right) / 2, (stamp.top + stamp.bottom) / 2);
            render_target_->SetTransform(D2D1::Matrix3x2F::Scale(1.0f + 0.7f * (1-appear), 1.0f + 0.7f*(1-appear), center) *
                D2D1::Matrix3x2F::Rotation(-8.0f - 6.0f*(1-appear), center) * saved);
            brush->SetColor(D2D1::ColorF(palette.window_tint, alpha));
            render_target_->FillRoundedRectangle(RoundRect(stamp, 4), brush);
            brush->SetColor(D2D1::ColorF(palette.danger, alpha));
            render_target_->DrawRoundedRectangle(RoundRect(stamp, 4), brush, 1.5f);
            drawAlignedText(L"已贴", small_format_, stamp, palette.danger, alpha, DWRITE_TEXT_ALIGNMENT_CENTER);
            render_target_->SetTransform(saved);
        }
        render_target_->PopAxisAlignedClip();
        if (row_layer_active) render_target_->PopLayer();
        ReleasePtr(row_layer);
        render_target_->SetTransform(row_transform);
    }

    // ---------- A8 空状态 ----------
    if (items_.empty()) {
        const float empty_top = PopupListTop() + 90.0f;
        const auto empty_text = PopupEmptyMessage(view_mode_ == ViewMode::Favorites,
                                                  active_favorite_group_id_ ? ActiveFavoriteGroupLabel() : L"");
        drawAlignedText(!query_.empty() || HasActiveFilter() ? L"没有匹配的记录" : empty_text, button_format_,
                 UiRect{content_left + 24.0f, empty_top + 34.0f, surface_width - content_left - 24.0f,
                        empty_top + 58.0f},
                 palette.text_tertiary, 1.0f, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    if (view_switch_active) {
        render_target_->SetTransform(list_transform);
    }
    render_target_->PopAxisAlignedClip();

    // ---------- A7 滚动条：4px（悬停/拖动 6px）+ 回到顶部圆形浮层 ----------
    const float content_height = TotalScrollHeight();
    const float viewport_height = ViewportScrollHeight();
    if (content_height > viewport_height && !items_.empty()) {
        const bool scrollbar_hovered =
            hover_action_ == UiAction::Scrollbar ||
            Contains(PopupScrollbarHitRectForSize(popup_logical_width_, popup_logical_height_), hover_point_);
        const bool emphasized = scrollbar_hovered || dragging_scrollbar_;
        const float thumb_width = emphasized ? 6.0f : 4.0f;
        const float track_right = surface_width - 3.0f;
        const float track_left = track_right - thumb_width;
        const float track_top = PopupListTop() + 2.0f;
        const float track_bottom = PopupListBottomForHeight(popup_logical_height_) - 2.0f;
        const float track_height = std::max(1.0f, track_bottom - track_top);
        const float thumb_height = std::max(32.0f, track_height * viewport_height / content_height);
        const float max_offset = std::max(1.0f, content_height - viewport_height);
        const float thumb_top =
            track_top + (track_height - thumb_height) * ClampSmoothScrollOffset(scroll_offset_) / max_offset;
        const float thumb_opacity = emphasized ? (dragging_scrollbar_ ? 0.70f : 0.40f + 0.30f * hover_progress_)
                                               : Contains(list_clip, hover_point_) ? 0.40f : 0.0f;
        brush->SetColor(D2D1::ColorF(palette.text_tertiary, thumb_opacity));
        render_target_->FillRoundedRectangle(
            RoundRect(track_left, thumb_top, track_left + thumb_width, thumb_top + thumb_height, thumb_width * 0.5f),
            brush);
    }
    // 回顶部滚动进行中按钮保持可见（否则越过阈值时会闪烁消失）。
    const bool scroll_to_top_visible =
        PopupScrollToTopButtonVisible(scroll_offset_) || smooth_scroll_active_;
    if (scroll_to_top_visible) {
        const auto button = PopupScrollToTopButtonRectForSize(popup_logical_width_, popup_logical_height_);
        const bool button_hovered = hover_action_ == UiAction::ScrollToTop || Contains(button, hover_point_);
        const float button_hover = button_hovered ? hover_progress_ : 0.0f;
        drawEdgeShadow(button, 15.0f, 0.22f + 0.10f * button_hover, 1.4f);
        brush->SetColor(D2D1::ColorF(palette.panel_fill, 0.96f));
        render_target_->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F((button.left + button.right) * 0.5f, (button.top + button.bottom) * 0.5f),
                          15.0f, 15.0f),
            brush);
        brush->SetColor(D2D1::ColorF(palette.text, 0.16f));
        render_target_->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F((button.left + button.right) * 0.5f, (button.top + button.bottom) * 0.5f),
                          15.0f, 15.0f),
            brush, 1.0f);
        const float cx = (button.left + button.right) * 0.5f;
        const float cy = (button.top + button.bottom) * 0.5f - 1.0f * button_hover;
        brush->SetColor(D2D1::ColorF(button_hovered ? palette.text : palette.muted, button_hovered ? 0.96f : 0.82f));
        render_target_->DrawLine(D2D1::Point2F(cx, cy + 5.0f), D2D1::Point2F(cx, cy - 5.0f), brush, 1.6f);
        render_target_->DrawLine(D2D1::Point2F(cx - 4.0f, cy - 1.0f), D2D1::Point2F(cx, cy - 5.0f), brush, 1.6f);
        render_target_->DrawLine(D2D1::Point2F(cx + 4.0f, cy - 1.0f), D2D1::Point2F(cx, cy - 5.0f), brush, 1.6f);
    }

    // ---------- A9 页脚：监听状态 + 快捷键提示 ----------
    const auto footer = PopupFooterRectForSize(popup_logical_width_, popup_logical_height_);
    hairline(0.0f, footer.top, surface_width);
    const float footer_center_y = (footer.top + footer.bottom) * 0.5f;
    const uint32_t status_color = palette.dark ? 0x7FAE7A : 0x5F8C5B;
    brush->SetColor(D2D1::ColorF(paused_ ? palette.text_tertiary : status_color, 1.0f));
    render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(content_left + 3.0f, footer_center_y), 2.6f, 2.6f),
                                brush);
    if (!paused_) {
        brush->SetColor(D2D1::ColorF(status_color, 0.18f));
        render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(content_left + 3.0f, footer_center_y), 6.0f, 6.0f),
                                    brush);
    }
    drawText(paused_ ? L"已暂停" : L"监听中", mono_format_,
             UiRect{content_left + 12.0f, footer.top, content_left + 80.0f, footer.bottom},
             paused_ ? palette.text_tertiary : palette.muted, 0.94f);
    drawAlignedText(L"↵ 粘贴    右键 更多    esc",mono_format_,
        {surface_width-174,footer.top,surface_width-18,footer.bottom},palette.text_tertiary,1,DWRITE_TEXT_ALIGNMENT_TRAILING);

    // ---------- C 收藏夹菜单：宽 180、行高 30、圆角 12 ----------
    if (favorite_group_menu_open_ || closing_favorite_group_menu_) {
        const auto menu = BuildPopupFavoriteGroupMenuLayout(favorite_groups_.size());
        D2D1_MATRIX_3X2_F previous_transform{};
        render_target_->GetTransform(&previous_transform);
        const bool exiting = motion_target_ == PopupMotionTarget::FavoriteMenu && motion_direction_ < 0;
        const float motion = motionProgressFor(PopupMotionTarget::FavoriteMenu);
        const auto offset = exiting ? PopupMotionExitOffsetFromTrigger(tabs.favorite_group, menu.panel,
                                                                      motion_progress_, 10.0f)
                                    : PopupMotionEnterOffsetFromTrigger(tabs.favorite_group, menu.panel, motion, 10.0f);
        render_target_->SetTransform(D2D1::Matrix3x2F::Translation(offset.x, offset.y));
        drawEdgeShadow(menu.panel, 12.0f, 0.42f, 2.0f);
        brush->SetColor(ColorWithAlpha(palette.panel_fill, palette.dark ? 0.98f : 0.99f));
        render_target_->FillRoundedRectangle(RoundRect(menu.panel, 12), brush);
        drawInkPanelFrame(menu.panel, 12.0f, 0.86f, false);

        auto menuRowHover = [&](PopupFavoriteGroupMenuTarget target, size_t index = 0) {
            if (hover_favorite_group_menu_hit_.target != target) {
                return 0.0f;
            }
            if (target == PopupFavoriteGroupMenuTarget::Group && hover_favorite_group_menu_hit_.group_index != index) {
                return 0.0f;
            }
            return hover_progress_;
        };
        auto drawMenuRow = [&](const UiRect& rect, const std::wstring& label, bool active, float hover,
                               bool group_row, size_t group_index, int count) {
            if (hover > 0.0f) {
                brush->SetColor(D2D1::ColorF(palette.paper_hover, 0.94f * hover));
                render_target_->FillRoundedRectangle(RoundRect(rect, 7), brush);
            }
            if (active) {
                // 当前组：前面画对勾并加粗
                brush->SetColor(D2D1::ColorF(palette.text, 0.92f));
                const float check_x = rect.left + 10.0f;
                const float check_y = (rect.top + rect.bottom) * 0.5f;
                render_target_->DrawLine(D2D1::Point2F(check_x - 3.0f, check_y),
                                         D2D1::Point2F(check_x - 1.0f, check_y + 2.4f), brush, 1.5f);
                render_target_->DrawLine(D2D1::Point2F(check_x - 1.0f, check_y + 2.4f),
                                         D2D1::Point2F(check_x + 3.4f, check_y - 2.4f), brush, 1.5f);
            }
            drawText(label, active ? title_format_ : button_format_,
                     UiRect{rect.left + 20.0f, rect.top, group_row ? rect.right - 52.0f : rect.right - 12.0f,
                            rect.bottom},
                     active ? palette.text : palette.muted, active ? 0.98f : 0.94f);
            if (group_row) {
                const auto delete_rect = PopupFavoriteGroupMenuDeleteRect(rect);
                const bool delete_hover = hover_favorite_group_menu_hit_.target ==
                                              PopupFavoriteGroupMenuTarget::DeleteGroup &&
                                          hover_favorite_group_menu_hit_.group_index == group_index;
                // 条目数（等宽）
                drawText(std::to_wstring(count), mono_format_,
                         UiRect{delete_rect.left - 34.0f, rect.top, delete_rect.left - 4.0f, rect.bottom},
                         palette.text_tertiary, 0.90f);
                if (delete_hover) {
                    brush->SetColor(D2D1::ColorF(palette.danger, 0.12f));
                    render_target_->FillRoundedRectangle(RoundRect(delete_rect, 5), brush);
                }
                brush->SetColor(D2D1::ColorF(delete_hover ? palette.danger : palette.text_tertiary,
                                             delete_hover ? 0.94f : 0.68f));
                const float cx = (delete_rect.left + delete_rect.right) * 0.5f;
                const float cy = (delete_rect.top + delete_rect.bottom) * 0.5f;
                render_target_->DrawLine(D2D1::Point2F(cx - 3.2f, cy - 3.2f), D2D1::Point2F(cx + 3.2f, cy + 3.2f),
                                         brush, 1.2f);
                render_target_->DrawLine(D2D1::Point2F(cx + 3.2f, cy - 3.2f), D2D1::Point2F(cx - 3.2f, cy + 3.2f),
                                         brush, 1.2f);
            }
        };

        int all_favorites_count = 0;
        for (const auto& [_, count] : favorite_group_counts_) {
            all_favorites_count += count;
        }
        drawMenuRow(menu.all_favorites, L"全部收藏", !active_favorite_group_id_,
                    menuRowHover(PopupFavoriteGroupMenuTarget::AllFavorites), false, 0, all_favorites_count);
        brush->SetColor(D2D1::ColorF(palette.text, 0.09f));
        render_target_->FillRectangle(Rect(menu.first_divider), brush);

        const size_t visible_groups = PopupFavoriteGroupMenuVisibleGroupCount(favorite_groups_.size());
        for (size_t index = 0; index < visible_groups; ++index) {
            const auto row = PopupFavoriteGroupMenuGroupRect(menu, index);
            const bool active =
                active_favorite_group_id_ && favorite_groups_[index].id == *active_favorite_group_id_;
            drawMenuRow(row, favorite_groups_[index].name, active,
                        menuRowHover(PopupFavoriteGroupMenuTarget::Group, index), true, index,
                        FavoriteGroupItemCount(favorite_groups_[index].id));
        }
        brush->SetColor(D2D1::ColorF(palette.text, 0.09f));
        render_target_->FillRectangle(Rect(menu.second_divider), brush);
        drawMenuRow(menu.new_group, L"＋新建收藏夹…", false,
                    menuRowHover(PopupFavoriteGroupMenuTarget::NewGroup), false, 0, 0);
        render_target_->SetTransform(previous_transform);
    }

    // ---------- C 删除收藏夹确认：朱砂圆形警示 + 遮罩 ----------
    if (pending_favorite_group_delete_ || closing_delete_confirm_) {
        const auto confirm = FavoriteGroupDeleteConfirmPanelRect(popup_logical_width_);
        const auto delete_button = FavoriteGroupDeleteConfirmDeleteRect(confirm);
        const auto cancel_button = FavoriteGroupDeleteConfirmCancelRect(confirm);
        UiRect confirm_trigger = tabs.favorite_group;
        const auto menu = BuildPopupFavoriteGroupMenuLayout(favorite_groups_.size());
        const PendingFavoriteGroupDelete confirm_data =
            pending_favorite_group_delete_ ? *pending_favorite_group_delete_ : closing_delete_confirm_snapshot_;
        if (confirm_data.group_index < PopupFavoriteGroupMenuVisibleGroupCount(favorite_groups_.size())) {
            confirm_trigger = PopupFavoriteGroupMenuDeleteRect(
                PopupFavoriteGroupMenuGroupRect(menu, confirm_data.group_index));
        }
        D2D1_MATRIX_3X2_F previous_transform{};
        render_target_->GetTransform(&previous_transform);
        const bool exiting = motion_target_ == PopupMotionTarget::DeleteConfirm && motion_direction_ < 0;
        const float motion = motionProgressFor(PopupMotionTarget::DeleteConfirm);
        const auto offset = exiting
                                ? PopupMotionExitOffsetFromTrigger(confirm_trigger, confirm, motion_progress_, 12.0f)
                                : PopupMotionEnterOffsetFromTrigger(confirm_trigger, confirm, motion, 12.0f);
        render_target_->SetTransform(D2D1::Matrix3x2F::Translation(offset.x, offset.y));
        // 半透明遮罩
        brush->SetColor(D2D1::ColorF(palette.dark ? 0x000000 : 0x1C1B19, palette.dark ? 0.40f : 0.12f));
        render_target_->FillRoundedRectangle(RoundRect(window_surface_rect, PopupWindowEdgeCornerRadius()), brush);
        drawEdgeShadow(confirm, 12.0f, 0.46f, 2.0f);
        brush->SetColor(ColorWithAlpha(palette.panel_fill, 0.99f));
        render_target_->FillRoundedRectangle(RoundRect(confirm, 12), brush);
        drawInkPanelFrame(confirm, 12.0f, 0.90f, false);
        // 朱砂圆形警示标记
        const float warn_cx = confirm.left + 26.0f;
        const float warn_cy = confirm.top + 26.0f;
        brush->SetColor(D2D1::ColorF(palette.danger, 0.12f));
        render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(warn_cx, warn_cy), 11.0f, 11.0f), brush);
        brush->SetColor(D2D1::ColorF(palette.danger, 0.94f));
        render_target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(warn_cx, warn_cy), 11.0f, 11.0f), brush, 1.4f);
        render_target_->DrawLine(D2D1::Point2F(warn_cx, warn_cy - 5.0f), D2D1::Point2F(warn_cx, warn_cy + 1.0f),
                                 brush, 1.5f);
        render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(warn_cx, warn_cy + 4.4f), 1.1f, 1.1f), brush);
        const std::wstring title_text = L"删除收藏夹「" + confirm_data.name + L"」？";
        drawText(title_text, title_format_,
                 UiRect{confirm.left + 46.0f, confirm.top + 14.0f, confirm.right - 16.0f, confirm.top + 40.0f},
                 palette.text, 0.98f);
        const std::wstring message =
            L"夹内 " + std::to_wstring(FavoriteGroupItemCount(confirm_data.id)) + L" 条收藏会一并删除，此操作无法撤销。";
        drawText(message, small_format_,
                 UiRect{confirm.left + 16.0f, confirm.top + 48.0f, confirm.right - 16.0f, confirm.top + 92.0f},
                 palette.muted, 0.94f);
        const bool delete_hover = hover_delete_confirm_target_ == PopupFavoriteGroupDeleteConfirmTarget::Delete;
        const bool cancel_hover = hover_delete_confirm_target_ == PopupFavoriteGroupDeleteConfirmTarget::Cancel;
        // 取消（描边）/ 删除（朱砂实底）
        if (cancel_hover) {
            brush->SetColor(D2D1::ColorF(palette.paper_hover, 0.96f));
            render_target_->FillRoundedRectangle(RoundRect(cancel_button, 7), brush);
        }
        brush->SetColor(D2D1::ColorF(palette.text, cancel_hover ? 0.28f : 0.16f));
        render_target_->DrawRoundedRectangle(RoundRect(cancel_button, 7), brush, 1.0f);
        drawText(L"取消", button_format_, cancel_button, palette.muted, 0.96f);
        brush->SetColor(D2D1::ColorF(palette.danger, delete_hover ? 1.0f : 0.92f));
        render_target_->FillRoundedRectangle(RoundRect(delete_button, 7), brush);
        drawText(L"删除", button_format_, delete_button, 0xFFFFFF, 0.99f);
        render_target_->SetTransform(previous_transform);
    }

    // ---------- B 筛选面板：类型分段条 + 五段预设 + 仅自定义时展开日历 + 底部匹配计数 ----------
    if (filter_open_ || closing_filter_panel_) {
        const auto filter = CurrentFilterLayout();
        D2D1_MATRIX_3X2_F previous_transform{};
        render_target_->GetTransform(&previous_transform);
        const bool exiting = motion_target_ == PopupMotionTarget::FilterPanel && motion_direction_ < 0;
        const float motion = motionProgressFor(PopupMotionTarget::FilterPanel);
        const auto offset = exiting
                                ? PopupMotionExitOffsetFromTrigger(toolbar.filter, filter.panel, motion_progress_, 16.0f)
                                : PopupMotionEnterOffsetFromTrigger(toolbar.filter, filter.panel, motion, 16.0f);
        render_target_->SetTransform(D2D1::Matrix3x2F::Translation(offset.x, offset.y));
        const float filter_opacity = exiting ? PopupPopoverExitOpacity(motion_progress_)
                                             : (motion_target_ == PopupMotionTarget::FilterPanel
                                                    ? PopupMotionEnterOpacity(motion)
                                                    : 1.0f);
        ID2D1Layer* filter_layer = nullptr;
        const bool filter_layer_active =
            filter_opacity < 0.999f && SUCCEEDED(render_target_->CreateLayer(nullptr, &filter_layer)) &&
            filter_layer != nullptr;
        if (filter_layer_active) {
            render_target_->PushLayer(D2D1::LayerParameters(Rect(filter.panel), nullptr,
                                                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                            D2D1::Matrix3x2F::Identity(), filter_opacity),
                                      filter_layer);
        }
        ScopedD2DLayer filter_layer_scope{render_target_, filter_layer, filter_layer_active};

        drawEdgeShadow(filter.panel, 12.0f, 0.40f, 2.0f);
        brush->SetColor(ColorWithAlpha(palette.panel_fill, 0.99f));
        render_target_->FillRoundedRectangle(RoundRect(filter.panel, 12), brush);
        drawInkPanelFrame(filter.panel, 12.0f, 0.88f, false);

        const auto filterHover = [&](PopupFilterTarget target) {
            return hover_filter_target_ == target ? hover_progress_ : 0.0f;
        };
        const auto hoverBackplate = [&](const UiRect& rect, PopupFilterTarget target, float radius,
                                        bool danger = false) {
            const float hover = filterHover(target);
            if (hover <= 0.0f) {
                return;
            }
            brush->SetColor(D2D1::ColorF(danger ? palette.danger : palette.paper_hover,
                                         danger ? 0.12f * hover : 0.94f * hover));
            render_target_->FillRoundedRectangle(RoundRect(rect, radius), brush);
        };

        // 头部：标题 / 重置（仅有条件时）/ ×
        drawText(L"筛选", title_format_,
                 UiRect{filter.panel.left + 14.0f, filter.panel.top + 12.0f, filter.panel.left + 90.0f,
                        filter.panel.top + 36.0f},
                 palette.text, 0.98f);
        if (HasActiveFilter()) {
            const float reset_hover = filterHover(PopupFilterTarget::Reset);
            if (reset_hover > 0.0f) {
                brush->SetColor(D2D1::ColorF(palette.paper_hover, 0.94f * reset_hover));
                render_target_->FillRoundedRectangle(RoundRect(filter.reset, 5), brush);
            }
            drawText(L"重置", button_format_, filter.reset,
                     reset_hover > 0.0f ? palette.danger : palette.text_tertiary, 0.94f);
        }
        const float close_hover = filterHover(PopupFilterTarget::Close);
        if (close_hover > 0.0f) {
            brush->SetColor(D2D1::ColorF(palette.danger, 0.12f * close_hover));
            render_target_->FillRoundedRectangle(RoundRect(filter.close, 5), brush);
        }
        brush->SetColor(D2D1::ColorF(close_hover > 0.0f ? palette.danger : palette.text_tertiary,
                                     close_hover > 0.0f ? 0.94f : 0.80f));
        {
            const float cx = (filter.close.left + filter.close.right) * 0.5f;
            const float cy = (filter.close.top + filter.close.bottom) * 0.5f;
            render_target_->DrawLine(D2D1::Point2F(cx - 3.6f, cy - 3.6f), D2D1::Point2F(cx + 3.6f, cy + 3.6f), brush,
                                     1.3f);
            render_target_->DrawLine(D2D1::Point2F(cx + 3.6f, cy - 3.6f), D2D1::Point2F(cx - 3.6f, cy + 3.6f), brush,
                                     1.3f);
        }

        // 类型：标签行（右侧显示已选类型）+ 四等分分段条
        drawText(L"类型", small_format_,
                 UiRect{filter.type_section.left, filter.type_section.top, filter.type_section.left + 60.0f,
                        filter.type_section.bottom},
                 palette.text_tertiary, 0.94f);
        std::wstring kind_hint = L"全部";
        if (!filter_kinds_.empty()) {
            kind_hint.clear();
            for (const auto kind : filter_kinds_) {
                if (!kind_hint.empty()) {
                    kind_hint += L" · ";
                }
                kind_hint += KindLabel(static_cast<int>(kind));
            }
        }
        drawAlignedText(kind_hint, small_format_,
                 UiRect{filter.type_section.right - 150.0f, filter.type_section.top, filter.type_section.right,
                        filter.type_section.bottom},
                 palette.text_tertiary, 1.0f,DWRITE_TEXT_ALIGNMENT_TRAILING);
        brush->SetColor(D2D1::ColorF(palette.paper_hover, palette.dark ? 0.92f : 0.96f));
        render_target_->FillRoundedRectangle(RoundRect(filter.type_bar, 8), brush);
        auto drawSegment = [&](const UiRect& rect, const wchar_t* label, bool active, PopupFilterTarget target) {
            const float hover = filterHover(target);
            if (active) {
                drawEdgeShadow(rect, 6.0f, 0.20f, 1.0f);
                brush->SetColor(D2D1::ColorF(palette.panel_fill, palette.dark ? 0.98f : 1.0f));
                render_target_->FillRoundedRectangle(RoundRect(rect, 6), brush);
                brush->SetColor(D2D1::ColorF(palette.text, 0.10f));
                render_target_->DrawRoundedRectangle(RoundRect(rect, 6), brush, 1.0f);
            } else if (hover > 0.0f) {
                brush->SetColor(D2D1::ColorF(palette.panel_fill, 0.55f * hover));
                render_target_->FillRoundedRectangle(RoundRect(rect, 6), brush);
            }
            InkGlyph segment_icon=InkGlyph::Text;
            const bool type=target==PopupFilterTarget::TextChip || target==PopupFilterTarget::ImageChip ||
                target==PopupFilterTarget::FileChip || target==PopupFilterTarget::LinkChip;
            if(target==PopupFilterTarget::ImageChip)segment_icon=InkGlyph::Image;
            if(target==PopupFilterTarget::FileChip)segment_icon=InkGlyph::File;
            if(target==PopupFilterTarget::LinkChip)segment_icon=InkGlyph::Link;
            const float label_width=textWidth(label,button_format_);
            if(type) {
                const float left=(rect.left+rect.right-label_width-17)/2;
                glyph(segment_icon,{left,rect.top+7,left+12,rect.top+19},active?palette.text:palette.muted);
                drawText(label,button_format_,{left+17,rect.top,rect.right,rect.bottom},active?palette.text:palette.muted,1);
            } else drawAlignedText(label,button_format_,rect,active?palette.text:palette.muted,1,DWRITE_TEXT_ALIGNMENT_CENTER);
        };
        drawSegment(filter.text_chip, L"文本", filter_kinds_.contains(ClipboardKind::Text),
                    PopupFilterTarget::TextChip);
        drawSegment(filter.image_chip, L"图片", filter_kinds_.contains(ClipboardKind::Image),
                    PopupFilterTarget::ImageChip);
        drawSegment(filter.file_chip, L"文件", filter_kinds_.contains(ClipboardKind::Files),
                    PopupFilterTarget::FileChip);
        drawSegment(filter.link_chip, L"链接", filter_kinds_.contains(ClipboardKind::Link),
                    PopupFilterTarget::LinkChip);

        // 时间：标签行右侧内嵌 MM/DD → MM/DD（当前选端加朱砂下划线）
        drawText(L"时间", small_format_,
                 UiRect{filter.date_card.left, filter.date_card.top, filter.date_card.left + 60.0f,
                        filter.date_card.bottom},
                 palette.text_tertiary, 0.94f);
        auto format_short_date = [](const std::optional<PopupCalendarDate>& date) {
            if (!date) {
                return std::wstring(L"开始");
            }
            wchar_t buffer[16]{};
            swprintf_s(buffer, L"%02d/%02d", date->month, date->day);
            return std::wstring(buffer);
        };
        const bool start_active = date_filter_.active_field == PopupDateRangeField::Start;
        const auto start_text = format_short_date(date_filter_.start);
        const auto end_text = date_filter_.end ? format_short_date(date_filter_.end) : std::wstring(L"结束");
        const bool show_range=date_filter_.start.has_value() || filter_preset_==kFilterPresetCustom;
        if(show_range) {
        drawText(start_text, mono_format_, filter.start_date, start_active ? palette.text : palette.muted, 1.0f);
        drawText(L"→", mono_format_,
                 UiRect{filter.start_date.right, filter.start_date.top, filter.end_date.left, filter.end_date.bottom},
                 palette.text_tertiary, 0.86f);
        drawText(end_text, mono_format_, filter.end_date, start_active ? palette.muted : palette.text, 0.94f);
        brush->SetColor(D2D1::ColorF(palette.danger, 0.92f));
        if (filter_preset_==kFilterPresetCustom && start_active) {
            render_target_->DrawLine(D2D1::Point2F(filter.start_date.left, filter.start_date.bottom - 1.0f),
                                     D2D1::Point2F(filter.start_date.right, filter.start_date.bottom - 1.0f), brush,
                                     1.5f);
        } else if(filter_preset_==kFilterPresetCustom) {
            render_target_->DrawLine(D2D1::Point2F(filter.end_date.left, filter.end_date.bottom - 1.0f),
                                     D2D1::Point2F(filter.end_date.right, filter.end_date.bottom - 1.0f), brush, 1.5f);
        }
        }
        // 五段预设
        brush->SetColor(D2D1::ColorF(palette.paper_hover, palette.dark ? 0.92f : 0.96f));
        render_target_->FillRoundedRectangle(RoundRect(filter.preset_bar, 8), brush);
        constexpr const wchar_t* kPresetLabels[kFilterPresetCount] = {L"不限", L"今天", L"近 7 天", L"近 30 天",
                                                                      L"自定义"};
        constexpr PopupFilterTarget kPresetTargets[kFilterPresetCount] = {
            PopupFilterTarget::PresetAll, PopupFilterTarget::PresetToday, PopupFilterTarget::PresetWeek,
            PopupFilterTarget::PresetMonth, PopupFilterTarget::PresetCustom};
        for (int index = 0; index < kFilterPresetCount; ++index) {
            drawSegment(filter.presets[index], kPresetLabels[index], filter_preset_ == index,
                        kPresetTargets[index]);
        }

        // 日历：仅"自定义"时展开；今天朱砂、未来置灰、区间为连续深纸色带 + 端点墨点
        if (filter.custom_range) {
            render_target_->PushAxisAlignedClip(Rect(filter.panel.left,filter.calendar.top,filter.panel.right,
                std::max(filter.calendar.top,filter.footer_divider.top-14)),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            drawText(FormatMonthTitle(calendar_year_, calendar_month_), button_format_, filter.calendar_title,
                     palette.text, 0.96f);
            const auto drawArrow = [&](const UiRect& button, bool next, PopupFilterTarget target,
                                       bool double_arrow) {
                hoverBackplate(button, target, 5.0f);
                const float hover = filterHover(target);
                const auto glyph = PopupFilterArrowGlyphRect(button);
                const float cx = (glyph.left + glyph.right) * 0.5f;
                const float cy = (glyph.top + glyph.bottom) * 0.5f;
                brush->SetColor(D2D1::ColorF(hover > 0.0f ? palette.text : palette.muted,
                                             hover > 0.0f ? 0.94f : 0.82f));
                const auto draw_one = [&](float x_offset) {
                    const float x1 = next ? cx - 2.2f + x_offset : cx + 2.2f + x_offset;
                    const float x2 = next ? cx + 2.6f + x_offset : cx - 2.6f + x_offset;
                    render_target_->DrawLine(D2D1::Point2F(x1, cy - 4.4f), D2D1::Point2F(x2, cy), brush, 1.5f);
                    render_target_->DrawLine(D2D1::Point2F(x2, cy), D2D1::Point2F(x1, cy + 4.4f), brush, 1.5f);
                };
                if (double_arrow) {
                    draw_one(next ? -2.2f : 2.2f);
                    draw_one(next ? 2.2f : -2.2f);
                } else {
                    draw_one(0.0f);
                }
            };
            drawArrow(filter.calendar_prev_year, false, PopupFilterTarget::CalendarPreviousYear, true);
            drawArrow(filter.calendar_prev, false, PopupFilterTarget::CalendarPrevious, false);
            drawArrow(filter.calendar_next, true, PopupFilterTarget::CalendarNext, false);
            drawArrow(filter.calendar_next_year, true, PopupFilterTarget::CalendarNextYear, true);
            for (const auto& label : BuildPopupCalendarWeekdayLabels(filter)) {
                drawAlignedText(std::wstring(1,label.text),small_format_,label.rect,palette.text_tertiary,1,DWRITE_TEXT_ALIGNMENT_CENTER);
            }
            for (const auto& segment : BuildPopupCalendarRangeSegments(filter, calendar_year_, calendar_month_,
                                                                       date_filter_)) {
                brush->SetColor(D2D1::ColorF(palette.paper_hover, palette.dark ? 0.94f : 0.98f));
                render_target_->FillRoundedRectangle(RoundRect(segment.rect, 11.0f), brush);
            }
            const auto today = TodayDate();
            for (const auto& cell : BuildPopupCalendarCells(filter, calendar_year_, calendar_month_)) {
                const bool is_start = date_filter_.start && *date_filter_.start == cell.date;
                const bool is_end = date_filter_.end && *date_filter_.end == cell.date;
                const bool is_today = cell.date == today;
                const bool future = today < cell.date;
                const bool date_hovered = hover_filter_date_ && *hover_filter_date_ == cell.date;
                const float cx = (cell.rect.left + cell.rect.right) * 0.5f;
                const float cy = (cell.rect.top + cell.rect.bottom) * 0.5f;
                if (is_start || is_end) {
                    brush->SetColor(D2D1::ColorF(palette.text, 0.94f));
                    render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 11.0f, 11.0f), brush);
                } else if (date_hovered && !future) {
                    brush->SetColor(D2D1::ColorF(palette.paper_hover, 0.98f));
                    render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 11.0f, 11.0f), brush);
                }
                const std::wstring day_text = std::to_wstring(cell.date.day);
                uint32_t color = palette.muted;
                float alpha = 0.94f;
                if (is_start || is_end) {
                    color = palette.panel_fill;
                    alpha = 0.99f;
                } else if (is_today) {
                    color = palette.danger;
                    alpha = 0.96f;
                } else if (future) {
                    color = palette.text_tertiary;
                    alpha = 0.45f;
                } else if (date_hovered) {
                    color = palette.text;
                    alpha = 0.96f;
                }
                drawAlignedText(day_text,date_value_format_ ? date_value_format_ : mono_format_,cell.rect,color,alpha,DWRITE_TEXT_ALIGNMENT_CENTER);
            }
            render_target_->PopAxisAlignedClip();
        }

        // 底部：发丝线 + 匹配 N 条 + 完成
        brush->SetColor(D2D1::ColorF(palette.text, 0.09f));
        render_target_->FillRectangle(Rect(filter.footer_divider), brush);
        const auto result_count=std::to_wstring(items_.size());
        const float count_label_width=textWidth(L"匹配 ",small_format_);
        const float count_value_width=textWidth(result_count,title_format_);
        drawText(L"匹配 ",small_format_,
                 UiRect{filter.count_text.left,filter.count_text.top,filter.count_text.left+count_label_width,
                        filter.count_text.bottom},palette.text_tertiary,1.0f);
        drawText(result_count,title_format_,
                 UiRect{filter.count_text.left+count_label_width,filter.count_text.top,
                        filter.count_text.left+count_label_width+count_value_width,filter.count_text.bottom},
                 palette.text,1.0f);
        drawText(L" 条",small_format_,
                 UiRect{filter.count_text.left+count_label_width+count_value_width,filter.count_text.top,
                        filter.count_text.right,filter.count_text.bottom},palette.text_tertiary,1.0f);
        const float done_hover = filterHover(PopupFilterTarget::Done);
        brush->SetColor(D2D1::ColorF(palette.text, 0.92f + 0.08f * done_hover));
        render_target_->FillRoundedRectangle(RoundRect(filter.done, 7), brush);
        drawAlignedText(L"完成",button_format_,filter.done,palette.window_tint,1,DWRITE_TEXT_ALIGNMENT_CENTER);

        render_target_->SetTransform(previous_transform);
    }
    // Prototype menus share the same paper, spacing and linear icons as the rows.
    if (context_menu_open_ || closing_context_menu_) {
        const float p = PopupMotionProgress(context_motion_progress_);
        const float alpha = closing_context_menu_ ? 1.0f - p : p;
        D2D1_MATRIX_3X2_F previous{}; render_target_->GetTransform(&previous);
        const auto panel = ContextPanelRect();
        render_target_->SetTransform(D2D1::Matrix3x2F::Scale(0.98f + 0.02f * alpha, 0.98f + 0.02f * alpha,
            D2D1::Point2F(panel.left, panel.top)) * D2D1::Matrix3x2F::Translation(0, -6.0f * (1-alpha)) * previous);
        ID2D1Layer* layer = nullptr;
        const bool layered = alpha < 0.999f && SUCCEEDED(render_target_->CreateLayer(nullptr, &layer));
        if (layered) render_target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::Matrix3x2F::Identity(), alpha), layer);
        auto menuSurface = [&](const UiRect& box) {
            for (int i = 10; i > 0; --i) {
                const float spread = static_cast<float>(i);
                brush->SetColor(D2D1::ColorF(0x000000, palette.dark ? 0.018f : 0.008f));
                render_target_->FillRoundedRectangle(RoundRect(box.left-spread,box.top+3-spread/2,
                    box.right+spread,box.bottom+3+spread,12+spread/2),brush);
            }
            brush->SetColor(D2D1::ColorF(palette.panel_fill)); render_target_->FillRoundedRectangle(RoundRect(box,12),brush);
            brush->SetColor(D2D1::ColorF(palette.text,0.12f)); render_target_->DrawRoundedRectangle(RoundRect(box,12),brush,1);
        };
        menuSurface(panel);
        const auto context_found = std::find_if(items_.begin(),items_.end(),[&](const auto& i){return context_item_id_ && i.id==*context_item_id_;});
        const HistoryItem* context_item = context_found == items_.end() ? nullptr : &*context_found;
        const wchar_t* labels[] = {context_item && context_item->is_pinned ? L"取消置顶" : L"置顶",
            context_item && context_item->is_favorite ? L"取消收藏" : L"收藏",L"设置备注",L"加入收藏夹",L"上移",L"下移",L"删除"};
        const InkGlyph icons[] = {InkGlyph::Pin,InkGlyph::Star,InkGlyph::Note,InkGlyph::Folder,
            InkGlyph::ArrowUp,InkGlyph::ArrowDown,InkGlyph::Trash};
        for (int row = 0; row < 7; ++row) {
            const auto rect = ContextRowRect(row);
            const bool enabled = ContextRowEnabled(row), hovered = context_hover_row_ == row && enabled;
            if (hovered) {
                brush->SetColor(D2D1::ColorF(row==6 ? palette.danger : palette.paper_hover,row==6 ? 0.10f : 1.0f));
                render_target_->FillRoundedRectangle(RoundRect(rect,7),brush);
            }
            const uint32_t color = hovered ? (row==6 ? palette.danger : palette.text) : palette.muted;
            glyph(icons[row],{rect.left+8,rect.top+8.5f,rect.left+21,rect.top+21.5f},color,enabled ? 1.0f : 0.35f);
            drawText(labels[row],button_format_,{rect.left+29,rect.top,rect.right-16,rect.bottom},color,enabled?1.0f:0.35f);
            if (row == 3) {
                brush->SetColor(D2D1::ColorF(color));
                render_target_->DrawLine(D2D1::Point2F(rect.right-12,rect.top+11),D2D1::Point2F(rect.right-8,rect.top+15),brush,1.2f);
                render_target_->DrawLine(D2D1::Point2F(rect.right-8,rect.top+15),D2D1::Point2F(rect.right-12,rect.top+19),brush,1.2f);
            }
            if (row==3 || row==5) hairline(panel.left+11,rect.bottom+4,panel.right-11);
        }
        if (context_submenu_open_) {
            const auto sub = ContextPanelRect(true); menuSurface(sub);
            const int count = static_cast<int>(favorite_groups_.size());
            render_target_->PushAxisAlignedClip(Rect(sub.left+5,sub.top+5,sub.right-5,sub.bottom-5),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            for (int row=0;row<=count;++row) {
                const auto rect=ContextRowRect(row,true);
                const bool active=row<count && context_item && context_item->favorite_group_id==favorite_groups_[row].id;
                if(context_submenu_hover_row_==row){brush->SetColor(D2D1::ColorF(palette.paper_hover));render_target_->FillRoundedRectangle(RoundRect(rect,7),brush);}
                if(active || row==count) glyph(row==count?InkGlyph::Plus:InkGlyph::Check,
                    {rect.left+8,rect.top+8.5f,rect.left+21,rect.top+21.5f},row==count?palette.text_tertiary:palette.text);
                drawText(row==count?L"新建收藏夹…":favorite_groups_[row].name,button_format_,
                    {rect.left+29,rect.top,rect.right-8,rect.bottom},row==count?palette.text_tertiary:palette.muted,1);
                if(row==count-1) hairline(sub.left+11,rect.bottom+4,sub.right-11);
            }
            render_target_->PopAxisAlignedClip();
        }
        if(layered)render_target_->PopLayer(); ReleasePtr(layer);
        render_target_->SetTransform(previous);
    }
    if (!context_menu_open_ && !filter_open_ && !favorite_group_menu_open_ && !clear_confirm_open_ && !prompt_open_ &&
        hover_action_!=UiAction::None && static_cast<double>(GetTickCount64())-hover_motion_start_ms_>=450.0) {
        std::wstring tip;
        UiRect anchor{};
        switch(hover_action_) {
        case UiAction::Pin:tip=L"固定窗口：粘贴后不隐藏";anchor=header.pin;break;
        case UiAction::Close:tip=L"关闭";anchor=header.close;break;
        case UiAction::Filter:tip=L"筛选";anchor=toolbar.filter;break;
        case UiAction::MultiSelect:if(!multi_select_){tip=L"多选";anchor=toolbar.multi_select;}break;
        case UiAction::ClearAll:tip=L"全部清除（保留收藏）";anchor=toolbar.clear_all;break;
        case UiAction::AddFavoritePhrase:tip=L"添加常用语";anchor=tabs.add_favorite_phrase;break;
        default:break;
        }
        if(!tip.empty()) {
            const float width=textWidth(tip,small_format_)+14;
            const UiRect box{std::max(4.0f,anchor.right-width),anchor.bottom+4,anchor.right,anchor.bottom+24};
            brush->SetColor(D2D1::ColorF(palette.text));render_target_->FillRoundedRectangle(RoundRect(box,4),brush);
            drawAlignedText(tip,small_format_,box,palette.window_tint,1,DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
    if (clear_confirm_open_ || prompt_open_) {
        brush->SetColor(D2D1::ColorF(palette.dark?0x000000:0x1C1B19,palette.dark?0.40f:0.12f));
        render_target_->FillRoundedRectangle(RoundRect(window_surface_rect,14),brush);
    }
    if (clear_confirm_open_) {
        const auto panel=FavoriteGroupDeleteConfirmPanelRect(popup_logical_width_);
        drawEdgeShadow(panel,12,0.8f,3);
        brush->SetColor(D2D1::ColorF(palette.panel_fill));render_target_->FillRoundedRectangle(RoundRect(panel,12),brush);
        brush->SetColor(D2D1::ColorF(palette.text,0.12f));render_target_->DrawRoundedRectangle(RoundRect(panel,12),brush,1);
        brush->SetColor(D2D1::ColorF(palette.danger,0.10f));
        render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(panel.left+27,panel.top+27),11,11),brush);
        drawAlignedText(L"!",title_format_,{panel.left+16,panel.top+16,panel.left+38,panel.top+38},palette.danger,1,DWRITE_TEXT_ALIGNMENT_CENTER);
        drawText(L"全部清除？",title_format_,{panel.left+46,panel.top+16,panel.right-16,panel.top+38},palette.text,1);
        drawText(L"将删除所有未收藏的历史记录，收藏内容会保留。",detail_format_,
            {panel.left+46,panel.top+46,panel.right-16,panel.top+94},palette.text_tertiary,1);
        const auto cancel=FavoriteGroupDeleteConfirmCancelRect(panel),ok=FavoriteGroupDeleteConfirmDeleteRect(panel);
        brush->SetColor(D2D1::ColorF(palette.text,0.16f));render_target_->DrawRoundedRectangle(RoundRect(cancel,7),brush,1);
        drawAlignedText(L"取消",button_format_,cancel,palette.muted,1,DWRITE_TEXT_ALIGNMENT_CENTER);
        brush->SetColor(D2D1::ColorF(palette.danger));render_target_->FillRoundedRectangle(RoundRect(ok,7),brush);
        drawAlignedText(L"清除",button_format_,ok,0xFFFFFF,1,DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if(!toast_text_.empty()) {
        const float elapsed=static_cast<float>(static_cast<double>(GetTickCount64())-toast_start_ms_);
        const float alpha=elapsed>1400 ? 1.0f-std::clamp((elapsed-1400)/200,0.0f,1.0f) : PopupMotionProgress(std::clamp(elapsed/200,0.0f,1.0f));
        const float width=textWidth(toast_text_,small_format_)+24;
        const float top=surface_height-42-26+8*(1-alpha);
        const UiRect box{(surface_width-width)/2,top,(surface_width+width)/2,top+26};
        brush->SetColor(D2D1::ColorF(palette.text,alpha));render_target_->FillRoundedRectangle(RoundRect(box,14),brush);
        drawAlignedText(toast_text_,small_format_,box,palette.window_tint,alpha,DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    render_target_->SetTransform(D2D1::Matrix3x2F::Identity());

    if (render_target_->EndDraw() == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
}

void PopupWindow::ApplyBackdrop() {
    SetPopupWindowChromeAttributes(hwnd_, theme_mode_);
}

void PopupWindow::ApplyWindowRegion() {
    if (!hwnd_) {
        return;
    }
    if (!PopupWindowShouldUseHardRoundedRegion()) {
        SetWindowRgn(hwnd_, nullptr, TRUE);
        return;
    }
    RECT client{};
    if (!GetClientRect(hwnd_, &client)) {
        return;
    }
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        return;
    }
    const int corner_diameter = PopupWindowRegionCornerDiameterForDpi(CurrentDpi());
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, corner_diameter, corner_diameter);
    if (!region) {
        return;
    }
    if (!SetWindowRgn(hwnd_, region, TRUE)) {
        DeleteObject(region);
    }
}

bool PopupWindow::HandleFilterClick(POINT point) {
    if (multi_select_) {
        return false;
    }
    if (!filter_open_) {
        return false;
    }

    const auto layout = CurrentFilterLayout();
    if (!Contains(layout.panel, point)) {
        CloseFilterPanelWithMotion();
        return true;
    }
    if (Contains(layout.close, point)) {
        CloseFilterPanelWithMotion();
        return true;
    }
    if (Contains(layout.done, point)) {
        CloseFilterPanelWithMotion();
        return true;
    }

    auto toggle_kind = [&](ClipboardKind kind) {
        if (filter_kinds_.contains(kind)) {
            filter_kinds_.erase(kind);
        } else {
            filter_kinds_.insert(kind);
        }
        scroll_offset_ = 0;
        ReloadItems();
        ResizeToCurrentItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
    };

    if (Contains(layout.text_chip, point)) {
        toggle_kind(ClipboardKind::Text);
        return true;
    }
    if (Contains(layout.image_chip, point)) {
        toggle_kind(ClipboardKind::Image);
        return true;
    }
    if (Contains(layout.file_chip, point)) {
        toggle_kind(ClipboardKind::Files);
        return true;
    }
    if (Contains(layout.link_chip, point)) {
        toggle_kind(ClipboardKind::Link);
        return true;
    }

    // 五段时间预设：点"今天/近 7 天/近 30 天"直接设置区间并重新查询。
    for (int index = 0; index < kFilterPresetCount; ++index) {
        if (Contains(layout.presets[index], point)) {
            ApplyFilterPreset(index);
            return true;
        }
    }

    if (const auto field = HitTestPopupDateRangeField(layout, static_cast<float>(point.x), static_cast<float>(point.y))) {
        // 点击区间端点时进入自定义，与原型的内嵌区间操作一致。
        if (date_filter_.start || filter_preset_ == kFilterPresetCustom) {
            filter_preset_=kFilterPresetCustom;
            date_filter_.active_field = *field;
            const auto& selected_date = *field == PopupDateRangeField::Start ? date_filter_.start : date_filter_.end;
            if (selected_date) {
                calendar_year_ = selected_date->year;
                calendar_month_ = selected_date->month;
            } else if (*field == PopupDateRangeField::End && date_filter_.start) {
                calendar_year_ = date_filter_.start->year;
                calendar_month_ = date_filter_.start->month;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return true;
    }

    switch (HitTestPopupCalendarArrow(layout, static_cast<float>(point.x), static_cast<float>(point.y))) {
    case PopupCalendarArrow::PreviousYear:
        ShiftMonth(calendar_year_, calendar_month_, -12);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    case PopupCalendarArrow::PreviousMonth:
        ShiftMonth(calendar_year_, calendar_month_, -1);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    case PopupCalendarArrow::NextMonth:
        ShiftMonth(calendar_year_, calendar_month_, 1);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    case PopupCalendarArrow::NextYear:
        ShiftMonth(calendar_year_, calendar_month_, 12);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    case PopupCalendarArrow::None:
        break;
    }

    if (const auto date = HitTestPopupCalendarDate(layout, calendar_year_, calendar_month_,
                                                  static_cast<float>(point.x), static_cast<float>(point.y))) {
        // 未来的日期不能点。
        if (TodayDate() < *date) {
            return true;
        }
        SelectPopupDateRangeDate(date_filter_, *date);
        scroll_offset_ = 0;
        ReloadItems();
        ResizeToCurrentItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    if (HasActiveFilter() && Contains(PopupFilterResetVisualRect(layout), point)) {
        filter_kinds_.clear();
        date_filter_ = PopupDateRangeState{};
        filter_preset_ = 0;
        scroll_offset_ = 0;
        ReloadItems();
        ResizeToCurrentItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    return true;
}

bool PopupWindow::HandleFavoriteGroupMenuClick(POINT point) {
    if (!favorite_group_menu_open_) {
        return false;
    }

    const auto layout = BuildPopupFavoriteGroupMenuLayout(favorite_groups_.size());
    const auto hit = HitTestPopupFavoriteGroupMenu(layout, favorite_groups_.size(), static_cast<float>(point.x),
                                                   static_cast<float>(point.y));
    if (hit.target == PopupFavoriteGroupMenuTarget::None) {
        CloseFavoriteGroupMenuWithMotion();
        return true;
    }
    if (hit.target == PopupFavoriteGroupMenuTarget::Panel) {
        return true;
    }
    if (hit.target == PopupFavoriteGroupMenuTarget::AllFavorites) {
        active_favorite_group_id_.reset();
    } else if (hit.target == PopupFavoriteGroupMenuTarget::Group) {
        if (hit.group_index >= favorite_groups_.size()) {
            return true;
        }
        active_favorite_group_id_ = favorite_groups_[hit.group_index].id;
    } else if (hit.target == PopupFavoriteGroupMenuTarget::DeleteGroup) {
        if (hit.group_index >= favorite_groups_.size()) {
            return true;
        }
        const auto group = favorite_groups_[hit.group_index];
        pending_favorite_group_delete_ = PendingFavoriteGroupDelete{group.id, hit.group_index, group.name};
        hover_delete_confirm_target_ = PopupFavoriteGroupDeleteConfirmTarget::None;
        StartMotion(PopupMotionTarget::DeleteConfirm);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    } else if (hit.target == PopupFavoriteGroupMenuTarget::NewGroup) {
        favorite_group_menu_open_ = false;
        PromptCreateFavoriteGroup();
        return true;
    }

    favorite_group_menu_open_ = false;
    scroll_offset_ = 0;
    ReloadItems();
    ResizeToCurrentItems();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool PopupWindow::HandleFavoriteGroupDeleteConfirmClick(POINT point) {
    if (!pending_favorite_group_delete_) {
        return false;
    }

    const auto target = HitTestFavoriteGroupDeleteConfirm(point, popup_logical_width_);
    if (target == PopupFavoriteGroupDeleteConfirmTarget::Panel) {
        return true;
    }
    if (target == PopupFavoriteGroupDeleteConfirmTarget::Cancel ||
        target == PopupFavoriteGroupDeleteConfirmTarget::None) {
        CloseFavoriteGroupDeleteConfirmWithMotion();
        return true;
    }
    if (target == PopupFavoriteGroupDeleteConfirmTarget::Delete) {
        const auto pending = *pending_favorite_group_delete_;
        if (active_favorite_group_id_ && *active_favorite_group_id_ == pending.id) {
            active_favorite_group_id_.reset();
            if (favorite_groups_.size() > 1) {
                const size_t next_index =
                    pending.group_index + 1 < favorite_groups_.size() ? pending.group_index + 1
                                                                      : pending.group_index - 1;
                active_favorite_group_id_ = favorite_groups_[next_index].id;
            }
        }

        store_.DeleteFavoriteGroup(pending.id);
        RefreshFavoriteGroupCounts();
        pending_favorite_group_delete_.reset();
        hover_delete_confirm_target_ = PopupFavoriteGroupDeleteConfirmTarget::None;
        favorite_group_menu_open_ = false;
        scroll_offset_ = 0;
        ReloadItems();
        ResizeToCurrentItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    return true;
}

int PopupWindow::HitTestItem(POINT point) const {
    if (items_.empty() || point.y < PopupListTop() || point.y >= PopupListBottomForHeight(popup_logical_height_) ||
        Contains(PopupScrollbarHitRectForSize(popup_logical_width_, popup_logical_height_), point)) {
        return -1;
    }
    // 与 Paint 共用前缀和：分区标题的高度也计入，命中与绘制不会错位。
    // 行高固定（只有展开项例外），所以从第一个可见行起最多检查几行就能命中。
    const float width_delta = static_cast<float>(popup_logical_width_ - PopupMetrics().width);
    const int first_index = FirstVisibleItemIndex();
    for (int item_index = first_index; item_index < static_cast<int>(items_.size()); ++item_index) {
        const float top = PopupListTop() + RowTopForIndex(item_index) - scroll_offset_;
        if (top > point.y) {
            break;
        }
        const auto& item = items_[item_index];
        if (row_transitions_.contains(item.id) && row_transitions_.at(item.id).removing) continue;
        auto card = BuildPopupCardLayout(false, top);
        card.card.right += width_delta;
        if (point.x >= card.card.left && point.x <= card.card.right && point.y >= card.card.top &&
            point.y <= card.card.top + ItemScrollHeight(item_index)) {
            return item_index;
        }
    }
    return -1;
}

int PopupWindow::HitTestExpandItem(POINT point) const {
    if (items_.empty() || point.y < PopupListTop() || point.y >= PopupListBottomForHeight(popup_logical_height_) ||
        Contains(PopupScrollbarHitRectForSize(popup_logical_width_, popup_logical_height_), point)) {
        return -1;
    }
    const float width_delta = static_cast<float>(popup_logical_width_ - PopupMetrics().width);
    const int first_index = FirstVisibleItemIndex();
    for (int item_index = first_index; item_index < static_cast<int>(items_.size()); ++item_index) {
        const float top = PopupListTop() + RowTopForIndex(item_index) - scroll_offset_;
        if (top > point.y) {
            break;
        }
        const auto& item = items_[item_index];
        if (row_transitions_.contains(item.id) && row_transitions_.at(item.id).removing) continue;
        auto card = BuildPopupCardLayout(false, top);
        card.card.right += width_delta;
        card.expand.left += width_delta;
        card.expand.right += width_delta;
        if (Contains(card.expand, point) && point.y < top + ItemScrollHeight(item_index)) {
            return item_index;
        }
    }
    return -1;
}

PopupWindow::UiAction PopupWindow::HitTestAction(POINT point) const {
    const bool has_scrollable_content = TotalScrollHeight() > ViewportScrollHeight();
    if (has_scrollable_content &&
        Contains(PopupScrollbarHitRectForSize(popup_logical_width_, popup_logical_height_), point)) {
        return UiAction::Scrollbar;
    }
    if (has_scrollable_content &&
        (PopupScrollToTopButtonVisible(scroll_offset_) || smooth_scroll_active_) &&
        Contains(PopupScrollToTopButtonRectForSize(popup_logical_width_, popup_logical_height_), point)) {
        return UiAction::ScrollToTop;
    }
    if (HitTestExpandItem(point) >= 0) {
        return UiAction::ExpandItem;
    }
    if (Contains(BuildPopupSearchLayoutForWidth(popup_logical_width_).box, point)) {
        const auto search_layout = BuildPopupSearchLayoutForWidth(popup_logical_width_);
        if (PopupSearchClearButtonHitTest(search_layout, !query_.empty(), point)) {
            return UiAction::ClearSearch;
        }
        return UiAction::Search;
    }
    const auto header = BuildPopupHeaderLayoutForWidth(popup_logical_width_);
    if (Contains(header.close, point)) return UiAction::Close;
    if (Contains(header.pin, point)) return UiAction::Pin;
    const auto toolbar = BuildPopupToolbarLayoutForWidth(multi_select_, popup_logical_width_);
    if (point.y >= PopupToolbarTop() &&
        point.y <= PopupToolbarTop() + static_cast<float>(PopupMetrics().toolbar_height)) {
        if (multi_select_) {
            if (Contains(toolbar.cancel_multi_select, point)) return UiAction::MultiSelect;
            if (Contains(toolbar.select_all, point)) return UiAction::SelectAll;
            if (Contains(toolbar.delete_selected, point)) return UiAction::DeleteSelected;
            if (Contains(toolbar.paste_selected, point)) return UiAction::PasteSelected;
        } else {
            if (Contains(toolbar.filter, point)) return UiAction::Filter;
            if (Contains(toolbar.multi_select, point)) return UiAction::MultiSelect;
            if (Contains(toolbar.clear_all, point)) return UiAction::ClearAll;
        }
    }
    const auto tabs = BuildPopupTabsLayoutForWidth(view_mode_ == ViewMode::Favorites, popup_logical_width_);
    // 标签的点击区是整段标签栏高度，不只是文字矩形。
    const auto tabHit = [&](const UiRect& rect) {
        return point.x >= rect.left && point.x <= rect.right && point.y >= PopupTabsTop() &&
               point.y <= PopupTabsTop() + static_cast<float>(PopupMetrics().tab_height);
    };
    if (tabHit(tabs.history)) return UiAction::HistoryTab;
    if (tabHit(tabs.favorites)) return UiAction::FavoritesTab;
    if (view_mode_ == ViewMode::Favorites && Contains(tabs.favorite_group, point)) {
        return UiAction::FavoriteGroupMenu;
    }
    if (view_mode_ == ViewMode::Favorites && Contains(tabs.add_favorite_phrase, point)) {
        return UiAction::AddFavoritePhrase;
    }
    return UiAction::None;
}

void PopupWindow::ShowContextMenu(POINT point, int item_index) {
    if (item_index < 0 || item_index >= static_cast<int>(items_.size())) return;
    selected_index_ = item_index;
    KickSlipAnimation();
    context_item_id_ = items_[item_index].id;
    ActivatePopupWindow(hwnd_);
    context_origin_ = ClientPointToDips(point);
    context_origin_.x = std::clamp(context_origin_.x, 8L, static_cast<LONG>(popup_logical_width_ - 176));
    context_origin_.y = std::clamp(context_origin_.y, 8L, static_cast<LONG>(popup_logical_height_ - 248));
    context_menu_open_ = true;
    closing_context_menu_ = context_submenu_open_ = false;
    context_hover_row_ = context_submenu_hover_row_ = -1;
    context_submenu_scroll_ = 0.0f;
    context_motion_start_ms_ = static_cast<double>(GetTickCount64());
    context_motion_progress_ = animations_enabled_ ? 0.0f : 1.0f;
    filter_open_ = favorite_group_menu_open_ = false;
    closing_filter_panel_ = closing_favorite_group_menu_ = false;
    SetTimer(hwnd_, kOutsideClickTimer, 50, nullptr);
    if (animations_enabled_) SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

UiRect PopupWindow::ContextPanelRect(bool submenu) const {
    float x = static_cast<float>(context_origin_.x), y = static_cast<float>(context_origin_.y);
    float width = 168.0f, height = 238.0f;
    if (submenu) {
        width = 150.0f;
        height = 49.0f + 30.0f * static_cast<float>(favorite_groups_.size());
        height = std::min(height, static_cast<float>(popup_logical_height_ - 16));
        x = x + 172.0f + width > popup_logical_width_ ? x - 152.0f : x + 172.0f;
        x = std::max(4.0f, x);
        y = std::clamp(y + 90.0f, 8.0f, static_cast<float>(popup_logical_height_) - height - 8.0f);
    }
    return {x, y, x + width, y + height};
}

UiRect PopupWindow::ContextRowRect(int row, bool submenu) const {
    auto panel = ContextPanelRect(submenu);
    float y = panel.top + 5.0f + static_cast<float>(row) * 30.0f;
    if (!submenu) { if (row >= 4) y += 9.0f; if (row >= 6) y += 9.0f; }
    else {
        if (row == static_cast<int>(favorite_groups_.size())) y += 9.0f;
        y -= context_submenu_scroll_;
    }
    return {panel.left + 5.0f, y, panel.right - 5.0f, y + 30.0f};
}

int PopupWindow::HitTestContextMenu(POINT point, bool submenu) const {
    if(!Contains(ContextPanelRect(submenu),point))return -1;
    const int count = submenu ? static_cast<int>(favorite_groups_.size()) + 1 : 7;
    for (int row = 0; row < count; ++row) if (Contains(ContextRowRect(row, submenu), point)) return row;
    return -1;
}

bool PopupWindow::ContextRowEnabled(int row) const {
    if (row < 0) return false;
    if (row != 4 && row != 5) return true;
    if (!context_item_id_) return false;
    auto found = std::find_if(items_.begin(), items_.end(), [&](const auto& i) {return i.id == *context_item_id_;});
    if (found == items_.end()) return false;
    return row == 4 ? found != items_.begin() : std::next(found) != items_.end();
}

void PopupWindow::CloseContextMenu() {
    if (!context_menu_open_) return;
    context_menu_open_ = false;
    context_submenu_open_ = false;
    closing_context_menu_ = animations_enabled_;
    context_motion_start_ms_ = static_cast<double>(GetTickCount64());
    context_motion_progress_ = animations_enabled_ ? 0.0f : 1.0f;
    if (animations_enabled_) SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
    EndTransientHideSuppressionSoon();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::ExecuteContextCommand(int row, bool submenu) {
    if (!context_item_id_ || (!submenu && !ContextRowEnabled(row))) return;
    const auto id = *context_item_id_;
    const auto item = store_.Get(id);
    if (!item) { CloseContextMenu(); return; }
    if (!submenu && row == 3) { context_submenu_open_ = true; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    CloseContextMenu();
    BeginTransientHideSuppression();
    if (submenu) {
        const size_t count = favorite_groups_.size();
        if (row >= 0 && static_cast<size_t>(row) < count) {
            store_.SetFavoriteGroup(id, favorite_groups_[row].id);
            ShowToast(L"已加入「" + favorite_groups_[row].name + L"」");
        } else {
            prompt_open_ = true;
            InvalidateRect(hwnd_, nullptr, FALSE); UpdateWindow(hwnd_);
            if (const auto name = PromptText(hwnd_, instance_, L"新建收藏夹", L"输入分组名称", L"", theme_mode_)) {
                store_.SetFavoriteGroup(id, store_.EnsureFavoriteGroup(*name));
                ShowToast(L"已加入「" + *name + L"」");
            }
            prompt_open_ = false;
        }
        ReloadItems(); RefreshFavoriteGroupCounts();
    } else {
        switch (row) {
        case 0: TogglePinned(id); ShowToast(item->is_pinned ? L"已取消置顶" : L"已置顶，始终显示在最上方"); break;
        case 1: ToggleFavorite(id); ShowToast(item->is_favorite ? L"已取消收藏" : L"已收藏"); break;
        case 2: PromptEditNote(id); break;
        case 4: case 5: {
            const auto found = std::find_if(items_.begin(), items_.end(), [id](const auto& i) {return i.id == id;});
            const int index = static_cast<int>(found - items_.begin()), next = index + (row == 4 ? -1 : 1);
            if (next >= 0 && next < static_cast<int>(items_.size()) && items_[index].is_pinned != items_[next].is_pinned)
                ShowToast(L"置顶与普通记录之间不能互换");
            else MoveItemInCurrentList(index, row == 4 ? -1 : 1);
            break;
        }
        case 6: DeleteItem(id); break;
        }
    }
    EndTransientHideSuppressionSoon();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::ShowToast(std::wstring text) {
    toast_text_ = std::move(text);
    toast_start_ms_ = static_cast<double>(GetTickCount64());
    SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

float PopupWindow::RowMotionProgress(int64_t id) const {
    const auto motion = row_transitions_.find(id);
    if (motion == row_transitions_.end()) return 1.0f;
    return PopupTimedMotionProgress(static_cast<double>(GetTickCount64()) - motion->second.start_ms,
                                    motion->second.removing ? 210.0 : 260.0, animations_enabled_);
}

void PopupWindow::BeginRemoval(const std::set<int64_t>& ids) {
    if (ids.empty()) { ReloadItems(); RefreshFavoriteGroupCounts(); InvalidateRect(hwnd_,nullptr,FALSE); return; }
    const double now = static_cast<double>(GetTickCount64());
    for (auto id : ids) {
        store_.Delete(id);
        if (animations_enabled_) row_transitions_[id] = {now, true};
    }
    selection_.Clear();
    InvalidateRowMetrics();
    if (!animations_enabled_) { ReloadItems(); RefreshFavoriteGroupCounts(); }
    else SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

float PopupWindow::AnimatedExpandedHeight(const HistoryItem& item) const {
    if (const auto found = detail_transitions_.find(item.id); found != detail_transitions_.end()) {
        const auto& motion = found->second;
        const float p = PopupMotionProgress(PopupTimedMotionProgress(
            static_cast<double>(GetTickCount64()) - motion.start_ms, 240.0, animations_enabled_));
        return motion.from + (motion.to - motion.from) * p;
    }
    return expanded_item_id_ && *expanded_item_id_ == item.id ? ExpandedExtraHeightForItem(item) : 0.0f;
}

LRESULT PopupWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd_, &ps);
        if(IsVisible())Paint();
        EndPaint(hwnd_, &ps);
        // 原生 EDIT 只有 1px 宽、用来接输入，父窗口又用 WS_CLIPCHILDREN 把它剪掉；之前这里每帧
        // 用 RDW_UPDATENOW 同步重画一次完全看不到的控件（父窗口还为它处理 WM_CTLCOLOREDIT）。
        return 0;
    }
    case WM_SIZE: {
        const RECT logical_before{0, 0, popup_logical_width_, popup_logical_height_};
        UpdatePopupLogicalSize();
        ApplyWindowRegion();
        ClampScrollToCurrentPopupHeight();
        const RECT logical_after{0, 0, popup_logical_width_, popup_logical_height_};
        const bool logical_size_changed = PopupShouldApplyWindowRect(logical_before, logical_after);
        if (PopupShouldResizeNativeSearchDuringLiveResize(resizing_window_, logical_size_changed)) {
            UpdateSearchEditBounds();
        }
        if (render_target_) {
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            const auto size = D2D1::SizeU(static_cast<UINT32>(std::max(1L, rc.right - rc.left)),
                                         static_cast<UINT32>(std::max(1L, rc.bottom - rc.top)));
            if (FAILED(render_target_->Resize(size)) && PopupResizeShouldDiscardDeviceResources()) {
                DiscardDeviceResources();
            }
        }
        if (PopupShouldInvalidateDuringLiveResize(resizing_window_, logical_size_changed)) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return PopupMouseActivateResult();
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lparam) == search_edit_) {
            const auto palette = ResolvePopupThemePalette(theme_mode_, IsSystemDarkTheme());
            const COLORREF search_fill = RGB((palette.search_fill >> 16) & 0xFF,
                                             (palette.search_fill >> 8) & 0xFF,
                                             palette.search_fill & 0xFF);
            SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
            SetBkColor(reinterpret_cast<HDC>(wparam), search_fill);
            SetTextColor(reinterpret_cast<HDC>(wparam), search_fill);
            if (!search_edit_brush_ || search_edit_brush_color_ != search_fill) {
                if (search_edit_brush_) {
                    DeleteObject(search_edit_brush_);
                }
                search_edit_brush_ = CreateSolidBrush(search_fill);
                search_edit_brush_color_ = search_fill;
            }
            return reinterpret_cast<LRESULT>(search_edit_brush_);
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE) {
            HideIfInactive(reinterpret_cast<HWND>(lparam));
        }
        return 0;
    case WM_ACTIVATEAPP:
        if (!wparam) {
            HideIfInactive(GetForegroundWindow());
        }
        return 0;
    case WM_DPICHANGED: {
        auto* suggested = reinterpret_cast<RECT*>(lparam);
        if (suggested) {
            const UINT dpi = LOWORD(wparam);
            const SIZE size = PhysicalPopupSize(dpi ? dpi : CurrentDpi(), static_cast<int>(items_.size()));
            const RECT next = PopupDpiChangedWindowRect(*suggested, size);
            SetWindowPos(hwnd_, nullptr, next.left, next.top, next.right - next.left,
                         next.bottom - next.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ApplyWindowRegion();
        if (PopupDpiChangeShouldDiscardDeviceResources()) {
            DiscardDeviceResources();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(hwnd_, message, wparam, lparam);
        if (hit != HTCLIENT && hit != HTNOWHERE) {
            return hit;
        }
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &point);
        const POINT logical_point = ClientPointToDips(point);
        if (ResizeHitTest(logical_point)) return HTCLIENT;
        return HTCLIENT;
    }
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            POINT cursor{};
            GetCursorPos(&cursor);
            ScreenToClient(hwnd_, &cursor);
            const POINT logical_point = ClientPointToDips(cursor);
            ApplyCursorForPoint(logical_point);
            return TRUE;
        }
        break;
    case WM_TIMER:
        if(wparam==kIdleMemoryTimer){TrimIdleWorkingSet(hwnd_,kIdleMemoryTimer);return 0;}
        if (wparam == kMultiPasteTimer) {
            KillTimer(hwnd_, kMultiPasteTimer);
            AdvanceMultiPaste();
            return 0;
        }
        if (wparam == kSearchReloadTimer) {
            KillTimer(hwnd_, kSearchReloadTimer);
            ReloadItems();
            ResizeToCurrentItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (wparam == kAnimationTimer) {
            AdvanceMotion();
            return 0;
        }
        if (wparam == kOutsideClickTimer) {
            const bool left_button_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (left_button_down) {
                if (mouse_down_started_inside_popup_ || left_button_was_down_) {
                    left_button_was_down_ = true;
                    return 0;
                }
                POINT cursor{};
                GetCursorPos(&cursor);
                const bool inside_popup = ScreenPointInsideWindow(hwnd_, cursor);
                const bool new_mouse_press = !left_button_was_down_;
                if (ShouldHidePopupAfterOutsideClick(pinned_open_, prompt_open_, moving_window_,
                                                     mouse_down_started_inside_popup_, IsTransientHideSuppressed(),
                                                     IsVisible(), new_mouse_press, inside_popup)) {
                    Hide(L"outside-click");
                }
            } else {
                mouse_down_started_inside_popup_ = false;
            }
            left_button_was_down_ = left_button_down;
            return 0;
        }
        if (wparam == kSuppressInactiveHideTimer) {
            if (!IsTransientHideSuppressed()) {
                suppress_inactive_hide_ = false;
                suppress_inactive_hide_until_ = 0;
                KillTimer(hwnd_, kSuppressInactiveHideTimer);
            }
            return 0;
        }
        if (wparam == kShellTopmostRaiseTimer) {
            RaiseAboveShellSurface();
            return 0;
        }
        if (wparam == kItemLongPressTimer) {
            HandleLongPressTimer();
            return 0;
        }
        if (wparam == kHoverTimer) {
            if(!IsVisible()){KillTimer(hwnd_,kHoverTimer);return 0;}
            // 悬停进度同样按时间推进（90ms），收敛后停表。
            hover_progress_ = PopupTimedMotionProgress(static_cast<double>(GetTickCount64()) - hover_motion_start_ms_,
                                                       PopupMotionDurationMs(PopupMotionKind::Hover),
                                                       animations_enabled_);
            if (hover_progress_ >= 1.0f) {
                hover_progress_ = 1.0f;
                KillTimer(hwnd_, kHoverTimer);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (wparam == kSearchCaretTimer) {
            if (search_focused_ && GetFocus() == search_edit_) {
                search_caret_on_ = !search_caret_on_;
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else {
                search_caret_on_ = false;
                KillTimer(hwnd_, kSearchCaretTimer);
            }
            return 0;
    }
    break;
    case WM_COMMAND:
        if (LOWORD(wparam) == kSearchEditId && HIWORD(wparam) == EN_SETFOCUS && search_edit_) {
            search_focused_ = true;
            search_focus_from_ = search_focus_to_; search_focus_to_ = 1.0f;
            search_focus_start_ms_ = static_cast<double>(GetTickCount64());
            SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
            search_caret_on_ = true;
            HideNativeSearchCaret(search_edit_);
            SetTimer(hwnd_, kSearchCaretTimer, GetCaretBlinkTime(), nullptr);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (LOWORD(wparam) == kSearchEditId && HIWORD(wparam) == EN_KILLFOCUS && search_edit_) {
            search_focused_ = false;
            search_focus_from_ = search_focus_to_; search_focus_to_ = 0.0f;
            search_focus_start_ms_ = static_cast<double>(GetTickCount64());
            SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
            search_caret_on_ = false;
            KillTimer(hwnd_, kSearchCaretTimer);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (LOWORD(wparam) == kSearchEditId && HIWORD(wparam) == EN_CHANGE && search_edit_) {
            HideNativeSearchCaret(search_edit_);
            const int length = GetWindowTextLengthW(search_edit_);
            std::wstring value(static_cast<size_t>(length) + 1, L'\0');
            if (length > 0) {
                GetWindowTextW(search_edit_, value.data(), length + 1);
            }
            value.resize(static_cast<size_t>(length));
            if (value != query_) {
                query_ = std::move(value);
                SyncSearchSelectionFromEdit();
                search_caret_on_ = true;
                // 防抖：先只刷新重绘（搜索框文字要立刻跟随），列表查询合并到定时器里做一次。
                SetTimer(hwnd_, kSearchReloadTimer, kSearchReloadDelayMs, nullptr);
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else {
                SyncSearchSelectionFromEdit();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        POINT point = ClientPointToDips(POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        if (context_menu_open_) {
            const int subrow = context_submenu_open_ ? HitTestContextMenu(point, true) : -1;
            const int row = subrow >= 0 ? -1 : HitTestContextMenu(point);
            if (row == 3) context_submenu_open_ = true;
            else if (row >= 0) context_submenu_open_ = false;
            context_hover_row_ = row;
            context_submenu_hover_row_ = subrow;
            hover_point_ = point;
            SetCursor(LoadCursorW(nullptr, subrow >= 0 || ContextRowEnabled(row) ? IDC_HAND : IDC_ARROW));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (moving_window_ || resizing_window_) {
            UpdateWindowMoveOrResize();
            return 0;
        }
        ApplyCursorForPoint(point);
        // 命中的行只算一次：长按拖动的判定和 hover 状态共用（以前这条路径每帧算两遍）。
        const int hit_item = HitTestItem(point);
        if (pressed_item_index_ >= 0) {
            constexpr int kDragCancelDistance = 6;
            const bool moved_past_cancel_distance = std::abs(point.x - press_point_.x) > kDragCancelDistance ||
                                                    std::abs(point.y - press_point_.y) > kDragCancelDistance;
            switch (PopupItemPressMoveActionFor(long_press_selected_, moved_past_cancel_distance, hit_item)) {
            case PopupItemPressMoveAction::CancelPress:
                CancelItemPress();
                break;
            case PopupItemPressMoveAction::SelectHitItem:
                if (const auto selection = PopupSelectionIndexWhileLongPressing(long_press_selected_, hit_item)) {
                    if (*selection != selected_index_) {
                        selected_index_ = *selection;
                        scroll_offset_ = ScrollOffsetToRevealSelection(scroll_offset_, selected_index_);
                        KickSlipAnimation();
                        InvalidateRect(hwnd_, nullptr, FALSE);
                    }
                }
                break;
            case PopupItemPressMoveAction::KeepPress:
                break;
            }
        }
        if (dragging_scrollbar_) {
            UpdateScrollDrag(point);
            return 0;
        }
        if (search_selecting_) {
            SetSearchSelection(search_selection_anchor_, SearchTextIndexFromPoint(point));
            return 0;
        }
        if (!tracking_mouse_) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&track);
            tracking_mouse_ = true;
        }
        const auto delete_confirm_target = pending_favorite_group_delete_
                                               ? HitTestFavoriteGroupDeleteConfirm(point, popup_logical_width_)
                                               : PopupFavoriteGroupDeleteConfirmTarget::None;
        // CurrentFilterLayout() 会按日历动效重算整块面板几何，菜单布局也要算一遍；
        // 只有浮层真的打开时才有意义，关着的时候不要再白白算。
        PopupFilterTarget filter_target = PopupFilterTarget::None;
        std::optional<PopupCalendarDate> filter_date;
        if (filter_open_) {
            const auto filter_layout = CurrentFilterLayout();
            filter_target = HitTestPopupFilterTarget(filter_layout, calendar_year_, calendar_month_,
                                                     static_cast<float>(point.x), static_cast<float>(point.y));
            if (filter_target == PopupFilterTarget::CalendarDate) {
                filter_date = HitTestPopupCalendarDate(filter_layout, calendar_year_, calendar_month_,
                                                       static_cast<float>(point.x), static_cast<float>(point.y));
            }
        }
        PopupFavoriteGroupMenuHit favorite_menu_hit;
        if (favorite_group_menu_open_) {
            const auto favorite_menu_layout = BuildPopupFavoriteGroupMenuLayout(favorite_groups_.size());
            favorite_menu_hit = HitTestPopupFavoriteGroupMenu(favorite_menu_layout, favorite_groups_.size(),
                                                              static_cast<float>(point.x),
                                                              static_cast<float>(point.y));
        }
        const bool popover_hovered = delete_confirm_target != PopupFavoriteGroupDeleteConfirmTarget::None ||
                                     filter_target != PopupFilterTarget::None ||
                                     favorite_menu_hit.target != PopupFavoriteGroupMenuTarget::None;
        const UiAction action = popover_hovered ? UiAction::None : HitTestAction(point);
        const int item = PopupHoverItemIndex(filter_open_ || favorite_group_menu_open_, hit_item);
        if (action != hover_action_ || filter_target != hover_filter_target_ ||
            delete_confirm_target != hover_delete_confirm_target_ || filter_date != hover_filter_date_ ||
            favorite_menu_hit.target != hover_favorite_group_menu_hit_.target ||
            favorite_menu_hit.group_index != hover_favorite_group_menu_hit_.group_index ||
            item != hover_item_index_) {
            const bool hover_target_changed = action != hover_action_ || filter_target != hover_filter_target_ ||
                                              delete_confirm_target != hover_delete_confirm_target_ ||
                                              filter_date != hover_filter_date_ ||
                                              favorite_menu_hit.target != hover_favorite_group_menu_hit_.target ||
                                              favorite_menu_hit.group_index != hover_favorite_group_menu_hit_.group_index ||
                                              item != hover_item_index_;
            hover_action_ = action;
            hover_filter_target_ = filter_target;
            hover_delete_confirm_target_ = delete_confirm_target;
            hover_filter_date_ = filter_date;
            hover_favorite_group_menu_hit_ = favorite_menu_hit;
            hover_item_index_ = action == UiAction::None && !popover_hovered ? item : -1;
            hover_point_ = point;
            if (hover_target_changed) {
                hover_progress_ = 0.0f;
                hover_motion_start_ms_ = static_cast<double>(GetTickCount64());
                SetTimer(hwnd_,kAnimationTimer,kAnimationTimerMs,nullptr);
                if (animations_enabled_) {
                    SetTimer(hwnd_, kHoverTimer, 16, nullptr);
                } else {
                    hover_progress_ = 1.0f;
                    KillTimer(hwnd_, kHoverTimer);
                }
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        } else if (point.x != hover_point_.x || point.y != hover_point_.y) {
            hover_point_ = point;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (dragging_scrollbar_ || moving_window_ || resizing_window_) {
            return 0;
        }
        tracking_mouse_ = false;
        hover_action_ = UiAction::None;
        hover_filter_target_ = PopupFilterTarget::None;
        hover_delete_confirm_target_ = PopupFavoriteGroupDeleteConfirmTarget::None;
        hover_filter_date_.reset();
        hover_favorite_group_menu_hit_ = {};
        hover_item_index_ = -1;
        hover_point_ = POINT{-1, -1};
        hover_progress_ = 0.0f;
        KillTimer(hwnd_, kHoverTimer);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        const bool window_drag = moving_window_ || resizing_window_;
        const bool was_left_interaction = window_drag || mouse_down_started_inside_popup_ || left_button_was_down_;
        if (window_drag) {
            EndWindowMoveOrResize();
            return 0;
        }
        if (search_selecting_) {
            EndSearchSelectionDrag();
            mouse_down_started_inside_popup_ = false;
            left_button_was_down_ = false;
            return 0;
        }
        mouse_down_started_inside_popup_ = false;
        left_button_was_down_ = false;
        if (moving_window_) {
            moving_window_ = false;
            SetTimer(hwnd_, kOutsideClickTimer, 50, nullptr);
        }
        if (was_left_interaction) {
            EndTransientHideSuppressionSoon();
        }
        if (pressed_item_index_ >= 0) {
            POINT point = ClientPointToDips(POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            CompleteItemPress(point);
            return 0;
        }
        if (dragging_scrollbar_) {
            dragging_scrollbar_ = false;
            ReleaseCapture();
            return 0;
        }
        break;
    }
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lparam) != hwnd_) {
            const bool left_button_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            const bool was_pointer_interaction = moving_window_ || resizing_window_ ||
                                                 mouse_down_started_inside_popup_ || left_button_was_down_;
            pressed_item_index_ = -1;
            long_press_selected_ = false;
            dragging_scrollbar_ = false;
            search_selecting_ = false;
            if (left_button_down && (moving_window_ || resizing_window_ ||
                                     mouse_down_started_inside_popup_ || left_button_was_down_)) {
                mouse_down_started_inside_popup_ = true;
                left_button_was_down_ = true;
            } else {
                moving_window_ = false;
                resizing_window_ = false;
                resize_edges_ = 0;
                mouse_down_started_inside_popup_ = false;
                left_button_was_down_ = left_button_down;
            }
            if (was_pointer_interaction) {
                EndTransientHideSuppressionSoon();
            }
            KillTimer(hwnd_, kItemLongPressTimer);
            SetTimer(hwnd_, kOutsideClickTimer, 50, nullptr);
        }
        return 0;
    case WM_KEYDOWN:
        if (context_menu_open_) {
            if (wparam == VK_ESCAPE) CloseContextMenu();
            else if (wparam == VK_DOWN || wparam == VK_UP) {
                auto& row = context_submenu_open_ ? context_submenu_hover_row_ : context_hover_row_;
                const int count = context_submenu_open_ ? static_cast<int>(favorite_groups_.size()) + 1 : 7;
                row = (row + (wparam == VK_DOWN ? 1 : -1) + count) % count;
                if(context_submenu_open_) {
                    const float visible=ContextPanelRect(true).Height()-10;
                    const float top=static_cast<float>(row)*30;
                    context_submenu_scroll_=std::clamp(context_submenu_scroll_,std::max(0.0f,top+30-visible),top);
                }
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (wparam == VK_RIGHT && context_hover_row_ == 3) {
                context_submenu_open_ = true; context_submenu_hover_row_ = 0; InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (wparam == VK_LEFT) { context_submenu_open_ = false; InvalidateRect(hwnd_, nullptr, FALSE); }
            else if (wparam == VK_RETURN) ExecuteContextCommand(context_submenu_open_ ? context_submenu_hover_row_ : context_hover_row_, context_submenu_open_);
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            if (clear_confirm_open_) { clear_confirm_open_ = false; InvalidateRect(hwnd_, nullptr, FALSE); return 0; }
            if (multi_select_ && !filter_open_ && !favorite_group_menu_open_ && !pending_favorite_group_delete_) {
                ToggleMultiSelect(); return 0;
            }
            if (pending_favorite_group_delete_) {
                CloseFavoriteGroupDeleteConfirmWithMotion();
                return 0;
            }
            if (filter_open_) {
                CloseFilterPanelWithMotion();
                return 0;
            }
            if (favorite_group_menu_open_) {
                CloseFavoriteGroupMenuWithMotion();
                return 0;
            }
            Hide(L"escape");
            return 0;
        }
        if (wparam == VK_DOWN) {
            MoveSelection(1);
            return 0;
        }
        if (wparam == VK_UP) {
            MoveSelection(-1);
            return 0;
        }
        if (wparam == VK_RETURN) {
            ActivateSelection();
            return 0;
        }
        break;
    case WM_MOUSEWHEEL: {
        POINT wheel_cursor{};GetCursorPos(&wheel_cursor);ScreenToClient(hwnd_,&wheel_cursor);wheel_cursor=ClientPointToDips(wheel_cursor);
        if(expanded_item_id_ && !context_menu_open_ && !filter_open_ && !clear_confirm_open_) {
            const int index=HitTestItem(wheel_cursor);
            if(index>=0 && items_[index].id==*expanded_item_id_ && items_[index].kind!=ClipboardKind::Image &&
                wheel_cursor.y>PopupListTop()+ItemRowTop(index)-scroll_offset_+PopupMetrics().card_height-2) {
                const float width=std::max(40.0f,static_cast<float>(popup_logical_width_-140-(multi_select_?28:0)));
                const float maximum=std::max(0.0f,MeasureDetailTextHeight(ItemDetailText(items_[index]),width)-134);
                detail_scroll_=std::clamp(detail_scroll_-static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam))/120*36,0.0f,maximum);
                InvalidateRect(hwnd_,nullptr,FALSE);return 0;
            }
        }
        if(context_menu_open_ && context_submenu_open_) {
            const float total=49.0f+30.0f*static_cast<float>(favorite_groups_.size());
            context_submenu_scroll_=std::clamp(context_submenu_scroll_-static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam))/120*60,
                0.0f,std::max(0.0f,total-ContextPanelRect(true).Height()));
            InvalidateRect(hwnd_,nullptr,FALSE);return 0;
        }
        if (pending_favorite_group_delete_ || context_menu_open_ || filter_open_ || clear_confirm_open_) {
            return 0;
        }
        const float next_offset = scroll_offset_ - static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA * 72.0f;
        if (next_offset != scroll_offset_) {
            // 用户接管滚动，回到顶部的平滑动画立即让位（与浏览器行为一致）。
            CancelSmoothScroll();
            scroll_offset_ = ClampSmoothScrollOffset(next_offset);
            KickSlipAnimation();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        mouse_down_started_inside_popup_ = true;
        left_button_was_down_ = true;
        POINT point = ClientPointToDips(POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        if (context_menu_open_) {
            if (context_submenu_open_ && Contains(ContextPanelRect(true), point)) {
                const int row = HitTestContextMenu(point, true);
                if (row >= 0) ExecuteContextCommand(row, true);
            } else if (Contains(ContextPanelRect(), point)) {
                const int row = HitTestContextMenu(point);
                if (row >= 0) ExecuteContextCommand(row);
            } else CloseContextMenu();
            return 0;
        }
        if (clear_confirm_open_) {
            const auto panel = FavoriteGroupDeleteConfirmPanelRect(popup_logical_width_);
            if (Contains(FavoriteGroupDeleteConfirmDeleteRect(panel), point)) {
                std::set<int64_t> ids;
                for (const auto& item : items_) if (!item.is_favorite) ids.insert(item.id);
                store_.Clear();
                clear_confirm_open_ = false;
                BeginRemoval(ids);
                ShowToast(L"已清除历史，收藏已保留");
            } else if (!Contains(panel, point) || Contains(FavoriteGroupDeleteConfirmCancelRect(panel), point)) {
                clear_confirm_open_ = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        if (pending_favorite_group_delete_ && HandleFavoriteGroupDeleteConfirmClick(point)) return 0;
        if (filter_open_ && HandleFilterClick(point)) return 0;
        if (favorite_group_menu_open_ && HandleFavoriteGroupMenuClick(point)) return 0;
        if (const int resize_edges = ResizeHitTest(point)) {
            BeginWindowResize(resize_edges);
            return 0;
        }
        if (IsPopupHeaderDragAreaForWidth(static_cast<float>(point.x), static_cast<float>(point.y),
                                          popup_logical_width_)) {
            BeginWindowMove();
            return 0;
        }
        const float search_top = PopupSearchTop();
        const auto& metrics = PopupMetrics();
        const UiRect search_rect{static_cast<float>(metrics.margin), search_top,
                                 static_cast<float>(popup_logical_width_ - metrics.margin),
                                 search_top + static_cast<float>(metrics.search_height)};
        if (Contains(search_rect, point)) {
            const auto search_layout = BuildPopupSearchLayoutForWidth(popup_logical_width_);
            if (PopupSearchClearButtonHitTest(search_layout, !query_.empty(), point)) {
                query_.clear();
                SetWindowTextW(search_edit_, L"");
                ReloadItems();
                ResizeToCurrentItems();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            if (pending_favorite_group_delete_) {
                CloseFavoriteGroupDeleteConfirmWithMotion();
                return 0;
            }
            if (PopupShouldActivateForSearchFocus(true, search_edit_ != nullptr)) {
                ActivatePopupWindow(hwnd_);
            }
            FocusSearchEdit(search_edit_);
            search_focused_ = true;
            search_caret_on_ = true;
            search_selecting_ = true;
            const size_t hit_index = SearchTextIndexFromPoint(point);
            SetSearchSelection(hit_index, hit_index);
            SetCapture(hwnd_);
            BeginTransientHideSuppression();
            KillTimer(hwnd_, kOutsideClickTimer);
            SetTimer(hwnd_, kSearchCaretTimer, GetCaretBlinkTime(), nullptr);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        search_focused_ = false;
        search_caret_on_ = false;
        search_selecting_ = false;
        KillTimer(hwnd_, kSearchCaretTimer);
        if (PopupShouldFocusWindowForPointerPress(false) && reinterpret_cast<HWND>(GetFocus()) != hwnd_ &&
            reinterpret_cast<HWND>(GetFocus()) != search_edit_) {
            SetFocus(hwnd_);
        }
        if (HandleFavoriteGroupDeleteConfirmClick(point)) {
            return 0;
        }
        if (HandleFilterClick(point)) {
            return 0;
        }
        if (HandleFavoriteGroupMenuClick(point)) {
            return 0;
        }
        switch (HitTestAction(point)) {
        case UiAction::Scrollbar:
            dragging_scrollbar_ = true;
            SetCapture(hwnd_);
            UpdateScrollDrag(point);
            return 0;
        case UiAction::ScrollToTop:
            dragging_scrollbar_ = false;
            // 预览是 list.scrollTo({top:0, behavior:'smooth'})，不瞬跳。
            selected_index_ = ClampPopupSelectedIndex(static_cast<int>(items_.size()), 0);
            StartSmoothScrollToTop();
            KickSlipAnimation();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case UiAction::ExpandItem: {
            const int expand_item = HitTestExpandItem(point);
            if (expand_item >= 0) {
                selected_index_ = expand_item;
                ToggleExpanded(items_[expand_item].id);
            }
            return 0;
        }
        case UiAction::Close:
            Hide(L"close-button");
            return 0;
        case UiAction::Pin:
            pin_motion_from_ = pinned_open_;
            pinned_open_ = !pinned_open_;
            pin_motion_to_ = pinned_open_;
            pin_motion_progress_ = 0.0f;
            pin_motion_start_ms_ = static_cast<double>(GetTickCount64());
            if (animations_enabled_) {
                SetTimer(hwnd_, kAnimationTimer, kAnimationTimerMs, nullptr);
            } else {
                pin_motion_progress_ = 1.0f;
            }
            ShowToast(pinned_open_ ? L"已固定：粘贴后窗口保持打开" : L"已取消固定");
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case UiAction::Filter:
            if (filter_open_) {
                CloseFilterPanelWithMotion();
            } else {
                filter_open_ = true;
                closing_filter_panel_ = false;
                RefreshFavoriteGroupCounts();
                if (favorite_group_menu_open_) {
                    favorite_group_menu_open_ = false;
                }
                closing_favorite_group_menu_ = false;
                StartMotion(PopupMotionTarget::FilterPanel);
            }
            hover_progress_ = 0.0f;
            SetTimer(hwnd_, kHoverTimer, 16, nullptr);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case UiAction::MultiSelect:
            ToggleMultiSelect();
            return 0;
        case UiAction::ClearAll:
            clear_confirm_open_ = true;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case UiAction::SelectAll:
            SelectAllVisible();
            return 0;
        case UiAction::DeleteSelected: {
            const auto selected_ids = selection_.SelectedIds();
            std::set<int64_t> ids(selected_ids.begin(), selected_ids.end());
            if (!ids.empty()) { BeginRemoval(ids); ShowToast(L"已删除 " + std::to_wstring(ids.size()) + L" 条"); }
            return 0;
        }
        case UiAction::PasteSelected:
            PasteSelected();
            return 0;
        case UiAction::HistoryTab:
            if (view_mode_ != ViewMode::History) {
                view_mode_ = ViewMode::History;
                filter_open_ = false;
                favorite_group_menu_open_ = false;
                multi_select_ = false;
                selection_.Clear();
                scroll_offset_ = 0;
                ReloadItems();
                ResizeToCurrentItems();
                StartMotion(PopupMotionTarget::ViewSwitch, -1);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case UiAction::FavoritesTab:
            if (view_mode_ != ViewMode::Favorites) {
                RefreshFavoriteGroupCounts();
                view_mode_ = ViewMode::Favorites;
                filter_open_ = false;
                favorite_group_menu_open_ = false;
                multi_select_ = false;
                selection_.Clear();
                scroll_offset_ = 0;
                ReloadItems();
                ResizeToCurrentItems();
                StartMotion(PopupMotionTarget::ViewSwitch, 1);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case UiAction::FavoriteGroupMenu:
            ToggleFavoriteGroupMenu();
            return 0;
        case UiAction::AddFavoritePhrase:
            PromptAddFavoritePhrase();
            return 0;
        case UiAction::Search:
            return 0;
        case UiAction::ClearSearch:
            query_.clear();
            SetWindowTextW(search_edit_, L"");
            ReloadItems();
            ResizeToCurrentItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case UiAction::None:
            break;
        }
        const int item = HitTestItem(point);
        if (item >= 0) {
            BeginItemPress(item, point);
        }
        return 0;
    }
    case WM_RBUTTONDOWN:
        mouse_down_started_inside_popup_ = true;
        BeginTransientHideSuppression();
        KillTimer(hwnd_, kOutsideClickTimer);
        return 0;
    case WM_RBUTTONUP: {
        POINT physical_point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        const POINT logical_point = ClientPointToDips(physical_point);
        const int item_index = HitTestItem(logical_point);
        if (item_index >= 0) {
            ShowContextMenu(physical_point, item_index);
        } else {
            EndTransientHideSuppressionSoon();
            SetTimer(hwnd_, kOutsideClickTimer, 50, nullptr);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

LRESULT CALLBACK PopupWindow::SearchEditSubclassProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR ref_data) {
    auto* self = reinterpret_cast<PopupWindow*>(ref_data);
    const auto hideNativeCaret = [&]() {
        HideNativeSearchCaret(hwnd);
    };
    if(message==WM_KEYDOWN && (wparam==VK_ESCAPE || wparam==VK_UP || wparam==VK_DOWN || wparam==VK_RETURN ||
        (self->context_menu_open_ && (wparam==VK_LEFT || wparam==VK_RIGHT))))
        return self->HandleMessage(message,wparam,lparam);
    if(message==WM_MOUSEWHEEL)return self->HandleMessage(message,wparam,lparam);
    switch (message) {
    case WM_SETFOCUS:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
    case WM_PAINT:
    case WM_TIMER: {
        const LRESULT result = DefSubclassProc(hwnd, message, wparam, lparam);
        hideNativeCaret();
        return result;
    }
    case WM_IME_STARTCOMPOSITION: {
        self->composing_ = true;
        self->composition_text_.clear();
        hideNativeCaret();
        InvalidateRect(self->hwnd_, nullptr, FALSE);
        break;
    }
    case WM_IME_COMPOSITION: {
        HIMC himc = ImmGetContext(hwnd);
        if (himc) {
            if (lparam & GCS_COMPSTR) {
                const int len = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
                if (len > 0) {
                    self->composition_text_.resize(static_cast<size_t>(len));
                    ImmGetCompositionStringW(himc, GCS_COMPSTR,
                        self->composition_text_.data(), static_cast<DWORD>(len) * sizeof(wchar_t));
                } else {
                    self->composition_text_.clear();
                }
            }
            if (lparam & GCS_RESULTSTR) {
                self->composition_text_.clear();
                self->composing_ = false;
            }
            ImmReleaseContext(hwnd, himc);
        }
        hideNativeCaret();
        InvalidateRect(self->hwnd_, nullptr, FALSE);
        break;
    }
    case WM_IME_ENDCOMPOSITION: {
        self->composing_ = false;
        self->composition_text_.clear();
        hideNativeCaret();
        InvalidateRect(self->hwnd_, nullptr, FALSE);
        break;
    }
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK PopupWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    PopupWindow* window = reinterpret_cast<PopupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        window = static_cast<PopupWindow*>(create->lpCreateParams);
        window->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    return window ? window->HandleMessage(message, wparam, lparam)
                  : DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace ClipSoul
