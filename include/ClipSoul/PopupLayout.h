#pragma once

#include <Windows.h>

#include <optional>
#include <cstdint>
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

struct PopupMetricsData {
    int width = 340;
    int height = 560;
    int margin = 14;
    int header_height = 40;
    int search_height = 38;
    int toolbar_height = 38;
    int tab_height = 42;
    int card_height = 72;
    int card_gap = 8;
    int corner_radius = 18;
    int header_button_size = 22;
    int toolbar_icon_size = 14;
    float glass_tint_opacity = 0.18f;
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
};

struct PopupTabsLayout {
    UiRect history;
    UiRect favorites;
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
    UiRect calendar_prev;
    UiRect calendar_next;
    UiRect calendar_title;
    UiRect reset;
    UiRect done;
};

struct PopupSearchLayout {
    UiRect box;
    UiRect icon;
    UiRect text;
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

struct PopupCalendarWeekdayLabel {
    wchar_t text = L'\0';
    UiRect rect;
};

enum class PopupCalendarArrow {
    None,
    PreviousMonth,
    NextMonth,
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
    uint32_t danger = 0;
    float window_opacity = 0.0f;
    float panel_opacity = 0.0f;
    float card_opacity = 0.0f;
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
    CalendarPrevious,
    CalendarNext,
    CalendarDate,
    Reset,
    Done,
};

const PopupMetricsData& PopupMetrics();
int ScalePopupMetricForDpi(int value, unsigned dpi);
int PopupHeightForVisibleItems(int visible_items);
int PopupVisibleCardCapacity();
int ClampPopupScrollOffset(int item_count, int requested_offset);
int PopupScrollOffsetAfterWheel(int item_count, int current_offset, short wheel_delta);
PopupThemePalette ResolvePopupThemePalette(int theme_mode, bool system_dark);
std::wstring_view PopupSearchPlaceholderText();
std::wstring_view PopupSearchDisplayText(std::wstring_view query);
bool PopupSearchCaretVisible(bool focused, bool blink_on);
bool PopupSearchAcceptsTextInput(bool focused);
bool PopupSearchShouldUpdateImePosition(bool focused, bool updating_ime);
bool PopupSearchDeletesOnChar(wchar_t value);
bool PopupSearchDeletesOnKeyDown(unsigned virtual_key);
bool PopupSearchAppendsChar(wchar_t value);
float PopupSearchFocusProgress(bool focused, bool hovered, float hover_progress);
float ClampPopupSearchCaretX(const PopupSearchLayout& layout, float measured_text_width);
POINT PopupSearchImeAnchorDips(const PopupSearchLayout& layout, float measured_text_width);
float PopupSearchTop();
float PopupToolbarTop();
float PopupTabsTop();
float PopupListTop();
PopupHeaderLayout BuildPopupHeaderLayout();
PopupSearchLayout BuildPopupSearchLayout();
UiRect BuildPopupHeaderPinIconRect(const PopupHeaderLayout& header);
UiRect BuildPopupHeaderCloseIconRect(const PopupHeaderLayout& header);
PopupToolbarLayout BuildPopupToolbarLayout(bool multi_select);
UiRect PopupToolbarLabelRect(const UiRect& button, bool has_chevron);
PopupTabsLayout BuildPopupTabsLayout(bool favorites_active);
PopupFilterLayout BuildPopupFilterLayout();
UiRect PopupFilterResetVisualRect(const PopupFilterLayout& layout);
UiRect PopupFilterArrowGlyphRect(const UiRect& arrow_button);
std::vector<PopupCalendarWeekdayLabel> BuildPopupCalendarWeekdayLabels(const PopupFilterLayout& layout);
PopupCardLayout BuildPopupCardLayout(bool multi_select, float top);
UiRect FitImageRectToBounds(float source_width, float source_height, const UiRect& bounds);
std::vector<PopupCalendarCell> BuildPopupCalendarCells(const PopupFilterLayout& layout, int year, int month);
std::optional<PopupCalendarDate> HitTestPopupCalendarDate(const PopupFilterLayout& layout, int year, int month,
                                                          float x, float y);
PopupCalendarArrow HitTestPopupCalendarArrow(const PopupFilterLayout& layout, float x, float y);
std::optional<PopupDateRangeField> HitTestPopupDateRangeField(const PopupFilterLayout& layout, float x, float y);
PopupFilterTarget HitTestPopupFilterTarget(const PopupFilterLayout& layout, int year, int month, float x, float y);
void SelectPopupDateRangeDate(PopupDateRangeState& state, PopupCalendarDate date);
bool RectsOverlap(const UiRect& a, const UiRect& b);
POINT ClampPopupToWorkArea(POINT anchor, SIZE size, RECT work, unsigned dpi);
POINT PopupBottomRightFallback(SIZE size, RECT work, unsigned dpi);
POINT CenterWindowInWorkArea(SIZE size, RECT work);
int PopupHoverItemIndex(bool filter_open, int hit_item_index);
bool ShouldHidePopupAfterPaste(bool pinned_open);
std::wstring_view PopupFavoriteMenuLabel(bool is_favorite);
std::wstring_view PopupPinMenuLabel(bool is_pinned);
bool IsPopupHeaderDragArea(float x, float y);
PopupIconAssetSlot PopupPinIconSlot(bool pinned_open);
PopupIconAssetSlot PopupFilterIconSlot(bool filter_open);
PopupIconAssetSlot PopupMultiSelectIconSlot(bool active);
PopupIconAssetSlot PopupPasteSelectedIconSlot();

} // namespace ClipSoul
