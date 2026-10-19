#include "TestHarness.h"

#include "ClipSoul/PopupLayout.h"

#include <cmath>
#include <iterator>
#include <string>

namespace {
void RequireInsidePanel(const ClipSoul::UiRect& rect) {
    const auto metrics = ClipSoul::PopupMetrics();
    REQUIRE(rect.left >= metrics.margin);
    REQUIRE(rect.right <= metrics.width - metrics.margin);
    REQUIRE(rect.Width() > 0.0f);
}

void RequireNear(float actual, float expected, float tolerance = 0.01f) {
    REQUIRE(std::abs(actual - expected) <= tolerance);
}
} // namespace

TEST_CASE(PopupToolbarButtonsFitCompactPanel) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto layout = ClipSoul::BuildPopupToolbarLayout(false);
    const auto tabs = ClipSoul::BuildPopupTabsLayout(false);

    REQUIRE(!ClipSoul::RectsOverlap(layout.filter, layout.multi_select));
    REQUIRE(!ClipSoul::RectsOverlap(layout.multi_select, layout.clear_all));
    REQUIRE_EQ(layout.filter.Width(), 28.0f);
    REQUIRE_EQ(layout.multi_select.Width(), 28.0f);
    REQUIRE_EQ(layout.clear_all.Width(), 28.0f);
    RequireNear(layout.multi_select.left - layout.filter.right, 2.0f);
    RequireNear(layout.clear_all.left - layout.multi_select.right, 2.0f);
    REQUIRE_EQ(layout.clear_all.right, static_cast<float>(metrics.width - 10));
    REQUIRE(layout.filter.left > tabs.favorites.right);
    RequireNear((layout.filter.top + layout.filter.bottom) * 0.5f, (tabs.history.top + tabs.history.bottom) * 0.5f, 1.0f);
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

TEST_CASE(PopupWindowEdgeUsesDwmCornersAndEdgeAlignedStroke) {
    const auto metrics = ClipSoul::PopupMetrics();

    REQUIRE_EQ(metrics.corner_radius, 14);
    REQUIRE_EQ(metrics.glass_tint_opacity, 1.0f);
    REQUIRE_EQ(ClipSoul::ScalePopupMetricForDpi(metrics.corner_radius, 96), 14);
    REQUIRE_EQ(ClipSoul::ScalePopupMetricForDpi(metrics.corner_radius, 144), 21);
    REQUIRE_EQ(ClipSoul::ScalePopupMetricForDpi(metrics.corner_radius, 192), 28);
    REQUIRE(!ClipSoul::PopupWindowShouldUseHardRoundedRegion());
    REQUIRE(!ClipSoul::PopupWindowShouldDrawClientEdge());
    REQUIRE(ClipSoul::PopupWindowShouldUseDwmAntialiasedFrame());
    REQUIRE_EQ(ClipSoul::PopupWindowRegionCornerDiameterForDpi(96), 0);
    REQUIRE_EQ(ClipSoul::PopupWindowRegionCornerDiameterForDpi(144), 0);
    REQUIRE_EQ(ClipSoul::PopupWindowRegionCornerDiameterForDpi(192), 0);

    const auto stroke = ClipSoul::PopupWindowEdgeStrokeRectForSize(metrics.width, metrics.height);
    RequireNear(stroke.left, 0.5f);
    RequireNear(stroke.top, 0.5f);
    RequireNear(stroke.right, static_cast<float>(metrics.width) - 0.5f);
    RequireNear(stroke.bottom, static_cast<float>(metrics.height) - 0.5f);
}

TEST_CASE(PopupDesignTokensLockLayoutTypographyIconAndMotionContracts) {
    const auto tokens = ClipSoul::PopupDesignTokens();

    REQUIRE_EQ(tokens.window_padding, 18);
    REQUIRE_EQ(tokens.section_gap, 16);
    REQUIRE_EQ(tokens.control_gap, 12);
    REQUIRE_EQ(tokens.card_gap, 0);
    REQUIRE_EQ(tokens.search_to_toolbar_gap, 0);
    REQUIRE_EQ(tokens.toolbar_to_tab_gap, 0);
    REQUIRE_EQ(tokens.tab_to_list_gap, 2);
    REQUIRE_EQ(tokens.popup_corner_radius, 14);
    REQUIRE_EQ(tokens.control_radius, 7);
    REQUIRE_EQ(tokens.card_radius, 9);
    REQUIRE_EQ(tokens.title_font_size, 18);
    REQUIRE_EQ(tokens.section_title_font_size, 14);
    REQUIRE_EQ(tokens.body_font_size, 13);
    REQUIRE_EQ(tokens.secondary_font_size, 12);
    REQUIRE_EQ(tokens.caption_font_size, 11);
    REQUIRE_EQ(tokens.toolbar_icon_size, 16);
    REQUIRE_EQ(tokens.list_type_icon_size, 20);
    REQUIRE_EQ(tokens.window_control_icon_size, 16);
    REQUIRE_EQ(tokens.menu_icon_size, 16);
    REQUIRE_EQ(tokens.popup_motion_ms, 100);
    REQUIRE_EQ(tokens.tab_motion_ms, 120);
    REQUIRE_EQ(tokens.hover_motion_ms, 80);
    REQUIRE_EQ(tokens.pressed_motion_ms, 60);
    REQUIRE_EQ(tokens.search_height, ClipSoul::PopupMetrics().search_height);
    REQUIRE_EQ(tokens.card_height, ClipSoul::PopupMetrics().card_height);
    REQUIRE_EQ(ClipSoul::ScalePopupMetricForDpi(tokens.search_height, 144), 60);
    REQUIRE_EQ(ClipSoul::ScalePopupMetricForDpi(tokens.card_height, 144), 90);
}

TEST_CASE(PopupThemePaletteUsesQuietPaperEfficiencyColors) {
    const auto light = ClipSoul::ResolvePopupThemePalette(1, false);
    const auto dark = ClipSoul::ResolvePopupThemePalette(2, false);

    REQUIRE(!light.dark);
    REQUIRE(dark.dark);
    REQUIRE_EQ(light.window_opacity, 1.0f);
    REQUIRE_EQ(light.panel_opacity, 1.0f);
    REQUIRE_EQ(light.card_opacity, 1.0f);
    REQUIRE_EQ(dark.window_opacity, 1.0f);
    REQUIRE_EQ(dark.panel_opacity, 1.0f);
    REQUIRE_EQ(dark.card_opacity, 1.0f);
    REQUIRE_EQ(light.accent, 0x1C1B19);
    REQUIRE_EQ(dark.accent, 0xECE6DA);
    REQUIRE_EQ(light.window_tint, 0xF7F4EE);
    REQUIRE_EQ(light.border, 0xE3E0DB);
}

TEST_CASE(PopupThemePaletteUsesDesignSystemUtilityTokens) {
    const auto light = ClipSoul::ResolvePopupThemePalette(1, false);
    const auto dark = ClipSoul::ResolvePopupThemePalette(2, false);

    REQUIRE_EQ(light.window_tint, 0xF7F4EE);
    REQUIRE_EQ(light.panel_fill, 0xFBF9F5);
    REQUIRE_EQ(light.card_fill, 0xEFEAE0);
    REQUIRE_EQ(light.search_fill, 0xF7F4EE);
    REQUIRE_EQ(light.text, 0x1C1B19);
    REQUIRE_EQ(light.muted, 0x5E5A52);
    REQUIRE_EQ(light.border, 0xE3E0DB);
    REQUIRE_EQ(light.strong_border, 0xD4D1CC);
    REQUIRE_EQ(light.accent, 0x1C1B19);
    REQUIRE_EQ(light.accent_hover, 0x34322E);
    REQUIRE_EQ(light.text_tertiary, 0xA39D92);
    REQUIRE_EQ(light.danger, 0xC8402F);
    REQUIRE_EQ(dark.window_tint, 0x171614);
    REQUIRE_EQ(dark.panel_fill, 0x1C1B18);
    REQUIRE_EQ(dark.card_fill, 0x22201C);
    REQUIRE_EQ(dark.search_fill, 0x171614);
    REQUIRE_EQ(dark.text, 0xECE6DA);
    REQUIRE_EQ(dark.muted, 0xA69F92);
    REQUIRE_EQ(dark.text_tertiary, 0x6A645A);
    REQUIRE_EQ(dark.border, 0x262522);
    REQUIRE_EQ(dark.strong_border, 0x353330);
    REQUIRE_EQ(dark.accent, 0xECE6DA);
    REQUIRE_EQ(dark.accent_hover, 0xF7F2E8);
    REQUIRE_EQ(dark.danger, 0xE0583F);
    REQUIRE_EQ(dark.paper_hover, 0x22201C);
    REQUIRE_EQ(dark.paper_selected, 0x262420);
    REQUIRE_EQ(dark.border_hover, 0x353330);
    REQUIRE_EQ(dark.border_selected, 0x353330);
    REQUIRE_EQ(dark.active_tab, 0xECE6DA);
    REQUIRE_EQ(dark.scrollbar_track, 0x171614);
    REQUIRE_EQ(dark.scrollbar_thumb, 0x6A645A);
    REQUIRE_EQ(dark.scrollbar_pressed, 0xA69F92);
    REQUIRE_EQ(dark.focus_outline, 0xECE6DA);
}

TEST_CASE(PopupDarkToolbarUsesLowSaturationReadableIconAndLabelColors) {
    const auto light = ClipSoul::ResolvePopupThemePalette(1, false);
    const auto dark = ClipSoul::ResolvePopupThemePalette(2, false);

    REQUIRE_EQ(ClipSoul::PopupDarkUiIconTintColor(dark), dark.muted);
    REQUIRE(ClipSoul::PopupDarkUiIconTintColor(dark) != 0xFFFFFF);
    REQUIRE_EQ(ClipSoul::PopupDarkUiIconTintColor(light), light.text);
    REQUIRE_EQ(ClipSoul::PopupToolbarLabelColor(dark, false, false), dark.muted);
    REQUIRE_EQ(ClipSoul::PopupToolbarLabelColor(dark, true, false), dark.accent_hover);
    REQUIRE_EQ(ClipSoul::PopupToolbarLabelColor(dark, false, true), dark.danger);
    REQUIRE(ClipSoul::PopupToolbarLabelColor(dark, false, false) != 0xFFFFFF);
    REQUIRE_EQ(ClipSoul::PopupToolbarLabelColor(light, false, false), light.muted);
    REQUIRE(!ClipSoul::PopupUiIconShouldFallbackToOriginalBitmap(dark, false));
    REQUIRE(ClipSoul::PopupUiIconShouldFallbackToOriginalBitmap(dark, true));
    REQUIRE(ClipSoul::PopupUiIconShouldFallbackToOriginalBitmap(light, false));
    REQUIRE(ClipSoul::PopupUiIconShouldLoadBitmap(dark, false, true));
    REQUIRE(!ClipSoul::PopupUiIconShouldLoadBitmap(dark, true, true));
    REQUIRE(!ClipSoul::PopupUiIconShouldLoadBitmap(light, false, true));
    REQUIRE(ClipSoul::PopupUiIconShouldLoadBitmap(light, false, false));
}

TEST_CASE(WindowMaterialPolicyDisablesTransparentAcrylicEffects) {
    const auto policy = ClipSoul::ResolveWindowMaterialPolicy();

    REQUIRE(!policy.acrylic_blur);
    REQUIRE(!policy.system_backdrop);
    REQUIRE(policy.rounded_corners);
    REQUIRE(policy.immersive_dark_mode);
}

TEST_CASE(PopupFileImagePreviewRecognizesImageFileExtensions) {
    REQUIRE(ClipSoul::PopupFileCanUseImagePreview(LR"(C:\Users\yang\Pictures\sample.png)"));
    REQUIRE(ClipSoul::PopupFileCanUseImagePreview(LR"(C:\Users\yang\Pictures\SAMPLE.JPG)"));
    REQUIRE(ClipSoul::PopupFileCanUseImagePreview(LR"(F:\xz\photo.jpeg)"));
    REQUIRE(ClipSoul::PopupFileCanUseImagePreview(LR"(F:\xz\scan.bmp)"));
    REQUIRE(ClipSoul::PopupFileCanUseImagePreview(LR"(F:\xz\animation.gif)"));
    REQUIRE(ClipSoul::PopupFileCanUseImagePreview(LR"(F:\xz\preview.webp)"));
    REQUIRE(ClipSoul::PopupFileCanUseImagePreview(LR"(F:\xz\image.tif)"));
    REQUIRE(ClipSoul::PopupFileCanUseImagePreview(LR"(F:\xz\image.tiff)"));
    REQUIRE(!ClipSoul::PopupFileCanUseImagePreview(LR"(F:\xz\document.pdf)"));
    REQUIRE(!ClipSoul::PopupFileCanUseImagePreview(LR"(F:\xz\notes.txt)"));
    REQUIRE(!ClipSoul::PopupFileCanUseImagePreview(LR"(F:\xz\no-extension)"));
}

TEST_CASE(PopupHeaderButtonsUseCompactIconTargets) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto layout = ClipSoul::BuildPopupHeaderLayout();

    REQUIRE(layout.title.left == static_cast<float>(metrics.margin));
    REQUIRE(!ClipSoul::RectsOverlap(layout.pin, layout.close));
    REQUIRE_EQ(metrics.header_button_size, 28);
    REQUIRE(layout.pin.Width() <= static_cast<float>(metrics.header_button_size));
    REQUIRE(layout.pin.Width() >= 22.0f);
    REQUIRE(layout.pin.Height() >= 22.0f);
    REQUIRE(layout.close.Width() <= static_cast<float>(metrics.header_button_size));
    REQUIRE(layout.close.right > layout.pin.right);
    REQUIRE(layout.close.right <= static_cast<float>(metrics.width - 10));
    REQUIRE(layout.close.bottom <= static_cast<float>(metrics.header_height));

    const auto pin_icon = ClipSoul::BuildPopupHeaderPinIconRect(layout);
    REQUIRE_EQ(pin_icon.Width(), 16.0f);
    REQUIRE_EQ(pin_icon.Height(), 16.0f);
    REQUIRE(pin_icon.left >= layout.pin.left);
    REQUIRE(pin_icon.right <= layout.pin.right);

    const auto close_icon = ClipSoul::BuildPopupHeaderCloseIconRect(layout);
    REQUIRE_EQ(close_icon.Width(), 16.0f);
    REQUIRE_EQ(close_icon.Height(), 16.0f);
    REQUIRE(close_icon.left >= layout.close.left);
    REQUIRE(close_icon.right <= layout.close.right);
}

TEST_CASE(PopupDesignSystemLayoutPrioritizesSearchAndCompactRows) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto header = ClipSoul::BuildPopupHeaderLayout();
    const auto search = ClipSoul::BuildPopupSearchLayout();
    const auto tabs = ClipSoul::BuildPopupTabsLayout(false);
    const auto card = ClipSoul::BuildPopupCardLayout(false, ClipSoul::PopupListTop());

    REQUIRE_EQ(metrics.width, 340);
    REQUIRE_EQ(metrics.margin, 18);
    REQUIRE_EQ(metrics.search_height, 40);
    REQUIRE_EQ(metrics.tab_height, 40);
    REQUIRE_EQ(metrics.card_height, 60);
    REQUIRE_EQ(metrics.card_gap, 0);
    REQUIRE_EQ(metrics.toolbar_icon_size, 16);
    REQUIRE(search.box.Height() == 40.0f);
    REQUIRE(header.title.bottom <= search.box.top);
    REQUIRE(search.box.bottom <= tabs.history.top);
    REQUIRE(tabs.active_indicator.Height() == 1.5f);
    REQUIRE(tabs.divider.bottom <= ClipSoul::PopupListTop());
    REQUIRE(card.card.Height() == 60.0f);
    REQUIRE(card.stripe.Width() == 0.0f);
    // 左对齐：搜索图标、标签、行内正文起点共用内容左线
    REQUIRE_EQ(search.icon.left, static_cast<float>(metrics.margin));
    REQUIRE_EQ(tabs.history.left, static_cast<float>(metrics.margin));
    REQUIRE(card.title.left >= card.card.left + 52.0f);
    REQUIRE(card.title.right <= card.time.left - 8.0f);
}

TEST_CASE(PopupTopRhythmKeepsToolbarTabsAndListCompact) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto toolbar = ClipSoul::BuildPopupToolbarLayout(false);
    const auto tabs = ClipSoul::BuildPopupTabsLayout(false);

    REQUIRE_EQ(metrics.height, 560);
    REQUIRE_EQ(metrics.header_height, 44);
    RequireNear(ClipSoul::PopupSearchTop(), 46.0f);
    RequireNear(ClipSoul::PopupTabsTop(), 86.0f);
    RequireNear(ClipSoul::PopupListTop(), 128.0f);
    RequireNear(ClipSoul::PopupSectionHeaderHeight(), 27.0f);
    RequireNear(ClipSoul::PopupTabsTop(), ClipSoul::PopupSearchTop() + static_cast<float>(metrics.search_height));
    REQUIRE_EQ(metrics.tab_height, 40);
    RequireNear(tabs.divider.bottom, ClipSoul::PopupTabsTop() + static_cast<float>(metrics.tab_height) + 0.5f);
    REQUIRE(toolbar.filter.top >= ClipSoul::PopupTabsTop());
    REQUIRE(toolbar.filter.bottom <= ClipSoul::PopupTabsTop() + static_cast<float>(metrics.tab_height));
    REQUIRE(ClipSoul::PopupListTop() < 140.0f);
    REQUIRE_EQ(ClipSoul::PopupFooterHeight(), 30.0f);
    RequireNear(ClipSoul::PopupListBottomForHeight(metrics.height), static_cast<float>(metrics.height) - 30.0f);
}

TEST_CASE(PopupToolbarIconsUseStableCompactSize) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto layout = ClipSoul::BuildPopupToolbarLayout(false);

    REQUIRE_EQ(metrics.toolbar_icon_size, 16);
    REQUIRE(layout.filter.Width() >= metrics.toolbar_icon_size + 10.0f);
    REQUIRE(layout.multi_select.Width() >= metrics.toolbar_icon_size + 10.0f);
    REQUIRE(layout.clear_all.Width() >= metrics.toolbar_icon_size + 10.0f);
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

