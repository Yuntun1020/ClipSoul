#include "TestHarness.h"

#include "ClipSoul/PopupLayout.h"

#include <cmath>
#include <string>

namespace {
void RequireInsidePanel(const ClipSoul::UiRect& rect) {
    const auto metrics = ClipSoul::PopupMetrics();
    REQUIRE(rect.left >= metrics.margin);
    REQUIRE(rect.right <= metrics.width - metrics.margin);
    REQUIRE(rect.Width() > 0.0f);
}
} // namespace

TEST_CASE(PopupToolbarButtonsFitCompactPanel) {
    const auto layout = ClipSoul::BuildPopupToolbarLayout(false);

    RequireInsidePanel(layout.filter);
    RequireInsidePanel(layout.multi_select);
    RequireInsidePanel(layout.clear_all);
    REQUIRE(!ClipSoul::RectsOverlap(layout.filter, layout.multi_select));
    REQUIRE(!ClipSoul::RectsOverlap(layout.multi_select, layout.clear_all));
    REQUIRE(layout.filter.Width() >= 88.0f);
    REQUIRE(layout.clear_all.Width() >= 86.0f);
}

TEST_CASE(PopupMetricsScaleToPhysicalPixelsForDpiAwareWindow) {
    const auto metrics = ClipSoul::PopupMetrics();

    REQUIRE_EQ(ClipSoul::ScalePopupMetricForDpi(metrics.width, 96), 340);
    REQUIRE_EQ(ClipSoul::ScalePopupMetricForDpi(metrics.height, 96), 560);
    REQUIRE_EQ(ClipSoul::ScalePopupMetricForDpi(metrics.width, 144), 510);
    REQUIRE_EQ(ClipSoul::ScalePopupMetricForDpi(metrics.height, 144), 840);
    REQUIRE_EQ(ClipSoul::ScalePopupMetricForDpi(metrics.width, 192), 680);
    REQUIRE_EQ(ClipSoul::ScalePopupMetricForDpi(metrics.height, 192), 1120);
}

TEST_CASE(PopupRoundedWindowRegionScalesCornerRadiusWithDpi) {
    const auto metrics = ClipSoul::PopupMetrics();

    REQUIRE_EQ(metrics.corner_radius, 18);
    REQUIRE(metrics.glass_tint_opacity <= 0.2f);
    REQUIRE_EQ(ClipSoul::ScalePopupMetricForDpi(metrics.corner_radius, 96), 18);
    REQUIRE_EQ(ClipSoul::ScalePopupMetricForDpi(metrics.corner_radius, 144), 27);
    REQUIRE_EQ(ClipSoul::ScalePopupMetricForDpi(metrics.corner_radius, 192), 36);
}

TEST_CASE(PopupHeaderButtonsUseCompactIconTargets) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto layout = ClipSoul::BuildPopupHeaderLayout();

    RequireInsidePanel(layout.title);
    RequireInsidePanel(layout.pin);
    RequireInsidePanel(layout.close);
    REQUIRE(!ClipSoul::RectsOverlap(layout.pin, layout.close));
    REQUIRE_EQ(metrics.header_button_size, 22);
    REQUIRE(layout.pin.Width() <= static_cast<float>(metrics.header_button_size));
    REQUIRE(layout.pin.Width() >= 22.0f);
    REQUIRE(layout.pin.Height() >= 22.0f);
    REQUIRE(layout.close.Width() <= static_cast<float>(metrics.header_button_size));
    REQUIRE(layout.close.right > layout.pin.right);

    const auto pin_icon = ClipSoul::BuildPopupHeaderPinIconRect(layout);
    REQUIRE(pin_icon.Width() >= 14.0f);
    REQUIRE(pin_icon.Width() <= 15.0f);
    REQUIRE(pin_icon.Height() >= 16.0f);
    REQUIRE(pin_icon.Height() <= 18.0f);
    REQUIRE(pin_icon.left >= layout.pin.left);
    REQUIRE(pin_icon.right <= layout.pin.right);

    const auto close_icon = ClipSoul::BuildPopupHeaderCloseIconRect(layout);
    REQUIRE(close_icon.Width() >= 14.0f);
    REQUIRE(close_icon.Height() >= 14.0f);
    REQUIRE(close_icon.left >= layout.close.left);
    REQUIRE(close_icon.right <= layout.close.right);
}

