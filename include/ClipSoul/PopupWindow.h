#pragma once

#include "ClipSoul/HistoryStore.h"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ClipSoul {

class PasteController;

class PopupWindow {
public:
    PopupWindow(HINSTANCE instance, HistoryStore& store, PasteController& paste_controller);
    ~PopupWindow();

    bool Create(HWND owner);
    void Show(HWND target);
    void Hide();
    HWND hwnd() const { return hwnd_; }
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

private:
    enum class ViewMode { History, Favorites };
    enum class UiAction {
        None,
        Filter,
        MultiSelect,
        ClearAll,
        DeleteSelected,
        PasteSelected,
        HistoryTab,
        FavoritesTab,
        Pin,
        Close,
    };

    void ReloadItems();
    void MoveSelection(int delta);
    void ActivateSelection();
    void ToggleMultiSelect();
    void ToggleFavorite(int64_t id);
    void TogglePinned(int64_t id);
    void DeleteItem(int64_t id);
    void PasteSelected();
    void Paint();
    void EnsureDeviceResources();
    void DiscardDeviceResources();
    void ApplyBackdrop();
    int HitTestItem(POINT point) const;
    UiAction HitTestAction(POINT point) const;
    void ShowContextMenu(POINT point, int item_index);
    HistoryQuery BuildQuery() const;
    bool HandleFilterClick(POINT point);

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    HWND paste_target_ = nullptr;
    HistoryStore& store_;
    PasteController& paste_controller_;
    std::wstring query_;
    std::vector<HistoryItem> items_;
    int selected_index_ = 0;
    ViewMode view_mode_ = ViewMode::History;
    HistorySelection selection_;
    bool multi_select_ = false;
    bool pinned_open_ = false;
    bool filter_open_ = false;
    std::set<ClipboardKind> filter_kinds_;
    float open_progress_ = 1.0f;

    ID2D1Factory* d2d_factory_ = nullptr;
    IDWriteFactory* dwrite_factory_ = nullptr;
    ID2D1HwndRenderTarget* render_target_ = nullptr;
    ID2D1SolidColorBrush* text_brush_ = nullptr;
    ID2D1SolidColorBrush* muted_brush_ = nullptr;
    ID2D1SolidColorBrush* accent_brush_ = nullptr;
    IDWriteTextFormat* title_format_ = nullptr;
    IDWriteTextFormat* body_format_ = nullptr;
    IDWriteTextFormat* small_format_ = nullptr;
};

} // namespace ClipSoul
