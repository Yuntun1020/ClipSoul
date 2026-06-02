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
#include <oleacc.h>
#include <oleauto.h>
#include <UIAutomationClient.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
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
namespace {
constexpr wchar_t kPopupClass[] = L"ClipSoul.PopupWindow";
constexpr UINT_PTR kAnimationTimer = 42;
constexpr UINT_PTR kHoverTimer = 43;
constexpr UINT_PTR kSearchCaretTimer = 44;
constexpr UINT_PTR kOutsideClickTimer = 45;
constexpr UINT_PTR kItemLongPressTimer = 46;
constexpr UINT_PTR kSuppressInactiveHideTimer = 47;
constexpr UINT_PTR kShellTopmostRaiseTimer = 48;
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
constexpr int kPhraseEditId = 4101;
constexpr int kPhraseSaveId = 4102;
constexpr int kPhraseCancelId = 4103;
constexpr int kPhraseNoteEditId = 4104;
constexpr wchar_t kPhrasePromptClass[] = L"ClipSoul.FavoritePhrasePrompt";
constexpr int kTextPromptEditId = 4201;
constexpr wchar_t kTextPromptClass[] = L"ClipSoul.TextPrompt";
constexpr UINT_PTR kPhraseHoverTimer = 62;
constexpr DWORD kAttachParentProcess = static_cast<DWORD>(-1);
constexpr DWORD kPopupBandShellSurface = 16;

using CreateWindowInBandProc = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                             LPVOID, DWORD);

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
}

HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

struct PhrasePromptState {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    HWND note_edit = nullptr;
    std::wstring text;
    std::wstring note;
    bool accepted = false;
    bool tracking_mouse = false;
    int hover_target = 0;
    float hover_progress = 0.0f;
};

struct FavoritePhraseInput {
    std::wstring text;
    std::wstring note;
};