TEST_CASE(PopupEmptyMessageMatchesPrototypeForHistoryAndFavorites) {
    REQUIRE_EQ(ClipSoul::PopupEmptyMessage(false, L""), std::wstring(L"复制点什么，它会出现在这里"));
    REQUIRE_EQ(ClipSoul::PopupEmptyMessage(true, L""), std::wstring(L"还没有收藏，右键条目即可收藏"));
    REQUIRE_EQ(ClipSoul::PopupEmptyMessage(true, L"工作"), std::wstring(L"还没有收藏，右键条目即可收藏"));
    REQUIRE_EQ(ClipSoul::PopupEmptyMessage(false, L"", true), std::wstring(L"没有匹配的记录"));
    REQUIRE_EQ(ClipSoul::PopupEmptyMessage(true, L"工作", true), std::wstring(L"没有匹配的记录"));
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

TEST_CASE(PopupItemLongPressThresholdAllowsDeliberateSlowClick) {
    REQUIRE_EQ(ClipSoul::PopupItemLongPressMilliseconds(), 200);
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

TEST_CASE(PopupInactiveHideWaitsWhileShellSurfaceRaiseIsActive) {
    REQUIRE(!ClipSoul::ShouldHidePopupAfterInactive(false, false, false, false, true, false, true));
    REQUIRE(ClipSoul::ShouldHidePopupAfterInactive(false, false, false, false, true, false, false));
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

    REQUIRE_EQ(std::wstring(ClipSoul::PopupSearchPlaceholderText()), std::wstring(L"搜索剪贴板\u2026"));
    RequireInsidePanel(search.box);
    REQUIRE(search.text.left > search.icon.right + 8.0f);
    REQUIRE(search.text.right <= static_cast<float>(metrics.width - metrics.margin - 12));
    REQUIRE(search.text.Height() >= 20.0f);
    REQUIRE(search.text.top > search.box.top);
    REQUIRE(search.text.bottom < search.box.bottom);
}

TEST_CASE(PopupHeaderAndSearchControlsAdaptToCurrentWindowWidth) {
    const auto metrics = ClipSoul::PopupMetrics();
    const int resized_width = metrics.width + 120;

    const auto header = ClipSoul::BuildPopupHeaderLayoutForWidth(resized_width);
    const auto search = ClipSoul::BuildPopupSearchLayoutForWidth(resized_width);

    REQUIRE_EQ(header.close.right, static_cast<float>(resized_width - 10));
    REQUIRE_EQ(header.pin.right, static_cast<float>(resized_width - 40));
    REQUIRE_EQ(search.box.right, static_cast<float>(resized_width - metrics.margin));
    REQUIRE_EQ(search.text.right, static_cast<float>(resized_width - metrics.margin - 28));
    REQUIRE(!ClipSoul::RectsOverlap(header.pin, header.close));
}

TEST_CASE(PopupSearchClearButtonLayoutAndHitTest) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto layout = ClipSoul::BuildPopupSearchLayout();

    // clear_button 在 box 内部
    REQUIRE(layout.clear_button.left >= layout.box.left);
    REQUIRE(layout.clear_button.right <= layout.box.right);
    REQUIRE(layout.clear_button.top >= layout.box.top);
    REQUIRE(layout.clear_button.bottom <= layout.box.bottom);

    // clear_button 尺寸合理
    REQUIRE_EQ(layout.clear_button.Width(), 20.0f);
    REQUIRE_EQ(layout.clear_button.Height(), 20.0f);
    RequireNear(layout.clear_button.right, layout.box.right);
    RequireNear(layout.clear_button.top, layout.box.top + 10.0f);
    RequireNear(layout.clear_button.bottom, layout.box.bottom - 10.0f);

    // clear_button 在 text 右侧
    REQUIRE(layout.clear_button.left >= layout.text.right);

    // 不与 icon 重叠
    REQUIRE(layout.clear_button.left > layout.icon.right);

    // HitTest: 有 query 时点击 clear_button 返回 true
    POINT inside{static_cast<LONG>(layout.clear_button.left + 5.0f),
                 static_cast<LONG>(layout.clear_button.top + 5.0f)};
    REQUIRE(ClipSoul::PopupSearchClearButtonHitTest(layout, true, inside));

    // HitTest: 无 query 时点击 clear_button 返回 false
    REQUIRE(!ClipSoul::PopupSearchClearButtonHitTest(layout, false, inside));

    // HitTest: 点击 box 内但 clear_button 外的位置返回 false
    POINT outside_box{static_cast<LONG>(layout.box.left + 5.0f),
                      static_cast<LONG>(layout.box.top + 5.0f)};
    REQUIRE(!ClipSoul::PopupSearchClearButtonHitTest(layout, true, outside_box));

    // CenterDips 返回正确中心点
    POINT center = ClipSoul::PopupSearchClearButtonCenterDips(layout);
    REQUIRE_EQ(center.x, static_cast<LONG>(std::lround((layout.clear_button.left + layout.clear_button.right) * 0.5f)));
    REQUIRE_EQ(center.y, static_cast<LONG>(std::lround((layout.clear_button.top + layout.clear_button.bottom) * 0.5f)));

    // Opacity
    REQUIRE_EQ(ClipSoul::PopupSearchClearButtonOpacity(true), 0.95f);
    REQUIRE_EQ(ClipSoul::PopupSearchClearButtonOpacity(false), 0.7f);
}

TEST_CASE(PopupSearchClearButtonAdaptsToWindowWidth) {
    const auto metrics = ClipSoul::PopupMetrics();

    // 默认宽度
    const auto default_layout = ClipSoul::BuildPopupSearchLayout();
    REQUIRE(default_layout.clear_button.right <= default_layout.box.right);

    // 加宽
    const int wider = metrics.width + 100;
    const auto wider_layout = ClipSoul::BuildPopupSearchLayoutForWidth(wider);
    REQUIRE(wider_layout.clear_button.right <= wider_layout.box.right);
    REQUIRE_EQ(wider_layout.clear_button.Width(), 20.0f);
    REQUIRE_EQ(wider_layout.clear_button.Height(), 20.0f);
    REQUIRE_EQ(wider_layout.clear_button.right, wider_layout.box.right);

    // 最小宽度
    const auto min_layout = ClipSoul::BuildPopupSearchLayoutForWidth(1);
    REQUIRE(min_layout.clear_button.right <= min_layout.box.right);
}

TEST_CASE(PopupSearchDisplayTextShowsQueryOrPlaceholder) {
    REQUIRE_EQ(std::wstring(ClipSoul::PopupSearchDisplayText(L"")),
               std::wstring(L"搜索剪贴板\u2026"));
    REQUIRE_EQ(std::wstring(ClipSoul::PopupSearchDisplayText(L"alpha")), std::wstring(L"alpha"));
    REQUIRE(ClipSoul::PopupSearchCaretVisible(true, true));
    REQUIRE(!ClipSoul::PopupSearchCaretVisible(true, false));
    REQUIRE(!ClipSoul::PopupSearchCaretVisible(false, true));
    REQUIRE(ClipSoul::PopupSearchHasComposition(true, L"abc"));
    REQUIRE(!ClipSoul::PopupSearchHasComposition(true, L""));
    REQUIRE(!ClipSoul::PopupSearchHasComposition(false, L"abc"));
    REQUIRE(!ClipSoul::PopupSearchHasComposition(false, L""));
    REQUIRE_EQ(ClipSoul::PopupSearchCompositionDisplayText(L"", true, L"abc"), std::wstring(L"abc"));
    REQUIRE_EQ(ClipSoul::PopupSearchCompositionDisplayText(L"hi", true, L"bc"), std::wstring(L"hibc"));
    REQUIRE_EQ(std::wstring(ClipSoul::PopupSearchCompositionDisplayText(L"", false, L"")),
               std::wstring(L"搜索剪贴板\u2026"));
    REQUIRE_EQ(std::wstring(ClipSoul::PopupSearchCompositionDisplayText(L"test", false, L"")),
               std::wstring(L"test"));
    REQUIRE(ClipSoul::PopupSearchCompositionTextColor(true, L""));
    REQUIRE(ClipSoul::PopupSearchCompositionTextColor(true, L"abc"));
    REQUIRE(ClipSoul::PopupSearchCompositionTextColor(false, L"abc"));
    REQUIRE(!ClipSoul::PopupSearchCompositionTextColor(false, L""));
    REQUIRE(ClipSoul::PopupSearchCaretVisibleDuringComposition(false));
    REQUIRE(!ClipSoul::PopupSearchCaretVisibleDuringComposition(true));
    REQUIRE(!ClipSoul::PopupResizeShouldDiscardDeviceResources());
    REQUIRE(ClipSoul::PopupDpiChangeShouldDiscardDeviceResources());
    const RECT suggested_dpi_rect{120, 80, 980, 1180};
    const auto dpi_rect = ClipSoul::PopupDpiChangedWindowRect(suggested_dpi_rect, SIZE{510, 840});
    REQUIRE_EQ(dpi_rect.left, 120);
    REQUIRE_EQ(dpi_rect.top, 80);
    REQUIRE_EQ(dpi_rect.right, 630);
    REQUIRE_EQ(dpi_rect.bottom, 920);
    REQUIRE(!ClipSoul::PopupPaintShouldUpdateLayout());
    REQUIRE(!ClipSoul::PopupShouldAnimateHoverWhileResizing(true));
    REQUIRE(ClipSoul::PopupShouldAnimateHoverWhileResizing(false));
    // 键盘呼出也必须激活：弹窗一旦是 NOACTIVATE 就永远收不到 WM_ACTIVATE(WA_INACTIVE)，
    // 「点外面自动收起」只能靠轮询鼠标状态，而提权窗口在前台时 UIPI 让普通进程读不到
    // 鼠标按键。激活后点到任何窗口都会收到失活通知，跨权限等级可靠。
    REQUIRE(ClipSoul::PopupShouldActivateWhenShown(true));
    REQUIRE(ClipSoul::PopupShouldActivateWhenShown(false));
    REQUIRE(ClipSoul::PopupShouldActivateForShellSurface(true));
    REQUIRE(!ClipSoul::PopupShouldActivateForShellSurface(false));
    REQUIRE(ClipSoul::PopupShowShouldActivate(true, false));
    REQUIRE(ClipSoul::PopupShowShouldActivate(false, false));
    REQUIRE(ClipSoul::PopupShowShouldActivate(true, true));
    REQUIRE(ClipSoul::PopupShowShouldActivate(false, true));
    REQUIRE(!ClipSoul::PopupSetWindowPosShouldUseNoActivate(true));
    REQUIRE(ClipSoul::PopupSetWindowPosShouldUseNoActivate(false));
    REQUIRE_EQ(ClipSoul::PopupMouseActivateResult(), static_cast<LRESULT>(MA_NOACTIVATE));
    REQUIRE(!ClipSoul::PopupShouldFocusWindowForPointerPress(false));
    REQUIRE(ClipSoul::PopupShouldFocusWindowForPointerPress(true));
    REQUIRE(ClipSoul::PopupShouldActivateForSearchFocus(true, true));
    REQUIRE(!ClipSoul::PopupShouldActivateForSearchFocus(false, true));
    REQUIRE(!ClipSoul::PopupShouldActivateForSearchFocus(true, false));
    // 预览启动即 q.focus()：有原生搜索框就自动聚焦（墨线随之展开）。
    REQUIRE(ClipSoul::PopupShouldAutoFocusSearchOnShow(true));
    REQUIRE(!ClipSoul::PopupShouldAutoFocusSearchOnShow(false));
    REQUIRE(ClipSoul::PopupWindowShouldUseNoActivateStyle());
    REQUIRE(!ClipSoul::PopupWindowShouldUseNoActivateStyle(true));
    REQUIRE(ClipSoul::PopupWindowShouldUseNoActivateStyle(false));
    REQUIRE(ClipSoul::PopupNativeSearchCueBannerShowsWhenFocused());
    REQUIRE(ClipSoul::PopupShouldRedrawNativeSearchAfterParentPaint(true, false));
    REQUIRE(!ClipSoul::PopupShouldRedrawNativeSearchAfterParentPaint(false, false));
    REQUIRE(!ClipSoul::PopupShouldRedrawNativeSearchAfterParentPaint(true, true));
    REQUIRE(!ClipSoul::PopupShouldDrawDecorativeShadows(true));
    REQUIRE(ClipSoul::PopupShouldDrawDecorativeShadows(false));
    REQUIRE(!ClipSoul::PopupShouldLoadUiIcon(true));
    REQUIRE(ClipSoul::PopupShouldLoadUiIcon(false));
    REQUIRE(!ClipSoul::PopupIsFastMediaInteraction(false, false, false));
    REQUIRE(ClipSoul::PopupIsFastMediaInteraction(true, false, false));
    REQUIRE(ClipSoul::PopupIsFastMediaInteraction(false, true, false));
    REQUIRE(ClipSoul::PopupIsFastMediaInteraction(false, false, true));
    REQUIRE(!ClipSoul::PopupShouldLoadImagePreview(true));
    REQUIRE(ClipSoul::PopupShouldLoadImagePreview(false));
    REQUIRE(!ClipSoul::PopupShouldLoadFileIcon(true));
    REQUIRE(ClipSoul::PopupShouldLoadFileIcon(false));
    REQUIRE(ClipSoul::PopupShouldDrawCachedMediaDuringFastInteraction(true, true));
    REQUIRE(!ClipSoul::PopupShouldDrawCachedMediaDuringFastInteraction(true, false));
    REQUIRE(ClipSoul::PopupShouldDrawCachedMediaDuringFastInteraction(false, false));
    REQUIRE(!ClipSoul::PopupShouldUseCurrentWindowWidthForLayout(false, false, 460, 340));
    REQUIRE(ClipSoul::PopupShouldUseCurrentWindowWidthForLayout(false, true, 460, 340));
    REQUIRE(ClipSoul::PopupShouldUseCurrentWindowWidthForLayout(true, false, 460, 340));
    REQUIRE(!ClipSoul::PopupShouldUseCurrentWindowWidthForLayout(true, false, 340, 340));
    // 预览启动即 q.focus()：有原生搜索框就自动聚焦（墨线随之展开）。
    REQUIRE(ClipSoul::PopupShouldAutoFocusSearchOnShow(true));
    REQUIRE(!ClipSoul::PopupShouldAutoFocusSearchOnShow(false));
    REQUIRE(ClipSoul::PopupWindowShouldUseNoActivateStyle());
    REQUIRE(ClipSoul::PopupNativeSearchCueBannerShowsWhenFocused());
    REQUIRE(ClipSoul::PopupImageFilePreviewDecodePixelLimit() <= 256);
    REQUIRE(ClipSoul::PopupImagePreviewDecodePixelLimit() <= 256);
    const auto wide_preview = ClipSoul::PopupPreviewDecodeSize(4000, 2000, ClipSoul::PopupImageFilePreviewDecodePixelLimit());
    REQUIRE_EQ(wide_preview.cx, static_cast<LONG>(ClipSoul::PopupImageFilePreviewDecodePixelLimit()));
    REQUIRE_EQ(wide_preview.cy, static_cast<LONG>(ClipSoul::PopupImageFilePreviewDecodePixelLimit() / 2));
    const auto tall_preview = ClipSoul::PopupPreviewDecodeSize(1200, 2400, 160);
    REQUIRE_EQ(tall_preview.cx, 80);
    REQUIRE_EQ(tall_preview.cy, 160);
    const auto small_preview = ClipSoul::PopupPreviewDecodeSize(64, 48, 160);
    REQUIRE_EQ(small_preview.cx, 64);
    REQUIRE_EQ(small_preview.cy, 48);
    REQUIRE_EQ(ClipSoul::PopupSearchFocusProgress(true, false, 0.0f), 1.0f);
    REQUIRE_EQ(ClipSoul::PopupSearchFocusProgress(false, false, 1.0f), 0.0f);
    REQUIRE_EQ(ClipSoul::PopupSearchFocusProgress(false, true, 0.45f), 0.45f);
    REQUIRE_EQ(ClipSoul::PopupSearchFocusProgress(false, true, 2.0f), 1.0f);

    const auto layout = ClipSoul::BuildPopupSearchLayout();
    const auto native_edit = ClipSoul::PopupNativeSearchEditRect(layout);
    REQUIRE(native_edit.left > layout.icon.right);
    REQUIRE(native_edit.right <= layout.box.right - 8.0f);
    REQUIRE(native_edit.Width() <= 2.0f);
    REQUIRE(native_edit.Height() <= layout.text.Height());
    REQUIRE(ClipSoul::PopupSearchCaretVisible(true, true));
    REQUIRE(!ClipSoul::PopupSearchCaretVisible(true, false));
    REQUIRE(!ClipSoul::PopupSearchCaretVisible(false, true));
    const auto forward_selection = ClipSoul::NormalizePopupSearchSelection(1, 4, 8);
    REQUIRE_EQ(forward_selection.start, static_cast<size_t>(1));
    REQUIRE_EQ(forward_selection.end, static_cast<size_t>(4));
    REQUIRE(ClipSoul::PopupSearchHasSelection(forward_selection));
    const auto reverse_selection = ClipSoul::NormalizePopupSearchSelection(7, 2, 5);
    REQUIRE_EQ(reverse_selection.start, static_cast<size_t>(2));
    REQUIRE_EQ(reverse_selection.end, static_cast<size_t>(5));
    REQUIRE(ClipSoul::PopupSearchHasSelection(reverse_selection));
    REQUIRE(!ClipSoul::PopupSearchHasSelection(ClipSoul::NormalizePopupSearchSelection(3, 3, 8)));
    REQUIRE(ClipSoul::PopupSearchShouldDrawSelection(true, true, forward_selection));
    REQUIRE(!ClipSoul::PopupSearchShouldDrawSelection(false, true, forward_selection));
    REQUIRE(!ClipSoul::PopupSearchShouldDrawSelection(true, false, forward_selection));
    REQUIRE(!ClipSoul::PopupSearchShouldDrawSelection(true, true,
                                                      ClipSoul::NormalizePopupSearchSelection(3, 3, 8)));

    REQUIRE(!ClipSoul::PopupShouldResizeNativeSearchDuringLiveResize(true, true));
    REQUIRE(ClipSoul::PopupShouldResizeNativeSearchDuringLiveResize(false, true));
    REQUIRE(!ClipSoul::PopupShouldResizeNativeSearchDuringLiveResize(false, false));
    REQUIRE(ClipSoul::PopupShouldInvalidateDuringLiveResize(true, true));
    REQUIRE(!ClipSoul::PopupShouldInvalidateDuringLiveResize(true, false));
    const RECT current_rect{10, 20, 350, 580};
    const RECT same_rect{10, 20, 350, 580};
    const RECT resized_rect{10, 20, 420, 640};
    REQUIRE(!ClipSoul::PopupShouldApplyWindowRect(current_rect, same_rect));
    REQUIRE(ClipSoul::PopupShouldApplyWindowRect(current_rect, resized_rect));
    REQUIRE(ClipSoul::PopupShouldFlushPaintDuringLiveResize(true, true));
    REQUIRE(!ClipSoul::PopupShouldFlushPaintDuringLiveResize(true, false));
    REQUIRE(!ClipSoul::PopupShouldFlushPaintDuringLiveResize(false, true));
}

TEST_CASE(PopupToolbarButtonsAdaptToCurrentWindowWidth) {
    const auto metrics = ClipSoul::PopupMetrics();
    const int resized_width = metrics.width + 120;
    const auto default_normal = ClipSoul::BuildPopupToolbarLayout(false);
    const auto default_multi = ClipSoul::BuildPopupToolbarLayout(true);

    const auto normal = ClipSoul::BuildPopupToolbarLayoutForWidth(false, resized_width);
    RequireNear(normal.filter.left, default_normal.filter.left + 120.0f);
    RequireNear(normal.clear_all.left, default_normal.clear_all.left + 120.0f);
    REQUIRE_EQ(normal.filter.Width(), default_normal.filter.Width());
    REQUIRE(!ClipSoul::RectsOverlap(normal.multi_select, normal.clear_all));

    const auto multi = ClipSoul::BuildPopupToolbarLayoutForWidth(true, resized_width);
    RequireNear(multi.paste_selected.left, default_multi.paste_selected.left + 120.0f);
    RequireNear(multi.cancel_multi_select.left, default_multi.cancel_multi_select.left + 120.0f);
    RequireNear(multi.select_all.left - multi.cancel_multi_select.right, 2.0f);
    RequireNear(multi.delete_selected.left - multi.select_all.right, 2.0f);
    RequireNear(multi.paste_selected.left - multi.delete_selected.right, 2.0f);
    REQUIRE_EQ(multi.paste_selected.right, static_cast<float>(resized_width - 10));
    REQUIRE(!ClipSoul::RectsOverlap(multi.delete_selected, multi.paste_selected));
}

TEST_CASE(PopupTabsButtonsAdaptToCurrentWindowWidth) {
    const auto metrics = ClipSoul::PopupMetrics();
    const int resized_width = metrics.width + 120;

    const auto default_tabs = ClipSoul::BuildPopupTabsLayout(true);
    const auto resized_tabs = ClipSoul::BuildPopupTabsLayoutForWidth(true, resized_width);

    REQUIRE_EQ(resized_tabs.history.Width(), default_tabs.history.Width());
    REQUIRE_EQ(resized_tabs.history.left, default_tabs.history.left);
    REQUIRE_EQ(resized_tabs.favorites.left, default_tabs.favorites.left);
    REQUIRE_EQ(resized_tabs.favorite_group.left, default_tabs.favorite_group.left);
    REQUIRE(resized_tabs.favorite_group.Width() >= default_tabs.favorite_group.Width());
    REQUIRE_EQ(resized_tabs.divider.right, static_cast<float>(resized_width));
}

TEST_CASE(PopupFavoriteControlsReserveSeparateClickTargetsAtSupportedWidths) {
    const int widths[] = {300, 340, 640};
    const float group_widths[] = {32.0f, 72.0f, 86.0f};
    for (int i = 0; i < 3; ++i) {
        const auto tabs = ClipSoul::BuildPopupTabsLayoutForWidth(true, widths[i]);
        const auto tools = ClipSoul::BuildPopupToolbarLayoutForWidth(false, widths[i]);
        const ClipSoul::UiRect targets[] = {tabs.history, tabs.favorites, tabs.favorite_group,
            tabs.add_favorite_phrase, tools.filter, tools.multi_select, tools.clear_all};
        RequireNear(tabs.history.Width(), 46.0f);
        RequireNear(tabs.favorites.left - tabs.history.right, 14.0f);
        RequireNear(tabs.favorite_group.Width(), group_widths[i]);
        RequireNear(tabs.add_favorite_phrase.Width(), 28.0f);
        RequireNear(tabs.add_favorite_phrase.Height(), 28.0f);
        REQUIRE(tools.filter.left - tabs.add_favorite_phrase.right >= 6.0f);
        RequireNear(tools.clear_all.right - tools.filter.left, 88.0f);
        RequireNear(tabs.active_indicator.Width(), 26.0f);
        for (size_t target = 0; target < std::size(targets); ++target) {
            REQUIRE(targets[target].Width() > 0.0f);
            REQUIRE(targets[target].Height() > 0.0f);
            REQUIRE(targets[target].left >= 18.0f);
            REQUIRE(targets[target].right <= static_cast<float>(widths[i] - 10));
            for (size_t other = target + 1; other < std::size(targets); ++other) {
                REQUIRE(!ClipSoul::RectsOverlap(targets[target], targets[other]));
            }
        }
        const auto icon = ClipSoul::PopupFavoriteGroupIconRect(tabs);
        REQUIRE(icon.left >= tabs.favorite_group.left);
        REQUIRE(icon.right <= tabs.favorite_group.right);
    }
}

TEST_CASE(PopupMultiSelectToolbarButtonsFitCompactPanel) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto layout = ClipSoul::BuildPopupToolbarLayout(true);
    const auto tabs = ClipSoul::BuildPopupTabsLayout(false);

    REQUIRE(!ClipSoul::RectsOverlap(layout.cancel_multi_select, layout.select_all));
    REQUIRE(!ClipSoul::RectsOverlap(layout.select_all, layout.delete_selected));
    REQUIRE(!ClipSoul::RectsOverlap(layout.delete_selected, layout.paste_selected));
    REQUIRE(layout.cancel_multi_select.Width() >= 36.0f);
    REQUIRE(layout.select_all.Width() >= 36.0f);
    REQUIRE(layout.delete_selected.Width() >= 36.0f);
    REQUIRE(layout.paste_selected.Width() >= 56.0f);
    RequireNear(layout.select_all.left - layout.cancel_multi_select.right, 2.0f);
    RequireNear(layout.delete_selected.left - layout.select_all.right, 2.0f);
    RequireNear(layout.paste_selected.left - layout.delete_selected.right, 2.0f);
    REQUIRE(layout.cancel_multi_select.left > tabs.favorites.right);
    REQUIRE_EQ(layout.paste_selected.right, static_cast<float>(metrics.width - 10));
}

