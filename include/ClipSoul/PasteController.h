#pragma once

#include "ClipSoul/HistoryStore.h"

#include <Windows.h>

namespace ClipSoul {

class ClipboardMonitor;

struct PasteShortcutOptions {
    bool restore_alt_after_paste = true;
};

inline bool ShouldActivatePasteTarget(HWND target, HWND foreground) {
    return target && target != foreground;
}

inline bool ShouldReleaseModifierForPaste(int virtual_key, bool down) {
    return down && (virtual_key == VK_MENU || virtual_key == VK_SHIFT ||
                    virtual_key == VK_LWIN || virtual_key == VK_RWIN);
}

inline bool ShouldRestoreModifierAfterPaste(int virtual_key, bool was_down) {
    return was_down && (virtual_key == VK_CONTROL || virtual_key == VK_MENU ||
                        virtual_key == VK_SHIFT || virtual_key == VK_LWIN ||
                        virtual_key == VK_RWIN);
}

inline bool ShouldRestoreModifierForPaste(int virtual_key, bool was_down, PasteShortcutOptions options) {
    if (virtual_key == VK_MENU && !options.restore_alt_after_paste) {
        return false;
    }
    return ShouldRestoreModifierAfterPaste(virtual_key, was_down);
}

class PasteController {
public:
    explicit PasteController(ClipboardMonitor& monitor);

    bool RestoreToClipboard(const HistoryItem& item, HWND owner);
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
