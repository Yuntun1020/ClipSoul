#include "ClipSoul/PopupWindow.h"

#include "ClipSoul/PasteController.h"
#include "ClipSoul/PasteModel.h"
#include "ClipSoul/PopupLayout.h"
#include "ClipSoul/ResourceIds.h"
#include "ClipSoul/TextUtil.h"
#include "ClipSoul/Version.h"
#include "ClipSoul/Win32Util.h"

#include <commctrl.h>
#include <imm.h>
#include <oleauto.h>
#include <UIAutomationClient.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <string>

namespace ClipSoul {
namespace {
constexpr wchar_t kPopupClass[] = L"ClipSoul.PopupWindow";
constexpr UINT_PTR kAnimationTimer = 42;
constexpr UINT_PTR kHoverTimer = 43;
constexpr UINT_PTR kSearchCaretTimer = 44;
constexpr int kContextDelete = 3001;
constexpr int kContextPin = 3002;
constexpr int kContextFavorite = 3003;
constexpr int kPhraseEditId = 4101;
constexpr int kPhraseSaveId = 4102;
constexpr int kPhraseCancelId = 4103;
constexpr wchar_t kPhrasePromptClass[] = L"ClipSoul.FavoritePhrasePrompt";
constexpr UINT_PTR kPhraseHoverTimer = 62;

template <typename T>
void ReleasePtr(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

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

bool Contains(const UiRect& rect, POINT value) {
    return value.x >= rect.left && value.x <= rect.right && value.y >= rect.top && value.y <= rect.bottom;
}

bool ContainsWindow(HWND parent, HWND candidate) {
    if (!parent || !candidate) {
        return false;
    }
    return parent == candidate || IsChild(parent, candidate);
}

HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

struct PhrasePromptState {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    std::wstring text;
    bool accepted = false;
    bool tracking_mouse = false;
    int hover_target = 0;
    float hover_progress = 0.0f;
};

void StyleChildControl(HWND control) {
    auto font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void DrawCenteredText(HDC dc, const RECT& rect, const wchar_t* text) {
    DrawTextW(dc, text, static_cast<int>(wcslen(text)), const_cast<RECT*>(&rect),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawGdiRoundedPanel(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF stroke) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, stroke);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void FillGdiSolid(HDC dc, const RECT& rect, COLORREF fill) {
    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

int PhraseHitTarget(int x, int y) {
    if (x >= 286 && x <= 310 && y >= 12 && y <= 36) return 1;
    if (x >= 146 && x <= 216 && y >= 166 && y <= 194) return 2;
    if (x >= 228 && x <= 298 && y >= 166 && y <= 194) return 3;
    return 0;
}

LRESULT CALLBACK PhrasePromptProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<PhrasePromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        state = static_cast<PhrasePromptState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_CREATE: {
        state->edit = CreateWindowExW(0, L"EDIT", L"",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL |
                                          ES_WANTRETURN,
                                      24, 78, 272, 72, hwnd, ControlId(kPhraseEditId),
                                      state->instance, nullptr);
        StyleChildControl(state->edit);
        SetFocus(state->edit);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HDC mem_dc = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, rc.right - rc.left, rc.bottom - rc.top);
        HGDIOBJ old_bitmap = SelectObject(mem_dc, bitmap);
        FillGdiSolid(mem_dc, rc, RGB(248, 251, 255));

        RECT panel_rect{0, 0, rc.right - 1, rc.bottom - 1};
        DrawGdiRoundedPanel(mem_dc, panel_rect, 18, RGB(248, 251, 255), RGB(220, 231, 244));
        RECT input_rect{22, 74, 298, 154};
        DrawGdiRoundedPanel(mem_dc, input_rect, 12, RGB(255, 255, 255), RGB(226, 234, 242));

        const bool save_hover = state->hover_target == 2 && state->hover_progress > 0.0f;
        RECT save_button{146, 166, 216, 194};
        DrawGdiRoundedPanel(mem_dc, save_button, 12,
                            save_hover ? RGB(13, 148, 145) : RGB(14, 165, 164),
                            save_hover ? RGB(13, 148, 145) : RGB(14, 165, 164));

        const bool cancel_hover = state->hover_target == 3 && state->hover_progress > 0.0f;
        RECT cancel_button{228, 166, 298, 194};
        DrawGdiRoundedPanel(mem_dc, cancel_button, 12,
                            cancel_hover ? RGB(244, 255, 253) : RGB(255, 255, 255),
                            cancel_hover ? RGB(101, 218, 210) : RGB(220, 231, 244));

        SetBkMode(mem_dc, TRANSPARENT);
        auto font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HGDIOBJ old_font = SelectObject(mem_dc, font);
        SetTextColor(mem_dc, RGB(23, 32, 51));
        TextOutW(mem_dc, 22, 18, L"添加常用语", 5);
        if (state->hover_target == 1 && state->hover_progress > 0.0f) {
            RECT close_rect{286, 12, 310, 36};
            DrawGdiRoundedPanel(mem_dc, close_rect, 8, RGB(255, 241, 242), RGB(242, 85, 90));
        }
        HPEN close_pen = CreatePen(PS_SOLID, 2,
                                   state->hover_target == 1 ? RGB(229, 72, 77) : RGB(38, 54, 75));
        HGDIOBJ old_pen = SelectObject(mem_dc, close_pen);
        MoveToEx(mem_dc, 294, 20, nullptr);
        LineTo(mem_dc, 302, 28);
        MoveToEx(mem_dc, 302, 20, nullptr);
        LineTo(mem_dc, 294, 28);
        SelectObject(mem_dc, old_pen);
        DeleteObject(close_pen);
        SetTextColor(mem_dc, RGB(123, 135, 152));
        TextOutW(mem_dc, 22, 48, L"保存后会出现在收藏夹", 10);
        SetTextColor(mem_dc, RGB(255, 255, 255));
        RECT save_rect{146, 166, 216, 194};
        DrawCenteredText(mem_dc, save_rect, L"保存");
        SetTextColor(mem_dc, RGB(23, 32, 51));
        RECT cancel_rect{228, 166, 298, 194};
        DrawCenteredText(mem_dc, cancel_rect, L"取消");
        SelectObject(mem_dc, old_font);
        BitBlt(dc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, mem_dc, 0, 0, SRCCOPY);
        SelectObject(mem_dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(mem_dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);
        const int target = PhraseHitTarget(x, y);
        if (target == 1) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (target == 2) {
            const int length = GetWindowTextLengthW(state->edit);
            std::wstring value(static_cast<size_t>(length) + 1, L'\0');
            if (length > 0) {
                GetWindowTextW(state->edit, value.data(), length + 1);
            }
            value.resize(static_cast<size_t>(length));
            state->text = NormalizeWhitespace(value);
            state->accepted = !state->text.empty();
            DestroyWindow(hwnd);
            return 0;
        }
        if (target == 3) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_TIMER:
        if (wparam == kPhraseHoverTimer) {
            state->hover_progress = std::min(1.0f, state->hover_progress + 0.18f);
            if (state->hover_progress >= 1.0f) KillTimer(hwnd, kPhraseHoverTimer);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        if (!state->tracking_mouse) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            state->tracking_mouse = true;
        }
        const int target = PhraseHitTarget(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (target != state->hover_target) {
            state->hover_target = target;
            state->hover_progress = 0.0f;
            SetTimer(hwnd, kPhraseHoverTimer, 16, nullptr);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        state->tracking_mouse = false;
        state->hover_target = 0;
        state->hover_progress = 0.0f;
        KillTimer(hwnd, kPhraseHoverTimer);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_COMMAND:
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

std::optional<std::wstring> PromptFavoritePhrase(HWND owner, HINSTANCE instance) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = PhrasePromptProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.lpszClassName = kPhrasePromptClass;
    RegisterClassW(&wc);

    PhrasePromptState state;
    state.instance = instance;

    RECT owner_rect{};
    GetWindowRect(owner, &owner_rect);
    const int width = 320;
    const int height = 230;
    const int x = owner_rect.left + (owner_rect.right - owner_rect.left - width) / 2;
    const int y = owner_rect.top + 92;
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kPhrasePromptClass, L"ClipSoul",
                                WS_POPUP, x, y, width, height, owner, nullptr, instance,
                                &state);
    if (!hwnd) {
        return std::nullopt;
    }

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    MSG message{};
    while (IsWindow(hwnd) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    SetFocus(owner);
    if (state.accepted) {
        return state.text;
    }
    return std::nullopt;
}

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
    return meta;
}

std::wstring ItemTime(const HistoryItem& item) {
    std::time_t value = static_cast<std::time_t>(item.created_at_unix);
    std::tm local_time{};
    if (localtime_s(&local_time, &value) != 0) {
        return {};
    }
    wchar_t buffer[16]{};
    wcsftime(buffer, std::size(buffer), L"%H:%M", &local_time);
    return buffer;
}

D2D1_COLOR_F KindColor(ClipboardKind kind) {
    switch (kind) {
    case ClipboardKind::Text:
    case ClipboardKind::Html:
        return D2D1::ColorF(0x3B82F6, 0.86f);
    case ClipboardKind::Image:
        return D2D1::ColorF(0x10B981, 0.86f);
    case ClipboardKind::Files:
        return D2D1::ColorF(0x8B5CF6, 0.86f);
    case ClipboardKind::Link:
        return D2D1::ColorF(0xF97316, 0.86f);
    }
    return D2D1::ColorF(0x94A3B8, 0.86f);
}

D2D1_COLOR_F ColorWithAlpha(uint32_t rgb, float alpha) {
    return D2D1::ColorF(rgb, alpha);
}

const wchar_t* KindGlyph(ClipboardKind kind) {
    switch (kind) {
    case ClipboardKind::Text:
    case ClipboardKind::Html:
        return L"T";
    case ClipboardKind::Image:
        return L"◐";
    case ClipboardKind::Files:
        return L"F";
    case ClipboardKind::Link:
        return L"↗";
    }
    return L"T";
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
    swprintf_s(buffer, L"%04d年%d月", year, month);
    return buffer;
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

std::filesystem::path IconPath(const wchar_t* filename) {
    wchar_t module_path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    std::filesystem::path base = std::filesystem::path(module_path).parent_path();
    for (int i = 0; i < 4; ++i) {
        const auto candidate = base / L"assets" / L"icons" / filename;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        base = base.parent_path();
    }
    return std::filesystem::path(module_path).parent_path() / L"assets" / L"icons" / filename;
}

bool LoadWicFrameFromResource(IWICImagingFactory* factory, int resource_id, IWICBitmapDecoder** decoder,
                              IWICBitmapFrameDecode** frame) {
    if (!factory || !decoder || !frame || resource_id == 0) {
        return false;
    }

    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (!resource) {
        return false;
    }
    HGLOBAL loaded = LoadResource(nullptr, resource);
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    const DWORD byte_count = SizeofResource(nullptr, resource);
    if (!bytes || byte_count == 0) {
        return false;
    }

    IWICStream* stream = nullptr;
    if (FAILED(factory->CreateStream(&stream))) {
        return false;
    }

    bool ok = false;
    if (SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(bytes)), byte_count)) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, decoder)) &&
        SUCCEEDED((*decoder)->GetFrame(0, frame))) {
        ok = true;
    }
    ReleasePtr(stream);
    return ok;
}

bool LoadWicFrameFromFile(IWICImagingFactory* factory, const std::filesystem::path& path, IWICBitmapDecoder** decoder,
                          IWICBitmapFrameDecode** frame) {
    return factory && decoder && frame &&
           SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                        WICDecodeMetadataCacheOnLoad, decoder)) &&
           SUCCEEDED((*decoder)->GetFrame(0, frame));
}

bool TryGetCaretAnchor(HWND target, POINT& anchor) {
    if (!target) {
        return false;
    }
    const DWORD thread_id = GetWindowThreadProcessId(target, nullptr);
    GUITHREADINFO info{sizeof(info)};
    if (!GetGUIThreadInfo(thread_id, &info) || !info.hwndCaret) {
        return false;
    }
    POINT point{info.rcCaret.left, info.rcCaret.bottom};
    if (!ClientToScreen(info.hwndCaret, &point)) {
        return false;
    }
    anchor = point;
    return true;
}

bool RectFromSafeArray(SAFEARRAY* rectangles, RECT& rect) {
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
    rect.right = static_cast<LONG>(std::lround(values[0] + std::max(1.0, values[2])));
    rect.bottom = static_cast<LONG>(std::lround(values[1] + std::max(1.0, values[3])));
    return true;
}

bool TryGetAutomationCaretAnchor(POINT& anchor) {
    IUIAutomation* automation = nullptr;
    IUIAutomationElement* focused = nullptr;
    IUIAutomationTextPattern2* text2 = nullptr;
    IUIAutomationTextPattern* text = nullptr;
    IUIAutomationTextRange* range = nullptr;
    IUIAutomationTextRangeArray* ranges = nullptr;
    SAFEARRAY* rectangles = nullptr;
    BOOL active = FALSE;
    bool ok = false;

    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->GetFocusedElement(&focused)) && focused) {
        if (SUCCEEDED(focused->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&text2))) && text2 &&
            SUCCEEDED(text2->GetCaretRange(&active, &range)) && range &&
            SUCCEEDED(range->GetBoundingRectangles(&rectangles))) {
            RECT rect{};
            if (RectFromSafeArray(rectangles, rect)) {
                anchor = POINT{rect.left, rect.bottom};
                ok = true;
            }
        }
        if (!ok && SUCCEEDED(focused->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text))) && text &&
            SUCCEEDED(text->GetSelection(&ranges)) && ranges) {
            int length = 0;
            if (SUCCEEDED(ranges->get_Length(&length)) && length > 0 &&
                SUCCEEDED(ranges->GetElement(0, &range)) && range &&
                SUCCEEDED(range->GetBoundingRectangles(&rectangles))) {
                RECT rect{};
                if (RectFromSafeArray(rectangles, rect)) {
                    anchor = POINT{rect.left, rect.bottom};
                    ok = true;
                }
            }
        }
    }

    if (rectangles) SafeArrayDestroy(rectangles);
    ReleasePtr(ranges);
    ReleasePtr(range);
    ReleasePtr(text);
    ReleasePtr(text2);
    ReleasePtr(focused);
    ReleasePtr(automation);
    return ok;
}

