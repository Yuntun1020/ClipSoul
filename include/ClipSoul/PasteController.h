#pragma once

#include "ClipSoul/HistoryStore.h"

#include <Windows.h>

namespace ClipSoul {

class ClipboardMonitor;

class PasteController {
public:
    explicit PasteController(ClipboardMonitor& monitor);

    bool RestoreToClipboard(const HistoryItem& item, HWND owner);
    bool RestoreMultipleToClipboard(const std::vector<HistoryItem>& items, HWND owner);
    void SendPaste(HWND target) const;

private:
    bool SetText(const HistoryItem& item) const;
    bool SetText(std::wstring_view text) const;
    bool SetFiles(const HistoryItem& item) const;
    bool SetFiles(const std::vector<std::wstring>& files) const;
    bool SetImage(const HistoryItem& item) const;

    ClipboardMonitor& monitor_;
};

} // namespace ClipSoul
