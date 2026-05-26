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
    REQUIRE_EQ(ClipSoul::PopupFavoriteFolderIconSlotForGroup(false), ClipSoul::PopupFavoriteFolderIconSlot::Outline);
    REQUIRE_EQ(ClipSoul::PopupFavoriteFolderIconSlotForGroup(true), ClipSoul::PopupFavoriteFolderIconSlot::Filled);
}

TEST_CASE(PopupEmptyMessageShowsSelectedFavoriteGroupName) {
    REQUIRE_EQ(ClipSoul::PopupEmptyMessage(false, L""), std::wstring(L"\u6682\u65e0\u5386\u53f2\u8bb0\u5f55"));
    REQUIRE_EQ(ClipSoul::PopupEmptyMessage(true, L""), std::wstring(L"\u6536\u85cf\u5939\u6682\u65e0\u5185\u5bb9"));
    REQUIRE_EQ(ClipSoul::PopupEmptyMessage(true, L"\u5de5\u4f5c"),
               std::wstring(L"\u5de5\u4f5c\u6682\u65e0\u5185\u5bb9"));
}

TEST_CASE(PopupNotePreviewShowsNoteSnippetInsteadOfGenericLabel) {
    REQUIRE_EQ(ClipSoul::PopupNotePreviewText(L""), std::wstring());
    REQUIRE_EQ(ClipSoul::PopupNotePreviewText(L"\u62a5\u4ef7\u6a21\u677f"), std::wstring(L"\u62a5\u4ef7\u6a21\u677f"));
    REQUIRE_EQ(ClipSoul::PopupNotePreviewText(L"\u7b2c\u4e00\u884c\n\u7b2c\u4e8c\u884c"),
               std::wstring(L"\u7b2c\u4e00\u884c \u7b2c\u4e8c\u884c"));
    REQUIRE_EQ(ClipSoul::PopupNotePreviewText(L"\u8fd9\u662f\u4e00\u6bb5\u5f88\u957f\u7684\u5907\u6ce8\u5185\u5bb9\u7528\u6765\u9a8c\u8bc1\u7f29\u7565\u663e\u793a"),
               std::wstring(L"\u8fd9\u662f\u4e00\u6bb5\u5f88\u957f\u7684\u5907\u6ce8\u5185\u5bb9\u7528\u6765..."));
}

