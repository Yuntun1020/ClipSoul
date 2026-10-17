#pragma once

#include <Windows.h>

#include <string>
#include <vector>

namespace ClipSoul {

class ClipboardMonitor {
public:
    ClipboardMonitor() = default;
    ~ClipboardMonitor();

    bool Start(HWND hwnd);
    void Stop();

    // Records the clipboard sequence number produced by ClipSoul's own write (paste from history).
    // Call right after CloseClipboard() of a write operation. content_hashes are the hashes of the
    // history entries that were written, used as a secondary guard (see IsRecentSelfWriteContent).
    void RecordSelfWrite(std::vector<std::wstring> content_hashes = {});
    // True when the current clipboard content was written by ClipSoul itself.
    bool IsSelfWrite() const;
    // True when content with this hash was written by ClipSoul moments ago. Covers the case where
    // another program re-writes the clipboard right after our paste (sequence number changes), so
    // pasting from history never reorders the history.
    bool IsRecentSelfWriteContent(const std::wstring& content_hash) const;

private:
    HWND hwnd_ = nullptr;
    DWORD self_write_sequence_ = 0;
    ULONGLONG self_write_tick_ = 0;
    std::vector<std::wstring> self_write_hashes_;
};

} // namespace ClipSoul
