#include "ClipSoul/PopupLayout.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace ClipSoul {
namespace {
constexpr PopupMetricsData kMetrics{
    340, // width
    560, // height
    14,  // margin
    40,  // header_height
    38,  // search_height
    38,  // toolbar_height
    42,  // tab_height
    68,  // card_height
    6,   // card_gap
    18,  // corner_radius
    22,  // header_button_size
    14,  // toolbar_icon_size
    0.14f,
};

UiRect Rect(float left, float top, float right, float bottom) {
    return UiRect{left, top, right, bottom};
}

bool Contains(const UiRect& rect, float x, float y) {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

int ClampToRange(int value, int min_value, int max_value) {
    if (min_value > max_value) {
        return min_value;
    }
    return std::clamp(value, min_value, max_value);
}

bool IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DaysInMonth(int year, int month) {
    constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && IsLeapYear(year)) {
        return 29;
    }
    return kDays[month - 1];
}

int WeekdaySundayFirst(int year, int month, int day) {
    if (month < 3) {
        month += 12;
        --year;
    }
    const int year_of_century = year % 100;
    const int zero_based_century = year / 100;
    const int h = (day + (13 * (month + 1)) / 5 + year_of_century + year_of_century / 4 +
                   zero_based_century / 4 + 5 * zero_based_century) %
                  7;
    return (h + 6) % 7;
}
} // namespace

const PopupMetricsData& PopupMetrics() {
    return kMetrics;
}

int PopupItemLongPressMilliseconds() {
    return 100;
}

int ScalePopupMetricForDpi(int value, unsigned dpi) {
    if (dpi == 0) {
        dpi = 96;
    }
    return static_cast<int>((static_cast<long long>(value) * dpi + 48) / 96);
}

int PopupHeightForVisibleItems(int) {
    return kMetrics.height;
}

int PopupVisibleCardCapacity() {
    return PopupVisibleCardCapacityForHeight(kMetrics.height);
}

int PopupVisibleCardCapacityForHeight(int logical_height) {
    const float available = static_cast<float>(logical_height) - PopupListTop();
    return std::max(0, static_cast<int>((available + static_cast<float>(kMetrics.card_gap)) /
                                        static_cast<float>(kMetrics.card_height + kMetrics.card_gap)));
}

int ClampPopupScrollOffset(int item_count, int requested_offset) {
    return ClampPopupScrollOffsetForHeight(item_count, requested_offset, kMetrics.height);
}

int ClampPopupScrollOffsetForHeight(int item_count, int requested_offset, int logical_height) {
    const int max_offset = std::max(0, item_count - PopupVisibleCardCapacityForHeight(logical_height));
    return std::clamp(requested_offset, 0, max_offset);
}

float ClampPopupScrollOffsetForHeight(int item_count, float requested_offset, int logical_height) {
    const auto& metrics = PopupMetrics();
    const float content_height =
        item_count <= 0 ? 0.0f : static_cast<float>(item_count * metrics.card_height +
                                                    std::max(0, item_count - 1) * metrics.card_gap);
    const float viewport_height =
        std::max(0.0f, static_cast<float>(logical_height) - PopupListTop() - 4.0f);
    const float max_offset = std::max(0.0f, content_height - viewport_height);
    return std::clamp(requested_offset, 0.0f, max_offset);
}

int PopupScrollOffsetAfterWheel(int item_count, int current_offset, short wheel_delta) {
    return PopupScrollOffsetAfterWheelForHeight(item_count, current_offset, wheel_delta, kMetrics.height);
}

int PopupScrollOffsetAfterWheelForHeight(int item_count, int current_offset, short wheel_delta, int logical_height) {
    const int delta = wheel_delta < 0 ? 1 : -1;
    return ClampPopupScrollOffsetForHeight(item_count, current_offset + delta, logical_height);
}

float PopupScrollOffsetAfterWheelForHeight(int item_count, float current_offset, short wheel_delta,
                                           int logical_height) {
    constexpr float kWheelStep = 42.0f;
    const float direction = wheel_delta < 0 ? 1.0f : -1.0f;
    return ClampPopupScrollOffsetForHeight(item_count, current_offset + direction * kWheelStep, logical_height);
}

int ClampPopupSelectedIndex(int item_count, int selected_index) {
    if (item_count <= 0) {
        return 0;
    }
    return std::clamp(selected_index, 0, item_count - 1);
}

int PopupNextSelectedIndex(int item_count, int selected_index) {
    if (item_count <= 0) {
        return 0;
    }
    return (ClampPopupSelectedIndex(item_count, selected_index) + 1) % item_count;
}

int PopupScrollOffsetToRevealSelection(int item_count, int current_offset, int selected_index) {
    return PopupScrollOffsetToRevealSelectionForHeight(item_count, current_offset, selected_index, kMetrics.height);
}

int PopupScrollOffsetToRevealSelectionForHeight(int item_count, int current_offset, int selected_index,
                                                int logical_height) {
    if (item_count <= 0) {
        return 0;
    }
    const int clamped_selection = ClampPopupSelectedIndex(item_count, selected_index);
    const int visible_capacity = PopupVisibleCardCapacityForHeight(logical_height);
    int offset = ClampPopupScrollOffsetForHeight(item_count, current_offset, logical_height);
    if (clamped_selection < offset) {
        offset = clamped_selection;
    } else if (clamped_selection >= offset + visible_capacity) {
        offset = clamped_selection - visible_capacity + 1;
    }
    return ClampPopupScrollOffsetForHeight(item_count, offset, logical_height);
}