TEST_CASE(PopupToolbarIconsUseStableCompactSize) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto layout = ClipSoul::BuildPopupToolbarLayout(false);

    REQUIRE_EQ(metrics.toolbar_icon_size, 14);
    REQUIRE(layout.filter.Width() >= metrics.toolbar_icon_size + 54.0f);
    REQUIRE(layout.multi_select.Width() >= metrics.toolbar_icon_size + 42.0f);
    REQUIRE(layout.clear_all.Width() >= metrics.toolbar_icon_size + 72.0f);
}

TEST_CASE(PopupIconSlotsUseOutlinedIdleAndFilledActiveStates) {
    REQUIRE_EQ(ClipSoul::PopupPinIconSlot(false), ClipSoul::PopupIconAssetSlot::PinActive);
    REQUIRE_EQ(ClipSoul::PopupPinIconSlot(true), ClipSoul::PopupIconAssetSlot::Pin);
    REQUIRE_EQ(ClipSoul::PopupFilterIconSlot(false), ClipSoul::PopupIconAssetSlot::FilterActive);
    REQUIRE_EQ(ClipSoul::PopupFilterIconSlot(true), ClipSoul::PopupIconAssetSlot::Filter);
    REQUIRE_EQ(ClipSoul::PopupMultiSelectIconSlot(false), ClipSoul::PopupIconAssetSlot::MultiSelect);
    REQUIRE_EQ(ClipSoul::PopupMultiSelectIconSlot(true), ClipSoul::PopupIconAssetSlot::MultiSelectActive);
    REQUIRE_EQ(ClipSoul::PopupPasteSelectedIconSlot(), ClipSoul::PopupIconAssetSlot::MultiSelectActive);
}

TEST_CASE(PopupPinnedWindowStaysVisibleAfterPaste) {
    REQUIRE(ClipSoul::ShouldHidePopupAfterPaste(false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterPaste(true));
}

TEST_CASE(PopupContextMenuLabelsReflectCurrentItemState) {
    REQUIRE_EQ(std::wstring(ClipSoul::PopupFavoriteMenuLabel(false)), std::wstring(L"收藏"));
    REQUIRE_EQ(std::wstring(ClipSoul::PopupFavoriteMenuLabel(true)), std::wstring(L"取消收藏"));
    REQUIRE_EQ(std::wstring(ClipSoul::PopupPinMenuLabel(false)), std::wstring(L"置顶"));
    REQUIRE_EQ(std::wstring(ClipSoul::PopupPinMenuLabel(true)), std::wstring(L"取消置顶"));
}

TEST_CASE(PopupHeaderDragAreaExcludesControlsAndContent) {
    const auto header = ClipSoul::BuildPopupHeaderLayout();

    REQUIRE(ClipSoul::IsPopupHeaderDragArea(80.0f, 22.0f));
    REQUIRE(!ClipSoul::IsPopupHeaderDragArea((header.pin.left + header.pin.right) / 2.0f,
                                             (header.pin.top + header.pin.bottom) / 2.0f));
    REQUIRE(!ClipSoul::IsPopupHeaderDragArea((header.close.left + header.close.right) / 2.0f,
                                             (header.close.top + header.close.bottom) / 2.0f));
    REQUIRE(!ClipSoul::IsPopupHeaderDragArea(80.0f, ClipSoul::PopupSearchTop() + 8.0f));
}

TEST_CASE(PopupSearchFieldHasVisiblePlaceholderAndTextArea) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto search = ClipSoul::BuildPopupSearchLayout();

    REQUIRE_EQ(std::wstring(ClipSoul::PopupSearchPlaceholderText()), std::wstring(L"\u641c\u7d22\u5386\u53f2\u8bb0\u5f55..."));
    RequireInsidePanel(search.box);
    REQUIRE(search.text.left > search.icon.right + 8.0f);
    REQUIRE(search.text.right <= static_cast<float>(metrics.width - metrics.margin - 12));
    REQUIRE(search.text.Height() >= 20.0f);
    REQUIRE(search.text.top > search.box.top);
    REQUIRE(search.text.bottom < search.box.bottom);
}

