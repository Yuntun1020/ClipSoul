#pragma once

#include <Windows.h>

#include <cstddef>
#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ClipSoul {

struct UiRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    float Width() const { return right - left; }
    float Height() const { return bottom - top; }
};

struct UiOffset {
    float x = 0.0f;
    float y = 0.0f;
};

struct PopupMetricsData {
    int width = 340;
    int height = 560;
    int margin = 16;
    int header_height = 40;
    int search_height = 44;
    int toolbar_height = 30;
    int tab_height = 34;
    int card_height = 72;
    int card_gap = 8;
    int corner_radius = 18;
    int header_button_size = 28;
    int toolbar_icon_size = 16;
    float glass_tint_opacity = 1.0f;
};

struct PopupDesignTokenData {
    int window_padding = 16;
    int section_gap = 16;
    int control_gap = 12;
    int card_gap = 8;
    int search_to_toolbar_gap = 12;
    int toolbar_to_tab_gap = 6;
    int tab_to_list_gap = 12;
    int popup_corner_radius = 18;
    int control_radius = 10;
    int card_radius = 12;
    int search_height = 44;
    int card_height = 72;
    int title_font_size = 18;
    int section_title_font_size = 14;
    int body_font_size = 13;
    int secondary_font_size = 12;
    int caption_font_size = 11;
    int toolbar_icon_size = 16;
    int list_type_icon_size = 20;
    int window_control_icon_size = 16;
    int menu_icon_size = 16;
    int popup_motion_ms = 100;
    int tab_motion_ms = 120;
    int hover_motion_ms = 80;
    int pressed_motion_ms = 60;
};

struct PopupToolbarLayout {
    UiRect filter;
    UiRect multi_select;
    UiRect clear_all;
    UiRect cancel_multi_select;
    UiRect select_all;
    UiRect delete_selected;
    UiRect paste_selected;
};

struct PopupHeaderLayout {
    UiRect title;
    UiRect pin;
    UiRect close;
};

struct PopupCardLayout {
    UiRect card;
    UiRect stripe;
    UiRect image_preview;
    UiRect file_icon;
    UiRect checkbox;
    UiRect title;
    UiRect meta;
    UiRect time;
    UiRect menu;
    UiRect expand;
};

struct PopupTabsLayout {
    UiRect history;
    UiRect favorites;
    UiRect favorite_group;
    UiRect add_favorite_phrase;
    UiRect active_indicator;
    UiRect divider;
};

struct PopupFilterLayout {
    UiRect panel;
    UiRect close;
    UiRect type_section;
    UiRect text_chip;
    UiRect image_chip;
    UiRect file_chip;
    UiRect link_chip;
    UiRect date_card;
    UiRect start_date;
    UiRect end_date;
    UiRect calendar;
    UiRect calendar_prev_year;
    UiRect calendar_prev;
    UiRect calendar_next;
    UiRect calendar_next_year;
    UiRect calendar_title;
    UiRect reset;
    UiRect done;
};

struct PopupFavoriteGroupMenuLayout {
    UiRect panel;
    UiRect all_favorites;
    UiRect group_rows;
    UiRect new_group;
    UiRect first_divider;
    UiRect second_divider;
};

struct PopupSearchLayout {
    UiRect box;
    UiRect icon;
    UiRect text;
    UiRect clear_button;
};

struct PopupSearchSelectionRange {
    size_t start = 0;
    size_t end = 0;
};

struct PopupCalendarDate {
    int year = 0;
    int month = 0;
    int day = 0;

    auto operator<=>(const PopupCalendarDate&) const = default;
};

struct PopupCalendarCell {
    PopupCalendarDate date;
    UiRect rect;
};

struct PopupCalendarRangeSegment {
    UiRect rect;
    bool starts_range = false;
    bool ends_range = false;
};

struct PopupCalendarWeekdayLabel {
    wchar_t text = L'\0';
    UiRect rect;
};

enum class PopupCalendarArrow {
    None,
    PreviousYear,
    PreviousMonth,
    NextMonth,
    NextYear,
};

enum class PopupDateRangeField {
    Start,
    End,
};

enum class PopupIconAssetSlot {
    Pin,
    PinActive,
    Close,
    Filter,
    FilterActive,
    MultiSelect,
    MultiSelectActive,
    Trash,
};

enum class PopupFavoriteFolderIconSlot {
    Outline,
    Filled,
};

