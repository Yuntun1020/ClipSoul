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
#include <functional>
#include <string>
#include <vector>

namespace ClipSoul {

class PasteController;

enum class PopupFavoriteGroupDeleteConfirmTarget {
    None,
    Panel,
    Delete,
    Cancel,
};

class PopupWindow {
public:
    PopupWindow(HINSTANCE instance, HistoryStore& store, PasteController& paste_controller);
    ~PopupWindow();

    bool Create(HWND owner);
    void Show(HWND target);
    void Hide();
    void Hide(std::wstring_view reason);
    void Refresh();
    HWND hwnd() const { return hwnd_; }
    bool IsVisible() const;
    std::optional<int64_t> SelectedItemId() const;
    HWND PasteTarget() const { return paste_target_; }
    bool PasteSelectedForContinuousPaste();
    void AdvanceSelectionAfterContinuousPaste();
    void UpdateBehaviorFromSettings();
    void ResetManualSize();
    void SetDebugLogger(std::function<void(std::wstring_view)> logger);
    void SetKeyboardInvocation(bool keyboard_invocation);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

private:
    enum class ViewMode { History, Favorites };
    enum class UiAction {
        None,
        Search,
        ClearSearch,
        Filter,
        MultiSelect,
        ClearAll,
        SelectAll,
        DeleteSelected,
        PasteSelected,
        HistoryTab,
        FavoritesTab,
        FavoriteGroupMenu,
        AddFavoritePhrase,
        ExpandItem,
        Scrollbar,
        ScrollToTop,
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
        AddFavoriteFolderOutline,
        AddFavoriteFolderFilled,
    };
    struct PendingFavoriteGroupDelete {
        int64_t id = 0;
        size_t group_index = 0;
        std::wstring name;
    };
    static constexpr int kSearchEditId = 5101;

