#pragma once

#include "ClipSoul/HistoryStore.h"
#include "ClipSoul/PopupLayout.h"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <map>
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
    void Refresh();
    HWND hwnd() const { return hwnd_; }
    bool IsVisible() const;
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

private:
    enum class ViewMode { History, Favorites };
    enum class UiAction {
        None,
        Search,
        Filter,
        MultiSelect,
        ClearAll,
        SelectAll,
        DeleteSelected,
        PasteSelected,
        HistoryTab,
        FavoritesTab,
        AddFavoritePhrase,
        Pin,
        Close,
    };
    enum class IconId {
        Pin,
        PinActive,
        Close,
        Filter,
        FilterActive,
        MultiSelect,
        MultiSelectActive,
        Trash,
        TextKind,
        LinkKind,
    };
    static constexpr int kSearchEditId = 5101;

    void ReloadItems();
    void MoveSelection(int delta);
    void ActivateSelection();
    void ToggleMultiSelect();
    void ToggleFavorite(int64_t id);
    void TogglePinned(int64_t id);
    void DeleteItem(int64_t id);
    void SelectAllVisible();
    void PasteSelected();
    void PromptAddFavoritePhrase();
    void Paint();
    void EnsureDeviceResources();
    void DiscardDeviceResources();
    void ApplyBackdrop();
    ID2D1Bitmap* LoadIconBitmap(IconId icon);
    ID2D1Bitmap* LoadImagePreviewBitmap(const std::filesystem::path& path);
    ID2D1Bitmap* LoadFileIconBitmap(const std::wstring& path);
    IconId ToWindowIcon(PopupIconAssetSlot slot) const;
    void DrawIcon(IconId icon, const UiRect& rect, float opacity = 0.82f);
    void DrawCardMedia(const HistoryItem& item, const PopupCardLayout& card, ID2D1SolidColorBrush* brush);
    UINT CurrentDpi() const;
    SIZE PhysicalPopupSize(UINT dpi, int visible_items = -1) const;
    POINT ClientPointToDips(POINT point) const;
    void ResizeToCurrentItems();
    void SyncSearchEdit();
    void UpdateSearchEditBounds();
    void UpdateThemeFromSettings();
    float SearchCaretOffsetDips() const;
    POINT SearchImeAnchorClient() const;
    void UpdateSearchImePosition();
    int HitTestItem(POINT point) const;
    UiAction HitTestAction(POINT point) const;
    void ShowContextMenu(POINT point, int item_index);
    HistoryQuery BuildQuery() const;
    bool HandleFilterClick(POINT point);
    POINT ResolvePopupPosition(HWND target, SIZE size, RECT work, UINT dpi) const;
    void HideIfInactive(HWND next_active);

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    HWND search_edit_ = nullptr;
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
    bool custom_position_ = false;
    bool prompt_open_ = false;
    bool tracking_mouse_ = false;
    bool search_focused_ = false;
    bool search_caret_on_ = true;
    bool updating_search_ime_ = false;
    int scroll_offset_ = 0;
    POINT hover_point_{-1, -1};
    UiAction hover_action_ = UiAction::None;
    PopupFilterTarget hover_filter_target_ = PopupFilterTarget::None;
    std::optional<PopupCalendarDate> hover_filter_date_;
    int hover_item_index_ = -1;
    std::set<ClipboardKind> filter_kinds_;
    PopupDateRangeState date_filter_;
    int calendar_year_ = 0;
    int calendar_month_ = 0;
    float open_progress_ = 1.0f;
    float hover_progress_ = 0.0f;
    int theme_mode_ = 0;

    ID2D1Factory* d2d_factory_ = nullptr;
    IDWriteFactory* dwrite_factory_ = nullptr;
    ID2D1HwndRenderTarget* render_target_ = nullptr;
    ID2D1SolidColorBrush* text_brush_ = nullptr;
    ID2D1SolidColorBrush* muted_brush_ = nullptr;
    ID2D1SolidColorBrush* accent_brush_ = nullptr;
    IWICImagingFactory* wic_factory_ = nullptr;
    ID2D1Bitmap* pin_icon_ = nullptr;
    ID2D1Bitmap* close_icon_ = nullptr;
    ID2D1Bitmap* filter_icon_ = nullptr;
    ID2D1Bitmap* filter_active_icon_ = nullptr;
    ID2D1Bitmap* multi_select_icon_ = nullptr;
    ID2D1Bitmap* multi_select_active_icon_ = nullptr;
    ID2D1Bitmap* trash_icon_ = nullptr;
    ID2D1Bitmap* text_kind_icon_ = nullptr;
    ID2D1Bitmap* link_kind_icon_ = nullptr;
    ID2D1Bitmap* pin_active_icon_ = nullptr;
    std::map<std::wstring, ID2D1Bitmap*> image_preview_cache_;
    std::map<std::wstring, ID2D1Bitmap*> file_icon_cache_;
    IDWriteTextFormat* title_format_ = nullptr;
    IDWriteTextFormat* body_format_ = nullptr;
    IDWriteTextFormat* small_format_ = nullptr;
    IDWriteTextFormat* centered_small_format_ = nullptr;
    IDWriteInlineObject* ellipsis_trimming_sign_ = nullptr;
    HBRUSH search_edit_brush_ = nullptr;
};

} // namespace ClipSoul