TEST_CASE(PopupCardGeometryMatchesCssForNormalAndMultiSelectRows) {
    constexpr float top = 128.0f;
    for (const bool multi : {false, true}) {
        const auto card = ClipSoul::BuildPopupCardLayout(multi, top);
        const float media_left = card.card.left + (multi ? 28.0f : 0.0f);
        RequireNear(card.card.Height(), 60.0f);
        RequireNear(card.title.left - media_left, 52.0f);
        RequireNear(card.title.top - top, 10.0f);
        RequireNear(card.title.Height(), 19.0f);
        RequireNear(card.meta.top - top, 31.0f);
        RequireNear(card.meta.Height(), 16.0f);
        RequireNear(card.meta.top - card.title.bottom, 2.0f);
        RequireNear(card.card.bottom - card.meta.bottom, 13.0f);
        RequireNear(card.card.bottom - card.expand.bottom, 10.0f);
        RequireNear(card.expand.top - card.time.bottom, 3.0f);
        RequireNear(card.expand.bottom - card.time.top, 40.0f);
        RequireNear(card.time.Width(), 28.875f);
        RequireNear(card.time.left, 291.125f);
        RequireNear(card.title.right, 283.125f);
        RequireNear(card.title.Width(), multi ? 195.125f : 223.125f);
        RequireNear(card.meta.right, card.title.right);
        RequireNear(card.image_preview.left - media_left, 9.0f);
        RequireNear(card.image_preview.top - top, 11.0f);
        RequireNear(card.image_preview.Width(), 34.0f);
        RequireNear(card.image_preview.Height(), 34.0f);
        RequireNear(card.kind_badge.left - media_left, 11.0f);
        RequireNear(card.kind_badge.top - top, 11.0f);
        RequireNear(card.kind_badge.Width(), 30.0f);
        RequireNear(card.kind_badge.Height(), 30.0f);
        RequireNear(card.file_icon.top - top, 11.0f);
        RequireNear(card.file_icon.Width(), 27.0f);
        RequireNear(card.file_icon.Height(), 33.0f);
        RequireNear(card.expand.top - top, 32.0f);
        RequireNear(card.expand.Width(), 20.0f);
        RequireNear(card.expand.Height(), 18.0f);
        REQUIRE(!ClipSoul::RectsOverlap(card.meta, card.expand));
        REQUIRE(card.image_preview.right <= card.title.left);
        if (multi) {
            RequireNear(card.checkbox.right, media_left);
            RequireNear(card.checkbox.top - top, 18.0f);
            RequireNear(card.checkbox.Width(), 15.0f);
        }
    }
}

TEST_CASE(PopupSectionedRowsMatchCdpMeasuredPositionsWithoutAccumulatingDrift) {
    // geometry-light.json: first two pinned rows y=155/215, first recent row y=302.
    const auto plan = ClipSoul::PopupSectionPlanFor(11, 2);
    const float measured_tops[] = {155.0f, 215.0f, 302.0f};
    for (size_t i = 0; i < std::size(measured_tops); ++i) {
        const float top = ClipSoul::PopupListTop() + ClipSoul::PopupSectionedItemTop(
            i, plan, static_cast<float>(ClipSoul::PopupMetrics().card_height),
            ClipSoul::PopupSectionHeaderHeight());
        const auto card = ClipSoul::BuildPopupCardLayout(false, top);
        RequireNear(card.card.top, measured_tops[i]);
        RequireNear(card.card.bottom, measured_tops[i] + 60.0f);
        RequireNear(card.title.left, 60.0f);
        RequireNear(card.title.top, measured_tops[i] + 10.0f);
        RequireNear(card.title.right, 283.125f);
        RequireNear(card.time.left, 291.125f);
        RequireNear(card.expand.left, 300.0f);
        RequireNear(card.expand.top, measured_tops[i] + 32.0f);
    }
}

TEST_CASE(PopupCardTextReservesTimeAndMenuArea) {
    const auto normal = ClipSoul::BuildPopupCardLayout(false, 168.0f);
    const auto multi = ClipSoul::BuildPopupCardLayout(true, 168.0f);

    REQUIRE(normal.title.right <= normal.time.left - 8.0f);
    REQUIRE(normal.time.right <= normal.menu.left);
    REQUIRE(normal.title.left >= normal.stripe.right + 10.0f);
    RequireNear(normal.title.Width(), 223.125f);
    RequireNear(multi.title.Width(), 195.125f);
    RequireNear(normal.time.Width(), 28.875f);
    RequireNear(normal.title.right, normal.time.left - 8.0f);
    REQUIRE(multi.title.left > normal.title.left);
    REQUIRE(multi.title.right == normal.title.right);
}

TEST_CASE(PopupCardMediaRectsSupportImagePreviewAndFileIcon) {
    const auto normal = ClipSoul::BuildPopupCardLayout(false, 168.0f);
    const auto multi = ClipSoul::BuildPopupCardLayout(true, 168.0f);

    REQUIRE_EQ(normal.image_preview.Width(), 34.0f);
    REQUIRE_EQ(normal.image_preview.Height(), 34.0f);
    REQUIRE(normal.image_preview.left > normal.card.left);
    REQUIRE(normal.title.left >= normal.image_preview.right + 6.0f);
    REQUIRE_EQ(normal.file_icon.Width(), 27.0f);
    REQUIRE_EQ(normal.file_icon.Height(), 33.0f);
    REQUIRE(normal.file_icon.left > normal.image_preview.left);
    REQUIRE(normal.file_icon.right < normal.image_preview.right);
    REQUIRE(multi.image_preview.left > normal.image_preview.left);
    REQUIRE(multi.title.left > normal.title.left);
    REQUIRE(multi.checkbox.right <= multi.image_preview.left);
}

TEST_CASE(PopupCardKindIconRectIsVisuallyCenteredInsideMediaSlot) {
    const auto card = ClipSoul::BuildPopupCardLayout(false, 168.0f);
    const auto icon = ClipSoul::PopupCardKindIconRect(card);
    const auto& badge = card.kind_badge;
    const auto center_x = [](const ClipSoul::UiRect& rect) { return (rect.left + rect.right) * 0.5f; };
    const auto center_y = [](const ClipSoul::UiRect& rect) { return (rect.top + rect.bottom) * 0.5f; };

    REQUIRE_EQ(badge.Width(), 30.0f);
    REQUIRE_EQ(badge.Height(), 30.0f);
    REQUIRE(icon.left > badge.left);
    REQUIRE(icon.top > badge.top);
    REQUIRE(icon.right < badge.right);
    REQUIRE(icon.bottom < badge.bottom);
    REQUIRE_EQ(icon.Width(), 14.0f);
    REQUIRE_EQ(icon.Height(), 14.0f);
    RequireNear(center_x(icon), center_x(badge));
    RequireNear(center_y(icon), center_y(badge));
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

TEST_CASE(PopupFixedHeightShowsSixCompleteBrowserMeasuredRows) {
    const auto metrics = ClipSoul::PopupMetrics();
    const float available = ClipSoul::PopupListBottomForHeight(metrics.height) - ClipSoul::PopupListTop();
    REQUIRE_EQ(available, 402.0f);
    REQUIRE_EQ(metrics.card_height * 6, 360);
    REQUIRE_EQ(ClipSoul::PopupVisibleCardCapacity(), 6);
    REQUIRE(available < static_cast<float>(metrics.card_height * 7));
}

TEST_CASE(PopupToolbarAndTabsFitLegacyCompactWindowWidth) {
    constexpr int legacy_width = 340;
    const auto toolbar = ClipSoul::BuildPopupToolbarLayoutForWidth(false, legacy_width);
    const auto multi_toolbar = ClipSoul::BuildPopupToolbarLayoutForWidth(true, legacy_width);
    const auto tabs = ClipSoul::BuildPopupTabsLayoutForWidth(true, legacy_width);

    REQUIRE(!ClipSoul::RectsOverlap(toolbar.filter, toolbar.multi_select));
    REQUIRE(!ClipSoul::RectsOverlap(toolbar.multi_select, toolbar.clear_all));
    REQUIRE(toolbar.clear_all.right <= 330.0f);
    REQUIRE(!ClipSoul::RectsOverlap(multi_toolbar.cancel_multi_select, multi_toolbar.select_all));
    REQUIRE(!ClipSoul::RectsOverlap(multi_toolbar.select_all, multi_toolbar.delete_selected));
    REQUIRE(!ClipSoul::RectsOverlap(multi_toolbar.delete_selected, multi_toolbar.paste_selected));
    REQUIRE(multi_toolbar.paste_selected.right <= 330.0f);
    REQUIRE(tabs.history.left >= 18.0f);
    REQUIRE(!ClipSoul::RectsOverlap(tabs.history, tabs.favorites));
    REQUIRE(tabs.add_favorite_phrase.right <= toolbar.filter.left);
    REQUIRE(!ClipSoul::RectsOverlap(tabs.favorite_group, tabs.add_favorite_phrase));
}

TEST_CASE(PopupVisibleCapacityTracksCurrentWindowHeight) {
    const auto metrics = ClipSoul::PopupMetrics();
    const int footer = static_cast<int>(ClipSoul::PopupFooterHeight());
    const int compact_height = static_cast<int>(ClipSoul::PopupListTop()) + metrics.card_height * 3 + footer;
    const int tall_height = static_cast<int>(ClipSoul::PopupListTop()) + metrics.card_height * 8 + footer;

    REQUIRE_EQ(ClipSoul::PopupVisibleCardCapacityForHeight(metrics.height), ClipSoul::PopupVisibleCardCapacity());
    REQUIRE_EQ(ClipSoul::PopupVisibleCardCapacityForHeight(compact_height), 3);
    REQUIRE_EQ(ClipSoul::PopupVisibleCardCapacityForHeight(tall_height), 8);
}

TEST_CASE(PopupCapacityAndPixelScrollClampAtExactCssRowBoundary) {
    constexpr int height = 128 + 60 * 3 + 30;
    REQUIRE_EQ(ClipSoul::PopupVisibleCardCapacityForHeight(height - 1), 2);
    REQUIRE_EQ(ClipSoul::PopupVisibleCardCapacityForHeight(height), 3);
    REQUIRE_EQ(ClipSoul::PopupVisibleCardCapacityForHeight(height + 1), 3);
    REQUIRE_EQ(ClipSoul::PopupVisibleCardCapacityForHeight(100), 0);
    RequireNear(ClipSoul::ClampPopupScrollOffsetForHeight(4, 999.0f, height), 60.0f);
    RequireNear(ClipSoul::PopupScrollOffsetToRevealSelectionForHeight(4, 0.0f, 3, height), 60.0f);
    RequireNear(ClipSoul::ClampPopupScrollOffsetForHeight(3, 999.0f, height), 0.0f);
}

TEST_CASE(PopupListScrollOffsetClampsToAvailableRows) {
    const int capacity = ClipSoul::PopupVisibleCardCapacity();
    REQUIRE_EQ(ClipSoul::ClampPopupScrollOffset(capacity + 1, 4), 1);
    REQUIRE_EQ(ClipSoul::ClampPopupScrollOffset(capacity + 5, 0), 0);
    REQUIRE_EQ(ClipSoul::ClampPopupScrollOffset(capacity + 5, 99), 5);
    REQUIRE_EQ(ClipSoul::ClampPopupScrollOffset(capacity + 5, -2), 0);
}

TEST_CASE(PopupScrollOffsetClampsToCurrentWindowHeight) {
    const auto metrics = ClipSoul::PopupMetrics();
    const int tall_height = static_cast<int>(ClipSoul::PopupListTop()) + metrics.card_height * 8 +
                            static_cast<int>(ClipSoul::PopupFooterHeight());

    REQUIRE_EQ(ClipSoul::ClampPopupScrollOffsetForHeight(9, 99, tall_height), 1);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetToRevealSelectionForHeight(9, 0, 7, tall_height), 0);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetToRevealSelectionForHeight(9, 0, 8, tall_height), 1);
}

TEST_CASE(PopupMouseWheelMovesVisibleHistoryWindow) {
    const int count = ClipSoul::PopupVisibleCardCapacity() + 5;
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetAfterWheel(count, 0, -120), 1);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetAfterWheel(count, 1, 120), 0);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetAfterWheel(count, 4, -120), 5);
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

TEST_CASE(PopupThemePaletteInterpolatesBetweenThemes) {
    using ClipSoul::LerpPopupThemePalette;
    using ClipSoul::ResolvePopupThemePalette;

    const auto light = ResolvePopupThemePalette(1, false);
    const auto dark = ResolvePopupThemePalette(2, false);

    // 端点必须精确返回原配色，否则切换结束时会有一帧跳变。
    const auto at_zero = LerpPopupThemePalette(light, dark, 0.0f);
    REQUIRE_EQ(at_zero.window_tint, light.window_tint);
    REQUIRE_EQ(at_zero.text, light.text);
    const auto at_one = LerpPopupThemePalette(light, dark, 1.0f);
    REQUIRE_EQ(at_one.window_tint, dark.window_tint);
    REQUIRE_EQ(at_one.text, dark.text);

    // 中间帧应当是两端的混合，而且不会越界（缓动带回弹时会被 clamp 住）。
    const auto middle = LerpPopupThemePalette(light, dark, 0.5f);
    REQUIRE(middle.window_tint != light.window_tint);
    REQUIRE(middle.window_tint != dark.window_tint);
    const auto clamped_low = LerpPopupThemePalette(light, dark, -1.0f);
    REQUIRE_EQ(clamped_low.window_tint, light.window_tint);
    const auto clamped_high = LerpPopupThemePalette(light, dark, 2.0f);
    REQUIRE_EQ(clamped_high.window_tint, dark.window_tint);
}

TEST_CASE(PopupFirstVisibleRowIndexCoversViewportTop) {
    using ClipSoul::PopupFirstVisibleRowIndex;
    const std::vector<float> tops{0.0f, 60.0f, 120.0f, 180.0f};

    // 关键回归：滚动偏移正好等于某行 top 时，必须返回那一行。
    // 之前用 upper_bound 直接返回下一行，列表顶部会空出一排。
    REQUIRE_EQ(PopupFirstVisibleRowIndex(tops, 0.0f), 0);
    REQUIRE_EQ(PopupFirstVisibleRowIndex(tops, 60.0f), 1);
    REQUIRE_EQ(PopupFirstVisibleRowIndex(tops, 120.0f), 2);

    // 行中间的偏移仍属于该行。
    REQUIRE_EQ(PopupFirstVisibleRowIndex(tops, 59.0f), 0);
    REQUIRE_EQ(PopupFirstVisibleRowIndex(tops, 121.0f), 2);

    // 越界时夹到合法范围。
    REQUIRE_EQ(PopupFirstVisibleRowIndex(tops, 9999.0f), 3);
    REQUIRE_EQ(PopupFirstVisibleRowIndex({}, 10.0f), 0);
}

