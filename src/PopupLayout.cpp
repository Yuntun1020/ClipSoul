#include "ClipSoul/PopupLayout.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string_view>

namespace ClipSoul {
namespace {
constexpr PopupMetricsData kMetrics{
    340, // width
    560, // height
    16,  // margin
    42,  // header_height
    44,  // search_height
    30,  // toolbar_height
    34,  // tab_height
    72,  // card_height
    8,   // card_gap
    18,  // corner_radius
    28,  // header_button_size
    16,  // toolbar_icon_size
    1.0f,
};
constexpr PopupDesignTokenData kDesignTokens{};
constexpr int kMinTextRangeHeight = 2;

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

const PopupDesignTokenData& PopupDesignTokens() {
    return kDesignTokens;
}

int PopupItemLongPressMilliseconds() {
    return 200;
}

int ScalePopupMetricForDpi(int value, unsigned dpi) {
    if (dpi == 0) {
        dpi = 96;
    }
    return static_cast<int>((static_cast<long long>(value) * dpi + 48) / 96);
}

int PopupWindowRegionCornerDiameterForDpi(unsigned dpi) {
    return PopupWindowShouldUseHardRoundedRegion()
               ? ScalePopupMetricForDpi(static_cast<int>(std::lround(PopupWindowEdgeCornerRadius() * 2.0f)), dpi)
               : 0;
}

bool PopupWindowShouldUseHardRoundedRegion() {
    return false;
}

UiRect PopupWindowEdgeStrokeRectForSize(int logical_width, int logical_height) {
    const float right = std::max(0.5f, static_cast<float>(logical_width) - 0.5f);
    const float bottom = std::max(0.5f, static_cast<float>(logical_height) - 0.5f);
    return Rect(0.5f, 0.5f, right, bottom);
}

bool PopupWindowEdgeShouldDrawCornerArcs() {
    return true;
}

float PopupWindowEdgeCornerRadius() {
    return 22.0f;
}

bool PopupWindowShouldDrawClientEdge() {
    return false;
}

bool PopupWindowShouldUseDwmAntialiasedFrame() {
    return true;
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

PopupContinuousPasteStep PopupContinuousPasteSelectionStep(int item_count, int selected_index) {
    const int paste_index = ClampPopupSelectedIndex(item_count, selected_index);
    return PopupContinuousPasteStep{
        paste_index,
        PopupNextSelectedIndex(item_count, paste_index),
    };
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

float PopupScrollOffsetToRevealRange(float current_offset, float range_top, float range_bottom,
                                     float content_height, float viewport_height) {
    const float max_offset = std::max(0.0f, content_height - viewport_height);
    float offset = std::clamp(current_offset, 0.0f, max_offset);
    if (range_top < offset) {
        offset = range_top;
    }
    if (range_bottom > offset + viewport_height) {
        offset = range_bottom - viewport_height;
    }
    return std::clamp(offset, 0.0f, max_offset);
}

UiRect PopupScrollbarTrackRect() {
    return PopupScrollbarTrackRectForHeight(kMetrics.height);
}

UiRect PopupScrollbarTrackRectForHeight(int logical_height) {
    return PopupScrollbarTrackRectForSize(kMetrics.width, logical_height);
}

UiRect PopupScrollbarTrackRectForSize(int logical_width, int logical_height) {
    return Rect(static_cast<float>(logical_width - 14), PopupListTop(),
                static_cast<float>(logical_width - 8), static_cast<float>(logical_height - 10));
}

UiRect PopupScrollbarHitRect() {
    return PopupScrollbarHitRectForHeight(kMetrics.height);
}

UiRect PopupScrollbarHitRectForHeight(int logical_height) {
    return PopupScrollbarHitRectForSize(kMetrics.width, logical_height);
}

UiRect PopupScrollbarHitRectForSize(int logical_width, int logical_height) {
    const auto track = PopupScrollbarTrackRectForSize(logical_width, logical_height);
    return Rect(track.left - 8.0f, track.top, static_cast<float>(logical_width), track.bottom);
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
    return PopupScrollbarThumbRectForSize(item_count, scroll_offset, kMetrics.width, logical_height);
}

UiRect PopupScrollbarThumbRectForSize(int item_count, float scroll_offset, int logical_width, int logical_height) {
    const auto track = PopupScrollbarTrackRectForSize(logical_width, logical_height);
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

bool PopupScrollToTopButtonVisible(float scroll_offset) {
    return scroll_offset > 0.0f;
}

UiRect PopupScrollToTopButtonRectForSize(int logical_width, int logical_height) {
    constexpr float kButtonSize = 40.0f;
    constexpr float kBottomInset = 22.0f;
    constexpr float kScrollbarGap = 12.0f;
    const auto scrollbar_hit = PopupScrollbarHitRectForSize(logical_width, logical_height);
    const float right = scrollbar_hit.left - kScrollbarGap;
    const float bottom = static_cast<float>(logical_height) - kBottomInset;
    return Rect(right - kButtonSize, bottom - kButtonSize, right, bottom);
}

UiRect PopupListClipRectForHeight(int logical_width, int logical_height) {
    return Rect(0.0f, PopupListTop(), static_cast<float>(logical_width),
                std::max(PopupListTop(), static_cast<float>(logical_height) - 4.0f));
}

float PopupMotionProgress(float raw_progress) {
    const float clamped = std::clamp(raw_progress, 0.0f, 1.0f);
    const float inverse = 1.0f - clamped;
    return 1.0f - inverse * inverse * inverse * inverse;
}

float PopupMotionEnterOffset(float progress, float max_offset) {
    return std::max(0.0f, max_offset) * (1.0f - std::clamp(progress, 0.0f, 1.0f));
}

float PopupViewSwitchListOffset(int direction, float raw_progress, float max_offset) {
    const float sign = direction < 0 ? -1.0f : 1.0f;
    return sign * PopupMotionEnterOffset(PopupMotionProgress(raw_progress), max_offset);
}

UiOffset PopupMotionEnterOffsetFromTrigger(const UiRect& trigger, const UiRect& panel,
                                           float progress, float max_offset) {
    const float remaining = PopupMotionEnterOffset(progress, max_offset);
    if (remaining <= 0.0f) {
        return UiOffset{};
    }
    const float trigger_x = (trigger.left + trigger.right) * 0.5f;
    const float trigger_y = (trigger.top + trigger.bottom) * 0.5f;
    const float anchor_x = std::clamp(trigger_x, panel.left, panel.right);
    const float anchor_y = std::clamp(trigger_y, panel.top, panel.bottom);
    const float dx = trigger_x - anchor_x;
    const float dy = trigger_y - anchor_y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.01f) {
        return UiOffset{0.0f, -remaining};
    }
    return UiOffset{dx / length * remaining, dy / length * remaining};
}

UiOffset PopupMotionExitOffsetFromTrigger(const UiRect& trigger, const UiRect& panel,
                                          float raw_progress, float max_offset) {
    (void)trigger;
    (void)panel;
    (void)raw_progress;
    (void)max_offset;
    return UiOffset{};
}

float PopupPopoverExitOpacity(float raw_progress) {
    const float t = std::clamp(raw_progress, 0.0f, 1.0f);
    const float smooth = t * t * (3.0f - 2.0f * t);
    return 1.0f - smooth;
}

UiRect PopupTabIndicatorRectForMotion(const UiRect& from, const UiRect& to, float progress) {
    const float t = std::clamp(progress, 0.0f, 1.0f);
    const auto lerp = [t](float a, float b) {
        return a + (b - a) * t;
    };
    return Rect(lerp(from.left, to.left), lerp(from.top, to.top),
                lerp(from.right, to.right), lerp(from.bottom, to.bottom));
}

float PopupMultiSelectToolbarOffset(float progress, bool entering) {
    constexpr float kOffset = 18.0f;
    const float t = std::clamp(progress, 0.0f, 1.0f);
    return entering ? PopupMotionEnterOffset(t, kOffset) : 0.0f;
}

float PopupMultiSelectCheckboxOffset(float progress, bool entering) {
    constexpr float kOffset = -18.0f;
    const float t = std::clamp(progress, 0.0f, 1.0f);
    return entering ? kOffset * (1.0f - t) : kOffset * t;
}

float PopupMultiSelectCheckboxOpacity(float progress, bool entering) {
    const float t = std::clamp(progress, 0.0f, 1.0f);
    return entering ? t : 1.0f - t;
}

float PopupToggleKnobPosition(bool from_active, bool to_active, float progress) {
    const float from = from_active ? 1.0f : 0.0f;
    const float to = to_active ? 1.0f : 0.0f;
    return from + (to - from) * PopupMotionProgress(progress);
}

bool PopupPromptShouldUseSlideAnimation() {
    return false;
}

bool PopupPromptShouldUseBlendAnimation() {
    return false;
}

bool PopupPromptShouldUsePositionNudgeAnimation() {
    return true;
}

int PopupPromptEntranceOffsetPixels() {
    return 6;
}

int PopupPromptEntranceStepCount() {
    return 7;
}

int PopupPromptEntranceTimerIntervalMs() {
    return 16;
}

float PopupMotionEnterOpacity(float progress) {
    constexpr float kStartOpacity = 0.84f;
    return kStartOpacity + (1.0f - kStartOpacity) * std::clamp(progress, 0.0f, 1.0f);
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
            0x151412,
            0x1F1C18,
            0x28231C,
            0x1C1A16,
            0xEEE9DF,
            0xAAA397,
            0x5A5348,
            0x8FA08B,
            0x6D665A,
            0xA8B7A4,
            0x81786A,
            0xC4685E,
            0x302D28,
            0x39342D,
            0x6D665A,
            0x8A7F6E,
            0xA8B7A4,
            0x2B2823,
            0x737D6D,
            0x8FA08B,
            0x8FA08B,
            1.0f,
            1.0f,
            1.0f,
        };
    }
    return PopupThemePalette{
        false,
        0xF6F1E8,
        0xFBF8F2,
        0xFBF8F2,
        0xFBF8F2,
        0x211E19,
        0x69645B,
        0xD8D0C5,
        0x73826B,
        0xB9A995,
        0x66725F,
        0x8D867C,
        0xA95148,
        0xF0E7D9,
        0xECE4D7,
        0xCFC5B8,
        0xB9A995,
        0x69745C,
        0xEFE8DD,
        0xA7B09F,
        0x66725F,
        0x73826B,
        1.0f,
        1.0f,
        1.0f,
    };
}

WindowMaterialPolicy ResolveWindowMaterialPolicy() {
    return WindowMaterialPolicy{
        false,
        false,
        true,
        true,
    };
}

uint32_t PopupDarkUiIconTintColor(const PopupThemePalette& palette) {
    return palette.dark ? palette.muted : palette.text;
}

uint32_t PopupToolbarLabelColor(const PopupThemePalette& palette, bool active, bool danger) {
    if (danger) {
        return palette.danger;
    }
    if (!palette.dark) {
        return active ? palette.text : palette.muted;
    }
    return active ? palette.accent_hover : palette.muted;
}

bool PopupUiIconShouldFallbackToOriginalBitmap(const PopupThemePalette& palette, bool content_icon) {
    return !palette.dark || content_icon;
}

bool PopupUiIconShouldLoadBitmap(const PopupThemePalette& palette, bool content_icon, bool fast_interaction) {
    return !fast_interaction || (palette.dark && !content_icon);
}

SettingsProjectLayout SettingsWindowProjectLayout() {
    return SettingsProjectLayout{
        Rect(30.0f, 458.0f, 604.0f, 576.0f),
        Rect(54.0f, 500.0f, 222.0f, 536.0f),
        Rect(44.0f, 544.0f, 592.0f, 545.0f),
        Rect(54.0f, 550.0f, 592.0f, 570.0f),
    };
}

SettingsControlLayout SettingsWindowControlLayout() {
    return SettingsControlLayout{
        Rect(170.0f, 83.0f, 268.0f, 105.0f),
        Rect(236.0f, 120.0f, 274.0f, 142.0f),
        Rect(236.0f, 154.0f, 274.0f, 176.0f),
        Rect(225.0f, 394.0f, 263.0f, 416.0f),
        Rect(150.0f, 356.0f, 250.0f, 384.0f),
        Rect(258.0f, 356.0f, 340.0f, 384.0f),
        Rect(348.0f, 356.0f, 430.0f, 384.0f),
        1,
        false,
        false,
    };
}

SettingsThemeTarget HitTestSettingsThemeTarget(const SettingsControlLayout& layout, float x, float y) {
    if (Contains(layout.theme_system, x, y)) {
        return SettingsThemeTarget::System;
    }
    if (Contains(layout.theme_light, x, y)) {
        return SettingsThemeTarget::Light;
    }
    if (Contains(layout.theme_dark, x, y)) {
        return SettingsThemeTarget::Dark;
    }
    return SettingsThemeTarget::None;
}

bool ThemeChangeShouldRefreshPalette(int theme_mode) {
    return std::clamp(theme_mode, 0, 2) == 0;
}

bool SettingsThemePreviewShouldRefreshNativeControls(int previous_mode, int next_mode) {
    return std::clamp(previous_mode, 0, 2) != std::clamp(next_mode, 0, 2);
}

bool ThemeModeResolvesDarkChrome(int theme_mode, bool system_dark) {
    const int mode = std::clamp(theme_mode, 0, 2);
    if (mode == 1) {
        return false;
    }
    if (mode == 2) {
        return true;
    }
    return system_dark;
}

bool SettingsLimitEditShouldSelectAllOnLoad() {
    return false;
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

std::wstring PopupNotePreviewText(std::wstring_view note) {
    constexpr size_t kMaxNotePreviewLength = 13;
    std::wstring preview;
    preview.reserve(std::min(note.size(), kMaxNotePreviewLength));
    bool pending_space = false;
    bool truncated = false;
    for (const wchar_t ch : note) {
        const bool line_break = ch == L'\r' || ch == L'\n' || ch == L'\u2028' || ch == L'\u2029';
        const bool whitespace = line_break || ch == L'\t' || ch == L' ';
        if (whitespace) {
            pending_space = !preview.empty();
            continue;
        }
        if (pending_space && !preview.empty() && preview.size() < kMaxNotePreviewLength) {
            preview.push_back(L' ');
        }
        pending_space = false;
        if (preview.size() >= kMaxNotePreviewLength) {
            truncated = true;
            break;
        }
        preview.push_back(ch);
    }
    if (truncated) {
        preview += L"...";
    }
    return preview;
}

bool PopupFileCanUseImagePreview(std::wstring_view path) {
    const size_t slash = path.find_last_of(LR"(\/)");
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring_view::npos || dot + 1 >= path.size() ||
        (slash != std::wstring_view::npos && dot < slash)) {
        return false;
    }

    wchar_t extension[6]{};
    const size_t length = std::min<size_t>(path.size() - dot - 1, std::size(extension) - 1);
    for (size_t index = 0; index < length; ++index) {
        extension[index] = static_cast<wchar_t>(std::towlower(path[dot + 1 + index]));
    }

    const std::wstring_view ext(extension, length);
    return ext == L"png" || ext == L"jpg" || ext == L"jpeg" || ext == L"bmp" || ext == L"gif" ||
           ext == L"tif" || ext == L"tiff" || ext == L"webp";
}

bool PopupSearchCaretVisible(bool focused, bool blink_on) {
    return focused && blink_on;
}


bool PopupSearchHasComposition(bool composing, const std::wstring& composition_text) {
    return composing && !composition_text.empty();
}

std::wstring PopupSearchCompositionDisplayText(std::wstring_view query, bool composing, const std::wstring& composition_text) {
    if (composing && !composition_text.empty()) {
        return std::wstring(query) + composition_text;
    }
    return std::wstring(PopupSearchDisplayText(query));
}

bool PopupSearchCompositionTextColor(bool has_composition, std::wstring_view query) {
    return has_composition || !query.empty();
}

bool PopupSearchCaretVisibleDuringComposition(bool has_composition) {
    return !has_composition;
}


bool PopupResizeShouldDiscardDeviceResources() {
    return false;
}

bool PopupDpiChangeShouldDiscardDeviceResources() {
    return true;
}

bool PopupPaintShouldUpdateLayout() {
    return false;
}

bool PopupShouldAnimateHoverWhileResizing(bool resizing_window) {
    return !resizing_window;
}

bool PopupShouldActivateWhenShown(bool keyboard_invocation) {
    return !keyboard_invocation;
}

bool PopupShouldActivateForShellSurface(bool shell_surface) {
    return shell_surface;
}

bool PopupShowShouldActivate(bool keyboard_invocation, bool shell_surface) {
    return PopupShouldActivateForShellSurface(shell_surface) || PopupShouldActivateWhenShown(keyboard_invocation);
}

bool PopupSetWindowPosShouldUseNoActivate(bool activate_window) {
    return !activate_window;
}

LRESULT PopupMouseActivateResult() {
    return MA_NOACTIVATE;
}

bool PopupShouldFocusWindowForPointerPress(bool search_clicked) {
    return search_clicked;
}

bool PopupShouldActivateForSearchFocus(bool search_clicked, bool has_native_edit) {
    return search_clicked && has_native_edit;
}


bool PopupShouldAutoFocusSearchOnShow(bool has_native_edit) {
    return false;
}

bool PopupWindowShouldUseNoActivateStyle() {
    return true;
}

bool PopupWindowShouldUseNoActivateStyle(bool activate_on_show) {
    return !activate_on_show && PopupWindowShouldUseNoActivateStyle();
}

bool PopupNativeSearchCueBannerShowsWhenFocused() {
    return true;
}


bool PopupShouldRedrawNativeSearchAfterParentPaint(bool has_native_edit, bool moving_or_resizing_window) {
    return has_native_edit && !moving_or_resizing_window;
}

bool PopupSearchShouldDrawSelection(bool focused, bool has_query_text, PopupSearchSelectionRange selection) {
    return focused && has_query_text && PopupSearchHasSelection(selection);
}

bool PopupShouldDrawDecorativeShadows(bool moving_or_resizing_window) {
    return !moving_or_resizing_window;
}

bool PopupShouldLoadUiIcon(bool moving_or_resizing_window) {
    return !moving_or_resizing_window;
}

bool PopupIsFastMediaInteraction(bool moving_window, bool resizing_window, bool view_switch_motion) {
    return moving_window || resizing_window || view_switch_motion;
}

bool PopupShouldLoadImagePreview(bool moving_or_resizing_window) {
    return !moving_or_resizing_window;
}

bool PopupShouldLoadFileIcon(bool moving_or_resizing_window) {
    return !moving_or_resizing_window;
}

bool PopupShouldDrawCachedMediaDuringFastInteraction(bool moving_or_resizing_window, bool cached) {
    return !moving_or_resizing_window || cached;
}

bool PopupShouldUseCurrentWindowWidthForLayout(bool popup_resizable, bool manual_popup_size,
                                               int current_logical_width, int default_logical_width) {
    return manual_popup_size || (popup_resizable && current_logical_width != default_logical_width);
}

UiRect PopupNativeSearchEditRect(const PopupSearchLayout& layout) {
    return Rect(layout.text.right - 1.0f, layout.text.top + 1.0f, layout.text.right, layout.text.bottom - 1.0f);
}

PopupSearchSelectionRange NormalizePopupSearchSelection(size_t anchor, size_t caret, size_t text_length) {
    anchor = std::min(anchor, text_length);
    caret = std::min(caret, text_length);
    return PopupSearchSelectionRange{std::min(anchor, caret), std::max(anchor, caret)};
}

bool PopupSearchHasSelection(PopupSearchSelectionRange selection) {
    return selection.start < selection.end;
}

bool PopupShouldResizeNativeSearchDuringLiveResize(bool resizing_window, bool bounds_changed) {
    return !resizing_window && bounds_changed;
}

bool PopupShouldInvalidateDuringLiveResize(bool, bool size_changed) {
    return size_changed;
}

bool PopupShouldApplyWindowRect(const RECT& current, const RECT& next) {
    return current.left != next.left || current.top != next.top || current.right != next.right ||
           current.bottom != next.bottom;
}

bool PopupShouldFlushPaintDuringLiveResize(bool resizing_window, bool rect_changed) {
    return resizing_window && rect_changed;
}

unsigned PopupImageFilePreviewDecodePixelLimit() {
    return 160;
}

unsigned PopupImagePreviewDecodePixelLimit() {
    return 220;
}

SIZE PopupPreviewDecodeSize(unsigned source_width, unsigned source_height, unsigned max_dimension) {
    if (source_width == 0 || source_height == 0 || max_dimension == 0) {
        return SIZE{0, 0};
    }
    const unsigned largest = std::max(source_width, source_height);
    if (largest <= max_dimension) {
        return SIZE{static_cast<LONG>(source_width), static_cast<LONG>(source_height)};
    }
    const double scale = static_cast<double>(max_dimension) / static_cast<double>(largest);
    return SIZE{static_cast<LONG>(std::max(1u, static_cast<unsigned>(std::lround(source_width * scale)))),
                static_cast<LONG>(std::max(1u, static_cast<unsigned>(std::lround(source_height * scale))))};
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


bool PopupSearchClearButtonHitTest(const PopupSearchLayout& layout, bool has_query, POINT point) {
    if (!has_query) return false;
    return point.x >= layout.clear_button.left && point.x <= layout.clear_button.right &&
           point.y >= layout.clear_button.top && point.y <= layout.clear_button.bottom;
}

POINT PopupSearchClearButtonCenterDips(const PopupSearchLayout& layout) {
    return POINT{
        static_cast<LONG>(std::lround((layout.clear_button.left + layout.clear_button.right) * 0.5f)),
        static_cast<LONG>(std::lround((layout.clear_button.top + layout.clear_button.bottom) * 0.5f)),
    };
}

float PopupSearchClearButtonOpacity(bool hovered) {
    return hovered ? 0.95f : 0.7f;
}

float PopupSearchTop() {
    return static_cast<float>(kMetrics.header_height + 6);
}

float PopupToolbarTop() {
    return PopupSearchTop() + static_cast<float>(kMetrics.search_height + kDesignTokens.search_to_toolbar_gap);
}

float PopupTabsTop() {
    return PopupToolbarTop() + static_cast<float>(kMetrics.toolbar_height + kDesignTokens.toolbar_to_tab_gap);
}

float PopupListTop() {
    return PopupTabsTop() + static_cast<float>(kMetrics.tab_height + kDesignTokens.tab_to_list_gap);
}

PopupHeaderLayout BuildPopupHeaderLayout() {
    return BuildPopupHeaderLayoutForWidth(kMetrics.width);
}

PopupHeaderLayout BuildPopupHeaderLayoutForWidth(int logical_width) {
    logical_width = std::max(logical_width, 1);
    PopupHeaderLayout layout;
    layout.title = Rect(static_cast<float>(kMetrics.margin), 12.0f, 190.0f, 36.0f);
    layout.pin = Rect(static_cast<float>(logical_width - 80), 11.0f,
                      static_cast<float>(logical_width - 52), 39.0f);
    layout.close = Rect(static_cast<float>(logical_width - 44), 11.0f,
                        static_cast<float>(logical_width - 16), 39.0f);
    return layout;
}

PopupSearchLayout BuildPopupSearchLayout() {
    return BuildPopupSearchLayoutForWidth(kMetrics.width);
}

PopupSearchLayout BuildPopupSearchLayoutForWidth(int logical_width) {
    logical_width = std::max(logical_width, 1);
    const float top = PopupSearchTop();
    PopupSearchLayout layout;
    layout.box = Rect(static_cast<float>(kMetrics.margin), top,
                      static_cast<float>(logical_width - kMetrics.margin),
                      top + static_cast<float>(kMetrics.search_height));
    layout.icon = Rect(static_cast<float>(kMetrics.margin + 17), top + 13.0f,
                       static_cast<float>(kMetrics.margin + 35), top + 31.0f);
    layout.text = Rect(static_cast<float>(kMetrics.margin + 48), top + 10.0f,
                       static_cast<float>(logical_width - kMetrics.margin - 40), top + 38.0f);
    layout.clear_button = Rect(static_cast<float>(logical_width - kMetrics.margin - 32), top + 14.0f,
                               static_cast<float>(logical_width - kMetrics.margin - 16), top + 30.0f);
    return layout;
}

UiRect BuildPopupHeaderPinIconRect(const PopupHeaderLayout& header) {
    return Rect(header.pin.left + 6.0f, header.pin.top + 6.0f, header.pin.right - 6.0f, header.pin.bottom - 6.0f);
}

UiRect BuildPopupHeaderCloseIconRect(const PopupHeaderLayout& header) {
    return Rect(header.close.left + 6.0f, header.close.top + 6.0f, header.close.right - 6.0f,
                header.close.bottom - 6.0f);
}

PopupToolbarLayout BuildPopupToolbarLayout(bool multi_select) {
    return BuildPopupToolbarLayoutForWidth(multi_select, kMetrics.width);
}

PopupToolbarLayout BuildPopupToolbarLayoutForWidth(bool multi_select, int logical_width) {
    logical_width = std::max(logical_width, 1);
    const float y = PopupToolbarTop();
    const float bottom = y + static_cast<float>(kMetrics.toolbar_height);
    const float left_edge = static_cast<float>(kMetrics.margin);
    const float right_edge = static_cast<float>(logical_width - kMetrics.margin);
    PopupToolbarLayout layout;
    const bool compact = logical_width < 520;
    layout.filter = Rect(left_edge, y, static_cast<float>(kMetrics.margin + (compact ? 92 : 112)), bottom);
    if (multi_select) {
        if (compact) {
            constexpr float kGap = 4.0f;
            constexpr float kCancelBase = 58.0f;
            constexpr float kSelectBase = 58.0f;
            constexpr float kDeleteBase = 88.0f;
            constexpr float kPasteBase = 92.0f;
            constexpr float kBaseTotal = kCancelBase + kSelectBase + kDeleteBase + kPasteBase;
            const float content_width = std::max(1.0f, right_edge - left_edge - kGap * 3.0f);
            const float scale = content_width < kBaseTotal ? content_width / kBaseTotal : 1.0f;
            const float extra = std::max(0.0f, content_width - kBaseTotal);
            const float cancel_width = kCancelBase * scale + extra * 0.15f;
            const float select_width = kSelectBase * scale + extra * 0.15f;
            const float delete_width = kDeleteBase * scale + extra * 0.35f;
            float x = left_edge;
            layout.cancel_multi_select = Rect(x, y, x + cancel_width, bottom);
            x = layout.cancel_multi_select.right + kGap;
            layout.select_all = Rect(x, y, x + select_width, bottom);
            x = layout.select_all.right + kGap;
            layout.delete_selected = Rect(x, y, x + delete_width, bottom);
            x = layout.delete_selected.right + kGap;
            layout.paste_selected = Rect(x, y, right_edge, bottom);
        } else {
            layout.cancel_multi_select = Rect(left_edge, y, 96.0f, bottom);
            layout.select_all = Rect(104.0f, y, 176.0f, bottom);
            const float right_group_left = right_edge - 216.0f;
            layout.delete_selected = Rect(right_group_left, y, right_group_left + 104.0f, bottom);
            layout.paste_selected = Rect(right_group_left + 112.0f, y, right_group_left + 216.0f, bottom);
        }
    } else {
        if (compact) {
            constexpr float kGap = 8.0f;
            const float button_width = std::max(1.0f, (right_edge - left_edge - kGap * 2.0f) / 3.0f);
            layout.filter = Rect(left_edge, y, left_edge + button_width, bottom);
            layout.multi_select = Rect(layout.filter.right + kGap, y,
                                       layout.filter.right + kGap + button_width, bottom);
            layout.clear_all = Rect(layout.multi_select.right + kGap, y, right_edge, bottom);
        } else {
            const float right_group_left = right_edge - 232.0f;
            layout.multi_select = Rect(right_group_left, y, right_group_left + 96.0f, bottom);
            layout.clear_all = Rect(right_group_left + 104.0f, y, right_group_left + 232.0f, bottom);
        }
    }
    return layout;
}

UiRect PopupToolbarLabelRect(const UiRect& button, bool has_chevron) {
    const bool compact = button.Width() < 94.0f;
    const float label_left = button.left + (compact ? 24.0f : 34.0f);
    const float label_right = button.right - (has_chevron ? 21.0f : 6.0f);
    return Rect(label_left, button.top + 6.0f, label_right, button.bottom - 6.0f);
}

PopupTabsLayout BuildPopupTabsLayout(bool favorites_active) {
    return BuildPopupTabsLayoutForWidth(favorites_active, kMetrics.width);
}

PopupTabsLayout BuildPopupTabsLayoutForWidth(bool favorites_active, int logical_width) {
    logical_width = std::max(logical_width, 1);
    const float top = PopupTabsTop();
    const float tab_left = std::max(static_cast<float>(kMetrics.margin + 40),
                                    (static_cast<float>(logical_width) - 144.0f) * 0.5f);
    const float action_offset = static_cast<float>(logical_width - kMetrics.width);
    PopupTabsLayout layout;
    layout.history = Rect(tab_left, top + 2.0f, tab_left + 72.0f, top + 28.0f);
    layout.favorites = Rect(tab_left + 72.0f, top + 2.0f, tab_left + 144.0f, top + 28.0f);
    layout.favorite_group = Rect(static_cast<float>(kMetrics.width - 72) + action_offset, top + 4.0f,
                                 static_cast<float>(kMetrics.width - 48) + action_offset, top + 28.0f);
    layout.add_favorite_phrase = Rect(static_cast<float>(kMetrics.width - 40) + action_offset, top + 4.0f,
                                      static_cast<float>(kMetrics.width - 16) + action_offset, top + 28.0f);
    const UiRect& active = favorites_active ? layout.favorites : layout.history;
    layout.active_indicator = Rect(active.left + 12.0f, top + 28.0f, active.right - 12.0f, top + 31.0f);
    layout.divider = Rect(static_cast<float>(kMetrics.margin), top + 33.0f,
                          static_cast<float>(logical_width - kMetrics.margin), top + 34.0f);
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
    layout.panel = Rect(left, top, static_cast<float>(kMetrics.width - kMetrics.margin), top + 394.0f);
    layout.close = Rect(layout.panel.right - 34.0f, top + 12.0f, layout.panel.right - 14.0f, top + 32.0f);
    layout.type_section = Rect(left + 14.0f, top + 42.0f, layout.panel.right - 14.0f, top + 108.0f);

    const float chip_width = 132.0f;
    const float chip_height = 22.0f;
    const float chip_gap = 12.0f;
    const float chip_left = layout.type_section.left;
    const float chip_top = layout.type_section.top + 19.0f;
    layout.text_chip = Rect(chip_left, chip_top, chip_left + chip_width, chip_top + chip_height);
    layout.image_chip = Rect(chip_left + chip_width + chip_gap, chip_top,
                             chip_left + chip_width * 2.0f + chip_gap, chip_top + chip_height);
    layout.file_chip = Rect(chip_left, chip_top + chip_height + 6.0f,
                            chip_left + chip_width, chip_top + chip_height * 2.0f + 6.0f);
    layout.link_chip = Rect(chip_left + chip_width + chip_gap, chip_top + chip_height + 6.0f,
                            chip_left + chip_width * 2.0f + chip_gap, chip_top + chip_height * 2.0f + 6.0f);

    layout.date_card = Rect(left + 14.0f, top + 121.0f, layout.panel.right - 14.0f, top + 187.0f);
    const float date_box_top = layout.date_card.top + 26.0f;
    const float date_box_width = (layout.date_card.Width() - 30.0f) / 2.0f;
    layout.start_date = Rect(layout.date_card.left + 10.0f, date_box_top,
                             layout.date_card.left + 10.0f + date_box_width, date_box_top + 30.0f);
    layout.end_date = Rect(layout.start_date.right + 10.0f, date_box_top,
                           layout.start_date.right + 10.0f + date_box_width, date_box_top + 30.0f);

    layout.calendar = Rect(left + 14.0f, top + 197.0f, layout.panel.right - 14.0f, top + 357.0f);
    layout.calendar_prev_year = Rect(layout.calendar.left + 10.0f, layout.calendar.top + 8.0f,
                                     layout.calendar.left + 30.0f, layout.calendar.top + 28.0f);
    layout.calendar_prev = Rect(layout.calendar.left + 34.0f, layout.calendar.top + 8.0f,
                                layout.calendar.left + 54.0f, layout.calendar.top + 28.0f);
    layout.calendar_next = Rect(layout.calendar.right - 54.0f, layout.calendar.top + 8.0f,
                                layout.calendar.right - 34.0f, layout.calendar.top + 28.0f);
    layout.calendar_next_year = Rect(layout.calendar.right - 30.0f, layout.calendar.top + 8.0f,
                                     layout.calendar.right - 10.0f, layout.calendar.top + 28.0f);
    layout.calendar_title = Rect(layout.calendar.left + 62.0f, layout.calendar.top + 9.0f,
                                 layout.calendar.right - 62.0f, layout.calendar.top + 28.0f);
    layout.reset = Rect(left + 16.0f, top + 364.0f, left + 110.0f, top + 384.0f);
    layout.done = Rect(layout.panel.right - 88.0f, top + 362.0f, layout.panel.right - 16.0f, top + 386.0f);
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
    const float top = PopupTabsTop() + 30.0f;
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
            Rect(left, layout.calendar.top + 32.0f, left + cell_width, layout.calendar.top + 46.0f),
        });
    }
    return labels;
}