struct PopupDateRangeState {
    std::optional<PopupCalendarDate> start;
    std::optional<PopupCalendarDate> end;
    PopupDateRangeField active_field = PopupDateRangeField::Start;
};

struct PopupThemePalette {
    bool dark = false;
    uint32_t window_tint = 0;
    uint32_t panel_fill = 0;
    uint32_t card_fill = 0;
    uint32_t search_fill = 0;
    uint32_t text = 0;
    uint32_t muted = 0;
    uint32_t border = 0;
    uint32_t accent = 0;
    uint32_t strong_border = 0;
    uint32_t accent_hover = 0;
    uint32_t text_tertiary = 0;
    uint32_t danger = 0;
    uint32_t paper_hover = 0;
    uint32_t paper_selected = 0;
    uint32_t border_hover = 0;
    uint32_t border_selected = 0;
    uint32_t active_tab = 0;
    uint32_t scrollbar_track = 0;
    uint32_t scrollbar_thumb = 0;
    uint32_t scrollbar_pressed = 0;
    uint32_t focus_outline = 0;
    float window_opacity = 0.0f;
    float panel_opacity = 0.0f;
    float card_opacity = 0.0f;
};

struct WindowMaterialPolicy {
    bool acrylic_blur = false;
    bool system_backdrop = false;
    bool rounded_corners = true;
    bool immersive_dark_mode = true;
};

struct SettingsProjectLayout {
    UiRect section;
    UiRect project_button;
    UiRect divider;
    UiRect version_row;
};

struct SettingsControlLayout {
    UiRect limit_edit;
    UiRect pause_toggle;
    UiRect startup_toggle;
    UiRect popup_resizable_toggle;
    UiRect theme_system;
    UiRect theme_light;
    UiRect theme_dark;
    int text_visual_offset = 0;
    bool uses_internal_dividers = false;
    bool uses_external_hover_backplates = false;
};

enum class SettingsThemeTarget {
    None,
    System,
    Light,
    Dark,
};

enum class PopupFilterTarget {
    None,
    Panel,
    Close,
    TextChip,
    ImageChip,
    FileChip,
    LinkChip,
    StartDate,
    EndDate,
    CalendarPreviousYear,
    CalendarPrevious,
    CalendarNext,
    CalendarNextYear,
    CalendarDate,
    Reset,
    Done,
};

enum class PopupFavoriteGroupMenuTarget {
    None,
    Panel,
    AllFavorites,
    Group,
    DeleteGroup,
    NewGroup,
};

struct PopupFavoriteGroupMenuHit {
    PopupFavoriteGroupMenuTarget target = PopupFavoriteGroupMenuTarget::None;
    size_t group_index = 0;
};

enum class PopupItemPressReleaseAction {
    None,
    Paste,
    SelectOnly,
};

enum class PopupItemPressMoveAction {
    KeepPress,
    CancelPress,
    SelectHitItem,
};

struct PopupContinuousPasteStep {
    int paste_index = 0;
    int next_selected_index = 0;
};

const PopupMetricsData& PopupMetrics();
const PopupDesignTokenData& PopupDesignTokens();
int PopupItemLongPressMilliseconds();
int ScalePopupMetricForDpi(int value, unsigned dpi);
int PopupWindowRegionCornerDiameterForDpi(unsigned dpi);
bool PopupWindowShouldUseHardRoundedRegion();
UiRect PopupWindowEdgeStrokeRectForSize(int logical_width, int logical_height);
bool PopupWindowEdgeShouldDrawCornerArcs();
float PopupWindowEdgeCornerRadius();
bool PopupWindowShouldDrawClientEdge();
bool PopupWindowShouldUseDwmAntialiasedFrame();
int PopupHeightForVisibleItems(int visible_items);
int PopupVisibleCardCapacity();
int PopupVisibleCardCapacityForHeight(int logical_height);
int ClampPopupScrollOffset(int item_count, int requested_offset);
int ClampPopupScrollOffsetForHeight(int item_count, int requested_offset, int logical_height);
float ClampPopupScrollOffsetForHeight(int item_count, float requested_offset, int logical_height);
int PopupScrollOffsetAfterWheel(int item_count, int current_offset, short wheel_delta);
int PopupScrollOffsetAfterWheelForHeight(int item_count, int current_offset, short wheel_delta, int logical_height);
float PopupScrollOffsetAfterWheelForHeight(int item_count, float current_offset, short wheel_delta, int logical_height);
int ClampPopupSelectedIndex(int item_count, int selected_index);
int PopupNextSelectedIndex(int item_count, int selected_index);
PopupContinuousPasteStep PopupContinuousPasteSelectionStep(int item_count, int selected_index);
int PopupScrollOffsetToRevealSelection(int item_count, int current_offset, int selected_index);
int PopupScrollOffsetToRevealSelectionForHeight(int item_count, int current_offset, int selected_index,
                                                int logical_height);