TEST_CASE(PopupDefaultSelectionSkipsPinnedSection) {
    using ClipSoul::PopupDefaultSelectedIndex;
    using ClipSoul::PopupSectionPlanFor;

    // 2 条置顶 + 39 条最近：默认高亮最近分组的第一条，而不是置顶的第 1 条。
    const auto with_pinned = PopupSectionPlanFor(41, 2);
    REQUIRE_EQ(PopupDefaultSelectedIndex(with_pinned), 2);

    // 没有置顶：仍然是第一条。
    REQUIRE_EQ(PopupDefaultSelectedIndex(PopupSectionPlanFor(5, 0)), 0);

    // 全是置顶（没有最近条目）：退回列表首项。
    REQUIRE_EQ(PopupDefaultSelectedIndex(PopupSectionPlanFor(3, 3)), 0);

    // 空列表。
    REQUIRE_EQ(PopupDefaultSelectedIndex(PopupSectionPlanFor(0, 0)), 0);
}

TEST_CASE(PopupDefaultSelectionSkipsOnePinnedItem) {
    using ClipSoul::PopupDefaultSelectedIndex;
    using ClipSoul::PopupSectionPlanFor;
    // 复现截图里的场景：1 条置顶 + N 条最近。默认高亮必须是最近分组的第一条（下标 1），
    // 而不是置顶的那条，否则回车会粘贴一条被置顶的旧记录。
    for (size_t recent = 1; recent <= 5; ++recent) {
        const auto plan = PopupSectionPlanFor(recent + 1, 1);
        REQUIRE_EQ(PopupDefaultSelectedIndex(plan), 1);
        REQUIRE_EQ(plan.show_recent_header, true);
    }
}

TEST_CASE(PopupSelectionStaysBoundToItemWhenListScrolls) {
    REQUIRE_EQ(ClipSoul::ClampPopupSelectedIndex(9, 7), 7);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetAfterWheel(9, 0, -120), 1);
    REQUIRE_EQ(ClipSoul::ClampPopupSelectedIndex(9, 7), 7);
}

TEST_CASE(PopupContinuousPasteMovesHighlightToNextItemAndRevealsIt) {
    const int capacity = ClipSoul::PopupVisibleCardCapacity();
    REQUIRE_EQ(ClipSoul::PopupNextSelectedIndex(4, 1), 2);
    REQUIRE_EQ(ClipSoul::PopupNextSelectedIndex(4, 3), 0);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetToRevealSelection(capacity + 5, 0, capacity + 3), 4);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetToRevealSelection(capacity + 5, 3, 2), 2);
}

TEST_CASE(PopupContinuousPasteUsesCurrentHighlightThenAdvances) {
    const auto middle = ClipSoul::PopupContinuousPasteSelectionStep(4, 1);
    REQUIRE_EQ(middle.paste_index, 1);
    REQUIRE_EQ(middle.next_selected_index, 2);

    const auto wrapped = ClipSoul::PopupContinuousPasteSelectionStep(4, 3);
    REQUIRE_EQ(wrapped.paste_index, 3);
    REQUIRE_EQ(wrapped.next_selected_index, 0);

    const auto clamped = ClipSoul::PopupContinuousPasteSelectionStep(4, 99);
    REQUIRE_EQ(clamped.paste_index, 3);
    REQUIRE_EQ(clamped.next_selected_index, 0);
}

TEST_CASE(PopupSmoothRevealKeepsSelectionVisibleWithoutRowJump) {
    const auto metrics = ClipSoul::PopupMetrics();
    const float row_pitch = static_cast<float>(metrics.card_height + metrics.card_gap);
    const int capacity = ClipSoul::PopupVisibleCardCapacity();

    const float offset = ClipSoul::PopupScrollOffsetToRevealSelectionForHeight(capacity + 8, 0.0f, capacity + 1, metrics.height);
    REQUIRE(offset > 8.0f);
    REQUIRE(offset < row_pitch * 2.0f);
}

TEST_CASE(PopupSmoothViewportClampDoesNotSnapBackToSelection) {
    const auto metrics = ClipSoul::PopupMetrics();
    const float scrolled = ClipSoul::PopupScrollOffsetAfterWheelForHeight(12, 0.0f, -120, metrics.height);

    REQUIRE_EQ(ClipSoul::PopupScrollOffsetAfterViewportClampForHeight(12, scrolled, metrics.height), scrolled);
    REQUIRE(ClipSoul::PopupScrollOffsetToRevealSelectionForHeight(12, scrolled, 0, metrics.height) < scrolled);
}

TEST_CASE(PopupRevealRangeKeepsCurrentHighlightVisibleInsteadOfResettingToTop) {
    const auto metrics = ClipSoul::PopupMetrics();
    const float row_pitch = static_cast<float>(metrics.card_height + metrics.card_gap);
    const int count = ClipSoul::PopupVisibleCardCapacity() + 3;
    const int target = count - 2;
    const float content_height = row_pitch * static_cast<float>(count) - static_cast<float>(metrics.card_gap);
    const float viewport_height = ClipSoul::PopupListBottomForHeight(metrics.height) - ClipSoul::PopupListTop();
    const float target_top = row_pitch * static_cast<float>(target);
    const float target_bottom = target_top + static_cast<float>(metrics.card_height);

    REQUIRE_EQ(ClipSoul::PopupScrollOffsetToRevealRange(0.0f, target_top, target_bottom,
                                                        content_height, viewport_height),
               ClipSoul::PopupScrollOffsetToRevealSelectionForHeight(count, 0.0f, target, metrics.height));
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetToRevealRange(120.0f, 0.0f, static_cast<float>(metrics.card_height),
                                                        content_height, viewport_height), 0.0f);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetToRevealRange(120.0f, 20.0f, 92.0f, 300.0f, 360.0f), 0.0f);
}

TEST_CASE(PopupScrollbarThumbMapsDragPositionToScrollOffset) {
    const int capacity = ClipSoul::PopupVisibleCardCapacity();
    const int count = capacity + 6;
    const auto track = ClipSoul::PopupScrollbarTrackRect();
    const auto hit = ClipSoul::PopupScrollbarHitRect();
    const auto top_thumb = ClipSoul::PopupScrollbarThumbRect(count, 0);
    const auto bottom_thumb = ClipSoul::PopupScrollbarThumbRect(count, 5);

    REQUIRE_EQ(track.Width(), 4.0f);
    REQUIRE(track.left >= ClipSoul::PopupMetrics().width - 8.0f);
    REQUIRE(hit.left < track.left);
    REQUIRE(hit.right >= track.right);
    REQUIRE(hit.Width() >= 12.0f);
    REQUIRE(top_thumb.top >= track.top);
    REQUIRE(bottom_thumb.bottom <= track.bottom);
    REQUIRE(bottom_thumb.top > top_thumb.top);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetForThumbCenterY(count, (track.top + track.bottom) * 0.5f), 3);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetForThumbCenterY(count, track.bottom + 200.0f), 6);
}

TEST_CASE(PopupScrollbarThumbMapsToCurrentWindowHeight) {
    const auto metrics = ClipSoul::PopupMetrics();
    const int tall_height = static_cast<int>(ClipSoul::PopupListTop()) + metrics.card_height * 8 +
                            static_cast<int>(ClipSoul::PopupFooterHeight()) + 10;
    const auto default_track = ClipSoul::PopupScrollbarTrackRect();
    const auto tall_track = ClipSoul::PopupScrollbarTrackRectForHeight(tall_height);
    const auto tall_thumb = ClipSoul::PopupScrollbarThumbRectForHeight(10, 0, tall_height);
    const auto tall_bottom_thumb = ClipSoul::PopupScrollbarThumbRectForHeight(10, 2, tall_height);

    REQUIRE(tall_track.bottom > default_track.bottom);
    REQUIRE(tall_thumb.Height() > ClipSoul::PopupScrollbarThumbRect(10, 0).Height());
    REQUIRE(tall_bottom_thumb.bottom <= tall_track.bottom);
    REQUIRE_EQ(ClipSoul::PopupScrollOffsetForThumbCenterYForHeight(10, tall_track.bottom + 200.0f, tall_height), 2);
}

TEST_CASE(PopupScrollbarTrackAndHitRectAdaptToCurrentWindowSize) {
    const auto metrics = ClipSoul::PopupMetrics();
    const int resized_width = metrics.width + 120;
    const int resized_height = metrics.height + 90;

    const auto default_track = ClipSoul::PopupScrollbarTrackRect();
    const auto resized_track = ClipSoul::PopupScrollbarTrackRectForSize(resized_width, resized_height);
    const auto resized_hit = ClipSoul::PopupScrollbarHitRectForSize(resized_width, resized_height);
    const auto resized_thumb = ClipSoul::PopupScrollbarThumbRectForSize(30, 80.0f, resized_width, resized_height);

    REQUIRE(resized_track.left > default_track.left);
    REQUIRE_EQ(resized_track.Width(), 4.0f);
    REQUIRE_EQ(resized_track.right, static_cast<float>(resized_width - 3));
    REQUIRE_EQ(resized_track.bottom, ClipSoul::PopupListBottomForHeight(resized_height) - 4.0f);
    REQUIRE_EQ(resized_hit.right, static_cast<float>(resized_width));
    REQUIRE(resized_hit.left < resized_track.left);
    REQUIRE_EQ(resized_thumb.left, resized_track.left);
    REQUIRE_EQ(resized_thumb.right, resized_track.right);
    REQUIRE(resized_thumb.bottom <= resized_track.bottom);
}

TEST_CASE(PopupScrollbarThumbOpacityReflectsHoverAndDragFeedback) {
    REQUIRE_EQ(ClipSoul::PopupScrollbarThumbOpacity(false, false, 1.0f), 0.56f);
    REQUIRE(ClipSoul::PopupScrollbarThumbOpacity(true, false, 0.5f) > 0.56f);
    REQUIRE(ClipSoul::PopupScrollbarThumbOpacity(true, false, 0.5f) < 0.86f);
    REQUIRE_EQ(ClipSoul::PopupScrollbarThumbOpacity(false, true, 0.0f), 0.90f);
}

TEST_CASE(PopupScrollToTopButtonAppearsOnlyBeyond120AndAvoidsScrollbar) {
    const auto metrics = ClipSoul::PopupMetrics();
    const int resized_width = metrics.width + 120;
    const int resized_height = metrics.height + 90;

    REQUIRE(!ClipSoul::PopupScrollToTopButtonVisible(-1.0f));
    REQUIRE(!ClipSoul::PopupScrollToTopButtonVisible(0.0f));
    REQUIRE(!ClipSoul::PopupScrollToTopButtonVisible(0.01f));
    REQUIRE(!ClipSoul::PopupScrollToTopButtonVisible(119.99f));
    REQUIRE(!ClipSoul::PopupScrollToTopButtonVisible(120.0f));
    REQUIRE(ClipSoul::PopupScrollToTopButtonVisible(120.01f));

    const auto rect = ClipSoul::PopupScrollToTopButtonRectForSize(resized_width, resized_height);
    const auto scrollbar_hit = ClipSoul::PopupScrollbarHitRectForSize(resized_width, resized_height);

    REQUIRE_EQ(rect.Width(), 30.0f);
    REQUIRE_EQ(rect.Height(), 30.0f);
    REQUIRE(rect.top >= ClipSoul::PopupListTop());
    REQUIRE(rect.bottom <= ClipSoul::PopupListBottomForHeight(resized_height) - 12.0f);
    REQUIRE(rect.right < scrollbar_hit.left);
}

TEST_CASE(PopupMotionUsesPaperSlipEaseOutEntranceCurve) {
    REQUIRE_EQ(ClipSoul::PopupMotionProgress(-0.25f), 0.0f);
    REQUIRE_EQ(ClipSoul::PopupMotionProgress(1.25f), 1.0f);
    REQUIRE_EQ(ClipSoul::PopupMotionEnterOffset(0.0f, 18.0f), 18.0f);
    REQUIRE_EQ(ClipSoul::PopupMotionEnterOffset(1.0f, 18.0f), 0.0f);
    REQUIRE_EQ(ClipSoul::PopupMotionEnterOpacity(0.0f), 0.84f);
    REQUIRE_EQ(ClipSoul::PopupMotionEnterOpacity(1.0f), 1.0f);

    const float early = ClipSoul::PopupMotionProgress(0.25f);
    const float middle = ClipSoul::PopupMotionProgress(0.50f);
    REQUIRE(early > 0.55f);
    REQUIRE(middle > 0.85f);
    REQUIRE(middle > early);
    REQUIRE(ClipSoul::PopupMotionEnterOffset(middle, 18.0f) <
            ClipSoul::PopupMotionEnterOffset(early, 18.0f));
    REQUIRE(ClipSoul::PopupMotionEnterOpacity(middle) >
            ClipSoul::PopupMotionEnterOpacity(early));
}

TEST_CASE(PopupViewSwitchListOffsetNeverReversesNegativeDirectionProgress) {
    const float start_left = ClipSoul::PopupViewSwitchListOffset(-1, 0.0f, 18.0f);
    const float middle_left = ClipSoul::PopupViewSwitchListOffset(-1, 0.5f, 18.0f);
    const float end_left = ClipSoul::PopupViewSwitchListOffset(-1, 1.0f, 18.0f);

    REQUIRE(start_left < 0.0f);
    REQUIRE(middle_left < 0.0f);
    REQUIRE_EQ(end_left, 0.0f);
    REQUIRE(std::abs(middle_left) < std::abs(start_left));

    const float start_right = ClipSoul::PopupViewSwitchListOffset(1, 0.0f, 18.0f);
    const float middle_right = ClipSoul::PopupViewSwitchListOffset(1, 0.5f, 18.0f);
    const float end_right = ClipSoul::PopupViewSwitchListOffset(1, 1.0f, 18.0f);

    REQUIRE(start_right > 0.0f);
    REQUIRE(middle_right > 0.0f);
    REQUIRE_EQ(end_right, 0.0f);
    REQUIRE(middle_right < start_right);
}

TEST_CASE(PopupPopoverMotionStartsFromInvokingButtonDirection) {
    const auto toolbar = ClipSoul::BuildPopupToolbarLayout(false);
    const auto tabs = ClipSoul::BuildPopupTabsLayoutForWidth(true, ClipSoul::PopupMetrics().width);
    const auto filter = ClipSoul::BuildPopupFilterLayout();
    const auto menu = ClipSoul::BuildPopupFavoriteGroupMenuLayout(3);

    const auto filter_start =
        ClipSoul::PopupMotionEnterOffsetFromTrigger(toolbar.filter, filter.panel, 0.0f, 16.0f);
    REQUIRE(filter_start.x <= 0.0f);
    REQUIRE(filter_start.y < 0.0f);

    const auto menu_start =
        ClipSoul::PopupMotionEnterOffsetFromTrigger(tabs.favorite_group, menu.panel, 0.0f, 10.0f);
    REQUIRE(std::abs(menu_start.x) < std::abs(menu_start.y));
    REQUIRE(menu_start.y < 0.0f);

    const auto filter_done =
        ClipSoul::PopupMotionEnterOffsetFromTrigger(toolbar.filter, filter.panel, 1.0f, 16.0f);
    REQUIRE_EQ(filter_done.x, 0.0f);
    REQUIRE_EQ(filter_done.y, 0.0f);
}

TEST_CASE(PopupPopoverExitMotionFadesWithoutDirectionalRetreat) {
    const auto toolbar = ClipSoul::BuildPopupToolbarLayout(false);
    const auto filter = ClipSoul::BuildPopupFilterLayout();

    const auto start =
        ClipSoul::PopupMotionExitOffsetFromTrigger(toolbar.filter, filter.panel, 0.0f, 16.0f);
    const auto first_frame =
        ClipSoul::PopupMotionExitOffsetFromTrigger(toolbar.filter, filter.panel, 0.16f, 16.0f);
    const auto middle =
        ClipSoul::PopupMotionExitOffsetFromTrigger(toolbar.filter, filter.panel, 0.50f, 16.0f);
    const auto end =
        ClipSoul::PopupMotionExitOffsetFromTrigger(toolbar.filter, filter.panel, 1.0f, 16.0f);

    REQUIRE_EQ(start.x, 0.0f);
    REQUIRE_EQ(start.y, 0.0f);
    REQUIRE_EQ(first_frame.x, 0.0f);
    REQUIRE_EQ(first_frame.y, 0.0f);
    REQUIRE_EQ(middle.x, 0.0f);
    REQUIRE_EQ(middle.y, 0.0f);
    REQUIRE_EQ(end.x, 0.0f);
    REQUIRE_EQ(end.y, 0.0f);
    REQUIRE_EQ(ClipSoul::PopupPopoverExitOpacity(0.0f), 1.0f);
    REQUIRE(ClipSoul::PopupPopoverExitOpacity(0.16f) > 0.92f);
    REQUIRE(ClipSoul::PopupPopoverExitOpacity(0.50f) < ClipSoul::PopupPopoverExitOpacity(0.16f));
    REQUIRE_EQ(ClipSoul::PopupPopoverExitOpacity(1.0f), 0.0f);
}

TEST_CASE(PopupMultiSelectMotionUsesShortPaperSlideForToolbarAndCheckboxes) {
    REQUIRE_EQ(ClipSoul::PopupMultiSelectToolbarOffset(0.0f, true), 18.0f);
    REQUIRE_EQ(ClipSoul::PopupMultiSelectToolbarOffset(1.0f, true), 0.0f);
    REQUIRE_EQ(ClipSoul::PopupMultiSelectToolbarOffset(0.0f, false), 0.0f);
    REQUIRE_EQ(ClipSoul::PopupMultiSelectToolbarOffset(1.0f, false), 0.0f);

    REQUIRE_EQ(ClipSoul::PopupMultiSelectCheckboxOffset(0.0f, true), -18.0f);
    REQUIRE_EQ(ClipSoul::PopupMultiSelectCheckboxOffset(1.0f, true), 0.0f);
    REQUIRE_EQ(ClipSoul::PopupMultiSelectCheckboxOffset(0.0f, false), 0.0f);
    REQUIRE_EQ(ClipSoul::PopupMultiSelectCheckboxOffset(1.0f, false), -18.0f);

    REQUIRE_EQ(ClipSoul::PopupMultiSelectCheckboxOpacity(0.0f, true), 0.0f);
    REQUIRE_EQ(ClipSoul::PopupMultiSelectCheckboxOpacity(1.0f, true), 1.0f);
    REQUIRE_EQ(ClipSoul::PopupMultiSelectCheckboxOpacity(0.0f, false), 1.0f);
    REQUIRE_EQ(ClipSoul::PopupMultiSelectCheckboxOpacity(1.0f, false), 0.0f);
}

TEST_CASE(PopupToggleKnobUsesAnimatedTravelBetweenStates) {
    REQUIRE_EQ(ClipSoul::PopupToggleKnobPosition(false, true, 0.0f), 0.0f);
    REQUIRE_EQ(ClipSoul::PopupToggleKnobPosition(false, true, 1.0f), 1.0f);
    REQUIRE_EQ(ClipSoul::PopupToggleKnobPosition(true, false, 0.0f), 1.0f);
    REQUIRE_EQ(ClipSoul::PopupToggleKnobPosition(true, false, 1.0f), 0.0f);

    const float turning_on = ClipSoul::PopupToggleKnobPosition(false, true, 0.25f);
    const float turning_off = ClipSoul::PopupToggleKnobPosition(true, false, 0.25f);
    REQUIRE(turning_on > 0.0f);
    REQUIRE(turning_on < 1.0f);
    REQUIRE(turning_off > 0.0f);
    REQUIRE(turning_off < 1.0f);
    REQUIRE(turning_on > 1.0f - turning_off - 0.01f);
}

