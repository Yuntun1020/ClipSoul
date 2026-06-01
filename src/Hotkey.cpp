#include "ClipSoul/Hotkey.h"

#include <Windows.h>

namespace ClipSoul {

std::optional<CapturedHotkey> BuildCapturedHotkey(unsigned vk, bool ctrl_down, bool alt_down,
                                                  bool shift_down, bool win_down) {
    if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN) {
        return std::nullopt;
    }

    unsigned modifiers = 0;
    if (ctrl_down) modifiers |= MOD_CONTROL;
    if (alt_down) modifiers |= MOD_ALT;
    if (shift_down) modifiers |= MOD_SHIFT;
    if (win_down) modifiers |= MOD_WIN;
    if (modifiers == 0) {
        modifiers = MOD_ALT;
    }

    return CapturedHotkey{modifiers, vk};
}

unsigned RegisteredHotkeyModifiers(unsigned modifiers) {
    return modifiers | MOD_NOREPEAT;
}

bool HotkeyUsesAlt(unsigned modifiers) {
    return (modifiers & MOD_ALT) != 0;
}

bool HotkeyUsesOnlyAlt(unsigned modifiers) {
    constexpr unsigned kModifierMask = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN;
    return (modifiers & kModifierMask) == MOD_ALT;
}

bool HotkeyIsAltKey(unsigned vk) {
    return vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU;
}

bool HotkeyShouldBufferBareAlt(unsigned popup_modifiers, unsigned continuous_modifiers, bool ctrl_down,
                               bool shift_down, bool win_down) {
    return (HotkeyUsesOnlyAlt(popup_modifiers) || HotkeyUsesOnlyAlt(continuous_modifiers)) &&
           !ctrl_down && !shift_down && !win_down;
}

bool HotkeyShouldBufferAltDown(unsigned popup_modifiers, unsigned continuous_modifiers, bool ctrl_down,
                               bool shift_down, bool win_down) {
    const auto matches_prefix = [&](unsigned modifiers) {
        constexpr unsigned kModifierMask = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN;
        const unsigned configured = modifiers & kModifierMask;
        if ((configured & MOD_ALT) == 0) {
            return false;
        }
        if ((configured & MOD_CONTROL) == 0 && ctrl_down) {
            return false;
        }
        if ((configured & MOD_SHIFT) == 0 && shift_down) {
            return false;
        }
        if ((configured & MOD_WIN) == 0 && win_down) {
            return false;
        }
        return true;
    };
    return matches_prefix(popup_modifiers) || matches_prefix(continuous_modifiers);
}

bool HotkeyShouldKeepBufferedAltForModifier(unsigned popup_modifiers, unsigned continuous_modifiers, bool ctrl_down,
                                            bool shift_down, bool win_down, unsigned vk) {
    unsigned modifier = 0;
    if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) {
        modifier = MOD_CONTROL;
    } else if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) {
        modifier = MOD_SHIFT;
    } else if (vk == VK_LWIN || vk == VK_RWIN) {
        modifier = MOD_WIN;
    } else {
        return false;
    }

    const auto matches_prefix = [&](unsigned modifiers) {
        constexpr unsigned kModifierMask = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN;
        const unsigned configured = modifiers & kModifierMask;
        return (configured & MOD_ALT) != 0 &&
               (configured & modifier) != 0 &&
               ((configured & MOD_CONTROL) != 0 || !ctrl_down) &&
               ((configured & MOD_SHIFT) != 0 || !shift_down) &&
               ((configured & MOD_WIN) != 0 || !win_down);
    };
    return matches_prefix(popup_modifiers) || matches_prefix(continuous_modifiers);
}

bool HotkeyShouldReplayBufferedAlt(bool matched_hotkey, bool key_is_alt, unsigned) {
    return !matched_hotkey && !key_is_alt;
}

bool HotkeyShouldSwallowAltReleaseAfterHandledHotkey(bool handled_bare_alt_hotkey, unsigned vk) {
    return handled_bare_alt_hotkey && HotkeyIsAltKey(vk);
}

bool HotkeyHookShouldIgnoreInjectedEvent(bool injected, bool replayed_alt_down, unsigned vk) {
    return injected && replayed_alt_down && HotkeyIsAltKey(vk);
}

bool HotkeyShouldRegisterSystemHotkeys(bool keyboard_hook_installed) {
    return !keyboard_hook_installed;
}

bool HotkeyHookShouldSuppressRegisteredHotkeyEcho(bool handled_by_hook) {
    return handled_by_hook;
}

bool HotkeyMatchesState(unsigned configured_modifiers, unsigned configured_vk, bool ctrl_down, bool alt_down,
                        bool shift_down, bool win_down, unsigned vk) {
    constexpr unsigned kModifierMask = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN;
    unsigned actual_modifiers = 0;
    if (ctrl_down) actual_modifiers |= MOD_CONTROL;
    if (alt_down) actual_modifiers |= MOD_ALT;
    if (shift_down) actual_modifiers |= MOD_SHIFT;
    if (win_down) actual_modifiers |= MOD_WIN;

    return vk == configured_vk && actual_modifiers == (configured_modifiers & kModifierMask);
}

bool HotkeyOpenPopupShouldHandleKey(unsigned vk) {
    return vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_UP || vk == VK_DOWN;
}

bool HotkeyOpenPopupShouldToggle(bool popup_visible, bool matched_popup_hotkey) {
    return popup_visible && matched_popup_hotkey;
}

bool HotkeyShouldTrackHandledKeyUp(bool handled_hotkey, unsigned vk) {
    return handled_hotkey && vk != 0;
}

bool HotkeyShouldSuppressRepeatedKeyDown(bool handled_key_down, unsigned handled_vk, unsigned vk) {
    return handled_key_down && handled_vk == vk;
}

HWND HotkeyMessageTarget(HWND captured_foreground, HWND fallback_target) {
    return captured_foreground ? captured_foreground : fallback_target;
}

} // namespace ClipSoul