float PopupScrollOffsetToRevealSelectionForHeight(int item_count, float current_offset, int selected_index,
                                                  int logical_height);
float PopupScrollOffsetAfterViewportClampForHeight(int item_count, float current_offset, int logical_height);
float PopupScrollOffsetToRevealRange(float current_offset, float range_top, float range_bottom,
                                     float content_height, float viewport_height);
UiRect PopupScrollbarTrackRect();
UiRect PopupScrollbarTrackRectForHeight(int logical_height);
UiRect PopupScrollbarTrackRectForSize(int logical_width, int logical_height);
UiRect PopupScrollbarHitRect();
UiRect PopupScrollbarHitRectForHeight(int logical_height);
UiRect PopupScrollbarHitRectForSize(int logical_width, int logical_height);
UiRect PopupScrollbarThumbRect(int item_count, int scroll_offset);
UiRect PopupScrollbarThumbRectForHeight(int item_count, int scroll_offset, int logical_height);
UiRect PopupScrollbarThumbRectForHeight(int item_count, float scroll_offset, int logical_height);
UiRect PopupScrollbarThumbRectForSize(int item_count, float scroll_offset, int logical_width, int logical_height);
int PopupScrollOffsetForThumbCenterY(int item_count, float thumb_center_y);
int PopupScrollOffsetForThumbCenterYForHeight(int item_count, float thumb_center_y, int logical_height);
float PopupScrollbarThumbOpacity(bool hovered, bool dragging, float hover_progress);
bool PopupScrollToTopButtonVisible(float scroll_offset);
UiRect PopupScrollToTopButtonRectForSize(int logical_width, int logical_height);
UiRect PopupListClipRectForHeight(int logical_width, int logical_height);
float PopupMotionProgress(float raw_progress);
float PopupMotionEnterOffset(float progress, float max_offset);
float PopupViewSwitchListOffset(int direction, float raw_progress, float max_offset);
UiOffset PopupMotionEnterOffsetFromTrigger(const UiRect& trigger, const UiRect& panel,
                                           float progress, float max_offset);
UiOffset PopupMotionExitOffsetFromTrigger(const UiRect& trigger, const UiRect& panel,
                                          float raw_progress, float max_offset);