TEST_CASE(PopupPinnedWindowStaysVisibleAfterPaste) {
    REQUIRE(ClipSoul::ShouldHidePopupAfterPaste(false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterPaste(true));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterContinuousPaste(false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterContinuousPaste(true));
}

TEST_CASE(PopupItemLongPressThresholdIsShorterThanSystemDoubleClick) {
    REQUIRE_EQ(ClipSoul::PopupItemLongPressMilliseconds(), 100);
}

TEST_CASE(PopupItemPressReleaseDistinguishesShortClickAndLongPress) {
    REQUIRE_EQ(ClipSoul::PopupItemPressReleaseActionFor(true, false),
               ClipSoul::PopupItemPressReleaseAction::Paste);
    REQUIRE_EQ(ClipSoul::PopupItemPressReleaseActionFor(true, true),
               ClipSoul::PopupItemPressReleaseAction::SelectOnly);
    REQUIRE_EQ(ClipSoul::PopupItemPressReleaseActionFor(false, false),
               ClipSoul::PopupItemPressReleaseAction::None);
}

TEST_CASE(PopupLongPressDragCanSelectItemBeforeMouseUp) {
    REQUIRE_EQ(ClipSoul::PopupItemPressMoveActionFor(false, true, 4),
               ClipSoul::PopupItemPressMoveAction::CancelPress);
    REQUIRE_EQ(ClipSoul::PopupItemPressMoveActionFor(false, false, 4),
               ClipSoul::PopupItemPressMoveAction::KeepPress);
    REQUIRE_EQ(ClipSoul::PopupItemPressMoveActionFor(true, true, 4),
               ClipSoul::PopupItemPressMoveAction::SelectHitItem);
    REQUIRE_EQ(ClipSoul::PopupItemPressMoveActionFor(true, true, -1),
               ClipSoul::PopupItemPressMoveAction::KeepPress);

    const auto selected = ClipSoul::PopupSelectionIndexWhileLongPressing(true, 4);
    REQUIRE(selected.has_value());
    REQUIRE_EQ(*selected, 4);
    REQUIRE(!ClipSoul::PopupSelectionIndexWhileLongPressing(false, 4).has_value());
    REQUIRE(!ClipSoul::PopupSelectionIndexWhileLongPressing(true, -1).has_value());
}

TEST_CASE(PopupOutsideClickHidesOnlyWhenUnpinned) {
    REQUIRE(ClipSoul::ShouldHidePopupAfterOutsideClick(false, false, false, false, false, true, true, false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterOutsideClick(true, false, false, false, false, true, true, false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterOutsideClick(false, true, false, false, false, true, true, false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterOutsideClick(false, false, true, false, false, true, true, false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterOutsideClick(false, false, false, true, false, true, true, false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterOutsideClick(false, false, false, false, true, true, true, false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterOutsideClick(false, false, false, false, false, true, false, false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterOutsideClick(false, false, false, false, false, true, true, true));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterOutsideClick(false, false, false, false, false, false, true, false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterOutsideClick(false, false, false, true, false, true, true, true));
}

TEST_CASE(PopupInactiveHideWaitsForTransientInteractions) {
    REQUIRE(ClipSoul::ShouldHidePopupAfterInactive(false, false, false, false, true, false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterInactive(true, false, false, false, true, false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterInactive(false, true, false, false, true, false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterInactive(false, false, true, false, true, false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterInactive(false, false, false, true, true, false));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterInactive(false, false, false, false, true, true));
    REQUIRE(!ClipSoul::ShouldHidePopupAfterInactive(false, false, false, false, false, false));
}

TEST_CASE(PopupPointerInteractionsSuppressInactiveHideUntilTheySettle) {
    REQUIRE(ClipSoul::PopupPointerInteractionSuppressesInactiveHide(true, false, false, false));
    REQUIRE(ClipSoul::PopupPointerInteractionSuppressesInactiveHide(false, true, false, false));
    REQUIRE(ClipSoul::PopupPointerInteractionSuppressesInactiveHide(false, false, true, false));
    REQUIRE(ClipSoul::PopupPointerInteractionSuppressesInactiveHide(false, false, false, true));
    REQUIRE(!ClipSoul::PopupPointerInteractionSuppressesInactiveHide(false, false, false, false));
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

TEST_CASE(PopupCardKindIconRectIsVisuallyCenteredInsideBadge) {
    const auto card = ClipSoul::BuildPopupCardLayout(false, 168.0f);
    const auto icon = ClipSoul::PopupCardKindIconRect(card);
    const auto center_x = [](const ClipSoul::UiRect& rect) { return (rect.left + rect.right) * 0.5f; };
    const auto center_y = [](const ClipSoul::UiRect& rect) { return (rect.top + rect.bottom) * 0.5f; };

    REQUIRE(icon.left > card.stripe.left);
    REQUIRE(icon.top > card.stripe.top);
    REQUIRE(icon.right <= card.stripe.right);
    REQUIRE(icon.bottom <= card.stripe.bottom);
    REQUIRE_EQ(icon.Width(), 16.0f);
    REQUIRE_EQ(icon.Height(), 16.0f);
    REQUIRE(std::abs(center_x(icon) - center_x(card.stripe)) <= 0.01f);
    REQUIRE(std::abs(center_y(icon) - center_y(card.stripe)) <= 0.01f);
}

TEST_CASE(PopupFavoriteGroupIconRectIsCenteredInButton) {
    const auto tabs = ClipSoul::BuildPopupTabsLayout(true);
    const auto icon = ClipSoul::PopupFavoriteGroupIconRect(tabs);
    const auto center_x = [](const ClipSoul::UiRect& rect) { return (rect.left + rect.right) * 0.5f; };
    const auto center_y = [](const ClipSoul::UiRect& rect) { return (rect.top + rect.bottom) * 0.5f; };

    REQUIRE(icon.left > tabs.favorite_group.left);
    REQUIRE(icon.top > tabs.favorite_group.top);
    REQUIRE(icon.right < tabs.favorite_group.right);
    REQUIRE(icon.bottom < tabs.favorite_group.bottom);
    REQUIRE_EQ(icon.Width(), 16.0f);
    REQUIRE_EQ(icon.Height(), 16.0f);
    REQUIRE(std::abs(center_x(icon) - center_x(tabs.favorite_group)) <= 0.01f);
    REQUIRE(std::abs(center_y(icon) - center_y(tabs.favorite_group)) <= 0.01f);
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

TEST_CASE(PopupVisibleCapacityTracksCurrentWindowHeight) {
    const auto metrics = ClipSoul::PopupMetrics();
    const int compact_height =
        static_cast<int>(ClipSoul::PopupListTop()) + metrics.card_height * 3 + metrics.card_gap * 2;
    const int tall_height =
        static_cast<int>(ClipSoul::PopupListTop()) + metrics.card_height * 8 + metrics.card_gap * 7;

    REQUIRE_EQ(ClipSoul::PopupVisibleCardCapacityForHeight(metrics.height), ClipSoul::PopupVisibleCardCapacity());
    REQUIRE_EQ(ClipSoul::PopupVisibleCardCapacityForHeight(compact_height), 3);
    REQUIRE_EQ(ClipSoul::PopupVisibleCardCapacityForHeight(tall_height), 8);
}

TEST_CASE(PopupListScrollOffsetClampsToAvailableRows) {
    REQUIRE_EQ(ClipSoul::ClampPopupScrollOffset(5, 4), 0);
    REQUIRE_EQ(ClipSoul::ClampPopupScrollOffset(9, 0), 0);
    REQUIRE_EQ(ClipSoul::ClampPopupScrollOffset(9, 99), 4);
    REQUIRE_EQ(ClipSoul::ClampPopupScrollOffset(9, -2), 0);
}

TEST_CASE(PopupScrollOffsetClampsToCurrentWindowHeight) {
    const auto metrics = ClipSoul::PopupMetrics();
    const int tall_height =
        static_cast<int>(ClipSoul::PopupListTop()) + metrics.card_height * 8 + metrics.card_gap * 7;

    REQUIRE_EQ(ClipSoul::ClampPopupScrollOffsetForHeight(9, 99, tall_height), 1);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetToRevealSelectionForHeight(9, 0, 7, tall_height), 0);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetToRevealSelectionForHeight(9, 0, 8, tall_height), 1);
}

TEST_CASE(PopupMouseWheelMovesVisibleHistoryWindow) {
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetAfterWheel(9, 0, -120), 1);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetAfterWheel(9, 1, 120), 0);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetAfterWheel(9, 4, -120), 4);
}

TEST_CASE(PopupMouseWheelMovesBySmoothPixelsWithinRows) {
    const auto metrics = ClipSoul::PopupMetrics();
    const float row_pitch = static_cast<float>(metrics.card_height + metrics.card_gap);

    const float first = ClipSoul::PopupScrollOffsetAfterWheelForHeight(12, 0.0f, -120, metrics.height);
    REQUIRE(first > 8.0f);
    REQUIRE(first < row_pitch);

    const float second = ClipSoul::PopupScrollOffsetAfterWheelForHeight(12, first, -120, metrics.height);
    REQUIRE(second > first);
    REQUIRE(second < row_pitch * 2.0f);

    const float up = ClipSoul::PopupScrollOffsetAfterWheelForHeight(12, second, 120, metrics.height);
    REQUIRE(up < second);
    REQUIRE(up > 0.0f);
}

TEST_CASE(PopupSelectionStaysBoundToItemWhenListScrolls) {
    REQUIRE_EQ(ClipSoul::ClampPopupSelectedIndex(9, 7), 7);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetAfterWheel(9, 0, -120), 1);
    REQUIRE_EQ(ClipSoul::ClampPopupSelectedIndex(9, 7), 7);
}

TEST_CASE(PopupContinuousPasteMovesHighlightToNextItemAndRevealsIt) {
    REQUIRE_EQ(ClipSoul::PopupNextSelectedIndex(4, 1), 2);
    REQUIRE_EQ(ClipSoul::PopupNextSelectedIndex(4, 3), 0);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetToRevealSelection(9, 0, 7), 3);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetToRevealSelection(9, 3, 2), 2);
}

TEST_CASE(PopupSmoothRevealKeepsSelectionVisibleWithoutRowJump) {
    const auto metrics = ClipSoul::PopupMetrics();
    const float row_pitch = static_cast<float>(metrics.card_height + metrics.card_gap);

    const float offset = ClipSoul::PopupScrollOffsetToRevealSelectionForHeight(12, 0.0f, 5, metrics.height);
    REQUIRE(offset > 8.0f);
    REQUIRE(offset < row_pitch);
}

TEST_CASE(PopupSmoothViewportClampDoesNotSnapBackToSelection) {
    const auto metrics = ClipSoul::PopupMetrics();
    const float scrolled = ClipSoul::PopupScrollOffsetAfterWheelForHeight(12, 0.0f, -120, metrics.height);

    REQUIRE_EQ(ClipSoul::PopupScrollOffsetAfterViewportClampForHeight(12, scrolled, metrics.height), scrolled);
    REQUIRE(ClipSoul::PopupScrollOffsetToRevealSelectionForHeight(12, scrolled, 0, metrics.height) < scrolled);
}

TEST_CASE(PopupScrollbarThumbMapsDragPositionToScrollOffset) {
    const auto track = ClipSoul::PopupScrollbarTrackRect();
    const auto hit = ClipSoul::PopupScrollbarHitRect();
    const auto top_thumb = ClipSoul::PopupScrollbarThumbRect(10, 0);
    const auto bottom_thumb = ClipSoul::PopupScrollbarThumbRect(10, 5);

    REQUIRE(track.left >= ClipSoul::PopupMetrics().width - 14.0f);
    REQUIRE(hit.left < track.left);
    REQUIRE(hit.right >= track.right);
    REQUIRE(hit.Width() >= 14.0f);
    REQUIRE(top_thumb.top >= track.top);
    REQUIRE(bottom_thumb.bottom <= track.bottom);
    REQUIRE(bottom_thumb.top > top_thumb.top);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetForThumbCenterY(10, (track.top + track.bottom) * 0.5f), 3);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetForThumbCenterY(10, track.bottom + 200.0f), 5);
}

TEST_CASE(PopupScrollbarThumbMapsToCurrentWindowHeight) {
    const auto metrics = ClipSoul::PopupMetrics();
    const int tall_height =
        static_cast<int>(ClipSoul::PopupListTop()) + metrics.card_height * 8 + metrics.card_gap * 7 + 10;
    const auto default_track = ClipSoul::PopupScrollbarTrackRect();
    const auto tall_track = ClipSoul::PopupScrollbarTrackRectForHeight(tall_height);
    const auto tall_thumb = ClipSoul::PopupScrollbarThumbRectForHeight(10, 0, tall_height);
    const auto tall_bottom_thumb = ClipSoul::PopupScrollbarThumbRectForHeight(10, 2, tall_height);

    REQUIRE(tall_track.bottom > default_track.bottom);
    REQUIRE(tall_thumb.Height() > ClipSoul::PopupScrollbarThumbRect(10, 0).Height());
    REQUIRE(tall_bottom_thumb.bottom <= tall_track.bottom);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetForThumbCenterYForHeight(10, tall_track.bottom + 200.0f, tall_height), 2);
}

TEST_CASE(PopupScrollbarThumbOpacityReflectsHoverAndDragFeedback) {
    REQUIRE_EQ(ClipSoul::PopupScrollbarThumbOpacity(false, false, 1.0f), 0.56f);
    REQUIRE(ClipSoul::PopupScrollbarThumbOpacity(true, false, 0.5f) > 0.56f);
    REQUIRE(ClipSoul::PopupScrollbarThumbOpacity(true, false, 0.5f) < 0.86f);
    REQUIRE_EQ(ClipSoul::PopupScrollbarThumbOpacity(false, true, 0.0f), 0.90f);
}

TEST_CASE(PopupListClipRectStartsAtListAndEndsBeforeWindowBottom) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto clip = ClipSoul::PopupListClipRectForHeight(metrics.width + 120, metrics.height + 80);

    REQUIRE_EQ(clip.left, 0.0f);
    REQUIRE_EQ(clip.top, ClipSoul::PopupListTop());
    REQUIRE_EQ(clip.right, static_cast<float>(metrics.width + 120));
    REQUIRE_EQ(clip.bottom, static_cast<float>(metrics.height + 80 - 4));
    REQUIRE(clip.top > ClipSoul::BuildPopupTabsLayout(false).divider.bottom);
}

TEST_CASE(PopupExpandedCardHeightUsesMeasuredTextHeight) {
    const auto card = ClipSoul::BuildPopupCardLayout(false, ClipSoul::PopupListTop());
    const float measured_height = 188.0f;
    const float extra = ClipSoul::PopupExpandedCardExtraHeightForMeasuredDetail(measured_height);
    const float detail_top = card.meta.bottom + 6.0f;
    const float detail_bottom = card.card.bottom + extra - 12.0f;

    REQUIRE(extra > ClipSoul::PopupExpandedCardExtraHeight(true));
    REQUIRE(detail_bottom - detail_top >= measured_height);
}

TEST_CASE(PopupExpandedImageCardHeightLeavesRoomForPreviewAndMeasuredText) {
    const auto card = ClipSoul::BuildPopupCardLayout(false, ClipSoul::PopupListTop());
    const float measured_height = 72.0f;
    const float extra = ClipSoul::PopupExpandedImageCardExtraHeightForMeasuredDetail(measured_height);
    const float detail_top = card.meta.bottom + 6.0f;
    const float detail_bottom = card.card.bottom + extra - 12.0f;

    REQUIRE(detail_bottom - detail_top >= 116.0f + 8.0f + measured_height);
}

TEST_CASE(PopupExpandedCardGrowsForMultiLineNotes) {
    REQUIRE_EQ(ClipSoul::PopupExpandedCardExtraHeightForText(L"one line"), ClipSoul::PopupExpandedCardExtraHeight(true));
    REQUIRE(ClipSoul::PopupExpandedCardExtraHeightForText(L"line 1\nline 2\nline 3\nline 4") >
            ClipSoul::PopupExpandedCardExtraHeight(true));
    REQUIRE(ClipSoul::PopupExpandedCardExtraHeightForText(L"备注：第一行\n第二行\n第三行\n\n正文") >
            ClipSoul::PopupExpandedCardExtraHeight(true));
}

TEST_CASE(PopupExpandedCardHeightUsesAvailableTextWidth) {
    const std::wstring long_path =
        L"F:\\ClipSoul\\cache\\very-long-folder-name\\another-long-folder-name\\record-with-a-very-long-file-name.png";

    REQUIRE(ClipSoul::PopupExpandedCardExtraHeightForText(long_path, 110.0f) >
            ClipSoul::PopupExpandedCardExtraHeightForText(long_path, 230.0f));
    REQUIRE(ClipSoul::PopupExpandedCardExtraHeightForText(L"备注：第一行\n第二行\n第三行\n第四行", 230.0f) >
            ClipSoul::PopupExpandedCardExtraHeight(true));
}

TEST_CASE(PopupExpandedCardHeightAccountsForWideChineseText) {
    const std::wstring narrow_chinese = L"备注：这是一段很长的中文备注内容用来验证宽字符换行高度不会被低估";

    REQUIRE(ClipSoul::PopupExpandedCardExtraHeightForText(narrow_chinese, 80.0f) >
            ClipSoul::PopupExpandedCardExtraHeightForText(narrow_chinese, 230.0f));
    REQUIRE(ClipSoul::PopupExpandedCardExtraHeightForText(narrow_chinese, 80.0f) >
            ClipSoul::PopupExpandedCardExtraHeight(true));
}

TEST_CASE(PopupExpandedCardHeightLeavesRoomForWrappedDetailText) {
    const std::wstring eight_lines = L"line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8";
    const auto card = ClipSoul::BuildPopupCardLayout(false, ClipSoul::PopupListTop());
    const float extra = ClipSoul::PopupExpandedCardExtraHeightForText(eight_lines, 180.0f);
    const float detail_top = card.meta.bottom + 6.0f;
    const float detail_bottom = card.card.bottom + extra - 12.0f;

    REQUIRE(detail_bottom - detail_top >= 8.0f * 20.0f + 6.0f);
}

TEST_CASE(PopupCardExpandButtonUsesRightSideChevronSlot) {
    const auto card = ClipSoul::BuildPopupCardLayout(false, 168.0f);

    REQUIRE(card.expand.Width() >= 22.0f);
    REQUIRE(card.expand.Height() >= 22.0f);
    REQUIRE(card.expand.right <= card.card.right - 8.0f);
    REQUIRE(card.expand.left > card.menu.left);
    REQUIRE(!ClipSoul::RectsOverlap(card.expand, card.time));
}

TEST_CASE(PopupHitTestingUsesExpandedCardHeight) {
    const auto first = ClipSoul::BuildPopupCardLayout(false, ClipSoul::PopupListTop());
    const std::vector<int64_t> ids{10, 20, 30};
    const float second_y = first.card.bottom + ClipSoul::PopupExpandedCardExtraHeight(true) +
                           ClipSoul::PopupMetrics().card_gap + 8.0f;

    REQUIRE_EQ(ClipSoul::HitTestPopupCardIndex(3, 0, static_cast<int64_t>(10), ids, 120.0f, second_y), -1);
    REQUIRE_EQ(ClipSoul::HitTestPopupCardExpandIndex(3, 0, static_cast<int64_t>(10), ids,
                                                     first.expand.left + 4.0f, first.expand.top + 4.0f),
               0);
}

TEST_CASE(PopupWindowTintIsSlightlyMoreTransparent) {
    const auto palette = ClipSoul::ResolvePopupThemePalette(1, false);

    REQUIRE(palette.window_opacity < 0.18f);
    REQUIRE(palette.window_opacity >= 0.12f);
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
    RequireInsidePanel(favorites.favorite_group);
    REQUIRE(!ClipSoul::RectsOverlap(favorites.favorite_group, favorites.favorites));
    REQUIRE(!ClipSoul::RectsOverlap(favorites.favorite_group, favorites.add_favorite_phrase));
    REQUIRE(favorites.add_favorite_phrase.Width() >= 24.0f);
    REQUIRE(favorites.add_favorite_phrase.right <= static_cast<float>(metrics.width - metrics.margin));
    REQUIRE(!ClipSoul::RectsOverlap(favorites.add_favorite_phrase, favorites.favorites));
}

TEST_CASE(PopupFavoriteGroupMenuUsesInWindowPopoverLayout) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto layout = ClipSoul::BuildPopupFavoriteGroupMenuLayout(3);

    RequireInsidePanel(layout.panel);
    REQUIRE(layout.panel.Width() >= 170.0f);
    REQUIRE(layout.panel.right == static_cast<float>(metrics.width - metrics.margin));
    REQUIRE(layout.panel.top > ClipSoul::PopupTabsTop());
    REQUIRE(layout.panel.bottom < ClipSoul::PopupListTop() + 220.0f);
    REQUIRE(layout.all_favorites.top > layout.panel.top);
    REQUIRE(layout.first_divider.top > layout.all_favorites.bottom);
    REQUIRE(layout.group_rows.top > layout.first_divider.bottom);
    REQUIRE(layout.second_divider.top > layout.group_rows.bottom);
    REQUIRE(layout.new_group.top > layout.second_divider.bottom);
    REQUIRE(layout.new_group.bottom < layout.panel.bottom);

    const auto first_group = ClipSoul::PopupFavoriteGroupMenuGroupRect(layout, 0);
    const auto third_group = ClipSoul::PopupFavoriteGroupMenuGroupRect(layout, 2);
    REQUIRE(first_group.top == layout.group_rows.top);
    REQUIRE(third_group.top > first_group.top);
    REQUIRE(third_group.bottom <= layout.group_rows.bottom);
}