bool TryGetTextInputAnchor(HWND target, POINT& anchor) {
    return TryGetCaretAnchor(target, anchor) || TryGetAutomationCaretAnchor(anchor);
}

} // namespace

PopupWindow::PopupWindow(HINSTANCE instance, HistoryStore& store, PasteController& paste_controller)
    : instance_(instance),
      store_(store),
      paste_controller_(paste_controller) {
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
    ReleasePtr(ellipsis_trimming_sign_);
    ReleasePtr(title_format_);
    ReleasePtr(body_format_);
    ReleasePtr(small_format_);
    ReleasePtr(centered_small_format_);
    ReleasePtr(wic_factory_);
    ReleasePtr(dwrite_factory_);
    ReleasePtr(d2d_factory_);
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
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kPopupClass, L"ClipSoul",
                            WS_POPUP | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, size.cx, size.cy, owner, nullptr,
                            instance_, this);
    if (!hwnd_) {
        return false;
    }
    search_edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | ES_AUTOHSCROLL,
                                   0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchEditId)),
                                   instance_, nullptr);
    if (search_edit_) {
        auto font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessageW(search_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(search_edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
        SendMessageW(search_edit_, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"搜索历史记录"));
        SendMessageW(search_edit_, EM_SETCUEBANNER, FALSE,
                     reinterpret_cast<LPARAM>(L"\u641c\u7d22\u5386\u53f2\u8bb0\u5f55"));
        UpdateSearchEditBounds();
    }
    ApplyBackdrop();
    return true;
}

void PopupWindow::Show(HWND target) {
    paste_target_ = target;
    ReloadItems();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const UINT dpi = CurrentDpi();
    const SIZE size = PhysicalPopupSize(dpi, static_cast<int>(items_.size()));
    UINT flags = SWP_SHOWWINDOW;
    int x = 0;
    int y = 0;
    const POINT position = ResolvePopupPosition(target, size, work, dpi);
    x = position.x;
    y = position.y;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, size.cx, size.cy, flags);
    UpdateSearchEditBounds();
    SyncSearchEdit();
    open_progress_ = 0.0f;
    SetTimer(hwnd_, kAnimationTimer, 16, nullptr);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    search_focused_ = false;
    search_caret_on_ = false;
    KillTimer(hwnd_, kSearchCaretTimer);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool PopupWindow::IsVisible() const {
    return hwnd_ && IsWindowVisible(hwnd_);
}

void PopupWindow::Hide() {
    ShowWindow(hwnd_, SW_HIDE);
}

