#include "TestHarness.h"

#include "ClipSoul/Hotkey.h"

#include <Windows.h>

TEST_CASE(HotkeyCaptureAcceptsTwoKeyAltCombination) {
    const auto hotkey = ClipSoul::BuildCapturedHotkey('C', false, true, false, false);

    REQUIRE(hotkey.has_value());
    REQUIRE_EQ(hotkey->modifiers, static_cast<unsigned>(MOD_ALT));
    REQUIRE_EQ(hotkey->vk, static_cast<unsigned>('C'));
}

TEST_CASE(HotkeyCaptureAcceptsThreeKeyCombination) {
    const auto hotkey = ClipSoul::BuildCapturedHotkey('V', true, true, false, false);

    REQUIRE(hotkey.has_value());
    REQUIRE_EQ(hotkey->modifiers, static_cast<unsigned>(MOD_CONTROL | MOD_ALT));
    REQUIRE_EQ(hotkey->vk, static_cast<unsigned>('V'));
}

TEST_CASE(HotkeyCaptureIgnoresModifierOnlyKeyPresses) {
    REQUIRE(!ClipSoul::BuildCapturedHotkey(VK_MENU, false, true, false, false).has_value());
}

TEST_CASE(HotkeyRegistrationAddsNoRepeatAndDetectsAltModifier) {
    REQUIRE_EQ(ClipSoul::RegisteredHotkeyModifiers(MOD_ALT),
               static_cast<unsigned>(MOD_ALT | MOD_NOREPEAT));
    REQUIRE_EQ(ClipSoul::RegisteredHotkeyModifiers(MOD_CONTROL | MOD_SHIFT),
               static_cast<unsigned>(MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT));
    REQUIRE(ClipSoul::HotkeyUsesAlt(MOD_ALT));
    REQUIRE(ClipSoul::HotkeyUsesAlt(MOD_CONTROL | MOD_ALT));
    REQUIRE(!ClipSoul::HotkeyUsesAlt(MOD_CONTROL | MOD_SHIFT));
}

TEST_CASE(HotkeyMatchesExactStateWithoutExtraModifiers) {
    REQUIRE(ClipSoul::HotkeyMatchesState(MOD_ALT, 'C', false, true, false, false, 'C'));
    REQUIRE(ClipSoul::HotkeyMatchesState(MOD_CONTROL | MOD_ALT, 'V', true, true, false, false, 'V'));
    REQUIRE(!ClipSoul::HotkeyMatchesState(MOD_ALT, 'C', true, true, false, false, 'C'));
    REQUIRE(!ClipSoul::HotkeyMatchesState(MOD_ALT, 'C', false, true, true, false, 'C'));
    REQUIRE(!ClipSoul::HotkeyMatchesState(MOD_ALT, 'C', false, true, false, false, 'V'));
}

