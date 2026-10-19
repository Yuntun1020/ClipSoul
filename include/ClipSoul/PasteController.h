#pragma once

#include "ClipSoul/HistoryStore.h"

#include <Windows.h>

#include <functional>
#include <optional>

namespace ClipSoul {

class ClipboardMonitor;

struct PasteShortcutOptions {
    bool restore_alt_after_paste = true;
};

inline bool ShouldActivatePasteTarget(HWND target, HWND foreground) {
    return target && target != foreground;
}

inline bool ShouldReleaseModifierForPaste(int virtual_key, bool down) {
    // Alt 不需要「临时松开」：它的按下从来就没送到目标程序过。
    //
    // 按热键时键盘钩子会吞掉 Alt 的按下（为了不触发菜单栏 / 不打乱游戏输入），所以
    // 目标程序那边的 Alt 一直是抬起状态。这里再注入一次 KEYUP，目标程序收到的就是一个
    // 没有配对按下的抬起 —— 对状态机严格的程序（游戏）同样会造成修饰键错乱。
    // Ctrl / Shift / Win 是用户真实按着、目标程序也真实收到过的，才需要临时松开。
    if (virtual_key == VK_MENU) {
        return false;
    }
    return down && (virtual_key == VK_SHIFT || virtual_key == VK_LWIN || virtual_key == VK_RWIN);
}

inline bool ShouldRestoreModifierAfterPaste(int virtual_key, bool was_down) {
    return was_down && (virtual_key == VK_CONTROL || virtual_key == VK_MENU ||
                        virtual_key == VK_SHIFT || virtual_key == VK_LWIN ||
                        virtual_key == VK_RWIN);
}

inline bool ShouldRestoreModifierForPaste(int virtual_key, bool was_down, PasteShortcutOptions options) {
    // Alt 永远不还原。
    //
    // SendPaste() 会先把物理按着的修饰键「注入抬起」，发完 Ctrl+V 再「注入按下」来表示
    // 用户手指还按着。但热键场景里 Alt 的按下早就被键盘钩子吞掉了 —— 目标程序从头到尾
    // 没收到过 Alt 按下。此时再补一个按下（也没有配对的抬起），目标程序就会认为 Alt
    // 一直处于按下的状态：游戏里表现为角色保持 Alt 姿态、必须再按一次 Alt 才解锁。
    //
    // 这不是连续粘贴独有的问题，回车粘贴、多选粘贴走的都是同一条路，所以对所有
    // 调用点一律不还原 Alt。Ctrl / Shift / Win 不受影响：它们本来就是用户按着不放的，
    // 而钩子只吞 Alt。
    if (virtual_key == VK_MENU) {
        return false;
    }
    (void)options;
    return ShouldRestoreModifierAfterPaste(virtual_key, was_down);
}

class PasteController {
public:
    explicit PasteController(ClipboardMonitor& monitor);

    // Copies a history row to the clipboard. `load_html` is invoked for HTML entries before writing,
    // because list queries no longer carry the (potentially huge) CF_HTML payload; pass a callable
    // that reloads the row (e.g. [&](int64_t id) { return store.Get(id); }).
    bool RestoreToClipboard(const HistoryItem& item, HWND owner);
    bool RestoreToClipboard(const HistoryItem& item, HWND owner,
                            const std::function<std::optional<HistoryItem>(int64_t)>& load_full_item);
    bool RestoreMultipleToClipboard(const std::vector<HistoryItem>& items, HWND owner);
    void SendPaste(HWND target, PasteShortcutOptions options = {}) const;

private:
    bool SetText(const HistoryItem& item) const;
    bool SetText(std::wstring_view text) const;
    bool SetFiles(const HistoryItem& item) const;
    bool SetFiles(const std::vector<std::wstring>& files) const;
    bool SetImage(const HistoryItem& item) const;

    ClipboardMonitor& monitor_;
};

} // namespace ClipSoul
