#include "popup/PromptDialogs.h"

#include "ClipSoul/PopupLayout.h"
#include "ClipSoul/TextUtil.h"
#include "ClipSoul/Win32Util.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <utility>

namespace ClipSoul::popup_detail {
namespace {

constexpr wchar_t kPromptClass[] = L"ClipSoul.PopupPrompt";
constexpr int kEditId = 4101;
constexpr int kNoteId = 4104;
constexpr int kCloseId = 4105;
constexpr UINT_PTR kEntranceTimer = 63;

COLORREF PromptColor(uint32_t rgb) {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

COLORREF PromptMix(COLORREF from, COLORREF to, float amount) {
    const auto channel = [amount](int a, int b) {
        return static_cast<int>(std::lround(a + (b - a) * amount));
    };
    return RGB(channel(GetRValue(from), GetRValue(to)), channel(GetGValue(from), GetGValue(to)),
               channel(GetBValue(from), GetBValue(to)));
}

bool PromptAnimationsEnabled() {
    BOOL client_animation = TRUE;
    ANIMATIONINFO animation{sizeof(animation), TRUE};
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &client_animation, 0);
    SystemParametersInfoW(SPI_GETANIMATION, sizeof(animation), &animation, 0);
    return client_animation && animation.iMinAnimate;
}

struct PromptLayout {
    RECT title{};
    RECT subtitle{};
    RECT field{};
    RECT label{};
    RECT note{};
    RECT cancel{};
    RECT save{};
    RECT close{};
    int height = 0;
};

struct PromptState {
    HINSTANCE instance = nullptr;
    HWND owner = nullptr;
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    HWND note_edit = nullptr;
    HWND cancel = nullptr;
    HWND save = nullptr;
    HWND close = nullptr;
    HWND hovered = nullptr;
    HWND ime_edit = nullptr;
    UINT dpi = 96;
    int theme_mode = 0;
    bool phrase = false;
    bool group = false;
    bool accepted = false;
    bool entering = false;
    ULONGLONG entrance_start = 0;
    int entrance_top = 0;
    std::wstring title;
    std::wstring subtitle;
    std::wstring initial_text;
    std::wstring text;
    std::wstring note;
    PopupThemePalette palette{};
    PromptLayout layout{};
    HFONT title_font = nullptr;
    HFONT subtitle_font = nullptr;
    HFONT field_font = nullptr;
    HFONT label_font = nullptr;
    HFONT button_font = nullptr;
    HBRUSH paper_brush = nullptr;

    int Px(float dip) const { return static_cast<int>(std::lround(dip * dpi / 96.0f)); }

    void ClearFonts() {
        for (HFONT font : {title_font, subtitle_font, field_font, label_font, button_font}) {
            if (font) DeleteObject(font);
        }
    }

