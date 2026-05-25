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

} // namespace ClipSoul
