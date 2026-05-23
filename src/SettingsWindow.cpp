#include "ClipSoul/SettingsWindow.h"

#include "ClipSoul/App.h"
#include "ClipSoul/Win32Util.h"

namespace ClipSoul {
namespace {
constexpr wchar_t kSettingsClass[] = L"ClipSoul.SettingsWindow";
constexpr int ID_LIMIT = 2001;
constexpr int ID_PAUSED = 2002;
constexpr int ID_STARTUP = 2003;
constexpr int ID_SAVE = 2004;
}

SettingsWindow::SettingsWindow(HINSTANCE instance, HistoryStore& store, App& app)
    : instance_(instance),
      store_(store),
      app_(app) {}

bool SettingsWindow::Create(HWND owner) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = SettingsWindow::WindowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kSettingsClass;
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kSettingsClass, L"ClipSoul 设置",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                            360, 230, owner, nullptr, instance_, this);
    if (!hwnd_) {
        return false;
    }

    CreateWindowW(L"STATIC", L"历史上限", WS_CHILD | WS_VISIBLE, 24, 28, 90, 24, hwnd_, nullptr,
                  instance_, nullptr);
    limit_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_NUMBER,
                                  120, 24, 160, 26, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LIMIT)),
                                  instance_, nullptr);
    paused_check_ = CreateWindowW(L"BUTTON", L"暂停监听", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                  24, 70, 160, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PAUSED)),
                                  instance_, nullptr);
    startup_check_ = CreateWindowW(L"BUTTON", L"开机自启", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                   24, 102, 160, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STARTUP)),
                                   instance_, nullptr);
    CreateWindowW(L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 214, 146, 72, 30,
                  hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SAVE)), instance_, nullptr);
    ApplyBackdrop();
    LoadToControls();
    return true;
}

void SettingsWindow::Show() {
    LoadToControls();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
}

void SettingsWindow::LoadToControls() {
    const auto settings = app_.Settings();
    SetWindowTextW(limit_edit_, std::to_wstring(settings.history_limit).c_str());
    SendMessageW(paused_check_, BM_SETCHECK, settings.paused ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(startup_check_, BM_SETCHECK, settings.start_with_windows ? BST_CHECKED : BST_UNCHECKED, 0);
}

void SettingsWindow::SaveFromControls() {
    wchar_t buffer[32]{};
    GetWindowTextW(limit_edit_, buffer, 32);
    auto settings = app_.Settings();
    settings.history_limit = std::max(1, _wtoi(buffer));
    settings.paused = SendMessageW(paused_check_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.start_with_windows = SendMessageW(startup_check_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    app_.SaveSettings(settings);
}

void SettingsWindow::ApplyBackdrop() {
    SetModernWindowAttributes(hwnd_);
}

LRESULT SettingsWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_COMMAND:
        if (LOWORD(wparam) == ID_SAVE) {
            SaveFromControls();
            ShowWindow(hwnd_, SW_HIDE);
            return 0;
        }
        break;
    case WM_CLOSE:
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

LRESULT CALLBACK SettingsWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    SettingsWindow* window = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        window = static_cast<SettingsWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    return window ? window->HandleMessage(message, wparam, lparam)
                  : DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace ClipSoul