TEST_CASE(PopupSearchDisplayTextShowsQueryOrPlaceholder) {
    REQUIRE_EQ(std::wstring(ClipSoul::PopupSearchDisplayText(L"")),
               std::wstring(L"\u641c\u7d22\u5386\u53f2\u8bb0\u5f55..."));
    REQUIRE_EQ(std::wstring(ClipSoul::PopupSearchDisplayText(L"alpha")), std::wstring(L"alpha"));
    REQUIRE(ClipSoul::PopupSearchCaretVisible(true, true));
    REQUIRE(!ClipSoul::PopupSearchCaretVisible(true, false));
    REQUIRE(!ClipSoul::PopupSearchCaretVisible(false, true));
    REQUIRE(ClipSoul::PopupSearchAcceptsTextInput(true));
    REQUIRE(!ClipSoul::PopupSearchAcceptsTextInput(false));
    REQUIRE(ClipSoul::PopupSearchShouldUpdateImePosition(true, false));
    REQUIRE(!ClipSoul::PopupSearchShouldUpdateImePosition(false, false));
    REQUIRE(!ClipSoul::PopupSearchShouldUpdateImePosition(true, true));
    REQUIRE(ClipSoul::PopupSearchDeletesOnChar(L'\b'));
    REQUIRE(!ClipSoul::PopupSearchDeletesOnKeyDown(VK_BACK));
    REQUIRE(ClipSoul::PopupSearchAppendsChar(L'a'));
    REQUIRE(!ClipSoul::PopupSearchAppendsChar(L'\b'));
    REQUIRE_EQ(ClipSoul::PopupSearchFocusProgress(true, false, 0.0f), 1.0f);
    REQUIRE_EQ(ClipSoul::PopupSearchFocusProgress(false, false, 1.0f), 0.0f);
    REQUIRE_EQ(ClipSoul::PopupSearchFocusProgress(false, true, 0.45f), 0.45f);
    REQUIRE_EQ(ClipSoul::PopupSearchFocusProgress(false, true, 2.0f), 1.0f);

    const auto layout = ClipSoul::BuildPopupSearchLayout();
    REQUIRE_EQ(ClipSoul::ClampPopupSearchCaretX(layout, 0.0f), layout.text.left);
    REQUIRE_EQ(ClipSoul::ClampPopupSearchCaretX(layout, 9999.0f), layout.text.right - 2.0f);
    REQUIRE(ClipSoul::ClampPopupSearchCaretX(layout, 28.0f) > layout.text.left + 20.0f);

    const auto ime_anchor = ClipSoul::PopupSearchImeAnchorDips(layout, 28.0f);
    REQUIRE(ime_anchor.x > static_cast<LONG>(layout.text.left + 20.0f));
    REQUIRE(ime_anchor.x <= static_cast<LONG>(layout.text.right));
    REQUIRE(ime_anchor.y > static_cast<LONG>(layout.text.top));
    REQUIRE(ime_anchor.y <= static_cast<LONG>(layout.box.bottom));
}