struct TextPromptState {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    std::wstring title;
    std::wstring subtitle;
    std::wstring initial_text;
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

bool IsPromptEditMessage(HWND edit, HWND candidate) {
    return edit && candidate && (candidate == edit || IsChild(edit, candidate));
}

void DispatchPromptMessage(HWND prompt, HWND edit, HWND note_edit, MSG& message) {
    if (IsPromptEditMessage(edit, message.hwnd) || IsPromptEditMessage(note_edit, message.hwnd)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
        return;
    }
    if (!IsDialogMessageW(prompt, &message)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

int PhraseHitTarget(int x, int y) {
    if (x >= 286 && x <= 310 && y >= 12 && y <= 36) return 1;
    if (x >= 146 && x <= 216 && y >= 236 && y <= 264) return 2;
    if (x >= 228 && x <= 298 && y >= 236 && y <= 264) return 3;
    return 0;
}

int TextPromptHitTarget(int x, int y) {
    if (x >= 286 && x <= 310 && y >= 12 && y <= 36) return 1;
    if (x >= 146 && x <= 216 && y >= 158 && y <= 186) return 2;
    if (x >= 228 && x <= 298 && y >= 158 && y <= 186) return 3;
    return 0;
}

UiRect FavoriteGroupDeleteConfirmPanelRect() {
    const auto& metrics = PopupMetrics();
    constexpr float kWidth = 236.0f;
    constexpr float kHeight = 136.0f;
    const float left = (static_cast<float>(metrics.width) - kWidth) * 0.5f;
    const float top = PopupTabsTop() + 50.0f;
    return UiRect{left, top, left + kWidth, top + kHeight};
}

UiRect FavoriteGroupDeleteConfirmDeleteRect(const UiRect& panel) {
    return UiRect{panel.right - 166.0f, panel.bottom - 42.0f, panel.right - 96.0f, panel.bottom - 14.0f};
}

UiRect FavoriteGroupDeleteConfirmCancelRect(const UiRect& panel) {
    return UiRect{panel.right - 84.0f, panel.bottom - 42.0f, panel.right - 14.0f, panel.bottom - 14.0f};
}

PopupFavoriteGroupDeleteConfirmTarget HitTestFavoriteGroupDeleteConfirm(POINT point) {
    const auto panel = FavoriteGroupDeleteConfirmPanelRect();
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

LRESULT CALLBACK TextPromptProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<TextPromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        state = static_cast<TextPromptState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_CREATE:
        state->initial_text = TextForMultilineEdit(state->initial_text);
        state->edit = CreateWindowExW(0, L"EDIT", state->initial_text.c_str(),
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL |
                                          ES_WANTRETURN,
                                      24, 82, 272, 58, hwnd, ControlId(kTextPromptEditId),
                                      state->instance, nullptr);
        StyleChildControl(state->edit);
        SetFocus(state->edit);
        return 0;
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
        RECT input_rect{22, 78, 298, 144};
        DrawGdiRoundedPanel(mem_dc, input_rect, 12, RGB(255, 255, 255), RGB(226, 234, 242));

        const bool save_hover = state->hover_target == 2 && state->hover_progress > 0.0f;
        RECT save_button{146, 158, 216, 186};
        DrawGdiRoundedPanel(mem_dc, save_button, 12,
                            save_hover ? RGB(13, 148, 145) : RGB(14, 165, 164),
                            save_hover ? RGB(13, 148, 145) : RGB(14, 165, 164));
        const bool cancel_hover = state->hover_target == 3 && state->hover_progress > 0.0f;
        RECT cancel_button{228, 158, 298, 186};
        DrawGdiRoundedPanel(mem_dc, cancel_button, 12,
                            cancel_hover ? RGB(244, 255, 253) : RGB(255, 255, 255),
                            cancel_hover ? RGB(101, 218, 210) : RGB(220, 231, 244));

        SetBkMode(mem_dc, TRANSPARENT);
        auto font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HGDIOBJ old_font = SelectObject(mem_dc, font);
        SetTextColor(mem_dc, RGB(23, 32, 51));
        TextOutW(mem_dc, 22, 18, state->title.c_str(), static_cast<int>(state->title.size()));
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
        TextOutW(mem_dc, 22, 48, state->subtitle.c_str(), static_cast<int>(state->subtitle.size()));
        SetTextColor(mem_dc, RGB(255, 255, 255));
        DrawCenteredText(mem_dc, save_button, L"保存");
        SetTextColor(mem_dc, RGB(23, 32, 51));
        DrawCenteredText(mem_dc, cancel_button, L"取消");
        SelectObject(mem_dc, old_font);
        BitBlt(dc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, mem_dc, 0, 0, SRCCOPY);
        SelectObject(mem_dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(mem_dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const int target = TextPromptHitTarget(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (target == 1 || target == 3) {
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
            state->text = NormalizeEditableNote(value);
            state->accepted = !state->text.empty();
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
        const int target = TextPromptHitTarget(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
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
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
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
                                      24, 78, 272, 70, hwnd, ControlId(kPhraseEditId),
                                      state->instance, nullptr);
        StyleChildControl(state->edit);
        state->note_edit = CreateWindowExW(0, L"EDIT", L"",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL |
                                               ES_WANTRETURN,
                                           24, 176, 272, 44, hwnd, ControlId(kPhraseNoteEditId),
                                           state->instance, nullptr);
        StyleChildControl(state->note_edit);
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
        RECT input_rect{22, 74, 298, 150};
        DrawGdiRoundedPanel(mem_dc, input_rect, 12, RGB(255, 255, 255), RGB(226, 234, 242));
        RECT note_rect{22, 172, 298, 222};
        DrawGdiRoundedPanel(mem_dc, note_rect, 12, RGB(255, 255, 255), RGB(226, 234, 242));

        const bool save_hover = state->hover_target == 2 && state->hover_progress > 0.0f;
        RECT save_button{146, 236, 216, 264};
        DrawGdiRoundedPanel(mem_dc, save_button, 12,
                            save_hover ? RGB(13, 148, 145) : RGB(14, 165, 164),
                            save_hover ? RGB(13, 148, 145) : RGB(14, 165, 164));

        const bool cancel_hover = state->hover_target == 3 && state->hover_progress > 0.0f;
        RECT cancel_button{228, 236, 298, 264};
        DrawGdiRoundedPanel(mem_dc, cancel_button, 12,
                            cancel_hover ? RGB(244, 255, 253) : RGB(255, 255, 255),
                            cancel_hover ? RGB(101, 218, 210) : RGB(220, 231, 244));

        SetBkMode(mem_dc, TRANSPARENT);
        auto font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HGDIOBJ old_font = SelectObject(mem_dc, font);
        SetTextColor(mem_dc, RGB(23, 32, 51));
        TextOutW(mem_dc, 22, 18, L"\u6dfb\u52a0\u5e38\u7528\u8bed", 5);
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
        TextOutW(mem_dc, 22, 154, L"备注", 2);
        SetTextColor(mem_dc, RGB(255, 255, 255));
        RECT save_rect{146, 236, 216, 264};
        DrawCenteredText(mem_dc, save_rect, L"保存");
        SetTextColor(mem_dc, RGB(23, 32, 51));
        RECT cancel_rect{228, 236, 298, 264};
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
            const int note_length = GetWindowTextLengthW(state->note_edit);
            std::wstring note_value(static_cast<size_t>(note_length) + 1, L'\0');
            if (note_length > 0) {
                GetWindowTextW(state->note_edit, note_value.data(), note_length + 1);
            }
            note_value.resize(static_cast<size_t>(note_length));
            state->note = NormalizeEditableNote(note_value);
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

std::optional<FavoritePhraseInput> PromptFavoritePhrase(HWND owner, HINSTANCE instance) {
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
    const int height = 300;
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
        DispatchPromptMessage(hwnd, state.edit, state.note_edit, message);
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    SetFocus(owner);
    if (state.accepted) {
        return FavoritePhraseInput{state.text, state.note};
    }
    return std::nullopt;
}

std::optional<std::wstring> PromptText(HWND owner, HINSTANCE instance, std::wstring title,
                                       std::wstring subtitle, std::wstring initial_text = L"") {
    WNDCLASSW wc{};
    wc.lpfnWndProc = TextPromptProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.lpszClassName = kTextPromptClass;
    RegisterClassW(&wc);

    TextPromptState state;
    state.instance = instance;
    state.title = std::move(title);
    state.subtitle = std::move(subtitle);
    state.initial_text = std::move(initial_text);

    RECT owner_rect{};
    GetWindowRect(owner, &owner_rect);
    const int width = 320;
    const int height = 220;
    const int x = owner_rect.left + (owner_rect.right - owner_rect.left - width) / 2;
    const int y = owner_rect.top + 110;
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kTextPromptClass, L"ClipSoul",
                                WS_POPUP, x, y, width, height, owner, nullptr, instance, &state);
    if (!hwnd) {
        return std::nullopt;
    }

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    MSG message{};
    while (IsWindow(hwnd) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        DispatchPromptMessage(hwnd, state.edit, nullptr, message);
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
        return L"\u25d0";
    case ClipboardKind::Files:
        return L"F";
    case ClipboardKind::Link:
        return L"\u2197";
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
    swprintf_s(buffer, L"%04d\u5e74%d\u6708", year, month);
    return buffer;
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

enum class TextAvoidSource {
    None,
    GuiCaret,
    MsaaCaret,
    ConsoleCursor,
    JavaAccessBridgeCaret,
    VisualTerminalCaret,
    AutomationText2Caret,
    AutomationText2Selection,
    AutomationTextSelection,
    AutomationTerminalVisibleLine,
    AutomationElementRect,
};

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

struct FocusedTextContext {
    bool editable = false;
    bool has_rect = false;
    RECT rect{};
};

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
    ReleasePtr(detail_format_);
    ReleasePtr(centered_small_format_);
    ReleasePtr(wic_factory_);
    ReleasePtr(dwrite_factory_);
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
    wc.style = CS_DROPSHADOW;
    wc.lpszClassName = kPopupClass;
    RegisterClassW(&wc);

    const UINT dpi = DpiForWindowHandle(owner);
    const SIZE size = PhysicalPopupSize(dpi);
    const DWORD popup_ex_style = WS_EX_TOOLWINDOW | WS_EX_TOPMOST |
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
    return true;
}

void PopupWindow::Show(HWND target) {
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
    const std::wstring target_class = WindowClassName(target);
    const std::wstring target_title = WindowTitle(target);
    const std::wstring target_process = WindowProcessName(target);
    shell_topmost_raise_ = PopupTargetNeedsShellTopmostRaise(target_class, target_title, target_process);
    shell_topmost_raise_until_ = shell_topmost_raise_ ? GetTickCount() + kShellTopmostRaiseMs : 0;
    if (shell_topmost_raise_ && target && IsWindow(target)) {
        SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(target));
    }
    ReloadItems();
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
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, size.cx, size.cy, flags);
    UpdatePopupLogicalSize();
    scroll_offset_ = PopupScrollOffsetAfterReopen(scroll_offset_, TotalScrollHeight(), ViewportScrollHeight());
    RECT shown_rect{};
    GetWindowRect(hwnd_, &shown_rect);
    const LONG_PTR shown_ex_style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
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
    if (shell_topmost_raise_) {
        SetTimer(hwnd_, kShellTopmostRaiseTimer, 50, nullptr);
    }
    if (activate_on_show) {
        ActivatePopupWindow(hwnd_);
        DebugLog(L"popup activate foreground=" + ForegroundWindowDebugInfo());
    }
    UpdateSearchEditBounds();
    SyncSearchEdit();
    SetTimer(hwnd_, kOutsideClickTimer, 50, nullptr);
    if (PopupShouldAutoFocusSearchOnShow(search_edit_ != nullptr)) {
        search_focused_ = true;
        search_caret_on_ = true;
        SetFocus(search_edit_);
        SetTimer(hwnd_, kSearchCaretTimer, GetCaretBlinkTime(), nullptr);
    } else {
        search_focused_ = false;
        search_caret_on_ = false;
        KillTimer(hwnd_, kSearchCaretTimer);
    }
    open_progress_ = 1.0f;
    KillTimer(hwnd_, kAnimationTimer);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool PopupWindow::IsVisible() const {
    return hwnd_ && IsWindowVisible(hwnd_);
}

std::optional<int64_t> PopupWindow::SelectedItemId() const {
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(items_.size())) {
        return items_[selected_index_].id;
    }
    return std::nullopt;
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
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::Hide() {
    Hide(L"unspecified");
}

void PopupWindow::Hide(std::wstring_view reason) {
    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    std::wstring log = L"popup hide reason=";
    log += reason;
    log += L" rect=";
    log += RectToLogString(rect);
    log += L" foreground=";
    log += ForegroundWindowDebugInfo();
    DebugLog(log);
    KillTimer(hwnd_, kOutsideClickTimer);
    KillTimer(hwnd_, kSuppressInactiveHideTimer);
    KillTimer(hwnd_, kShellTopmostRaiseTimer);
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
    favorite_groups_ = store_.FavoriteGroups();
    if (view_mode_ == ViewMode::Favorites && active_favorite_group_id_) {
        const auto id = *active_favorite_group_id_;
        const auto found = std::find_if(favorite_groups_.begin(), favorite_groups_.end(),
                                        [id](const FavoriteGroup& group) { return group.id == id; });
        if (found == favorite_groups_.end()) {
            active_favorite_group_id_.reset();
        }
    }
    items_ = store_.Query(BuildQuery());
    selected_index_ = std::clamp(selected_index_, 0, std::max(0, static_cast<int>(items_.size()) - 1));
    ClampScrollToCurrentPopupHeight();
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
    if (SUCCEEDED(dwrite_factory_->CreateTextLayout(query_.c_str(), static_cast<UINT32>(query_.size()),
                                                    small_format_, search_layout.text.Width(),
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
    if (SUCCEEDED(dwrite_factory_->CreateTextLayout(query_.c_str(), static_cast<UINT32>(query_.size()),
                                                    small_format_, search_layout.text.Width(),
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

void PopupWindow::UpdateThemeFromSettings() {
    const auto settings = store_.LoadSettings();
    const int next_theme = std::clamp(settings.theme_mode, 0, 2);
    const bool resize_changed = popup_resizable_ != settings.popup_resizable;
    popup_resizable_ = settings.popup_resizable;
    if (next_theme == theme_mode_ && text_brush_) {
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
    const bool next_resizable = store_.LoadSettings().popup_resizable;
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
    query.limit = 60;
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
    selected_index_ = ClampPopupSelectedIndex(static_cast<int>(items_.size()), selected_index_ + delta);
    scroll_offset_ = ScrollOffsetToRevealSelection(scroll_offset_, selected_index_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::AdvanceSelectionAfterContinuousPaste() {
    if (items_.empty()) {
        return;
    }
    selected_index_ = PopupNextSelectedIndex(static_cast<int>(items_.size()), selected_index_);
    scroll_offset_ = ScrollOffsetToRevealSelection(scroll_offset_, selected_index_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool PopupWindow::PasteSelectedForContinuousPaste() {
    if (items_.empty()) {
        return false;
    }
    selected_index_ = ClampPopupSelectedIndex(static_cast<int>(items_.size()), selected_index_);
    const auto item = items_[selected_index_];
    if (!paste_controller_.RestoreToClipboard(item, hwnd_)) {
        return false;
    }
    paste_controller_.SendPaste(paste_target_);
    AdvanceSelectionAfterContinuousPaste();
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
    CancelItemPress();
    switch (PopupItemPressReleaseActionFor(pressed == released, long_press)) {
    case PopupItemPressReleaseAction::Paste:
        selected_index_ = pressed;
        ActivateSelection();
        break;
    case PopupItemPressReleaseAction::SelectOnly:
        selected_index_ = pressed;
        scroll_offset_ = ScrollOffsetToRevealSelection(scroll_offset_, selected_index_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
    case PopupItemPressReleaseAction::None:
        break;
    }
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
            Hide(L"paste");
        }
        paste_controller_.SendPaste(paste_target_);
    }
}

void PopupWindow::ToggleMultiSelect() {
    multi_select_ = !multi_select_;
    selection_.Clear();
    if (multi_select_) {
        filter_open_ = false;
        favorite_group_menu_open_ = false;
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
            Hide(L"multi-paste");
        } else {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
}

void PopupWindow::PromptCreateFavoriteGroup() {
    prompt_open_ = true;
    BeginTransientHideSuppression();
    if (const auto name = PromptText(hwnd_, instance_, L"\u65b0\u5efa\u6536\u85cf\u5939", L"\u8f93\u5165\u5206\u7ec4\u540d\u79f0")) {
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
    EndTransientHideSuppressionSoon();
    SetTimer(hwnd_, kOutsideClickTimer, 50, nullptr);
}

void PopupWindow::PromptEditNote(int64_t id) {
    const auto item = store_.Get(id);
    if (!item) {
        return;
    }
    prompt_open_ = true;
    BeginTransientHideSuppression();
    if (const auto note = PromptText(hwnd_, instance_, L"编辑备注", L"备注会显示在收藏内容下方", item->note)) {
        store_.SetNote(id, *note);
        ReloadItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    prompt_open_ = false;
    EndTransientHideSuppressionSoon();
}

void PopupWindow::PromptAddFavoritePhrase() {
    prompt_open_ = true;
    BeginTransientHideSuppression();
    if (const auto phrase = PromptFavoritePhrase(hwnd_, instance_)) {
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
    EndTransientHideSuppressionSoon();
}

void PopupWindow::ToggleFavoriteGroupMenu() {
    favorite_group_menu_open_ = !favorite_group_menu_open_;
    if (favorite_group_menu_open_) {
        filter_open_ = false;
        hover_filter_target_ = PopupFilterTarget::None;
        hover_filter_date_.reset();
    }
    hover_progress_ = 0.0f;
    SetTimer(hwnd_, kHoverTimer, 16, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::ToggleExpanded(int64_t id) {
    if (expanded_item_id_ && *expanded_item_id_ == id) {
        expanded_item_id_.reset();
    } else {
        expanded_item_id_ = id;
    }
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
    std::wstring log = L"popup position target=";
    log += HexWindow(target);
    log += L" class=";
    log += target_class;
    log += L" process=";
    log += target_process;
    log += L" keyboard=";
    log += keyboard_invocation_ ? L"1" : L"0";
    DebugLog(log);
    RECT avoid{};
    TextAvoidSource avoid_source = TextAvoidSource::None;
    const auto text_position = [&](const wchar_t* path, TextAvoidSource source, RECT resolved_avoid) {
        HMONITOR avoid_monitor = MonitorFromRect(&resolved_avoid, MONITOR_DEFAULTTONEAREST);
        const RECT avoid_work = WorkAreaForMonitor(avoid_monitor, work);
        const RECT avoid_monitor_rect = MonitorRectForMonitor(avoid_monitor, monitor);
        const UINT avoid_dpi = DpiForMonitorHandle(avoid_monitor, dpi);
        const POINT position =
            PopupKeyboardInvocationPosition(true, resolved_avoid, size, avoid_monitor_rect, avoid_work, avoid_dpi);
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
    if (PopupTargetNeedsShellTopmostRaise(target_class, target_title, target_process)) {
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
        DebugLog(L"popup inactive hide next=" + HexWindow(next_active) + L" next_class=" +
                 WindowClassName(next_active) + L" next_process=" + WindowProcessName(next_active));
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
            desired = static_cast<int>(std::ceil(PopupListTop() + metrics.card_height +
                                                 ExpandedExtraHeightForItem(*found) + metrics.card_gap + 8.0f));
        }
    }
    return std::clamp(desired, kMinPopupHeight, kMaxPopupHeight);
}

float PopupWindow::ExpandedExtraHeightForItem(const HistoryItem& item) const {
    const auto& metrics = PopupMetrics();
    const float detail_width = std::max(80.0f, static_cast<float>(popup_logical_width_ - metrics.margin * 2 - 96));
    const auto detail = ItemDetailText(item);
    const float measured_height = MeasureDetailTextHeight(detail, detail_width);
    if (measured_height <= 0.0f) {
        return PopupExpandedCardExtraHeightForText(detail, detail_width);
    }
    const bool image_item = item.kind == ClipboardKind::Image && !item.payload_path.empty();
    const bool image_file_item =
        item.kind == ClipboardKind::Files && !item.files.empty() && PopupFileCanUseImagePreview(item.files.front());
    if (image_item || image_file_item) {
        return PopupExpandedImageCardExtraHeightForMeasuredDetail(measured_height);
    }
    return PopupExpandedCardExtraHeightForMeasuredDetail(measured_height);
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
    const auto& metrics = PopupMetrics();
    float top = 0.0f;
    const int clamped_index = std::clamp(item_index, 0, static_cast<int>(items_.size()));
    for (int index = 0; index < clamped_index; ++index) {
        top += ItemScrollHeight(index);
        if (index + 1 < static_cast<int>(items_.size())) {
            top += static_cast<float>(metrics.card_gap);
        }
    }
    return top;
}

float PopupWindow::ItemScrollHeight(int item_index) const {
    const auto& metrics = PopupMetrics();
    if (item_index < 0 || item_index >= static_cast<int>(items_.size())) {
        return 0.0f;
    }
    const auto& item = items_[item_index];
    const bool expanded = expanded_item_id_ && *expanded_item_id_ == item.id;
    return static_cast<float>(metrics.card_height) + (expanded ? ExpandedExtraHeightForItem(item) : 0.0f);
}

float PopupWindow::TotalScrollHeight() const {
    const auto& metrics = PopupMetrics();
    if (items_.empty()) {
        return 0.0f;
    }
    float height = 0.0f;
    for (int index = 0; index < static_cast<int>(items_.size()); ++index) {
        height += ItemScrollHeight(index);
        if (index + 1 < static_cast<int>(items_.size())) {
            height += static_cast<float>(metrics.card_gap);
        }
    }
    return height;
}

float PopupWindow::ViewportScrollHeight() const {
    return std::max(0.0f, static_cast<float>(popup_logical_height_) - PopupListTop() - 4.0f);
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
    const float viewport_height = ViewportScrollHeight();
    float offset = ClampSmoothScrollOffset(requested_offset);
    if (selected_top < offset) {
        offset = selected_top;
    }
    if (selected_bottom > offset + viewport_height) {
        offset = selected_bottom - viewport_height;
    }
    return ClampSmoothScrollOffset(offset);
}

int PopupWindow::FirstVisibleItemIndex() const {
    float top = 0.0f;
    for (int index = 0; index < static_cast<int>(items_.size()); ++index) {
        const float bottom = top + ItemScrollHeight(index);
        if (bottom >= scroll_offset_) {
            return index;
        }
        top = bottom + static_cast<float>(PopupMetrics().card_gap);
    }
    return std::max(0, static_cast<int>(items_.size()) - 1);
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
        scroll_offset_ = next_offset;
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
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          18.0f, L"zh-CN", &title_format_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          14.0f, L"zh-CN", &body_format_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          12.0f, L"zh-CN", &small_format_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                          12.0f, L"zh-CN", &detail_format_);
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
        detail_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_CHARACTER);
        detail_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
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
    ReleasePtr(add_favorite_folder_outline_icon_);
    ReleasePtr(add_favorite_folder_filled_icon_);
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
    case IconId::AddFavoriteFolderOutline:
        slot = &add_favorite_folder_outline_icon_;
        filename = L"add-favorite-folder-outline.png";
        resource_id = IDR_CLIPSOUL_ICON_ADD_FAVORITE_FOLDER_OUTLINE;
        break;
    case IconId::AddFavoriteFolderFilled:
        slot = &add_favorite_folder_filled_icon_;
        filename = L"add-favorite-folder-filled.png";
        resource_id = IDR_CLIPSOUL_ICON_ADD_FAVORITE_FOLDER_FILLED;
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

    const SIZE target = PopupPreviewDecodeSize(static_cast<unsigned>(width), static_cast<unsigned>(height),
                                               PopupImagePreviewDecodePixelLimit());
    if (target.cx <= 0 || target.cy <= 0) {
        image_preview_cache_[key] = nullptr;
        return nullptr;
    }

    std::vector<uint32_t> pixels(static_cast<size_t>(target.cx) * static_cast<size_t>(target.cy));
    const auto* src_base = reinterpret_cast<const uint8_t*>(dib.data() + pixel_offset);
    for (int y = 0; y < target.cy; ++y) {
        const int sampled_y = std::min(height - 1, static_cast<int>((static_cast<int64_t>(y) * height) / target.cy));
        const int src_y = top_down ? sampled_y : height - 1 - sampled_y;
        const auto* src = src_base + row_stride * static_cast<size_t>(src_y);
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
    image_preview_cache_[key] = bitmap;
    return bitmap;
}

ID2D1Bitmap* PopupWindow::LoadImageFilePreviewBitmap(const std::filesystem::path& path) {
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

ID2D1Bitmap* PopupWindow::CachedIconBitmap(IconId icon) const {
    switch (icon) {
    case IconId::Pin:
        return pin_icon_;
    case IconId::PinActive:
        return pin_active_icon_;
    case IconId::Close:
        return close_icon_;
    case IconId::Filter:
        return filter_icon_;
    case IconId::FilterActive:
        return filter_active_icon_;
    case IconId::MultiSelect:
        return multi_select_icon_;
    case IconId::MultiSelectActive:
        return multi_select_active_icon_;
    case IconId::Trash:
        return trash_icon_;
    case IconId::TextKind:
        return text_kind_icon_;
    case IconId::LinkKind:
        return link_kind_icon_;
    case IconId::AddFavoriteFolderOutline:
        return add_favorite_folder_outline_icon_;
    case IconId::AddFavoriteFolderFilled:
        return add_favorite_folder_filled_icon_;
    }
    return nullptr;
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

ID2D1Bitmap* PopupWindow::CachedFileIconBitmap(const std::wstring& path) const {
    const std::wstring extension = std::filesystem::path(path).extension().wstring();
    const std::wstring key = extension.empty() ? path : extension;
    if (const auto found = file_icon_cache_.find(key); found != file_icon_cache_.end()) {
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
    const bool fast_interaction = moving_window_ || resizing_window_;
    ID2D1Bitmap* bitmap = PopupShouldLoadUiIcon(fast_interaction) ? LoadIconBitmap(icon) : CachedIconBitmap(icon);
    if (!PopupShouldDrawCachedMediaDuringFastInteraction(fast_interaction, bitmap != nullptr)) {
        return;
    }
    if (bitmap) {
        render_target_->DrawBitmap(bitmap, Rect(rect), opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
}

void PopupWindow::DrawCardMedia(const HistoryItem& item, const PopupCardLayout& card, ID2D1SolidColorBrush* brush) {
    const bool fast_interaction = moving_window_ || resizing_window_;
    const bool load_previews = PopupShouldLoadImagePreview(fast_interaction);
    const auto kind_color = KindColor(item.kind);
    if (item.kind == ClipboardKind::Image) {
        brush->SetColor(D2D1::ColorF(0xFFFFFF, 0.46f));
        render_target_->FillRoundedRectangle(RoundRect(card.image_preview, 10), brush);
        ID2D1Bitmap* bitmap =
            load_previews ? LoadImagePreviewBitmap(item.payload_path) : CachedImagePreviewBitmap(item.payload_path);
        if (PopupShouldDrawCachedMediaDuringFastInteraction(fast_interaction, bitmap != nullptr) && bitmap) {
            const auto size = bitmap->GetSize();
            const auto fitted =
                FitImageRectToBounds(size.width, size.height, ShrinkRect(card.image_preview, 2.0f, 2.0f));
            render_target_->DrawBitmap(bitmap, Rect(fitted), 0.96f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            brush->SetColor(D2D1::ColorF(0xFFFFFF, 0.62f));
            render_target_->DrawRoundedRectangle(RoundRect(card.image_preview, 8), brush, 1.0f);
            return;
        }
    }

    if (item.kind == ClipboardKind::Files && !item.files.empty()) {
        const auto& first_file = item.files.front();
        if (PopupFileCanUseImagePreview(first_file)) {
            brush->SetColor(D2D1::ColorF(0xFFFFFF, 0.46f));
            render_target_->FillRoundedRectangle(RoundRect(card.image_preview, 10), brush);
            ID2D1Bitmap* bitmap =
                load_previews ? LoadImageFilePreviewBitmap(first_file) : CachedImagePreviewBitmap(first_file);
            if (PopupShouldDrawCachedMediaDuringFastInteraction(fast_interaction, bitmap != nullptr) && bitmap) {
                const auto size = bitmap->GetSize();
                const auto fitted =
                    FitImageRectToBounds(size.width, size.height, ShrinkRect(card.image_preview, 2.0f, 2.0f));
                render_target_->DrawBitmap(bitmap, Rect(fitted), 0.96f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                brush->SetColor(D2D1::ColorF(0xFFFFFF, 0.62f));
                render_target_->DrawRoundedRectangle(RoundRect(card.image_preview, 8), brush, 1.0f);
                return;
            }
        }
        ID2D1Bitmap* file_icon =
            PopupShouldLoadFileIcon(fast_interaction) ? LoadFileIconBitmap(first_file) : CachedFileIconBitmap(first_file);
        if (PopupShouldDrawCachedMediaDuringFastInteraction(fast_interaction, file_icon != nullptr) && file_icon) {
            render_target_->DrawBitmap(file_icon, Rect(card.file_icon), 0.96f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
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
        const auto icon_rect = PopupCardKindIconRect(card);
        DrawIcon(link ? IconId::LinkKind : IconId::TextKind, icon_rect, 0.92f);
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
    if (PopupPaintShouldUpdateLayout()) {
        UpdatePopupLogicalSize();
        ClampScrollToCurrentPopupHeight();
    }
    const auto palette = ResolvePopupThemePalette(theme_mode_, IsSystemDarkTheme());
    render_target_->BeginDraw();
    render_target_->Clear(D2D1::ColorF(0x000000, 0.0f));

    ID2D1SolidColorBrush* brush = nullptr;
    render_target_->CreateSolidColorBrush(ColorWithAlpha(palette.window_tint, palette.window_opacity), &brush);
    const bool drawing_fast_interaction = moving_window_ || resizing_window_;
    auto drawEdgeShadow = [&](const UiRect& rect, float radius, float strength = 1.0f, float y_offset = 2.0f) {
        if (!PopupShouldDrawDecorativeShadows(drawing_fast_interaction)) {
            return;
        }
        const uint32_t color = palette.dark ? 0x000000 : 0x8FA0B6;
        for (int i = 2; i >= 1; --i) {
            const float t = static_cast<float>(i);
            const float alpha = (palette.dark ? 0.030f : 0.022f) * strength * (3.0f - t);
            brush->SetColor(D2D1::ColorF(color, alpha));
            render_target_->FillRoundedRectangle(
                RoundRect(rect.left - t, rect.top + y_offset - t * 0.4f,
                          rect.right + t, rect.bottom + y_offset + t * 0.8f,
                          radius + t),
                brush);
        }
    };
    render_target_->FillRoundedRectangle(RoundRect(1.0f, 1.0f, popup_logical_width_ - 1.0f,
                                                   popup_logical_height_ - 1.0f,
                                                   static_cast<float>(metrics.corner_radius)),
                                         brush);
    brush->SetColor(ColorWithAlpha(palette.dark ? 0x5A6473 : 0xFFFFFF, palette.dark ? 0.42f : 0.62f));
    render_target_->DrawRoundedRectangle(RoundRect(1.0f, 1.0f, popup_logical_width_ - 1.0f,
                                                   popup_logical_height_ - 1.0f,
                                                   static_cast<float>(metrics.corner_radius)),
                                         brush, 1.0f);

    const auto header = BuildPopupHeaderLayoutForWidth(popup_logical_width_);
    render_target_->DrawTextW(L"ClipSoul", 8, title_format_, Rect(header.title), text_brush_);
    render_target_->DrawTextW(kClipSoulVersion.data(), static_cast<UINT32>(kClipSoulVersion.size()), small_format_,
                              Rect(header.title.left + 80.0f, header.title.top + 4.0f,
                                   header.title.left + 142.0f, header.title.bottom),
                              muted_brush_);
    auto drawHeaderButton = [&](const UiRect& rect, bool active, UiAction action) {
        const bool hovered = hover_action_ == action;
        const float hover = hovered ? hover_progress_ : 0.0f;
        if (active || hovered) {
            drawEdgeShadow(rect, 6.0f, active ? 0.42f : 0.28f + 0.12f * hover, 1.5f);
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
    const auto search_layout = BuildPopupSearchLayoutForWidth(popup_logical_width_);
    const bool search_hovered = hover_action_ == UiAction::Search || Contains(search_layout.box, hover_point_);
    const bool search_active = search_focused_ && GetFocus() == search_edit_;
    const float search_focus = PopupSearchFocusProgress(search_active, search_hovered, hover_progress_);
    if (search_hovered || search_active) {
        brush->SetColor(D2D1::ColorF(palette.accent, search_active ? 0.11f : 0.05f + 0.05f * search_focus));
        render_target_->FillRoundedRectangle(
            RoundRect(metrics.margin - 1.0f, search_top - 1.0f, popup_logical_width_ - metrics.margin + 1.0f,
                      search_top + metrics.search_height + 1.0f, 10),
            brush);
    }
    brush->SetColor(ColorWithAlpha(search_hovered || search_active ? (palette.dark ? 0x243040 : 0xFFFFFF)
                                                                   : palette.search_fill,
                                    palette.dark ? 0.76f + 0.08f * search_focus : 0.90f + 0.06f * search_focus));
    render_target_->FillRoundedRectangle(
        RoundRect(metrics.margin, search_top, popup_logical_width_ - metrics.margin, search_top + metrics.search_height, 9),
        brush);
    brush->SetColor(search_active ? D2D1::ColorF(palette.accent, 0.70f)
                                   : D2D1::ColorF(search_hovered ? palette.accent : palette.border,
                                                  search_hovered ? 0.42f + 0.18f * search_focus : 0.68f));
    render_target_->DrawRoundedRectangle(
        RoundRect(metrics.margin, search_top, popup_logical_width_ - metrics.margin, search_top + metrics.search_height, 9),
        brush, search_active ? 1.45f : 1.0f + 0.25f * search_focus);
    const float search_icon_x = static_cast<float>(metrics.margin) + 20.0f;
    const float search_icon_y = search_top + metrics.search_height * 0.5f;
    brush->SetColor(D2D1::ColorF(palette.muted, 0.72f));
    render_target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(search_icon_x, search_icon_y - 1.0f), 5.0f, 5.0f),
                                brush, 1.45f);
    render_target_->DrawLine(D2D1::Point2F(search_icon_x + 3.8f, search_icon_y + 3.0f),
                             D2D1::Point2F(search_icon_x + 8.0f, search_icon_y + 7.0f), brush, 1.45f);
    const bool has_composition = PopupSearchHasComposition(composing_, composition_text_);
    const auto search_text = PopupSearchCompositionDisplayText(query_, composing_, composition_text_);
    const auto search_selection = CurrentSearchSelection();
    if (PopupSearchShouldDrawSelection(search_active, !query_.empty(), search_selection)) {
        const float selection_left =
            ClampPopupSearchCaretX(search_layout, SearchTextOffsetDips(search_selection.start, false));
        const float selection_right =
            ClampPopupSearchCaretX(search_layout, SearchTextOffsetDips(search_selection.end, false));
        if (selection_right > selection_left) {
            brush->SetColor(D2D1::ColorF(palette.accent, palette.dark ? 0.34f : 0.22f));
            render_target_->FillRoundedRectangle(
                RoundRect(selection_left, search_layout.text.top + 3.0f, selection_right,
                          search_layout.text.bottom - 3.0f, 3.0f),
                brush);
        }
    }
    brush->SetColor(PopupSearchCompositionTextColor(has_composition, query_)
        ? D2D1::ColorF(palette.text, 0.94f)
        : D2D1::ColorF(palette.muted, 0.46f));
    if (has_composition && dwrite_factory_ && small_format_) {
        const auto committed_text = std::wstring(query_);
        IDWriteTextLayout* text_layout = nullptr;
        if (SUCCEEDED(dwrite_factory_->CreateTextLayout(
                search_text.data(), static_cast<UINT32>(search_text.size()),
                small_format_, search_layout.text.Width(), search_layout.text.Height(),
                &text_layout))) {
            DWRITE_TEXT_RANGE range{static_cast<UINT32>(committed_text.size()),
                                   static_cast<UINT32>(composition_text_.size())};
            text_layout->SetUnderline(TRUE, range);
            render_target_->DrawTextLayout(
                D2D1::Point2F(search_layout.text.left, search_layout.text.top),
                text_layout, brush);
            text_layout->Release();
        }
    } else {
        render_target_->DrawTextW(search_text.data(), static_cast<UINT32>(search_text.size()),
                                  small_format_, Rect(search_layout.text), brush);
    }
    if (PopupSearchCaretVisible(search_active, search_caret_on_) &&
        !PopupSearchHasSelection(search_selection) &&
        PopupSearchCaretVisibleDuringComposition(has_composition)) {
        const float measured_text_width = SearchCaretOffsetDips();
        const float caret_x = ClampPopupSearchCaretX(search_layout, measured_text_width);
        brush->SetColor(D2D1::ColorF(palette.accent, 0.82f));
        render_target_->DrawLine(D2D1::Point2F(caret_x, search_layout.text.top + 3.0f),
                                 D2D1::Point2F(caret_x, search_layout.text.bottom - 3.0f),
                                 brush, 1.0f);
    }
    if (!query_.empty()) {
        const bool clear_hovered = hover_action_ == UiAction::ClearSearch ||
                                   PopupSearchClearButtonHitTest(search_layout, true, hover_point_);
        const float clear_opacity = clear_hovered ? 0.95f : 0.82f;
        brush->SetColor(D2D1::ColorF(palette.muted, clear_opacity));
        const auto center = PopupSearchClearButtonCenterDips(search_layout);
        render_target_->DrawLine(D2D1::Point2F(static_cast<float>(center.x) - 4.0f, static_cast<float>(center.y) - 4.0f),
                                 D2D1::Point2F(static_cast<float>(center.x) + 4.0f, static_cast<float>(center.y) + 4.0f), brush, 1.8f);
        render_target_->DrawLine(D2D1::Point2F(static_cast<float>(center.x) + 4.0f, static_cast<float>(center.y) - 4.0f),
                                 D2D1::Point2F(static_cast<float>(center.x) - 4.0f, static_cast<float>(center.y) + 4.0f), brush, 1.8f);
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
        drawEdgeShadow(rect, 9.0f, active ? 0.48f : 0.30f + 0.12f * hover, 2.0f);
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
    const auto toolbar = BuildPopupToolbarLayoutForWidth(multi_select_, popup_logical_width_);
    if (multi_select_) {
        drawButton(toolbar.select_all, L"\u5168\u9009", L'A', UiAction::SelectAll, true);
        drawButton(toolbar.cancel_multi_select, L"取消", L'C', UiAction::MultiSelect);
        drawButton(toolbar.delete_selected, L"选中删除", L'D', UiAction::DeleteSelected);
        drawButton(toolbar.paste_selected, L"选中粘贴", L'P', UiAction::PasteSelected, true);
    } else {
        drawButton(toolbar.filter, L"\u7b5b\u9009", L'F', UiAction::Filter, filter_open_, true);
        drawButton(toolbar.multi_select, L"\u591a\u9009", L'M', UiAction::MultiSelect);
        drawButton(toolbar.clear_all, L"全部清除", L'D', UiAction::ClearAll);
    }

    const auto tabs = BuildPopupTabsLayoutForWidth(view_mode_ == ViewMode::Favorites, popup_logical_width_);
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
    render_target_->DrawTextW(L"\u6536\u85cf\u5939", 3, centered_small_format_, Rect(tabs.favorites),
                              view_mode_ == ViewMode::Favorites ? accent_brush_ : text_brush_);
    if (view_mode_ == ViewMode::Favorites) {
        const bool group_hovered = hover_action_ == UiAction::FavoriteGroupMenu;
        const float group_hover = group_hovered ? hover_progress_ : 0.0f;
        drawEdgeShadow(tabs.favorite_group, 11.0f, 0.24f + 0.12f * group_hover, 1.6f);
        brush->SetColor(group_hovered ? D2D1::ColorF(0xE0F7F4, 0.64f + 0.18f * group_hover)
                                      : ColorWithAlpha(palette.panel_fill, 0.72f));
        render_target_->FillRoundedRectangle(RoundRect(tabs.favorite_group, 10), brush);
        brush->SetColor(group_hovered ? D2D1::ColorF(palette.accent, 0.52f)
                                      : D2D1::ColorF(palette.border, 0.62f));
        render_target_->DrawRoundedRectangle(RoundRect(tabs.favorite_group, 10), brush, 0.9f);
        const auto group_icon_slot =
            PopupFavoriteFolderIconSlotForGroup(favorite_group_menu_open_ || active_favorite_group_id_.has_value());
        DrawIcon(group_icon_slot == PopupFavoriteFolderIconSlot::Filled ? IconId::AddFavoriteFolderFilled
                                                                        : IconId::AddFavoriteFolderOutline,
                 PopupFavoriteGroupIconRect(tabs), 0.82f);

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

    const auto list_clip = PopupListClipRectForHeight(popup_logical_width_, popup_logical_height_);
    render_target_->PushAxisAlignedClip(Rect(list_clip), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const int first_visible_index = FirstVisibleItemIndex();
    float y = PopupListTop() - (scroll_offset_ - ItemScrollTop(first_visible_index));
    for (int item_index = first_visible_index; item_index < static_cast<int>(items_.size()) &&
                                           y <= static_cast<float>(popup_logical_height_) - 4.0f;
         ++item_index) {
        const auto& item = items_[item_index];
        const bool selected = item_index == selected_index_;
        const bool hovered = item_index == hover_item_index_;
        const float hover = hovered ? hover_progress_ : 0.0f;
        const bool checked = selection_.IsSelected(item.id);
        auto card = BuildPopupCardLayout(multi_select_, y);
        const float width_delta = static_cast<float>(popup_logical_width_ - metrics.width);
        card.card.right += width_delta;
        card.title.right += width_delta;
        card.meta.right += width_delta;
        card.time.left += width_delta;
        card.time.right += width_delta;
        card.menu.left += width_delta;
        card.menu.right += width_delta;
        card.expand.left += width_delta;
        card.expand.right += width_delta;
        const bool expanded = expanded_item_id_ && *expanded_item_id_ == item.id;
        const auto detail = expanded ? ItemDetailText(item) : std::wstring{};
        const float expanded_height = expanded ? ExpandedExtraHeightForItem(item) : 0.0f;
        card.card.bottom += expanded_height;
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

        brush->SetColor(D2D1::ColorF(expanded ? palette.accent : palette.muted, expanded ? 0.92f : 0.70f));
        const float ex = (card.expand.left + card.expand.right) * 0.5f;
        const float ey = (card.expand.top + card.expand.bottom) * 0.5f;
        if (expanded) {
            render_target_->DrawLine(D2D1::Point2F(ex - 4.0f, ey - 2.0f), D2D1::Point2F(ex, ey + 3.0f), brush,
                                     1.5f);
            render_target_->DrawLine(D2D1::Point2F(ex + 4.0f, ey - 2.0f), D2D1::Point2F(ex, ey + 3.0f), brush,
                                     1.5f);
        } else {
            render_target_->DrawLine(D2D1::Point2F(ex - 2.0f, ey - 5.0f), D2D1::Point2F(ex + 3.0f, ey), brush,
                                     1.5f);
            render_target_->DrawLine(D2D1::Point2F(ex + 3.0f, ey), D2D1::Point2F(ex - 2.0f, ey + 5.0f), brush,
                                     1.5f);
        }

        if (expanded) {
            const UiRect detail_rect{card.title.left, card.meta.bottom + 6.0f, card.card.right - 16.0f,
                                     card.card.bottom - 12.0f};
            const bool image_item = item.kind == ClipboardKind::Image && !item.payload_path.empty();
            const bool image_file_item =
                item.kind == ClipboardKind::Files && !item.files.empty() && PopupFileCanUseImagePreview(item.files.front());
            if (image_item || image_file_item) {
                const UiRect image_rect{detail_rect.left, detail_rect.top, detail_rect.left + 116.0f,
                                        std::min(detail_rect.top + 116.0f, detail_rect.bottom)};
                brush->SetColor(D2D1::ColorF(0xFFFFFF, 0.54f));
                render_target_->FillRoundedRectangle(RoundRect(image_rect, 10), brush);
                const bool fast_interaction = moving_window_ || resizing_window_;
                const bool load_preview = PopupShouldLoadImagePreview(fast_interaction);
                ID2D1Bitmap* bitmap = nullptr;
                if (image_item) {
                    bitmap = load_preview ? LoadImagePreviewBitmap(item.payload_path)
                                          : CachedImagePreviewBitmap(item.payload_path);
                } else {
                    bitmap = load_preview ? LoadImageFilePreviewBitmap(item.files.front())
                                          : CachedImagePreviewBitmap(item.files.front());
                }
                if (PopupShouldDrawCachedMediaDuringFastInteraction(fast_interaction, bitmap != nullptr) && bitmap) {
                    const auto size = bitmap->GetSize();
                    const auto fitted =
                        FitImageRectToBounds(size.width, size.height, ShrinkRect(image_rect, 2.0f, 2.0f));
                    render_target_->DrawBitmap(bitmap, Rect(fitted), 0.98f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                }
                const std::wstring path = ItemDetailText(item);
                const UiRect path_rect{detail_rect.left, image_rect.bottom + 8.0f, detail_rect.right,
                                       detail_rect.bottom};
                render_target_->DrawTextW(path.c_str(), static_cast<UINT32>(path.size()), detail_format_,
                                          Rect(path_rect), muted_brush_);
            } else {
                render_target_->DrawTextW(detail.c_str(), static_cast<UINT32>(detail.size()), detail_format_,
                                          Rect(detail_rect), text_brush_);
            }
        }

        y += metrics.card_height + expanded_height + metrics.card_gap;
        if (expanded) {
            break;
        }
    }

    if (items_.empty()) {
        const auto empty_text = PopupEmptyMessage(view_mode_ == ViewMode::Favorites,
                                                  active_favorite_group_id_ ? ActiveFavoriteGroupLabel() : L"");
        render_target_->DrawTextW(empty_text.c_str(), static_cast<UINT32>(empty_text.size()), body_format_,
                                  Rect(metrics.margin, PopupListTop() + 24.0f,
                                       popup_logical_width_ - metrics.margin, PopupListTop() + 56.0f),
                                  muted_brush_);
    }

    render_target_->PopAxisAlignedClip();

    const float content_height = TotalScrollHeight();
    const float viewport_height = ViewportScrollHeight();
    if (content_height > viewport_height) {
        const auto track = PopupScrollbarTrackRectForSize(popup_logical_width_, popup_logical_height_);
        const float thumb_height = std::max(32.0f, track.Height() * viewport_height / content_height);
        const float max_offset = std::max(1.0f, content_height - viewport_height);
        const float thumb_top =
            track.top + (track.Height() - thumb_height) * ClampSmoothScrollOffset(scroll_offset_) / max_offset;
        const UiRect thumb{track.left, thumb_top, track.right, thumb_top + thumb_height};
        const bool scrollbar_hovered =
            hover_action_ == UiAction::Scrollbar ||
            Contains(PopupScrollbarHitRectForSize(popup_logical_width_, popup_logical_height_), hover_point_);
        const float thumb_opacity = PopupScrollbarThumbOpacity(scrollbar_hovered, dragging_scrollbar_, hover_progress_);
        brush->SetColor(D2D1::ColorF(palette.border, 0.34f));
        render_target_->FillRoundedRectangle(RoundRect(track, 2), brush);
        brush->SetColor(D2D1::ColorF(palette.accent, thumb_opacity));
            render_target_->FillRoundedRectangle(RoundRect(thumb, 2), brush);
    }

    if (favorite_group_menu_open_) {
        const auto menu = BuildPopupFavoriteGroupMenuLayout(favorite_groups_.size());
        const auto isMenuHovered = [&](PopupFavoriteGroupMenuTarget target, size_t index = 0) {
            if (hover_favorite_group_menu_hit_.target != target) {
                return 0.0f;
            }
            if (target == PopupFavoriteGroupMenuTarget::Group && hover_favorite_group_menu_hit_.group_index != index) {
                return 0.0f;
            }
            return hover_progress_;
        };
        auto drawMenuRow = [&](const UiRect& rect, const std::wstring& label, bool active, float hover,
                               bool group_row = false, size_t group_index = 0) {
            if (hover > 0.0f || active) {
                brush->SetColor(active ? D2D1::ColorF(0xDDFCF8, 0.72f)
                                       : D2D1::ColorF(0xF4FFFD, 0.58f * hover));
                render_target_->FillRoundedRectangle(RoundRect(rect, 10), brush);
            }
            if (active) {
                brush->SetColor(D2D1::ColorF(palette.accent, 0.88f));
                render_target_->FillRoundedRectangle(
                    RoundRect(rect.left + 5.0f, rect.top + 9.0f, rect.left + 8.0f, rect.bottom - 9.0f, 1.5f),
                    brush);
            }
            render_target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()), small_format_,
                                      Rect(rect.left + 16.0f, rect.top + 8.0f,
                                           group_row ? rect.right - 36.0f : rect.right - 14.0f,
                                           rect.bottom - 6.0f),
                                      active ? accent_brush_ : text_brush_);
            if (group_row) {
                const auto delete_rect = PopupFavoriteGroupMenuDeleteRect(rect);
                const bool delete_hover =
                    hover_favorite_group_menu_hit_.target == PopupFavoriteGroupMenuTarget::DeleteGroup &&
                    hover_favorite_group_menu_hit_.group_index == group_index;
                if (delete_hover) {
                    brush->SetColor(D2D1::ColorF(0xFFF1F2, 0.88f));
                    render_target_->FillEllipse(D2D1::Ellipse(
                                                    D2D1::Point2F((delete_rect.left + delete_rect.right) * 0.5f,
                                                                  (delete_rect.top + delete_rect.bottom) * 0.5f),
                                                    10.0f, 10.0f),
                                                brush);
                }
                brush->SetColor(D2D1::ColorF(delete_hover ? 0xE5484D : palette.muted, delete_hover ? 0.92f : 0.68f));
                const float cx = (delete_rect.left + delete_rect.right) * 0.5f;
                const float cy = (delete_rect.top + delete_rect.bottom) * 0.5f;
                render_target_->DrawLine(D2D1::Point2F(cx - 3.2f, cy - 3.2f),
                                         D2D1::Point2F(cx + 3.2f, cy + 3.2f), brush, 1.15f);
                render_target_->DrawLine(D2D1::Point2F(cx + 3.2f, cy - 3.2f),
                                         D2D1::Point2F(cx - 3.2f, cy + 3.2f), brush, 1.15f);
            }
        };

        drawEdgeShadow(menu.panel, 16.0f, 0.62f, 3.0f);
        brush->SetColor(ColorWithAlpha(palette.dark ? 0x1E293B : 0xFFFFFF, palette.dark ? 0.98f : 0.97f));
        render_target_->FillRoundedRectangle(RoundRect(menu.panel, 16), brush);
        brush->SetColor(ColorWithAlpha(palette.border, 0.78f));
        render_target_->DrawRoundedRectangle(RoundRect(menu.panel, 16), brush, 1.0f);

        drawMenuRow(menu.all_favorites, L"全部收藏", !active_favorite_group_id_,
                    isMenuHovered(PopupFavoriteGroupMenuTarget::AllFavorites));
        brush->SetColor(D2D1::ColorF(palette.border, 0.70f));
        render_target_->FillRectangle(Rect(menu.first_divider), brush);

        const size_t visible_groups = PopupFavoriteGroupMenuVisibleGroupCount(favorite_groups_.size());
        for (size_t index = 0; index < visible_groups; ++index) {
            const auto row = PopupFavoriteGroupMenuGroupRect(menu, index);
            const bool active = active_favorite_group_id_ && favorite_groups_[index].id == *active_favorite_group_id_;
            drawMenuRow(row, favorite_groups_[index].name, active,
                        isMenuHovered(PopupFavoriteGroupMenuTarget::Group, index), true, index);
        }

        brush->SetColor(D2D1::ColorF(palette.border, 0.70f));
        render_target_->FillRectangle(Rect(menu.second_divider), brush);
        drawMenuRow(menu.new_group, L"\u65b0\u5efa\u6536\u85cf\u5939...", false,
                    isMenuHovered(PopupFavoriteGroupMenuTarget::NewGroup));
    }

    if (pending_favorite_group_delete_) {
        const auto confirm = FavoriteGroupDeleteConfirmPanelRect();
        const auto delete_button = FavoriteGroupDeleteConfirmDeleteRect(confirm);
        const auto cancel_button = FavoriteGroupDeleteConfirmCancelRect(confirm);
        brush->SetColor(D2D1::ColorF(0x0B1220, palette.dark ? 0.24f : 0.10f));
        render_target_->FillRoundedRectangle(RoundRect(8.0f, PopupTabsTop() - 4.0f,
                                                       metrics.width - 8.0f,
                                                       std::min<float>(metrics.height - 8.0f, confirm.bottom + 18.0f),
                                                       16.0f),
                                             brush);
        drawEdgeShadow(confirm, 16.0f, 0.76f, 3.0f);
        brush->SetColor(ColorWithAlpha(palette.dark ? 0x1E293B : 0xFFFFFF, palette.dark ? 0.99f : 0.98f));
        render_target_->FillRoundedRectangle(RoundRect(confirm, 16), brush);
        brush->SetColor(ColorWithAlpha(palette.border, 0.82f));
        render_target_->DrawRoundedRectangle(RoundRect(confirm, 16), brush, 1.0f);

        render_target_->DrawTextW(L"\u5220\u9664\u6536\u85cf\u5939", 5, body_format_,
                                  Rect(confirm.left + 16.0f, confirm.top + 14.0f,
                                       confirm.right - 16.0f, confirm.top + 38.0f),
                                  text_brush_);
        const std::wstring message = L"\u5220\u9664 \"" + pending_favorite_group_delete_->name + L"\"\uff1f";
        render_target_->DrawTextW(message.c_str(), static_cast<UINT32>(message.size()), small_format_,
                                  Rect(confirm.left + 16.0f, confirm.top + 46.0f,
                                       confirm.right - 16.0f, confirm.top + 66.0f),
                                  text_brush_);
        render_target_->DrawTextW(L"\u6536\u85cf\u5939\u91cc\u7684\u5185\u5bb9\u4e5f\u4f1a\u88ab\u5220\u9664\u3002", 13, small_format_,
                                  Rect(confirm.left + 16.0f, confirm.top + 68.0f,
                                       confirm.right - 16.0f, confirm.top + 90.0f),
                                  muted_brush_);

        const bool delete_hover =
            hover_delete_confirm_target_ == PopupFavoriteGroupDeleteConfirmTarget::Delete;
        const bool cancel_hover =
            hover_delete_confirm_target_ == PopupFavoriteGroupDeleteConfirmTarget::Cancel;
        drawEdgeShadow(delete_button, 11.0f, delete_hover ? 0.44f : 0.30f, 2.0f);
        brush->SetColor(delete_hover ? D2D1::ColorF(0xDC2626, 0.98f) : D2D1::ColorF(0xE5484D, 0.94f));
        render_target_->FillRoundedRectangle(RoundRect(delete_button, 11), brush);
        brush->SetColor(D2D1::ColorF(0xFFFFFF, 0.34f));
        render_target_->DrawRoundedRectangle(RoundRect(delete_button, 11), brush, 0.8f);

        drawEdgeShadow(cancel_button, 11.0f, cancel_hover ? 0.34f : 0.22f, 2.0f);
        brush->SetColor(cancel_hover ? D2D1::ColorF(0xF4FFFD, 0.96f) : D2D1::ColorF(0xFFFFFF, 0.92f));
        render_target_->FillRoundedRectangle(RoundRect(cancel_button, 11), brush);
        brush->SetColor(cancel_hover ? D2D1::ColorF(palette.accent, 0.46f)
                                     : D2D1::ColorF(palette.border, 0.82f));
        render_target_->DrawRoundedRectangle(RoundRect(cancel_button, 11), brush, 1.0f);

        render_target_->DrawTextW(L"删除", 2, centered_small_format_, Rect(delete_button),
                                  text_brush_);
        brush->SetColor(D2D1::ColorF(0xFFFFFF, 0.98f));
        render_target_->DrawTextW(L"删除", 2, centered_small_format_, Rect(delete_button), brush);
        render_target_->DrawTextW(L"取消", 2, centered_small_format_, Rect(cancel_button), text_brush_);
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
        render_target_->DrawTextW(L"\u7b5b\u9009", 2, body_format_, Rect(fx + 16, fy + 12, fx + 128, fy + 34), text_brush_);
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
        const wchar_t* active_hint = selecting_start ? L"\u9009\u62e9\u5f00\u59cb" : L"\u9009\u62e9\u7ed3\u675f";
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
        drawDateBox(filter.start_date, L"\u5f00\u59cb\u65e5\u671f", FormatDateValue(date_filter_.start),
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
        render_target_->DrawTextW(L"\u2039", 1, small_format_, Rect(filter.calendar_prev), muted_brush_);
        const auto month_title = FormatMonthTitle(calendar_year_, calendar_month_);
        render_target_->DrawTextW(month_title.c_str(), static_cast<UINT32>(month_title.size()), small_format_,
                                  Rect(filter.calendar_title), text_brush_);
        render_target_->DrawTextW(L"\u203a", 1, small_format_, Rect(filter.calendar_next), muted_brush_);
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

bool PopupWindow::HandleFavoriteGroupMenuClick(POINT point) {
    if (!favorite_group_menu_open_) {
        return false;
    }

    const auto layout = BuildPopupFavoriteGroupMenuLayout(favorite_groups_.size());
    const auto hit = HitTestPopupFavoriteGroupMenu(layout, favorite_groups_.size(), static_cast<float>(point.x),
                                                   static_cast<float>(point.y));
    if (hit.target == PopupFavoriteGroupMenuTarget::None) {
        favorite_group_menu_open_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
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

    const auto target = HitTestFavoriteGroupDeleteConfirm(point);
    if (target == PopupFavoriteGroupDeleteConfirmTarget::Panel) {
        return true;
    }
    if (target == PopupFavoriteGroupDeleteConfirmTarget::Cancel ||
        target == PopupFavoriteGroupDeleteConfirmTarget::None) {
        pending_favorite_group_delete_.reset();
        hover_delete_confirm_target_ = PopupFavoriteGroupDeleteConfirmTarget::None;
        InvalidateRect(hwnd_, nullptr, FALSE);
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
    if (items_.empty() || point.y < PopupListTop() ||
        Contains(PopupScrollbarHitRectForSize(popup_logical_width_, popup_logical_height_), point)) {
        return -1;
    }
    const int first_index = FirstVisibleItemIndex();
    float top = PopupListTop() - (scroll_offset_ - ItemScrollTop(first_index));
    for (int item_index = first_index; item_index < static_cast<int>(items_.size()); ++item_index) {
        const auto& item = items_[item_index];
        auto card = BuildPopupCardLayout(false, top);
        const float width_delta = static_cast<float>(popup_logical_width_ - PopupMetrics().width);
        card.card.right += width_delta;
        card.expand.left += width_delta;
        card.expand.right += width_delta;
        const bool expanded = expanded_item_id_ && *expanded_item_id_ == item.id;
        const float expanded_height = expanded ? ExpandedExtraHeightForItem(item) : 0.0f;
        if (point.x >= card.card.left && point.x <= card.card.right && point.y >= card.card.top &&
            point.y <= card.card.bottom + expanded_height) {
            return item_index;
        }
        if (expanded) {
            break;
        }
        top = card.card.bottom + expanded_height + static_cast<float>(PopupMetrics().card_gap);
        if (top > static_cast<float>(popup_logical_height_)) {
            break;
        }
    }
    return -1;
}

int PopupWindow::HitTestExpandItem(POINT point) const {
    if (items_.empty() || point.y < PopupListTop() ||
        Contains(PopupScrollbarHitRectForSize(popup_logical_width_, popup_logical_height_), point)) {
        return -1;
    }
    const int first_index = FirstVisibleItemIndex();
    float top = PopupListTop() - (scroll_offset_ - ItemScrollTop(first_index));
    for (int item_index = first_index; item_index < static_cast<int>(items_.size()); ++item_index) {
        const auto& item = items_[item_index];
        auto card = BuildPopupCardLayout(false, top);
        const float width_delta = static_cast<float>(popup_logical_width_ - PopupMetrics().width);
        card.card.right += width_delta;
        card.expand.left += width_delta;
        card.expand.right += width_delta;
        if (Contains(card.expand, point)) {
            return item_index;
        }
        const bool expanded = expanded_item_id_ && *expanded_item_id_ == item.id;
        if (expanded) {
            break;
        }
        top = card.card.bottom + (expanded ? ExpandedExtraHeightForItem(item) : 0.0f) +
              static_cast<float>(PopupMetrics().card_gap);
        if (top > static_cast<float>(popup_logical_height_)) {
            break;
        }
    }
    return -1;
}

PopupWindow::UiAction PopupWindow::HitTestAction(POINT point) const {
    if (TotalScrollHeight() > ViewportScrollHeight() &&
        Contains(PopupScrollbarHitRectForSize(popup_logical_width_, popup_logical_height_), point)) {
        return UiAction::Scrollbar;
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
    const auto tabs = BuildPopupTabsLayoutForWidth(view_mode_ == ViewMode::Favorites, popup_logical_width_);
    if (Contains(tabs.history, point)) return UiAction::HistoryTab;
    if (Contains(tabs.favorites, point)) return UiAction::FavoritesTab;
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
    const auto& item = items_[item_index];
    prompt_open_ = true;
    BeginTransientHideSuppression();
    HMENU menu = CreatePopupMenu();
    HMENU group_menu = CreatePopupMenu();
    for (size_t i = 0; i < favorite_groups_.size() && i < 80; ++i) {
        AppendMenuW(group_menu, MF_STRING, kContextFavoriteGroupBase + 1 + static_cast<UINT>(i),
                    favorite_groups_[i].name.c_str());
    }
    AppendMenuW(group_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(group_menu, MF_STRING, kContextFavoriteGroupMax, L"\u65b0\u5efa\u6536\u85cf\u5939...");
    AppendMenuW(menu, MF_STRING, kContextDelete, L"删除");
    const auto pin_label = PopupPinMenuLabel(item.is_pinned);
    const auto favorite_label = PopupFavoriteMenuLabel(item.is_favorite);
    AppendMenuW(menu, MF_STRING, kContextPin, std::wstring(pin_label).c_str());
    AppendMenuW(menu, MF_STRING, kContextFavorite, std::wstring(favorite_label).c_str());
    AppendMenuW(menu, MF_STRING, kContextSetNote, L"设置备注");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(group_menu), L"\u52a0\u5165\u6536\u85cf\u5939");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, item_index > 0 ? MF_STRING : MF_STRING | MF_GRAYED, kContextMoveUp, L"\u4e0a\u79fb");
    AppendMenuW(menu, item_index + 1 < static_cast<int>(items_.size()) ? MF_STRING : MF_STRING | MF_GRAYED,
                kContextMoveDown, L"\u4e0b\u79fb");
    ClientToScreen(hwnd_, &point);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    BeginTransientHideSuppression();
    const auto id = item.id;
    if (command == kContextDelete) DeleteItem(id);
    if (command == kContextPin) TogglePinned(id);
    if (command == kContextFavorite) ToggleFavorite(id);
    if (command == kContextSetNote) PromptEditNote(id);
    if (command == kContextMoveUp) MoveItemInCurrentList(item_index, -1);
    if (command == kContextMoveDown) MoveItemInCurrentList(item_index, 1);
    if (command > kContextFavoriteGroupBase && command < kContextFavoriteGroupMax) {
        const size_t index = static_cast<size_t>(command - kContextFavoriteGroupBase - 1);
        if (index < favorite_groups_.size()) {
            store_.SetFavoriteGroup(id, favorite_groups_[index].id);
            if (view_mode_ == ViewMode::Favorites) {
                active_favorite_group_id_ = favorite_groups_[index].id;
            }
            ReloadItems();
            ResizeToCurrentItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    if (command == kContextFavoriteGroupMax) {
        prompt_open_ = true;
        BeginTransientHideSuppression();
        if (const auto name = PromptText(hwnd_, instance_, L"\u65b0\u5efa\u6536\u85cf\u5939", L"\u8f93\u5165\u5206\u7ec4\u540d\u79f0")) {
            const auto group_id = store_.EnsureFavoriteGroup(*name);
            store_.SetFavoriteGroup(id, group_id);
            active_favorite_group_id_ = group_id;
            view_mode_ = ViewMode::Favorites;
            ReloadItems();
            ResizeToCurrentItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        prompt_open_ = false;
    }
    prompt_open_ = false;
    EndTransientHideSuppressionSoon();
    SetTimer(hwnd_, kOutsideClickTimer, 50, nullptr);
}

LRESULT PopupWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd_, &ps);
        Paint();
        EndPaint(hwnd_, &ps);
        if (PopupShouldRedrawNativeSearchAfterParentPaint(search_edit_ != nullptr, moving_window_ || resizing_window_)) {
            RedrawWindow(search_edit_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        }
        return 0;
    }
    case WM_SIZE: {
        const RECT logical_before{0, 0, popup_logical_width_, popup_logical_height_};
        UpdatePopupLogicalSize();
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
            SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
            SetBkColor(reinterpret_cast<HDC>(wparam), palette.dark ? RGB(34, 48, 68) : RGB(255, 255, 255));
            const COLORREF hidden_text = palette.dark ? RGB(34, 48, 68) : RGB(255, 255, 255);
            SetTextColor(reinterpret_cast<HDC>(wparam), hidden_text);
            if (!search_edit_brush_) {
                search_edit_brush_ = CreateSolidBrush(palette.dark ? RGB(34, 48, 68) : RGB(255, 255, 255));
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
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
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
            const int resize_edges = ResizeHitTest(logical_point);
            if ((resize_edges & kPopupResizeLeft && resize_edges & kPopupResizeTop) ||
                (resize_edges & kPopupResizeRight && resize_edges & kPopupResizeBottom)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZENWSE));
                return TRUE;
            }
            if ((resize_edges & kPopupResizeRight && resize_edges & kPopupResizeTop) ||
                (resize_edges & kPopupResizeLeft && resize_edges & kPopupResizeBottom)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZENESW));
                return TRUE;
            }
            if (resize_edges & (kPopupResizeLeft | kPopupResizeRight)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            if (resize_edges & (kPopupResizeTop | kPopupResizeBottom)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                return TRUE;
            }
            if (Contains(BuildPopupSearchLayoutForWidth(popup_logical_width_).box, logical_point)) {
                const auto search_layout = BuildPopupSearchLayoutForWidth(popup_logical_width_);
                if (PopupSearchClearButtonHitTest(search_layout, !query_.empty(), logical_point)) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                } else {
                    SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                }
                return TRUE;
            }
        }
        break;
    case WM_TIMER:
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
            search_caret_on_ = true;
            SetTimer(hwnd_, kSearchCaretTimer, GetCaretBlinkTime(), nullptr);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (LOWORD(wparam) == kSearchEditId && HIWORD(wparam) == EN_KILLFOCUS && search_edit_) {
            search_focused_ = false;
            search_caret_on_ = false;
            KillTimer(hwnd_, kSearchCaretTimer);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (LOWORD(wparam) == kSearchEditId && HIWORD(wparam) == EN_CHANGE && search_edit_) {
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
                ReloadItems();
                ResizeToCurrentItems();
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
        if (moving_window_ || resizing_window_) {
            UpdateWindowMoveOrResize();
            return 0;
        }
        if (pressed_item_index_ >= 0) {
            constexpr int kDragCancelDistance = 6;
            const bool moved_past_cancel_distance = std::abs(point.x - press_point_.x) > kDragCancelDistance ||
                                                    std::abs(point.y - press_point_.y) > kDragCancelDistance;
            const int hit_item = HitTestItem(point);
            switch (PopupItemPressMoveActionFor(long_press_selected_, moved_past_cancel_distance, hit_item)) {
            case PopupItemPressMoveAction::CancelPress:
                CancelItemPress();
                break;
            case PopupItemPressMoveAction::SelectHitItem:
                if (const auto selection = PopupSelectionIndexWhileLongPressing(long_press_selected_, hit_item)) {
                    if (*selection != selected_index_) {
                        selected_index_ = *selection;
                        scroll_offset_ = ScrollOffsetToRevealSelection(scroll_offset_, selected_index_);
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
        if (Contains(BuildPopupSearchLayoutForWidth(popup_logical_width_).box, point)) {
            const auto search_layout = BuildPopupSearchLayoutForWidth(popup_logical_width_);
            if (PopupSearchClearButtonHitTest(search_layout, !query_.empty(), point)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
            } else {
                SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
            }
        }
        if (!tracking_mouse_) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&track);
            tracking_mouse_ = true;
        }
        const auto delete_confirm_target = pending_favorite_group_delete_
                                               ? HitTestFavoriteGroupDeleteConfirm(point)
                                               : PopupFavoriteGroupDeleteConfirmTarget::None;
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
        const auto favorite_menu_layout = BuildPopupFavoriteGroupMenuLayout(favorite_groups_.size());
        const auto favorite_menu_hit = favorite_group_menu_open_
                                           ? HitTestPopupFavoriteGroupMenu(favorite_menu_layout, favorite_groups_.size(),
                                                                          static_cast<float>(point.x),
                                                                          static_cast<float>(point.y))
                                           : PopupFavoriteGroupMenuHit{};
        const bool popover_hovered = delete_confirm_target != PopupFavoriteGroupDeleteConfirmTarget::None ||
                                     filter_target != PopupFilterTarget::None ||
                                     favorite_menu_hit.target != PopupFavoriteGroupMenuTarget::None;
        const UiAction action = popover_hovered ? UiAction::None : HitTestAction(point);
        const int item = PopupHoverItemIndex(filter_open_ || favorite_group_menu_open_, HitTestItem(point));
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
                if (PopupShouldAnimateHoverWhileResizing(resizing_window_)) {
                    hover_progress_ = 0.0f;
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
        if (wparam == VK_ESCAPE) {
            if (pending_favorite_group_delete_) {
                pending_favorite_group_delete_.reset();
                hover_delete_confirm_target_ = PopupFavoriteGroupDeleteConfirmTarget::None;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            if (filter_open_ || favorite_group_menu_open_) {
                filter_open_ = false;
                favorite_group_menu_open_ = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
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
        if (pending_favorite_group_delete_) {
            return 0;
        }
        const float next_offset = PopupScrollOffsetAfterWheelForHeight(static_cast<int>(items_.size()), scroll_offset_,
                                                                       GET_WHEEL_DELTA_WPARAM(wparam),
                                                                       popup_logical_height_);
        if (next_offset != scroll_offset_) {
            scroll_offset_ = ClampSmoothScrollOffset(next_offset);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        mouse_down_started_inside_popup_ = true;
        left_button_was_down_ = true;
        POINT point = ClientPointToDips(POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
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
                pending_favorite_group_delete_.reset();
                hover_delete_confirm_target_ = PopupFavoriteGroupDeleteConfirmTarget::None;
                InvalidateRect(hwnd_, nullptr, FALSE);
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
            pinned_open_ = !pinned_open_;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case UiAction::Filter:
            filter_open_ = !filter_open_;
            if (filter_open_) {
                favorite_group_menu_open_ = false;
            }
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
                favorite_group_menu_open_ = false;
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
                favorite_group_menu_open_ = false;
                multi_select_ = false;
                selection_.Clear();
                scroll_offset_ = 0;
                ReloadItems();
                ResizeToCurrentItems();
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
    switch (message) {
    case WM_IME_STARTCOMPOSITION: {
        self->composing_ = true;
        self->composition_text_.clear();
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
        InvalidateRect(self->hwnd_, nullptr, FALSE);
        break;
    }
    case WM_IME_ENDCOMPOSITION: {
        self->composing_ = false;
        self->composition_text_.clear();
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