TEST_CASE(PopupFavoriteGroupMenuHitTestFindsRowsAndCreateAction) {
    const auto layout = ClipSoul::BuildPopupFavoriteGroupMenuLayout(2);

    const auto all = ClipSoul::HitTestPopupFavoriteGroupMenu(
        layout, 2, (layout.all_favorites.left + layout.all_favorites.right) * 0.5f,
        (layout.all_favorites.top + layout.all_favorites.bottom) * 0.5f);
    REQUIRE_EQ(all.target, ClipSoul::PopupFavoriteGroupMenuTarget::AllFavorites);

    const auto second_group_rect = ClipSoul::PopupFavoriteGroupMenuGroupRect(layout, 1);
    const auto second_group = ClipSoul::HitTestPopupFavoriteGroupMenu(
        layout, 2, (second_group_rect.left + second_group_rect.right) * 0.5f,
        (second_group_rect.top + second_group_rect.bottom) * 0.5f);
    REQUIRE_EQ(second_group.target, ClipSoul::PopupFavoriteGroupMenuTarget::Group);
    REQUIRE_EQ(second_group.group_index, static_cast<size_t>(1));

    const auto delete_rect = ClipSoul::PopupFavoriteGroupMenuDeleteRect(second_group_rect);
    const auto delete_group = ClipSoul::HitTestPopupFavoriteGroupMenu(
        layout, 2, (delete_rect.left + delete_rect.right) * 0.5f, (delete_rect.top + delete_rect.bottom) * 0.5f);
    REQUIRE_EQ(delete_group.target, ClipSoul::PopupFavoriteGroupMenuTarget::DeleteGroup);
    REQUIRE_EQ(delete_group.group_index, static_cast<size_t>(1));

    const auto create = ClipSoul::HitTestPopupFavoriteGroupMenu(
        layout, 2, (layout.new_group.left + layout.new_group.right) * 0.5f,
        (layout.new_group.top + layout.new_group.bottom) * 0.5f);
    REQUIRE_EQ(create.target, ClipSoul::PopupFavoriteGroupMenuTarget::NewGroup);

    const auto outside = ClipSoul::HitTestPopupFavoriteGroupMenu(layout, 2, layout.panel.left - 4.0f,
                                                                 layout.panel.top - 4.0f);
    REQUIRE_EQ(outside.target, ClipSoul::PopupFavoriteGroupMenuTarget::None);
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
