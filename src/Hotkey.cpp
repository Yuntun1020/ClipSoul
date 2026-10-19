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
    // 一律不缓冲 Alt 的「按下」。
    //
    // 这里原本只判断「热键配置里是不是只有 Alt」，于是任何一次独立按 Alt 都会被钩子吞掉、
    // 记进 buffered_alt_down_，等着下一个键来判断是不是 Alt+C。热键匹配不上时（游戏里
    // 按 Alt 开技能、Alt+其它快捷键）那次按下已经被吞了，目标程序再也收不到 ——
    // 表现为「游戏中 Alt 完全失效、只有 Alt+C 还能用」。
    //
    // 现在系统热键（RegisterHotKey）始终注册着，Alt+C 由系统层直接匹配，钩子只是补充路径，
    // 不再需要「先把 Alt 按住缓冲起来」。所以 Alt 的按下直接放行给目标程序，
    // 真正匹配到热键时再连 Alt+C 一起吞（见 HandleKeyboardHook 里的 matched_hotkey 分支）。
    (void)popup_modifiers;
    (void)continuous_modifiers;
    (void)ctrl_down;
    (void)shift_down;
    (void)win_down;
    return false;
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

bool HotkeyShouldKeepBufferedAltAfterHandledHotkey(unsigned handled_modifiers, bool ctrl_down, bool shift_down,
                                                   bool win_down) {
    constexpr unsigned kModifierMask = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN;
    const unsigned configured = handled_modifiers & kModifierMask;
    if ((configured & MOD_ALT) == 0 || HotkeyUsesOnlyAlt(configured)) {
        return false;
    }
    if ((configured & MOD_CONTROL) != 0 && ctrl_down) {
        return true;
    }
    if ((configured & MOD_SHIFT) != 0 && shift_down) {
        return true;
    }
    if ((configured & MOD_WIN) != 0 && win_down) {
        return true;
    }
    return false;
}

bool HotkeyShouldReplayBufferedAlt(bool matched_hotkey, bool key_is_alt, unsigned) {
    return !matched_hotkey && !key_is_alt;
}

bool HotkeyShouldSwallowAltReleaseAfterHandledHotkey(bool handled_bare_alt_hotkey, unsigned vk) {
    return handled_bare_alt_hotkey && HotkeyIsAltKey(vk);
}

AltReleaseAction HotkeyAltReleaseActionFor(bool swallow_alt_release, bool buffered_alt_down,
                                           bool replayed_alt_down, unsigned vk) {
    if (!HotkeyIsAltKey(vk)) {
        return AltReleaseAction::PassThrough;
    }
    // 只要是「被我们吞掉过按下」的 Alt，它的抬起就必须丢掉：放行的话目标程序会收到一次
    // 没有配对按下的 Alt 抬起，反而把修饰键状态搞乱。两个缓冲标志无条件清掉，
    // 否则下一次按键状态会错位。
    if (swallow_alt_release || buffered_alt_down) {
        return AltReleaseAction::SwallowAndClearAll;
    }
    // replayed_alt_down 不再参与判定：补发的 Alt 已经是「按下+抬起」成对发出的，
    // 不存在需要等待配对的状态。
    (void)replayed_alt_down;
    return AltReleaseAction::PassThrough;
}

bool HotkeyHookShouldIgnoreInjectedEvent(bool injected, ULONG_PTR extra_info) {
    return injected && extra_info == kClipSoulInjectedInputExtraInfo;
}

bool HotkeyHookShouldBypassSettingsForeground(HWND foreground, HWND settings_window) {
    return settings_window && foreground == settings_window;
}

bool HotkeyHookShouldBypassSettingsWindow(HWND foreground, HWND settings_window, bool settings_window_visible) {
    if (!settings_window) {
        return false;
    }
    return settings_window_visible && HotkeyHookShouldBypassSettingsForeground(foreground, settings_window);
}

bool HotkeyShouldRegisterSystemHotkeys(bool keyboard_hook_installed) {
    // 两者必须同时存在，不能二选一。
    //
    // 低层键盘钩子（WH_KEYBOARD_LL）受 UIPI 限制：当后台是提权窗口（任务管理器、管理员
    // PowerShell、以管理员运行的各种工具）时，普通权限进程根本收不到按键 —— 热键整体失灵。
    // RegisterHotKey 由 user32 在系统层匹配，不受 UIPI 影响，这种场景下仍然可用。
    //
    // 反过来，钩子能拿到更准确的前台窗口（用来判断粘贴目标、屏蔽重复触发），所以也要保留。
    // 同时启用时用 suppress_*_hotkey_count_ 吃掉钩子已经处理过的那次 WM_HOTKEY 回声。
    (void)keyboard_hook_installed;
    return true;
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
    // 弹窗在游戏前台里是 WS_EX_NOACTIVATE（为了不破坏游戏输入），拿不到键盘焦点，
    // 所以这些操作键必须由键盘钩子转发进弹窗，否则弹窗在游戏里只能看不能用。
    return vk == VK_ESCAPE || vk == VK_UP || vk == VK_DOWN || vk == VK_RETURN;
}

bool HotkeyOpenPopupShouldToggle(bool popup_visible, bool matched_popup_hotkey) {
    return popup_visible && matched_popup_hotkey;
}

bool HotkeyOpenPopupShouldHandleContinuousPaste(bool popup_visible, bool matched_continuous_hotkey) {
    return popup_visible && matched_continuous_hotkey;
}

OpenPopupHotkeyAction HotkeyOpenPopupActionFor(bool popup_visible, bool matched_popup_hotkey,
                                               bool matched_continuous_hotkey, unsigned vk) {
    if (HotkeyOpenPopupShouldToggle(popup_visible, matched_popup_hotkey)) {
        return OpenPopupHotkeyAction::TogglePopup;
    }
    if (HotkeyOpenPopupShouldHandleContinuousPaste(popup_visible, matched_continuous_hotkey)) {
        return OpenPopupHotkeyAction::ContinuousPaste;
    }
    if (popup_visible && HotkeyOpenPopupShouldHandleKey(vk)) {
        return OpenPopupHotkeyAction::ForwardKey;
    }
    return OpenPopupHotkeyAction::None;
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