float PopupScrollOffsetToRevealSelectionForHeight(int item_count, float current_offset, int selected_index,
                                                  int logical_height) {
    if (item_count <= 0) {
        return 0.0f;
    }
    const auto& metrics = PopupMetrics();
    const float row_pitch = static_cast<float>(metrics.card_height + metrics.card_gap);
    const int clamped_selection = ClampPopupSelectedIndex(item_count, selected_index);
    const float selected_top = static_cast<float>(clamped_selection) * row_pitch;
    const float selected_bottom = selected_top + static_cast<float>(metrics.card_height);
    const float viewport_height =
        std::max(0.0f, static_cast<float>(logical_height) - PopupListTop() - 4.0f);
    float offset = ClampPopupScrollOffsetForHeight(item_count, current_offset, logical_height);
    if (selected_top < offset) {
        offset = selected_top;
    } else if (selected_bottom > offset + viewport_height) {
        offset = selected_bottom - viewport_height;
    }
    return ClampPopupScrollOffsetForHeight(item_count, offset, logical_height);
}

float PopupScrollOffsetAfterViewportClampForHeight(int item_count, float current_offset, int logical_height) {
    return ClampPopupScrollOffsetForHeight(item_count, current_offset, logical_height);
}

UiRect PopupScrollbarTrackRect() {
    return PopupScrollbarTrackRectForHeight(kMetrics.height);
}

UiRect PopupScrollbarTrackRectForHeight(int logical_height) {
    return Rect(static_cast<float>(kMetrics.width - 12), PopupListTop(),
                static_cast<float>(kMetrics.width - 6), static_cast<float>(logical_height - 10));
}

UiRect PopupScrollbarHitRect() {
    return PopupScrollbarHitRectForHeight(kMetrics.height);
}

UiRect PopupScrollbarHitRectForHeight(int logical_height) {
    const auto track = PopupScrollbarTrackRectForHeight(logical_height);
    return Rect(track.left - 8.0f, track.top, static_cast<float>(kMetrics.width), track.bottom);
}

UiRect PopupScrollbarThumbRect(int item_count, int scroll_offset) {
    return PopupScrollbarThumbRectForHeight(item_count, scroll_offset, kMetrics.height);
}

UiRect PopupScrollbarThumbRectForHeight(int item_count, int scroll_offset, int logical_height) {
    const auto track = PopupScrollbarTrackRectForHeight(logical_height);
    const int visible_capacity = PopupVisibleCardCapacityForHeight(logical_height);
    if (item_count <= visible_capacity) {
        return Rect(track.left, track.top, track.right, track.top);
    }
    const float track_height = track.Height();
    const float thumb_height =
        std::max(32.0f, track_height * static_cast<float>(visible_capacity) / static_cast<float>(item_count));
    const int max_offset = std::max(1, item_count - visible_capacity);
    const int clamped_offset = ClampPopupScrollOffsetForHeight(item_count, scroll_offset, logical_height);
    const float thumb_top =
        track.top + (track_height - thumb_height) * static_cast<float>(clamped_offset) / static_cast<float>(max_offset);
    return Rect(track.left, thumb_top, track.right, thumb_top + thumb_height);
}

UiRect PopupScrollbarThumbRectForHeight(int item_count, float scroll_offset, int logical_height) {
    const auto track = PopupScrollbarTrackRectForHeight(logical_height);
    const auto& metrics = PopupMetrics();
    const float viewport_height =
        std::max(0.0f, static_cast<float>(logical_height) - PopupListTop() - 4.0f);
    const float content_height =
        item_count <= 0 ? 0.0f : static_cast<float>(item_count * metrics.card_height +
                                                    std::max(0, item_count - 1) * metrics.card_gap);
    if (content_height <= viewport_height) {
        return Rect(track.left, track.top, track.right, track.top);
    }
    const float track_height = track.Height();
    const float thumb_height = std::max(32.0f, track_height * viewport_height / content_height);
    const float max_offset = std::max(1.0f, content_height - viewport_height);
    const float clamped_offset = ClampPopupScrollOffsetForHeight(item_count, scroll_offset, logical_height);
    const float thumb_top = track.top + (track_height - thumb_height) * clamped_offset / max_offset;
    return Rect(track.left, thumb_top, track.right, thumb_top + thumb_height);
}

int PopupScrollOffsetForThumbCenterY(int item_count, float thumb_center_y) {
    return PopupScrollOffsetForThumbCenterYForHeight(item_count, thumb_center_y, kMetrics.height);
}

int PopupScrollOffsetForThumbCenterYForHeight(int item_count, float thumb_center_y, int logical_height) {
    const int visible_capacity = PopupVisibleCardCapacityForHeight(logical_height);
    if (item_count <= visible_capacity) {
        return 0;
    }
    const auto track = PopupScrollbarTrackRectForHeight(logical_height);
    const auto thumb = PopupScrollbarThumbRectForHeight(item_count, 0, logical_height);
    const float travel = std::max(1.0f, track.Height() - thumb.Height());
    const int max_offset = std::max(1, item_count - visible_capacity);
    const float top = std::clamp(thumb_center_y - thumb.Height() * 0.5f, track.top, track.bottom - thumb.Height());
    const int requested = static_cast<int>(std::lround((top - track.top) * static_cast<float>(max_offset) / travel));
    return ClampPopupScrollOffsetForHeight(item_count, requested, logical_height);
}

float PopupScrollbarThumbOpacity(bool hovered, bool dragging, float hover_progress) {
    if (dragging) {
        return 0.90f;
    }
    if (hovered) {
        return 0.56f + 0.30f * std::clamp(hover_progress, 0.0f, 1.0f);
    }
    return 0.56f;
}

