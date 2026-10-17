#include "popup/PromptDialogs.h"

#include "ClipSoul/PopupLayout.h"
#include "ClipSoul/TextUtil.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace ClipSoul::popup_detail {
namespace {

constexpr int kPhraseEditId = 4101;
constexpr int kPhraseSaveId = 4102;
constexpr int kPhraseCancelId = 4103;
constexpr int kPhraseNoteEditId = 4104;
constexpr wchar_t kPhrasePromptClass[] = L"ClipSoul.FavoritePhrasePrompt";
constexpr int kTextPromptEditId = 4201;
constexpr wchar_t kTextPromptClass[] = L"ClipSoul.TextPrompt";
constexpr UINT_PTR kPhraseHoverTimer = 62;
constexpr UINT_PTR kPromptEntranceTimer = 63;
constexpr DWORD kChildWindowEntranceMs = 120;
constexpr wchar_t kPromptEntranceTopProp[] = L"ClipSoul.PromptEntranceTop";
constexpr wchar_t kPromptEntranceStepProp[] = L"ClipSoul.PromptEntranceStep";

void ShowChildWindowWithEntrance(HWND hwnd) {
    if (PopupPromptShouldUseSlideAnimation() &&
        AnimateWindow(hwnd, kChildWindowEntranceMs, AW_ACTIVATE | AW_SLIDE | AW_VER_POSITIVE)) {
        return;
    }
    if (PopupPromptShouldUseBlendAnimation() && AnimateWindow(hwnd, kChildWindowEntranceMs, AW_ACTIVATE | AW_BLEND)) {
        return;
    }
    if (PopupPromptShouldUsePositionNudgeAnimation()) {
        RECT rect{};
        if (GetWindowRect(hwnd, &rect)) {
            const int offset = PopupPromptEntranceOffsetPixels();
            SetWindowPos(hwnd, nullptr, rect.left, rect.top + offset, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);
            SetPropW(hwnd, kPromptEntranceTopProp, IntToPtr(rect.top));
            SetPropW(hwnd, kPromptEntranceStepProp, IntToPtr(0));
            SetTimer(hwnd, kPromptEntranceTimer, PopupPromptEntranceTimerIntervalMs(), nullptr);
            return;
        }
    }
    ShowWindow(hwnd, SW_SHOW);
}

bool AdvancePromptEntranceAnimation(HWND hwnd) {
    auto top_handle = GetPropW(hwnd, kPromptEntranceTopProp);
    if (!top_handle) {
        KillTimer(hwnd, kPromptEntranceTimer);
        return false;
    }
    const int top = PtrToInt(top_handle);
    const int step = PtrToInt(GetPropW(hwnd, kPromptEntranceStepProp)) + 1;
    const int steps = std::max(1, PopupPromptEntranceStepCount());
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        KillTimer(hwnd, kPromptEntranceTimer);
        RemovePropW(hwnd, kPromptEntranceTopProp);
        RemovePropW(hwnd, kPromptEntranceStepProp);
        return true;
    }
    const float progress = PopupMotionProgress(static_cast<float>(std::min(step, steps)) / static_cast<float>(steps));
    const int offset = PopupPromptEntranceOffsetPixels();
    const int y = top + static_cast<int>(std::lround(static_cast<float>(offset) * (1.0f - progress)));
    SetWindowPos(hwnd, nullptr, rect.left, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (step >= steps) {
        KillTimer(hwnd, kPromptEntranceTimer);
        RemovePropW(hwnd, kPromptEntranceTopProp);
        RemovePropW(hwnd, kPromptEntranceStepProp);
        SetWindowPos(hwnd, nullptr, rect.left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        SetPropW(hwnd, kPromptEntranceStepProp, IntToPtr(step));
    }
    return true;
}

void CloseChildWindowWithExit(HWND hwnd) {
    KillTimer(hwnd, kPromptEntranceTimer);
    RemovePropW(hwnd, kPromptEntranceTopProp);
    RemovePropW(hwnd, kPromptEntranceStepProp);
    DestroyWindow(hwnd);
}

void SetPromptCursorForTarget(int target) {
    SetCursor(LoadCursorW(nullptr, target == 0 ? IDC_ARROW : IDC_HAND));
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

COLORREF PromptCanvasFill() {
    return RGB(246, 241, 232);
}

COLORREF PromptSurfaceFill() {
    return RGB(251, 248, 242);
}

COLORREF PromptHoverFill() {
    return RGB(240, 231, 217);
}

COLORREF PromptBorderColor() {
    return RGB(216, 208, 197);
}

COLORREF PromptStrongBorderColor() {
    return RGB(185, 169, 149);
}

COLORREF PromptTextColor() {
    return RGB(33, 30, 25);
}

COLORREF PromptMutedColor() {
    return RGB(105, 100, 91);
}

COLORREF PromptPrimaryColor(bool hovered) {
    return hovered ? RGB(102, 114, 95) : RGB(115, 130, 107);
}

COLORREF PromptDangerColor() {
    return RGB(169, 81, 72);
}

COLORREF PromptDangerFill() {
    return RGB(248, 229, 224);
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
        FillGdiSolid(mem_dc, rc, PromptCanvasFill());
        RECT panel_rect{0, 0, rc.right - 1, rc.bottom - 1};
        DrawGdiRoundedPanel(mem_dc, panel_rect, 18, PromptCanvasFill(), PromptBorderColor());
        RECT input_rect{22, 78, 298, 144};
        DrawGdiRoundedPanel(mem_dc, input_rect, 12, PromptSurfaceFill(), PromptBorderColor());

        const bool save_hover = state->hover_target == 2 && state->hover_progress > 0.0f;
        RECT save_button{146, 158, 216, 186};
        DrawGdiRoundedPanel(mem_dc, save_button, 12, PromptPrimaryColor(save_hover),
                            PromptPrimaryColor(save_hover));
        const bool cancel_hover = state->hover_target == 3 && state->hover_progress > 0.0f;
        RECT cancel_button{228, 158, 298, 186};
        DrawGdiRoundedPanel(mem_dc, cancel_button, 12,
                            cancel_hover ? PromptHoverFill() : PromptSurfaceFill(),
                            cancel_hover ? PromptStrongBorderColor() : PromptBorderColor());

        SetBkMode(mem_dc, TRANSPARENT);
        auto font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HGDIOBJ old_font = SelectObject(mem_dc, font);
        SetTextColor(mem_dc, PromptTextColor());
        TextOutW(mem_dc, 22, 18, state->title.c_str(), static_cast<int>(state->title.size()));
        if (state->hover_target == 1 && state->hover_progress > 0.0f) {
            RECT close_rect{286, 12, 310, 36};
            DrawGdiRoundedPanel(mem_dc, close_rect, 8, PromptDangerFill(), PromptDangerColor());
        }
        HPEN close_pen = CreatePen(PS_SOLID, 2,
                                   state->hover_target == 1 ? PromptDangerColor() : PromptMutedColor());
        HGDIOBJ old_pen = SelectObject(mem_dc, close_pen);
        MoveToEx(mem_dc, 294, 20, nullptr);
        LineTo(mem_dc, 302, 28);
        MoveToEx(mem_dc, 302, 20, nullptr);
        LineTo(mem_dc, 294, 28);
        SelectObject(mem_dc, old_pen);
        DeleteObject(close_pen);
        SetTextColor(mem_dc, PromptMutedColor());
        TextOutW(mem_dc, 22, 48, state->subtitle.c_str(), static_cast<int>(state->subtitle.size()));
        SetTextColor(mem_dc, PromptSurfaceFill());
        DrawCenteredText(mem_dc, save_button, L"保存");
        SetTextColor(mem_dc, PromptTextColor());
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
            CloseChildWindowWithExit(hwnd);
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
            CloseChildWindowWithExit(hwnd);
            return 0;
        }
        break;
    }
    case WM_TIMER:
        if (wparam == kPromptEntranceTimer) {
            AdvancePromptEntranceAnimation(hwnd);
            return 0;
        }
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
        SetPromptCursorForTarget(target);
        if (target != state->hover_target) {
            state->hover_target = target;
            state->hover_progress = 0.0f;
            SetTimer(hwnd, kPhraseHoverTimer, 16, nullptr);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            POINT cursor{};
            GetCursorPos(&cursor);
            ScreenToClient(hwnd, &cursor);
            SetPromptCursorForTarget(TextPromptHitTarget(cursor.x, cursor.y));
            return TRUE;
        }
        break;
    case WM_MOUSELEAVE:
        state->tracking_mouse = false;
        state->hover_target = 0;
        state->hover_progress = 0.0f;
        KillTimer(hwnd, kPhraseHoverTimer);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        CloseChildWindowWithExit(hwnd);
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
        FillGdiSolid(mem_dc, rc, PromptCanvasFill());

        RECT panel_rect{0, 0, rc.right - 1, rc.bottom - 1};
        DrawGdiRoundedPanel(mem_dc, panel_rect, 18, PromptCanvasFill(), PromptBorderColor());
        RECT input_rect{22, 74, 298, 150};
        DrawGdiRoundedPanel(mem_dc, input_rect, 12, PromptSurfaceFill(), PromptBorderColor());
        RECT note_rect{22, 172, 298, 222};
        DrawGdiRoundedPanel(mem_dc, note_rect, 12, PromptSurfaceFill(), PromptBorderColor());

        const bool save_hover = state->hover_target == 2 && state->hover_progress > 0.0f;
        RECT save_button{146, 236, 216, 264};
        DrawGdiRoundedPanel(mem_dc, save_button, 12, PromptPrimaryColor(save_hover),
                            PromptPrimaryColor(save_hover));

        const bool cancel_hover = state->hover_target == 3 && state->hover_progress > 0.0f;
        RECT cancel_button{228, 236, 298, 264};
        DrawGdiRoundedPanel(mem_dc, cancel_button, 12,
                            cancel_hover ? PromptHoverFill() : PromptSurfaceFill(),
                            cancel_hover ? PromptStrongBorderColor() : PromptBorderColor());

        SetBkMode(mem_dc, TRANSPARENT);
        auto font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HGDIOBJ old_font = SelectObject(mem_dc, font);
        SetTextColor(mem_dc, PromptTextColor());
        TextOutW(mem_dc, 22, 18, L"\u6dfb\u52a0\u5e38\u7528\u8bed", 5);
        if (state->hover_target == 1 && state->hover_progress > 0.0f) {
            RECT close_rect{286, 12, 310, 36};
            DrawGdiRoundedPanel(mem_dc, close_rect, 8, PromptDangerFill(), PromptDangerColor());
        }
        HPEN close_pen = CreatePen(PS_SOLID, 2,
                                   state->hover_target == 1 ? PromptDangerColor() : PromptMutedColor());
        HGDIOBJ old_pen = SelectObject(mem_dc, close_pen);
        MoveToEx(mem_dc, 294, 20, nullptr);
        LineTo(mem_dc, 302, 28);
        MoveToEx(mem_dc, 302, 20, nullptr);
        LineTo(mem_dc, 294, 28);
        SelectObject(mem_dc, old_pen);
        DeleteObject(close_pen);
        SetTextColor(mem_dc, PromptMutedColor());
        TextOutW(mem_dc, 22, 48, L"保存后会出现在收藏夹", 10);
        TextOutW(mem_dc, 22, 154, L"备注", 2);
        SetTextColor(mem_dc, PromptSurfaceFill());
        RECT save_rect{146, 236, 216, 264};
        DrawCenteredText(mem_dc, save_rect, L"保存");
        SetTextColor(mem_dc, PromptTextColor());
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
            CloseChildWindowWithExit(hwnd);
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
            CloseChildWindowWithExit(hwnd);
            return 0;
        }
        if (target == 3) {
            CloseChildWindowWithExit(hwnd);
            return 0;
        }
        break;
    }
    case WM_TIMER:
        if (wparam == kPromptEntranceTimer) {
            AdvancePromptEntranceAnimation(hwnd);
            return 0;
        }
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
        SetPromptCursorForTarget(target);
        if (target != state->hover_target) {
            state->hover_target = target;
            state->hover_progress = 0.0f;
            SetTimer(hwnd, kPhraseHoverTimer, 16, nullptr);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            POINT cursor{};
            GetCursorPos(&cursor);
            ScreenToClient(hwnd, &cursor);
            SetPromptCursorForTarget(PhraseHitTarget(cursor.x, cursor.y));
            return TRUE;
        }
        break;
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
        CloseChildWindowWithExit(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace

std::optional<FavoritePhraseInput> PromptFavoritePhrase(HWND owner, HINSTANCE instance) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = PhrasePromptProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
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
    ShowChildWindowWithEntrance(hwnd);
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
                                       std::wstring subtitle, std::wstring initial_text) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = TextPromptProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
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
    ShowChildWindowWithEntrance(hwnd);
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

} // namespace ClipSoul::popup_detail
