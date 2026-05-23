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

bool ClipboardMonitor::IsSelfWrite() const {
    return self_write_;
}

void ClipboardMonitor::MarkSelfWrite() {
    self_write_ = true;
}

void ClipboardMonitor::ClearSelfWrite() {
    self_write_ = false;
}

} // namespace ClipSoul