UiRect PopupListClipRectForHeight(int logical_width, int logical_height) {
    return Rect(0.0f, PopupListTop(), static_cast<float>(logical_width),
                std::max(PopupListTop(), static_cast<float>(logical_height) - 4.0f));
}

float PopupExpandedCardExtraHeightForMeasuredDetail(float measured_detail_height) {
    constexpr float kBaseHeight = 102.0f;
    constexpr float kDetailVerticalPadding = 18.0f;
    return std::max(kBaseHeight, std::ceil(std::max(0.0f, measured_detail_height) + kDetailVerticalPadding));
}

float PopupExpandedImageCardExtraHeightForMeasuredDetail(float measured_detail_height) {
    constexpr float kBaseHeight = 102.0f;
    constexpr float kImagePreviewHeight = 116.0f;
    constexpr float kImageTextGap = 8.0f;
    constexpr float kDetailVerticalPadding = 18.0f;
    return std::max(kBaseHeight, std::ceil(kImagePreviewHeight + kImageTextGap +
                                           std::max(0.0f, measured_detail_height) + kDetailVerticalPadding));
}

float PopupExpandedCardExtraHeightForText(std::wstring_view text, float detail_width) {
    int line_count = 1;
    const int columns_per_line = std::max(1, static_cast<int>(std::floor(detail_width / 7.0f)));
    int column_count = 0;
    for (const wchar_t ch : text) {
        if (ch == L'\n') {
            ++line_count;
            column_count = 0;
            continue;
        }
        if (ch == L'\r') {
            continue;
        }
        const int column_width = ch <= 0x007Fu ? 1 : 2;
        column_count += column_width;
        if (column_count > columns_per_line) {
            ++line_count;
            column_count = column_width;
        }
    }

    constexpr float kBaseHeight = 102.0f;
    constexpr float kLineHeight = 20.0f;
    const int extra_lines = std::max(0, line_count - 3);
    const float estimated_extra = kBaseHeight + static_cast<float>(extra_lines) * kLineHeight;
    const float measured_extra =
        PopupExpandedCardExtraHeightForMeasuredDetail(static_cast<float>(line_count) * kLineHeight);
    return std::max(estimated_extra, measured_extra);
}

float PopupExpandedCardExtraHeightForText(std::wstring_view text) {
    const float default_detail_width = static_cast<float>(kMetrics.width - kMetrics.margin * 2 - 96);
    return PopupExpandedCardExtraHeightForText(text, default_detail_width);
}

PopupThemePalette ResolvePopupThemePalette(int theme_mode, bool system_dark) {
    const bool dark = theme_mode == 2 || (theme_mode == 0 && system_dark);
    if (dark) {
        return PopupThemePalette{
            true,
            0x101318,
            0x1A202A,
            0x202734,
            0x18202B,
            0xF3F6FA,
            0xA7B0BF,
            0x465163,
            0xAEB8C6,
            0xF87171,
            0.96f,
            0.82f,
            0.78f,
        };
    }
    return PopupThemePalette{
        false,
        0xF8FBFF,
        0xFFFFFF,
        0xFFFFFF,
        0xFFFFFF,
        0x172033,
        0x7B8798,
        0xDCE5F0,
        0x0EA5A4,
        0xE5484D,
        kMetrics.glass_tint_opacity,
        0.88f,
        0.78f,
    };
}

std::wstring_view PopupSearchPlaceholderText() {
    return L"\u641c\u7d22\u5386\u53f2\u8bb0\u5f55...";
}

std::wstring_view PopupSearchDisplayText(std::wstring_view query) {
    return query.empty() ? PopupSearchPlaceholderText() : query;
}

std::wstring PopupEmptyMessage(bool favorites_view, std::wstring_view active_favorite_group) {
    if (!favorites_view) {
        return L"\u6682\u65e0\u5386\u53f2\u8bb0\u5f55";
    }
    if (active_favorite_group.empty()) {
        return L"\u6536\u85cf\u5939\u6682\u65e0\u5185\u5bb9";
    }
    std::wstring message(active_favorite_group);
    message += L"\u6682\u65e0\u5185\u5bb9";
    return message;
}

bool PopupSearchCaretVisible(bool focused, bool blink_on) {
    return focused && blink_on;
}

bool PopupSearchAcceptsTextInput(bool focused) {
    return focused;
}

bool PopupSearchShouldUpdateImePosition(bool focused, bool updating_ime) {
    return focused && !updating_ime;
}

bool PopupSearchDeletesOnChar(wchar_t value) {
    return value == L'\b';
}

bool PopupSearchDeletesOnKeyDown(unsigned) {
    return false;
}

bool PopupSearchAppendsChar(wchar_t value) {
    return value >= 32;
}

float PopupSearchFocusProgress(bool focused, bool hovered, float hover_progress) {
    if (focused) {
        return 1.0f;
    }
    return hovered ? std::clamp(hover_progress, 0.0f, 1.0f) : 0.0f;
}

float ClampPopupSearchCaretX(const PopupSearchLayout& layout, float measured_text_width) {
    return std::clamp(layout.text.left + measured_text_width, layout.text.left, layout.text.right - 2.0f);
}

POINT PopupSearchImeAnchorDips(const PopupSearchLayout& layout, float measured_text_width) {
    return POINT{
        static_cast<LONG>(std::lround(ClampPopupSearchCaretX(layout, measured_text_width))),
        static_cast<LONG>(std::lround(layout.text.bottom)),
    };
}

float PopupSearchTop() {
    return static_cast<float>(kMetrics.header_height + 8);
}

