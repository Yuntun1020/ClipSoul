#include "ClipSoul/ClipboardMonitor.h"

namespace ClipSoul {

ClipboardMonitor::~ClipboardMonitor() {
    Stop();
}

bool ClipboardMonitor::Start(HWND hwnd) {
    Stop();
    if (!AddClipboardFormatListener(hwnd)) {
        return false;
    }
    hwnd_ = hwnd;
    return true;
}

void ClipboardMonitor::Stop() {
    if (hwnd_) {
        RemoveClipboardFormatListener(hwnd_);
        hwnd_ = nullptr;
    }
}

void ClipboardMonitor::RecordSelfWrite(std::vector<std::wstring> content_hashes) {
    self_write_sequence_ = GetClipboardSequenceNumber();
    self_write_tick_ = GetTickCount64();
    self_write_hashes_ = std::move(content_hashes);
}

bool ClipboardMonitor::IsSelfWrite() const {
    // A sequence number of 0 means "unknown" (no access to the window station).
    return self_write_sequence_ != 0 && GetClipboardSequenceNumber() == self_write_sequence_;
}

bool ClipboardMonitor::IsRecentSelfWriteContent(const std::wstring& content_hash) const {
    constexpr ULONGLONG kWindowMs = 2000;
    if (content_hash.empty() || self_write_hashes_.empty() || GetTickCount64() - self_write_tick_ > kWindowMs) {
        return false;
    }
    for (const auto& hash : self_write_hashes_) {
        if (hash == content_hash) {
            return true;
        }
    }
    return false;
}

} // namespace ClipSoul
