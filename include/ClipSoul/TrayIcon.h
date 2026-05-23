#pragma once

#include <Windows.h>
#include <shellapi.h>

namespace ClipSoul {

class TrayIcon {
public:
    TrayIcon() = default;
    ~TrayIcon();

    bool Add(HWND hwnd, UINT callback_message);
    void Remove();
    void ShowMenu(HWND hwnd, POINT point, bool paused);

private:
    NOTIFYICONDATAW data_{};
    bool added_ = false;
};

} // namespace ClipSoul