PopupCardLayout BuildPopupCardLayout(bool multi_select, float top) {
    const float left = static_cast<float>(kMetrics.margin);
    const float right = static_cast<float>(kMetrics.width - kMetrics.margin);
    const float bottom = top + static_cast<float>(kMetrics.card_height);
    const float title_right = right - 76.0f;

    PopupCardLayout layout;
    layout.card = Rect(left, top, right, bottom);
    layout.stripe = Rect(left + 4.0f, top + 12.0f, left + 4.0f, bottom - 12.0f);
    const float media_left = multi_select ? left + 54.0f : left + 26.0f;
    layout.image_preview = Rect(media_left, top + 16.0f, media_left + 40.0f, top + 56.0f);
    layout.file_icon = Rect(media_left + 5.0f, top + 21.0f, media_left + 35.0f, top + 51.0f);
    layout.checkbox = Rect(left + 24.0f, top + 29.0f, left + 38.0f, top + 43.0f);
    const float text_left = multi_select ? left + 104.0f : left + 78.0f;
    layout.title = Rect(text_left, top + 12.0f, title_right, top + 34.0f);
    layout.meta = Rect(text_left, top + 40.0f, right - 18.0f, top + 60.0f);
    layout.time = Rect(right - 68.0f, top + 13.0f, right - 38.0f, top + 31.0f);
    layout.menu = Rect(right - 34.0f, top + 12.0f, right - 10.0f, top + 34.0f);
    layout.expand = Rect(right - 30.0f, top + 38.0f, right - 8.0f, top + 60.0f);
    return layout;
}