TEST_CASE(PopupMultiSelectToolbarButtonsFitCompactPanel) {
    const auto layout = ClipSoul::BuildPopupToolbarLayout(true);

    RequireInsidePanel(layout.cancel_multi_select);
    RequireInsidePanel(layout.select_all);
    RequireInsidePanel(layout.delete_selected);
    RequireInsidePanel(layout.paste_selected);
    REQUIRE(!ClipSoul::RectsOverlap(layout.cancel_multi_select, layout.select_all));
    REQUIRE(!ClipSoul::RectsOverlap(layout.select_all, layout.delete_selected));
    REQUIRE(!ClipSoul::RectsOverlap(layout.cancel_multi_select, layout.delete_selected));
    REQUIRE(!ClipSoul::RectsOverlap(layout.delete_selected, layout.paste_selected));
    REQUIRE(layout.cancel_multi_select.Width() >= 58.0f);
    REQUIRE(layout.select_all.Width() >= 58.0f);
    REQUIRE(layout.delete_selected.Width() >= 88.0f);
    REQUIRE(layout.paste_selected.Width() >= 90.0f);
    REQUIRE(ClipSoul::PopupToolbarLabelRect(layout.delete_selected, false).Width() >= 58.0f);
    REQUIRE(ClipSoul::PopupToolbarLabelRect(layout.paste_selected, false).Width() >= 60.0f);
}

TEST_CASE(PopupCardTextReservesTimeAndMenuArea) {
    const auto normal = ClipSoul::BuildPopupCardLayout(false, 168.0f);
    const auto multi = ClipSoul::BuildPopupCardLayout(true, 168.0f);

    REQUIRE(normal.title.right <= normal.time.left - 8.0f);
    REQUIRE(normal.time.right <= normal.menu.left);
    REQUIRE(normal.title.left >= normal.stripe.right + 10.0f);
    REQUIRE(normal.title.Width() >= 154.0f);
    REQUIRE(multi.title.left > normal.title.left);
    REQUIRE(multi.title.right == normal.title.right);
}

TEST_CASE(PopupCardMediaRectsSupportImagePreviewAndFileIcon) {
    const auto normal = ClipSoul::BuildPopupCardLayout(false, 168.0f);
    const auto multi = ClipSoul::BuildPopupCardLayout(true, 168.0f);

    REQUIRE(normal.image_preview.Width() >= 62.0f);
    REQUIRE(normal.image_preview.Width() <= 64.0f);
    REQUIRE(normal.image_preview.Height() >= 62.0f);
    REQUIRE(normal.image_preview.Height() <= 64.0f);
    REQUIRE(normal.image_preview.left <= normal.stripe.left);
    REQUIRE(normal.title.left >= normal.image_preview.right + 8.0f);
    REQUIRE(normal.file_icon.Width() >= 42.0f);
    REQUIRE(normal.file_icon.Width() <= 46.0f);
    REQUIRE(normal.file_icon.Height() >= 42.0f);
    REQUIRE(normal.file_icon.Height() <= 46.0f);
    REQUIRE(normal.file_icon.left > normal.image_preview.left);
    REQUIRE(normal.file_icon.right < normal.image_preview.right);
    REQUIRE(multi.image_preview.left > normal.image_preview.left);
    REQUIRE(multi.title.left > normal.title.left);
}

TEST_CASE(PopupImagePreviewFitPreservesAspectRatioInsideBounds) {
    const ClipSoul::UiRect bounds{10.0f, 20.0f, 68.0f, 78.0f};

    const auto wide = ClipSoul::FitImageRectToBounds(1600.0f, 900.0f, bounds);
    REQUIRE(wide.left >= bounds.left);
    REQUIRE(wide.right <= bounds.right);
    REQUIRE(wide.top > bounds.top);
    REQUIRE(wide.bottom < bounds.bottom);
    REQUIRE(std::abs((wide.Width() / wide.Height()) - (1600.0f / 900.0f)) < 0.01f);

    const auto tall = ClipSoul::FitImageRectToBounds(600.0f, 1200.0f, bounds);
    REQUIRE(tall.left > bounds.left);
    REQUIRE(tall.right < bounds.right);
    REQUIRE(tall.top >= bounds.top);
    REQUIRE(tall.bottom <= bounds.bottom);
    REQUIRE(std::abs((tall.Width() / tall.Height()) - 0.5f) < 0.01f);
}