TEST_CASE(PopupListClipRectStartsAtListAndEndsBeforeWindowBottom) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto clip = ClipSoul::PopupListClipRectForHeight(metrics.width + 120, metrics.height + 80);
    const auto footer = ClipSoul::PopupFooterRectForSize(metrics.width + 120, metrics.height + 80);

    REQUIRE_EQ(clip.left, 0.0f);
    REQUIRE_EQ(clip.top, ClipSoul::PopupListTop());
    REQUIRE_EQ(clip.right, static_cast<float>(metrics.width + 120));
    REQUIRE_EQ(clip.bottom, footer.top);
    REQUIRE_EQ(footer.bottom, static_cast<float>(metrics.height + 80));
    REQUIRE(clip.top >= ClipSoul::BuildPopupTabsLayout(false).divider.bottom);
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
    REQUIRE(ClipSoul::PopupExpandedCardExtraHeightForText(L"\u5907\u6ce8\uff1a\u7b2c\u4e00\u884c\n\u7b2c\u4e8c\u884c\n\u7b2c\u4e09\u884c\n\u7b2c\u56db\u884c", 230.0f) >
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

    REQUIRE_EQ(card.expand.Width(), 20.0f);
    REQUIRE_EQ(card.expand.Height(), 18.0f);
    REQUIRE(card.expand.right <= card.card.right - 8.0f);
    REQUIRE_EQ(card.expand.right, card.time.right);
    REQUIRE(card.expand.top >= card.time.bottom);
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

TEST_CASE(PopupWindowTintUsesOpaqueSolidSurface) {
    const auto palette = ClipSoul::ResolvePopupThemePalette(1, false);

    REQUIRE_EQ(palette.window_opacity, 1.0f);
    REQUIRE_EQ(palette.window_tint, 0xF7F4EE);
}

TEST_CASE(PopupTabsUseSegmentedTextLayoutNotButtons) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto history = ClipSoul::BuildPopupTabsLayout(false);
    const auto favorites = ClipSoul::BuildPopupTabsLayout(true);
    const auto toolbar = ClipSoul::BuildPopupToolbarLayout(false);

    RequireInsidePanel(history.history);
    RequireInsidePanel(history.favorites);
    REQUIRE(!ClipSoul::RectsOverlap(history.history, history.favorites));
    REQUIRE(history.history.Width() >= 40.0f);
    REQUIRE(history.favorites.Width() >= 40.0f);
    REQUIRE_EQ(history.history.left, static_cast<float>(metrics.margin));
    REQUIRE(history.active_indicator.bottom <= history.divider.bottom);
    REQUIRE_EQ(history.active_indicator.left, history.history.left);
    REQUIRE_EQ(history.active_indicator.right, history.history.left + 26.0f);
    REQUIRE_EQ(favorites.active_indicator.Width(), 26.0f);
    REQUIRE_EQ(history.divider.left, 0.0f);
    REQUIRE_EQ(history.divider.right, static_cast<float>(metrics.width));
    REQUIRE(favorites.active_indicator.left > history.active_indicator.left);
    RequireInsidePanel(favorites.favorite_group);
    REQUIRE(!ClipSoul::RectsOverlap(favorites.favorite_group, favorites.favorites));
    REQUIRE(!ClipSoul::RectsOverlap(favorites.favorite_group, favorites.add_favorite_phrase));
    REQUIRE(favorites.add_favorite_phrase.Width() >= 24.0f);
    REQUIRE(favorites.add_favorite_phrase.right <= toolbar.filter.left);
    REQUIRE(!ClipSoul::RectsOverlap(favorites.add_favorite_phrase, favorites.favorites));
}

TEST_CASE(PopupTabIndicatorInterpolatesBetweenActiveTabs) {
    const auto history = ClipSoul::BuildPopupTabsLayout(false);
    const auto favorites = ClipSoul::BuildPopupTabsLayout(true);

    const auto start = ClipSoul::PopupTabIndicatorRectForMotion(history.active_indicator,
                                                                favorites.active_indicator, 0.0f);
    const auto middle = ClipSoul::PopupTabIndicatorRectForMotion(history.active_indicator,
                                                                 favorites.active_indicator, 0.5f);
    const auto end = ClipSoul::PopupTabIndicatorRectForMotion(history.active_indicator,
                                                              favorites.active_indicator, 1.0f);

    RequireNear(start.left, history.active_indicator.left);
    RequireNear(start.right, history.active_indicator.right);
    RequireNear(end.left, favorites.active_indicator.left);
    RequireNear(end.right, favorites.active_indicator.right);
    RequireNear(middle.left, (history.active_indicator.left + favorites.active_indicator.left) * 0.5f);
    RequireNear(middle.right, (history.active_indicator.right + favorites.active_indicator.right) * 0.5f);
    RequireNear(middle.top, history.active_indicator.top);
    RequireNear(middle.bottom, history.active_indicator.bottom);
}

TEST_CASE(PopupFavoriteGroupMenuUsesInWindowPopoverLayout) {
    const auto metrics = ClipSoul::PopupMetrics();
    const auto layout = ClipSoul::BuildPopupFavoriteGroupMenuLayout(3);
    const auto tabs = ClipSoul::BuildPopupTabsLayout(true);

    REQUIRE(layout.panel.left >= 12.0f);
    REQUIRE(layout.panel.right <= static_cast<float>(metrics.width - 12));
    REQUIRE_EQ(layout.panel.left, 92.0f);
    REQUIRE_EQ(layout.panel.top, 124.0f);
    REQUIRE_EQ(layout.panel.Width(), 180.0f);
    REQUIRE_EQ(layout.all_favorites.Height(), 30.0f);
    REQUIRE_EQ(layout.new_group.Height(), 30.0f);
    REQUIRE(layout.panel.left <= tabs.favorite_group.left);
    REQUIRE(layout.panel.top > tabs.favorite_group.bottom);
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

TEST_CASE(PopupPopoverMotionOffsetStartsFromTriggerNearestPanelAnchor) {
    const auto tabs = ClipSoul::BuildPopupTabsLayout(true);
    const auto menu = ClipSoul::BuildPopupFavoriteGroupMenuLayout(3);

    const auto offset = ClipSoul::PopupMotionEnterOffsetFromTrigger(tabs.favorite_group, menu.panel, 0.0f, 10.0f);

    REQUIRE(offset.y < 0.0f);
    REQUIRE(std::abs(offset.x) < std::abs(offset.y));
    REQUIRE(std::abs(offset.y) > 9.0f);

    const auto filter_trigger = ClipSoul::BuildPopupToolbarLayout(false).filter;
    const auto filter = ClipSoul::BuildPopupFilterLayout();
    const auto filter_offset = ClipSoul::PopupMotionEnterOffsetFromTrigger(filter_trigger, filter.panel, 0.0f, 16.0f);
    REQUIRE(filter_offset.y < 0.0f);
    REQUIRE(std::abs(filter_offset.x) < std::abs(filter_offset.y));
}

TEST_CASE(PopupPromptWindowsAvoidSystemSlideAnimation) {
    REQUIRE(!ClipSoul::PopupPromptShouldUseSlideAnimation());
    REQUIRE(!ClipSoul::PopupPromptShouldUseBlendAnimation());
    REQUIRE(ClipSoul::PopupPromptShouldUsePositionNudgeAnimation());
    REQUIRE(ClipSoul::PopupPromptEntranceOffsetPixels() >= 4);
    REQUIRE(ClipSoul::PopupPromptEntranceOffsetPixels() <= 8);
    REQUIRE(ClipSoul::PopupPromptEntranceStepCount() >= 6);
    REQUIRE(ClipSoul::PopupPromptEntranceStepCount() <= 8);
    REQUIRE_EQ(ClipSoul::PopupPromptEntranceTimerIntervalMs(), 16);
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
    const auto metrics = ClipSoul::PopupMetrics();
    const auto layout = ClipSoul::BuildPopupFilterLayout();
    const auto compact = ClipSoul::BuildPopupFilterLayout(false, 5);

    REQUIRE(layout.panel.left >= 12.0f);
    REQUIRE(layout.panel.right <= static_cast<float>(metrics.width - 12));
    REQUIRE(layout.panel.Width() >= 300.0f);
    // 六周日历按 CSS 自然高度延伸到 569；不能压缩日期格来迁就列表底线。
    REQUIRE_EQ(layout.panel.bottom, 569.0f);
    // 统一左右边线：类型条、时间条、日历、底部分隔线都贴同一内容边线
    REQUIRE_EQ(layout.type_bar.left, layout.panel.left + 14.0f);
    REQUIRE_EQ(layout.type_bar.right, layout.panel.right - 14.0f);
    REQUIRE_EQ(layout.preset_bar.left, layout.type_bar.left);
    REQUIRE_EQ(layout.preset_bar.right, layout.type_bar.right);
    REQUIRE_EQ(layout.calendar_grid.left, layout.type_bar.left);
    REQUIRE_EQ(layout.calendar_grid.right, layout.type_bar.right);
    REQUIRE_EQ(layout.footer_divider.left, layout.type_bar.left);
    REQUIRE_EQ(layout.done.right, layout.type_bar.right);
    // 类型四等分
    REQUIRE(layout.image_chip.left > layout.text_chip.right);
    REQUIRE(layout.file_chip.left > layout.image_chip.right);
    REQUIRE(layout.link_chip.left > layout.file_chip.right);
    RequireNear(layout.text_chip.Width(), layout.link_chip.Width());
    REQUIRE_EQ(layout.text_chip.top, layout.link_chip.top);
    // 五段时间预设
    for (int i = 1; i < 5; ++i) {
        REQUIRE(layout.presets[i].left > layout.presets[i - 1].right);
        RequireNear(layout.presets[i].Width(), layout.presets[0].Width());
    }
    REQUIRE(layout.preset_bar.top > layout.type_bar.bottom);
    // 起止日期内嵌在"时间"标签行右侧
    REQUIRE(layout.end_date.left > layout.start_date.right);
    REQUIRE(layout.start_date.bottom <= layout.preset_bar.top);
    // 日历仅自定义时展开
    REQUIRE(layout.calendar.top > layout.preset_bar.bottom);
    REQUIRE(layout.footer_divider.top >= layout.calendar_grid.bottom);
    REQUIRE(compact.panel.Height() < layout.panel.Height() - 100.0f);
    REQUIRE(!compact.custom_range);
    REQUIRE(ClipSoul::BuildPopupCalendarCells(compact, 2026, 5).empty());
    // 重置在头部，完成在底部
    REQUIRE(layout.reset.bottom < layout.type_bar.top);
    REQUIRE(layout.reset.right <= layout.close.left);
    REQUIRE(layout.done.top > layout.footer_divider.bottom);
    REQUIRE(layout.count_text.right <= layout.done.left);
    REQUIRE_EQ(ClipSoul::PopupFilterResetVisualRect(layout).left, layout.reset.left);
}

TEST_CASE(PopupFilterLayoutMatchesCssRhythmAtEachCalendarHeight) {
    const auto compact = ClipSoul::BuildPopupFilterLayout(false, 6);
    RequireNear(compact.panel.left, 12.0f);
    RequireNear(compact.panel.top, 124.0f);
    RequireNear(compact.panel.Width(), 316.0f);
    RequireNear(compact.close.top, 136.0f);
    RequireNear(compact.close.Height(), 24.0f);
    RequireNear(compact.type_section.top, 172.0f);
    RequireNear(compact.type_section.Height(), 16.0f);
    RequireNear(compact.type_bar.top, 194.0f);
    RequireNear(compact.type_bar.Height(), 30.0f);
    RequireNear(compact.date_card.top, 238.0f);
    RequireNear(compact.preset_bar.top, 260.0f);
    RequireNear(compact.preset_bar.Height(), 30.0f);
    RequireNear(compact.footer_divider.top, 304.0f);
    RequireNear(compact.done.top, 317.0f);
    RequireNear(compact.done.Height(), 28.0f);
    RequireNear(compact.panel.bottom, 359.0f);
    for (int rows = 4; rows <= 6; ++rows) {
        const auto layout = ClipSoul::BuildPopupFilterLayout(true, rows);
        const auto labels = ClipSoul::BuildPopupCalendarWeekdayLabels(layout);
        RequireNear(layout.calendar.top, 300.0f);
        RequireNear(layout.calendar_title.Height(), 22.0f);
        RequireNear(labels.front().rect.top, 326.0f);
        RequireNear(labels.front().rect.Height(), 18.0f);
        RequireNear(layout.calendar_grid.top, 346.0f);
        RequireNear(layout.calendar_grid.Height(), static_cast<float>(rows * 26 - 2));
        RequireNear(layout.calendar.bottom, layout.calendar_grid.bottom);
        RequireNear(layout.footer_divider.top - layout.calendar.bottom, 14.0f);
        RequireNear(layout.footer_divider.Height(), 1.0f);
        RequireNear(layout.done.top - layout.footer_divider.bottom, 12.0f);
        RequireNear(layout.panel.bottom - layout.done.bottom, 14.0f);
        RequireNear(layout.panel.bottom, 413.0f + static_cast<float>(rows * 26));
        RequireNear(layout.calendar_next_year.right - layout.calendar_prev_year.left, 88.0f);
    }
    RequireNear(ClipSoul::BuildPopupFilterLayout(true, -1).panel.bottom,
                ClipSoul::BuildPopupFilterLayout(true, 4).panel.bottom);
    RequireNear(ClipSoul::BuildPopupFilterLayout(true, 99).panel.bottom,
                ClipSoul::BuildPopupFilterLayout(true, 6).panel.bottom);
}

TEST_CASE(PopupFilterWidthOverloadKeepsSegmentsAndDateHitsInsidePanel) {
    const auto original = ClipSoul::BuildPopupFilterLayout();
    const auto explicit_default = ClipSoul::BuildPopupFilterLayout(true, 6, 340);
    RequireNear(original.panel.right, explicit_default.panel.right);
    RequireNear(original.panel.bottom, explicit_default.panel.bottom);
    for (const int width : {300, 340, 640}) {
        for (const bool custom : {false, true}) {
            const auto layout = ClipSoul::BuildPopupFilterLayout(custom, 5, width);
            RequireNear(layout.panel.Width(), static_cast<float>(width - 24));
            RequireNear(layout.type_bar.Width(), static_cast<float>(width - 52));
            RequireNear(layout.type_bar.top, 194.0f);
            RequireNear(layout.preset_bar.top, 260.0f);
            const ClipSoul::UiRect targets[] = {layout.text_chip, layout.image_chip, layout.file_chip,
                layout.link_chip, layout.presets[0], layout.presets[1], layout.presets[2],
                layout.presets[3], layout.presets[4], layout.reset, layout.close,
                layout.start_date, layout.end_date, layout.done};
            const ClipSoul::PopupFilterTarget expected[] = {ClipSoul::PopupFilterTarget::TextChip,
                ClipSoul::PopupFilterTarget::ImageChip, ClipSoul::PopupFilterTarget::FileChip,
                ClipSoul::PopupFilterTarget::LinkChip, ClipSoul::PopupFilterTarget::PresetAll,
                ClipSoul::PopupFilterTarget::PresetToday, ClipSoul::PopupFilterTarget::PresetWeek,
                ClipSoul::PopupFilterTarget::PresetMonth, ClipSoul::PopupFilterTarget::PresetCustom,
                ClipSoul::PopupFilterTarget::Reset, ClipSoul::PopupFilterTarget::Close,
                ClipSoul::PopupFilterTarget::StartDate, ClipSoul::PopupFilterTarget::EndDate,
                ClipSoul::PopupFilterTarget::Done};
            for (size_t i = 0; i < std::size(targets); ++i) {
                REQUIRE(targets[i].left >= layout.panel.left);
                REQUIRE(targets[i].right <= layout.panel.right);
                REQUIRE(targets[i].Width() > 0.0f);
                REQUIRE_EQ(ClipSoul::HitTestPopupFilterTarget(layout, 2026, 6,
                    (targets[i].left + targets[i].right) * 0.5f,
                    (targets[i].top + targets[i].bottom) * 0.5f), expected[i]);
                for (size_t j = i + 1; j < std::size(targets); ++j) {
                    REQUIRE(!ClipSoul::RectsOverlap(targets[i], targets[j]));
                }
            }
        }
    }
}

TEST_CASE(PopupCalendarUsesFourFiveAndSixRowsWithoutHittingGapsOrFooter) {
    // February 2026 starts Sunday (4 weeks); June 2026 has 5; May 2026 has 6.
    const int months[] = {2, 6, 5};
    for (int i = 0; i < 3; ++i) {
        const int rows = ClipSoul::PopupCalendarRowCount(2026, months[i]);
        REQUIRE_EQ(rows, i + 4);
        const auto layout = ClipSoul::BuildPopupFilterLayout(true, rows);
        const auto cells = ClipSoul::BuildPopupCalendarCells(layout, 2026, months[i]);
        RequireNear(cells.back().rect.bottom, layout.calendar_grid.bottom);
        for (const auto& cell : cells) {
            RequireNear(cell.rect.Height(), 24.0f);
            REQUIRE(cell.rect.bottom <= layout.calendar_grid.bottom);
            const float x = (cell.rect.left + cell.rect.right) * 0.5f;
            const auto hit = ClipSoul::HitTestPopupCalendarDate(layout, 2026, months[i], x,
                (cell.rect.top + cell.rect.bottom) * 0.5f);
            REQUIRE(hit.has_value());
            REQUIRE(*hit == cell.date);
            REQUIRE(!ClipSoul::HitTestPopupCalendarDate(layout, 2026, months[i], x,
                cell.rect.bottom + 1.0f).has_value());
        }
        REQUIRE_EQ(ClipSoul::HitTestPopupFilterTarget(layout, 2026, months[i],
            layout.done.left + 10.0f, layout.done.top + 10.0f), ClipSoul::PopupFilterTarget::Done);
    }
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
    REQUIRE_EQ(cells.front().rect.Height(), 24.0f);
    for (size_t index = 7; index < cells.size(); ++index) {
        REQUIRE_EQ(cells[index].rect.top - cells[index - 7].rect.top, 26.0f);
        REQUIRE_EQ(cells[index].rect.top - cells[index - 7].rect.bottom, 2.0f);
    }
    REQUIRE_EQ(cells.back().rect.bottom, layout.calendar.bottom);
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
                                                   (layout.calendar_prev_year.left + layout.calendar_prev_year.right) / 2.0f,
                                                   (layout.calendar_prev_year.top + layout.calendar_prev_year.bottom) / 2.0f),
               ClipSoul::PopupCalendarArrow::PreviousYear);
    REQUIRE_EQ(ClipSoul::HitTestPopupCalendarArrow(layout,
                                                   (layout.calendar_prev.left + layout.calendar_prev.right) / 2.0f,
                                                   (layout.calendar_prev.top + layout.calendar_prev.bottom) / 2.0f),
               ClipSoul::PopupCalendarArrow::PreviousMonth);
    REQUIRE_EQ(ClipSoul::HitTestPopupCalendarArrow(layout,
                                                   (layout.calendar_next.left + layout.calendar_next.right) / 2.0f,
                                                   (layout.calendar_next.top + layout.calendar_next.bottom) / 2.0f),
               ClipSoul::PopupCalendarArrow::NextMonth);
    REQUIRE_EQ(ClipSoul::HitTestPopupCalendarArrow(layout,
                                                   (layout.calendar_next_year.left + layout.calendar_next_year.right) / 2.0f,
                                                   (layout.calendar_next_year.top + layout.calendar_next_year.bottom) / 2.0f),
               ClipSoul::PopupCalendarArrow::NextYear);
}

