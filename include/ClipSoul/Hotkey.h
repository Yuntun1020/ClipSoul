#pragma once

#include <optional>

namespace ClipSoul {

struct CapturedHotkey {
    unsigned modifiers = 0;
    unsigned vk = 0;
};

std::optional<CapturedHotkey> BuildCapturedHotkey(unsigned vk, bool ctrl_down, bool alt_down,
                                                  bool shift_down, bool win_down);

} // namespace ClipSoul
