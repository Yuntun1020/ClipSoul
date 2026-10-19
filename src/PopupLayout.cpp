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
    18,  // margin
    44,  // header_height
    40,  // search_height
    28,  // toolbar_height（工具按钮与标签同处一栏）
    40,  // tab_height（标签栏）
    60,  // card_height（padding 20 + side: time 19 + gap 3 + expand 18）
    0,   // card_gap（行间用发丝线分隔）
    14,  // corner_radius
    28,  // header_button_size
    16,  // toolbar_icon_size
    1.0f,
};
constexpr PopupDesignTokenData kDesignTokens{};
constexpr int kMinTextRangeHeight = 2;
constexpr float kFooterHeight = 30.0f;
constexpr float kSectionHeaderHeight = 27.0f;
constexpr float kBarRightInset = 10.0f;

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
    return 14.0f;
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
    const float available = PopupListBottomForHeight(logical_height) - PopupListTop();
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
        std::max(0.0f, PopupListBottomForHeight(logical_height) - PopupListTop());
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
        std::max(0.0f, PopupListBottomForHeight(logical_height) - PopupListTop());
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
    return Rect(static_cast<float>(logical_width - 7), PopupListTop() + 2.0f,
                static_cast<float>(logical_width - 3), PopupListBottomForHeight(logical_height) - 4.0f);
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
        std::max(0.0f, PopupListBottomForHeight(logical_height) - PopupListTop());
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
    return scroll_offset > 120.0f;
}

UiRect PopupScrollToTopButtonRectForSize(int logical_width, int logical_height) {
    constexpr float kButtonSize = 30.0f;
    constexpr float kBottomInset = 14.0f;
    constexpr float kScrollbarGap = 4.0f;
    const auto scrollbar_hit = PopupScrollbarHitRectForSize(logical_width, logical_height);
    const float right = scrollbar_hit.left - kScrollbarGap;
    const float bottom = PopupListBottomForHeight(logical_height) - kBottomInset;
    return Rect(right - kButtonSize, bottom - kButtonSize, right, bottom);
}

UiRect PopupListClipRectForHeight(int logical_width, int logical_height) {
    return Rect(0.0f, PopupListTop(), static_cast<float>(logical_width),
                std::max(PopupListTop(), PopupListBottomForHeight(logical_height)));
}

float PopupMotionProgress(float raw_progress) {
    const float clamped = std::clamp(raw_progress, 0.0f, 1.0f);
    const float inverse = 1.0f - clamped;
    return 1.0f - inverse * inverse * inverse * inverse;
}

int PopupMotionDurationMs(PopupMotionKind kind) {
    switch (kind) {
    case PopupMotionKind::Panel:
        return 160;
    case PopupMotionKind::ViewSwitch:
        return 180;
    case PopupMotionKind::Arrow:
        return 220;
    case PopupMotionKind::MultiSelect:
        return 200;
    case PopupMotionKind::Hover:
        return 90;
    }
    return 160;
}

float PopupTimedMotionProgress(double elapsed_ms, double duration_ms, bool animations_enabled) {
    if (!animations_enabled || duration_ms <= 0.0) {
        return 1.0f;
    }
    if (elapsed_ms <= 0.0) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp(elapsed_ms / duration_ms, 0.0, 1.0));
}