TEST_CASE(PopupCalendarArrowGlyphsAreCenteredInHoverTargets) {
    const auto layout = ClipSoul::BuildPopupFilterLayout();
    const auto prev_year = ClipSoul::PopupFilterArrowGlyphRect(layout.calendar_prev_year);
    const auto prev = ClipSoul::PopupFilterArrowGlyphRect(layout.calendar_prev);
    const auto next = ClipSoul::PopupFilterArrowGlyphRect(layout.calendar_next);
    const auto next_year = ClipSoul::PopupFilterArrowGlyphRect(layout.calendar_next_year);
    const auto center_x = [](const ClipSoul::UiRect& rect) { return (rect.left + rect.right) * 0.5f; };
    const auto center_y = [](const ClipSoul::UiRect& rect) { return (rect.top + rect.bottom) * 0.5f; };

    REQUIRE(std::abs(center_x(prev_year) - center_x(layout.calendar_prev_year)) <= 0.01f);
    REQUIRE(std::abs(center_y(prev_year) - center_y(layout.calendar_prev_year)) <= 0.01f);
    REQUIRE(std::abs(center_x(prev) - center_x(layout.calendar_prev)) <= 0.01f);
    REQUIRE(std::abs(center_y(prev) - center_y(layout.calendar_prev)) <= 0.01f);
    REQUIRE(std::abs(center_x(next) - center_x(layout.calendar_next)) <= 0.01f);
    REQUIRE(std::abs(center_y(next) - center_y(layout.calendar_next)) <= 0.01f);
    REQUIRE(std::abs(center_x(next_year) - center_x(layout.calendar_next_year)) <= 0.01f);
    REQUIRE(std::abs(center_y(next_year) - center_y(layout.calendar_next_year)) <= 0.01f);
    REQUIRE(prev.Width() <= 12.0f);
    REQUIRE(next.Height() <= 14.0f);
}

TEST_CASE(PopupCalendarRangeSegmentsWrapSelectedDateSpanByRows) {
    const auto layout = ClipSoul::BuildPopupFilterLayout();
    ClipSoul::PopupDateRangeState state;
    state.start = ClipSoul::PopupCalendarDate{2026, 6, 3};
    state.end = ClipSoul::PopupCalendarDate{2026, 6, 12};

    const auto segments = ClipSoul::BuildPopupCalendarRangeSegments(layout, 2026, 6, state);

    REQUIRE(!segments.empty());
    for (const auto& segment : segments) {
        REQUIRE(segment.rect.left < segment.rect.right);
        REQUIRE(segment.rect.Height() >= 14.0f);
        REQUIRE(segment.starts_range);
        REQUIRE(segment.ends_range);
        REQUIRE(segment.rect.left >= layout.calendar.left);
        REQUIRE(segment.rect.right <= layout.calendar.right);
    }
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
    REQUIRE(!ClipSoul::ThemeModeResolvesDarkChrome(0, false));
    REQUIRE(ClipSoul::ThemeModeResolvesDarkChrome(0, true));
    REQUIRE(!ClipSoul::ThemeModeResolvesDarkChrome(1, true));
    REQUIRE(ClipSoul::ThemeModeResolvesDarkChrome(2, false));
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
    REQUIRE_EQ(fallback.y, 504);

    const auto no_caret = ClipSoul::PopupBottomRightFallback(size, work, 96);
    REQUIRE_EQ(no_caret.x, 1548);
    REQUIRE_EQ(no_caret.y, 504);

    const RECT target{120, 100, 920, 740};
    const auto window = ClipSoul::PopupWindowRectFallback(target, size, work, 96);
    REQUIRE_EQ(window.x, 930);
    REQUIRE_EQ(window.y, 110);

    const auto target_center = ClipSoul::PopupTargetCenterFallback(target, size, work, 96);
    REQUIRE_EQ(target_center.x, 530);
    REQUIRE_EQ(target_center.y, 430);

    const auto console_anchor =
        ClipSoul::PopupConsoleCellAnchor(POINT{100, 80}, COORD{12, 8}, SMALL_RECT{0, 3, 79, 32}, COORD{8, 16});
    REQUIRE_EQ(console_anchor.x, 204);
    REQUIRE_EQ(console_anchor.y, 160);

    CONSOLE_SELECTION_INFO selection{};
    selection.dwFlags = CONSOLE_SELECTION_NOT_EMPTY;
    selection.dwSelectionAnchor = COORD{42, 19};
    selection.srSelection = SMALL_RECT{10, 5, 30, 7};
    const auto selection_anchor = ClipSoul::PopupConsoleSelectionAnchor(selection);
    REQUIRE_EQ(selection_anchor.X, 42);
    REQUIRE_EQ(selection_anchor.Y, 19);
    const auto selected_anchor_cell = ClipSoul::PopupConsoleAnchorCell(selection, COORD{7, 4});
    REQUIRE_EQ(selected_anchor_cell.X, 42);
    REQUIRE_EQ(selected_anchor_cell.Y, 19);

    CONSOLE_SELECTION_INFO cursor_selection{};
    cursor_selection.dwSelectionAnchor = COORD{42, 19};
    const auto cursor_anchor = ClipSoul::PopupConsoleSelectionAnchor(cursor_selection);
    REQUIRE_EQ(cursor_anchor.X, 42);
    REQUIRE_EQ(cursor_anchor.Y, 19);
    const auto cursor_anchor_cell = ClipSoul::PopupConsoleAnchorCell(cursor_selection, COORD{7, 4});
    REQUIRE_EQ(cursor_anchor_cell.X, 7);
    REQUIRE_EQ(cursor_anchor_cell.Y, 4);

    REQUIRE(target_center.x >= target.left);
    REQUIRE(target_center.x <= target.right);
    REQUIRE(target_center.y >= target.top);
    REQUIRE(target_center.y <= target.bottom);
}

TEST_CASE(PopupPositionClampsAroundCaretAnchor) {
    RECT work{0, 0, 1920, 1080};
    SIZE size{340, 560};
    const auto position = ClipSoul::ClampPopupToWorkArea(POINT{1000, 400}, size, work, 96);
    REQUIRE_EQ(position.x, 1010);
    REQUIRE_EQ(position.y, 410);

    const POINT caret{1000, 700};
    const auto text_anchor = ClipSoul::PopupTextAnchorPosition(caret, size, work, 96);
    REQUIRE_EQ(text_anchor.x, 1008);
    REQUIRE_EQ(text_anchor.y, 132);
    REQUIRE(text_anchor.x > caret.x);
    REQUIRE(text_anchor.y + size.cy <= caret.y - 8);

    const RECT text_range{1000, 680, 1000, 704};
    REQUIRE(ClipSoul::PopupTextRangeRectUsable(text_range));
    const auto range_anchor = ClipSoul::PopupTextRangeAnchor(text_range);
    REQUIRE_EQ(range_anchor.x, 1000);
    REQUIRE_EQ(range_anchor.y, 680);
    REQUIRE(ClipSoul::PopupTextRangeRectUsable(RECT{1000, 680, 1001, 704}));
    REQUIRE(!ClipSoul::PopupTextRangeRectUsable(RECT{0, 0, 0, 0}));
    REQUIRE(!ClipSoul::PopupTextRangeRectUsable(RECT{0, 0, 0, 1}));
    REQUIRE(!ClipSoul::PopupTextRangeRectUsable(RECT{64, 96, 1370, 148}));
    REQUIRE(!ClipSoul::PopupTextRangeRectUsable(RECT{1000, 680, 1060, 704}));
    REQUIRE(ClipSoul::PopupTextCharacterRectUsable(RECT{246, 78, 259, 102}, 144));
    REQUIRE(!ClipSoul::PopupTextCharacterRectUsable(RECT{240, 72, 2199, 108}, 144));
    const auto before_character = ClipSoul::PopupTextCharacterCaretRect(RECT{246, 78, 259, 102}, false);
    REQUIRE_EQ(before_character.left, 246);
    REQUIRE_EQ(before_character.right, 247);
    const auto after_character = ClipSoul::PopupTextCharacterCaretRect(RECT{246, 78, 259, 102}, true);
    REQUIRE_EQ(after_character.left, 258);
    REQUIRE_EQ(after_character.right, 259);
    REQUIRE(ClipSoul::PopupTextInputRectUsable(RECT{640, 96, 1370, 148}));
    REQUIRE(!ClipSoul::PopupTextInputRectUsable(RECT{0, 0, 0, 0}));
    REQUIRE(!ClipSoul::PopupTextInputRectUsable(RECT{100, 120, 110, 124}));
    REQUIRE(!ClipSoul::PopupTextInputRectUsable(RECT{6, 120, 2555, 1593}));
    REQUIRE(ClipSoul::PopupCaretAnchorAllowedByFocusedText(
        true, RECT{520, 1260, 1720, 1420}, true, RECT{1113, 1319, 1114, 1347}, 144));
    REQUIRE(!ClipSoul::PopupCaretAnchorAllowedByFocusedText(
        false, RECT{520, 1260, 1720, 1420}, true, RECT{1113, 1319, 1114, 1347}, 144));
    REQUIRE(!ClipSoul::PopupCaretAnchorAllowedByFocusedText(
        true, RECT{100, 100, 500, 240}, true, RECT{1113, 1319, 1114, 1347}, 144));
    REQUIRE(!ClipSoul::PopupCaretAnchorAllowedByFocusedText(
        true, RECT{}, false, RECT{1113, 1319, 1114, 1347}, 144));
    REQUIRE(ClipSoul::PopupJavaCaretRectUsable(RECT{560, 980, 562, 1008}, RECT{120, 80, 1800, 1200}, 144));
    REQUIRE(!ClipSoul::PopupJavaCaretRectUsable(RECT{0, 0, 0, 0}, RECT{120, 80, 1800, 1200}, 144));
    REQUIRE(!ClipSoul::PopupJavaCaretRectUsable(RECT{560, 980, 760, 1008}, RECT{120, 80, 1800, 1200}, 144));
    REQUIRE(!ClipSoul::PopupJavaCaretRectUsable(RECT{2400, 980, 2401, 1008}, RECT{120, 80, 1800, 1200}, 144));
    REQUIRE(ClipSoul::PopupVisualCaretRectUsable(RECT{650, 1040, 654, 1068}, RECT{120, 80, 1800, 1200}, 144));
    REQUIRE(!ClipSoul::PopupVisualCaretRectUsable(RECT{650, 1040, 720, 1068}, RECT{120, 80, 1800, 1200}, 144));
    REQUIRE(!ClipSoul::PopupVisualCaretRectUsable(RECT{650, 1040, 654, 1068}, RECT{800, 80, 1800, 1200}, 144));
    const auto input_anchor = ClipSoul::PopupTextInputRectAnchor(RECT{640, 96, 1370, 148}, 144);
    REQUIRE_EQ(input_anchor.x, 652);
    // 锚点落在输入框第一行文字的位置（框顶 + 10dip），而不是框顶本身。
    REQUIRE_EQ(input_anchor.y, 111);
    const auto input_position =
        ClipSoul::PopupTextAvoidRectPosition(RECT{640, 96, 1370, 148}, size, work, 144);
    REQUIRE_EQ(input_position.x, 664);
    // 上方放不下时落到输入框下方。
    REQUIRE_EQ(input_position.y, 160);
    REQUIRE(input_position.y > 148);

    const auto right_edge_text_anchor = ClipSoul::PopupTextAnchorPosition(POINT{1860, 700}, size, work, 96);
    REQUIRE_EQ(right_edge_text_anchor.x, 1564);
    REQUIRE_EQ(right_edge_text_anchor.y, 132);

    const auto top_edge_text_anchor = ClipSoul::PopupTextAnchorPosition(POINT{1000, 80}, size, work, 96);
    REQUIRE_EQ(top_edge_text_anchor.x, 1008);
    REQUIRE_EQ(top_edge_text_anchor.y, 88);
    REQUIRE(top_edge_text_anchor.y >= 80 + 8);

    const auto bottom_edge_text_anchor = ClipSoul::PopupTextAnchorPosition(POINT{1000, 1040}, size, work, 96);
    REQUIRE_EQ(bottom_edge_text_anchor.x, 1008);
    REQUIRE_EQ(bottom_edge_text_anchor.y, 472);
    REQUIRE(bottom_edge_text_anchor.y + size.cy <= 1040 - 8);

    REQUIRE(ClipSoul::PopupTargetNeedsShellTopmostRaise(L"Windows.UI.Core.CoreWindow", L""));
    REQUIRE(ClipSoul::PopupTargetNeedsShellTopmostRaise(L"", L"\u641c\u7d22"));
    REQUIRE(ClipSoul::PopupTargetNeedsShellTopmostRaise(L"Windows.UI.Core.CoreWindow", L"", L"SearchHost"));
    REQUIRE(ClipSoul::PopupTargetNeedsShellTopmostRaise(L"ApplicationFrameWindow", L"", L"TextInputHost"));
    REQUIRE(!ClipSoul::PopupTargetNeedsShellTopmostRaise(L"Chrome_WidgetWin_1", L"Codex"));
    REQUIRE(!ClipSoul::PopupShellTopmostRaiseShouldExpire(true, -1));
    REQUIRE(!ClipSoul::PopupShellTopmostRaiseShouldExpire(true, 0));
    REQUIRE(ClipSoul::PopupShellTopmostRaiseShouldExpire(false, -1));
    REQUIRE(!ClipSoul::PopupShellTopmostRaiseShouldExpire(false, 1));
    REQUIRE(ClipSoul::PopupShouldCreateInShellWindowBand());
    REQUIRE(!ClipSoul::PopupTargetRequiresExplicitTextInputFocus(L"Chrome_WidgetWin_1", L"Codex"));
    REQUIRE(!ClipSoul::PopupTargetRequiresExplicitTextInputFocus(L"Chrome_WidgetWin_1", L"Codex", L"Codex"));
    REQUIRE(!ClipSoul::PopupTargetRequiresExplicitTextInputFocus(L"Chrome_WidgetWin_1", L"Codex", L"msedge"));
    REQUIRE(!ClipSoul::PopupTargetRequiresExplicitTextInputFocus(L"Chrome_WidgetWin_1", L"Chrome"));
    REQUIRE(!ClipSoul::PopupTargetRequiresExplicitTextInputFocus(L"Chrome_WidgetWin_1", L"Visual Studio Code"));
    REQUIRE(!ClipSoul::PopupTargetRequiresExplicitTextInputFocus(L"Notepad", L""));
    REQUIRE(ClipSoul::PopupTargetCanUseConsoleAnchor(L"ConsoleWindowClass"));
    REQUIRE(!ClipSoul::PopupTargetCanUseConsoleAnchor(L"SunAwtFrame"));
    REQUIRE(!ClipSoul::PopupTargetCanUseConsoleAnchor(L"CASCADIA_HOSTING_WINDOW_CLASS"));
    REQUIRE(ClipSoul::PopupShouldUseTextAvoidForTarget(true, L"Windows.UI.Core.CoreWindow", L""));
    REQUIRE(ClipSoul::PopupShouldUseTextAvoidForTarget(true, L"", L"\u641c\u7d22"));
    REQUIRE(ClipSoul::PopupShouldUseTextAvoidForTarget(true, L"Chrome_WidgetWin_1", L"Codex"));
    REQUIRE(!ClipSoul::PopupShouldUseTextAvoidForTarget(false, L"Chrome_WidgetWin_1", L"Codex"));
}

TEST_CASE(PopupTextCaretPositionPrefersRightAboveCaret) {
    const SIZE size{340, 560};
    const RECT work{0, 0, 1920, 1080};

    const auto normal = ClipSoul::PopupTextAnchorPosition(POINT{1000, 700}, size, work, 96);
    REQUIRE_EQ(normal.x, 1008);
    REQUIRE_EQ(normal.y, 132);
    REQUIRE(normal.x > 1000);
    REQUIRE(normal.y + size.cy <= 700 - 8);
    REQUIRE(normal.y + size.cy <= work.bottom - 16);

    const auto right_edge = ClipSoul::PopupTextAnchorPosition(POINT{1860, 700}, size, work, 96);
    REQUIRE_EQ(right_edge.x, 1564);
    REQUIRE_EQ(right_edge.y, 132);
    REQUIRE(right_edge.x + size.cx <= work.right - 16);

    const auto top_edge = ClipSoul::PopupTextAnchorPosition(POINT{1000, 40}, size, work, 96);
    REQUIRE_EQ(top_edge.x, 1008);
    REQUIRE_EQ(top_edge.y, 48);
    REQUIRE(top_edge.y >= 40 + 8);

    const auto bottom_edge = ClipSoul::PopupTextAnchorPosition(POINT{1000, 1060}, size, work, 96);
    REQUIRE_EQ(bottom_edge.x, 1008);
    REQUIRE_EQ(bottom_edge.y, 492);
    REQUIRE(bottom_edge.y + size.cy <= work.bottom - 16);
}

TEST_CASE(PopupTextCaretPositionScalesGapsAndSupportsNegativeMonitorCoordinates) {
    const SIZE size{510, 840};
    const RECT work{-1920, 0, 0, 1440};

    const auto position = ClipSoul::PopupTextAnchorPosition(POINT{-900, 1100}, size, work, 144);
    REQUIRE_EQ(position.x, -888);
    REQUIRE_EQ(position.y, 248);
    REQUIRE(position.x >= work.left + 24);
    REQUIRE(position.x + size.cx <= work.right - 24);
    REQUIRE(position.y >= work.top + 24);
    REQUIRE(position.y + size.cy <= work.bottom - 24);
}

TEST_CASE(PopupTextCaretPositionFallsBelowOnlyWhenAboveDoesNotFit) {
    const SIZE size{510, 840};
    const RECT work{0, 0, 2560, 1600};

    const auto top_caret = ClipSoul::PopupTextAnchorPosition(POINT{1277, 100}, size, work, 144);
    REQUIRE_EQ(top_caret.x, 1289);
    REQUIRE_EQ(top_caret.y, 112);
    REQUIRE(top_caret.y > 100);

    const auto middle_caret = ClipSoul::PopupTextAnchorPosition(POINT{1277, 950}, size, work, 144);
    REQUIRE_EQ(middle_caret.x, 1289);
    REQUIRE_EQ(middle_caret.y, 98);
    REQUIRE(middle_caret.y + size.cy <= 950 - 12);

    const auto bottom_clamped = ClipSoul::PopupTextAnchorPosition(POINT{1277, 1500}, size, work, 144);
    REQUIRE_EQ(bottom_clamped.x, 1289);
    REQUIRE_EQ(bottom_clamped.y, 648);
    REQUIRE(bottom_clamped.y < 1500);
    REQUIRE(bottom_clamped.y + size.cy <= work.bottom - 24);
}

TEST_CASE(PopupTextCaretPositionDoesNotClampBelowChoiceBackOverCaret) {
    const SIZE size{510, 840};
    const RECT work{0, 0, 2560, 1000};
    const RECT caret{834, 855, 835, 884};

    const auto position = ClipSoul::PopupTextAvoidRectPosition(caret, size, work, 144);

    REQUIRE(position.y + size.cy <= work.bottom - 24);
    REQUIRE(position.y + size.cy <= caret.top - 12);
}