float PopupToolbarTop() {
    return PopupSearchTop() + static_cast<float>(kMetrics.search_height) + 10.0f;
}

float PopupTabsTop() {
    return PopupToolbarTop() + static_cast<float>(kMetrics.toolbar_height);
}

float PopupListTop() {
    return PopupTabsTop() + static_cast<float>(kMetrics.tab_height) + 2.0f;
}

PopupHeaderLayout BuildPopupHeaderLayout() {
    PopupHeaderLayout layout;
    layout.title = Rect(static_cast<float>(kMetrics.margin), 10.0f, 160.0f, 38.0f);
    layout.pin = Rect(static_cast<float>(kMetrics.width - 70), 13.0f,
                      static_cast<float>(kMetrics.width - 48), 35.0f);
    layout.close = Rect(static_cast<float>(kMetrics.width - 38), 13.0f,
                        static_cast<float>(kMetrics.width - 16), 35.0f);
    return layout;
}

PopupSearchLayout BuildPopupSearchLayout() {
    const float top = PopupSearchTop();
    PopupSearchLayout layout;
    layout.box = Rect(static_cast<float>(kMetrics.margin), top,
                      static_cast<float>(kMetrics.width - kMetrics.margin),
                      top + static_cast<float>(kMetrics.search_height));
    layout.icon = Rect(static_cast<float>(kMetrics.margin + 14), top + 11.0f,
                       static_cast<float>(kMetrics.margin + 30), top + 27.0f);
    layout.text = Rect(static_cast<float>(kMetrics.margin + 42), top + 8.0f,
                       static_cast<float>(kMetrics.width - kMetrics.margin - 12), top + 31.0f);
    return layout;
}

UiRect BuildPopupHeaderPinIconRect(const PopupHeaderLayout& header) {
    return Rect(header.pin.left + 4.0f, header.pin.top + 2.5f, header.pin.right - 4.0f, header.pin.bottom - 2.5f);
}

UiRect BuildPopupHeaderCloseIconRect(const PopupHeaderLayout& header) {
    return Rect(header.close.left + 4.0f, header.close.top + 4.0f, header.close.right - 4.0f,
                header.close.bottom - 4.0f);
}

PopupToolbarLayout BuildPopupToolbarLayout(bool multi_select) {
    const float y = PopupToolbarTop();
    const float bottom = y + 30.0f;
    PopupToolbarLayout layout;
    layout.filter = Rect(static_cast<float>(kMetrics.margin), y, static_cast<float>(kMetrics.margin + 92), bottom);
    if (multi_select) {
        layout.cancel_multi_select = Rect(static_cast<float>(kMetrics.margin), y, 76.0f, bottom);
        layout.select_all = Rect(80.0f, y, 138.0f, bottom);
        layout.delete_selected = Rect(142.0f, y, 232.0f, bottom);
        layout.paste_selected = Rect(236.0f, y, static_cast<float>(kMetrics.width - kMetrics.margin), bottom);
    } else {
        layout.multi_select = Rect(160.0f, y, 224.0f, bottom);
        layout.clear_all = Rect(230.0f, y, static_cast<float>(kMetrics.width - kMetrics.margin), bottom);
    }
    return layout;
}

UiRect PopupToolbarLabelRect(const UiRect& button, bool has_chevron) {
    const bool compact = button.Width() < 94.0f;
    const float label_left = button.left + (compact ? 24.0f : 29.0f);
    const float label_right = button.right - (has_chevron ? 21.0f : 6.0f);
    return Rect(label_left, button.top + 7.0f, label_right, button.bottom - 6.0f);
}

PopupTabsLayout BuildPopupTabsLayout(bool favorites_active) {
    const float top = PopupTabsTop();
    PopupTabsLayout layout;
    layout.history = Rect(96.0f, top + 4.0f, 170.0f, top + 32.0f);
    layout.favorites = Rect(170.0f, top + 4.0f, 244.0f, top + 32.0f);
    layout.favorite_group = Rect(static_cast<float>(kMetrics.width - 74), top + 8.0f,
                                 static_cast<float>(kMetrics.width - 50), top + 32.0f);
    layout.add_favorite_phrase = Rect(static_cast<float>(kMetrics.width - 42), top + 8.0f,
                                      static_cast<float>(kMetrics.width - 18), top + 32.0f);
    const UiRect& active = favorites_active ? layout.favorites : layout.history;
    layout.active_indicator = Rect(active.left + 14.0f, top + 33.0f, active.right - 14.0f, top + 35.0f);
    layout.divider = Rect(static_cast<float>(kMetrics.margin), top + 35.0f,
                         static_cast<float>(kMetrics.width - kMetrics.margin), top + 36.0f);
    return layout;
}

UiRect PopupFavoriteGroupIconRect(const PopupTabsLayout& tabs) {
    const float center_x = (tabs.favorite_group.left + tabs.favorite_group.right) * 0.5f;
    const float center_y = (tabs.favorite_group.top + tabs.favorite_group.bottom) * 0.5f;
    return Rect(center_x - 8.0f, center_y - 8.0f, center_x + 8.0f, center_y + 8.0f);
}