TEST_CASE(PopupWindowHeightStaysFixedForEmptyFavorites) {
    const auto metrics = ClipSoul::PopupMetrics();

    REQUIRE_EQ(ClipSoul::PopupHeightForVisibleItems(0), metrics.height);
    REQUIRE_EQ(ClipSoul::PopupHeightForVisibleItems(1), metrics.height);
    REQUIRE_EQ(ClipSoul::PopupHeightForVisibleItems(5), metrics.height);
}

TEST_CASE(PopupFixedHeightShowsFiveCompactCards) {
    const auto metrics = ClipSoul::PopupMetrics();
    const int available = metrics.height - static_cast<int>(ClipSoul::PopupListTop());
    const int five_cards = metrics.card_height * 5 + metrics.card_gap * 4;

    REQUIRE(available >= five_cards);
    REQUIRE_EQ(ClipSoul::PopupVisibleCardCapacity(), 5);
}

TEST_CASE(PopupListScrollOffsetClampsToAvailableRows) {
    REQUIRE_EQ(ClipSoul::ClampPopupScrollOffset(5, 4), 0);
    REQUIRE_EQ(ClipSoul::ClampPopupScrollOffset(9, 0), 0);
    REQUIRE_EQ(ClipSoul::ClampPopupScrollOffset(9, 99), 4);
    REQUIRE_EQ(ClipSoul::ClampPopupScrollOffset(9, -2), 0);
}

TEST_CASE(PopupMouseWheelMovesVisibleHistoryWindow) {
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetAfterWheel(9, 0, -120), 1);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetAfterWheel(9, 1, 120), 0);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetAfterWheel(9, 4, -120), 4);
}

TEST_CASE(PopupTabsUseSegmentedTextLayoutNotButtons) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto history = ClipSoul::BuildPopupTabsLayout(false);
    const auto favorites = ClipSoul::BuildPopupTabsLayout(true);

    RequireInsidePanel(history.history);
    RequireInsidePanel(history.favorites);
    REQUIRE(!ClipSoul::RectsOverlap(history.history, history.favorites));
    REQUIRE(history.history.Width() >= 64.0f);
    REQUIRE(history.favorites.Width() >= 74.0f);
    REQUIRE(history.history.right == history.favorites.left);
    const float group_center = (history.history.left + history.favorites.right) / 2.0f;
    REQUIRE(std::abs(group_center - metrics.width / 2.0f) <= 1.0f);
    REQUIRE(history.active_indicator.bottom <= history.divider.bottom);
    REQUIRE(history.active_indicator.left > history.history.left);
    REQUIRE(history.active_indicator.right < history.history.right);
    REQUIRE(history.divider.left == static_cast<float>(metrics.margin));
    REQUIRE(history.divider.right == static_cast<float>(metrics.width - metrics.margin));
    REQUIRE(favorites.active_indicator.left > history.active_indicator.left);
    REQUIRE(favorites.add_favorite_phrase.Width() >= 24.0f);
    REQUIRE(favorites.add_favorite_phrase.right <= static_cast<float>(metrics.width - metrics.margin));
    REQUIRE(!ClipSoul::RectsOverlap(favorites.add_favorite_phrase, favorites.favorites));
}

TEST_CASE(PopupFilterPopoverContainsDateCalendarAndReset) {
    const auto layout = ClipSoul::BuildPopupFilterLayout();

    RequireInsidePanel(layout.panel);
    REQUIRE(layout.panel.Width() >= 300.0f);
    REQUIRE(layout.panel.Height() >= 360.0f);
    REQUIRE(layout.type_section.top > layout.panel.top);
    REQUIRE(layout.text_chip.top > layout.type_section.top);
    REQUIRE(layout.image_chip.left > layout.text_chip.right);
    REQUIRE(layout.file_chip.top > layout.text_chip.bottom);
    REQUIRE(layout.link_chip.left > layout.file_chip.right);
    REQUIRE(layout.date_card.top > layout.file_chip.bottom);
    REQUIRE(layout.start_date.top > layout.date_card.top);
    REQUIRE(layout.end_date.left > layout.start_date.right);
    REQUIRE(layout.calendar.top > layout.date_card.bottom);
    REQUIRE(layout.calendar.Height() >= 150.0f);
    REQUIRE(layout.reset.top > layout.calendar.bottom);
    REQUIRE(layout.done.left > layout.reset.right);
    REQUIRE(layout.done.bottom <= layout.panel.bottom);

    const auto reset_visual = ClipSoul::PopupFilterResetVisualRect(layout);
    REQUIRE(reset_visual.left == layout.reset.left);
    REQUIRE(reset_visual.Width() <= 52.0f);
    REQUIRE(reset_visual.Height() >= 20.0f);
    REQUIRE(reset_visual.top >= layout.calendar.bottom);
    REQUIRE(reset_visual.right < layout.done.left);
}

