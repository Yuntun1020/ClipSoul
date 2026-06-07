#pragma once

#include <Windows.h>

#include <optional>

namespace ClipSoul {

struct CapturedHotkey {
    unsigned modifiers = 0;
    unsigned vk = 0;
};

enum class OpenPopupHotkeyAction {
    None,
    TogglePopup,
    ContinuousPaste,
    ForwardKey,
};

enum class AltReleaseAction {
    PassThrough,
    Swallow,
    SwallowAndClearBuffered,
    ReplayBufferedAltUp,
};

std::optional<CapturedHotkey> BuildCapturedHotkey(unsigned vk, bool ctrl_down, bool alt_down,
                                                  bool shift_down, bool win_down);
unsigned RegisteredHotkeyModifiers(unsigned modifiers);
bool HotkeyUsesAlt(unsigned modifiers);
bool HotkeyUsesOnlyAlt(unsigned modifiers);
bool HotkeyIsAltKey(unsigned vk);
bool HotkeyShouldBufferBareAlt(unsigned popup_modifiers, unsigned continuous_modifiers, bool ctrl_down,
                               bool shift_down, bool win_down);
bool HotkeyShouldBufferAltDown(unsigned popup_modifiers, unsigned continuous_modifiers, bool ctrl_down,
                               bool shift_down, bool win_down);
bool HotkeyShouldKeepBufferedAltForModifier(unsigned popup_modifiers, unsigned continuous_modifiers, bool ctrl_down,
                                            bool shift_down, bool win_down, unsigned vk);
bool HotkeyShouldKeepBufferedAltAfterHandledHotkey(unsigned handled_modifiers, bool ctrl_down, bool shift_down,
                                                   bool win_down);
bool HotkeyShouldReplayBufferedAlt(bool matched_hotkey, bool key_is_alt, unsigned vk);
bool HotkeyShouldSwallowAltReleaseAfterHandledHotkey(bool handled_bare_alt_hotkey, unsigned vk);
AltReleaseAction HotkeyAltReleaseActionFor(bool swallow_alt_release, bool buffered_alt_down,
                                           bool replayed_alt_down, unsigned vk);
inline constexpr ULONG_PTR kClipSoulInjectedInputExtraInfo = static_cast<ULONG_PTR>(0x43534F55);
bool HotkeyHookShouldIgnoreInjectedEvent(bool injected, ULONG_PTR extra_info);
bool HotkeyShouldRegisterSystemHotkeys(bool keyboard_hook_installed);
bool HotkeyHookShouldSuppressRegisteredHotkeyEcho(bool handled_by_hook);
bool HotkeyMatchesState(unsigned configured_modifiers, unsigned configured_vk, bool ctrl_down, bool alt_down,
                        bool shift_down, bool win_down, unsigned vk);
bool HotkeyOpenPopupShouldHandleKey(unsigned vk);
bool HotkeyOpenPopupShouldToggle(bool popup_visible, bool matched_popup_hotkey);
bool HotkeyOpenPopupShouldHandleContinuousPaste(bool popup_visible, bool matched_continuous_hotkey);
OpenPopupHotkeyAction HotkeyOpenPopupActionFor(bool popup_visible, bool matched_popup_hotkey,
                                               bool matched_continuous_hotkey, unsigned vk);
bool HotkeyShouldTrackHandledKeyUp(bool handled_hotkey, unsigned vk);
bool HotkeyShouldSuppressRepeatedKeyDown(bool handled_key_down, unsigned handled_vk, unsigned vk);
HWND HotkeyMessageTarget(HWND captured_foreground, HWND fallback_target);

} // namespace ClipSoul
