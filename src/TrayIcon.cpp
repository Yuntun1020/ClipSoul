#include "ClipSoul/TrayIcon.h"

#include "ClipSoul/App.h"
#include "ClipSoul/ResourceIds.h"

namespace ClipSoul {

TrayIcon::~TrayIcon() {
    Remove();
}

bool TrayIcon::Add(HWND hwnd, UINT callback_message) {
    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.hWnd = hwnd;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
    data_.uCallbackMessage = callback_message;
    const int icon_size = GetSystemMetrics(SM_CXSMICON);
    data_.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_CLIPSOUL_APP),
                                                IMAGE_ICON, icon_size, icon_size, LR_DEFAULTCOLOR));
    if (!data_.hIcon) {
        data_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wcscpy_s(data_.szTip, L"ClipSoul");
    added_ = Shell_NotifyIconW(NIM_ADD, &data_) == TRUE;
    if (added_) {
        data_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data_);
    }
    return added_;
}

void TrayIcon::Remove() {
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &data_);
        added_ = false;
    }
    if (data_.hIcon) {
        DestroyIcon(data_.hIcon);
        data_.hIcon = nullptr;
    }
}

void TrayIcon::ShowMenu(HWND hwnd, POINT point, bool paused) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"显示 ClipSoul");
    AppendMenuW(menu, MF_STRING, ID_TRAY_PAUSE, paused ? L"继续监听" : L"暂停监听");
    AppendMenuW(menu, MF_STRING, ID_TRAY_SETTINGS, L"设置");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_CLEAR, L"清空历史");
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

} // namespace ClipSoul