PopupFilterLayout BuildPopupFilterLayout() {
    const float left = static_cast<float>(kMetrics.margin);
    const float top = PopupToolbarTop() + 34.0f;
    PopupFilterLayout layout;
    layout.panel = Rect(left, top, static_cast<float>(kMetrics.width - kMetrics.margin), top + 424.0f);
    layout.close = Rect(layout.panel.right - 34.0f, top + 12.0f, layout.panel.right - 14.0f, top + 32.0f);
    layout.type_section = Rect(left + 14.0f, top + 46.0f, layout.panel.right - 14.0f, top + 122.0f);

    const float chip_width = 132.0f;
    const float chip_height = 26.0f;
    const float chip_gap = 12.0f;
    const float chip_left = layout.type_section.left;
    const float chip_top = layout.type_section.top + 22.0f;
    layout.text_chip = Rect(chip_left, chip_top, chip_left + chip_width, chip_top + chip_height);
    layout.image_chip = Rect(chip_left + chip_width + chip_gap, chip_top,
                             chip_left + chip_width * 2.0f + chip_gap, chip_top + chip_height);
    layout.file_chip = Rect(chip_left, chip_top + chip_height + 8.0f,
                            chip_left + chip_width, chip_top + chip_height * 2.0f + 8.0f);
    layout.link_chip = Rect(chip_left + chip_width + chip_gap, chip_top + chip_height + 8.0f,
                            chip_left + chip_width * 2.0f + chip_gap, chip_top + chip_height * 2.0f + 8.0f);

    layout.date_card = Rect(left + 14.0f, top + 132.0f, layout.panel.right - 14.0f, top + 206.0f);
    const float date_box_top = layout.date_card.top + 32.0f;
    const float date_box_width = (layout.date_card.Width() - 30.0f) / 2.0f;
    layout.start_date = Rect(layout.date_card.left + 10.0f, date_box_top,
                             layout.date_card.left + 10.0f + date_box_width, date_box_top + 32.0f);
    layout.end_date = Rect(layout.start_date.right + 10.0f, date_box_top,
                           layout.start_date.right + 10.0f + date_box_width, date_box_top + 32.0f);

    layout.calendar = Rect(left + 14.0f, top + 216.0f, layout.panel.right - 14.0f, top + 390.0f);
    layout.calendar_prev = Rect(layout.calendar.left + 10.0f, layout.calendar.top + 9.0f,
                                layout.calendar.left + 30.0f, layout.calendar.top + 29.0f);
    layout.calendar_next = Rect(layout.calendar.right - 30.0f, layout.calendar.top + 9.0f,
                                layout.calendar.right - 10.0f, layout.calendar.top + 29.0f);
    layout.calendar_title = Rect(layout.calendar.left + 46.0f, layout.calendar.top + 10.0f,
                                 layout.calendar.right - 46.0f, layout.calendar.top + 29.0f);
    layout.reset = Rect(left + 16.0f, top + 398.0f, left + 110.0f, top + 414.0f);
    layout.done = Rect(layout.panel.right - 88.0f, top + 394.0f, layout.panel.right - 16.0f, top + 416.0f);
    return layout;
}

size_t PopupFavoriteGroupMenuVisibleGroupCount(size_t group_count) {
    return std::min<size_t>(group_count, 8);
}

PopupFavoriteGroupMenuLayout BuildPopupFavoriteGroupMenuLayout(size_t group_count) {
    constexpr float kPanelWidth = 184.0f;
    constexpr float kPanelPadding = 8.0f;
    constexpr float kRowHeight = 34.0f;
    constexpr float kDividerGap = 8.0f;

    const float right = static_cast<float>(kMetrics.width - kMetrics.margin);
    const float left = right - kPanelWidth;
    const float top = PopupTabsTop() + 34.0f;
    const size_t visible_groups = PopupFavoriteGroupMenuVisibleGroupCount(group_count);
    const float groups_height = static_cast<float>(visible_groups) * kRowHeight;
    const float panel_height = kPanelPadding * 2.0f + kRowHeight + kDividerGap + groups_height + kDividerGap + kRowHeight;

    PopupFavoriteGroupMenuLayout layout;
    layout.panel = Rect(left, top, right, top + panel_height);
    layout.all_favorites = Rect(left + kPanelPadding, top + kPanelPadding,
                                right - kPanelPadding, top + kPanelPadding + kRowHeight);
    layout.first_divider = Rect(left + kPanelPadding, layout.all_favorites.bottom + 3.0f,
                                right - kPanelPadding, layout.all_favorites.bottom + 4.0f);
    layout.group_rows = Rect(left + kPanelPadding, layout.all_favorites.bottom + kDividerGap,
                             right - kPanelPadding, layout.all_favorites.bottom + kDividerGap + groups_height);
    layout.second_divider = Rect(left + kPanelPadding, layout.group_rows.bottom + 3.0f,
                                 right - kPanelPadding, layout.group_rows.bottom + 4.0f);
    layout.new_group = Rect(left + kPanelPadding, layout.group_rows.bottom + kDividerGap,
                            right - kPanelPadding, layout.group_rows.bottom + kDividerGap + kRowHeight);
    return layout;
}

UiRect PopupFavoriteGroupMenuGroupRect(const PopupFavoriteGroupMenuLayout& layout, size_t index) {
    constexpr float kRowHeight = 34.0f;
    const float top = layout.group_rows.top + static_cast<float>(index) * kRowHeight;
    return Rect(layout.group_rows.left, top, layout.group_rows.right, top + kRowHeight);
}

UiRect PopupFavoriteGroupMenuDeleteRect(const UiRect& group_row) {
    const float center_y = (group_row.top + group_row.bottom) * 0.5f;
    return Rect(group_row.right - 31.0f, center_y - 10.0f, group_row.right - 11.0f, center_y + 10.0f);
}

