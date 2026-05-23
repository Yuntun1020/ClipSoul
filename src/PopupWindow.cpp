#include "ClipSoul/PopupWindow.h"

#include "ClipSoul/PasteController.h"
#include "ClipSoul/TextUtil.h"
#include "ClipSoul/Win32Util.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace ClipSoul {
namespace {
constexpr wchar_t kPopupClass[] = L"ClipSoul.PopupWindow";
constexpr UINT_PTR kAnimationTimer = 42;
constexpr int kWidth = 340;
constexpr int kHeight = 560;
constexpr int kMargin = 14;
constexpr int kHeaderHeight = 40;
constexpr int kSearchHeight = 38;
constexpr int kToolbarHeight = 34;
constexpr int kTabHeight = 32;
constexpr int kCardHeight = 72;
constexpr int kCardGap = 8;
constexpr int kContextDelete = 3001;
constexpr int kContextPin = 3002;
constexpr int kContextFavorite = 3003;
constexpr float kFilterX = kMargin;
constexpr float kFilterWidth = 220.0f;
constexpr float kFilterHeight = 172.0f;

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

D2D1_ROUNDED_RECT RoundRect(float left, float top, float right, float bottom, float radius) {
    return D2D1::RoundedRect(Rect(left, top, right, bottom), radius, radius);
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

D2D1_COLOR_F KindColor(ClipboardKind kind) {
    switch (kind) {
    case ClipboardKind::Text:
    case ClipboardKind::Html:
        return D2D1::ColorF(0x2563EB, 0.85f);
    case ClipboardKind::Image:
        return D2D1::ColorF(0x22C55E, 0.85f);
    case ClipboardKind::Files:
        return D2D1::ColorF(0xA855F7, 0.85f);
    case ClipboardKind::Link:
        return D2D1::ColorF(0xF59E0B, 0.9f);
    }
    return D2D1::ColorF(0x64748B, 0.85f);
}

} // namespace

PopupWindow::PopupWindow(HINSTANCE instance, HistoryStore& store, PasteController& paste_controller)
    : instance_(instance),
      store_(store),
      paste_controller_(paste_controller) {}

PopupWindow::~PopupWindow() {
    DiscardDeviceResources();
    ReleasePtr(title_format_);
    ReleasePtr(body_format_);
    ReleasePtr(small_format_);
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

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, kPopupClass, L"ClipSoul",
                            WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, kWidth, kHeight, owner, nullptr,
                            instance_, this);
    if (!hwnd_) {
        return false;
    }
    SetLayeredWindowAttributes(hwnd_, 0, 244, LWA_ALPHA);
    ApplyBackdrop();
    return true;
}

void PopupWindow::Show(HWND target) {
    paste_target_ = target;
    ReloadItems();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.right - kWidth - 32;
    const int y = work.bottom - kHeight - 56;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, kWidth, kHeight, SWP_SHOWWINDOW);
    open_progress_ = 0.0f;
    SetTimer(hwnd_, kAnimationTimer, 16, nullptr);
    SetForegroundWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::Hide() {
    if (!pinned_open_) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void PopupWindow::ReloadItems() {
    items_ = store_.Query(BuildQuery());
    selected_index_ = std::clamp(selected_index_, 0, std::max(0, static_cast<int>(items_.size()) - 1));
}

HistoryQuery PopupWindow::BuildQuery() const {
    HistoryQuery query;
    query.limit = 60;
    query.text = query_;
    query.kinds = filter_kinds_;
    query.favorites_only = view_mode_ == ViewMode::Favorites;
    return query;
}

void PopupWindow::MoveSelection(int delta) {
    if (items_.empty()) return;
    selected_index_ = std::clamp(selected_index_ + delta, 0, static_cast<int>(items_.size()) - 1);
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
        ShowWindow(hwnd_, SW_HIDE);
        paste_controller_.SendPaste(paste_target_);
    }
}

void PopupWindow::ToggleMultiSelect() {
    multi_select_ = !multi_select_;
    selection_.Clear();
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

void PopupWindow::PasteSelected() {
    const auto ids = selection_.SelectedIds();
    for (const auto id : ids) {
        if (auto item = store_.Get(id)) {
            paste_controller_.RestoreToClipboard(*item, hwnd_);
            break;
        }
    }
    ShowWindow(hwnd_, SW_HIDE);
    paste_controller_.SendPaste(paste_target_);
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
    }
    if (!render_target_) {
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        d2d_factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                         D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)),
            &render_target_);
        render_target_->CreateSolidColorBrush(D2D1::ColorF(0x111827, 0.92f), &text_brush_);
        render_target_->CreateSolidColorBrush(D2D1::ColorF(0x64748B, 0.85f), &muted_brush_);
        render_target_->CreateSolidColorBrush(D2D1::ColorF(0x14B8A6, 0.85f), &accent_brush_);
    }
}

