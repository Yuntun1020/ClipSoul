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

TEST_CASE(HotkeyHookBypassesOnlyClipSoulInjectedPasteShortcutEvents) {
    REQUIRE(ClipSoul::HotkeyHookShouldIgnoreInjectedEvent(true, ClipSoul::kClipSoulInjectedInputExtraInfo));
    REQUIRE(!ClipSoul::HotkeyHookShouldIgnoreInjectedEvent(true, 0));
    REQUIRE(!ClipSoul::HotkeyHookShouldIgnoreInjectedEvent(false, ClipSoul::kClipSoulInjectedInputExtraInfo));
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

    // 任何情况下都不再吞掉独立的 Alt 按下。
    // 曾经为了 +「Alt 先到、C 后到」而缓冲 Alt 按下，结果游戏里单独按 Alt（技能/快捷键）
    // 会被吞掉且永不补发，表现为「只有 Alt+C 能用，其它 Alt 组合全失效」。
    REQUIRE(!ClipSoul::HotkeyShouldBufferAltDown(MOD_ALT, MOD_CONTROL | MOD_ALT, false, false, false));
    REQUIRE(!ClipSoul::HotkeyShouldBufferAltDown(MOD_CONTROL | MOD_ALT, MOD_CONTROL | MOD_ALT, false,
                                                 false, false));
    REQUIRE(!ClipSoul::HotkeyShouldBufferAltDown(MOD_CONTROL | MOD_ALT, MOD_ALT, true, false, false));
    REQUIRE(!ClipSoul::HotkeyShouldBufferAltDown(MOD_CONTROL | MOD_ALT | MOD_SHIFT, MOD_ALT, true, true,
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

    REQUIRE(ClipSoul::HotkeyShouldKeepBufferedAltAfterHandledHotkey(MOD_CONTROL | MOD_ALT, true, false, false));
    REQUIRE(!ClipSoul::HotkeyShouldKeepBufferedAltAfterHandledHotkey(MOD_CONTROL | MOD_ALT, false, false, false));
    REQUIRE(!ClipSoul::HotkeyShouldKeepBufferedAltAfterHandledHotkey(MOD_ALT, true, false, false));

    REQUIRE(ClipSoul::HotkeyShouldReplayBufferedAlt(false, false, 'X'));
    REQUIRE(!ClipSoul::HotkeyShouldReplayBufferedAlt(true, false, 'X'));
    REQUIRE(!ClipSoul::HotkeyShouldReplayBufferedAlt(false, true, VK_MENU));
    REQUIRE(!ClipSoul::HotkeyShouldReplayBufferedAlt(true, false, 'C'));

    REQUIRE(ClipSoul::HotkeyShouldSwallowAltReleaseAfterHandledHotkey(true, VK_MENU));
    REQUIRE(ClipSoul::HotkeyShouldSwallowAltReleaseAfterHandledHotkey(true, VK_LMENU));
    REQUIRE(!ClipSoul::HotkeyShouldSwallowAltReleaseAfterHandledHotkey(false, VK_MENU));
    REQUIRE(!ClipSoul::HotkeyShouldSwallowAltReleaseAfterHandledHotkey(true, 'C'));
    // 只要按下的 Alt 被吞过，它的抬起就必须丢掉（否则目标程序会收到没配对的抬起），
    // 并且两个缓冲标志都要清干净 —— 残留会让下一次按键状态错位。
    REQUIRE_EQ(ClipSoul::HotkeyAltReleaseActionFor(true, true, false, VK_MENU),
               ClipSoul::AltReleaseAction::SwallowAndClearAll);
    REQUIRE_EQ(ClipSoul::HotkeyAltReleaseActionFor(true, false, false, VK_MENU),
               ClipSoul::AltReleaseAction::SwallowAndClearAll);
    REQUIRE_EQ(ClipSoul::HotkeyAltReleaseActionFor(false, true, false, VK_MENU),
               ClipSoul::AltReleaseAction::SwallowAndClearAll);
    // replayed_alt_down 不再影响抬起处理：补发的 Alt 已是成对的按下+抬起，
    // 没有「等待配对」的挂起状态需要在这里收尾。
    REQUIRE_EQ(ClipSoul::HotkeyAltReleaseActionFor(false, false, true, VK_MENU),
               ClipSoul::AltReleaseAction::PassThrough);
    // 与 Alt 无关的按键不受影响。
    REQUIRE_EQ(ClipSoul::HotkeyAltReleaseActionFor(true, true, false, 'V'),
               ClipSoul::AltReleaseAction::PassThrough);

    REQUIRE(ClipSoul::HotkeyHookShouldIgnoreInjectedEvent(true, ClipSoul::kClipSoulInjectedInputExtraInfo));
    REQUIRE(!ClipSoul::HotkeyHookShouldIgnoreInjectedEvent(true, 0));
    REQUIRE(!ClipSoul::HotkeyHookShouldIgnoreInjectedEvent(false, ClipSoul::kClipSoulInjectedInputExtraInfo));

    const auto settings_window = reinterpret_cast<HWND>(0x2468);
    REQUIRE(ClipSoul::HotkeyHookShouldBypassSettingsForeground(settings_window, settings_window));
    REQUIRE(!ClipSoul::HotkeyHookShouldBypassSettingsForeground(reinterpret_cast<HWND>(0x1357),
                                                               settings_window));
    REQUIRE(!ClipSoul::HotkeyHookShouldBypassSettingsForeground(settings_window, nullptr));
    REQUIRE(ClipSoul::HotkeyHookShouldBypassSettingsWindow(settings_window, settings_window, true));
    REQUIRE(!ClipSoul::HotkeyHookShouldBypassSettingsWindow(reinterpret_cast<HWND>(0x1357),
                                                            settings_window, true));
    REQUIRE(!ClipSoul::HotkeyHookShouldBypassSettingsWindow(settings_window, settings_window, false));
    REQUIRE(!ClipSoul::HotkeyHookShouldBypassSettingsWindow(reinterpret_cast<HWND>(0x1357),
                                                            settings_window, false));
    REQUIRE(!ClipSoul::HotkeyHookShouldBypassSettingsWindow(settings_window, nullptr, true));

    // 系统热键必须始终注册：提权窗口（任务管理器）在前台时低层键盘钩子收不到按键，
    // 只有 RegisterHotKey 还能用。钩子保留是为了拿准确的前台窗口。
    REQUIRE(ClipSoul::HotkeyShouldRegisterSystemHotkeys(true));
    REQUIRE(ClipSoul::HotkeyShouldRegisterSystemHotkeys(false));
    REQUIRE(ClipSoul::HotkeyHookShouldSuppressRegisteredHotkeyEcho(true));
    REQUIRE(!ClipSoul::HotkeyHookShouldSuppressRegisteredHotkeyEcho(false));

    REQUIRE(ClipSoul::HotkeyOpenPopupShouldHandleKey(VK_ESCAPE));
    // 弹窗在游戏前台里不抢焦点，回车必须由钩子转发，否则游戏里无法粘贴。
    REQUIRE(ClipSoul::HotkeyOpenPopupShouldHandleKey(VK_RETURN));
    REQUIRE(ClipSoul::HotkeyOpenPopupShouldHandleKey(VK_UP));
    REQUIRE(ClipSoul::HotkeyOpenPopupShouldHandleKey(VK_DOWN));
    REQUIRE(!ClipSoul::HotkeyOpenPopupShouldHandleKey('A'));
    REQUIRE(!ClipSoul::HotkeyOpenPopupShouldHandleKey(VK_MENU));

    REQUIRE(ClipSoul::HotkeyOpenPopupShouldToggle(true, true));
    REQUIRE(!ClipSoul::HotkeyOpenPopupShouldToggle(true, false));
    REQUIRE(!ClipSoul::HotkeyOpenPopupShouldToggle(false, true));
    REQUIRE(ClipSoul::HotkeyOpenPopupShouldHandleContinuousPaste(true, true));
    REQUIRE(!ClipSoul::HotkeyOpenPopupShouldHandleContinuousPaste(true, false));
    REQUIRE(!ClipSoul::HotkeyOpenPopupShouldHandleContinuousPaste(false, true));
    REQUIRE_EQ(ClipSoul::HotkeyOpenPopupActionFor(true, true, false, 'C'),
               ClipSoul::OpenPopupHotkeyAction::TogglePopup);
    REQUIRE_EQ(ClipSoul::HotkeyOpenPopupActionFor(true, false, true, 'V'),
               ClipSoul::OpenPopupHotkeyAction::ContinuousPaste);
    REQUIRE_EQ(ClipSoul::HotkeyOpenPopupActionFor(true, false, false, VK_ESCAPE),
               ClipSoul::OpenPopupHotkeyAction::ForwardKey);
    REQUIRE_EQ(ClipSoul::HotkeyOpenPopupActionFor(true, false, false, 'X'),
               ClipSoul::OpenPopupHotkeyAction::None);
    REQUIRE_EQ(ClipSoul::HotkeyOpenPopupActionFor(false, false, true, 'V'),
               ClipSoul::OpenPopupHotkeyAction::None);

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

TEST_CASE(BareAltIsNeverSwallowedWhenAltIsAConfiguredModifier) {
    // 用户在游戏里发现「只有 Alt+C 能用，其它 Alt 快捷键全失效」。
    // 原因：钩子把任何独立的 Alt 按下都吞掉并缓冲，等着下一个键来判断是不是 Alt+C；
    // 匹配不上时那次按下已经被吞了，目标程序再也收不到。
    // Alt 现在一律放行，热键匹配交给系统层的 RegisterHotKey。
    const unsigned kAltOnly = MOD_ALT;
    const unsigned kCtrlAlt = MOD_CONTROL | MOD_ALT;

    const unsigned kCombos[] = {kAltOnly, kCtrlAlt, MOD_CONTROL | MOD_SHIFT};
    for (const unsigned popup : kCombos) {
        for (const unsigned continuous : kCombos) {
            // 无论热键怎么配，独立 Alt 都不能被吞。
            REQUIRE(!ClipSoul::HotkeyShouldBufferAltDown(popup, continuous, false, false, false));
            REQUIRE(!ClipSoul::HotkeyShouldBufferAltDown(popup, continuous, true, false, false));
            REQUIRE(!ClipSoul::HotkeyShouldBufferAltDown(popup, continuous, false, true, false));
        }
    }
}