PopupFavoriteGroupMenuHit HitTestPopupFavoriteGroupMenu(const PopupFavoriteGroupMenuLayout& layout,
                                                        size_t group_count, float x, float y) {
    if (!Contains(layout.panel, x, y)) {
        return {};
    }
    if (Contains(layout.all_favorites, x, y)) {
        return PopupFavoriteGroupMenuHit{PopupFavoriteGroupMenuTarget::AllFavorites, 0};
    }
    const size_t visible_groups = PopupFavoriteGroupMenuVisibleGroupCount(group_count);
    for (size_t index = 0; index < visible_groups; ++index) {
        const auto row = PopupFavoriteGroupMenuGroupRect(layout, index);
        if (Contains(PopupFavoriteGroupMenuDeleteRect(row), x, y)) {
            return PopupFavoriteGroupMenuHit{PopupFavoriteGroupMenuTarget::DeleteGroup, index};
        }
        if (Contains(row, x, y)) {
            return PopupFavoriteGroupMenuHit{PopupFavoriteGroupMenuTarget::Group, index};
        }
    }
    if (Contains(layout.new_group, x, y)) {
        return PopupFavoriteGroupMenuHit{PopupFavoriteGroupMenuTarget::NewGroup, 0};
    }
    return PopupFavoriteGroupMenuHit{PopupFavoriteGroupMenuTarget::Panel, 0};
}

UiRect PopupFilterResetVisualRect(const PopupFilterLayout& layout) {
    return Rect(layout.reset.left, layout.reset.top - 2.0f, layout.reset.left + 46.0f, layout.reset.bottom + 2.0f);
}

UiRect PopupFilterArrowGlyphRect(const UiRect& arrow_button) {
    const float center_x = (arrow_button.left + arrow_button.right) * 0.5f;
    const float center_y = (arrow_button.top + arrow_button.bottom) * 0.5f;
    return Rect(center_x - 5.0f, center_y - 6.0f, center_x + 5.0f, center_y + 6.0f);
}

std::vector<PopupCalendarWeekdayLabel> BuildPopupCalendarWeekdayLabels(const PopupFilterLayout& layout) {
    constexpr wchar_t kWeekdays[] = {L'\u65e5', L'\u4e00', L'\u4e8c', L'\u4e09', L'\u56db', L'\u4e94', L'\u516d'};
    std::vector<PopupCalendarWeekdayLabel> labels;
    labels.reserve(7);
    const float cell_width = layout.calendar.Width() / 7.0f;
    for (int i = 0; i < 7; ++i) {
        const float left = layout.calendar.left + static_cast<float>(i) * cell_width;
        labels.push_back(PopupCalendarWeekdayLabel{
            kWeekdays[i],
            Rect(left, layout.calendar.top + 35.0f, left + cell_width, layout.calendar.top + 51.0f),
        });
    }
    return labels;
}

PopupCardLayout BuildPopupCardLayout(bool multi_select, float top) {
    const float left = static_cast<float>(kMetrics.margin);
    const float right = static_cast<float>(kMetrics.width - kMetrics.margin);
    const float bottom = top + static_cast<float>(kMetrics.card_height);
    const float title_right = right - 78.0f;
    const float icon_left = multi_select ? left + 42.0f : left + 16.0f;

    PopupCardLayout layout;
    layout.card = Rect(left, top, right, bottom);
    layout.stripe = Rect(icon_left, top + 17.0f, icon_left + 20.0f, top + 37.0f);
    layout.image_preview = Rect(icon_left - 9.0f, top + 5.0f, icon_left + 53.0f, top + 67.0f);
    layout.file_icon = Rect(icon_left - 1.0f, top + 13.0f, icon_left + 43.0f, top + 57.0f);
    layout.checkbox = Rect(left + 16.0f, top + 26.0f, left + 30.0f, top + 40.0f);
    const float text_left = multi_select ? left + 104.0f : left + 80.0f;
    layout.title = Rect(text_left, top + 12.0f, title_right, top + 35.0f);
    layout.meta = Rect(text_left, top + 41.0f, right - 18.0f, top + 60.0f);
    layout.time = Rect(right - 70.0f, top + 13.0f, right - 34.0f, top + 31.0f);
    layout.menu = Rect(right - 34.0f, top + 11.0f, right - 12.0f, top + 33.0f);
    layout.expand = Rect(right - 30.0f, top + 36.0f, right - 8.0f, top + 58.0f);
    return layout;
}

UiRect PopupCardKindIconRect(const PopupCardLayout& card) {
    const float center_x = (card.stripe.left + card.stripe.right) * 0.5f;
    const float center_y = (card.stripe.top + card.stripe.bottom) * 0.5f;
    return Rect(center_x - 8.0f, center_y - 8.0f, center_x + 8.0f, center_y + 8.0f);
}

float PopupExpandedCardExtraHeight(bool expanded) {
    return expanded ? 102.0f : 0.0f;
}

int HitTestPopupCardIndex(int item_count, int scroll_offset, std::optional<int64_t> expanded_item_id,
                          const std::vector<int64_t>& visible_item_ids, float x, float y) {
    if (item_count <= 0 || y < PopupListTop() || Contains(PopupScrollbarHitRect(), x, y)) {
        return -1;
    }

    const int visible_capacity = PopupVisibleCardCapacity();
    float top = PopupListTop();
    for (int row = 0; row < visible_capacity && scroll_offset + row < item_count; ++row) {
        const int item_index = scroll_offset + row;
        const bool expanded = item_index < static_cast<int>(visible_item_ids.size()) && expanded_item_id &&
                              visible_item_ids[static_cast<size_t>(item_index)] == *expanded_item_id;
        const auto card = BuildPopupCardLayout(false, top);
        const float bottom = card.card.bottom + PopupExpandedCardExtraHeight(expanded);
        if (x >= card.card.left && x <= card.card.right && y >= card.card.top && y <= bottom) {
            return item_index;
        }
        if (expanded) {
            break;
        }
        top = bottom + static_cast<float>(kMetrics.card_gap);
        if (top > static_cast<float>(kMetrics.height)) {
            break;
        }
    }
    return -1;
}