void PopupWindow::Refresh() {
    ReloadItems();
    if (hwnd_) {
        ResizeToCurrentItems();
        SyncSearchEdit();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PopupWindow::ReloadItems() {
    UpdateThemeFromSettings();
    items_ = store_.Query(BuildQuery());
    scroll_offset_ = ClampPopupScrollOffset(static_cast<int>(items_.size()), scroll_offset_);
    selected_index_ = std::clamp(selected_index_, 0, std::max(0, static_cast<int>(items_.size()) - 1));
}

void PopupWindow::ResizeToCurrentItems() {
    if (!hwnd_) {
        return;
    }
    RECT current{};
    GetWindowRect(hwnd_, &current);
    const SIZE size = PhysicalPopupSize(CurrentDpi(), static_cast<int>(items_.size()));
    SetWindowPos(hwnd_, nullptr, current.left, current.top, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
    UpdateSearchEditBounds();
}

void PopupWindow::SyncSearchEdit() {
    if (search_edit_ && GetWindowTextLengthW(search_edit_) != static_cast<int>(query_.size())) {
        SetWindowTextW(search_edit_, query_.c_str());
    }
    if (search_edit_) {
        RedrawWindow(search_edit_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }
}

void PopupWindow::UpdateSearchEditBounds() {
    if (!search_edit_) {
        return;
    }
    const auto& metrics = PopupMetrics();
    const UINT dpi = CurrentDpi();
    const float search_top = PopupSearchTop();
    SetWindowPos(search_edit_, nullptr, 0, 0, 1, 1, SWP_NOZORDER | SWP_NOACTIVATE);
}

void PopupWindow::UpdateThemeFromSettings() {
    const int next_theme = std::clamp(store_.LoadSettings().theme_mode, 0, 2);
    if (next_theme == theme_mode_ && text_brush_) {
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

float PopupWindow::SearchCaretOffsetDips() const {
    if (query_.empty() || !dwrite_factory_ || !small_format_) {
        return 0.0f;
    }

    const auto search_layout = BuildPopupSearchLayout();
    IDWriteTextLayout* text_layout = nullptr;
    float offset = 0.0f;
    if (SUCCEEDED(dwrite_factory_->CreateTextLayout(query_.c_str(), static_cast<UINT32>(query_.size()),
                                                    small_format_, search_layout.text.Width(),
                                                    search_layout.text.Height(), &text_layout)) &&
        text_layout) {
        FLOAT caret_offset = 0.0f;
        FLOAT caret_y = 0.0f;
        DWRITE_HIT_TEST_METRICS hit_metrics{};
        const UINT32 text_length = static_cast<UINT32>(query_.size());
        if (SUCCEEDED(text_layout->HitTestTextPosition(text_length, FALSE, &caret_offset, &caret_y, &hit_metrics))) {
            offset = caret_offset;
        } else if (text_length > 0 &&
                   SUCCEEDED(text_layout->HitTestTextPosition(text_length - 1, TRUE, &caret_offset, &caret_y,
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

POINT PopupWindow::SearchImeAnchorClient() const {
    const POINT anchor = PopupSearchImeAnchorDips(BuildPopupSearchLayout(), SearchCaretOffsetDips());
    const UINT dpi = CurrentDpi();
    return POINT{
        MulDiv(anchor.x, static_cast<int>(dpi), 96),
        MulDiv(anchor.y, static_cast<int>(dpi), 96),
    };
}

void PopupWindow::UpdateSearchImePosition() {
    if (!hwnd_ || !PopupSearchShouldUpdateImePosition(search_focused_, updating_search_ime_)) {
        return;
    }

    HIMC context = ImmGetContext(hwnd_);
    if (!context) {
        return;
    }

    updating_search_ime_ = true;
    const POINT anchor = SearchImeAnchorClient();
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_FORCE_POSITION;
    composition.ptCurrentPos = anchor;
    ImmSetCompositionWindow(context, &composition);

    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = anchor;
    ImmSetCandidateWindow(context, &candidate);
    ImmReleaseContext(hwnd_, context);
    updating_search_ime_ = false;
}

HistoryQuery PopupWindow::BuildQuery() const {
    HistoryQuery query;
    query.limit = 60;
    query.text = query_;
    query.kinds = filter_kinds_;
    query.favorites_only = view_mode_ == ViewMode::Favorites;
    if (date_filter_.start) {
        query.start_unix = LocalDateUnix(*date_filter_.start, false);
    }
    if (date_filter_.end) {
        query.end_unix = LocalDateUnix(*date_filter_.end, true);
    }
    return query;
}

void PopupWindow::MoveSelection(int delta) {
    if (items_.empty()) return;
    selected_index_ = std::clamp(selected_index_ + delta, 0, static_cast<int>(items_.size()) - 1);
    if (selected_index_ < scroll_offset_) {
        scroll_offset_ = selected_index_;
    } else if (selected_index_ >= scroll_offset_ + PopupVisibleCardCapacity()) {
        scroll_offset_ = ClampPopupScrollOffset(static_cast<int>(items_.size()),
                                                selected_index_ - PopupVisibleCardCapacity() + 1);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::ActivateSelection() {
    if (items_.empty()) return;
    const auto& item = items_[selected_index_];
    if (multi_select_) {
        selection_.Toggle(item.id);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (paste_controller_.RestoreToClipboard(item, hwnd_)) {
        if (ShouldHidePopupAfterPaste(pinned_open_)) {
            ShowWindow(hwnd_, SW_HIDE);
        }
        paste_controller_.SendPaste(paste_target_);
    }
}

void PopupWindow::ToggleMultiSelect() {
    multi_select_ = !multi_select_;
    selection_.Clear();
    if (multi_select_) {
        filter_open_ = false;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::ToggleFavorite(int64_t id) {
    if (auto item = store_.Get(id)) {
        store_.SetFavorite(id, !item->is_favorite);
        ReloadItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PopupWindow::TogglePinned(int64_t id) {
    if (auto item = store_.Get(id)) {
        store_.SetPinned(id, !item->is_pinned);
        ReloadItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PopupWindow::DeleteItem(int64_t id) {
    store_.Delete(id);
    ReloadItems();
    InvalidateRect(hwnd_, nullptr, FALSE);
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

    bool pasted_any = false;
    for (const auto& operation : BuildMultiPasteOperations(selected_items)) {
        if (paste_controller_.RestoreMultipleToClipboard(operation.items, hwnd_)) {
            paste_controller_.SendPaste(paste_target_);
            pasted_any = true;
            Sleep(70);
        }
    }
    if (pasted_any) {
        selection_.Clear();
        multi_select_ = false;
        if (ShouldHidePopupAfterPaste(pinned_open_)) {
            ShowWindow(hwnd_, SW_HIDE);
        } else {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
}

void PopupWindow::PromptAddFavoritePhrase() {
    prompt_open_ = true;
    if (const auto phrase = PromptFavoritePhrase(hwnd_, instance_)) {
        if (store_.AddFavoritePhrase(*phrase)) {
            view_mode_ = ViewMode::Favorites;
            filter_open_ = false;
            multi_select_ = false;
            selection_.Clear();
            ReloadItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    prompt_open_ = false;
}

UINT PopupWindow::CurrentDpi() const {
    return DpiForWindowHandle(hwnd_);
}

SIZE PopupWindow::PhysicalPopupSize(UINT dpi, int visible_items) const {
    const auto& metrics = PopupMetrics();
    const int height = PopupHeightForVisibleItems(visible_items);
    return SIZE{ScalePopupMetricForDpi(metrics.width, dpi), ScalePopupMetricForDpi(height, dpi)};
}

POINT PopupWindow::ResolvePopupPosition(HWND target, SIZE size, RECT work, UINT dpi) const {
    POINT anchor{};
    if (!TryGetTextInputAnchor(target, anchor)) {
        return PopupBottomRightFallback(size, work, dpi);
    }
    return ClampPopupToWorkArea(anchor, size, work, dpi);
}

void PopupWindow::HideIfInactive(HWND next_active) {
    if (pinned_open_ || prompt_open_ || !IsVisible()) {
        return;
    }
    if (ContainsWindow(hwnd_, next_active)) {
        return;
    }
    ShowWindow(hwnd_, SW_HIDE);
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
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          18.0f, L"zh-CN", &title_format_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          14.0f, L"zh-CN", &body_format_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          12.0f, L"zh-CN", &small_format_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          12.0f, L"zh-CN", &centered_small_format_);
        centered_small_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        centered_small_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        body_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        small_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        DWRITE_TRIMMING trimming{};
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        dwrite_factory_->CreateEllipsisTrimmingSign(body_format_, &ellipsis_trimming_sign_);
        for (auto* format : {title_format_, body_format_, small_format_, centered_small_format_}) {
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            format->SetTrimming(&trimming, ellipsis_trimming_sign_);
        }
    }
    if (!wic_factory_) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_factory_));
    }
    if (!render_target_) {
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        d2d_factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                         D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)),
            &render_target_);
        const float dpi = static_cast<float>(CurrentDpi());
        render_target_->SetDpi(dpi, dpi);
        render_target_->CreateSolidColorBrush(D2D1::ColorF(0x172033, 0.94f), &text_brush_);
        render_target_->CreateSolidColorBrush(D2D1::ColorF(0x7B8798, 0.84f), &muted_brush_);
        render_target_->CreateSolidColorBrush(D2D1::ColorF(0x0EA5A4, 0.9f), &accent_brush_);
        UpdateThemeFromSettings();
    }
}

void PopupWindow::DiscardDeviceResources() {
    ReleasePtr(pin_icon_);
    ReleasePtr(pin_active_icon_);
    ReleasePtr(close_icon_);
    ReleasePtr(filter_icon_);
    ReleasePtr(filter_active_icon_);
    ReleasePtr(multi_select_icon_);
    ReleasePtr(multi_select_active_icon_);
    ReleasePtr(trash_icon_);
    ReleasePtr(text_kind_icon_);
    ReleasePtr(link_kind_icon_);
    for (auto& [_, bitmap] : image_preview_cache_) {
        ReleasePtr(bitmap);
    }
    image_preview_cache_.clear();
    for (auto& [_, bitmap] : file_icon_cache_) {
        ReleasePtr(bitmap);
    }
    file_icon_cache_.clear();
    ReleasePtr(text_brush_);
    ReleasePtr(muted_brush_);
    ReleasePtr(accent_brush_);
    ReleasePtr(render_target_);
}

ID2D1Bitmap* PopupWindow::LoadIconBitmap(IconId icon) {
    ID2D1Bitmap** slot = nullptr;
    const wchar_t* filename = nullptr;
    int resource_id = 0;
    switch (icon) {
    case IconId::Pin:
        slot = &pin_icon_;
        filename = L"pin.png";
        resource_id = IDR_CLIPSOUL_ICON_PIN;
        break;
    case IconId::PinActive:
        slot = &pin_active_icon_;
        filename = L"pin-active.png";
        resource_id = IDR_CLIPSOUL_ICON_PIN_ACTIVE;
        break;
    case IconId::Close:
        slot = &close_icon_;
        filename = L"close.png";
        resource_id = IDR_CLIPSOUL_ICON_CLOSE;
        break;
    case IconId::Filter:
        slot = &filter_icon_;
        filename = L"filter.png";
        resource_id = IDR_CLIPSOUL_ICON_FILTER;
        break;
    case IconId::FilterActive:
        slot = &filter_active_icon_;
        filename = L"filter-active.png";
        resource_id = IDR_CLIPSOUL_ICON_FILTER_ACTIVE;
        break;
    case IconId::MultiSelect:
        slot = &multi_select_icon_;
        filename = L"multi-select.png";
        resource_id = IDR_CLIPSOUL_ICON_MULTI_SELECT;
        break;
    case IconId::MultiSelectActive:
        slot = &multi_select_active_icon_;
        filename = L"multi-select-active.png";
        resource_id = IDR_CLIPSOUL_ICON_MULTI_SELECT_ACTIVE;
        break;
    case IconId::Trash:
        slot = &trash_icon_;
        filename = L"trash.png";
        resource_id = IDR_CLIPSOUL_ICON_TRASH;
        break;
    case IconId::TextKind:
        slot = &text_kind_icon_;
        filename = L"text-kind.png";
        resource_id = IDR_CLIPSOUL_ICON_TEXT_KIND;
        break;
    case IconId::LinkKind:
        slot = &link_kind_icon_;
        filename = L"link-kind.png";
        resource_id = IDR_CLIPSOUL_ICON_LINK_KIND;
        break;
    }
    if (!slot || *slot || !wic_factory_ || !render_target_) {
        return slot ? *slot : nullptr;
    }

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    if ((LoadWicFrameFromResource(wic_factory_, resource_id, &decoder, &frame) ||
         LoadWicFrameFromFile(wic_factory_, IconPath(filename), &decoder, &frame)) &&
        SUCCEEDED(wic_factory_->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeMedianCut))) {
        render_target_->CreateBitmapFromWicBitmap(converter, nullptr, slot);
    }
    ReleasePtr(converter);
    ReleasePtr(frame);
    ReleasePtr(decoder);
    return *slot;
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
    if (size <= 0) {
        image_preview_cache_[key] = nullptr;
        return nullptr;
    }
    file.seekg(0, std::ios::beg);
    std::vector<char> dib(static_cast<size_t>(size));
    file.read(dib.data(), static_cast<std::streamsize>(dib.size()));
    if (!file || dib.size() < sizeof(BITMAPINFOHEADER)) {
        image_preview_cache_[key] = nullptr;
        return nullptr;
    }

    const auto* header = reinterpret_cast<const BITMAPINFOHEADER*>(dib.data());
    if (header->biWidth <= 0 || header->biHeight == 0 || header->biPlanes != 1 ||
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
    if (required > dib.size()) {
        image_preview_cache_[key] = nullptr;
        return nullptr;
    }

    std::vector<uint32_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
    const auto* src_base = reinterpret_cast<const uint8_t*>(dib.data() + pixel_offset);
    for (int y = 0; y < height; ++y) {
        const int src_y = top_down ? y : height - 1 - y;
        const auto* src = src_base + row_stride * static_cast<size_t>(src_y);
        auto* dst = pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
        for (int x = 0; x < width; ++x) {
            const uint8_t b = src[x * (header->biBitCount / 8) + 0];
            const uint8_t g = src[x * (header->biBitCount / 8) + 1];
            const uint8_t r = src[x * (header->biBitCount / 8) + 2];
            dst[x] = 0xFF000000u | (static_cast<uint32_t>(r) << 16u) | (static_cast<uint32_t>(g) << 8u) |
                     static_cast<uint32_t>(b);
        }
    }

    const auto props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    render_target_->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)),
                                 pixels.data(), static_cast<UINT32>(width * sizeof(uint32_t)), props, &bitmap);
    image_preview_cache_[key] = bitmap;
    return bitmap;
}

ID2D1Bitmap* PopupWindow::LoadFileIconBitmap(const std::wstring& path) {
    if (path.empty() || !wic_factory_ || !render_target_) {
        return nullptr;
    }
    const std::wstring extension = std::filesystem::path(path).extension().wstring();
    const std::wstring key = extension.empty() ? path : extension;
    if (const auto found = file_icon_cache_.find(key); found != file_icon_cache_.end()) {
        return found->second;
    }

    SHFILEINFOW info{};
    const DWORD_PTR result = SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                                           SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    if (!result || !info.hIcon) {
        file_icon_cache_[key] = nullptr;
        return nullptr;
    }

    IWICBitmap* wic_bitmap = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID2D1Bitmap* bitmap = nullptr;
    if (SUCCEEDED(wic_factory_->CreateBitmapFromHICON(info.hIcon, &wic_bitmap)) &&
        SUCCEEDED(wic_factory_->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(wic_bitmap, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                        0.0, WICBitmapPaletteTypeMedianCut))) {
        render_target_->CreateBitmapFromWicBitmap(converter, nullptr, &bitmap);
    }
    DestroyIcon(info.hIcon);
    ReleasePtr(converter);
    ReleasePtr(wic_bitmap);
    file_icon_cache_[key] = bitmap;
    return bitmap;
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
    if (ID2D1Bitmap* bitmap = LoadIconBitmap(icon)) {
        render_target_->DrawBitmap(bitmap, Rect(rect), opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
}

void PopupWindow::DrawCardMedia(const HistoryItem& item, const PopupCardLayout& card, ID2D1SolidColorBrush* brush) {
    const auto kind_color = KindColor(item.kind);
    if (item.kind == ClipboardKind::Image) {
        brush->SetColor(D2D1::ColorF(0xFFFFFF, 0.46f));
        render_target_->FillRoundedRectangle(RoundRect(card.image_preview, 10), brush);
        if (ID2D1Bitmap* bitmap = LoadImagePreviewBitmap(item.payload_path)) {
            const auto size = bitmap->GetSize();
            const auto fitted = FitImageRectToBounds(size.width, size.height, ShrinkRect(card.image_preview, 2.0f, 2.0f));
            render_target_->DrawBitmap(bitmap, Rect(fitted), 0.96f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            brush->SetColor(D2D1::ColorF(0xFFFFFF, 0.62f));
            render_target_->DrawRoundedRectangle(RoundRect(card.image_preview, 8), brush, 1.0f);
            return;
        }
    }

    if (item.kind == ClipboardKind::Files && !item.files.empty()) {
        if (ID2D1Bitmap* bitmap = LoadFileIconBitmap(item.files.front())) {
            render_target_->DrawBitmap(bitmap, Rect(card.file_icon), 0.96f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            return;
        }
    }

    if (item.kind == ClipboardKind::Text || item.kind == ClipboardKind::Html || item.kind == ClipboardKind::Link) {
        const bool link = item.kind == ClipboardKind::Link;
        const UiRect badge = ShrinkRect(card.stripe, -4.0f, -4.0f);
        brush->SetColor(link ? D2D1::ColorF(0xFFF4DE, 0.92f) : D2D1::ColorF(0xEEF2FF, 0.90f));
        render_target_->FillRoundedRectangle(RoundRect(badge, 6), brush);
        brush->SetColor(link ? D2D1::ColorF(0xF59E0B, 0.48f) : D2D1::ColorF(0x7C8EF8, 0.42f));
        render_target_->DrawRoundedRectangle(RoundRect(badge, 6), brush, 1.0f);
        DrawIcon(link ? IconId::LinkKind : IconId::TextKind, CenteredRect(card.stripe, 14.0f, 14.0f), 0.92f);
        return;
    }

    brush->SetColor(D2D1::ColorF(kind_color.r, kind_color.g, kind_color.b, 0.12f));
    render_target_->FillRoundedRectangle(RoundRect(card.stripe, 6), brush);
    brush->SetColor(kind_color);
    render_target_->DrawRoundedRectangle(RoundRect(card.stripe, 6), brush, 1.0f);
    render_target_->DrawTextW(KindGlyph(item.kind), 1, centered_small_format_, Rect(card.stripe), brush);
}

void PopupWindow::Paint() {
    EnsureDeviceResources();
    const auto& metrics = PopupMetrics();
    const auto palette = ResolvePopupThemePalette(theme_mode_, IsSystemDarkTheme());
    render_target_->BeginDraw();
    render_target_->Clear(D2D1::ColorF(0x000000, 0.0f));

    ID2D1SolidColorBrush* brush = nullptr;
    render_target_->CreateSolidColorBrush(ColorWithAlpha(palette.window_tint, palette.window_opacity), &brush);
    render_target_->FillRoundedRectangle(
        RoundRect(1.0f, 1.0f, metrics.width - 1.0f, metrics.height - 1.0f,
                  static_cast<float>(metrics.corner_radius)),
        brush);
    brush->SetColor(ColorWithAlpha(palette.dark ? 0x5A6473 : 0xFFFFFF, palette.dark ? 0.42f : 0.62f));
    render_target_->DrawRoundedRectangle(
        RoundRect(1.0f, 1.0f, metrics.width - 1.0f, metrics.height - 1.0f,
                  static_cast<float>(metrics.corner_radius)),
        brush, 1.0f);

    const auto header = BuildPopupHeaderLayout();
    render_target_->DrawTextW(L"ClipSoul", 8, title_format_, Rect(header.title), text_brush_);
    render_target_->DrawTextW(kClipSoulVersion.data(), static_cast<UINT32>(kClipSoulVersion.size()), small_format_,
                              Rect(header.title.left + 80.0f, header.title.top + 4.0f,
                                   header.title.left + 142.0f, header.title.bottom),
                              muted_brush_);
    auto drawHeaderButton = [&](const UiRect& rect, bool active, UiAction action) {
        const bool hovered = hover_action_ == action;
        const float hover = hovered ? hover_progress_ : 0.0f;
        if (active || hovered) {
            if (action == UiAction::Close && hovered) {
                brush->SetColor(D2D1::ColorF(0xFFF1F2, 0.40f + 0.35f * hover));
            } else {
                brush->SetColor(active ? D2D1::ColorF(0xDDFCF8, 0.48f + 0.20f * hover)
                                       : D2D1::ColorF(0xF4FFFD, 0.70f * hover));
            }
            render_target_->FillRoundedRectangle(RoundRect(rect, 6), brush);
            if (action == UiAction::Close && hovered) {
                brush->SetColor(D2D1::ColorF(0xE5484D, 0.32f + 0.42f * hover));
            } else {
                brush->SetColor(active ? D2D1::ColorF(0x14B8A6, 0.42f)
                                       : D2D1::ColorF(0xDDE5EF, 0.72f * hover));
            }
            render_target_->DrawRoundedRectangle(RoundRect(rect, 6), brush, 0.75f + 0.3f * hover);
        }
    };
    drawHeaderButton(header.pin, pinned_open_, UiAction::Pin);
    DrawIcon(ToWindowIcon(PopupPinIconSlot(pinned_open_)),
             BuildPopupHeaderPinIconRect(header),
             pinned_open_ ? 0.94f : 0.74f);
    drawHeaderButton(header.close, false, UiAction::Close);
    DrawIcon(IconId::Close,
             BuildPopupHeaderCloseIconRect(header),
             palette.dark ? 0.92f : 0.70f);

    const float search_top = PopupSearchTop();
    const auto search_layout = BuildPopupSearchLayout();
    const bool search_hovered = hover_action_ == UiAction::Search || Contains(search_layout.box, hover_point_);
    const bool search_active = search_focused_ && GetFocus() == hwnd_;
    const float search_focus = PopupSearchFocusProgress(search_active, search_hovered, hover_progress_);
    if (search_hovered || search_active) {
        brush->SetColor(D2D1::ColorF(palette.accent, search_active ? 0.11f : 0.05f + 0.05f * search_focus));
        render_target_->FillRoundedRectangle(
            RoundRect(metrics.margin - 1.0f, search_top - 1.0f, metrics.width - metrics.margin + 1.0f,
                      search_top + metrics.search_height + 1.0f, 10),
            brush);
    }
    brush->SetColor(ColorWithAlpha(search_hovered || search_active ? (palette.dark ? 0x243040 : 0xFFFFFF)
                                                                   : palette.search_fill,
                                    palette.dark ? 0.76f + 0.08f * search_focus : 0.90f + 0.06f * search_focus));
    render_target_->FillRoundedRectangle(
        RoundRect(metrics.margin, search_top, metrics.width - metrics.margin, search_top + metrics.search_height, 9),
        brush);
    brush->SetColor(search_active ? D2D1::ColorF(palette.accent, 0.70f)
                                   : D2D1::ColorF(search_hovered ? palette.accent : palette.border,
                                                  search_hovered ? 0.42f + 0.18f * search_focus : 0.68f));
    render_target_->DrawRoundedRectangle(
        RoundRect(metrics.margin, search_top, metrics.width - metrics.margin, search_top + metrics.search_height, 9),
        brush, search_active ? 1.45f : 1.0f + 0.25f * search_focus);
    const float search_icon_x = static_cast<float>(metrics.margin) + 20.0f;
    const float search_icon_y = search_top + metrics.search_height * 0.5f;
    brush->SetColor(D2D1::ColorF(palette.muted, 0.72f));
    render_target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(search_icon_x, search_icon_y - 1.0f), 5.0f, 5.0f),
                                brush, 1.45f);
    render_target_->DrawLine(D2D1::Point2F(search_icon_x + 3.8f, search_icon_y + 3.0f),
                             D2D1::Point2F(search_icon_x + 8.0f, search_icon_y + 7.0f), brush, 1.45f);
    const auto search_text = PopupSearchDisplayText(query_);
    brush->SetColor(query_.empty() ? D2D1::ColorF(palette.muted, 0.46f) : D2D1::ColorF(palette.text, 0.94f));
    render_target_->DrawTextW(search_text.data(), static_cast<UINT32>(search_text.size()), small_format_,
                              Rect(search_layout.text), brush);
    if (PopupSearchCaretVisible(search_focused_ && GetFocus() == hwnd_, search_caret_on_)) {
        const float measured_text_width = SearchCaretOffsetDips();
        const float caret_x = ClampPopupSearchCaretX(search_layout, measured_text_width);
        brush->SetColor(D2D1::ColorF(palette.accent, 0.82f));
        render_target_->DrawLine(D2D1::Point2F(caret_x, search_layout.text.top + 3.0f),
                                 D2D1::Point2F(caret_x, search_layout.text.bottom - 3.0f),
                                 brush, 1.0f);
    }
    auto drawToolbarIcon = [&](const UiRect& rect, wchar_t icon, bool active = false, bool has_chevron = false) {
        IconId icon_id = IconId::Filter;
        if (icon == L'F') icon_id = ToWindowIcon(PopupFilterIconSlot(active));
        if (icon == L'M') icon_id = ToWindowIcon(PopupMultiSelectIconSlot(active));
        if (icon == L'P') icon_id = ToWindowIcon(PopupPasteSelectedIconSlot());
        if (icon == L'A') icon_id = ToWindowIcon(PopupMultiSelectIconSlot(true));
        if (icon == L'D') icon_id = IconId::Trash;
        if (icon == L'C') icon_id = IconId::Close;
        const bool compact = rect.Width() < 94.0f;
        DrawIcon(icon_id, CenteredRect(UiRect{rect.left + 7.0f, rect.top + 4.0f,
                                             rect.left + (compact ? 25.0f : 29.0f), rect.bottom - 4.0f},
                                      compact ? 12.0f : 13.0f, compact ? 12.0f : 13.0f),
                 active ? 0.86f : 0.66f);
        if (has_chevron) {
            const float cx = rect.right - 15.0f;
            const float cy = rect.top + 14.0f;
            brush->SetColor(D2D1::ColorF(0x26364B, 0.72f));
            render_target_->DrawLine(D2D1::Point2F(cx, cy), D2D1::Point2F(cx + 4.0f, cy + 4.0f), brush, 1.15f);
            render_target_->DrawLine(D2D1::Point2F(cx + 8.0f, cy), D2D1::Point2F(cx + 4.0f, cy + 4.0f), brush, 1.15f);
        }
    };
    auto drawButton = [&](const UiRect& rect, const wchar_t* label, wchar_t icon, UiAction action,
                          bool active = false, bool has_chevron = false) {
        const bool hovered = hover_action_ == action;
        const float hover = hovered ? hover_progress_ : 0.0f;
        const bool danger = action == UiAction::ClearAll || action == UiAction::DeleteSelected;
        if (danger) {
            brush->SetColor(D2D1::ColorF(hovered ? 0xFFF1F2 : 0xFFFFFF, 0.66f + 0.18f * hover));
        } else {
            brush->SetColor(active ? D2D1::ColorF(0xDDFCF8, 0.82f)
                                   : ColorWithAlpha(palette.panel_fill, 0.62f + 0.26f * hover));
        }
        render_target_->FillRoundedRectangle(RoundRect(rect, 9), brush);
        if (danger) {
            brush->SetColor(D2D1::ColorF(hovered ? 0xE5484D : 0xF2555A, hovered ? 0.72f : 0.58f));
        } else {
            brush->SetColor(active ? D2D1::ColorF(0x0EA5A4, 0.56f)
                                   : D2D1::ColorF(hover > 0.0f ? 0x65DAD2 : palette.border,
                                                  hover > 0.0f ? 0.54f * hover : 0.52f));
        }
        render_target_->DrawRoundedRectangle(RoundRect(rect, 9), brush, 0.85f + 0.4f * hover);
        drawToolbarIcon(rect, icon, active, has_chevron);
        ID2D1SolidColorBrush* label_brush = danger ? brush : text_brush_;
        if (danger) {
            brush->SetColor(D2D1::ColorF(hovered ? 0xC92A2A : 0xE5484D, 0.94f));
        }
        render_target_->DrawTextW(label, static_cast<UINT32>(wcslen(label)), small_format_,
                                  Rect(PopupToolbarLabelRect(rect, has_chevron)), label_brush);
    };
    const auto toolbar = BuildPopupToolbarLayout(multi_select_);
    if (multi_select_) {
        drawButton(toolbar.select_all, L"\u5168\u9009", L'A', UiAction::SelectAll, true);
        drawButton(toolbar.cancel_multi_select, L"取消", L'C', UiAction::MultiSelect);
        drawButton(toolbar.delete_selected, L"选中删除", L'D', UiAction::DeleteSelected);
        drawButton(toolbar.paste_selected, L"选中粘贴", L'P', UiAction::PasteSelected, true);
    } else {
        drawButton(toolbar.filter, L"筛选", L'F', UiAction::Filter, filter_open_, true);
        drawButton(toolbar.multi_select, L"多选", L'M', UiAction::MultiSelect);
        drawButton(toolbar.clear_all, L"全部清除", L'D', UiAction::ClearAll);
    }

    const auto tabs = BuildPopupTabsLayout(view_mode_ == ViewMode::Favorites);
    auto drawTabGlow = [&](const UiRect& rect) {
        const float cx = (rect.left + rect.right) * 0.5f;
        const float cy = (rect.top + rect.bottom) * 0.5f;
        for (int i = 7; i >= 0; --i) {
            const float t = static_cast<float>(i) / 7.0f;
            const float width = 42.0f + t * 48.0f;
            const float height = 18.0f + t * 28.0f;
            const float fade = 1.0f - t;
            const float alpha = (0.006f + 0.040f * fade * fade) * hover_progress_;
            brush->SetColor(D2D1::ColorF(palette.dark ? 0xC8D0DA : 0x7DE7DE, alpha));
            render_target_->FillRoundedRectangle(
                RoundRect(cx - width * 0.5f, cy - height * 0.5f, cx + width * 0.5f, cy + height * 0.5f,
                          height * 0.5f),
                brush);
        }
    };
    if (hover_action_ == UiAction::HistoryTab) {
        drawTabGlow(tabs.history);
    }
    if (hover_action_ == UiAction::FavoritesTab) {
        drawTabGlow(tabs.favorites);
    }
    render_target_->DrawTextW(L"历史", 2, centered_small_format_, Rect(tabs.history),
                              view_mode_ == ViewMode::History ? accent_brush_ : text_brush_);
    render_target_->DrawTextW(L"收藏夹", 3, centered_small_format_, Rect(tabs.favorites),
                              view_mode_ == ViewMode::Favorites ? accent_brush_ : text_brush_);
    if (view_mode_ == ViewMode::Favorites) {
        const float add_cx = (tabs.add_favorite_phrase.left + tabs.add_favorite_phrase.right) * 0.5f;
        const float add_cy = (tabs.add_favorite_phrase.top + tabs.add_favorite_phrase.bottom) * 0.5f;
        const bool add_hovered = hover_action_ == UiAction::AddFavoritePhrase;
        const float add_hover = add_hovered ? hover_progress_ : 0.0f;
        brush->SetColor(add_hovered ? D2D1::ColorF(0xE0F7F4, 0.64f + 0.22f * add_hover)
                                    : D2D1::ColorF(0xFFFFFF, 0.64f));
        render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(add_cx, add_cy), 11.0f, 11.0f), brush);
        brush->SetColor(D2D1::ColorF(0xDDE5EF, 0.72f));
        render_target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(add_cx, add_cy), 11.0f, 11.0f), brush,
                                    1.0f + 0.35f * add_hover);
        brush->SetColor(D2D1::ColorF(0x0EA5A4, 0.9f));
        render_target_->DrawLine(D2D1::Point2F(add_cx - 4.0f, add_cy), D2D1::Point2F(add_cx + 4.0f, add_cy), brush,
                                 1.35f);
        render_target_->DrawLine(D2D1::Point2F(add_cx, add_cy - 4.0f), D2D1::Point2F(add_cx, add_cy + 4.0f), brush,
                                 1.35f);
    }
    brush->SetColor(D2D1::ColorF(palette.accent, 0.92f));
    render_target_->FillRectangle(Rect(tabs.active_indicator), brush);
    brush->SetColor(D2D1::ColorF(0xDDE5EF, 0.65f));
    render_target_->FillRectangle(Rect(tabs.divider), brush);

    float y = PopupListTop();
    const int visible_capacity = PopupVisibleCardCapacity();
    for (int row = 0; row < visible_capacity && scroll_offset_ + row < static_cast<int>(items_.size()) &&
                      y + metrics.card_height <= metrics.height - 4;
         ++row) {
        const int item_index = scroll_offset_ + row;
        const auto& item = items_[item_index];
        const bool selected = item_index == selected_index_;
        const bool hovered = item_index == hover_item_index_;
        const float hover = hovered ? hover_progress_ : 0.0f;
        const bool checked = selection_.IsSelected(item.id);
        const auto card = BuildPopupCardLayout(multi_select_, y);
        brush->SetColor(selected ? D2D1::ColorF(palette.dark ? 0x2B3442 : 0xF0FFFD, 0.94f)
                                 : ColorWithAlpha(hovered ? (palette.dark ? 0x2D3B4E : 0xF7FFFE) : palette.card_fill,
                                                  palette.card_opacity + 0.14f * hover));
        render_target_->FillRoundedRectangle(RoundRect(card.card, 9), brush);
        brush->SetColor(selected ? D2D1::ColorF(palette.accent, 0.78f)
                                 : D2D1::ColorF(hovered ? (palette.dark ? 0xAEB8C6 : 0x65DAD2) : palette.border,
                                                hovered ? 0.58f * hover : 0.72f));
        render_target_->DrawRoundedRectangle(RoundRect(card.card, 9), brush,
                                             selected ? 1.25f : 1.0f + 0.25f * hover);
        DrawCardMedia(item, card, brush);

        if (multi_select_) {
            brush->SetColor(checked ? D2D1::ColorF(0x0EA5A4, 0.9f) : D2D1::ColorF(0xFFFFFF, 0.72f));
            render_target_->FillRoundedRectangle(RoundRect(card.checkbox, 4), brush);
            brush->SetColor(D2D1::ColorF(0x7B8798, 0.7f));
            render_target_->DrawRoundedRectangle(RoundRect(card.checkbox, 4), brush, 1.0f);
            if (checked) {
                brush->SetColor(D2D1::ColorF(0xFFFFFF, 0.96f));
                render_target_->DrawLine(D2D1::Point2F(card.checkbox.left + 3.0f, card.checkbox.top + 7.0f),
                                         D2D1::Point2F(card.checkbox.left + 6.0f, card.checkbox.top + 10.0f),
                                         brush, 1.7f);
                render_target_->DrawLine(D2D1::Point2F(card.checkbox.left + 6.0f, card.checkbox.top + 10.0f),
                                         D2D1::Point2F(card.checkbox.right - 3.0f, card.checkbox.top + 4.0f),
                                         brush, 1.7f);
            }
        }

        const auto title = ItemTitle(item);
        const auto meta = ItemMeta(item);
        render_target_->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), body_format_,
                                  Rect(card.title), text_brush_);
        render_target_->DrawTextW(meta.c_str(), static_cast<UINT32>(meta.size()), small_format_,
                                  Rect(card.meta), muted_brush_);
        const auto time = ItemTime(item);
        render_target_->DrawTextW(time.c_str(), static_cast<UINT32>(time.size()), small_format_, Rect(card.time),
                                  muted_brush_);
        y += metrics.card_height + metrics.card_gap;
    }

    if (items_.size() > static_cast<size_t>(visible_capacity)) {
        const float track_top = PopupListTop();
        const float track_bottom = static_cast<float>(metrics.height - 10);
        const float track_height = track_bottom - track_top;
        const float thumb_height = std::max(32.0f, track_height * static_cast<float>(visible_capacity) /
                                                       static_cast<float>(items_.size()));
        const float max_offset = static_cast<float>(std::max(1, static_cast<int>(items_.size()) - visible_capacity));
        const float thumb_top = track_top + (track_height - thumb_height) * static_cast<float>(scroll_offset_) / max_offset;
        brush->SetColor(D2D1::ColorF(palette.border, 0.34f));
        render_target_->FillRoundedRectangle(RoundRect(static_cast<float>(metrics.width - 10), track_top,
                                                       static_cast<float>(metrics.width - 6), track_bottom, 2),
                                             brush);
        brush->SetColor(D2D1::ColorF(palette.accent, 0.56f));
        render_target_->FillRoundedRectangle(RoundRect(static_cast<float>(metrics.width - 10), thumb_top,
                                                       static_cast<float>(metrics.width - 6), thumb_top + thumb_height, 2),
                                             brush);
    }

    if (items_.empty()) {
        const wchar_t* empty_text = view_mode_ == ViewMode::Favorites ? L"收藏夹暂无内容" : L"暂无历史记录";
        render_target_->DrawTextW(empty_text, static_cast<UINT32>(wcslen(empty_text)), body_format_,
                                  Rect(metrics.margin, y + 24.0f, metrics.width - metrics.margin, y + 56.0f),
                                  muted_brush_);
    }

    if (filter_open_) {
        const auto filter = BuildPopupFilterLayout();
        const auto isFilterHovered = [&](PopupFilterTarget target) {
            return hover_filter_target_ == target ? 1.0f : 0.0f;
        };
        const auto drawFilterHover = [&](const UiRect& rect, PopupFilterTarget target, float radius = 10.0f,
                                         bool danger = false) {
            const float hover = isFilterHovered(target);
            if (hover <= 0.0f) {
                return;
            }
            for (int i = 4; i >= 0; --i) {
                const float t = static_cast<float>(i) / 4.0f;
                brush->SetColor(D2D1::ColorF(danger ? 0xF2555A : (palette.dark ? 0xAEB8C6 : 0x65DAD2),
                                             (0.036f - t * 0.025f) * hover));
                render_target_->FillRoundedRectangle(
                    RoundRect(rect.left - 1.0f - t * 4.0f, rect.top - 1.0f - t * 2.5f,
                              rect.right + 1.0f + t * 4.0f, rect.bottom + 1.0f + t * 2.5f,
                              radius + t * 4.0f),
                    brush);
            }
            brush->SetColor(D2D1::ColorF(danger ? 0xFFF1F2 : 0xF4FFFD, 0.18f * hover));
            render_target_->FillRoundedRectangle(RoundRect(rect, radius), brush);
        };
        const auto fillCircleHover = [&](const UiRect& rect, PopupFilterTarget target, float radius, bool danger = false) {
            const float hover = isFilterHovered(target);
            if (hover <= 0.0f) {
                return;
            }
            const D2D1_POINT_2F center =
                D2D1::Point2F((rect.left + rect.right) * 0.5f, (rect.top + rect.bottom) * 0.5f);
            for (int i = 4; i >= 0; --i) {
                const float t = static_cast<float>(i) / 4.0f;
                brush->SetColor(D2D1::ColorF(danger ? 0xF2555A : (palette.dark ? 0xAEB8C6 : 0x65DAD2),
                                             (0.036f - t * 0.025f) * hover));
                render_target_->FillEllipse(D2D1::Ellipse(center, radius + t * 3.0f, radius + t * 3.0f), brush);
            }
            brush->SetColor(D2D1::ColorF(danger ? 0xFFF1F2 : 0xF4FFFD, 0.22f * hover));
            render_target_->FillEllipse(D2D1::Ellipse(center, radius, radius), brush);
        };
        const float fx = filter.panel.left;
        const float fy = filter.panel.top;
        brush->SetColor(ColorWithAlpha(palette.dark ? 0x1E293B : 0xFFFFFF, palette.dark ? 0.98f : 0.97f));
        render_target_->FillRoundedRectangle(RoundRect(filter.panel, 18), brush);
        brush->SetColor(ColorWithAlpha(palette.border, 0.78f));
        render_target_->DrawRoundedRectangle(RoundRect(filter.panel, 18), brush, 1.0f);
        render_target_->DrawTextW(L"筛选", 2, body_format_, Rect(fx + 16, fy + 12, fx + 128, fy + 34), text_brush_);
        fillCircleHover(filter.close, PopupFilterTarget::Close, 10.0f, true);
        brush->SetColor(hover_filter_target_ == PopupFilterTarget::Close
                            ? D2D1::ColorF(0xFFF1F2, 0.94f)
                            : ColorWithAlpha(palette.dark ? 0x334155 : 0xF8FAFC, 0.90f));
        render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((filter.close.left + filter.close.right) * 0.5f,
                                                                (filter.close.top + filter.close.bottom) * 0.5f),
                                                  10.0f, 10.0f),
                                    brush);
        brush->SetColor(hover_filter_target_ == PopupFilterTarget::Close ? D2D1::ColorF(0xE5484D, 0.95f)
                                                                         : D2D1::ColorF(palette.muted, 0.82f));
        const float close_cx = (filter.close.left + filter.close.right) * 0.5f;
        const float close_cy = (filter.close.top + filter.close.bottom) * 0.5f;
        render_target_->DrawLine(D2D1::Point2F(close_cx - 3.4f, close_cy - 3.4f),
                                 D2D1::Point2F(close_cx + 3.4f, close_cy + 3.4f), brush, 1.15f);
        render_target_->DrawLine(D2D1::Point2F(close_cx + 3.4f, close_cy - 3.4f),
                                 D2D1::Point2F(close_cx - 3.4f, close_cy + 3.4f), brush, 1.15f);
        render_target_->DrawTextW(L"类型", 2, small_format_,
                                  Rect(filter.type_section.left, filter.type_section.top, filter.type_section.right,
                                       filter.type_section.top + 18.0f),
                                  muted_brush_);
        auto drawChip = [&](const UiRect& rect, const wchar_t* label, bool active, D2D1_COLOR_F color,
                            PopupFilterTarget target) {
            const float hover = isFilterHovered(target);
            if (hover > 0.0f) {
                for (int i = 4; i >= 0; --i) {
                    const float t = static_cast<float>(i) / 4.0f;
                    brush->SetColor(D2D1::ColorF(palette.dark ? 0xAEB8C6 : 0x65DAD2,
                                                 (0.036f - t * 0.024f) * hover));
                    render_target_->FillRoundedRectangle(
                        RoundRect(rect.left - 1.0f - t * 3.5f, rect.top - 0.8f - t * 2.0f,
                                  rect.right + 1.0f + t * 3.5f, rect.bottom + 0.8f + t * 2.0f,
                                  15.0f + t * 4.0f),
                        brush);
                }
            }
            brush->SetColor(active ? D2D1::ColorF(color.r, color.g, color.b, std::min(1.0f, color.a + 0.05f * hover))
                                   : ColorWithAlpha(palette.panel_fill, 0.68f + 0.12f * hover));
            render_target_->FillRoundedRectangle(RoundRect(rect, 14), brush);
            brush->SetColor(active ? D2D1::ColorF(0x0EA5A4, 0.42f)
                                   : D2D1::ColorF(palette.border, 0.50f));
            render_target_->DrawRoundedRectangle(RoundRect(rect, 14), brush, active ? 0.9f : 0.75f);
            render_target_->DrawTextW(label, static_cast<UINT32>(wcslen(label)), small_format_,
                                      Rect(rect.left + 12.0f, rect.top + 5.0f, rect.right - 12.0f, rect.bottom - 5.0f),
                                      text_brush_);
        };
        const bool all_types = filter_kinds_.empty();
        drawChip(filter.text_chip, L"文本", all_types || filter_kinds_.contains(ClipboardKind::Text),
                 D2D1::ColorF(0xE0F7F4, 0.92f), PopupFilterTarget::TextChip);
        drawChip(filter.image_chip, L"图片", all_types || filter_kinds_.contains(ClipboardKind::Image),
                 D2D1::ColorF(0xECFDF5, 0.92f), PopupFilterTarget::ImageChip);
        drawChip(filter.file_chip, L"文件", all_types || filter_kinds_.contains(ClipboardKind::Files),
                 D2D1::ColorF(0xF5F3FF, 0.92f), PopupFilterTarget::FileChip);
        drawChip(filter.link_chip, L"链接", all_types || filter_kinds_.contains(ClipboardKind::Link),
                 D2D1::ColorF(0xFFF7ED, 0.92f), PopupFilterTarget::LinkChip);

        const bool selecting_start = date_filter_.active_field == PopupDateRangeField::Start;
        brush->SetColor(ColorWithAlpha(palette.panel_fill, 0.72f));
        render_target_->FillRoundedRectangle(RoundRect(filter.date_card, 14), brush);
        brush->SetColor(ColorWithAlpha(palette.border, 0.62f));
        render_target_->DrawRoundedRectangle(RoundRect(filter.date_card, 14), brush, 1.0f);
        render_target_->DrawTextW(L"日期范围", 4, small_format_,
                                  Rect(filter.date_card.left + 12.0f, filter.date_card.top + 10.0f,
                                       filter.date_card.left + 92.0f, filter.date_card.top + 28.0f),
                                  muted_brush_);
        const wchar_t* active_hint = selecting_start ? L"选择开始" : L"选择结束";
        render_target_->DrawTextW(active_hint, static_cast<UINT32>(wcslen(active_hint)), small_format_,
                                  Rect(filter.date_card.right - 88.0f, filter.date_card.top + 10.0f,
                                       filter.date_card.right - 12.0f, filter.date_card.top + 28.0f),
                                  accent_brush_);
        auto drawDateBox = [&](const UiRect& rect, const wchar_t* label, const std::wstring& value, bool active,
                               PopupFilterTarget target) {
            const float hover = isFilterHovered(target);
            drawFilterHover(rect, target, 11.0f);
            brush->SetColor(active ? D2D1::ColorF(0xDDFCF8, 0.74f)
                                   : ColorWithAlpha(palette.panel_fill, 0.72f + 0.10f * hover));
            render_target_->FillRoundedRectangle(RoundRect(rect, 11), brush);
            brush->SetColor(active ? D2D1::ColorF(0x0EA5A4, 0.86f)
                                   : D2D1::ColorF(palette.border, 0.60f));
            render_target_->DrawRoundedRectangle(RoundRect(rect, 11), brush,
                                                 active ? 1.25f : 0.85f);
            render_target_->DrawTextW(label, static_cast<UINT32>(wcslen(label)), small_format_,
                                      Rect(rect.left + 10.0f, rect.top + 4.0f, rect.right - 8.0f, rect.top + 17.0f),
                                      muted_brush_);
            render_target_->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), small_format_,
                                      Rect(rect.left + 10.0f, rect.top + 17.0f, rect.right - 8.0f, rect.bottom - 3.0f),
                                      text_brush_);
        };
        drawDateBox(filter.start_date, L"开始日期", FormatDateValue(date_filter_.start),
                    date_filter_.active_field == PopupDateRangeField::Start, PopupFilterTarget::StartDate);
        drawDateBox(filter.end_date, L"结束日期", FormatDateValue(date_filter_.end),
                    date_filter_.active_field == PopupDateRangeField::End, PopupFilterTarget::EndDate);

        const auto calendar = filter.calendar;
        brush->SetColor(ColorWithAlpha(palette.panel_fill, 0.70f));
        render_target_->FillRoundedRectangle(RoundRect(calendar, 14), brush);
        brush->SetColor(ColorWithAlpha(palette.border, 0.62f));
        render_target_->DrawRoundedRectangle(RoundRect(calendar, 14), brush, 1.0f);
        fillCircleHover(filter.calendar_prev, PopupFilterTarget::CalendarPrevious, 10.0f);
        fillCircleHover(filter.calendar_next, PopupFilterTarget::CalendarNext, 10.0f);
        const auto drawChevron = [&](const UiRect& button, bool next) {
            const auto glyph = PopupFilterArrowGlyphRect(button);
            const float cx = (glyph.left + glyph.right) * 0.5f;
            const float cy = (glyph.top + glyph.bottom) * 0.5f;
            const float x1 = next ? cx - 2.4f : cx + 2.4f;
            const float x2 = next ? cx + 2.8f : cx - 2.8f;
            brush->SetColor(D2D1::ColorF(palette.muted, 0.82f));
            render_target_->DrawLine(D2D1::Point2F(x1, cy - 4.8f), D2D1::Point2F(x2, cy), brush, 1.55f);
            render_target_->DrawLine(D2D1::Point2F(x2, cy), D2D1::Point2F(x1, cy + 4.8f), brush, 1.55f);
        };
        brush->SetColor(ColorWithAlpha(palette.dark ? 0x334155 : 0xF8FAFC, 0.74f));
        render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((filter.calendar_prev.left + filter.calendar_prev.right) * 0.5f,
                                                                (filter.calendar_prev.top + filter.calendar_prev.bottom) * 0.5f),
                                                  10.0f, 10.0f),
                                    brush);
        render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((filter.calendar_next.left + filter.calendar_next.right) * 0.5f,
                                                                (filter.calendar_next.top + filter.calendar_next.bottom) * 0.5f),
                                                  10.0f, 10.0f),
                                    brush);
        render_target_->DrawTextW(L"‹", 1, small_format_, Rect(filter.calendar_prev), muted_brush_);
        const auto month_title = FormatMonthTitle(calendar_year_, calendar_month_);
        render_target_->DrawTextW(month_title.c_str(), static_cast<UINT32>(month_title.size()), small_format_,
                                  Rect(filter.calendar_title), text_brush_);
        render_target_->DrawTextW(L"›", 1, small_format_, Rect(filter.calendar_next), muted_brush_);
        brush->SetColor(ColorWithAlpha(palette.dark ? 0x334155 : 0xF8FAFC, 0.74f));
        render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((filter.calendar_prev.left + filter.calendar_prev.right) * 0.5f,
                                                                (filter.calendar_prev.top + filter.calendar_prev.bottom) * 0.5f),
                                                  10.0f, 10.0f),
                                    brush);
        render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((filter.calendar_next.left + filter.calendar_next.right) * 0.5f,
                                                                (filter.calendar_next.top + filter.calendar_next.bottom) * 0.5f),
                                                  10.0f, 10.0f),
                                    brush);
        drawChevron(filter.calendar_prev, false);
        drawChevron(filter.calendar_next, true);
        for (const auto& label : BuildPopupCalendarWeekdayLabels(filter)) {
            render_target_->DrawTextW(&label.text, 1, centered_small_format_, Rect(label.rect), muted_brush_);
        }
        for (const auto& cell : BuildPopupCalendarCells(filter, calendar_year_, calendar_month_)) {
            wchar_t text[4]{};
            _itow_s(cell.date.day, text, 10);
            const bool selected_start = date_filter_.start && *date_filter_.start == cell.date;
            const bool selected_end = date_filter_.end && *date_filter_.end == cell.date;
            const bool in_range = date_filter_.start && date_filter_.end && *date_filter_.start < cell.date &&
                                  cell.date < *date_filter_.end;
            if (in_range) {
                brush->SetColor(D2D1::ColorF(0xBFEFEB, 0.46f));
                const float range_center_y = (cell.rect.top + cell.rect.bottom) * 0.5f;
                render_target_->FillRoundedRectangle(RoundRect(cell.rect.left + 6.0f, range_center_y - 5.0f,
                                                               cell.rect.right - 6.0f, range_center_y + 5.0f, 5),
                                                     brush);
            }
            const bool date_hovered = hover_filter_date_ && *hover_filter_date_ == cell.date;
            if (date_hovered && !selected_start && !selected_end) {
                brush->SetColor(D2D1::ColorF(0xE0F7F4, 0.42f * hover_progress_));
                render_target_->FillEllipse(D2D1::Ellipse(
                                                D2D1::Point2F((cell.rect.left + cell.rect.right) * 0.5f,
                                                              (cell.rect.top + cell.rect.bottom) * 0.5f),
                                                9.0f, 9.0f),
                                            brush);
            }
            if (selected_start || selected_end) {
                brush->SetColor(D2D1::ColorF(0x0EA5A4, 0.88f));
                render_target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((cell.rect.left + cell.rect.right) * 0.5f,
                                                                        (cell.rect.top + cell.rect.bottom) * 0.5f),
                                                          8.5f, 8.5f),
                                            brush);
                brush->SetColor(D2D1::ColorF(0xFFFFFF, 0.58f));
                render_target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F((cell.rect.left + cell.rect.right) * 0.5f,
                                                                        (cell.rect.top + cell.rect.bottom) * 0.5f),
                                                          8.5f, 8.5f),
                                            brush, 1.0f);
                render_target_->DrawTextW(text, static_cast<UINT32>(wcslen(text)), centered_small_format_,
                                          Rect(cell.rect.left, cell.rect.top + 1.0f, cell.rect.right,
                                               cell.rect.bottom + 1.0f),
                                          text_brush_);
            } else {
                render_target_->DrawTextW(text, static_cast<UINT32>(wcslen(text)), centered_small_format_,
                                          Rect(cell.rect.left, cell.rect.top + 1.0f, cell.rect.right,
                                               cell.rect.bottom + 1.0f),
                                          muted_brush_);
            }
        }
        const auto reset_visual = PopupFilterResetVisualRect(filter);
        drawFilterHover(reset_visual, PopupFilterTarget::Reset, 9.0f, true);
        if (hover_filter_target_ == PopupFilterTarget::Reset) {
            brush->SetColor(D2D1::ColorF(0xE5484D, 0.92f));
            render_target_->DrawTextW(L"重置", 2, centered_small_format_, Rect(reset_visual), brush);
        } else {
            render_target_->DrawTextW(L"重置", 2, centered_small_format_, Rect(reset_visual), accent_brush_);
        }
        const float done_hover = isFilterHovered(PopupFilterTarget::Done);
        brush->SetColor(D2D1::ColorF(done_hover > 0.0f ? 0x0D948D : 0x0EA5A4, 0.88f + 0.08f * done_hover));
        render_target_->FillRoundedRectangle(RoundRect(filter.done, 11), brush);
        brush->SetColor(D2D1::ColorF(0xFFFFFF, 0.96f));
        render_target_->DrawTextW(L"完成", 2, centered_small_format_, Rect(filter.done), brush);
    }
    ReleasePtr(brush);

    if (render_target_->EndDraw() == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
}