TEST_CASE(PopupCalendarHitTestUsesRealMonthGrid) {
    const auto layout = ClipSoul::BuildPopupFilterLayout();
    const auto cells = ClipSoul::BuildPopupCalendarCells(layout, 2024, 5);

    REQUIRE_EQ(cells.size(), static_cast<size_t>(31));
    REQUIRE_EQ(cells.front().date.day, 1);
    REQUIRE(cells.front().rect.left > layout.calendar.left + layout.calendar.Width() * 2.0f / 7.0f);

    const auto twelfth = cells[11];
    const float x = (twelfth.rect.left + twelfth.rect.right) / 2.0f;
    const float y = (twelfth.rect.top + twelfth.rect.bottom) / 2.0f;
    const auto hit = ClipSoul::HitTestPopupCalendarDate(layout, 2024, 5, x, y);

    REQUIRE(hit.has_value());
    REQUIRE_EQ(hit->year, 2024);
    REQUIRE_EQ(hit->month, 5);
    REQUIRE_EQ(hit->day, 12);
}

TEST_CASE(PopupCalendarDateCellsLeaveRoomForSelectionDots) {
    const auto layout = ClipSoul::BuildPopupFilterLayout();
    const auto cells = ClipSoul::BuildPopupCalendarCells(layout, 2026, 5);

    REQUIRE_EQ(cells.size(), static_cast<size_t>(31));
    REQUIRE(cells.front().rect.Height() >= 18.0f);
    for (size_t index = 7; index < cells.size(); ++index) {
        REQUIRE(cells[index].rect.top - cells[index - 7].rect.top >= 18.0f);
    }
    REQUIRE(cells.back().rect.bottom <= layout.calendar.bottom - 12.0f);
}

TEST_CASE(PopupCalendarDateCellsAreWideEnoughForCenteredNumbers) {
    const auto layout = ClipSoul::BuildPopupFilterLayout();
    const auto cells = ClipSoul::BuildPopupCalendarCells(layout, 2026, 5);

    REQUIRE(!cells.empty());
    REQUIRE(cells.front().rect.Width() >= 36.0f);
    for (const auto& cell : cells) {
        const float center_x = (cell.rect.left + cell.rect.right) / 2.0f;
        REQUIRE(center_x > cell.rect.left + 14.0f);
        REQUIRE(center_x < cell.rect.right - 14.0f);
    }
}

TEST_CASE(PopupCalendarWeekdayLabelsCenterOverDateColumns) {
    const auto layout = ClipSoul::BuildPopupFilterLayout();
    const auto labels = ClipSoul::BuildPopupCalendarWeekdayLabels(layout);

    REQUIRE_EQ(labels.size(), static_cast<size_t>(7));
    REQUIRE_EQ(labels.front().text, L'\u65e5');
    REQUIRE_EQ(labels.back().text, L'\u516d');

    const auto cells = ClipSoul::BuildPopupCalendarCells(layout, 2026, 5);
    REQUIRE(!cells.empty());
    const float cell_width = layout.calendar.Width() / 7.0f;
    for (size_t index = 0; index < labels.size(); ++index) {
        const float label_center = (labels[index].rect.left + labels[index].rect.right) * 0.5f;
        const float column_center = layout.calendar.left + (static_cast<float>(index) + 0.5f) * cell_width;
        REQUIRE(std::abs(label_center - column_center) <= 0.01f);
    }
}