int HitTestPopupCardExpandIndex(int item_count, int scroll_offset, std::optional<int64_t> expanded_item_id,
                                const std::vector<int64_t>& visible_item_ids, float x, float y) {
    if (item_count <= 0 || y < PopupListTop() || Contains(PopupScrollbarHitRect(), x, y)) {
        return -1;
    }

    const int visible_capacity = PopupVisibleCardCapacity();
    float top = PopupListTop();
    for (int row = 0; row < visible_capacity && scroll_offset + row < item_count; ++row) {
        const int item_index = scroll_offset + row;
        const bool expanded = item_index < static_cast<int>(visible_item_ids.size()) && expanded_item_id &&
                              visible_item_ids[static_cast<size_t>(item_index)] == *expanded_item_id;
        const auto card = BuildPopupCardLayout(false, top);
        if (Contains(card.expand, x, y)) {
            return item_index;
        }
        if (expanded) {
            break;
        }
        top = card.card.bottom + PopupExpandedCardExtraHeight(expanded) + static_cast<float>(kMetrics.card_gap);
        if (top > static_cast<float>(kMetrics.height)) {
            break;
        }
    }
    return -1;
}

UiRect FitImageRectToBounds(float source_width, float source_height, const UiRect& bounds) {
    if (source_width <= 0.0f || source_height <= 0.0f || bounds.Width() <= 0.0f || bounds.Height() <= 0.0f) {
        return bounds;
    }
    const float scale = std::min(bounds.Width() / source_width, bounds.Height() / source_height);
    const float width = source_width * scale;
    const float height = source_height * scale;
    const float left = bounds.left + (bounds.Width() - width) * 0.5f;
    const float top = bounds.top + (bounds.Height() - height) * 0.5f;
    return Rect(left, top, left + width, top + height);
}