    ~PromptState() {
        ClearFonts();
        if (paper_brush) DeleteObject(paper_brush);
    }
};

void RefreshPromptPalette(PromptState& state) {
    // Explicit light/dark settings take precedence over the system preference.
    state.palette = ResolvePopupThemePalette(state.theme_mode, IsSystemDarkTheme());
    if (state.paper_brush) DeleteObject(state.paper_brush);
    state.paper_brush = CreateSolidBrush(PromptColor(state.palette.window_tint));
    if (state.hwnd) RedrawWindow(state.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

HFONT CreatePromptFont(const PromptState& state, float dip, int weight = FW_NORMAL) {
    return CreateFontW(-state.Px(dip), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void RefreshPromptFonts(PromptState& state) {
    state.ClearFonts();
    state.title_font = CreatePromptFont(state, 14, FW_SEMIBOLD);
    state.subtitle_font = CreatePromptFont(state, 11.5f);
    state.field_font = CreatePromptFont(state, 13);
    state.label_font = CreatePromptFont(state, 11);
    state.button_font = CreatePromptFont(state, 12);
    for (HWND control : {state.edit, state.note_edit}) {
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state.field_font), TRUE);
    }
    for (HWND control : {state.cancel, state.save, state.close}) {
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state.button_font), TRUE);
    }
}

int PromptLineHeight(HDC dc, HFONT font) {
    HGDIOBJ previous = SelectObject(dc, font);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, previous);
    return metrics.tmHeight;
}

PromptLayout MeasurePrompt(const PromptState& state, int width) {
    const auto px = [&state](float dip) { return state.Px(dip); };
    PromptLayout layout;
    const int left = px(16);
    const int right = width - px(16);
    layout.close = {right - px(24), px(16), right, px(40)};
    layout.title = {left, px(16), layout.close.left - px(8), px(40)};
    HDC dc = GetDC(state.hwnd);
    HGDIOBJ previous = SelectObject(dc, state.subtitle_font);
    RECT subtitle{left, 0, right, 0};
    DrawTextW(dc, state.subtitle.c_str(), -1, &subtitle, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    const int subtitle_height = std::max(static_cast<int>(subtitle.bottom), PromptLineHeight(dc, state.subtitle_font));
    layout.subtitle = {left, px(43), right, px(43) + subtitle_height};
    const int line_height = PromptLineHeight(dc, state.field_font);
    const int field_top = layout.subtitle.bottom + px(12);
    layout.field = {left, field_top, right, field_top + line_height * (state.phrase ? 4 : 1) + px(16)};
    int content_bottom = layout.field.bottom;
    if (state.phrase) {
        const int label_top = content_bottom + px(10);
        layout.label = {left, label_top, right, label_top + PromptLineHeight(dc, state.label_font)};
        layout.note = {left, layout.label.bottom + px(5), right,
                       layout.label.bottom + px(5) + line_height + px(16)};
        content_bottom = layout.note.bottom;
    }
    SelectObject(dc, state.button_font);
    SIZE text_size{};
    GetTextExtentPoint32W(dc, L"保存", 2, &text_size);
    const int button_width = text_size.cx + px(28);
    const int buttons_top = content_bottom + px(14);
    layout.save = {right - button_width, buttons_top, right, buttons_top + px(28)};
    layout.cancel = {layout.save.left - px(6) - button_width, buttons_top,
                     layout.save.left - px(6), buttons_top + px(28)};
    layout.height = layout.save.bottom + px(14);
    SelectObject(dc, previous);
    ReleaseDC(state.hwnd, dc);
    return layout;
}

void PositionControl(HWND control, const RECT& rect) {
    if (control) MoveWindow(control, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE);
}

void PositionPromptControls(PromptState& state) {
    const auto position_edit = [&state](HWND edit, RECT rect, bool multiline) {
        if (!edit) return;
        InflateRect(&rect, -state.Px(10), -state.Px(8));
        PositionControl(edit, rect);
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        if (multiline) {
            RECT client{};
            GetClientRect(edit, &client);
            SendMessageW(edit, EM_SETRECTNP, 0, reinterpret_cast<LPARAM>(&client));
        }
    };
    position_edit(state.edit, state.layout.field, state.phrase);
    position_edit(state.note_edit, state.layout.note, false);
    PositionControl(state.cancel, state.layout.cancel);
    PositionControl(state.save, state.layout.save);
    PositionControl(state.close, state.layout.close);
}

void SetPromptRegion(PromptState& state, int width, int height) {
    // RoundRect/region APIs take the ellipse diameter, CSS specifies the radius.
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, state.Px(24), state.Px(24));
    if (region && !SetWindowRgn(state.hwnd, region, TRUE)) DeleteObject(region);
}

void AnchorPrompt(PromptState& state) {
    RECT owner_client{};
    GetClientRect(state.owner, &owner_client);
    POINT origin{};
    ClientToScreen(state.owner, &origin);
    const int width = std::max(1, static_cast<int>(owner_client.right - owner_client.left) - 2 * state.Px(20));
    state.layout = MeasurePrompt(state, width);
    state.entrance_top = origin.y + state.Px(104);
    SetWindowPos(state.hwnd, nullptr, origin.x + state.Px(20), state.entrance_top, width,
                 state.layout.height, SWP_NOZORDER | SWP_NOACTIVATE);
    SetPromptRegion(state, width, state.layout.height);
    PositionPromptControls(state);
}

void FinishPromptEntrance(PromptState& state) {
    KillTimer(state.hwnd, kEntranceTimer);
    if (!state.entering) return;
    state.entering = false;
    RECT rect{};
    GetWindowRect(state.hwnd, &rect);
    SetWindowPos(state.hwnd, nullptr, rect.left, state.entrance_top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetLayeredWindowAttributes(state.hwnd, 0, 255, LWA_ALPHA);
}

void ShowPrompt(PromptState& state) {
    if (PromptAnimationsEnabled()) {
        state.entering = true;
        state.entrance_start = GetTickCount64();
        RECT rect{};
        GetWindowRect(state.hwnd, &rect);
        SetWindowPos(state.hwnd, nullptr, rect.left, state.entrance_top + state.Px(10), 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetLayeredWindowAttributes(state.hwnd, 0, 0, LWA_ALPHA);
        if (!SetTimer(state.hwnd, kEntranceTimer, 16, nullptr)) FinishPromptEntrance(state);
    }
    ShowWindow(state.hwnd, SW_SHOW);
    SetFocus(state.edit);
    SendMessageW(state.edit, EM_SETSEL, 0, -1);
    UpdateWindow(state.hwnd);
}

void AdvancePromptEntrance(PromptState& state) {
    if (!PromptAnimationsEnabled()) {
        FinishPromptEntrance(state);
        return;
    }
    const float elapsed = static_cast<float>(GetTickCount64() - state.entrance_start);
    const float progress = std::clamp(elapsed / 200.0f, 0.0f, 1.0f);
    const float eased = 1.0f - std::pow(1.0f - progress, 3.0f);
    RECT rect{};
    GetWindowRect(state.hwnd, &rect);
    SetWindowPos(state.hwnd, nullptr, rect.left,
                 state.entrance_top + static_cast<int>(std::lround(state.Px(10) * (1.0f - eased))),
                 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetLayeredWindowAttributes(state.hwnd, 0,
                               static_cast<BYTE>(255 * std::min(1.0f, elapsed / 130.0f)), LWA_ALPHA);
    if (progress >= 1.0f) FinishPromptEntrance(state);
}

void DrawRoundedPanel(HDC dc, const RECT& rect, int radius, int stroke_width, COLORREF fill, COLORREF stroke) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, stroke_width, stroke);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawPromptText(HDC dc, HFONT font, COLORREF color, RECT rect, const std::wstring& text, UINT flags) {
    HGDIOBJ previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, previous);
}

void PaintPrompt(PromptState& state) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(state.hwnd, &ps);
    RECT client{};
    GetClientRect(state.hwnd, &client);
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ previous = SelectObject(memory, bitmap);
    const auto& palette = state.palette;
    HBRUSH surface = CreateSolidBrush(PromptColor(palette.panel_fill));
    FillRect(memory, &client, surface);
    DeleteObject(surface);
    RECT panel{0, 0, client.right, client.bottom};
    DrawRoundedPanel(memory, panel, state.Px(12), std::max(1, state.Px(1)),
                     PromptColor(palette.panel_fill), PromptColor(palette.border));
    const auto draw_field = [&](const RECT& rect, HWND edit) {
        const bool focused = GetFocus() == edit;
        DrawRoundedPanel(memory, rect, state.Px(7), std::max(1, state.Px(focused ? 1.2f : 1.0f)),
                         PromptColor(palette.window_tint),
                         PromptColor(focused ? palette.text : palette.strong_border));
    };
    draw_field(state.layout.field, state.edit);
    if (state.phrase) {
        draw_field(state.layout.note, state.note_edit);
        DrawPromptText(memory, state.label_font, PromptColor(palette.text_tertiary), state.layout.label,
                       L"备注", DT_SINGLELINE | DT_VCENTER);
    }
    DrawPromptText(memory, state.title_font, PromptColor(palette.text), state.layout.title,
                   state.title, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    DrawPromptText(memory, state.subtitle_font, PromptColor(palette.text_tertiary), state.layout.subtitle,
                   state.subtitle, DT_WORDBREAK);
    BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(state.hwnd, &ps);
}

void DrawPromptButton(const PromptState& state, const DRAWITEMSTRUCT& item) {
    const auto& palette = state.palette;
    const bool hovered = state.hovered == item.hwndItem || (item.itemState & ODS_SELECTED);
    const bool close = item.CtlID == kCloseId;
    const bool save = item.CtlID == IDOK;
    COLORREF fill = PromptColor(palette.panel_fill);
    COLORREF text = PromptColor(palette.muted);
    COLORREF border = PromptColor(palette.strong_border);
    HBRUSH surface = CreateSolidBrush(fill);
    FillRect(item.hDC, &item.rcItem, surface);
    DeleteObject(surface);
    if (close) {
        text = PromptColor(hovered ? palette.danger : palette.text_tertiary);
        if (hovered) {
            fill = PromptMix(fill, PromptColor(palette.danger), palette.dark ? 0.15f : 0.10f);
            DrawRoundedPanel(item.hDC, item.rcItem, state.Px(5), std::max(1, state.Px(1)), fill, fill);
        }
        HPEN pen = CreatePen(PS_SOLID, std::max(1, state.Px(1)), text);
        HGDIOBJ previous = SelectObject(item.hDC, pen);
        const int cx = (item.rcItem.left + item.rcItem.right) / 2;
        const int cy = (item.rcItem.top + item.rcItem.bottom) / 2;
        const int half = state.Px(3.5f);
        MoveToEx(item.hDC, cx - half, cy - half, nullptr);
        LineTo(item.hDC, cx + half, cy + half);
        MoveToEx(item.hDC, cx + half, cy - half, nullptr);
        LineTo(item.hDC, cx - half, cy + half);
        SelectObject(item.hDC, previous);
        DeleteObject(pen);
    } else {
        if (save) {
            fill = PromptColor(hovered ? palette.accent_hover : palette.accent);
            border = fill;
            text = PromptColor(palette.window_tint);
        } else if (hovered) {
            fill = PromptColor(palette.paper_hover);
            text = PromptColor(palette.text);
        }
        DrawRoundedPanel(item.hDC, item.rcItem, state.Px(7), std::max(1, state.Px(1)), fill, border);
        DrawPromptText(item.hDC, state.button_font, text, item.rcItem,
                       save ? (state.group ? L"创建" : L"保存") : L"取消", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -state.Px(3), -state.Px(3));
        DrawFocusRect(item.hDC, &focus);
    }
}

std::wstring ReadPromptEdit(HWND edit) {
    const int length = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(edit, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void AcceptPrompt(PromptState& state) {
    state.text = state.phrase ? NormalizeEditableNote(ReadPromptEdit(state.edit))
                              : NormalizeWhitespace(ReadPromptEdit(state.edit));
    // An empty note is a successful edit; a group/phrase must contain text.
    if (state.text.empty() && (state.phrase || state.group)) {
        SetFocus(state.edit);
        return;
    }
    if (state.phrase) state.note = NormalizeWhitespace(ReadPromptEdit(state.note_edit));
    state.accepted = true;
    DestroyWindow(state.hwnd);
}

LRESULT CALLBACK PromptControlProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                   UINT_PTR subclass_id, DWORD_PTR reference) {
    auto& state = *reinterpret_cast<PromptState*>(reference);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, PromptControlProc, subclass_id);
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    const bool edit = hwnd == state.edit || hwnd == state.note_edit;
    if (edit) {
        if (message == WM_IME_STARTCOMPOSITION) state.ime_edit = hwnd;
        if (message == WM_IME_ENDCOMPOSITION) state.ime_edit = nullptr;
        if (message == WM_PAINT) {
            const LRESULT result = DefSubclassProc(hwnd, message, wparam, lparam);
            if (GetWindowTextLengthW(hwnd) == 0 && state.ime_edit != hwnd) {
                RECT client{};
                GetClientRect(hwnd, &client);
                const wchar_t* placeholder = hwnd == state.note_edit ? L"可选" : state.phrase ? L"输入常用语内容"
                                                             : state.group ? L"例如：工作" : L"例如：公司地址";
                HDC dc = GetDC(hwnd);
                DrawPromptText(dc, state.field_font, PromptColor(state.palette.text_tertiary), client,
                               placeholder, DT_SINGLELINE | DT_TOP);
                ReleaseDC(hwnd, dc);
            }
            return result;
        }
    } else {
        if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        if (message == WM_MOUSEMOVE && state.hovered != hwnd) {
            if (state.hovered) InvalidateRect(state.hovered, nullptr, FALSE);
            state.hovered = hwnd;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (message == WM_MOUSELEAVE && state.hovered == hwnd) {
            state.hovered = nullptr;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

HWND CreatePromptControl(PromptState& state, const wchar_t* type, const wchar_t* text, DWORD style, int id) {
    HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                                    0, 0, 1, 1, state.hwnd,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), state.instance, nullptr);
    if (control) SetWindowSubclass(control, PromptControlProc, 1, reinterpret_cast<DWORD_PTR>(&state));
    return control;
}

LRESULT CALLBACK PromptProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        state = static_cast<PromptState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
    case WM_CREATE: {
        const std::wstring initial = NormalizeWhitespace(state->initial_text);
        const DWORD edit_style = state->phrase ? ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN : ES_AUTOHSCROLL;
        state->edit = CreatePromptControl(*state, L"EDIT", initial.c_str(), edit_style, kEditId);
        if (state->phrase) state->note_edit = CreatePromptControl(*state, L"EDIT", L"", ES_AUTOHSCROLL, kNoteId);
        state->cancel = CreatePromptControl(*state, L"BUTTON", L"取消", BS_OWNERDRAW, IDCANCEL);
        state->save = CreatePromptControl(*state, L"BUTTON", state->group ? L"创建" : L"保存", BS_OWNERDRAW, IDOK);
        state->close = CreatePromptControl(*state, L"BUTTON", L"关闭", BS_OWNERDRAW, kCloseId);
        if (!state->edit || (state->phrase && !state->note_edit) || !state->cancel || !state->save || !state->close) {
            return -1;
        }
        RefreshPromptFonts(*state);
        AnchorPrompt(*state);
        return 0;
    }
    case WM_PAINT:
        PaintPrompt(*state);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), PromptColor(state->palette.text));
        SetBkColor(reinterpret_cast<HDC>(wparam), PromptColor(state->palette.window_tint));
        return reinterpret_cast<LRESULT>(state->paper_brush);
    case WM_DRAWITEM:
        if (wparam == IDOK || wparam == IDCANCEL || wparam == kCloseId) {
            DrawPromptButton(*state, *reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED) {
            if (LOWORD(wparam) == IDOK) {
                AcceptPrompt(*state);
                return 0;
            }
            if (LOWORD(wparam) == IDCANCEL || LOWORD(wparam) == kCloseId) {
                DestroyWindow(hwnd);
                return 0;
            }
        }
        if (HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS || HIWORD(wparam) == EN_CHANGE) {
            InvalidateRect(hwnd, nullptr, FALSE);
            InvalidateRect(reinterpret_cast<HWND>(lparam), nullptr, FALSE);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wparam == kEntranceTimer) {
            AdvancePromptEntrance(*state);
            return 0;
        }
        break;
    case WM_DPICHANGED:
        FinishPromptEntrance(*state);
        state->dpi = HIWORD(wparam);
        if (!state->title_font) return 0;
        RefreshPromptFonts(*state);
        AnchorPrompt(*state);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        RefreshPromptPalette(*state);
        if (!PromptAnimationsEnabled()) FinishPromptEntrance(*state);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kEntranceTimer);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool RunPrompt(PromptState& state) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = PromptProc;
    wc.hInstance = state.instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kPromptClass;
    wc.style = CS_DROPSHADOW;
    RegisterClassW(&wc);
    const UINT owner_dpi = GetDpiForWindow(state.owner);
    state.dpi = owner_dpi ? owner_dpi : 96;
    RefreshPromptPalette(state);
    POINT origin{};
    ClientToScreen(state.owner, &origin);
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, kPromptClass,
                                state.title.c_str(), WS_POPUP | WS_CLIPCHILDREN,
                                origin.x + state.Px(20), origin.y + state.Px(104), 1, 1,
                                state.owner, nullptr, state.instance, &state);
    if (!hwnd) return false;
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    const BOOL owner_enabled = IsWindowEnabled(state.owner);
    EnableWindow(state.owner, FALSE);
    ShowPrompt(state);
    MSG message{};
    int result = 0;
    while (IsWindow(hwnd) && (result = static_cast<int>(GetMessageW(&message, nullptr, 0, 0))) > 0) {
        const bool prompt_message = message.hwnd == hwnd || IsChild(hwnd, message.hwnd);
        if (prompt_message && message.message == WM_KEYDOWN && !state.ime_edit) {
            if (message.wParam == VK_ESCAPE) {
                DestroyWindow(hwnd);
                continue;
            }
            if (message.wParam == VK_RETURN && !(state.phrase && message.hwnd == state.edit)) {
                // Buttons keep their own Enter action, including Cancel and Close.
                if (message.hwnd == state.cancel || message.hwnd == state.close) DestroyWindow(hwnd);
                else AcceptPrompt(state);
                continue;
            }
        }
        // Let the edit/IME consume composition keys, even Escape/Enter.
        if ((state.ime_edit && message.hwnd == state.ime_edit) || !IsDialogMessageW(hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    if (IsWindow(state.owner)) {
        EnableWindow(state.owner, owner_enabled);
        if (owner_enabled) {
            SetForegroundWindow(state.owner);
            SetFocus(state.owner);
        }
    }
    if (result == 0 && message.message == WM_QUIT) PostQuitMessage(static_cast<int>(message.wParam));
    return state.accepted;
}

} // namespace

std::optional<FavoritePhraseInput> PromptFavoritePhrase(HWND owner, HINSTANCE instance, int theme_mode) {
    PromptState state;
    state.owner = owner;
    state.instance = instance;
    state.theme_mode = theme_mode;
    state.phrase = true;
    state.title = L"添加常用语";
    state.subtitle = L"保存后会出现在收藏中";
    if (RunPrompt(state)) return FavoritePhraseInput{state.text, state.note};
    return std::nullopt;
}

std::optional<std::wstring> PromptText(HWND owner, HINSTANCE instance, std::wstring title,
                                       std::wstring subtitle, std::wstring initial_text, int theme_mode) {
    PromptState state;
    state.owner = owner;
    state.instance = instance;
    state.theme_mode = theme_mode;
    state.group = title == L"新建收藏夹";
    state.title = std::move(title);
    state.subtitle = state.title == L"编辑备注" ? L"备注会显示在内容下方，也可以被搜索到" : std::move(subtitle);
    state.initial_text = std::move(initial_text);
    if (RunPrompt(state)) return state.text;
    return std::nullopt;
}

} // namespace ClipSoul::popup_detail
