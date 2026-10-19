#pragma once

#include "ClipSoul/HistoryStore.h"
#include "ClipSoul/PasteModel.h"
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

namespace popup_detail { class InkGlyphCache; }
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
    void RefreshTheme();
    HWND hwnd() const { return hwnd_; }
    bool IsVisible() const;
    std::optional<int64_t> SelectedItemId() const;
    HWND PasteTarget() const { return paste_target_; }
    bool PasteSelectedForContinuousPaste();
    void AdvanceSelectionAfterContinuousPaste();
    void UpdateBehaviorFromSettings();
    // The stored settings changed: drop the cached snapshot so the next read is fresh.
    void InvalidateSettingsCache() { settings_cache_valid_ = false; }
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
    enum class PopupMotionTarget {
        None,
        ViewSwitch,
        FilterPanel,
        FavoriteMenu,
        DeleteConfirm,
        MultiSelect,
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
    // Multi-paste writes one operation, waits for the target app to consume it, then writes the next.
    // The wait runs on a timer instead of Sleep() so the message pump keeps running.
    void StartMultiPaste(std::vector<MultiPasteOperation> operations);
    void AdvanceMultiPaste();
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
    void CloseFilterPanelWithMotion();
    void CloseFavoriteGroupMenuWithMotion();
    void CloseFavoriteGroupDeleteConfirmWithMotion();
    void Paint();
    void EnsureDeviceResources();
    void DiscardDeviceResources();
    void DiscardTextResources();
    void PruneImagePreviewCache();
    void ApplyBackdrop();
    void ApplyWindowRegion();
    ID2D1Bitmap* LoadImagePreviewBitmap(const std::filesystem::path& path);
    ID2D1Bitmap* LoadImageFilePreviewBitmap(const std::filesystem::path& path);
    ID2D1Bitmap* CachedImagePreviewBitmap(const std::filesystem::path& path) const;
    IconId ToWindowIcon(PopupIconAssetSlot slot) const;
    void DrawIcon(IconId icon, const UiRect& rect, float opacity = 0.82f);
    UINT CurrentDpi() const;
    SIZE PhysicalPopupSize(UINT dpi, int visible_items = -1) const;
    POINT ClientPointToDips(POINT point) const;
    void ResizeToCurrentItems();
    void SyncSearchEdit();
    void UpdateSearchEditBounds();
    void UpdateThemeFromSettings(bool force = false);
    // Cached settings snapshot: the popup used to re-read the whole settings table (plus
    // SystemParametersInfo) on every ReloadItems(), i.e. on every search keystroke.
    void ReloadSettingsCache();
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
    UiRect ContextPanelRect(bool submenu = false) const;
    UiRect ContextRowRect(int row, bool submenu = false) const;
    int HitTestContextMenu(POINT point, bool submenu = false) const;
    bool ContextRowEnabled(int row) const;
    void CloseContextMenu();
    void ExecuteContextCommand(int row, bool submenu = false);
    void ShowToast(std::wstring text);
    void BeginRemoval(const std::set<int64_t>& ids);
    float RowMotionProgress(int64_t id) const;
    float AnimatedExpandedHeight(const HistoryItem& item) const;
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
    float ItemRowTop(int item_index) const;
    PopupSectionPlan CurrentSectionPlan() const;
    // Row geometry is asked for every frame (paint, hit test, scroll animation). Walking the
    // whole list each time made these O(n^2); the prefix sums make them O(1).
    void RebuildRowMetrics();
    // Call after anything that changes row heights (expand, removal transition, width, multi-select).
    void InvalidateRowMetrics();
    // Row heights are time-dependent while expand/remove transitions run.
    bool RowMetricsAreAnimating() const;
    float RowTopForIndex(int item_index) const;
    PopupFilterLayout CurrentFilterLayout() const;
    bool HasActiveFilter() const;
    void UpdateAnimationPreferences();
    void ApplyFilterPreset(int preset_index);
    void RefreshFavoriteGroupCounts();
    int FavoriteGroupItemCount(int64_t group_id) const;
    float SlipTargetTop() const;
    float SlipTargetHeight() const;
    void KickSlipAnimation();
    void AdvanceSlipSpring();
    bool SlipNeedsAnimation() const;
    float TotalScrollHeight() const;
    float ViewportScrollHeight() const;
    float ClampSmoothScrollOffset(float requested_offset) const;
    float ScrollOffsetToRevealSelection(float requested_offset, int selected_index) const;
    void StartSmoothScrollToTop();
    void AdvanceSmoothScroll();
    bool SmoothScrollNeedsAnimation() const;
    void CancelSmoothScroll();
    int FirstVisibleItemIndex() const;
    int ResizeHitTest(POINT point) const;
    HCURSOR CursorForPoint(POINT point) const;
    void ApplyCursorForPoint(POINT point) const;
    void BeginWindowMove();
    void BeginWindowResize(int edges);
    void UpdateWindowMoveOrResize();
    void EndWindowMoveOrResize();
    void UpdateScrollDrag(POINT point);
    void StartMotion(PopupMotionTarget target, int direction = 1);
    void AdvanceMotion();
    // 把预览的 opacity 帧应用到真实窗口上（含 DWM 阴影）。
    void ApplyWindowAlpha();
    BYTE WindowAlphaForFrame() const;
    static PopupMotionKind MotionKindForTarget(PopupMotionTarget target);
    void FinishMotionIfSettled();
    void DebugLog(std::wstring_view message) const;
    // Foreground window captured when the popup was shown. The popup is WS_EX_NOACTIVATE and
    // never becomes foreground itself, so "foreground changed away from this" is how we detect
    // that the user clicked outside (works for elevated windows too).
    HWND popup_foreground_at_show_ = nullptr;
    bool DebugEnabled() const { return static_cast<bool>(debug_logger_); }

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
    std::vector<MultiPasteOperation> multi_paste_operations_;
    std::vector<HistoryItem> items_;
    int selected_index_ = 0;
    std::vector<float> row_tops_;
    std::vector<float> row_heights_;
    float total_scroll_height_ = 0.0f;
    bool row_metrics_dirty_ = true;
    std::optional<int64_t> hidden_selected_item_id_;
    ViewMode view_mode_ = ViewMode::History;
    HistorySelection selection_;
    bool multi_select_ = false;
    bool pinned_open_ = false;
    // 固定按钮旋转动画（预览 #pinWin 的 260ms 弹性旋转）。
    double pin_motion_start_ms_ = 0.0;
    bool pin_motion_from_ = false;
    bool pin_motion_to_ = false;
    float pin_motion_progress_ = 1.0f;
    bool filter_open_ = false;
    bool favorite_group_menu_open_ = false;
    bool closing_filter_panel_ = false;
    bool closing_favorite_group_menu_ = false;
    bool closing_delete_confirm_ = false;
    PendingFavoriteGroupDelete closing_delete_confirm_snapshot_;
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
    mutable std::map<int64_t, std::pair<float,float>> detail_height_cache_;
    std::optional<int64_t> arrow_motion_item_id_;
    PopupDateRangeState date_filter_;
    int calendar_year_ = 0;
    int calendar_month_ = 0;
    float hover_progress_ = 0.0f;
    PopupMotionTarget motion_target_ = PopupMotionTarget::None;
    float motion_progress_ = 1.0f;
    float arrow_motion_progress_ = 1.0f;
    int motion_direction_ = 1;
    int theme_mode_ = 0;
    AppSettings settings_cache_{};
    bool settings_cache_valid_ = false;
    int history_limit_ = 60;

    std::unique_ptr<popup_detail::InkGlyphCache> ink_glyph_cache_;
    ID2D1Factory* d2d_factory_ = nullptr;
    IDWriteFactory* dwrite_factory_ = nullptr;
    ID2D1HwndRenderTarget* render_target_ = nullptr;
    ID2D1SolidColorBrush* text_brush_ = nullptr;
    ID2D1SolidColorBrush* muted_brush_ = nullptr;
    ID2D1SolidColorBrush* accent_brush_ = nullptr;
    // Paint() needs one recolorable brush every frame; it used to create and release one per frame.
    ID2D1SolidColorBrush* paint_brush_ = nullptr;
    // The favorite ribbon notch is fixed geometry (left half and its mirror); cache both instead
    // of building path geometry twice per favorite row per frame.
    ID2D1PathGeometry* ribbon_notch_left_ = nullptr;
    ID2D1PathGeometry* ribbon_notch_right_ = nullptr;
    IWICImagingFactory* wic_factory_ = nullptr;
    // Measured text widths for strings that never change (brand, labels). Cache invalidation is
    // handled by clearing it whenever the text formats are recreated.
    std::map<std::pair<std::wstring, const void*>, float> text_width_cache_;
    std::map<std::wstring, ID2D1Bitmap*> image_preview_cache_;
    int filter_preset_ = 0;
    bool paused_ = false;
    bool animations_enabled_ = true;
    bool context_menu_open_ = false;
    bool context_submenu_open_ = false;
    bool closing_context_menu_ = false;
    std::optional<int64_t> context_item_id_;
    POINT context_origin_{};
    int context_hover_row_ = -1;
    int context_submenu_hover_row_ = -1;
    float context_submenu_scroll_ = 0.0f;
    float detail_scroll_ = 0.0f;
    double context_motion_start_ms_ = 0.0;
    float context_motion_progress_ = 1.0f;
    bool clear_confirm_open_ = false;
    bool window_entering_ = false;
    bool window_leaving_ = false;
    bool completing_window_hide_ = false;
    double window_motion_start_ms_ = 0.0;
    float window_motion_progress_ = 1.0f;
    double rows_entrance_start_ms_ = 0.0;
    bool rows_entering_ = false;
    struct RowTransition { double start_ms; bool removing; };
    std::map<int64_t, RowTransition> row_transitions_;
    struct DetailTransition { float from; float to; double start_ms; };
    std::map<int64_t, DetailTransition> detail_transitions_;
    struct BadgeTransition { double start_ms; bool pin; bool from; bool to; };
    std::map<int64_t,BadgeTransition> badge_transitions_;
    double calendar_motion_start_ms_ = 0.0;
    float calendar_motion_from_ = 0.0f;
    float calendar_motion_to_ = 0.0f;
    std::wstring toast_text_;
    double toast_start_ms_ = 0.0;
    std::optional<int64_t> stamp_item_id_;
    double stamp_start_ms_ = 0.0;
    float search_focus_from_ = 0.0f;
    float search_focus_to_ = 0.0f;
    double search_focus_start_ms_ = 0.0;
    int history_count_ = 0;
    double motion_start_ms_ = 0.0;
    double arrow_motion_start_ms_ = 0.0;
    double hover_motion_start_ms_ = 0.0;
    double slip_last_ms_ = 0.0;
    // 回到顶部：预览用 scrollTo({behavior:'smooth'})，不是瞬跳。
    bool smooth_scroll_active_ = false;
    float smooth_scroll_from_ = 0.0f;
    float smooth_scroll_to_ = 0.0f;
    double smooth_scroll_start_ms_ = 0.0;
    PopupSpringState slip_y_;
    PopupSpringState slip_h_;
    std::map<int64_t, int> favorite_group_counts_;
    int favorite_group_counts_total_ = 0;
    IDWriteTextFormat* brand_format_ = nullptr;
    IDWriteTextFormat* label_format_ = nullptr;
    IDWriteTextFormat* button_format_ = nullptr;
    IDWriteTextFormat* mono_format_ = nullptr;
    IDWriteTextFormat* search_format_ = nullptr;
    IDWriteTextFormat* title_format_ = nullptr;
    IDWriteTextFormat* body_format_ = nullptr;
    IDWriteTextFormat* code_format_ = nullptr;
    IDWriteTextFormat* extension_format_ = nullptr;
    IDWriteTextFormat* small_format_ = nullptr;
    IDWriteTextFormat* detail_format_ = nullptr;
    IDWriteTextFormat* centered_small_format_ = nullptr;
    IDWriteTextFormat* date_value_format_ = nullptr;
    IDWriteInlineObject* ellipsis_trimming_sign_ = nullptr;
    HBRUSH search_edit_brush_ = nullptr;
    COLORREF search_edit_brush_color_ = CLR_INVALID;
    std::function<void(std::wstring_view)> debug_logger_;
};

} // namespace ClipSoul