float PopupPopoverExitOpacity(float raw_progress);
UiRect PopupTabIndicatorRectForMotion(const UiRect& from, const UiRect& to, float progress);
float PopupMultiSelectToolbarOffset(float progress, bool entering);
float PopupMultiSelectCheckboxOffset(float progress, bool entering);
float PopupMultiSelectCheckboxOpacity(float progress, bool entering);
float PopupToggleKnobPosition(bool from_active, bool to_active, float progress);
bool PopupPromptShouldUseSlideAnimation();
bool PopupPromptShouldUseBlendAnimation();
bool PopupPromptShouldUsePositionNudgeAnimation();
int PopupPromptEntranceOffsetPixels();
int PopupPromptEntranceStepCount();
int PopupPromptEntranceTimerIntervalMs();
float PopupMotionEnterOpacity(float progress);
float PopupExpandedCardExtraHeightForMeasuredDetail(float measured_detail_height);
float PopupExpandedImageCardExtraHeightForMeasuredDetail(float measured_detail_height);
float PopupExpandedCardExtraHeightForText(std::wstring_view text);
float PopupExpandedCardExtraHeightForText(std::wstring_view text, float detail_width);
PopupThemePalette ResolvePopupThemePalette(int theme_mode, bool system_dark);
WindowMaterialPolicy ResolveWindowMaterialPolicy();
uint32_t PopupDarkUiIconTintColor(const PopupThemePalette& palette);
uint32_t PopupToolbarLabelColor(const PopupThemePalette& palette, bool active, bool danger);
bool PopupUiIconShouldFallbackToOriginalBitmap(const PopupThemePalette& palette, bool content_icon);
bool PopupUiIconShouldLoadBitmap(const PopupThemePalette& palette, bool content_icon, bool fast_interaction);
SettingsProjectLayout SettingsWindowProjectLayout();
SettingsControlLayout SettingsWindowControlLayout();
SettingsThemeTarget HitTestSettingsThemeTarget(const SettingsControlLayout& layout, float x, float y);
bool ThemeChangeShouldRefreshPalette(int theme_mode);
bool SettingsThemePreviewShouldRefreshNativeControls(int previous_mode, int next_mode);
bool ThemeModeResolvesDarkChrome(int theme_mode, bool system_dark);
bool SettingsLimitEditShouldSelectAllOnLoad();
std::wstring_view PopupSearchPlaceholderText();
std::wstring_view PopupSearchDisplayText(std::wstring_view query);
std::wstring PopupEmptyMessage(bool favorites_view, std::wstring_view active_favorite_group);
std::wstring PopupNotePreviewText(std::wstring_view note);
bool PopupFileCanUseImagePreview(std::wstring_view path);
bool PopupSearchCaretVisible(bool focused, bool blink_on);
bool PopupSearchHasComposition(bool composing, const std::wstring& composition_text);
std::wstring PopupSearchCompositionDisplayText(std::wstring_view query, bool composing, const std::wstring& composition_text);
bool PopupSearchCompositionTextColor(bool has_composition, std::wstring_view query);
bool PopupSearchCaretVisibleDuringComposition(bool has_composition);
bool PopupResizeShouldDiscardDeviceResources();
bool PopupDpiChangeShouldDiscardDeviceResources();
bool PopupPaintShouldUpdateLayout();
bool PopupShouldAnimateHoverWhileResizing(bool resizing_window);
bool PopupShouldActivateWhenShown(bool keyboard_invocation);
bool PopupShouldActivateForShellSurface(bool shell_surface);
bool PopupShowShouldActivate(bool keyboard_invocation, bool shell_surface);
bool PopupSetWindowPosShouldUseNoActivate(bool activate_window);
LRESULT PopupMouseActivateResult();
bool PopupShouldFocusWindowForPointerPress(bool search_clicked);
bool PopupShouldActivateForSearchFocus(bool search_clicked, bool has_native_edit);
bool PopupShouldAutoFocusSearchOnShow(bool has_native_edit);
bool PopupWindowShouldUseNoActivateStyle();
bool PopupWindowShouldUseNoActivateStyle(bool activate_on_show);
bool PopupNativeSearchCueBannerShowsWhenFocused();

bool PopupShouldRedrawNativeSearchAfterParentPaint(bool has_native_edit, bool moving_or_resizing_window);
bool PopupSearchShouldDrawSelection(bool focused, bool has_query_text, PopupSearchSelectionRange selection);
bool PopupShouldDrawDecorativeShadows(bool moving_or_resizing_window);
bool PopupShouldLoadUiIcon(bool moving_or_resizing_window);
bool PopupIsFastMediaInteraction(bool moving_window, bool resizing_window, bool view_switch_motion);
bool PopupShouldLoadImagePreview(bool moving_or_resizing_window);
bool PopupShouldLoadFileIcon(bool moving_or_resizing_window);
bool PopupShouldDrawCachedMediaDuringFastInteraction(bool moving_or_resizing_window, bool cached);
bool PopupShouldUseCurrentWindowWidthForLayout(bool popup_resizable, bool manual_popup_size,
                                               int current_logical_width, int default_logical_width);