PopupSpringState PopupSpringStep(float position, float velocity, float target, float dt_seconds) {
    // 纸条弹簧：K=520，阻尼比 0.86（略欠阻尼，回弹一次就停），dt 上限 32ms。
    constexpr float kStiffness = 520.0f;
    constexpr float kDampingRatio = 0.86f;
    constexpr float kMaxStep = 0.032f;
    const float dt = std::clamp(dt_seconds, 0.0f, kMaxStep);
    if (dt <= 0.0f) {
        return PopupSpringState{position, velocity};
    }
    const float damping = 2.0f * kDampingRatio * std::sqrt(kStiffness);
    const float acceleration = kStiffness * (target - position) - damping * velocity;
    const float next_velocity = velocity + acceleration * dt;
    const float next_position = position + next_velocity * dt;
    return PopupSpringState{next_position, next_velocity};
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

float PopupCubicBezierEase(float x1, float y1, float x2, float y2, float progress) {
    // CSS cubic-bezier(x1,y1,x2,y2)：先由 x 反解参数 t，再取 y。用于预览里的弹性过渡。
    const float target = std::clamp(progress, 0.0f, 1.0f);
    if (target <= 0.0f) return 0.0f;
    if (target >= 1.0f) return 1.0f;
    const auto bezier = [](float t, float p1, float p2) {
        const float inv = 1.0f - t;
        return 3.0f * inv * inv * t * p1 + 3.0f * inv * t * t * p2 + t * t * t;
    };
    float low = 0.0f;
    float high = 1.0f;
    float t = target;
    for (int i = 0; i < 24; ++i) {
        const float value = bezier(t, x1, x2);
        if (std::fabs(value - target) < 1e-5f) break;
        if (value < target) low = t; else high = t;
        t = (low + high) * 0.5f;
    }
    return bezier(t, y1, y2);
}

float PopupPinRotationDegrees(bool pinned, float progress) {
    // 预览 #pinWin.on svg{transform:rotate(-40deg)}，过渡 260ms cubic-bezier(.3,1.5,.5,1)。
    const float eased = PopupCubicBezierEase(0.3f, 1.5f, 0.5f, 1.0f, progress);
    constexpr float kPinnedAngle = -40.0f;
    const float from = pinned ? 0.0f : kPinnedAngle;
    const float to = pinned ? kPinnedAngle : 0.0f;
    return from + (to - from) * eased;
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

PopupWindowMotionFrame PopupWindowEnterFrame(float raw_progress) {
    // 预览：.win.enter{animation:winIn 170ms cubic-bezier(.2,.8,.2,1) both}
    //       @keyframes winIn{from{opacity:0;transform:translateY(8px) scale(.985)}}
    const float t = std::clamp(raw_progress, 0.0f, 1.0f);
    const float eased = PopupCubicBezierEase(0.2f, 0.8f, 0.2f, 1.0f, t);
    PopupWindowMotionFrame frame;
    frame.opacity = eased;
    frame.translate_y = 8.0f * (1.0f - eased);
    frame.scale = 0.985f + 0.015f * eased;
    return frame;
}

PopupWindowMotionFrame PopupWindowLeaveFrame(float raw_progress) {
    // 预览：.win.leave{animation:winOut 120ms cubic-bezier(.4,0,1,1) both}
    //       @keyframes winOut{to{opacity:0;transform:translateY(4px) scale(.99)}}
    const float t = std::clamp(raw_progress, 0.0f, 1.0f);
    const float eased = PopupCubicBezierEase(0.4f, 0.0f, 1.0f, 1.0f, t);
    PopupWindowMotionFrame frame;
    frame.opacity = 1.0f - eased;
    frame.translate_y = 4.0f * eased;
    frame.scale = 1.0f - 0.01f * eased;
    return frame;
}

PopupRowEntranceFrame PopupRowEntranceFrameAt(double elapsed_ms, int entrance_index) {
    // 预览：.win.enter .row / .sect{animation:rowRise 220ms cubic-bezier(.2,.8,.2,1) both;
    //                              animation-delay:calc(var(--i)*18ms + 40ms)}
    //       @keyframes rowRise{from{opacity:0;transform:translateY(5px)}}
    // entrance_index 就是预览 JS 里的 --i（首项 1，上限 9）。
    const int index = std::clamp(entrance_index, 0, 9);
    const double delay_ms = 40.0 + static_cast<double>(index) * 18.0;
    const double local_ms = std::max(0.0, elapsed_ms - delay_ms);
    const float t = static_cast<float>(std::clamp(local_ms / 220.0, 0.0, 1.0));
    const float eased = PopupCubicBezierEase(0.2f, 0.8f, 0.2f, 1.0f, t);
    PopupRowEntranceFrame frame;
    frame.opacity = eased;
    frame.translate_y = 5.0f * (1.0f - eased);
    return frame;
}

double PopupRowEntranceTotalMs() {
    // 最后一档延迟 40 + 9*18 = 202ms，再加 220ms 时长。
    return 40.0 + 9.0 * 18.0 + 220.0;
}

float PopupSmoothScrollProgress(float raw_progress) {
    // Chromium 的 smooth scroll 是 ease-in-out（cubic-bezier(.42,0,.58,1)）。
    return PopupCubicBezierEase(0.42f, 0.0f, 0.58f, 1.0f, std::clamp(raw_progress, 0.0f, 1.0f));
}

float PopupSmoothScrollOffset(float from_offset, float to_offset, float raw_progress) {
    const float eased = PopupSmoothScrollProgress(raw_progress);
    return from_offset + (to_offset - from_offset) * eased;
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

namespace {
uint32_t LerpColorChannel(uint32_t from, uint32_t to, float t, int shift) {
    const uint32_t a = (from >> shift) & 0xFFu;
    const uint32_t b = (to >> shift) & 0xFFu;
    const auto mixed = static_cast<uint32_t>(std::lround(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t));
    return (mixed & 0xFFu) << shift;
}

uint32_t LerpRgb(uint32_t from, uint32_t to, float t) {
    return LerpColorChannel(from, to, t, 16) | LerpColorChannel(from, to, t, 8) | LerpColorChannel(from, to, t, 0);
}
} // namespace

PopupThemePalette LerpPopupThemePalette(const PopupThemePalette& from, const PopupThemePalette& to, float t) {
    const float progress = std::clamp(t, 0.0f, 1.0f);
    if (progress <= 0.0f) {
        return from;
    }
    if (progress >= 1.0f) {
        return to;
    }
    PopupThemePalette mixed = to;
    mixed.dark = progress < 0.5f ? from.dark : to.dark;
    const auto lerp = [progress](uint32_t a, uint32_t b) { return LerpRgb(a, b, progress); };
    mixed.window_tint = lerp(from.window_tint, to.window_tint);
    mixed.panel_fill = lerp(from.panel_fill, to.panel_fill);
    mixed.card_fill = lerp(from.card_fill, to.card_fill);
    mixed.search_fill = lerp(from.search_fill, to.search_fill);
    mixed.text = lerp(from.text, to.text);
    mixed.muted = lerp(from.muted, to.muted);
    mixed.border = lerp(from.border, to.border);
    mixed.accent = lerp(from.accent, to.accent);
    mixed.strong_border = lerp(from.strong_border, to.strong_border);
    mixed.accent_hover = lerp(from.accent_hover, to.accent_hover);
    mixed.text_tertiary = lerp(from.text_tertiary, to.text_tertiary);
    mixed.danger = lerp(from.danger, to.danger);
    mixed.paper_hover = lerp(from.paper_hover, to.paper_hover);
    mixed.paper_selected = lerp(from.paper_selected, to.paper_selected);
    mixed.border_hover = lerp(from.border_hover, to.border_hover);
    mixed.border_selected = lerp(from.border_selected, to.border_selected);
    mixed.active_tab = lerp(from.active_tab, to.active_tab);
    mixed.scrollbar_track = lerp(from.scrollbar_track, to.scrollbar_track);
    mixed.scrollbar_thumb = lerp(from.scrollbar_thumb, to.scrollbar_thumb);
    mixed.scrollbar_pressed = lerp(from.scrollbar_pressed, to.scrollbar_pressed);
    mixed.focus_outline = lerp(from.focus_outline, to.focus_outline);
    const auto lerp_alpha = [progress](float a, float b) { return a + (b - a) * progress; };
    mixed.window_opacity = lerp_alpha(from.window_opacity, to.window_opacity);
    mixed.panel_opacity = lerp_alpha(from.panel_opacity, to.panel_opacity);
    mixed.card_opacity = lerp_alpha(from.card_opacity, to.card_opacity);
    return mixed;
}

PopupThemePalette ResolvePopupThemePalette(int theme_mode, bool system_dark) {
    const bool dark = theme_mode == 2 || (theme_mode == 0 && system_dark);
    // Ink Ledger：墨与纸。accent = 墨色（主操作/选中），danger = 朱砂（置顶、收藏、危险操作的唯一彩色）。
    if (dark) {
        return PopupThemePalette{
            true,
            0x171614, // window_tint     纸（深）
            0x1C1B18, // panel_fill      浮层
            0x22201C, // card_fill       深纸：类型徽章 / 分段底板
            0x171614, // search_fill     搜索与窗口同底
            0xECE6DA, // text            墨
            0xA69F92, // muted
            0x262522, // border          发丝线
            0xECE6DA, // accent          墨（深色模式下为浅墨）
            0x353330, // strong_border
            0xF7F2E8, // accent_hover
            0x6A645A, // text_tertiary
            0xE0583F, // danger          朱砂
            0x22201C, // paper_hover
            0x262420, // paper_selected  抽起的纸条
            0x353330, // border_hover
            0x353330, // border_selected
            0xECE6DA, // active_tab
            0x171614, // scrollbar_track
            0x6A645A, // scrollbar_thumb
            0xA69F92, // scrollbar_pressed
            0xECE6DA, // focus_outline
            1.0f,
            1.0f,
            1.0f,
        };
    }
    return PopupThemePalette{
        false,
        0xF7F4EE, // window_tint     纸
        0xFBF9F5, // panel_fill      浮层
        0xEFEAE0, // card_fill       深纸：类型徽章 / 分段底板
        0xF7F4EE, // search_fill     搜索与窗口同底
        0x1C1B19, // text            墨
        0x5E5A52, // muted
        0xE3E0DB, // border          发丝线
        0x1C1B19, // accent          墨
        0xD4D1CC, // strong_border
        0x34322E, // accent_hover
        0xA39D92, // text_tertiary
        0xC8402F, // danger          朱砂
        0xEFEAE0, // paper_hover
        0xFFFDF9, // paper_selected  抽起的纸条
        0xD4D1CC, // border_hover
        0xE3E0DB, // border_selected
        0x1C1B19, // active_tab
        0xF7F4EE, // scrollbar_track
        0xA39D92, // scrollbar_thumb
        0x5E5A52, // scrollbar_pressed
        0x1C1B19, // focus_outline
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
    // Ink Ledger 设置页：左右 18px 内边距，内容右线 = 640 - 18 = 622。
    return SettingsProjectLayout{
        Rect(18.0f, 552.0f, 622.0f, 676.0f),
        Rect(466.0f, 584.0f, 574.0f, 612.0f),
        Rect(18.0f, 621.0f, 622.0f, 622.0f),
        Rect(466.0f, 632.0f, 622.0f, 670.0f),
    };
}

SettingsControlLayout SettingsWindowControlLayout() {
    // 控件只有两档宽度：开关 32×18，其余统一 156×28，右边缘贴齐内容右线。
    constexpr float kControlLeft = 466.0f;
    constexpr float kControlRight = 622.0f;
    constexpr float kToggleLeft = kControlRight - 32.0f;
    return SettingsControlLayout{
        Rect(kControlLeft, 86.0f, kControlRight, 114.0f),
        Rect(kToggleLeft, 139.0f, kControlRight, 157.0f),
        Rect(kToggleLeft, 187.0f, kControlRight, 205.0f),
        Rect(kToggleLeft, 519.0f, kControlRight, 537.0f),
        Rect(kControlLeft, 466.0f, kControlLeft + 51.0f, 494.0f),
        Rect(kControlLeft + 52.0f, 466.0f, kControlLeft + 103.0f, 494.0f),
        Rect(kControlLeft + 104.0f, 466.0f, kControlRight, 494.0f),
        0,
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
    return L"搜索剪贴板\u2026";
}

std::wstring_view PopupSearchDisplayText(std::wstring_view query) {
    return query.empty() ? PopupSearchPlaceholderText() : query;
}

std::wstring PopupEmptyMessage(bool favorites_view, std::wstring_view active_favorite_group) {
    return PopupEmptyMessage(favorites_view, active_favorite_group, false);
}

std::wstring PopupEmptyMessage(bool favorites_view, std::wstring_view, bool has_filters) {
    if (has_filters) {
        return L"没有匹配的记录";
    }
    return favorites_view ? L"还没有收藏，右键条目即可收藏" : L"复制点什么，它会出现在这里";
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

RECT PopupDpiChangedWindowRect(RECT suggested, SIZE desired_size) {
    suggested.right = suggested.left + desired_size.cx;
    suggested.bottom = suggested.top + desired_size.cy;
    return suggested;
}

bool PopupPaintShouldUpdateLayout() {
    return false;
}

bool PopupShouldAnimateHoverWhileResizing(bool resizing_window) {
    return !resizing_window;
}

bool PopupShouldActivateWhenShown(bool keyboard_invocation) {
    // 键盘呼出也必须激活。原因：弹窗一旦是 WS_EX_NOACTIVATE，就永远收不到
    // WM_ACTIVATE(WA_INACTIVE)，「点外面自动收起」只能靠轮询鼠标状态；
    // 而当前台是提权窗口（任务管理器等）时，UIPI 让普通权限进程读不到鼠标按键，
    // 轮询彻底失效。激活弹窗后，点到任何窗口都会收到失活通知 —— 那是窗口管理器
    // 事件，跨权限等级一样可靠。
    // 粘贴目标早已记在 paste_target_ 里，SendPaste 会先 SetForegroundWindow，
    // 所以抢焦点不影响「粘贴回原窗口」。
    (void)keyboard_invocation;
    return true;
}

bool PopupActivationMayAttachThreadInput(bool modifier_held) {
    // 有修饰键按着时绝不借用输入队列。AttachThreadInput 会把两个进程的键盘状态并到一起，
    // 而此时 Alt 的按下已经被钩子吞掉（对方进程没收到），对方仍可能因为共用队列把修饰键
    // 记成一直按住。游戏（永劫无间）对修饰键状态跟踪很严，表现就是呼出剪贴板后 Alt 卡长按。
    return !modifier_held;
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
    // 预览启动时就是 q.focus()：呼出即可打字，搜索框下的墨线也随之从左展开。
    return has_native_edit;
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
    return static_cast<float>(kMetrics.header_height + 2);
}

float PopupTabsTop() {
    return PopupSearchTop() + static_cast<float>(kMetrics.search_height);
}

float PopupToolbarTop() {
    // 工具按钮与标签处于同一栏，垂直居中
    return PopupTabsTop() + static_cast<float>(kMetrics.tab_height - kMetrics.toolbar_height) * 0.5f;
}

float PopupListTop() {
    return PopupTabsTop() + static_cast<float>(kMetrics.tab_height) + 2.0f;
}

float PopupFooterHeight() {
    return kFooterHeight;
}

float PopupListBottomForHeight(int logical_height) {
    return std::max(PopupListTop(), static_cast<float>(logical_height) - kFooterHeight);
}

float PopupSectionHeaderHeight() {
    return kSectionHeaderHeight;
}

PopupSectionPlan PopupSectionPlanFor(size_t item_count, size_t pinned_count) {
    PopupSectionPlan plan;
    plan.pinned_count = std::min(pinned_count, item_count);
    plan.recent_count = item_count - plan.pinned_count;
    plan.show_pinned_header = plan.pinned_count > 0;
    plan.show_recent_header = plan.pinned_count > 0 && plan.recent_count > 0;
    return plan;
}

int PopupFirstVisibleRowIndex(const std::vector<float>& row_tops, float scroll_offset) {
    if (row_tops.empty()) {
        return 0;
    }
    // upper_bound gives the first row strictly below the viewport top; step back to the one
    // that actually covers it. Starting at the upper bound skips the first row whenever the
    // offset equals its top (which is always true at offset 0), leaving a blank strip.
    const auto beyond = std::upper_bound(row_tops.begin(), row_tops.end(), scroll_offset);
    const int covering =
        beyond == row_tops.begin() ? 0 : static_cast<int>(std::distance(row_tops.begin(), beyond)) - 1;
    return std::clamp(covering, 0, static_cast<int>(row_tops.size()) - 1);
}

int PopupDefaultSelectedIndex(const PopupSectionPlan& plan) {
    // 置顶区只是快捷区：呼出弹窗时默认高亮（回车会粘贴的那一条）应该是最近分组的
    // 第一条，而不是被置顶的旧条目。没有最近条目时才回落到列表首项。
    if (plan.recent_count == 0) {
        return 0;
    }
    return static_cast<int>(plan.pinned_count);
}

float PopupSectionedItemTop(size_t item_index, const PopupSectionPlan& plan, float item_height, float header_height) {
    const float pinned_header = plan.show_pinned_header ? header_height : 0.0f;
    if (item_index < plan.pinned_count) {
        return pinned_header + static_cast<float>(item_index) * item_height;
    }
    const float recent_header = plan.show_recent_header ? header_height : 0.0f;
    return pinned_header + static_cast<float>(plan.pinned_count) * item_height + recent_header +
           static_cast<float>(item_index - plan.pinned_count) * item_height;
}

float PopupSectionedListHeight(const PopupSectionPlan& plan, float item_height, float header_height) {
    const float items = static_cast<float>(plan.pinned_count + plan.recent_count) * item_height;
    const float headers = (plan.show_pinned_header ? header_height : 0.0f) +
                          (plan.show_recent_header ? header_height : 0.0f);
    return items + headers;
}

UiRect PopupFooterRectForSize(int logical_width, int logical_height) {
    return Rect(0.0f, PopupListBottomForHeight(logical_height), static_cast<float>(logical_width),
                static_cast<float>(logical_height));
}

PopupHeaderLayout BuildPopupHeaderLayout() {
    return BuildPopupHeaderLayoutForWidth(kMetrics.width);
}

PopupHeaderLayout BuildPopupHeaderLayoutForWidth(int logical_width) {
    logical_width = std::max(logical_width, 1);
    PopupHeaderLayout layout;
    layout.title = Rect(static_cast<float>(kMetrics.margin), 10.0f, 190.0f, 34.0f);
    layout.pin = Rect(static_cast<float>(logical_width - 68), 8.0f,
                      static_cast<float>(logical_width - 40), 36.0f);
    layout.close = Rect(static_cast<float>(logical_width - 38), 8.0f,
                        static_cast<float>(logical_width - 10), 36.0f);
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
    layout.icon = Rect(static_cast<float>(kMetrics.margin), top + 12.5f,
                       static_cast<float>(kMetrics.margin + 15), top + 27.5f);
    layout.text = Rect(static_cast<float>(kMetrics.margin + 24), top + 8.0f,
                       static_cast<float>(logical_width - kMetrics.margin - 28), top + 32.0f);
    layout.clear_button = Rect(static_cast<float>(logical_width - kMetrics.margin - 20), top + 10.0f,
                               static_cast<float>(logical_width - kMetrics.margin), top + 30.0f);
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
    // Ink Ledger：工具按钮与标签同处一栏，靠右排列；常规模式为 28×28 图标按钮，多选模式为文字按钮。
    logical_width = std::max(logical_width, 1);
    const float y = PopupToolbarTop();
    const float bottom = y + static_cast<float>(kMetrics.toolbar_height);
    const float right_edge = static_cast<float>(logical_width) - kBarRightInset;
    constexpr float kIcon = 28.0f;
    constexpr float kIconGap = 2.0f;
    PopupToolbarLayout layout;
    layout.clear_all = Rect(right_edge - kIcon, y, right_edge, bottom);
    layout.multi_select = Rect(layout.clear_all.left - kIconGap - kIcon, y, layout.clear_all.left - kIconGap, bottom);
    layout.filter = Rect(layout.multi_select.left - kIconGap - kIcon, y, layout.multi_select.left - kIconGap, bottom);
    if (multi_select) {
        constexpr float kGap = 2.0f;
        const float text_top = y + 1.0f;
        const float text_bottom = bottom - 1.0f;
        layout.paste_selected = Rect(right_edge - 62.0f, text_top, right_edge, text_bottom);
        layout.delete_selected = Rect(layout.paste_selected.left - kGap - 42.0f, text_top,
                                      layout.paste_selected.left - kGap, text_bottom);
        layout.select_all = Rect(layout.delete_selected.left - kGap - 42.0f, text_top,
                                 layout.delete_selected.left - kGap, text_bottom);
        layout.cancel_multi_select = Rect(layout.select_all.left - kGap - 42.0f, text_top,
                                          layout.select_all.left - kGap, text_bottom);
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
    // 标签左对齐到内容左线；收藏视图下，分组按钮与"添加常用语"紧随标签之后。
    logical_width = std::max(logical_width, 1);
    const float top = PopupTabsTop();
    const float left = static_cast<float>(kMetrics.margin);
    const auto toolbar = BuildPopupToolbarLayoutForWidth(false, logical_width);
    PopupTabsLayout layout;
    layout.history = Rect(left, top + 7.0f, left + 46.0f, top + 33.0f);
    layout.favorites = Rect(layout.history.right + 14.0f, top + 7.0f, layout.history.right + 60.0f, top + 33.0f);
    const float add_size = 28.0f;
    const float group_left = layout.favorites.right + 10.0f;
    const float group_limit = toolbar.filter.left - 6.0f - add_size - 2.0f;
    // 先为 28px 添加按钮及右侧 88px 工具区留位，分组按钮使用剩余宽度。
    const float group_width = std::clamp(group_limit - group_left, 0.0f, 86.0f);
    layout.favorite_group = Rect(group_left, top + 8.0f, group_left + group_width, top + 32.0f);
    layout.add_favorite_phrase = Rect(layout.favorite_group.right + 2.0f, top + 6.0f,
                                      layout.favorite_group.right + 2.0f + add_size, top + 34.0f);
    const UiRect& active = favorites_active ? layout.favorites : layout.history;
    layout.active_indicator = Rect(active.left, top + 32.5f, active.left + 26.0f, top + 34.0f);
    layout.divider = Rect(0.0f, top + static_cast<float>(kMetrics.tab_height) - 0.5f,
                          static_cast<float>(logical_width), top + static_cast<float>(kMetrics.tab_height) + 0.5f);
    return layout;
}

UiRect PopupFavoriteGroupIconRect(const PopupTabsLayout& tabs) {
    const float center_x = (tabs.favorite_group.left + tabs.favorite_group.right) * 0.5f;
    const float center_y = (tabs.favorite_group.top + tabs.favorite_group.bottom) * 0.5f;
    return Rect(center_x - 8.0f, center_y - 8.0f, center_x + 8.0f, center_y + 8.0f);
}

PopupFilterLayout BuildPopupFilterLayout() {
    return BuildPopupFilterLayout(true, 6);
}

int PopupCalendarRowCount(int year, int month) {
    if (month < 1 || month > 12) {
        return 6;
    }
    return (WeekdaySundayFirst(year, month, 1) + DaysInMonth(year, month) + 6) / 7;
}

PopupFilterLayout BuildPopupFilterLayout(bool custom_range, int calendar_rows) {
    return BuildPopupFilterLayout(custom_range, calendar_rows, kMetrics.width);
}

PopupFilterLayout BuildPopupFilterLayout(bool custom_range, int calendar_rows, int logical_width) {
    // Ink Ledger 筛选面板：统一 14px 内边距；所有区块共用同一左右边线。
    constexpr float kPadX = 14.0f;
    constexpr float kSegHeight = 30.0f;
    constexpr float kLabelHeight = 16.0f;
    constexpr float kCalRowPitch = 26.0f; // 日期高 24 + 行距 2
    const float panel_left = 12.0f;
    const float panel_right = static_cast<float>(std::max(logical_width, 1)) - 12.0f;
    const float top = 124.0f;
    const float left = panel_left + kPadX;
    const float right = panel_right - kPadX;
    const UiRect hidden = Rect(-100.0f, -100.0f, -99.0f, -99.0f);

    PopupFilterLayout layout;
    layout.custom_range = custom_range;
    // 头部：标题 / 重置 / 关闭
    layout.close = Rect(right - 18.0f, top + 12.0f, right + 6.0f, top + 36.0f);
    layout.reset = Rect(layout.close.left - 40.0f, top + 14.0f, layout.close.left - 2.0f, top + 34.0f);

    // 类型：标签行 + 四等分分段条
    layout.type_section = Rect(left, top + 48.0f, right, top + 48.0f + kLabelHeight);
    layout.type_bar = Rect(left, layout.type_section.bottom + 6.0f, right, layout.type_section.bottom + 6.0f + kSegHeight);
    {
        const float slot = (layout.type_bar.Width() - 2.0f * 2.0f - 2.0f * 3.0f) / 4.0f;
        UiRect* chips[] = {&layout.text_chip, &layout.image_chip, &layout.file_chip, &layout.link_chip};
        for (int i = 0; i < 4; ++i) {
            const float x = layout.type_bar.left + 2.0f + static_cast<float>(i) * (slot + 2.0f);
            *chips[i] = Rect(x, layout.type_bar.top + 2.0f, x + slot, layout.type_bar.bottom - 2.0f);
        }
    }

    // 时间：标签行（右侧内嵌 开始 → 结束）+ 五段预设
    layout.date_card = Rect(left, layout.type_bar.bottom + 14.0f, right, layout.type_bar.bottom + 14.0f + kLabelHeight);
    layout.end_date = Rect(right - 38.0f, layout.date_card.top - 2.0f, right, layout.date_card.bottom + 2.0f);
    layout.start_date = Rect(layout.end_date.left - 54.0f, layout.end_date.top, layout.end_date.left - 16.0f,
                             layout.end_date.bottom);
    layout.preset_bar = Rect(left, layout.date_card.bottom + 6.0f, right, layout.date_card.bottom + 6.0f + kSegHeight);
    {
        const float slot = (layout.preset_bar.Width() - 2.0f * 2.0f - 2.0f * 4.0f) / 5.0f;
        for (int i = 0; i < 5; ++i) {
            const float x = layout.preset_bar.left + 2.0f + static_cast<float>(i) * (slot + 2.0f);
            layout.presets[i] = Rect(x, layout.preset_bar.top + 2.0f, x + slot, layout.preset_bar.bottom - 2.0f);
        }
    }

    float content_bottom = layout.preset_bar.bottom;
    if (custom_range) {
        const int rows = std::clamp(calendar_rows, 4, 6);
        const float cal_top = layout.preset_bar.bottom + 10.0f;
        layout.calendar_title = Rect(left, cal_top, right - 88.0f, cal_top + 22.0f);
        layout.calendar_next_year = Rect(right - 22.0f, cal_top, right, cal_top + 22.0f);
        layout.calendar_next = Rect(right - 44.0f, cal_top, right - 22.0f, cal_top + 22.0f);
        layout.calendar_prev = Rect(right - 66.0f, cal_top, right - 44.0f, cal_top + 22.0f);
        layout.calendar_prev_year = Rect(right - 88.0f, cal_top, right - 66.0f, cal_top + 22.0f);
        const float grid_top = cal_top + 22.0f + 4.0f + 18.0f + 2.0f;
        layout.calendar_grid = Rect(left, grid_top, right, grid_top + static_cast<float>(rows) * kCalRowPitch - 2.0f);
        layout.calendar = Rect(left, cal_top, right, layout.calendar_grid.bottom);
        content_bottom = layout.calendar.bottom;
    } else {
        layout.calendar = hidden;
        layout.calendar_grid = hidden;
        layout.calendar_title = hidden;
        layout.calendar_prev_year = hidden;
        layout.calendar_prev = hidden;
        layout.calendar_next = hidden;
        layout.calendar_next_year = hidden;
    }

    // 底部：发丝线 + 匹配 N 条 + 完成
    layout.footer_divider = Rect(left, content_bottom + 14.0f, right, content_bottom + 15.0f);
    layout.done = Rect(right - 52.0f, layout.footer_divider.bottom + 12.0f, right,
                       layout.footer_divider.bottom + 40.0f);
    layout.count_text = Rect(left, layout.done.top, layout.done.left - 8.0f, layout.done.bottom);
    layout.panel = Rect(panel_left, top, panel_right, layout.done.bottom + 14.0f);
    return layout;
}

int PopupFilterPresetIndex(PopupFilterTarget target) {
    switch (target) {
    case PopupFilterTarget::PresetAll:
        return 0;
    case PopupFilterTarget::PresetToday:
        return 1;
    case PopupFilterTarget::PresetWeek:
        return 2;
    case PopupFilterTarget::PresetMonth:
        return 3;
    case PopupFilterTarget::PresetCustom:
        return 4;
    default:
        return -1;
    }
}

size_t PopupFavoriteGroupMenuVisibleGroupCount(size_t group_count) {
    return std::min<size_t>(group_count, 8);
}

PopupFavoriteGroupMenuLayout BuildPopupFavoriteGroupMenuLayout(size_t group_count) {
    // Ink Ledger 菜单：CSS 固定 left 92 / top 124 / width 180，内边距 5，行高 30。
    constexpr float kPanelWidth = 180.0f;
    constexpr float kPanelPadding = 5.0f;
    constexpr float kRowHeight = 30.0f;
    constexpr float kDividerGap = 9.0f;

    const float left = 92.0f;
    const float right = left + kPanelWidth;
    const float top = 124.0f;
    const size_t visible_groups = PopupFavoriteGroupMenuVisibleGroupCount(group_count);
    const float groups_height = static_cast<float>(visible_groups) * kRowHeight;
    const float panel_height = kPanelPadding * 2.0f + kRowHeight + kDividerGap + groups_height + kDividerGap + kRowHeight;

    PopupFavoriteGroupMenuLayout layout;
    layout.panel = Rect(left, top, right, top + panel_height);
    layout.all_favorites = Rect(left + kPanelPadding, top + kPanelPadding,
                                right - kPanelPadding, top + kPanelPadding + kRowHeight);
    layout.first_divider = Rect(left + 11.0f, layout.all_favorites.bottom + 4.0f,
                                right - 11.0f, layout.all_favorites.bottom + 5.0f);
    layout.group_rows = Rect(left + kPanelPadding, layout.all_favorites.bottom + kDividerGap,
                             right - kPanelPadding, layout.all_favorites.bottom + kDividerGap + groups_height);
    layout.second_divider = Rect(left + 11.0f, layout.group_rows.bottom + 4.0f,
                                 right - 11.0f, layout.group_rows.bottom + 5.0f);
    layout.new_group = Rect(left + kPanelPadding, layout.group_rows.bottom + kDividerGap,
                            right - kPanelPadding, layout.group_rows.bottom + kDividerGap + kRowHeight);
    return layout;
}

UiRect PopupFavoriteGroupMenuGroupRect(const PopupFavoriteGroupMenuLayout& layout, size_t index) {
    constexpr float kRowHeight = 30.0f;
    const float top = layout.group_rows.top + static_cast<float>(index) * kRowHeight;
    return Rect(layout.group_rows.left, top, layout.group_rows.right, top + kRowHeight);
}

UiRect PopupFavoriteGroupMenuDeleteRect(const UiRect& group_row) {
    const float center_y = (group_row.top + group_row.bottom) * 0.5f;
    return Rect(group_row.right - 24.0f, center_y - 10.0f, group_row.right - 4.0f, center_y + 10.0f);
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
    return layout.reset;
}

UiRect PopupFilterArrowGlyphRect(const UiRect& arrow_button) {
    const float center_x = (arrow_button.left + arrow_button.right) * 0.5f;
    const float center_y = (arrow_button.top + arrow_button.bottom) * 0.5f;
    return Rect(center_x - 5.0f, center_y - 6.0f, center_x + 5.0f, center_y + 6.0f);
}

std::vector<PopupCalendarWeekdayLabel> BuildPopupCalendarWeekdayLabels(const PopupFilterLayout& layout) {
    constexpr wchar_t kWeekdays[] = {L'\u65e5', L'\u4e00', L'\u4e8c', L'\u4e09', L'\u56db', L'\u4e94', L'\u516d'};
    std::vector<PopupCalendarWeekdayLabel> labels;
    if (!layout.custom_range) {
        return labels;
    }
    labels.reserve(7);
    const float cell_width = layout.calendar_grid.Width() / 7.0f;
    for (int i = 0; i < 7; ++i) {
        const float left = layout.calendar_grid.left + static_cast<float>(i) * cell_width;
        labels.push_back(PopupCalendarWeekdayLabel{
            kWeekdays[i],
            Rect(left, layout.calendar_grid.top - 20.0f, left + cell_width, layout.calendar_grid.top - 2.0f),
        });
    }
    return labels;
}

PopupCardLayout BuildPopupCardLayout(bool multi_select, float top) {
    // Ink Ledger 行：左右各 8px 外边距；媒体列 52px；正文；右侧时间 + 展开箭头。
    const float left = 8.0f;
    const float right = static_cast<float>(kMetrics.width) - 8.0f;
    const float bottom = top + static_cast<float>(kMetrics.card_height);
    const float shift = multi_select ? 28.0f : 0.0f;

    PopupCardLayout layout;
    layout.card = Rect(left, top, right, bottom);
    layout.stripe = Rect(left, top, left, top);
    const float media_center = left + 26.0f + shift;
    layout.image_preview = Rect(media_center - 17.0f, top + 11.0f, media_center + 17.0f, top + 45.0f);
    layout.kind_badge = Rect(media_center - 15.0f, top + 11.0f, media_center + 15.0f, top + 41.0f);
    layout.file_icon = Rect(media_center - 13.5f, top + 11.0f, media_center + 13.5f, top + 44.0f);
    layout.checkbox = Rect(left + 13.0f, top + 18.0f, left + 28.0f, top + 33.0f);
    const float text_left = left + 52.0f + shift;
    // CDP 实测五字符 HH:mm（Consolas 10.5px）宽 28.875px。
    layout.time = Rect(right - 12.0f - 28.875f, top + 10.0f, right - 12.0f, top + 29.0f);
    layout.menu = Rect(right - 12.0f, top + 10.0f, right - 12.0f, top + 29.0f);
    layout.expand = Rect(right - 32.0f, top + 32.0f, right - 12.0f, top + 50.0f);
    layout.title = Rect(text_left, top + 10.0f, layout.time.left - 8.0f, top + 29.0f);
    layout.meta = Rect(text_left, top + 31.0f, layout.title.right, top + 47.0f);
    return layout;
}

UiRect PopupCardKindIconRect(const PopupCardLayout& card) {
    const float center_x = (card.kind_badge.left + card.kind_badge.right) * 0.5f;
    const float center_y = (card.kind_badge.top + card.kind_badge.bottom) * 0.5f;
    return Rect(center_x - 7.0f, center_y - 7.0f, center_x + 7.0f, center_y + 7.0f);
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
    // 上限放到 480：多行输入框（微信聊天框实测 670x159）本来就比单行输入框高得多，
    // 原来的 120 会把这类 Qt 自绘输入框直接判成「不是输入框」，于是只能退回屏幕角落。
    constexpr int kMaxInputHeight = 480;
    return rect.right > rect.left &&
           rect.bottom > rect.top &&
           rect.right - rect.left >= kMinInputWidth &&
           rect.bottom - rect.top >= kMinInputHeight &&
           rect.bottom - rect.top <= kMaxInputHeight &&
           rect.bottom > 1 &&
           (rect.left > 1 || rect.top > 1);
}

POINT PopupTextInputRectAnchor(RECT rect, unsigned dpi) {
    // 多行输入框（微信聊天框实测 670x159）的文字从顶部内边距开始排，
    // 所以「光标所在行」≈ rect.top + 10dip；原来的锚点直接用 rect.top，
    // 对高输入框会把弹窗顶到框外太远处。
    const int text_inset = ScalePopupMetricForDpi(8, dpi);
    const int vertical_inset = ScalePopupMetricForDpi(10, dpi);
    return POINT{rect.left + text_inset, rect.top + vertical_inset};
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
    // 输入框往往是整个可编辑区域（微信聊天框 670x159）。这时锚点要收进框内，
    // 而不是拿整个框的右边当锚，否则弹窗会飘到输入框外侧很远。
    const bool input_rect = PopupTextInputRectUsable(avoid);
    const bool wide_input = input_rect && avoid.right - avoid.left > ScalePopupMetricForDpi(420, dpi);
    const POINT input_anchor = input_rect ? PopupTextInputRectAnchor(avoid, dpi) : POINT{avoid.right, avoid.top};
    const int horizontal_anchor = wide_input ? input_anchor.x : avoid.right;

    const int soft_min_x = work.left + work_gap;
    const int soft_max_x = work.right - size.cx - work_gap;
    const int hard_min_y = work.top;
    const int hard_max_y = work.bottom - size.cy;

    const int right_x = horizontal_anchor + corner_gap;
    const int x = ClampToRange(right_x, soft_min_x, soft_max_x);

    // 优先贴在光标行「上方」（和原生 Win+V 一致，不遮住正在输入的内容），
    // 上方放不下再放「下方」。之前是对上下两个候选都先 clamp 再比重叠，
    // 结果两种都被挤到同一个贴边位置，看起来就像固定在屏幕中间。
    const int anchor_top = input_rect ? input_anchor.y : avoid.top;
    const int anchor_bottom = input_rect ? std::min(avoid.bottom, input_anchor.y + ScalePopupMetricForDpi(28, dpi))
                                         : avoid.bottom;
    const int above_y = anchor_top - size.cy - corner_gap;
    const int below_y = anchor_bottom + corner_gap;
    const bool fits_above = above_y >= hard_min_y;
    const bool fits_below = below_y <= hard_max_y;

    int y = 0;
    if (fits_above) {
        y = above_y;
    } else if (fits_below) {
        y = below_y;
    } else {
        // 两边都放不下（弹窗比上下剩余空间都高）：贴着输入框那一侧，最少遮挡输入文字。
        const int space_above = anchor_top - work.top;
        const int space_below = work.bottom - anchor_bottom;
        y = space_above >= space_below ? ClampToRange(above_y, hard_min_y, hard_max_y)
                                       : ClampToRange(below_y, hard_min_y, hard_max_y);
    }

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

POINT PopupTargetWindowFallback(RECT target, SIZE size, RECT work, unsigned dpi) {
    const int gap = ScalePopupMetricForDpi(12, dpi);
    // 目标窗口如果比弹窗还小（或几乎一样大），就没法「放在窗口里」：
    // 这时退回窗口右下角外侧一点，至少紧挨着它。
    const int x_right = target.right - size.cx - gap;
    const int y_bottom = target.bottom - size.cy - gap;
    const int x = ClampToRange(x_right, work.left + gap, work.right - size.cx - gap);
    const int y = ClampToRange(y_bottom, work.top + gap, work.bottom - size.cy - gap);
    return POINT{x, y};
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

bool PopupShouldHideWhenForegroundChanges(bool pinned_open, bool prompt_open, bool moving_window, bool visible,
                                          bool transient_hide_suppressed, HWND popup, HWND baseline_foreground,
                                          HWND foreground) {
    if (!visible || pinned_open || prompt_open || moving_window || transient_hide_suppressed) {
        return false;
    }
    // 前台落在弹窗自己或它的子窗口上（点搜索框时顶层父窗口会被激活）：这是「还在弹窗里」，
    // 不能算离开。
    if (foreground && popup && (foreground == popup || IsChild(popup, foreground))) {
        return false;
    }
    // 没有基准（呼出时前台未知）就不做判断，避免误收。
    if (!baseline_foreground) {
        return false;
    }
    // 前台没变，或者变成了空（切换桌面/锁屏的瞬间），都不算离开。
    if (!foreground || foreground == baseline_foreground) {
        return false;
    }
    return true;
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
    if (month < 1 || month > 12 || !layout.custom_range) {
        return cells;
    }

    const float cell_width = layout.calendar_grid.Width() / 7.0f;
    constexpr float kRowPitch = 26.0f;
    const int first_weekday = WeekdaySundayFirst(year, month, 1);
    const int days = DaysInMonth(year, month);
    cells.reserve(static_cast<size_t>(days));

    for (int day = 1; day <= days; ++day) {
        const int zero_based_cell = first_weekday + day - 1;
        const int row = zero_based_cell / 7;
        const int col = zero_based_cell % 7;
        const float left = layout.calendar_grid.left + col * cell_width;
        const float top = layout.calendar_grid.top + row * kRowPitch;
        cells.push_back(PopupCalendarCell{PopupCalendarDate{year, month, day},
                                          Rect(left, top, left + cell_width, top + 24.0f)});
    }
    return cells;
}

std::vector<PopupCalendarRangeSegment> BuildPopupCalendarRangeSegments(
    const PopupFilterLayout& layout, int year, int month, const PopupDateRangeState& state) {
    // 区间：每行一条连续的圆角色带，两端收在端点圆心外 12px（与 24px 墨点同径）。
    std::vector<PopupCalendarRangeSegment> segments;
    if (!state.start || !state.end || *state.end < *state.start) {
        return segments;
    }

    const auto cells = BuildPopupCalendarCells(layout, year, month);
    UiRect current{};
    bool active = false;
    float current_top = -1.0f;
    constexpr float kHalf = 12.0f;

    auto flush = [&]() {
        if (active) {
            segments.push_back(PopupCalendarRangeSegment{current, true, true});
            active = false;
        }
    };

    for (const auto& cell : cells) {
        if (cell.date < *state.start || *state.end < cell.date) {
            flush();
            continue;
        }
        const float cx = (cell.rect.left + cell.rect.right) * 0.5f;
        const float cy = (cell.rect.top + cell.rect.bottom) * 0.5f;
        const UiRect part = Rect(std::max(cell.rect.left, cx - kHalf), cy - kHalf,
                                 std::min(cell.rect.right, cx + kHalf), cy + kHalf);
        if (!active || cell.rect.top != current_top) {
            flush();
            current = part;
            active = true;
            current_top = cell.rect.top;
            continue;
        }
        current.right = part.right;
    }
    flush();
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
    if (Contains(layout.reset, x, y)) return PopupFilterTarget::Reset;
    if (Contains(layout.text_chip, x, y)) return PopupFilterTarget::TextChip;
    if (Contains(layout.image_chip, x, y)) return PopupFilterTarget::ImageChip;
    if (Contains(layout.file_chip, x, y)) return PopupFilterTarget::FileChip;
    if (Contains(layout.link_chip, x, y)) return PopupFilterTarget::LinkChip;
    if (Contains(layout.start_date, x, y)) return PopupFilterTarget::StartDate;
    if (Contains(layout.end_date, x, y)) return PopupFilterTarget::EndDate;
    constexpr PopupFilterTarget kPresets[] = {PopupFilterTarget::PresetAll, PopupFilterTarget::PresetToday,
                                              PopupFilterTarget::PresetWeek, PopupFilterTarget::PresetMonth,
                                              PopupFilterTarget::PresetCustom};
    for (int i = 0; i < 5; ++i) {
        if (Contains(layout.presets[i], x, y)) return kPresets[i];
    }
    if (layout.custom_range) {
        if (Contains(layout.calendar_prev_year, x, y)) return PopupFilterTarget::CalendarPreviousYear;
        if (Contains(layout.calendar_prev, x, y)) return PopupFilterTarget::CalendarPrevious;
        if (Contains(layout.calendar_next, x, y)) return PopupFilterTarget::CalendarNext;
        if (Contains(layout.calendar_next_year, x, y)) return PopupFilterTarget::CalendarNextYear;
        if (HitTestPopupCalendarDate(layout, year, month, x, y)) return PopupFilterTarget::CalendarDate;
    }
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

PopupCalendarDate PopupAddDays(PopupCalendarDate date, int days) {
    // 民用日历 <-> 天数换算（Howard Hinnant 算法），避免时区/DST 干扰。
    auto days_from_civil = [](int year, int month, int day) -> int64_t {
        year -= month <= 2 ? 1 : 0;
        const int era = (year >= 0 ? year : year - 399) / 400;
        const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
        const unsigned day_of_year =
            static_cast<unsigned>((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
        const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
        return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(day_of_era) - 719468;
    };
    auto civil_from_days = [](int64_t z) -> PopupCalendarDate {
        z += 719468;
        const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned day_of_era = static_cast<unsigned>(z - era * 146097);
        const unsigned year_of_era =
            (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
        int year = static_cast<int>(year_of_era) + static_cast<int>(era) * 400;
        const unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
        const unsigned mp = (5 * day_of_year + 2) / 153;
        const unsigned day = day_of_year - (153 * mp + 2) / 5 + 1;
        const unsigned month = mp + (mp < 10 ? 3 : -9);
        year += month <= 2 ? 1 : 0;
        return PopupCalendarDate{year, static_cast<int>(month), static_cast<int>(day)};
    };
    return civil_from_days(days_from_civil(date.year, date.month, date.day) + days);
}

PopupPresetDateRange PopupPresetDateRangeFor(int preset_index, PopupCalendarDate today) {
    PopupPresetDateRange range;
    switch (preset_index) {
    case 0: // 不限
        break;
    case 1: // 今天
        range.start = today;
        range.end = today;
        break;
    case 2: // 近 7 天（含今天）
        range.start = PopupAddDays(today, -6);
        range.end = today;
        break;
    case 3: // 近 30 天（含今天）
        range.start = PopupAddDays(today, -29);
        range.end = today;
        break;
    default: // 自定义：由日历决定，不动现有区间
        break;
    }
    return range;
}

} // namespace ClipSoul