bool RectsOverlap(const UiRect& a, const UiRect& b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

POINT ClampPopupToWorkArea(POINT anchor, SIZE size, RECT work, unsigned dpi) {
    const int gap = ScalePopupMetricForDpi(10, dpi);
    int x = anchor.x + gap;
    int y = anchor.y + gap;
    if (x + size.cx > work.right) {
        x = anchor.x - size.cx - gap;
    }
    if (y + size.cy > work.bottom) {
        y = anchor.y - size.cy - gap;
    }

    return POINT{
        ClampToRange(x, work.left + gap, work.right - size.cx - gap),
        ClampToRange(y, work.top + gap, work.bottom - size.cy - gap),
    };
}

POINT PopupBottomRightFallback(SIZE size, RECT work, unsigned dpi) {
    return POINT{
        work.right - size.cx - ScalePopupMetricForDpi(32, dpi),
        work.bottom - size.cy - ScalePopupMetricForDpi(56, dpi),
    };
}

POINT CenterWindowInWorkArea(SIZE size, RECT work) {
    return POINT{
        work.left + (work.right - work.left - size.cx) / 2,
        work.top + (work.bottom - work.top - size.cy) / 2,
    };
}

int PopupHoverItemIndex(bool filter_open, int hit_item_index) {
    return filter_open ? -1 : hit_item_index;
}

bool ShouldHidePopupAfterPaste(bool pinned_open) {
    return !pinned_open;
}

bool ShouldHidePopupAfterContinuousPaste(bool) {
    return false;
}

bool ShouldHidePopupAfterOutsideClick(bool pinned_open, bool prompt_open, bool moving_window,
                                      bool mouse_down_started_inside_popup, bool transient_hide_suppressed,
                                      bool visible, bool new_mouse_press, bool click_inside_popup) {
    return visible && !pinned_open && !prompt_open && !moving_window && !mouse_down_started_inside_popup &&
           !transient_hide_suppressed && new_mouse_press && !click_inside_popup;
}

bool ShouldHidePopupAfterInactive(bool pinned_open, bool prompt_open, bool moving_window,
                                  bool transient_hide_suppressed, bool visible, bool next_active_inside_popup) {
    return visible && !pinned_open && !prompt_open && !moving_window && !transient_hide_suppressed &&
           !next_active_inside_popup;
}

bool PopupPointerInteractionSuppressesInactiveHide(bool moving_window, bool resizing_window,
                                                   bool mouse_down_started_inside_popup,
                                                   bool left_button_was_down) {
    return moving_window || resizing_window || mouse_down_started_inside_popup || left_button_was_down;
}

PopupItemPressReleaseAction PopupItemPressReleaseActionFor(bool same_item, bool long_press_selected) {
    if (!same_item) {
        return PopupItemPressReleaseAction::None;
    }
    return long_press_selected ? PopupItemPressReleaseAction::SelectOnly : PopupItemPressReleaseAction::Paste;
}

PopupItemPressMoveAction PopupItemPressMoveActionFor(bool long_press_selected, bool moved_past_cancel_distance,
                                                     int hit_item_index) {
    if (long_press_selected) {
        return hit_item_index >= 0 ? PopupItemPressMoveAction::SelectHitItem
                                   : PopupItemPressMoveAction::KeepPress;
    }
    return moved_past_cancel_distance ? PopupItemPressMoveAction::CancelPress : PopupItemPressMoveAction::KeepPress;
}

std::optional<int> PopupSelectionIndexWhileLongPressing(bool long_press_selected, int hit_item_index) {
    if (!long_press_selected || hit_item_index < 0) {
        return std::nullopt;
    }
    return hit_item_index;
}

std::wstring_view PopupFavoriteMenuLabel(bool is_favorite) {
    return is_favorite ? L"取消收藏" : L"收藏";
}

std::wstring_view PopupPinMenuLabel(bool is_pinned) {
    return is_pinned ? L"取消置顶" : L"置顶";
}

bool IsPopupHeaderDragArea(float x, float y) {
    if (y < 0.0f || y > static_cast<float>(kMetrics.header_height)) {
        return false;
    }
    const auto header = BuildPopupHeaderLayout();
    return !Contains(header.pin, x, y) && !Contains(header.close, x, y);
}

PopupIconAssetSlot PopupPinIconSlot(bool pinned_open) {
    return pinned_open ? PopupIconAssetSlot::Pin : PopupIconAssetSlot::PinActive;
}

PopupIconAssetSlot PopupFilterIconSlot(bool filter_open) {
    return filter_open ? PopupIconAssetSlot::Filter : PopupIconAssetSlot::FilterActive;
}

PopupIconAssetSlot PopupMultiSelectIconSlot(bool active) {
    return active ? PopupIconAssetSlot::MultiSelectActive : PopupIconAssetSlot::MultiSelect;
}

PopupIconAssetSlot PopupPasteSelectedIconSlot() {
    return PopupIconAssetSlot::MultiSelectActive;
}

PopupFavoriteFolderIconSlot PopupFavoriteFolderIconSlotForGroup(bool active) {
    return active ? PopupFavoriteFolderIconSlot::Filled : PopupFavoriteFolderIconSlot::Outline;
}

std::vector<PopupCalendarCell> BuildPopupCalendarCells(const PopupFilterLayout& layout, int year, int month) {
    std::vector<PopupCalendarCell> cells;
    if (month < 1 || month > 12) {
        return cells;
    }

    const float cell_width = layout.calendar.Width() / 7.0f;
    const float cell_height = 18.0f;
    const float grid_top = layout.calendar.top + 54.0f;
    const int first_weekday = WeekdaySundayFirst(year, month, 1);
    const int days = DaysInMonth(year, month);
    cells.reserve(static_cast<size_t>(days));

    for (int day = 1; day <= days; ++day) {
        const int zero_based_cell = first_weekday + day - 1;
        const int row = zero_based_cell / 7;
        const int col = zero_based_cell % 7;
        const float left = layout.calendar.left + col * cell_width;
        const float top = grid_top + row * cell_height;
        cells.push_back(PopupCalendarCell{PopupCalendarDate{year, month, day},
                                          Rect(left, top, left + cell_width, top + cell_height)});
    }
    return cells;
}

std::optional<PopupCalendarDate> HitTestPopupCalendarDate(const PopupFilterLayout& layout, int year, int month,
                                                          float x, float y) {
    for (const auto& cell : BuildPopupCalendarCells(layout, year, month)) {
        if (Contains(cell.rect, x, y)) {
            return cell.date;
        }
    }
    return std::nullopt;
}

PopupCalendarArrow HitTestPopupCalendarArrow(const PopupFilterLayout& layout, float x, float y) {
    if (Contains(layout.calendar_prev, x, y)) {
        return PopupCalendarArrow::PreviousMonth;
    }
    if (Contains(layout.calendar_next, x, y)) {
        return PopupCalendarArrow::NextMonth;
    }
    return PopupCalendarArrow::None;
}

std::optional<PopupDateRangeField> HitTestPopupDateRangeField(const PopupFilterLayout& layout, float x, float y) {
    if (Contains(layout.start_date, x, y)) {
        return PopupDateRangeField::Start;
    }
    if (Contains(layout.end_date, x, y)) {
        return PopupDateRangeField::End;
    }
    return std::nullopt;
}

PopupFilterTarget HitTestPopupFilterTarget(const PopupFilterLayout& layout, int year, int month, float x, float y) {
    if (!Contains(layout.panel, x, y)) {
        return PopupFilterTarget::None;
    }
    if (Contains(layout.close, x, y)) return PopupFilterTarget::Close;
    if (Contains(layout.text_chip, x, y)) return PopupFilterTarget::TextChip;
    if (Contains(layout.image_chip, x, y)) return PopupFilterTarget::ImageChip;
    if (Contains(layout.file_chip, x, y)) return PopupFilterTarget::FileChip;
    if (Contains(layout.link_chip, x, y)) return PopupFilterTarget::LinkChip;
    if (Contains(layout.start_date, x, y)) return PopupFilterTarget::StartDate;
    if (Contains(layout.end_date, x, y)) return PopupFilterTarget::EndDate;
    if (Contains(layout.calendar_prev, x, y)) return PopupFilterTarget::CalendarPrevious;
    if (Contains(layout.calendar_next, x, y)) return PopupFilterTarget::CalendarNext;
    if (HitTestPopupCalendarDate(layout, year, month, x, y)) return PopupFilterTarget::CalendarDate;
    if (Contains(layout.reset, x, y)) return PopupFilterTarget::Reset;
    if (Contains(layout.done, x, y)) return PopupFilterTarget::Done;
    return PopupFilterTarget::Panel;
}

void SelectPopupDateRangeDate(PopupDateRangeState& state, PopupCalendarDate date) {
    if (state.active_field == PopupDateRangeField::Start) {
        state.start = date;
        if (state.end && *state.end < date) {
            state.end.reset();
        }
        state.active_field = PopupDateRangeField::End;
        return;
    }

    state.end = date;
    if (state.start && date < *state.start) {
        std::swap(state.start, state.end);
    }
    state.active_field = PopupDateRangeField::Start;
}

} // namespace ClipSoul