UiRect PopupNativeSearchEditRect(const PopupSearchLayout& layout);
PopupSearchSelectionRange NormalizePopupSearchSelection(size_t anchor, size_t caret, size_t text_length);
bool PopupSearchHasSelection(PopupSearchSelectionRange selection);
bool PopupShouldResizeNativeSearchDuringLiveResize(bool resizing_window, bool bounds_changed);
bool PopupShouldInvalidateDuringLiveResize(bool resizing_window, bool size_changed);
bool PopupShouldApplyWindowRect(const RECT& current, const RECT& next);
bool PopupShouldFlushPaintDuringLiveResize(bool resizing_window, bool rect_changed);
unsigned PopupImageFilePreviewDecodePixelLimit();
unsigned PopupImagePreviewDecodePixelLimit();
SIZE PopupPreviewDecodeSize(unsigned source_width, unsigned source_height, unsigned max_dimension);
float PopupSearchFocusProgress(bool focused, bool hovered, float hover_progress);
float ClampPopupSearchCaretX(const PopupSearchLayout& layout, float measured_text_width);
bool PopupSearchClearButtonHitTest(const PopupSearchLayout& layout, bool has_query, POINT point);
POINT PopupSearchClearButtonCenterDips(const PopupSearchLayout& layout);
float PopupSearchClearButtonOpacity(bool hovered);
float PopupSearchTop();
float PopupToolbarTop();
float PopupTabsTop();
float PopupListTop();
PopupHeaderLayout BuildPopupHeaderLayout();
PopupHeaderLayout BuildPopupHeaderLayoutForWidth(int logical_width);
PopupSearchLayout BuildPopupSearchLayout();
PopupSearchLayout BuildPopupSearchLayoutForWidth(int logical_width);
UiRect BuildPopupHeaderPinIconRect(const PopupHeaderLayout& header);
UiRect BuildPopupHeaderCloseIconRect(const PopupHeaderLayout& header);
PopupToolbarLayout BuildPopupToolbarLayout(bool multi_select);
PopupToolbarLayout BuildPopupToolbarLayoutForWidth(bool multi_select, int logical_width);
UiRect PopupToolbarLabelRect(const UiRect& button, bool has_chevron);
PopupTabsLayout BuildPopupTabsLayout(bool favorites_active);
PopupTabsLayout BuildPopupTabsLayoutForWidth(bool favorites_active, int logical_width);
UiRect PopupFavoriteGroupIconRect(const PopupTabsLayout& tabs);
PopupFilterLayout BuildPopupFilterLayout();
size_t PopupFavoriteGroupMenuVisibleGroupCount(size_t group_count);
PopupFavoriteGroupMenuLayout BuildPopupFavoriteGroupMenuLayout(size_t group_count);
UiRect PopupFavoriteGroupMenuGroupRect(const PopupFavoriteGroupMenuLayout& layout, size_t index);
UiRect PopupFavoriteGroupMenuDeleteRect(const UiRect& group_row);
PopupFavoriteGroupMenuHit HitTestPopupFavoriteGroupMenu(const PopupFavoriteGroupMenuLayout& layout,
                                                        size_t group_count, float x, float y);
UiRect PopupFilterResetVisualRect(const PopupFilterLayout& layout);
UiRect PopupFilterArrowGlyphRect(const UiRect& arrow_button);
std::vector<PopupCalendarWeekdayLabel> BuildPopupCalendarWeekdayLabels(const PopupFilterLayout& layout);
PopupCardLayout BuildPopupCardLayout(bool multi_select, float top);
UiRect PopupCardKindIconRect(const PopupCardLayout& card);
float PopupExpandedCardExtraHeight(bool expanded);
int HitTestPopupCardIndex(int item_count, int scroll_offset, std::optional<int64_t> expanded_item_id,
                          const std::vector<int64_t>& visible_item_ids, float x, float y);
int HitTestPopupCardExpandIndex(int item_count, int scroll_offset, std::optional<int64_t> expanded_item_id,
                                const std::vector<int64_t>& visible_item_ids, float x, float y);
UiRect FitImageRectToBounds(float source_width, float source_height, const UiRect& bounds);
std::vector<PopupCalendarCell> BuildPopupCalendarCells(const PopupFilterLayout& layout, int year, int month);
std::vector<PopupCalendarRangeSegment> BuildPopupCalendarRangeSegments(
    const PopupFilterLayout& layout, int year, int month, const PopupDateRangeState& state);
std::optional<PopupCalendarDate> HitTestPopupCalendarDate(const PopupFilterLayout& layout, int year, int month,
                                                          float x, float y);
PopupCalendarArrow HitTestPopupCalendarArrow(const PopupFilterLayout& layout, float x, float y);
std::optional<PopupDateRangeField> HitTestPopupDateRangeField(const PopupFilterLayout& layout, float x, float y);
PopupFilterTarget HitTestPopupFilterTarget(const PopupFilterLayout& layout, int year, int month, float x, float y);
void SelectPopupDateRangeDate(PopupDateRangeState& state, PopupCalendarDate date);
bool RectsOverlap(const UiRect& a, const UiRect& b);
POINT ClampPopupToWorkArea(POINT anchor, SIZE size, RECT work, unsigned dpi);
bool PopupTextRangeRectUsable(RECT rect);
POINT PopupTextRangeAnchor(RECT rect);
bool PopupTextCharacterRectUsable(RECT rect, unsigned dpi);
RECT PopupTextCharacterCaretRect(RECT rect, bool after_character);
bool PopupTextInputRectUsable(RECT rect);
POINT PopupTextInputRectAnchor(RECT rect, unsigned dpi);
bool PopupCaretAnchorAllowedByFocusedText(bool focused_text_editable, RECT focused_rect, bool has_focused_rect,
                                          RECT caret_rect, unsigned dpi);
