#pragma once

#include <Windows.h>

namespace ClipSoul {

class ClipboardMonitor {
public:
    ClipboardMonitor() = default;
    ~ClipboardMonitor();

    bool Start(HWND hwnd);
    void Stop();
    bool IsSelfWrite() const;
    void MarkSelfWrite();
    void ClearSelfWrite();

private:
    HWND hwnd_ = nullptr;
    bool self_write_ = false;
};

} // namespace ClipSoul
