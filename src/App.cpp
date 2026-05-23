#include "ClipSoul/App.h"

#include "ClipSoul/Win32Util.h"

#include <commctrl.h>

namespace ClipSoul {
namespace {
constexpr wchar_t kAppWindowClass[] = L"ClipSoul.HiddenAppWindow";
}

App::App(HINSTANCE instance)
    : instance_(instance),
      paste_controller_(monitor_) {}

int App::Run(int) {
    if (!Initialize()) {
        MessageBoxW(nullptr, L"ClipSoul 初始化失败", L"ClipSoul", MB_ICONERROR);
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool App::Initialize() {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_DATE_CLASSES};
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSW wc{};
    wc.lpfnWndProc = App::WindowProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kAppWindowClass;
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(0, kAppWindowClass, L"ClipSoul", 0, 0, 0, 0, 0, HWND_MESSAGE,
                            nullptr, instance_, this);
    if (!hwnd_) {
        return false;
    }

    const auto data_dir = AppDataDir();
    store_.Open(data_dir / L"clipsoul.db");
    settings_ = store_.LoadSettings();
    settings_.start_with_windows = GetStartWithWindows();
    if (settings_.hotkey_modifiers == 0) settings_.hotkey_modifiers = MOD_CONTROL | MOD_SHIFT;
    if (settings_.hotkey_vk == 0) settings_.hotkey_vk = 'V';

    capture_ = std::make_unique<ClipboardCapture>(data_dir / L"cache");
    popup_ = std::make_unique<PopupWindow>(instance_, store_, paste_controller_);
    popup_->Create(hwnd_);

    monitor_.Start(hwnd_);
    RegisterHotkey();
    tray_.Add(hwnd_, WM_CLIPSOUL_TRAY);
    return true;
}

void App::RegisterHotkey() {
    UnregisterHotKey(hwnd_, HOTKEY_ID_POPUP);
    RegisterHotKey(hwnd_, HOTKEY_ID_POPUP, settings_.hotkey_modifiers, settings_.hotkey_vk);
}

void App::TogglePaused() {
    settings_.paused = !settings_.paused;
    store_.SaveSettings(settings_);
}

void App::SaveSettings(const AppSettings& settings) {
    settings_ = settings;
    store_.SaveSettings(settings_);
    SetStartWithWindows(settings_.start_with_windows);
    RegisterHotkey();
}

void App::ClearHistory() {
    store_.Clear();
}

void App::ShowSettings() {
    if (!settings_window_) {
        settings_window_ = std::make_unique<SettingsWindow>(instance_, store_, *this);
        settings_window_->Create(nullptr);
    }
    settings_window_->Show();
}

AppSettings App::Settings() const {
    return settings_;
}

void App::OnClipboardUpdate() {
    if (monitor_.IsSelfWrite()) {
        monitor_.ClearSelfWrite();
        return;
    }
    if (settings_.paused || !capture_) {
        return;
    }
    if (auto content = capture_->Capture(hwnd_)) {
        store_.Add(*content);
    }
}

void App::OnHotkey() {
    if (popup_) {
        popup_->Show(GetForegroundWindow());
    }
}

void App::OnTray(LPARAM lparam) {
    if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) {
        POINT point{};
        GetCursorPos(&point);
        tray_.ShowMenu(hwnd_, point, settings_.paused);
    } else if (LOWORD(lparam) == WM_LBUTTONUP) {
        OnHotkey();
    }
}

void App::OnCommand(WPARAM wparam) {
    switch (LOWORD(wparam)) {
    case ID_TRAY_SHOW:
        OnHotkey();
        break;
    case ID_TRAY_PAUSE:
        TogglePaused();
        break;
    case ID_TRAY_SETTINGS:
        ShowSettings();
        break;
    case ID_TRAY_CLEAR:
        ClearHistory();
        break;
    case ID_TRAY_EXIT:
        tray_.Remove();
        PostQuitMessage(0);
        break;
    }
}

LRESULT App::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CLIPBOARDUPDATE:
        OnClipboardUpdate();
        return 0;
    case WM_HOTKEY:
        if (wparam == HOTKEY_ID_POPUP) {
            OnHotkey();
            return 0;
        }
        break;
    case WM_COMMAND:
        OnCommand(wparam);
        return 0;
    case WM_CLIPSOUL_TRAY:
        OnTray(lparam);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

LRESULT CALLBACK App::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        app = static_cast<App*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    return app ? app->HandleMessage(message, wparam, lparam)
               : DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace ClipSoul