UiRect PopupCardKindIconRect(const PopupCardLayout& card) {
    const float center_x = (card.image_preview.left + card.image_preview.right) * 0.5f;
    const float center_y = (card.image_preview.top + card.image_preview.bottom) * 0.5f;
    return Rect(center_x - 10.0f, center_y - 10.0f, center_x + 10.0f, center_y + 10.0f);
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

bool PopupTextRangeRectUsable(RECT rect) {
    constexpr int kMaxCaretRangeWidth = 12;
    return rect.right >= rect.left &&
           rect.right - rect.left <= kMaxCaretRangeWidth &&
           rect.bottom - rect.top >= kMinTextRangeHeight &&
           rect.bottom > 1 &&
           (rect.left > 1 || rect.top > 1);
}

POINT PopupTextRangeAnchor(RECT rect) {
    return POINT{rect.right, rect.top};
}

bool PopupTextCharacterRectUsable(RECT rect, unsigned dpi) {
    const int max_width = ScalePopupMetricForDpi(64, dpi);
    const int max_height = ScalePopupMetricForDpi(72, dpi);
    return rect.right > rect.left &&
           rect.bottom > rect.top &&
           rect.right - rect.left <= max_width &&
           rect.bottom - rect.top >= kMinTextRangeHeight &&
           rect.bottom - rect.top <= max_height &&
           rect.bottom > 1 &&
           (rect.left > 1 || rect.top > 1);
}

RECT PopupTextCharacterCaretRect(RECT rect, bool after_character) {
    const LONG x = after_character ? rect.right : rect.left;
    return RECT{after_character ? x - 1 : x, rect.top, after_character ? x : x + 1, rect.bottom};
}

bool PopupTextInputRectUsable(RECT rect) {
    constexpr int kMinInputWidth = 24;
    constexpr int kMinInputHeight = 8;
    constexpr int kMaxInputHeight = 120;
    return rect.right > rect.left &&
           rect.bottom > rect.top &&
           rect.right - rect.left >= kMinInputWidth &&
           rect.bottom - rect.top >= kMinInputHeight &&
           rect.bottom - rect.top <= kMaxInputHeight &&
           rect.bottom > 1 &&
           (rect.left > 1 || rect.top > 1);
}

POINT PopupTextInputRectAnchor(RECT rect, unsigned dpi) {
    const int text_inset = ScalePopupMetricForDpi(8, dpi);
    const int right_padding = ScalePopupMetricForDpi(48, dpi);
    return POINT{ClampToRange(rect.left + text_inset, rect.left, rect.right - right_padding), rect.top};
}

bool PopupCaretAnchorAllowedByFocusedText(bool focused_text_editable, RECT focused_rect, bool has_focused_rect,
                                          RECT caret_rect, unsigned dpi) {
    if (!focused_text_editable || !has_focused_rect || focused_rect.right <= focused_rect.left ||
        focused_rect.bottom <= focused_rect.top) {
        return false;
    }
    const int tolerance = ScalePopupMetricForDpi(96, dpi);
    return focused_rect.left - tolerance <= caret_rect.right &&
           focused_rect.right + tolerance >= caret_rect.left &&
           focused_rect.top - tolerance <= caret_rect.bottom &&
           focused_rect.bottom + tolerance >= caret_rect.top;
}

bool PopupJavaCaretRectUsable(RECT caret_rect, RECT window_rect, unsigned dpi) {
    if (caret_rect.right <= caret_rect.left || caret_rect.bottom <= caret_rect.top ||
        window_rect.right <= window_rect.left || window_rect.bottom <= window_rect.top) {
        return false;
    }
    const int max_width = ScalePopupMetricForDpi(16, dpi);
    const int min_height = ScalePopupMetricForDpi(4, dpi);
    const int max_height = ScalePopupMetricForDpi(96, dpi);
    const int tolerance = ScalePopupMetricForDpi(96, dpi);
    const int width = caret_rect.right - caret_rect.left;
    const int height = caret_rect.bottom - caret_rect.top;
    if (width > max_width || height < min_height || height > max_height) {
        return false;
    }
    return window_rect.left - tolerance <= caret_rect.right &&
           window_rect.right + tolerance >= caret_rect.left &&
           window_rect.top - tolerance <= caret_rect.bottom &&
           window_rect.bottom + tolerance >= caret_rect.top;
}

bool PopupVisualCaretRectUsable(RECT caret_rect, RECT window_rect, unsigned dpi) {
    if (caret_rect.right <= caret_rect.left || caret_rect.bottom <= caret_rect.top ||
        window_rect.right <= window_rect.left || window_rect.bottom <= window_rect.top) {
        return false;
    }
    const int max_width = ScalePopupMetricForDpi(10, dpi);
    const int min_height = ScalePopupMetricForDpi(8, dpi);
    const int max_height = ScalePopupMetricForDpi(48, dpi);
    const int tolerance = ScalePopupMetricForDpi(4, dpi);
    const int width = caret_rect.right - caret_rect.left;
    const int height = caret_rect.bottom - caret_rect.top;
    if (width > max_width || height < min_height || height > max_height) {
        return false;
    }
    return window_rect.left - tolerance <= caret_rect.left &&
           window_rect.right + tolerance >= caret_rect.right &&
           window_rect.top - tolerance <= caret_rect.top &&
           window_rect.bottom + tolerance >= caret_rect.bottom;
}

POINT PopupTextAvoidRectPosition(RECT avoid, SIZE size, RECT work, unsigned dpi) {
    const int corner_gap = ScalePopupMetricForDpi(8, dpi);
    const int work_gap = ScalePopupMetricForDpi(16, dpi);
    const bool wide_input_fallback =
        PopupTextInputRectUsable(avoid) && avoid.right - avoid.left > ScalePopupMetricForDpi(420, dpi);
    const int horizontal_anchor = wide_input_fallback ? PopupTextInputRectAnchor(avoid, dpi).x : avoid.right;
    const int soft_min_x = work.left + work_gap;
    const int soft_max_x = work.right - size.cx - work_gap;
    const int hard_min_y = work.top;
    const int hard_max_y = work.bottom - size.cy;

    const int right_x = horizontal_anchor + corner_gap;
    const int above_y = avoid.top - size.cy - corner_gap;
    const int below_y = avoid.bottom + corner_gap;
    const int x = ClampToRange(right_x, soft_min_x, soft_max_x);

    const int clamped_above_y = ClampToRange(above_y, hard_min_y, hard_max_y);
    const int clamped_below_y = ClampToRange(below_y, hard_min_y, hard_max_y);
    const int above_overlap =
        std::max(0, clamped_above_y + static_cast<int>(size.cy) + corner_gap - static_cast<int>(avoid.top));
    const int below_overlap = std::max(0, static_cast<int>(avoid.bottom) + corner_gap - clamped_below_y);
    const int y = above_overlap <= below_overlap ? clamped_above_y : clamped_below_y;

    return POINT{
        x,
        y,
    };
}

POINT PopupTextAnchorPosition(POINT anchor, SIZE size, RECT work, unsigned dpi) {
    return PopupTextAvoidRectPosition(RECT{anchor.x, anchor.y, anchor.x, anchor.y}, size, work, dpi);
}

POINT PopupWindowsClipboardPosition(SIZE size, RECT monitor, RECT work, unsigned dpi) {
    const int min_gap = ScalePopupMetricForDpi(16, dpi);
    const int taskbar_gap = ScalePopupMetricForDpi(16, dpi);
    const int unadjusted_work_threshold = ScalePopupMetricForDpi(4, dpi);
    const int taskbar_guard = ScalePopupMetricForDpi(72, dpi);
    const bool work_looks_unadjusted = std::abs(work.bottom - monitor.bottom) <= unadjusted_work_threshold;
    const int bottom_gap = work_looks_unadjusted ? std::max(taskbar_gap, taskbar_guard) : taskbar_gap;
    const int bottom_limit = std::min(work.bottom, monitor.bottom) - size.cy - bottom_gap;
    const RECT bounds{
        monitor.left,
        monitor.top,
        monitor.right,
        monitor.bottom,
    };
    return POINT{
        ClampToRange(bounds.right - size.cx, bounds.left + min_gap, bounds.right - size.cx),
        ClampToRange(work.bottom - size.cy - bottom_gap, bounds.top + min_gap, bottom_limit),
    };
}

POINT PopupKeyboardInvocationPosition(bool has_text_avoid, RECT avoid, SIZE size, RECT monitor, RECT work,
                                      unsigned dpi) {
    if (has_text_avoid) {
        return PopupTextAvoidRectPosition(avoid, size, work, dpi);
    }
    return PopupWindowsClipboardPosition(size, monitor, work, dpi);
}

bool PopupTargetNeedsShellTopmostRaise(std::wstring_view class_name, std::wstring_view title) {
    return class_name == L"Windows.UI.Core.CoreWindow" || title == L"搜索" || title == L"Search";
}

bool PopupTargetNeedsShellTopmostRaise(std::wstring_view class_name, std::wstring_view title,
                                       std::wstring_view process_name) {
    return PopupTargetNeedsShellTopmostRaise(class_name, title) ||
           process_name == L"SearchHost" || process_name == L"StartMenuExperienceHost" ||
           process_name == L"TextInputHost";
}

bool PopupShellTopmostRaiseShouldExpire(bool keep_above_shell_surface, long remaining_ms) {
    return !keep_above_shell_surface && remaining_ms <= 0;
}

bool PopupShouldCreateInShellWindowBand() {
    return true;
}

bool PopupTargetRequiresExplicitTextInputFocus(std::wstring_view class_name, std::wstring_view title,
                                               std::wstring_view process_name) {
    (void)class_name;
    (void)title;
    (void)process_name;
    return false;
}

bool PopupShouldUseTextAvoidForTarget(bool has_text_avoid, std::wstring_view class_name, std::wstring_view title) {
    (void)class_name;
    (void)title;
    return has_text_avoid;
}

bool PopupTargetCanUseConsoleAnchor(std::wstring_view class_name) {
    return class_name == L"ConsoleWindowClass";
}

POINT PopupWindowRectFallback(RECT target, SIZE size, RECT work, unsigned dpi) {
    const int gap = ScalePopupMetricForDpi(10, dpi);
    POINT anchor{target.right, target.top};
    POINT position = ClampPopupToWorkArea(anchor, size, work, dpi);
    if (position.x + size.cx <= target.left || position.x >= target.right ||
        position.y + size.cy <= target.top || position.y >= target.bottom) {
        return position;
    }
    anchor = POINT{target.left, target.bottom};
    return ClampPopupToWorkArea(anchor, size, work, dpi);
}

POINT PopupTargetCenterFallback(RECT target, SIZE size, RECT work, unsigned dpi) {
    const POINT anchor{
        target.left + (target.right - target.left) / 2,
        target.top + (target.bottom - target.top) / 2,
    };
    return ClampPopupToWorkArea(anchor, size, work, dpi);
}

POINT PopupConsoleCellAnchor(POINT client_origin_screen, COORD cell, SMALL_RECT viewport, COORD font_size) {
    const int column = std::max(0, static_cast<int>(cell.X) - static_cast<int>(viewport.Left));
    const int row = std::max(0, static_cast<int>(cell.Y) - static_cast<int>(viewport.Top));
    return POINT{
        client_origin_screen.x + (column + 1) * std::max(1, static_cast<int>(font_size.X)),
        client_origin_screen.y + row * std::max(1, static_cast<int>(font_size.Y)),
    };
}

COORD PopupConsoleAnchorCell(CONSOLE_SELECTION_INFO selection, COORD cursor_position) {
    return (selection.dwFlags & CONSOLE_SELECTION_NOT_EMPTY) ? selection.dwSelectionAnchor : cursor_position;
}

COORD PopupConsoleSelectionAnchor(CONSOLE_SELECTION_INFO selection) {
    return selection.dwSelectionAnchor;
}

POINT PopupBottomRightFallback(SIZE size, RECT work, unsigned dpi) {
    return POINT{
        work.right - size.cx - ScalePopupMetricForDpi(32, dpi),
        work.bottom - size.cy - ScalePopupMetricForDpi(16, dpi),
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
                                  bool transient_hide_suppressed, bool visible, bool next_active_inside_popup,
                                  bool shell_surface_raise_active) {
    return visible && !pinned_open && !prompt_open && !moving_window && !transient_hide_suppressed &&
           !next_active_inside_popup && !shell_surface_raise_active;
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
    return IsPopupHeaderDragAreaForWidth(x, y, kMetrics.width);
}

bool IsPopupHeaderDragAreaForWidth(float x, float y, int logical_width) {
    if (y < 0.0f || y > static_cast<float>(kMetrics.header_height)) {
        return false;
    }
    const auto header = BuildPopupHeaderLayoutForWidth(logical_width);
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
    const float cell_height = 16.0f;
    const float grid_top = layout.calendar.top + 48.0f;
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

std::vector<PopupCalendarRangeSegment> BuildPopupCalendarRangeSegments(
    const PopupFilterLayout& layout, int year, int month, const PopupDateRangeState& state) {
    std::vector<PopupCalendarRangeSegment> segments;
    if (!state.start || !state.end || *state.end < *state.start) {
        return segments;
    }

    const auto cells = BuildPopupCalendarCells(layout, year, month);
    UiRect current{};
    bool active = false;
    int current_row = -1;
    constexpr float kRangeInsetX = 8.0f;
    constexpr float kRangeHalfHeight = 8.0f;

    auto flush = [&]() {
        if (active) {
            segments.push_back(PopupCalendarRangeSegment{current, false, false});
            active = false;
        }
    };

    for (const auto& cell : cells) {
        if (cell.date < *state.start || *state.end < cell.date) {
            flush();
            continue;
        }
        const int row = static_cast<int>(
            std::lround((cell.rect.top - (layout.calendar.top + 48.0f)) / 16.0f));
        const float center_y = (cell.rect.top + cell.rect.bottom) * 0.5f;
        const UiRect segment_part =
            Rect(cell.rect.left + kRangeInsetX, center_y - kRangeHalfHeight,
                 cell.rect.right - kRangeInsetX, center_y + kRangeHalfHeight);
        if (!active || row != current_row) {
            flush();
            current = segment_part;
            active = true;
            current_row = row;
            continue;
        }
        current.right = segment_part.right;
    }
    flush();

    for (auto& segment : segments) {
        segment.starts_range = true;
        segment.ends_range = true;
    }
    return segments;
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
    if (Contains(layout.calendar_prev_year, x, y)) {
        return PopupCalendarArrow::PreviousYear;
    }
    if (Contains(layout.calendar_prev, x, y)) {
        return PopupCalendarArrow::PreviousMonth;
    }
    if (Contains(layout.calendar_next, x, y)) {
        return PopupCalendarArrow::NextMonth;
    }
    if (Contains(layout.calendar_next_year, x, y)) {
        return PopupCalendarArrow::NextYear;
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
    if (Contains(layout.calendar_prev_year, x, y)) return PopupFilterTarget::CalendarPreviousYear;
    if (Contains(layout.calendar_prev, x, y)) return PopupFilterTarget::CalendarPrevious;
    if (Contains(layout.calendar_next, x, y)) return PopupFilterTarget::CalendarNext;
    if (Contains(layout.calendar_next_year, x, y)) return PopupFilterTarget::CalendarNextYear;
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