TEST_CASE(PopupCalendarMonthArrowsAreHitTestable) {
    const auto layout = ClipSoul::BuildPopupFilterLayout();

    REQUIRE_EQ(ClipSoul::HitTestPopupCalendarArrow(layout,
                                                   (layout.calendar_prev.left + layout.calendar_prev.right) / 2.0f,
                                                   (layout.calendar_prev.top + layout.calendar_prev.bottom) / 2.0f),
               ClipSoul::PopupCalendarArrow::PreviousMonth);
    REQUIRE_EQ(ClipSoul::HitTestPopupCalendarArrow(layout,
                                                   (layout.calendar_next.left + layout.calendar_next.right) / 2.0f,
                                                   (layout.calendar_next.top + layout.calendar_next.bottom) / 2.0f),
               ClipSoul::PopupCalendarArrow::NextMonth);
}

TEST_CASE(PopupCalendarArrowGlyphsAreCenteredInHoverTargets) {
    const auto layout = ClipSoul::BuildPopupFilterLayout();
    const auto prev = ClipSoul::PopupFilterArrowGlyphRect(layout.calendar_prev);
    const auto next = ClipSoul::PopupFilterArrowGlyphRect(layout.calendar_next);
    const auto center_x = [](const ClipSoul::UiRect& rect) { return (rect.left + rect.right) * 0.5f; };
    const auto center_y = [](const ClipSoul::UiRect& rect) { return (rect.top + rect.bottom) * 0.5f; };

    REQUIRE(std::abs(center_x(prev) - center_x(layout.calendar_prev)) <= 0.01f);
    REQUIRE(std::abs(center_y(prev) - center_y(layout.calendar_prev)) <= 0.01f);
    REQUIRE(std::abs(center_x(next) - center_x(layout.calendar_next)) <= 0.01f);
    REQUIRE(std::abs(center_y(next) - center_y(layout.calendar_next)) <= 0.01f);
    REQUIRE(prev.Width() <= 12.0f);
    REQUIRE(next.Height() <= 14.0f);
}

TEST_CASE(PopupDateRangeFieldsAreHitTestable) {
    const auto layout = ClipSoul::BuildPopupFilterLayout();

    REQUIRE_EQ(ClipSoul::HitTestPopupDateRangeField(layout,
                                                    (layout.start_date.left + layout.start_date.right) / 2.0f,
                                                    (layout.start_date.top + layout.start_date.bottom) / 2.0f),
               ClipSoul::PopupDateRangeField::Start);
    REQUIRE_EQ(ClipSoul::HitTestPopupDateRangeField(layout,
                                                    (layout.end_date.left + layout.end_date.right) / 2.0f,
                                                    (layout.end_date.top + layout.end_date.bottom) / 2.0f),
               ClipSoul::PopupDateRangeField::End);
    REQUIRE(!ClipSoul::HitTestPopupDateRangeField(layout, layout.start_date.right + 4.0f,
                                                  layout.start_date.top + 8.0f)
                 .has_value());
}

TEST_CASE(PopupDateRangeSelectionNormalizesReverseDates) {
    ClipSoul::PopupDateRangeState state;

    ClipSoul::SelectPopupDateRangeDate(state, ClipSoul::PopupCalendarDate{2024, 5, 19});
    REQUIRE(state.start.has_value());
    REQUIRE_EQ(state.start->day, 19);
    REQUIRE_EQ(state.active_field, ClipSoul::PopupDateRangeField::End);

    ClipSoul::SelectPopupDateRangeDate(state, ClipSoul::PopupCalendarDate{2024, 5, 12});
    REQUIRE(state.start.has_value());
    REQUIRE(state.end.has_value());
    REQUIRE_EQ(state.start->day, 12);
    REQUIRE_EQ(state.end->day, 19);
    REQUIRE_EQ(state.active_field, ClipSoul::PopupDateRangeField::Start);
}