TEST_CASE(PopupWindowsClipboardPositionDocksToMonitorBottomRight) {
    const SIZE size{340, 560};
    const RECT monitor{0, 0, 1707, 1067};
    const RECT work{0, 0, 1707, 1019};

    const auto position = ClipSoul::PopupWindowsClipboardPosition(size, monitor, work, 96);
    REQUIRE_EQ(position.x, 1367);
    REQUIRE_EQ(position.y, 443);
    REQUIRE_EQ(position.x + size.cx, monitor.right);
    REQUIRE(position.y + size.cy <= work.bottom - 16);

    const SIZE large_size{510, 840};
    const RECT scaled_monitor{0, 0, 2560, 1600};
    const RECT scaled_work{0, 0, 2560, 1528};
    const auto scaled = ClipSoul::PopupWindowsClipboardPosition(large_size, scaled_monitor, scaled_work, 144);
    REQUIRE_EQ(scaled.x, 2050);
    REQUIRE_EQ(scaled.y, 664);
    REQUIRE_EQ(scaled.x + large_size.cx, scaled_monitor.right);
    REQUIRE(scaled.y + large_size.cy <= scaled_work.bottom - 24);

    const RECT unadjusted_work{0, 0, 2560, 1600};
    const auto unadjusted = ClipSoul::PopupWindowsClipboardPosition(large_size, scaled_monitor, unadjusted_work, 144);
    REQUIRE_EQ(unadjusted.x, 2050);
    REQUIRE_EQ(unadjusted.y, 652);
    REQUIRE(unadjusted.y + large_size.cy <= unadjusted_work.bottom - 108);
}

TEST_CASE(PopupWindowsClipboardPositionSupportsNegativeMonitorCoordinates) {
    const SIZE size{340, 560};
    const RECT monitor{-1920, 0, 1920, 1080};
    const RECT work{-1920, 0, 0, 1032};

    const auto position = ClipSoul::PopupWindowsClipboardPosition(size, monitor, work, 96);
    REQUIRE_EQ(position.x, 1580);
    REQUIRE_EQ(position.y, 456);
    REQUIRE(position.x >= monitor.left + 16);
    REQUIRE(position.x + size.cx <= monitor.right);
    REQUIRE(position.y >= monitor.top + 16);
    REQUIRE(position.y + size.cy <= work.bottom - 16);
}

TEST_CASE(PopupTextCaretPositionTakesPrecedenceOverWindowsClipboardDock) {
    const SIZE size{340, 560};
    const RECT monitor{0, 0, 1707, 1067};
    const RECT work{0, 0, 1707, 1019};
    const RECT caret{540, 220, 542, 253};

    const auto text_position = ClipSoul::PopupTextAvoidRectPosition(caret, size, work, 96);
    const auto dock_position = ClipSoul::PopupWindowsClipboardPosition(size, monitor, work, 96);

    REQUIRE_EQ(text_position.x, 550);
    REQUIRE_EQ(text_position.y, 261);
    REQUIRE(text_position.y >= caret.bottom + 8);
    REQUIRE(text_position.x != dock_position.x);
    REQUIRE(text_position.y != dock_position.y);
}

TEST_CASE(PopupTextCaretPositionUsesSameAnchorForShellSurfaces) {
    const SIZE size{340, 560};
    const RECT work{0, 0, 2560, 1528};
    const RECT start_search_surface{16, 64, 1910, 1420};
    const RECT middle_caret{120, 900, 121, 928};

    const auto position = ClipSoul::PopupTextAvoidRectPosition(middle_caret, size, work, 96);

    REQUIRE_EQ(position.x, 129);
    REQUIRE(position.x < start_search_surface.right);
    REQUIRE(position.y + size.cy <= middle_caret.top - 8);
    REQUIRE(position.y >= work.top + 16);

    const RECT top_caret{120, 120, 121, 148};
    const auto below = ClipSoul::PopupTextAvoidRectPosition(top_caret, size, work, 96);
    REQUIRE_EQ(below.x, 129);
    REQUIRE(below.x < start_search_surface.right);
    REQUIRE(below.y >= top_caret.bottom + 8);
}

TEST_CASE(PopupKeyboardInvocationPositionUsesTextCaretBeforeDockFallback) {
    const SIZE size{340, 560};
    const RECT monitor{0, 0, 1707, 1067};
    const RECT work{0, 0, 1707, 1019};
    const RECT caret{540, 220, 542, 253};

    const auto with_caret = ClipSoul::PopupKeyboardInvocationPosition(true, caret, size, monitor, work, 96);
    REQUIRE_EQ(with_caret.x, 550);
    REQUIRE_EQ(with_caret.y, 261);
    REQUIRE(with_caret.y >= caret.bottom + 8);

    const auto without_caret = ClipSoul::PopupKeyboardInvocationPosition(false, RECT{}, size, monitor, work, 96);
    REQUIRE_EQ(without_caret.x, 1367);
    REQUIRE_EQ(without_caret.y, 443);
}

TEST_CASE(PopupTextAvoidRectPositionKeepsPopupOutsideCaretRectangle) {
    const SIZE size{510, 840};
    const RECT work{0, 0, 2560, 1600};

    const RECT top_input{300, 60, 980, 108};
    const auto below_top_input = ClipSoul::PopupTextAvoidRectPosition(top_input, size, work, 144);
    REQUIRE_EQ(below_top_input.x, 324);
    REQUIRE_EQ(below_top_input.y, 120);
    REQUIRE(below_top_input.y >= top_input.bottom + 12);

    const RECT mid_caret{410, 1000, 422, 1040};
    const auto above_mid_caret = ClipSoul::PopupTextAvoidRectPosition(mid_caret, size, work, 144);
    REQUIRE_EQ(above_mid_caret.x, 434);
    REQUIRE_EQ(above_mid_caret.y, 148);
    REQUIRE(above_mid_caret.y + size.cy <= mid_caret.top - 12);

    const RECT lower_caret{1050, 1400, 1060, 1432};
    const auto clamped_lower_caret = ClipSoul::PopupTextAvoidRectPosition(lower_caret, size, work, 144);
    REQUIRE_EQ(clamped_lower_caret.x, 1072);
    REQUIRE_EQ(clamped_lower_caret.y, 548);
    REQUIRE(clamped_lower_caret.y + size.cy <= work.bottom - 24);
}

TEST_CASE(SettingsWindowCentersInWorkArea) {
    RECT work{0, 0, 1920, 1080};
    const auto position = ClipSoul::CenterWindowInWorkArea(SIZE{340, 340}, work);
    REQUIRE_EQ(position.x, 790);
    REQUIRE_EQ(position.y, 370);
}

TEST_CASE(SettingsWindowProjectButtonAvoidsDividerAndVersionRow) {
    const auto layout = ClipSoul::SettingsWindowProjectLayout();

    REQUIRE(layout.project_button.top > layout.section.top);
    REQUIRE(layout.project_button.bottom < layout.divider.top);
    REQUIRE(layout.divider.bottom <= layout.version_row.top);
    REQUIRE(!ClipSoul::RectsOverlap(layout.project_button, layout.divider));
    REQUIRE(!ClipSoul::RectsOverlap(layout.project_button, layout.version_row));
}

TEST_CASE(SettingsWindowControlsStayCenteredWithoutClipping) {
    const auto layout = ClipSoul::SettingsWindowControlLayout();
    constexpr float kContentRight = 622.0f;

    // 控件高 28；除开关外右边缘贴齐内容右线（Ink Ledger 对齐规则）。
    REQUIRE_EQ(static_cast<int>(layout.limit_edit.top), 86);
    REQUIRE_EQ(static_cast<int>(layout.limit_edit.bottom), 114);
    REQUIRE_EQ(static_cast<int>(layout.limit_edit.right), static_cast<int>(kContentRight));
    REQUIRE_EQ(static_cast<int>(layout.limit_edit.Width()), 156);
    REQUIRE_EQ(static_cast<int>(layout.theme_system.bottom - layout.theme_system.top), 28);
    REQUIRE_EQ(static_cast<int>(layout.theme_dark.right), static_cast<int>(kContentRight));
    // 开关统一 32×18
    for (const auto& toggle : {layout.pause_toggle, layout.startup_toggle, layout.popup_resizable_toggle}) {
        REQUIRE_EQ(static_cast<int>(toggle.Width()), 32);
        REQUIRE_EQ(static_cast<int>(toggle.Height()), 18);
        REQUIRE_EQ(static_cast<int>(toggle.right), static_cast<int>(kContentRight));
    }
    REQUIRE(layout.pause_toggle.top < layout.startup_toggle.top);
    REQUIRE(layout.startup_toggle.top < layout.popup_resizable_toggle.top);
    REQUIRE(!ClipSoul::RectsOverlap(layout.theme_system, layout.theme_light));
    REQUIRE(!ClipSoul::RectsOverlap(layout.theme_light, layout.theme_dark));
    REQUIRE_EQ(ClipSoul::HitTestSettingsThemeTarget(layout,
                                                    (layout.theme_system.left + layout.theme_system.right) / 2.0f,
                                                    (layout.theme_system.top + layout.theme_system.bottom) / 2.0f),
               ClipSoul::SettingsThemeTarget::System);
    REQUIRE_EQ(ClipSoul::HitTestSettingsThemeTarget(layout,
                                                    (layout.theme_light.left + layout.theme_light.right) / 2.0f,
                                                    (layout.theme_light.top + layout.theme_light.bottom) / 2.0f),
               ClipSoul::SettingsThemeTarget::Light);
    REQUIRE_EQ(ClipSoul::HitTestSettingsThemeTarget(layout,
                                                    (layout.theme_dark.left + layout.theme_dark.right) / 2.0f,
                                                    (layout.theme_dark.top + layout.theme_dark.bottom) / 2.0f),
               ClipSoul::SettingsThemeTarget::Dark);
    REQUIRE(layout.text_visual_offset >= 0);
    REQUIRE(layout.text_visual_offset <= 1);
    REQUIRE(!layout.uses_internal_dividers);
    REQUIRE(!layout.uses_external_hover_backplates);
}

TEST_CASE(PinButtonRotatesToFortyDegreesWhenPinned) {
    using ClipSoul::PopupPinRotationDegrees;
    // 未固定：0°；固定后：-40°（预览 #pinWin.on svg 的 rotate(-40deg)）。
    REQUIRE(std::fabs(PopupPinRotationDegrees(false, 1.0f)) < 0.001f);
    REQUIRE(std::fabs(PopupPinRotationDegrees(true, 1.0f) - (-40.0f)) < 0.001f);
    // 起始帧不跳变：从当前状态出发。
    REQUIRE(std::fabs(PopupPinRotationDegrees(true, 0.0f)) < 0.001f);
    REQUIRE(std::fabs(PopupPinRotationDegrees(false, 0.0f) - (-40.0f)) < 0.001f);
}

TEST_CASE(PinButtonRotationUsesOvershootingSpringEase) {
    using ClipSoul::PopupPinRotationDegrees;
    // cubic-bezier(.3,1.5,.5,1) 会先冲过终点再回落，形成预览里的弹性旋转。
    float max_angle = 0.0f;
    bool overshoot = false;
    for (int step = 0; step <= 100; ++step) {
        const float angle = PopupPinRotationDegrees(true, static_cast<float>(step) / 100.0f);
        max_angle = std::min(max_angle, angle);
        if (angle < -40.05f) {
            overshoot = true;
        }
    }
    REQUIRE(overshoot);
    REQUIRE(max_angle > -50.0f);
    // 越过终点后必须回落到 -40°（弹性回弹只冲过头一次，不来回抖）。
    const int peak_step = [&] {
        int best = 0;
        for (int step = 0; step <= 100; ++step) {
            if (std::fabs(PopupPinRotationDegrees(true, static_cast<float>(step) / 100.0f) - max_angle) < 0.001f) {
                best = step;
                break;
            }
        }
        return best;
    }();
    REQUIRE(peak_step > 0);
    REQUIRE(peak_step < 100);
    for (int step = peak_step; step <= 100; ++step) {
        const float angle = PopupPinRotationDegrees(true, static_cast<float>(step) / 100.0f);
        // 冲过头之后一路回落，始终在 [-43.3, -40] 之间，最终停在 -40°。
        REQUIRE(angle <= -40.0f + 0.001f);
        REQUIRE(angle >= -44.0f);
    }
}

TEST_CASE(CubicBezierEaseMatchesCssEndpoints) {
    using ClipSoul::PopupCubicBezierEase;
    REQUIRE(std::fabs(PopupCubicBezierEase(0.3f, 1.5f, 0.5f, 1.0f, 0.0f)) < 0.0001f);
    REQUIRE(std::fabs(PopupCubicBezierEase(0.3f, 1.5f, 0.5f, 1.0f, 1.0f) - 1.0f) < 0.0001f);
    // 越界的进度被夹住，不会外插。
    REQUIRE(std::fabs(PopupCubicBezierEase(0.3f, 1.5f, 0.5f, 1.0f, -1.0f)) < 0.0001f);
    REQUIRE(std::fabs(PopupCubicBezierEase(0.3f, 1.5f, 0.5f, 1.0f, 2.0f) - 1.0f) < 0.0001f);
}

TEST_CASE(SettingsWindowHistoryLimitEditDoesNotAutoSelectAllOnLoad) {
    REQUIRE(!ClipSoul::SettingsLimitEditShouldSelectAllOnLoad());
}

TEST_CASE(SettingsThemePreviewRefreshesNativeControlsWhenModeChanges) {
    REQUIRE(ClipSoul::SettingsThemePreviewShouldRefreshNativeControls(1, 2));
    REQUIRE(ClipSoul::SettingsThemePreviewShouldRefreshNativeControls(2, 1));
    REQUIRE(ClipSoul::SettingsThemePreviewShouldRefreshNativeControls(0, 2));
    REQUIRE(!ClipSoul::SettingsThemePreviewShouldRefreshNativeControls(1, 1));
    REQUIRE(!ClipSoul::SettingsThemePreviewShouldRefreshNativeControls(-1, 0));
    REQUIRE(!ClipSoul::SettingsThemePreviewShouldRefreshNativeControls(3, 2));
}