void PopupWindow::DiscardDeviceResources() {
    ReleasePtr(text_brush_);
    ReleasePtr(muted_brush_);
    ReleasePtr(accent_brush_);
    ReleasePtr(render_target_);
}

void PopupWindow::Paint() {
    EnsureDeviceResources();
    render_target_->BeginDraw();
    render_target_->Clear(D2D1::ColorF(0xF8FAFC, 0.78f));

    ID2D1SolidColorBrush* brush = nullptr;
    render_target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.74f), &brush);
    render_target_->FillRoundedRectangle(RoundRect(1, 1, kWidth - 1, kHeight - 1, 12), brush);
    brush->SetColor(D2D1::ColorF(0xCBD5E1, 0.55f));
    render_target_->DrawRoundedRectangle(RoundRect(1, 1, kWidth - 1, kHeight - 1, 12), brush, 1.0f);

    render_target_->DrawTextW(L"ClipSoul", 8, title_format_, Rect(kMargin, 12, 160, 36), text_brush_);
    render_target_->DrawTextW(L"Pin", 3, small_format_, Rect(kWidth - 76, 16, kWidth - 42, 34), muted_brush_);
    render_target_->DrawTextW(L"×", 1, title_format_, Rect(kWidth - 36, 10, kWidth - 14, 36), muted_brush_);

    const float search_top = kHeaderHeight + 8.0f;
    brush->SetColor(D2D1::ColorF(0xFFFFFF, 0.88f));
    render_target_->FillRoundedRectangle(RoundRect(kMargin, search_top, kWidth - kMargin, search_top + kSearchHeight, 10), brush);
    brush->SetColor(D2D1::ColorF(0xCBD5E1, 0.7f));
    render_target_->DrawRoundedRectangle(RoundRect(kMargin, search_top, kWidth - kMargin, search_top + kSearchHeight, 10), brush, 1.0f);
    const auto search_text = query_.empty() ? std::wstring(L"搜索历史记录") : query_;
    render_target_->DrawTextW(search_text.c_str(), static_cast<UINT32>(search_text.size()), body_format_,
                              Rect(kMargin + 14, search_top + 10, kWidth - kMargin - 14, search_top + 31),
                              query_.empty() ? muted_brush_ : text_brush_);

    const float toolbar_top = search_top + kSearchHeight + 8.0f;
    auto drawButton = [&](float x, float w, const wchar_t* label, bool active = false) {
        brush->SetColor(active ? D2D1::ColorF(0xCCFBF1, 0.92f) : D2D1::ColorF(0xFFFFFF, 0.62f));
        render_target_->FillRoundedRectangle(RoundRect(x, toolbar_top, x + w, toolbar_top + 26, 8), brush);
        brush->SetColor(active ? D2D1::ColorF(0x14B8A6, 0.7f) : D2D1::ColorF(0xCBD5E1, 0.65f));
        render_target_->DrawRoundedRectangle(RoundRect(x, toolbar_top, x + w, toolbar_top + 26, 8), brush, 1.0f);
        render_target_->DrawTextW(label, static_cast<UINT32>(wcslen(label)), small_format_,
                                  Rect(x + 10, toolbar_top + 6, x + w - 8, toolbar_top + 22), text_brush_);
    };
    drawButton(kMargin, 54, L"筛选", filter_open_);
    if (multi_select_) {
        drawButton(kWidth - 150, 66, L"选中删除");
        drawButton(kWidth - 78, 64, L"选中粘贴");
    } else {
        drawButton(kWidth - 132, 50, L"多选");
        drawButton(kWidth - 76, 62, L"全部清除");
    }

    const float tab_top = toolbar_top + kToolbarHeight;
    drawButton(kMargin, 64, L"历史", view_mode_ == ViewMode::History);
    drawButton(kMargin + 72, 78, L"收藏夹", view_mode_ == ViewMode::Favorites);

    float y = tab_top + kTabHeight + 4.0f;
    for (size_t i = 0; i < items_.size() && y + kCardHeight < kHeight - 28; ++i) {
        const auto& item = items_[i];
        const bool selected = static_cast<int>(i) == selected_index_;
        const bool checked = selection_.IsSelected(item.id);
        brush->SetColor(selected ? D2D1::ColorF(0xECFEFF, 0.9f) : D2D1::ColorF(0xFFFFFF, 0.66f));
        render_target_->FillRoundedRectangle(RoundRect(kMargin, y, kWidth - kMargin, y + kCardHeight, 10), brush);
        brush->SetColor(selected ? D2D1::ColorF(0x14B8A6, 0.85f) : D2D1::ColorF(0xE2E8F0, 0.75f));
        render_target_->DrawRoundedRectangle(RoundRect(kMargin, y, kWidth - kMargin, y + kCardHeight, 10), brush, 1.0f);
        brush->SetColor(KindColor(item.kind));
        render_target_->FillRoundedRectangle(RoundRect(kMargin + 8, y + 12, kMargin + 11, y + kCardHeight - 12, 2), brush);

        float text_left = kMargin + 22;
        if (multi_select_) {
            brush->SetColor(checked ? D2D1::ColorF(0x14B8A6, 0.9f) : D2D1::ColorF(0xFFFFFF, 0.7f));
            render_target_->FillRoundedRectangle(RoundRect(kMargin + 16, y + 27, kMargin + 30, y + 41, 4), brush);
            brush->SetColor(D2D1::ColorF(0x64748B, 0.7f));
            render_target_->DrawRoundedRectangle(RoundRect(kMargin + 16, y + 27, kMargin + 30, y + 41, 4), brush, 1.0f);
            text_left = kMargin + 40;
        }

        const auto title = ItemTitle(item);
        const auto meta = ItemMeta(item);
        render_target_->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), body_format_,
                                  Rect(text_left, y + 13, kWidth - 28, y + 36), text_brush_);
        render_target_->DrawTextW(meta.c_str(), static_cast<UINT32>(meta.size()), small_format_,
                                  Rect(text_left, y + 43, kWidth - 28, y + 62), muted_brush_);
        y += kCardHeight + kCardGap;
    }

    if (items_.empty()) {
        const wchar_t* empty_text = view_mode_ == ViewMode::Favorites ? L"收藏夹暂无内容" : L"暂无历史记录";
        render_target_->DrawTextW(empty_text, static_cast<UINT32>(wcslen(empty_text)), body_format_,
                                  Rect(kMargin, y + 24, kWidth - kMargin, y + 56), muted_brush_);
    }

    if (filter_open_) {
        const float fx = kFilterX;
        const float fy = toolbar_top + 30;
        brush->SetColor(D2D1::ColorF(0xFFFFFF, 0.95f));
        render_target_->FillRoundedRectangle(RoundRect(fx, fy, fx + kFilterWidth, fy + kFilterHeight, 12), brush);
        brush->SetColor(D2D1::ColorF(0xCBD5E1, 0.75f));
        render_target_->DrawRoundedRectangle(RoundRect(fx, fy, fx + kFilterWidth, fy + kFilterHeight, 12), brush, 1.0f);
        render_target_->DrawTextW(L"筛选", 2, body_format_, Rect(fx + 12, fy + 10, fx + 120, fy + 30), text_brush_);
        render_target_->DrawTextW(L"文本  图片  文件  链接", 13, small_format_, Rect(fx + 12, fy + 42, fx + 200, fy + 62), muted_brush_);
        render_target_->DrawTextW(L"开始      结束", 8, small_format_, Rect(fx + 12, fy + 76, fx + 200, fy + 96), muted_brush_);
        render_target_->DrawTextW(L"重置筛选", 4, small_format_, Rect(fx + 12, fy + 134, fx + 120, fy + 154), accent_brush_);
    }

    render_target_->DrawTextW(L"v1", 2, small_format_, Rect(kWidth - 34, kHeight - 24, kWidth - 12, kHeight - 8), muted_brush_);
    ReleasePtr(brush);

    if (render_target_->EndDraw() == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
}

