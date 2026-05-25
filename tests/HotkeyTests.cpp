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