void PopupWindow::ApplyBackdrop() {
    SetModernWindowAttributes(hwnd_);
}

bool PopupWindow::HandleFilterClick(POINT point) {
    if (multi_select_) {
        return false;
    }
    if (!filter_open_) {
        return false;
    }

    const auto layout = BuildPopupFilterLayout();
    if (!Contains(layout.panel, point)) {
        return false;
    }
    if (Contains(layout.close, point)) {
        filter_open_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (Contains(layout.done, point)) {
        filter_open_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    auto toggle_kind = [&](ClipboardKind kind) {
        if (filter_kinds_.contains(kind)) {
            filter_kinds_.erase(kind);
        } else {
            filter_kinds_.insert(kind);
        }
    };

    if (Contains(layout.text_chip, point)) {
        toggle_kind(ClipboardKind::Text);
        scroll_offset_ = 0;
        ReloadItems();
        ResizeToCurrentItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (Contains(layout.image_chip, point)) {
        toggle_kind(ClipboardKind::Image);
        scroll_offset_ = 0;
        ReloadItems();
        ResizeToCurrentItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (Contains(layout.file_chip, point)) {
        toggle_kind(ClipboardKind::Files);
        scroll_offset_ = 0;
        ReloadItems();
        ResizeToCurrentItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (Contains(layout.link_chip, point)) {
        toggle_kind(ClipboardKind::Link);
        scroll_offset_ = 0;
        ReloadItems();
        ResizeToCurrentItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    if (const auto field = HitTestPopupDateRangeField(layout, static_cast<float>(point.x), static_cast<float>(point.y))) {
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
        return true;
    }

    switch (HitTestPopupCalendarArrow(layout, static_cast<float>(point.x), static_cast<float>(point.y))) {
    case PopupCalendarArrow::PreviousMonth:
        ShiftMonth(calendar_year_, calendar_month_, -1);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    case PopupCalendarArrow::NextMonth:
        ShiftMonth(calendar_year_, calendar_month_, 1);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    case PopupCalendarArrow::None:
        break;
    }

    if (const auto date = HitTestPopupCalendarDate(layout, calendar_year_, calendar_month_,
                                                  static_cast<float>(point.x), static_cast<float>(point.y))) {
        SelectPopupDateRangeDate(date_filter_, *date);
        scroll_offset_ = 0;
        ReloadItems();
        ResizeToCurrentItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    if (Contains(layout.reset, point)) {
        filter_kinds_.clear();
        date_filter_ = PopupDateRangeState{};
        scroll_offset_ = 0;
        ReloadItems();
        ResizeToCurrentItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    return true;
}

int PopupWindow::HitTestItem(POINT point) const {
    const auto& metrics = PopupMetrics();
    const int list_top = static_cast<int>(PopupListTop());
    if (point.y < list_top) return -1;
    const int index = (point.y - list_top) / (metrics.card_height + metrics.card_gap);
    const int row_y = list_top + index * (metrics.card_height + metrics.card_gap);
    const int item_index = scroll_offset_ + index;
    if (index >= 0 && item_index >= 0 && item_index < static_cast<int>(items_.size()) &&
        point.y <= row_y + metrics.card_height) {
        return item_index;
    }
    return -1;
}

PopupWindow::UiAction PopupWindow::HitTestAction(POINT point) const {
    if (Contains(BuildPopupSearchLayout().box, point)) return UiAction::Search;
    const auto header = BuildPopupHeaderLayout();
    if (Contains(header.close, point)) return UiAction::Close;
    if (Contains(header.pin, point)) return UiAction::Pin;
    const auto toolbar = BuildPopupToolbarLayout(multi_select_);
    if (point.y >= PopupToolbarTop() && point.y <= PopupToolbarTop() + 32.0f) {
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
    const auto tabs = BuildPopupTabsLayout(view_mode_ == ViewMode::Favorites);
    if (Contains(tabs.history, point)) return UiAction::HistoryTab;
    if (Contains(tabs.favorites, point)) return UiAction::FavoritesTab;
    if (view_mode_ == ViewMode::Favorites && Contains(tabs.add_favorite_phrase, point)) {
        return UiAction::AddFavoritePhrase;
    }
    return UiAction::None;
}

void PopupWindow::ShowContextMenu(POINT point, int item_index) {
    if (item_index < 0 || item_index >= static_cast<int>(items_.size())) return;
    const auto& item = items_[item_index];
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kContextDelete, L"删除");
    const auto pin_label = PopupPinMenuLabel(item.is_pinned);
    const auto favorite_label = PopupFavoriteMenuLabel(item.is_favorite);
    AppendMenuW(menu, MF_STRING, kContextPin, std::wstring(pin_label).c_str());
    AppendMenuW(menu, MF_STRING, kContextFavorite, std::wstring(favorite_label).c_str());
    ClientToScreen(hwnd_, &point);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    const auto id = item.id;
        if (command == kContextDelete) DeleteItem(id);
        if (command == kContextPin) TogglePinned(id);
        if (command == kContextFavorite) ToggleFavorite(id);
    SetForegroundWindow(hwnd_);
}

LRESULT PopupWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd_, &ps);
        Paint();
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_SIZE:
        UpdateSearchEditBounds();
        DiscardDeviceResources();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lparam) == search_edit_) {
            const auto palette = ResolvePopupThemePalette(theme_mode_, IsSystemDarkTheme());
            SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
            SetBkColor(reinterpret_cast<HDC>(wparam), palette.dark ? RGB(34, 48, 68) : RGB(255, 255, 255));
            SetTextColor(reinterpret_cast<HDC>(wparam), palette.dark ? RGB(248, 250, 252) : RGB(23, 32, 51));
            if (!search_edit_brush_) {
                search_edit_brush_ = CreateSolidBrush(palette.dark ? RGB(34, 48, 68) : RGB(255, 255, 255));
            }
            return reinterpret_cast<LRESULT>(search_edit_brush_);
        }
        break;
    case WM_SETFOCUS:
        if (search_focused_) {
            search_caret_on_ = true;
            SetTimer(hwnd_, kSearchCaretTimer, GetCaretBlinkTime(), nullptr);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
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
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        DiscardDeviceResources();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_EXITSIZEMOVE:
        custom_position_ = true;
        return 0;
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(hwnd_, message, wparam, lparam);
        if (hit != HTCLIENT) {
            return hit;
        }
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &point);
        const POINT logical_point = ClientPointToDips(point);
        if (IsPopupHeaderDragArea(static_cast<float>(logical_point.x), static_cast<float>(logical_point.y))) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            POINT cursor{};
            GetCursorPos(&cursor);
            ScreenToClient(hwnd_, &cursor);
            const POINT logical_point = ClientPointToDips(cursor);
            if (Contains(BuildPopupSearchLayout().box, logical_point)) {
                SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                return TRUE;
            }
        }
        break;
    case WM_TIMER:
        if (wparam == kAnimationTimer) {
            open_progress_ = std::min(1.0f, open_progress_ + 0.18f);
            if (open_progress_ >= 1.0f) KillTimer(hwnd_, kAnimationTimer);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (wparam == kHoverTimer) {
            hover_progress_ = std::min(1.0f, hover_progress_ + 0.18f);
            if (hover_progress_ >= 1.0f) {
                KillTimer(hwnd_, kHoverTimer);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (wparam == kSearchCaretTimer) {
            if (search_focused_ && GetFocus() == hwnd_) {
                search_caret_on_ = !search_caret_on_;
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else {
                KillTimer(hwnd_, kSearchCaretTimer);
                search_caret_on_ = false;
            }
            return 0;
        }
        break;
    case WM_GETDLGCODE:
        return DLGC_WANTCHARS | DLGC_WANTARROWS;
    case WM_IME_STARTCOMPOSITION:
        UpdateSearchImePosition();
        break;
    case WM_CHAR:
        if (!PopupSearchAcceptsTextInput(search_focused_ && GetFocus() == hwnd_)) {
            return 0;
        }
        if (PopupSearchDeletesOnChar(static_cast<wchar_t>(wparam))) {
            if (!query_.empty()) query_.pop_back();
        } else if (PopupSearchAppendsChar(static_cast<wchar_t>(wparam))) {
            query_.push_back(static_cast<wchar_t>(wparam));
        }
        scroll_offset_ = 0;
        ReloadItems();
        ResizeToCurrentItems();
        UpdateSearchImePosition();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == kSearchEditId && HIWORD(wparam) == EN_CHANGE && search_edit_) {
            const int length = GetWindowTextLengthW(search_edit_);
            std::wstring value(static_cast<size_t>(length) + 1, L'\0');
            if (length > 0) {
                GetWindowTextW(search_edit_, value.data(), length + 1);
            }
            value.resize(static_cast<size_t>(length));
            if (value != query_) {
                query_ = std::move(value);
                ReloadItems();
                ResizeToCurrentItems();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        POINT point = ClientPointToDips(POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        if (Contains(BuildPopupSearchLayout().box, point)) {
            SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
        }
        if (!tracking_mouse_) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&track);
            tracking_mouse_ = true;
        }
        const PopupFilterTarget filter_target = filter_open_
                                                    ? HitTestPopupFilterTarget(BuildPopupFilterLayout(), calendar_year_,
                                                                               calendar_month_,
                                                                               static_cast<float>(point.x),
                                                                               static_cast<float>(point.y))
                                                    : PopupFilterTarget::None;
        const auto filter_layout = BuildPopupFilterLayout();
        const auto filter_date = filter_open_ && filter_target == PopupFilterTarget::CalendarDate
                                     ? HitTestPopupCalendarDate(filter_layout, calendar_year_, calendar_month_,
                                                               static_cast<float>(point.x),
                                                               static_cast<float>(point.y))
                                     : std::optional<PopupCalendarDate>{};
        const UiAction action = filter_target == PopupFilterTarget::None ? HitTestAction(point) : UiAction::None;
        const int item = PopupHoverItemIndex(filter_open_, HitTestItem(point));
        if (action != hover_action_ || filter_target != hover_filter_target_ || filter_date != hover_filter_date_ ||
            item != hover_item_index_) {
            const bool hover_target_changed = action != hover_action_ || filter_target != hover_filter_target_ ||
                                              filter_date != hover_filter_date_ || item != hover_item_index_;
            hover_action_ = action;
            hover_filter_target_ = filter_target;
            hover_filter_date_ = filter_date;
            hover_item_index_ = action == UiAction::None && filter_target == PopupFilterTarget::None ? item : -1;
            hover_point_ = point;
            if (hover_target_changed) {
                hover_progress_ = 0.0f;
                SetTimer(hwnd_, kHoverTimer, 16, nullptr);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        } else if (point.x != hover_point_.x || point.y != hover_point_.y) {
            hover_point_ = point;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_mouse_ = false;
        hover_action_ = UiAction::None;
        hover_filter_target_ = PopupFilterTarget::None;
        hover_filter_date_.reset();
        hover_item_index_ = -1;
        hover_point_ = POINT{-1, -1};
        hover_progress_ = 0.0f;
        KillTimer(hwnd_, kHoverTimer);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            ShowWindow(hwnd_, SW_HIDE);
            return 0;
        }
        if (PopupSearchDeletesOnKeyDown(static_cast<unsigned>(wparam))) {
            if (!PopupSearchAcceptsTextInput(search_focused_ && GetFocus() == hwnd_)) {
                return 0;
            }
            if (!query_.empty()) {
                query_.pop_back();
                scroll_offset_ = 0;
                ReloadItems();
                ResizeToCurrentItems();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
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
        const int next_offset = PopupScrollOffsetAfterWheel(static_cast<int>(items_.size()), scroll_offset_,
                                                            GET_WHEEL_DELTA_WPARAM(wparam));
        if (next_offset != scroll_offset_) {
            scroll_offset_ = next_offset;
            selected_index_ = std::clamp(selected_index_, scroll_offset_,
                                         std::min(static_cast<int>(items_.size()) - 1,
                                                  scroll_offset_ + PopupVisibleCardCapacity() - 1));
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT point = ClientPointToDips(POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        const float search_top = PopupSearchTop();
        const auto& metrics = PopupMetrics();
        const UiRect search_rect{static_cast<float>(metrics.margin), search_top,
                                 static_cast<float>(metrics.width - metrics.margin),
                                 search_top + static_cast<float>(metrics.search_height)};
        if (Contains(search_rect, point)) {
            search_focused_ = true;
            search_caret_on_ = true;
            SetFocus(hwnd_);
            SetTimer(hwnd_, kSearchCaretTimer, GetCaretBlinkTime(), nullptr);
            UpdateSearchImePosition();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        search_focused_ = false;
        search_caret_on_ = false;
        KillTimer(hwnd_, kSearchCaretTimer);
        if (reinterpret_cast<HWND>(GetFocus()) != hwnd_) {
            SetFocus(hwnd_);
        }
        if (HandleFilterClick(point)) {
            return 0;
        }
        switch (HitTestAction(point)) {
        case UiAction::Close:
            ShowWindow(hwnd_, SW_HIDE);
            return 0;
        case UiAction::Pin:
            pinned_open_ = !pinned_open_;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case UiAction::Filter:
            filter_open_ = !filter_open_;
            hover_progress_ = 0.0f;
            SetTimer(hwnd_, kHoverTimer, 16, nullptr);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case UiAction::MultiSelect:
            ToggleMultiSelect();
            return 0;
        case UiAction::ClearAll:
            store_.Clear();
            ReloadItems();
            ResizeToCurrentItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case UiAction::SelectAll:
            SelectAllVisible();
            return 0;
        case UiAction::DeleteSelected:
            for (const auto id : selection_.SelectedIds()) store_.Delete(id);
            selection_.Clear();
            ReloadItems();
            ResizeToCurrentItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case UiAction::PasteSelected:
            PasteSelected();
            return 0;
        case UiAction::HistoryTab:
            if (view_mode_ != ViewMode::History) {
                view_mode_ = ViewMode::History;
                filter_open_ = false;
                multi_select_ = false;
                selection_.Clear();
                scroll_offset_ = 0;
                ReloadItems();
                ResizeToCurrentItems();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case UiAction::FavoritesTab:
            if (view_mode_ != ViewMode::Favorites) {
                view_mode_ = ViewMode::Favorites;
                filter_open_ = false;
                multi_select_ = false;
                selection_.Clear();
                scroll_offset_ = 0;
                ReloadItems();
                ResizeToCurrentItems();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case UiAction::AddFavoritePhrase:
            PromptAddFavoritePhrase();
            return 0;
        case UiAction::Search:
            return 0;
        case UiAction::None:
            break;
        }
        const int item = HitTestItem(point);
        if (item >= 0) {
            selected_index_ = item;
            ActivateSelection();
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT physical_point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        const POINT logical_point = ClientPointToDips(physical_point);
        ShowContextMenu(physical_point, HitTestItem(logical_point));
        return 0;
    }
    case WM_KILLFOCUS:
        if (reinterpret_cast<HWND>(wparam) == search_edit_ || IsChild(hwnd_, reinterpret_cast<HWND>(wparam))) {
            return 0;
        }
        search_focused_ = false;
        search_caret_on_ = false;
        KillTimer(hwnd_, kSearchCaretTimer);
        if (!prompt_open_) {
            HideIfInactive(reinterpret_cast<HWND>(wparam));
        }
        return 0;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
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