void PopupWindow::ApplyBackdrop() {
    SetModernWindowAttributes(hwnd_);
}

bool PopupWindow::HandleFilterClick(POINT point) {
    if (!filter_open_) {
        return false;
    }

    const int toolbar_y = kHeaderHeight + 8 + kSearchHeight + 8;
    const float fx = kFilterX;
    const float fy = static_cast<float>(toolbar_y) + 30.0f;
    if (point.x < fx || point.x > fx + kFilterWidth || point.y < fy || point.y > fy + kFilterHeight) {
        return false;
    }

    auto toggle_kind = [&](ClipboardKind kind) {
        if (filter_kinds_.contains(kind)) {
            filter_kinds_.erase(kind);
        } else {
            filter_kinds_.insert(kind);
        }
    };

    if (point.y >= fy + 38 && point.y <= fy + 68) {
        if (point.x < fx + 58) {
            toggle_kind(ClipboardKind::Text);
        } else if (point.x < fx + 110) {
            toggle_kind(ClipboardKind::Image);
        } else if (point.x < fx + 166) {
            toggle_kind(ClipboardKind::Files);
        } else {
            toggle_kind(ClipboardKind::Link);
        }
        ReloadItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    if (point.y >= fy + 128 && point.y <= fy + 160) {
        filter_kinds_.clear();
        ReloadItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    return true;
}

int PopupWindow::HitTestItem(POINT point) const {
    const int list_top = kHeaderHeight + 8 + kSearchHeight + 8 + kToolbarHeight + kTabHeight + 4;
    if (point.y < list_top) return -1;
    const int index = (point.y - list_top) / (kCardHeight + kCardGap);
    const int row_y = list_top + index * (kCardHeight + kCardGap);
    if (index >= 0 && index < static_cast<int>(items_.size()) && point.y <= row_y + kCardHeight) {
        return index;
    }
    return -1;
}

PopupWindow::UiAction PopupWindow::HitTestAction(POINT point) const {
    if (point.y < 40 && point.x > kWidth - 42) return UiAction::Close;
    if (point.y < 40 && point.x > kWidth - 76) return UiAction::Pin;
    const int toolbar_y = kHeaderHeight + 8 + kSearchHeight + 8;
    if (point.y >= toolbar_y && point.y <= toolbar_y + 28) {
        if (point.x < 80) return UiAction::Filter;
        if (multi_select_) {
            if (point.x > kWidth - 154 && point.x < kWidth - 84) return UiAction::DeleteSelected;
            if (point.x > kWidth - 82) return UiAction::PasteSelected;
        } else {
            if (point.x > kWidth - 136 && point.x < kWidth - 80) return UiAction::MultiSelect;
            if (point.x > kWidth - 80) return UiAction::ClearAll;
        }
    }
    const int tab_y = toolbar_y + kToolbarHeight;
    if (point.y >= tab_y && point.y <= tab_y + 28) {
        if (point.x < kMargin + 70) return UiAction::HistoryTab;
        if (point.x < kMargin + 156) return UiAction::FavoritesTab;
    }
    return UiAction::None;
}

void PopupWindow::ShowContextMenu(POINT point, int item_index) {
    if (item_index < 0 || item_index >= static_cast<int>(items_.size())) return;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kContextDelete, L"删除");
    AppendMenuW(menu, MF_STRING, kContextPin, L"置顶");
    AppendMenuW(menu, MF_STRING, kContextFavorite, L"收藏");
    ClientToScreen(hwnd_, &point);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    const auto id = items_[item_index].id;
    if (command == kContextDelete) DeleteItem(id);
    if (command == kContextPin) TogglePinned(id);
    if (command == kContextFavorite) ToggleFavorite(id);
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
        DiscardDeviceResources();
        return 0;
    case WM_TIMER:
        if (wparam == kAnimationTimer) {
            open_progress_ = std::min(1.0f, open_progress_ + 0.18f);
            const BYTE alpha = static_cast<BYTE>(220 + 24 * open_progress_);
            SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA);
            if (open_progress_ >= 1.0f) KillTimer(hwnd_, kAnimationTimer);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_CHAR:
        if (wparam == VK_BACK) {
            if (!query_.empty()) query_.pop_back();
        } else if (wparam >= 32) {
            query_.push_back(static_cast<wchar_t>(wparam));
        }
        ReloadItems();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            ShowWindow(hwnd_, SW_HIDE);
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
    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
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
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case UiAction::MultiSelect:
            ToggleMultiSelect();
            return 0;
        case UiAction::ClearAll:
            store_.Clear();
            ReloadItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case UiAction::DeleteSelected:
            for (const auto id : selection_.SelectedIds()) store_.Delete(id);
            selection_.Clear();
            ReloadItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case UiAction::PasteSelected:
            PasteSelected();
            return 0;
        case UiAction::HistoryTab:
            view_mode_ = ViewMode::History;
            ReloadItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case UiAction::FavoritesTab:
            view_mode_ = ViewMode::Favorites;
            ReloadItems();
            InvalidateRect(hwnd_, nullptr, FALSE);
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
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ShowContextMenu(point, HitTestItem(point));
        return 0;
    }
    case WM_KILLFOCUS:
        Hide();
        return 0;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

LRESULT CALLBACK PopupWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    PopupWindow* window = reinterpret_cast<PopupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        window = static_cast<PopupWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    return window ? window->HandleMessage(message, wparam, lparam)
                  : DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace ClipSoul