bool PopupJavaCaretRectUsable(RECT caret_rect, RECT window_rect, unsigned dpi);
bool PopupVisualCaretRectUsable(RECT caret_rect, RECT window_rect, unsigned dpi);
POINT PopupTextAvoidRectPosition(RECT avoid, SIZE size, RECT work, unsigned dpi);
POINT PopupTextAnchorPosition(POINT anchor, SIZE size, RECT work, unsigned dpi);
POINT PopupWindowsClipboardPosition(SIZE size, RECT monitor, RECT work, unsigned dpi);
POINT PopupKeyboardInvocationPosition(bool has_text_avoid, RECT avoid, SIZE size, RECT monitor, RECT work, unsigned dpi);
bool PopupTargetNeedsShellTopmostRaise(std::wstring_view class_name, std::wstring_view title);
bool PopupTargetNeedsShellTopmostRaise(std::wstring_view class_name, std::wstring_view title,
                                       std::wstring_view process_name);
bool PopupShellTopmostRaiseShouldExpire(bool keep_above_shell_surface, long remaining_ms);
bool PopupShouldCreateInShellWindowBand();
bool PopupTargetRequiresExplicitTextInputFocus(std::wstring_view class_name, std::wstring_view title,
                                               std::wstring_view process_name = {});
bool PopupShouldUseTextAvoidForTarget(bool has_text_avoid, std::wstring_view class_name, std::wstring_view title);
bool PopupTargetCanUseConsoleAnchor(std::wstring_view class_name);
POINT PopupWindowRectFallback(RECT target, SIZE size, RECT work, unsigned dpi);
POINT PopupTargetCenterFallback(RECT target, SIZE size, RECT work, unsigned dpi);
POINT PopupConsoleCellAnchor(POINT client_origin_screen, COORD cell, SMALL_RECT viewport, COORD font_size);
COORD PopupConsoleAnchorCell(CONSOLE_SELECTION_INFO selection, COORD cursor_position);
COORD PopupConsoleSelectionAnchor(CONSOLE_SELECTION_INFO selection);
POINT PopupBottomRightFallback(SIZE size, RECT work, unsigned dpi);
POINT CenterWindowInWorkArea(SIZE size, RECT work);
int PopupHoverItemIndex(bool filter_open, int hit_item_index);
bool ShouldHidePopupAfterPaste(bool pinned_open);
bool ShouldHidePopupAfterContinuousPaste(bool pinned_open);
bool ShouldHidePopupAfterOutsideClick(bool pinned_open, bool prompt_open, bool moving_window,
                                      bool mouse_down_started_inside_popup, bool transient_hide_suppressed,
                                      bool visible, bool new_mouse_press, bool click_inside_popup);
bool ShouldHidePopupAfterInactive(bool pinned_open, bool prompt_open, bool moving_window,
                                  bool transient_hide_suppressed, bool visible, bool next_active_inside_popup,
                                  bool shell_surface_raise_active = false);
bool PopupPointerInteractionSuppressesInactiveHide(bool moving_window, bool resizing_window,
                                                   bool mouse_down_started_inside_popup,
                                                   bool left_button_was_down);
PopupItemPressReleaseAction PopupItemPressReleaseActionFor(bool same_item, bool long_press_selected);
PopupItemPressMoveAction PopupItemPressMoveActionFor(bool long_press_selected, bool moved_past_cancel_distance,
                                                     int hit_item_index);
std::optional<int> PopupSelectionIndexWhileLongPressing(bool long_press_selected, int hit_item_index);
std::wstring_view PopupFavoriteMenuLabel(bool is_favorite);
std::wstring_view PopupPinMenuLabel(bool is_pinned);
bool IsPopupHeaderDragArea(float x, float y);
bool IsPopupHeaderDragAreaForWidth(float x, float y, int logical_width);
PopupIconAssetSlot PopupPinIconSlot(bool pinned_open);
PopupIconAssetSlot PopupFilterIconSlot(bool filter_open);
PopupIconAssetSlot PopupMultiSelectIconSlot(bool active);
PopupIconAssetSlot PopupPasteSelectedIconSlot();
PopupFavoriteFolderIconSlot PopupFavoriteFolderIconSlotForGroup(bool active);

} // namespace ClipSoul