    void ReloadItems();
    void MoveSelection(int delta);
    void ActivateSelection();
    void ToggleMultiSelect();
    void ToggleFavorite(int64_t id);
    void TogglePinned(int64_t id);
    void DeleteItem(int64_t id);
    void MoveItemInCurrentList(int item_index, int delta);
    void SelectAllVisible();
    void PasteSelected();
    void PromptAddFavoritePhrase();
    void PromptCreateFavoriteGroup();
    void PromptEditNote(int64_t id);
    void ToggleFavoriteGroupMenu();
    void ToggleExpanded(int64_t id);
    void SetSelectedItemId(std::optional<int64_t> id);
    void BeginItemPress(int item_index, POINT point);
    void CancelItemPress();
    void CompleteItemPress(POINT point);
    void HandleLongPressTimer();
    void Paint();
    void EnsureDeviceResources();
    void DiscardDeviceResources();
    void ApplyBackdrop();
    ID2D1Bitmap* LoadIconBitmap(IconId icon);
    ID2D1Bitmap* LoadImagePreviewBitmap(const std::filesystem::path& path);
    ID2D1Bitmap* LoadImageFilePreviewBitmap(const std::filesystem::path& path);
    ID2D1Bitmap* LoadFileIconBitmap(const std::wstring& path);
    ID2D1Bitmap* CachedIconBitmap(IconId icon) const;
    ID2D1Bitmap* CachedImagePreviewBitmap(const std::filesystem::path& path) const;
    ID2D1Bitmap* CachedFileIconBitmap(const std::wstring& path) const;
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
    float SearchTextOffsetDips(size_t text_index, bool trailing) const;
    size_t SearchTextIndexFromPoint(POINT point) const;
    PopupSearchSelectionRange CurrentSearchSelection() const;
    void SyncSearchSelectionFromEdit();
    void SetSearchSelection(size_t anchor, size_t caret);
    void EndSearchSelectionDrag();
    int HitTestItem(POINT point) const;
    int HitTestExpandItem(POINT point) const;
    UiAction HitTestAction(POINT point) const;
    void ShowContextMenu(POINT point, int item_index);
    HistoryQuery BuildQuery() const;
    std::wstring ActiveFavoriteGroupLabel() const;
    std::wstring FavoriteGroupName(std::optional<int64_t> group_id) const;
    bool HandleFilterClick(POINT point);
    bool HandleFavoriteGroupMenuClick(POINT point);
    bool HandleFavoriteGroupDeleteConfirmClick(POINT point);
    POINT ResolvePopupPosition(HWND target, SIZE size, RECT monitor, RECT work, UINT dpi) const;
    void HideIfInactive(HWND next_active);
    void RaiseAboveShellSurface();
    void BeginTransientHideSuppression();
    void EndTransientHideSuppressionSoon();
    bool IsTransientHideSuppressed() const;
    void UpdatePopupLogicalSize();
    void ClampScrollToCurrentPopupHeight();
    int DesiredPopupLogicalHeight() const;
    float ExpandedExtraHeightForItem(const HistoryItem& item) const;
    float MeasureDetailTextHeight(std::wstring_view text, float width) const;
    float ItemScrollTop(int item_index) const;
    float ItemScrollHeight(int item_index) const;
    float TotalScrollHeight() const;
    float ViewportScrollHeight() const;
    float ClampSmoothScrollOffset(float requested_offset) const;
    float ScrollOffsetToRevealSelection(float requested_offset, int selected_index) const;
    int FirstVisibleItemIndex() const;
    int ResizeHitTest(POINT point) const;
    void BeginWindowMove();
    void BeginWindowResize(int edges);
    void UpdateWindowMoveOrResize();
    void EndWindowMoveOrResize();
    void UpdateScrollDrag(POINT point);
    void DebugLog(std::wstring_view message) const;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK SearchEditSubclassProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id_ptr, DWORD_PTR ref_data);

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    HWND search_edit_ = nullptr;
    WNDPROC search_edit_prev_proc_ = nullptr;
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
    bool favorite_group_menu_open_ = false;
    bool custom_position_ = false;
    bool prompt_open_ = false;
    bool tracking_mouse_ = false;
    bool search_focused_ = false;
    bool search_caret_on_ = true;
    bool search_selecting_ = false;
    size_t search_selection_anchor_ = 0;
    size_t search_selection_caret_ = 0;
    std::wstring composition_text_;
    bool composing_ = false;
    bool moving_window_ = false;
    bool resizing_window_ = false;
    int resize_edges_ = 0;
    POINT drag_start_screen_{0, 0};
    RECT drag_start_rect_{};
    bool mouse_down_started_inside_popup_ = false;
    bool suppress_inactive_hide_ = false;
    bool left_button_was_down_ = false;
    DWORD suppress_inactive_hide_until_ = 0;
    bool popup_resizable_ = false;
    bool manual_popup_size_ = false;
    bool keyboard_invocation_ = false;
    bool shell_topmost_raise_ = false;
    DWORD shell_topmost_raise_until_ = 0;
    int popup_logical_width_ = 340;
    int popup_logical_height_ = 560;
    float scroll_offset_ = 0.0f;
    bool dragging_scrollbar_ = false;
    int pressed_item_index_ = -1;
    bool long_press_selected_ = false;
    POINT press_point_{-1, -1};
    POINT hover_point_{-1, -1};
    UiAction hover_action_ = UiAction::None;
    PopupFilterTarget hover_filter_target_ = PopupFilterTarget::None;
    PopupFavoriteGroupDeleteConfirmTarget hover_delete_confirm_target_ = PopupFavoriteGroupDeleteConfirmTarget::None;
    std::optional<PopupCalendarDate> hover_filter_date_;
    PopupFavoriteGroupMenuHit hover_favorite_group_menu_hit_;
    int hover_item_index_ = -1;
    std::set<ClipboardKind> filter_kinds_;
    std::vector<FavoriteGroup> favorite_groups_;
    std::optional<int64_t> active_favorite_group_id_;
    std::optional<PendingFavoriteGroupDelete> pending_favorite_group_delete_;
    std::optional<int64_t> expanded_item_id_;
    PopupDateRangeState date_filter_;
    int calendar_year_ = 0;
    int calendar_month_ = 0;
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
    ID2D1Bitmap* add_favorite_folder_outline_icon_ = nullptr;
    ID2D1Bitmap* add_favorite_folder_filled_icon_ = nullptr;
    ID2D1Bitmap* pin_active_icon_ = nullptr;
    std::map<std::wstring, ID2D1Bitmap*> image_preview_cache_;
    std::map<std::wstring, ID2D1Bitmap*> file_icon_cache_;
    IDWriteTextFormat* title_format_ = nullptr;
    IDWriteTextFormat* body_format_ = nullptr;
    IDWriteTextFormat* small_format_ = nullptr;
    IDWriteTextFormat* detail_format_ = nullptr;
    IDWriteTextFormat* centered_small_format_ = nullptr;
    IDWriteInlineObject* ellipsis_trimming_sign_ = nullptr;
    HBRUSH search_edit_brush_ = nullptr;
    std::function<void(std::wstring_view)> debug_logger_;
};

} // namespace ClipSoul