TEST_CASE(SystemThemeChangeRefreshesOnlyWhenFollowingSystem) {
    REQUIRE(ClipSoul::ThemeChangeShouldRefreshPalette(0));
    REQUIRE(!ClipSoul::ThemeChangeShouldRefreshPalette(1));
    REQUIRE(!ClipSoul::ThemeChangeShouldRefreshPalette(2));
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

// ---------- Ink Ledger：时间驱动动效（计划 E） ----------

TEST_CASE(PopupMotionDurationsMatchInkLedgerSpec) {
    using ClipSoul::PopupMotionKind;
    REQUIRE_EQ(ClipSoul::PopupMotionDurationMs(PopupMotionKind::Panel), 160);
    REQUIRE_EQ(ClipSoul::PopupMotionDurationMs(PopupMotionKind::ViewSwitch), 180);
    REQUIRE_EQ(ClipSoul::PopupMotionDurationMs(PopupMotionKind::Arrow), 220);
    REQUIRE_EQ(ClipSoul::PopupMotionDurationMs(PopupMotionKind::MultiSelect), 200);
    REQUIRE_EQ(ClipSoul::PopupMotionDurationMs(PopupMotionKind::Hover), 90);
}

TEST_CASE(PopupTimedMotionProgressAdvancesByElapsedTime) {
    // 按时间推进：同样帧数、不同帧率得到同样的进度。
    const float at_half = ClipSoul::PopupTimedMotionProgress(80.0, 160.0, true);
    RequireNear(at_half, 0.5f);
    RequireNear(ClipSoul::PopupTimedMotionProgress(0.0, 160.0, true), 0.0f);
    RequireNear(ClipSoul::PopupTimedMotionProgress(160.0, 160.0, true), 1.0f);
    RequireNear(ClipSoul::PopupTimedMotionProgress(400.0, 160.0, true), 1.0f);
    RequireNear(ClipSoul::PopupTimedMotionProgress(-5.0, 160.0, true), 0.0f);
    // 系统关闭动画时直接到达终点。
    RequireNear(ClipSoul::PopupTimedMotionProgress(0.0, 160.0, false), 1.0f);
    RequireNear(ClipSoul::PopupTimedMotionProgress(10.0, 0.0, true), 1.0f);
}

TEST_CASE(PopupSpringStepSettlesOnTargetWithoutOvershootingForever) {
    ClipSoul::PopupSpringState state{0.0f, 0.0f};
    const float target = 58.0f;
    int steps = 0;
    float peak = 0.0f;
    while (steps < 240) {
        state = ClipSoul::PopupSpringStep(state.position, state.velocity, target, 1.0f / 60.0f);
        peak = std::max(peak, state.position);
        ++steps;
        if (std::abs(state.position - target) < 0.35f && std::abs(state.velocity) < 6.0f) {
            break;
        }
    }
    REQUIRE(steps < 120);
    REQUIRE(std::abs(state.position - target) < 1.0f);
    // 阻尼比 0.86：允许一次轻微回弹，但任何时刻都不越过目标的 12%。
    REQUIRE(peak <= target * 1.12f);
    REQUIRE(peak > 0.9f * target);
}

TEST_CASE(PopupSpringStepClampsOversizedFrameDelta) {
    // 卡顿帧（dt=200ms）按 32ms 上限推进，避免弹簧炸开。
    const auto clamped = ClipSoul::PopupSpringStep(0.0f, 0.0f, 100.0f, 0.2f);
    const auto capped = ClipSoul::PopupSpringStep(0.0f, 0.0f, 100.0f, 0.032f);
    RequireNear(clamped.position, capped.position, 0.001f);
    RequireNear(clamped.velocity, capped.velocity, 0.001f);
    REQUIRE(clamped.position < 100.0f);
    const auto frozen = ClipSoul::PopupSpringStep(12.0f, 3.0f, 100.0f, 0.0f);
    RequireNear(frozen.position, 12.0f);
    RequireNear(frozen.velocity, 3.0f);
}

// ---------- Ink Ledger：分区几何（计划 A.5） ----------

TEST_CASE(PopupSectionPlanSplitsPinnedAndRecent) {
    const auto empty = ClipSoul::PopupSectionPlanFor(0, 0);
    REQUIRE(!empty.show_pinned_header);
    REQUIRE(!empty.show_recent_header);
    REQUIRE_EQ(empty.pinned_count, static_cast<size_t>(0));

    const auto recent_only = ClipSoul::PopupSectionPlanFor(5, 0);
    REQUIRE(!recent_only.show_pinned_header);
    REQUIRE(!recent_only.show_recent_header);
    REQUIRE_EQ(recent_only.recent_count, static_cast<size_t>(5));

    const auto mixed = ClipSoul::PopupSectionPlanFor(6, 2);
    REQUIRE(mixed.show_pinned_header);
    REQUIRE(mixed.show_recent_header);
    REQUIRE_EQ(mixed.pinned_count, static_cast<size_t>(2));
    REQUIRE_EQ(mixed.recent_count, static_cast<size_t>(4));

    // 全是置顶项时只有"置顶"标题。
    const auto all_pinned = ClipSoul::PopupSectionPlanFor(3, 3);
    REQUIRE(all_pinned.show_pinned_header);
    REQUIRE(!all_pinned.show_recent_header);

    // 数据层给多了也不越界。
    const auto clamped = ClipSoul::PopupSectionPlanFor(2, 9);
    REQUIRE_EQ(clamped.pinned_count, static_cast<size_t>(2));
    REQUIRE_EQ(clamped.recent_count, static_cast<size_t>(0));
}

TEST_CASE(PopupSectionedItemTopAccountsForHeaders) {
    const auto plan = ClipSoul::PopupSectionPlanFor(5, 2);
    const float row = static_cast<float>(ClipSoul::PopupMetrics().card_height);
    const float header = ClipSoul::PopupSectionHeaderHeight();

    RequireNear(ClipSoul::PopupSectionedItemTop(0, plan, row, header), header);
    RequireNear(ClipSoul::PopupSectionedItemTop(1, plan, row, header), header + row);
    // 第二个分区：置顶标题 + 2 行 + "最近"标题
    RequireNear(ClipSoul::PopupSectionedItemTop(2, plan, row, header), header + 2.0f * row + header);
    RequireNear(ClipSoul::PopupSectionedItemTop(4, plan, row, header), header + 2.0f * row + header + 2.0f * row);
    RequireNear(ClipSoul::PopupSectionedListHeight(plan, row, header), 5.0f * row + 2.0f * header);

    const auto plain = ClipSoul::PopupSectionPlanFor(3, 0);
    RequireNear(ClipSoul::PopupSectionedItemTop(0, plain, row, header), 0.0f);
    RequireNear(ClipSoul::PopupSectionedItemTop(2, plain, row, header), 2.0f * row);
    RequireNear(ClipSoul::PopupSectionedListHeight(plain, row, header), 3.0f * row);
}

// ---------- Ink Ledger：时间预设（计划 B） ----------

TEST_CASE(PopupPresetDateRangeCoversAllTodayWeekMonth) {
    const ClipSoul::PopupCalendarDate today{2026, 5, 24};

    const auto all = ClipSoul::PopupPresetDateRangeFor(0, today);
    REQUIRE(!all.start.has_value());
    REQUIRE(!all.end.has_value());

    const auto today_range = ClipSoul::PopupPresetDateRangeFor(1, today);
    REQUIRE(today_range.start.has_value());
    REQUIRE(*today_range.start == today);
    REQUIRE(*today_range.end == today);

    const auto week = ClipSoul::PopupPresetDateRangeFor(2, today);
    REQUIRE((*week.start == ClipSoul::PopupCalendarDate{2026, 5, 18}));
    REQUIRE(*week.end == today);

    const auto month = ClipSoul::PopupPresetDateRangeFor(3, today);
    REQUIRE((*month.start == ClipSoul::PopupCalendarDate{2026, 4, 25}));
    REQUIRE(*month.end == today);

    // 自定义不改动区间
    const auto custom = ClipSoul::PopupPresetDateRangeFor(4, today);
    REQUIRE(!custom.start.has_value());
    REQUIRE(!custom.end.has_value());
}

TEST_CASE(PopupAddDaysCrossesMonthAndYearBoundaries) {
    using ClipSoul::PopupCalendarDate;
    REQUIRE((ClipSoul::PopupAddDays(PopupCalendarDate{2026, 3, 1}, -1) == PopupCalendarDate{2026, 2, 28}));
    REQUIRE((ClipSoul::PopupAddDays(PopupCalendarDate{2024, 3, 1}, -1) == PopupCalendarDate{2024, 2, 29}));
    REQUIRE((ClipSoul::PopupAddDays(PopupCalendarDate{2026, 1, 1}, -1) == PopupCalendarDate{2025, 12, 31}));
    REQUIRE((ClipSoul::PopupAddDays(PopupCalendarDate{2026, 12, 31}, 1) == PopupCalendarDate{2027, 1, 1}));
    REQUIRE((ClipSoul::PopupAddDays(PopupCalendarDate{2026, 5, 24}, -29) == PopupCalendarDate{2026, 4, 25}));
    REQUIRE((ClipSoul::PopupAddDays(PopupCalendarDate{2026, 5, 24}, 0) == PopupCalendarDate{2026, 5, 24}));
}

TEST_CASE(SettingsWindowFooterFitsInsideClientArea) {
    // 设置窗口高度必须容纳 行几何累计 + 底栏，否则"保存/取消"会被裁掉。
    const auto layout = ClipSoul::SettingsWindowProjectLayout();
    REQUIRE(layout.version_row.bottom <= 726.0f - 40.0f);
    REQUIRE(layout.section.bottom <= 726.0f);
    REQUIRE(layout.project_button.bottom < layout.divider.top);
}

TEST_CASE(PopupSectionedGeometryMatchesPaintRowOffsets) {
    // 绘制、命中、滚动共用同一套行偏移：标题高度必须被计入，且顺序单调递增。
    const auto plan = ClipSoul::PopupSectionPlanFor(4, 2);
    const float row = static_cast<float>(ClipSoul::PopupMetrics().card_height);
    const float header = ClipSoul::PopupSectionHeaderHeight();
    float previous = -1.0f;
    for (size_t index = 0; index < 4; ++index) {
        const float top = ClipSoul::PopupSectionedItemTop(index, plan, row, header);
        REQUIRE(top > previous);
        previous = top;
    }
    // 首行从"置顶"标题之下开始；第二分区跨过第二个标题。
    RequireNear(ClipSoul::PopupSectionedItemTop(0, plan, row, header), header);
    RequireNear(ClipSoul::PopupSectionedItemTop(2, plan, row, header), header * 2.0f + row * 2.0f);
    // 总高 = 行高之和 + 两个标题
    RequireNear(ClipSoul::PopupSectionedListHeight(plan, row, header), row * 4.0f + header * 2.0f);
}

TEST_CASE(PopupWindowEnterMatchesPreviewWinInKeyframes) {
    // 预览：.win.enter{animation:winIn 170ms cubic-bezier(.2,.8,.2,1) both}
    //       @keyframes winIn{from{opacity:0;transform:translateY(8px) scale(.985)}}
    const auto start = ClipSoul::PopupWindowEnterFrame(0.0f);
    REQUIRE(std::fabs(start.opacity) < 0.0001f);
    RequireNear(start.translate_y, 8.0f);
    RequireNear(start.scale, 0.985f);

    const auto end = ClipSoul::PopupWindowEnterFrame(1.0f);
    RequireNear(end.opacity, 1.0f);
    RequireNear(end.translate_y, 0.0f);
    RequireNear(end.scale, 1.0f);

    // 位移与缩放都由同一条缓动驱动：透明度越大，剩余上浮越小。
    float previous_offset = start.translate_y;
    float previous_opacity = start.opacity;
    for (int step = 1; step <= 20; ++step) {
        const auto frame = ClipSoul::PopupWindowEnterFrame(static_cast<float>(step) / 20.0f);
        REQUIRE(frame.translate_y <= previous_offset + 0.0001f);
        REQUIRE(frame.opacity >= previous_opacity - 0.0001f);
        RequireNear(frame.scale, 0.985f + 0.015f * frame.opacity);
        previous_offset = frame.translate_y;
        previous_opacity = frame.opacity;
    }

    // cubic-bezier(.2,.8,.2,1) 起步极快：1/6 处就已过半。
    REQUIRE(ClipSoul::PopupWindowEnterFrame(0.1667f).opacity > 0.55f);
}

TEST_CASE(PopupWindowLeaveMatchesPreviewWinOutKeyframes) {
    // 预览：.win.leave{animation:winOut 120ms cubic-bezier(.4,0,1,1) both}
    //       @keyframes winOut{to{opacity:0;transform:translateY(4px) scale(.99)}}
    const auto start = ClipSoul::PopupWindowLeaveFrame(0.0f);
    RequireNear(start.opacity, 1.0f);
    RequireNear(start.translate_y, 0.0f);
    RequireNear(start.scale, 1.0f);

    const auto end = ClipSoul::PopupWindowLeaveFrame(1.0f);
    REQUIRE(std::fabs(end.opacity) < 0.0001f);
    RequireNear(end.translate_y, 4.0f);
    RequireNear(end.scale, 0.99f);

    float previous_opacity = start.opacity;
    for (int step = 1; step <= 20; ++step) {
        const auto frame = ClipSoul::PopupWindowLeaveFrame(static_cast<float>(step) / 20.0f);
        REQUIRE(frame.opacity <= previous_opacity + 0.0001f);
        RequireNear(frame.translate_y, 4.0f * (1.0f - frame.opacity));
        RequireNear(frame.scale, 1.0f - 0.01f * (1.0f - frame.opacity));
        previous_opacity = frame.opacity;
    }
}

TEST_CASE(PopupRowEntranceMatchesPreviewRowRiseStagger) {
    // 预览：animation:rowRise 220ms cubic-bezier(.2,.8,.2,1) both;
    //       animation-delay:calc(var(--i)*18ms + 40ms)
    //       @keyframes rowRise{from{opacity:0;transform:translateY(5px)}}
    // 延迟期间必须保持 from 状态（both 的 backwards fill）。
    for (int index = 0; index <= 9; ++index) {
        const double delay = 40.0 + index * 18.0;
        const auto before = ClipSoul::PopupRowEntranceFrameAt(delay - 1.0, index);
        REQUIRE(std::fabs(before.opacity) < 0.0001f);
        RequireNear(before.translate_y, 5.0f);

        const auto done = ClipSoul::PopupRowEntranceFrameAt(delay + 220.0, index);
        RequireNear(done.opacity, 1.0f);
        RequireNear(done.translate_y, 0.0f);
    }

    // 同一时刻，索引越靠后的行进度越少。
    const double sample_ms = 150.0;
    float previous = 1.1f;
    for (int index = 0; index <= 9; ++index) {
        const auto frame = ClipSoul::PopupRowEntranceFrameAt(sample_ms, index);
        REQUIRE(frame.opacity <= previous + 0.0001f);
        previous = frame.opacity;
    }

    // 索引被夹在 [0,9]：第 10 行与第 9 行同步。
    const auto clamped_low = ClipSoul::PopupRowEntranceFrameAt(sample_ms, -3);
    const auto first = ClipSoul::PopupRowEntranceFrameAt(sample_ms, 0);
    RequireNear(clamped_low.opacity, first.opacity);
    const auto clamped_high = ClipSoul::PopupRowEntranceFrameAt(sample_ms, 40);
    const auto ninth = ClipSoul::PopupRowEntranceFrameAt(sample_ms, 9);
    RequireNear(clamped_high.opacity, ninth.opacity);

    // 总时长要覆盖最后一档（40 + 9*18 + 220）。
    RequireNear(static_cast<float>(ClipSoul::PopupRowEntranceTotalMs()), 422.0f);
}

TEST_CASE(PopupSearchUnderlineFocusProgressStartsAtZeroOnShow) {
    // 呼出时搜索墨线要从左端开始展开（预览 .search::after 的 scaleX 0→1，200ms）。
    // 起点必须严格为 0，终点为 1，中间按三次方缓出推进。
    REQUIRE_EQ(ClipSoul::PopupMotionProgress(0.0f), 0.0f);
    REQUIRE_EQ(ClipSoul::PopupMotionProgress(1.0f), 1.0f);
    // 未聚焦且未悬停时不该有任何进度（原来写成 !x > 0，实际等价于 1 > 0，等于没断言）。
    REQUIRE_EQ(ClipSoul::PopupSearchFocusProgress(false, false, 0.0f), 0.0f);
    REQUIRE(ClipSoul::PopupSearchFocusProgress(true, false, 0.0f) == 1.0f);
}

TEST_CASE(PopupSmoothScrollToTopEasesInsteadOfJumping) {
    // 预览：list.scrollTo({top:0, behavior:'smooth'})，不是瞬跳。
    const float from = 640.0f;
    const float to = 0.0f;

    // 起点就在原位，终点精确归零。
    RequireNear(ClipSoul::PopupSmoothScrollOffset(from, to, 0.0f), from);
    RequireNear(ClipSoul::PopupSmoothScrollOffset(from, to, 1.0f), to);

    // 单调向上，且不会越过目标（后半段逐帧靠拢）。
    float previous = from;
    for (int step = 1; step <= 30; ++step) {
        const float offset = ClipSoul::PopupSmoothScrollOffset(from, to, static_cast<float>(step) / 30.0f);
        REQUIRE(offset <= previous + 0.0001f);
        REQUIRE(offset >= to - 0.0001f);
        previous = offset;
    }

    // ease-in-out：中途才是最快的那一段，起点很慢（与瞬跳区分开）。
    const float at_5pct = ClipSoul::PopupSmoothScrollOffset(from, to, 0.05f);
    REQUIRE(at_5pct > from * 0.9f);
    const float at_half = ClipSoul::PopupSmoothScrollOffset(from, to, 0.5f);
    RequireNear(at_half, from * 0.5f);
    RequireNear(ClipSoul::PopupSmoothScrollProgress(0.5f), 0.5f);

    // 参数被夹住，越界不会外插。
    RequireNear(ClipSoul::PopupSmoothScrollOffset(from, to, -1.0f), from);
    RequireNear(ClipSoul::PopupSmoothScrollOffset(from, to, 2.0f), to);
}

TEST_CASE(PopupTextInputRectAcceptsMultilineChatInput) {
    // 微信 mmui::ChatInputField 实测 670x159，老的 120px 上限会把它判定成「不是输入框」，
    // 于是所有插入符路径都失败、弹窗只能退回屏幕右下角。
    REQUIRE(ClipSoul::PopupTextInputRectUsable(RECT{463, 694, 1133, 853}));
    REQUIRE(ClipSoul::PopupTextInputRectUsable(RECT{694, 1041, 1699, 1279}));
    // 整块屏幕或整窗口仍然要挡住。
    REQUIRE(!ClipSoul::PopupTextInputRectUsable(RECT{6, 120, 2555, 1593}));

    // 高输入框的锚点应落在第一行文字附近，而不是框顶或框中心。
    const auto anchor = ClipSoul::PopupTextInputRectAnchor(RECT{463, 694, 1133, 853}, 144);
    REQUIRE_EQ(anchor.y, 694 + 15);
    REQUIRE(anchor.y > 694);
    REQUIRE(anchor.y < 853);
}

TEST_CASE(PopupTextAvoidRectPositionPrefersAboveForTallInputField) {
    // 聊天输入框（1005x238）上方空间充足时必须贴上方，不能被 clamp 到屏幕中间。
    const SIZE size{510, 840};
    const RECT work{0, 0, 2560, 1528};
    const RECT input{694, 1041, 1699, 1279};

    const auto position = ClipSoul::PopupTextAvoidRectPosition(input, size, work, 144);
    // 弹窗底边贴在「输入框第一行文字」上方，留 8dip（144dpi => 12px）间距。
    const int first_line_y = 1041 + 15; // rect.top + 10dip
    REQUIRE_EQ(position.y + size.cy, first_line_y - 12);
    REQUIRE(position.y >= work.top);
    // 弹窗整体不压在输入框主体上。
    REQUIRE(position.y + size.cy <= input.bottom);
    // 水平方向贴着输入框左侧文字处，不会跑到右半屏。
    REQUIRE(position.x < input.right);
}

TEST_CASE(PopupContinuousPasteStepsMatchTheHighlightedRow) {
    // 连续粘贴的核心约束：本次粘贴的项 == 当前高亮项，下一次高亮 == 顺序下一条。
    // 之前 AdvanceSelectionAfterContinuousPaste 因窗口隐藏提前 return，导致
    // 每次都重复粘贴同一条、高亮也不再移动。
    const int item_count = 5;
    int selected = 1; // 置顶 1 条时的默认选中：最近分区第一条

    const std::vector<int> expected_paste{1, 2, 3, 4, 0, 1};
    for (size_t step = 0; step < expected_paste.size(); ++step) {
        const auto advance = ClipSoul::PopupContinuousPasteSelectionStep(item_count, selected);
        REQUIRE_EQ(advance.paste_index, expected_paste[step]);
        // 粘贴项必须就是当前高亮项。
        REQUIRE_EQ(advance.paste_index, selected);
        selected = advance.next_selected_index;
        // 每次都要真的往前走一步（不能原地踏步）。
        REQUIRE(selected != advance.paste_index);
    }
}

TEST_CASE(PopupDefaultSelectionAndPasteAgree) {
    // 呼出弹窗时的默认高亮，必须与「按一次连续粘贴会粘哪条」完全一致。
    using ClipSoul::PopupSectionPlan;
    PopupSectionPlan plan;
    plan.pinned_count = 1;
    plan.recent_count = 4;
    plan.show_pinned_header = true;
    plan.show_recent_header = true;

    const int default_index = ClipSoul::PopupDefaultSelectedIndex(plan);
    REQUIRE_EQ(default_index, 1); // 跳过置顶区

    const auto first_paste = ClipSoul::PopupContinuousPasteSelectionStep(1 + 4, default_index);
    REQUIRE_EQ(first_paste.paste_index, default_index);
}

TEST_CASE(PopupHidesWhenForegroundLeavesBaseline) {
    // 弹窗是 WS_EX_NOACTIVATE，自身永远不会成为前台：只能拿"呼出瞬间的前台窗口"作基准，
    // 一旦前台换成别的窗口就收起。这条判据对提权窗口（任务管理器）同样成立，
    // 不依赖鼠标状态或低级输入钩子。
    const auto should_hide = [](bool pinned, bool prompt, bool moving, bool visible, bool suppressed,
                                std::uintptr_t baseline, std::uintptr_t foreground) {
        return ClipSoul::PopupShouldHideWhenForegroundChanges(
            pinned, prompt, moving, visible, suppressed, reinterpret_cast<HWND>(0x9999),
            reinterpret_cast<HWND>(baseline), reinterpret_cast<HWND>(foreground));
    };

    // 正常情况：前台从微信换成任务管理器 -> 收起。
    REQUIRE(should_hide(false, false, false, true, false, 0x1111, 0x2222));

    // 前台没变 -> 不收起（用户还在原窗口里打字）。
    REQUIRE(!should_hide(false, false, false, true, false, 0x1111, 0x1111));

    // 固定窗口（pinned）时不自动收起。
    REQUIRE(!should_hide(true, false, false, true, false, 0x1111, 0x2222));

    // 有对话框、正在拖动/缩放窗口时不收起。
    REQUIRE(!should_hide(false, true, false, true, false, 0x1111, 0x2222));
    REQUIRE(!should_hide(false, false, true, true, false, 0x1111, 0x2222));

    // 粘贴后的短暂抑制期内不收起。
    REQUIRE(!should_hide(false, false, false, true, true, 0x1111, 0x2222));

    // 弹窗不可见、没有基准前台、前台为空（切换桌面瞬间）都不收起。
    REQUIRE(!should_hide(false, false, false, false, false, 0x1111, 0x2222));
    REQUIRE(!should_hide(false, false, false, true, false, 0, 0x2222));
    REQUIRE(!should_hide(false, false, false, true, false, 0x1111, 0));
}

TEST_CASE(PopupFallsBackToScreenBottomRight) {
    // 拿不到文字插入符时（Unity 自绘 UI 的游戏不向 UIA 暴露任何输入框），
    // 落点固定是屏幕右下角，不跟随鼠标、也不贴目标窗口边缘。
    const SIZE size{510, 840};
    const RECT monitor{0, 0, 2560, 1600};
    const RECT work{0, 0, 2560, 1600};

    const auto position = ClipSoul::PopupKeyboardInvocationPosition(false, RECT{}, size, monitor, work, 96);
    // 靠右、靠下（贴住工作区右下角，留一点边距），且完整落在工作区内。
    REQUIRE(position.x + size.cx <= work.right);
    REQUIRE(position.y + size.cy <= work.bottom);
    REQUIRE(position.x > work.right / 2);
    // 底边距工作区底部不超过 100px：确实是「右下角」而不是屏幕中部。
    REQUIRE(work.bottom - (position.y + size.cy) <= 100);

    // 有插入符时必须跟随插入符（放在输入框上方），而不是走右下角。
    const RECT input{499, 861, 1274, 892};
    const auto with_caret = ClipSoul::PopupKeyboardInvocationPosition(true, input, size, monitor, work, 96);
    REQUIRE(with_caret.x < work.right / 2);
    // 弹窗底边贴住输入框顶部上方。
    REQUIRE(with_caret.y + size.cy <= input.bottom);
}
