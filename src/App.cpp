#include "ClipSoul/App.h"

#include "ClipSoul/Win32Util.h"

#include <commctrl.h>

#include <exception>
#include <string_view>

namespace ClipSoul {
namespace {
constexpr wchar_t kAppWindowClass[] = L"ClipSoul.HiddenAppWindow";

std::wstring WidenAscii(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}
}

App::App(HINSTANCE instance)
    : instance_(instance),
      paste_controller_(monitor_) {}

int App::Run(int) {
    if (!Initialize()) {
        std::wstring message = L"ClipSoul 初始化失败";
        if (!initialization_error_.empty()) {
            message += L"\n\n";
            message += initialization_error_;
        }
        MessageBoxW(nullptr, message.c_str(), L"ClipSoul", MB_ICONERROR);
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
    initialization_error_.clear();
    auto fail = [this](std::wstring step) {
        const DWORD error = GetLastError();
        initialization_error_ = std::move(step);
        if (error != ERROR_SUCCESS) {
            initialization_error_ += L": ";
            initialization_error_ += FormatWin32Error(error);
        }
        return false;
    };

    try {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_DATE_CLASSES};
        InitCommonControlsEx(&icc);
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        WNDCLASSW wc{};
        wc.lpfnWndProc = App::WindowProc;
        wc.hInstance = instance_;
        wc.lpszClassName = kAppWindowClass;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return fail(L"注册后台窗口类失败");
        }

        hwnd_ = CreateWindowExW(0, kAppWindowClass, L"ClipSoul", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                nullptr, instance_, this);
        if (!hwnd_) {
            return fail(L"创建后台消息窗口失败");
        }

        storage_dir_ = LoadStorageDir();
        store_.Open(storage_dir_ / L"clipsoul.db");
        settings_ = store_.LoadSettings();
        settings_.start_with_windows = GetStartWithWindows();
        ApplyAppSettingsDefaults(settings_);
        store_.SaveSettings(settings_);

        capture_ = std::make_unique<ClipboardCapture>(storage_dir_ / L"cache");
        popup_ = std::make_unique<PopupWindow>(instance_, store_, paste_controller_);
        if (!popup_->Create(nullptr)) {
            return fail(L"创建历史弹窗失败");
        }

        if (!monitor_.Start(hwnd_)) {
            return fail(L"注册剪贴板监听失败");
        }
        RegisterHotkey();
        tray_.Add(hwnd_, WM_CLIPSOUL_TRAY);
        return true;
    } catch (const std::exception& ex) {
        initialization_error_ = L"初始化异常: ";
        initialization_error_ += WidenAscii(ex.what());
        return false;
    } catch (...) {
        initialization_error_ = L"初始化异常: unknown exception";
        return false;
    }
}

bool App::RegisterHotkey() {
    UnregisterHotKey(hwnd_, HOTKEY_ID_POPUP);
    UnregisterHotKey(hwnd_, HOTKEY_ID_CONTINUOUS_PASTE);
    const bool popup_registered =
        RegisterHotKey(hwnd_, HOTKEY_ID_POPUP, settings_.hotkey_modifiers, settings_.hotkey_vk) == TRUE;
    RegisterHotKey(hwnd_, HOTKEY_ID_CONTINUOUS_PASTE,
                   settings_.continuous_paste_hotkey_modifiers,
                   settings_.continuous_paste_hotkey_vk);
    return popup_registered;
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
    RefreshPopupTheme();
}

void App::ClearHistory() {
    store_.Clear();
}

void App::ShowSettings() {
    if (!settings_window_) {
        settings_window_ = std::make_unique<SettingsWindow>(instance_, *this);
        settings_window_->Create(nullptr);
    }
    settings_window_->Show();
}

AppSettings App::Settings() const {
    return settings_;
}

std::filesystem::path App::StorageDirectory() const {
    return storage_dir_.empty() ? DefaultStorageDir() : storage_dir_;
}

bool App::SaveStorageDirectory(const std::filesystem::path& storage_dir) {
    if (!SaveStorageDir(storage_dir)) {
        return false;
    }
    storage_dir_ = storage_dir;
    return true;
}

bool App::HotkeyAvailable(unsigned modifiers, unsigned vk) const {
    if (settings_.hotkey_modifiers == modifiers && settings_.hotkey_vk == vk) {
        return true;
    }
    if (settings_.continuous_paste_hotkey_modifiers == modifiers &&
        settings_.continuous_paste_hotkey_vk == vk) {
        return true;
    }
    constexpr int probe_id = 9171;
    if (RegisterHotKey(hwnd_, probe_id, modifiers, vk) != TRUE) {
        return false;
    }
    UnregisterHotKey(hwnd_, probe_id);
    return true;
}

void App::RefreshPopupTheme() {
    if (popup_) {
        popup_->Refresh();
    }
    if (settings_window_) {
        InvalidateRect(settings_window_->hwnd(), nullptr, FALSE);
    }
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
        if (store_.Add(*content) && popup_) {
            popup_->Refresh();
        }
    }
}

void App::OnHotkey() {
    if (popup_) {
        if (popup_->IsVisible()) {
            popup_->Hide();
        } else {
            continuous_paste_.Reset();
            popup_->Show(GetForegroundWindow());
        }
    }
}

std::optional<int64_t> App::SelectedPopupItemId() const {
    if (popup_ && popup_->IsVisible()) {
        return popup_->SelectedItemId();
    }
    return std::nullopt;
}

void App::OnContinuousPasteHotkey() {
    if (popup_ && popup_->IsVisible()) {
        popup_->PasteSelectedForContinuousPaste();
        return;
    }

    HistoryQuery query;
    query.limit = settings_.history_limit;
    const auto items = store_.Query(query);
    HWND target = GetForegroundWindow();
    if (const auto item = continuous_paste_.NextFromSelection(items, std::nullopt)) {
        if (paste_controller_.RestoreToClipboard(*item, hwnd_)) {
            paste_controller_.SendPaste(target);
        }
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
        if (wparam == HOTKEY_ID_CONTINUOUS_PASTE) {
            OnContinuousPasteHotkey();
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
        app->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    return app ? app->HandleMessage(message, wparam, lparam)
               : DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace ClipSoul