TEST_CASE(HotkeyClassifiesBareAltChords) {
    REQUIRE(ClipSoul::HotkeyUsesOnlyAlt(MOD_ALT));
    REQUIRE(!ClipSoul::HotkeyUsesOnlyAlt(MOD_CONTROL | MOD_ALT));
    REQUIRE(!ClipSoul::HotkeyUsesOnlyAlt(MOD_CONTROL | MOD_SHIFT));

    REQUIRE(ClipSoul::HotkeyIsAltKey(VK_MENU));
    REQUIRE(ClipSoul::HotkeyIsAltKey(VK_LMENU));
    REQUIRE(ClipSoul::HotkeyIsAltKey(VK_RMENU));
    REQUIRE(!ClipSoul::HotkeyIsAltKey(VK_CONTROL));

    REQUIRE(ClipSoul::HotkeyShouldBufferBareAlt(MOD_ALT, MOD_CONTROL | MOD_ALT, false, false, false));
    REQUIRE(!ClipSoul::HotkeyShouldBufferBareAlt(MOD_CONTROL | MOD_ALT, MOD_CONTROL | MOD_ALT, false, false,
                                                 false));
    REQUIRE(!ClipSoul::HotkeyShouldBufferBareAlt(MOD_ALT, MOD_CONTROL | MOD_ALT, true, false, false));
    REQUIRE(!ClipSoul::HotkeyShouldBufferBareAlt(MOD_ALT, MOD_CONTROL | MOD_ALT, false, true, false));
    REQUIRE(!ClipSoul::HotkeyShouldBufferBareAlt(MOD_ALT, MOD_CONTROL | MOD_ALT, false, false, true));

    REQUIRE(ClipSoul::HotkeyShouldBufferAltDown(MOD_ALT, MOD_CONTROL | MOD_ALT, false, false, false));
    REQUIRE(ClipSoul::HotkeyShouldBufferAltDown(MOD_CONTROL | MOD_ALT, MOD_CONTROL | MOD_ALT, false,
                                                false, false));
    REQUIRE(ClipSoul::HotkeyShouldBufferAltDown(MOD_CONTROL | MOD_ALT, MOD_ALT, true, false, false));
    REQUIRE(ClipSoul::HotkeyShouldBufferAltDown(MOD_CONTROL | MOD_ALT | MOD_SHIFT, MOD_ALT, true, true,
                                                false));
    REQUIRE(!ClipSoul::HotkeyShouldBufferAltDown(MOD_CONTROL | MOD_ALT, MOD_ALT, false, true, false));
    REQUIRE(!ClipSoul::HotkeyShouldBufferAltDown(MOD_CONTROL | MOD_SHIFT, MOD_CONTROL | MOD_SHIFT, true,
                                                 false, false));

    REQUIRE(ClipSoul::HotkeyShouldKeepBufferedAltForModifier(MOD_CONTROL | MOD_ALT, MOD_ALT, true, false,
                                                             false, VK_CONTROL));
    REQUIRE(ClipSoul::HotkeyShouldKeepBufferedAltForModifier(MOD_CONTROL | MOD_ALT, MOD_ALT, true, false,
                                                             false, VK_LCONTROL));
    REQUIRE(ClipSoul::HotkeyShouldKeepBufferedAltForModifier(MOD_CONTROL | MOD_ALT, MOD_ALT, true, false,
                                                             false, VK_RCONTROL));
    REQUIRE(ClipSoul::HotkeyShouldKeepBufferedAltForModifier(MOD_CONTROL | MOD_ALT | MOD_SHIFT, MOD_ALT,
                                                             true, true, false, VK_SHIFT));
    REQUIRE(ClipSoul::HotkeyShouldKeepBufferedAltForModifier(MOD_CONTROL | MOD_ALT | MOD_SHIFT, MOD_ALT,
                                                             true, true, false, VK_LSHIFT));
    REQUIRE(!ClipSoul::HotkeyShouldKeepBufferedAltForModifier(MOD_CONTROL | MOD_ALT, MOD_ALT, true, false,
                                                              false, 'V'));
    REQUIRE(!ClipSoul::HotkeyShouldKeepBufferedAltForModifier(MOD_ALT, MOD_ALT, true, false, false,
                                                              VK_CONTROL));
    REQUIRE(!ClipSoul::HotkeyShouldKeepBufferedAltForModifier(MOD_CONTROL | MOD_SHIFT, MOD_CONTROL | MOD_SHIFT,
                                                              true, false, false, VK_CONTROL));

    REQUIRE(ClipSoul::HotkeyShouldReplayBufferedAlt(false, false, 'X'));
    REQUIRE(!ClipSoul::HotkeyShouldReplayBufferedAlt(true, false, 'X'));
    REQUIRE(!ClipSoul::HotkeyShouldReplayBufferedAlt(false, true, VK_MENU));
    REQUIRE(!ClipSoul::HotkeyShouldReplayBufferedAlt(true, false, 'C'));

    REQUIRE(ClipSoul::HotkeyShouldSwallowAltReleaseAfterHandledHotkey(true, VK_MENU));
    REQUIRE(ClipSoul::HotkeyShouldSwallowAltReleaseAfterHandledHotkey(true, VK_LMENU));
    REQUIRE(!ClipSoul::HotkeyShouldSwallowAltReleaseAfterHandledHotkey(false, VK_MENU));
    REQUIRE(!ClipSoul::HotkeyShouldSwallowAltReleaseAfterHandledHotkey(true, 'C'));

    REQUIRE(ClipSoul::HotkeyHookShouldIgnoreInjectedEvent(true, true, VK_MENU));
    REQUIRE(!ClipSoul::HotkeyHookShouldIgnoreInjectedEvent(true, false, VK_MENU));
    REQUIRE(!ClipSoul::HotkeyHookShouldIgnoreInjectedEvent(true, true, 'C'));
    REQUIRE(!ClipSoul::HotkeyHookShouldIgnoreInjectedEvent(false, true, VK_MENU));

    REQUIRE(!ClipSoul::HotkeyShouldRegisterSystemHotkeys(true));
    REQUIRE(ClipSoul::HotkeyShouldRegisterSystemHotkeys(false));
    REQUIRE(ClipSoul::HotkeyHookShouldSuppressRegisteredHotkeyEcho(true));
    REQUIRE(!ClipSoul::HotkeyHookShouldSuppressRegisteredHotkeyEcho(false));

    REQUIRE(ClipSoul::HotkeyOpenPopupShouldHandleKey(VK_ESCAPE));
    REQUIRE(ClipSoul::HotkeyOpenPopupShouldHandleKey(VK_RETURN));
    REQUIRE(ClipSoul::HotkeyOpenPopupShouldHandleKey(VK_UP));
    REQUIRE(ClipSoul::HotkeyOpenPopupShouldHandleKey(VK_DOWN));
    REQUIRE(!ClipSoul::HotkeyOpenPopupShouldHandleKey('A'));
    REQUIRE(!ClipSoul::HotkeyOpenPopupShouldHandleKey(VK_MENU));

    REQUIRE(ClipSoul::HotkeyOpenPopupShouldToggle(true, true));
    REQUIRE(!ClipSoul::HotkeyOpenPopupShouldToggle(true, false));
    REQUIRE(!ClipSoul::HotkeyOpenPopupShouldToggle(false, true));

    REQUIRE(ClipSoul::HotkeyShouldTrackHandledKeyUp(true, 'C'));
    REQUIRE(!ClipSoul::HotkeyShouldTrackHandledKeyUp(false, 'C'));
    REQUIRE(!ClipSoul::HotkeyShouldTrackHandledKeyUp(true, 0));
    REQUIRE(ClipSoul::HotkeyShouldSuppressRepeatedKeyDown(true, 'C', 'C'));
    REQUIRE(!ClipSoul::HotkeyShouldSuppressRepeatedKeyDown(false, 'C', 'C'));
    REQUIRE(!ClipSoul::HotkeyShouldSuppressRepeatedKeyDown(true, 'C', 'V'));

    const auto captured = reinterpret_cast<HWND>(0x1234);
    const auto fallback = reinterpret_cast<HWND>(0x5678);
    REQUIRE_EQ(ClipSoul::HotkeyMessageTarget(captured, fallback), captured);
    REQUIRE_EQ(ClipSoul::HotkeyMessageTarget(nullptr, fallback), fallback);
}