TEST_CASE(PopupThemePaletteResolvesLightDarkAndSystemModes) {
    const auto system_light = ClipSoul::ResolvePopupThemePalette(0, false);
    const auto system_dark = ClipSoul::ResolvePopupThemePalette(0, true);
    const auto forced_light = ClipSoul::ResolvePopupThemePalette(1, true);
    const auto forced_dark = ClipSoul::ResolvePopupThemePalette(2, false);

    REQUIRE(!system_light.dark);
    REQUIRE(system_dark.dark);
    REQUIRE(!forced_light.dark);
    REQUIRE(forced_dark.dark);
    REQUIRE(system_light.text != system_dark.text);
    REQUIRE(system_light.window_tint != system_dark.window_tint);
}

TEST_CASE(PopupFilterHoverIgnoresUnderlyingListItems) {
    REQUIRE_EQ(ClipSoul::PopupHoverItemIndex(false, 3), 3);
    REQUIRE_EQ(ClipSoul::PopupHoverItemIndex(true, 3), -1);
}

TEST_CASE(PopupPositionFallsBackToBottomRightWithoutCaret) {
    RECT work{0, 0, 1920, 1080};
    SIZE size{340, 560};
    const auto fallback = ClipSoul::PopupBottomRightFallback(size, work, 96);
    REQUIRE_EQ(fallback.x, 1548);
    REQUIRE_EQ(fallback.y, 464);
}

TEST_CASE(PopupPositionClampsAroundCaretAnchor) {
    RECT work{0, 0, 1920, 1080};
    SIZE size{340, 560};
    const auto position = ClipSoul::ClampPopupToWorkArea(POINT{1000, 400}, size, work, 96);
    REQUIRE_EQ(position.x, 1010);
    REQUIRE_EQ(position.y, 410);
}

TEST_CASE(SettingsWindowCentersInWorkArea) {
    RECT work{0, 0, 1920, 1080};
    const auto position = ClipSoul::CenterWindowInWorkArea(SIZE{340, 340}, work);
    REQUIRE_EQ(position.x, 790);
    REQUIRE_EQ(position.y, 370);
}

TEST_CASE(PopupFilterHitTestCoversInteractiveMenuTargets) {
    const auto layout = ClipSoul::BuildPopupFilterLayout();

    REQUIRE_EQ(ClipSoul::HitTestPopupFilterTarget(layout, 2026, 5,
                                                  (layout.close.left + layout.close.right) / 2.0f,
                                                  (layout.close.top + layout.close.bottom) / 2.0f),
               ClipSoul::PopupFilterTarget::Close);
    REQUIRE_EQ(ClipSoul::HitTestPopupFilterTarget(layout, 2026, 5,
                                                  (layout.text_chip.left + layout.text_chip.right) / 2.0f,
                                                  (layout.text_chip.top + layout.text_chip.bottom) / 2.0f),
               ClipSoul::PopupFilterTarget::TextChip);
    REQUIRE_EQ(ClipSoul::HitTestPopupFilterTarget(layout, 2026, 5,
                                                  (layout.start_date.left + layout.start_date.right) / 2.0f,
                                                  (layout.start_date.top + layout.start_date.bottom) / 2.0f),
               ClipSoul::PopupFilterTarget::StartDate);
    REQUIRE_EQ(ClipSoul::HitTestPopupFilterTarget(layout, 2026, 5,
                                                  (layout.done.left + layout.done.right) / 2.0f,
                                                  (layout.done.top + layout.done.bottom) / 2.0f),
               ClipSoul::PopupFilterTarget::Done);

    const auto cells = ClipSoul::BuildPopupCalendarCells(layout, 2026, 5);
    REQUIRE(!cells.empty());
    const auto& cell = cells.front();
    REQUIRE_EQ(ClipSoul::HitTestPopupFilterTarget(layout, 2026, 5,
                                                  (cell.rect.left + cell.rect.right) / 2.0f,
                                                  (cell.rect.top + cell.rect.bottom) / 2.0f),
               ClipSoul::PopupFilterTarget::CalendarDate);
}
