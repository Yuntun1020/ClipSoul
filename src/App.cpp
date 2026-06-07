#include "ClipSoul/App.h"

#include "ClipSoul/Hotkey.h"
#include "ClipSoul/Win32Util.h"

#include <commctrl.h>

#include <exception>
#include <fstream>
#include <string_view>

namespace ClipSoul {
namespace {
constexpr wchar_t kAppWindowClass[] = L"ClipSoul.HiddenAppWindow";

std::wstring WidenAscii(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

bool DebugHotkeyLoggingEnabled() {
    wchar_t value[8]{};
    if (GetEnvironmentVariableW(L"CLIPSOUL_DEBUG_HOTKEY", value, static_cast<DWORD>(std::size(value))) > 0 &&
        value[0] == L'1') {
        return true;
    }
    return std::filesystem::exists(ExecutableDir() / L"clipsoul.debug");
}

std::wstring HexWindow(HWND hwnd) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"0x%p", hwnd);
    return buffer;
}
}

App* App::keyboard_hook_app_ = nullptr;

App::App(HINSTANCE instance)
    : instance_(instance),
      paste_controller_(monitor_) {}

App::~App() {
    UninstallKeyboardHook();
}

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
        popup_->SetDebugLogger([this](std::wstring_view message) { DebugLog(message); });
        if (!popup_->Create(nullptr)) {
            return fail(L"创建历史弹窗失败");
        }

        if (!monitor_.Start(hwnd_)) {
            return fail(L"注册剪贴板监听失败");
        }
        InstallKeyboardHook();
        RefreshHotkeyRegistration();
        DebugLog(L"app initialized");
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
        RegisterHotKey(hwnd_, HOTKEY_ID_POPUP, RegisteredHotkeyModifiers(settings_.hotkey_modifiers),
                       settings_.hotkey_vk) == TRUE;
    RegisterHotKey(hwnd_, HOTKEY_ID_CONTINUOUS_PASTE,
                   RegisteredHotkeyModifiers(settings_.continuous_paste_hotkey_modifiers),
                   settings_.continuous_paste_hotkey_vk);
    return popup_registered;
}

void App::RefreshHotkeyRegistration() {
    UnregisterHotKey(hwnd_, HOTKEY_ID_POPUP);
    UnregisterHotKey(hwnd_, HOTKEY_ID_CONTINUOUS_PASTE);
    const bool keyboard_hook_installed = keyboard_hook_ != nullptr;
    if (HotkeyShouldRegisterSystemHotkeys(keyboard_hook_installed)) {
        DebugLog(keyboard_hook_installed ? L"system hotkeys enabled with keyboard hook fallback"
                                         : L"system hotkeys enabled as fallback");
        RegisterHotkey();
    } else {
        DebugLog(L"keyboard hook active; system hotkeys disabled");
    }
}

bool App::InstallKeyboardHook() {
    UninstallKeyboardHook();
    keyboard_hook_app_ = this;
    keyboard_hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, App::KeyboardProc, instance_, 0);
    DebugLog(keyboard_hook_ ? L"keyboard hook installed" : L"keyboard hook install failed");
    return keyboard_hook_ != nullptr;
}

void App::DebugLog(std::wstring_view message) const {
    if (!DebugHotkeyLoggingEnabled()) {
        return;
    }
    std::wofstream file(StorageDirectory() / L"clipsoul-debug.log", std::ios::app);
    if (!file) {
        return;
    }
    SYSTEMTIME time{};
    GetLocalTime(&time);
    file << L'[' << time.wYear << L'-' << time.wMonth << L'-' << time.wDay << L' '
         << time.wHour << L':' << time.wMinute << L':' << time.wSecond << L'.' << time.wMilliseconds
         << L"] " << message << L'\n';
}

void App::UninstallKeyboardHook() {
    if (keyboard_hook_) {
        UnhookWindowsHookEx(keyboard_hook_);
        keyboard_hook_ = nullptr;
    }
    if (keyboard_hook_app_ == this) {
        keyboard_hook_app_ = nullptr;
    }
}

void App::TogglePaused() {
    settings_.paused = !settings_.paused;
    store_.SaveSettings(settings_);
}

void App::SaveSettings(const AppSettings& settings) {
    settings_ = settings;
    store_.SaveSettings(settings_);
    SetStartWithWindows(settings_.start_with_windows);
    RefreshHotkeyRegistration();
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
    if (RegisterHotKey(hwnd_, probe_id, RegisteredHotkeyModifiers(modifiers), vk) != TRUE) {
        return false;
    }
    UnregisterHotKey(hwnd_, probe_id);
    return true;
}

void App::RefreshPopupTheme() {
    if (popup_) {
        popup_->UpdateBehaviorFromSettings();
        popup_->Refresh();
    }
    if (settings_window_) {
        InvalidateRect(settings_window_->hwnd(), nullptr, FALSE);
    }
}

void App::ResetPopupSize() {
    if (popup_) {
        popup_->ResetManualSize();
        popup_->Refresh();
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

void App::OnHotkey(HWND target) {
    if (popup_) {
        if (popup_->IsVisible()) {
            popup_->Hide(L"hotkey-toggle");
        } else {
            continuous_paste_.Reset();
            popup_->SetKeyboardInvocation(hotkey_keyboard_invocation_);
            popup_->Show(target);
        }
    }
}

std::optional<int64_t> App::SelectedPopupItemId() const {
    if (popup_ && popup_->IsVisible()) {
        return popup_->SelectedItemId();
    }
    return std::nullopt;
}

void App::OnContinuousPasteHotkey(HWND target) {
    if (popup_ && popup_->IsVisible()) {
        popup_->PasteSelectedForContinuousPaste();
        return;
    }

    HistoryQuery query;
    query.limit = settings_.history_limit;
    const auto items = store_.Query(query);
    const auto selected_id = popup_ ? popup_->SelectedItemId() : std::nullopt;
    if (const auto item = continuous_paste_.NextFromSelection(items, selected_id)) {
        if (paste_controller_.RestoreToClipboard(*item, hwnd_)) {
            paste_controller_.SendPaste(target, PasteShortcutOptions{false});
            if (popup_) {
                popup_->AdvanceSelectionAfterContinuousPaste();
            }
        }
    }
}

HWND App::CurrentInputTarget() const {
    HWND foreground = GetForegroundWindow();
    if (foreground && foreground != hwnd_ && (!popup_ || foreground != popup_->hwnd())) {
        return foreground;
    }
    if (last_input_target_ && IsWindow(last_input_target_)) {
        return last_input_target_;
    }
    return foreground;
}

bool App::ConsumeSuppressedRegisteredHotkey(WPARAM hotkey_id) {
    unsigned* counter = nullptr;
    if (hotkey_id == HOTKEY_ID_POPUP) {
        counter = &suppress_popup_hotkey_count_;
    } else if (hotkey_id == HOTKEY_ID_CONTINUOUS_PASTE) {
        counter = &suppress_continuous_hotkey_count_;
    }
    if (!counter || *counter == 0) {
        return false;
    }
    --(*counter);
    return true;
}

void App::ReplayBufferedAltDown() {
    buffered_alt_down_ = false;
    replayed_alt_down_ = true;
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_MENU;
    input.ki.dwExtraInfo = kClipSoulInjectedInputExtraInfo;
    SendInput(1, &input, sizeof(INPUT));
}

void App::ReplayBufferedAltUp() {
    if (!replayed_alt_down_) {
        return;
    }
    replayed_alt_down_ = false;
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_MENU;
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    input.ki.dwExtraInfo = kClipSoulInjectedInputExtraInfo;
    SendInput(1, &input, sizeof(INPUT));
}

LRESULT App::HandleKeyboardHook(int code, WPARAM wparam, LPARAM lparam) {
    if (code != HC_ACTION || !hwnd_) {
        return CallNextHookEx(keyboard_hook_, code, wparam, lparam);
    }

    const auto* event = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
    if (!event || event->vkCode == 0) {
        return CallNextHookEx(keyboard_hook_, code, wparam, lparam);
    }
    if (HotkeyHookShouldIgnoreInjectedEvent((event->flags & LLKHF_INJECTED) != 0, event->dwExtraInfo)) {
        return CallNextHookEx(keyboard_hook_, code, wparam, lparam);
    }

    if (wparam == WM_KEYUP || wparam == WM_SYSKEYUP) {
        switch (HotkeyAltReleaseActionFor(swallow_alt_release_, buffered_alt_down_, replayed_alt_down_,
                                          event->vkCode)) {
        case AltReleaseAction::SwallowAndClearBuffered:
            swallow_alt_release_ = false;
            buffered_alt_down_ = false;
            return 1;
        case AltReleaseAction::Swallow:
            swallow_alt_release_ = false;
            return 1;
        case AltReleaseAction::ReplayBufferedAltUp:
            ReplayBufferedAltUp();
            return 1;
        case AltReleaseAction::PassThrough:
            break;
        }
        if (hook_hotkey_down_ && hook_hotkey_vk_ == event->vkCode) {
            hook_hotkey_down_ = false;
            hook_hotkey_vk_ = 0;
            return 1;
        }
        return CallNextHookEx(keyboard_hook_, code, wparam, lparam);
    }

    if (wparam != WM_KEYDOWN && wparam != WM_SYSKEYDOWN) {
        return CallNextHookEx(keyboard_hook_, code, wparam, lparam);
    }

    const unsigned vk = static_cast<unsigned>(event->vkCode);
    HWND foreground = GetForegroundWindow();
    const bool ctrl_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt_down = buffered_alt_down_ || (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    const bool shift_down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool win_down = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
                          (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

    if (HotkeyIsAltKey(vk) &&
        HotkeyShouldBufferAltDown(settings_.hotkey_modifiers, settings_.continuous_paste_hotkey_modifiers,
                                  ctrl_down, shift_down, win_down)) {
        buffered_alt_down_ = true;
        return 1;
    }

    const bool popup_hotkey = HotkeyMatchesState(settings_.hotkey_modifiers, settings_.hotkey_vk, ctrl_down,
                                                 alt_down, shift_down, win_down, vk);
    const bool continuous_hotkey =
        HotkeyMatchesState(settings_.continuous_paste_hotkey_modifiers, settings_.continuous_paste_hotkey_vk,
                           ctrl_down, alt_down, shift_down, win_down, vk);
    const bool matched_hotkey = popup_hotkey || continuous_hotkey;

    if (HotkeyShouldSuppressRepeatedKeyDown(hook_hotkey_down_, hook_hotkey_vk_, vk)) {
        return 1;
    }

    switch (HotkeyOpenPopupActionFor(popup_ && popup_->IsVisible(), popup_hotkey, continuous_hotkey, vk)) {
    case OpenPopupHotkeyAction::TogglePopup:
        last_input_target_ = foreground && foreground != hwnd_ && (!popup_ || foreground != popup_->hwnd())
                                 ? foreground
                                 : last_input_target_;
        if (HotkeyShouldTrackHandledKeyUp(true, vk)) {
            hook_hotkey_down_ = true;
            hook_hotkey_vk_ = vk;
        }
        swallow_alt_release_ = buffered_alt_down_;
        buffered_alt_down_ = false;
        PostMessageW(hwnd_, WM_CLIPSOUL_HOOK_HOTKEY, HOTKEY_ID_POPUP,
                      reinterpret_cast<LPARAM>(HotkeyMessageTarget(foreground, CurrentInputTarget())));
        return 1;
    case OpenPopupHotkeyAction::ContinuousPaste:
        if (HotkeyShouldTrackHandledKeyUp(true, vk)) {
            hook_hotkey_down_ = true;
            hook_hotkey_vk_ = vk;
        }
        swallow_alt_release_ = buffered_alt_down_;
        buffered_alt_down_ =
            buffered_alt_down_ &&
            HotkeyShouldKeepBufferedAltAfterHandledHotkey(settings_.continuous_paste_hotkey_modifiers,
                                                          ctrl_down, shift_down, win_down);
        if (HotkeyHookShouldSuppressRegisteredHotkeyEcho(true)) {
            ++suppress_continuous_hotkey_count_;
        }
        PostMessageW(hwnd_, WM_CLIPSOUL_HOOK_HOTKEY, HOTKEY_ID_CONTINUOUS_PASTE,
                     reinterpret_cast<LPARAM>(HotkeyMessageTarget(foreground, CurrentInputTarget())));
        return 1;
    case OpenPopupHotkeyAction::ForwardKey:
        PostMessageW(popup_->hwnd(), WM_KEYDOWN, static_cast<WPARAM>(vk), lparam);
        return 1;
    case OpenPopupHotkeyAction::None:
        break;
    }

    if (!foreground || foreground == hwnd_ || (popup_ && foreground == popup_->hwnd()) ||
        (settings_window_ && foreground == settings_window_->hwnd())) {
        return CallNextHookEx(keyboard_hook_, code, wparam, lparam);
    }

    if (buffered_alt_down_ && HotkeyShouldReplayBufferedAlt(matched_hotkey, HotkeyIsAltKey(vk), vk)) {
        if (HotkeyShouldKeepBufferedAltForModifier(settings_.hotkey_modifiers,
                                                   settings_.continuous_paste_hotkey_modifiers,
                                                   ctrl_down, shift_down, win_down, vk)) {
            return CallNextHookEx(keyboard_hook_, code, wparam, lparam);
        }
        ReplayBufferedAltDown();
    }
    if (!matched_hotkey) {
        return CallNextHookEx(keyboard_hook_, code, wparam, lparam);
    }
    last_input_target_ = foreground;
    std::wstring log = popup_hotkey ? L"hook popup hotkey target=" : L"hook continuous hotkey target=";
    log += HexWindow(foreground);
    log += L" vk=";
    log += std::to_wstring(vk);
    DebugLog(log);
    if (HotkeyHookShouldSuppressRegisteredHotkeyEcho(true) && popup_hotkey) {
        ++suppress_popup_hotkey_count_;
    } else if (HotkeyHookShouldSuppressRegisteredHotkeyEcho(true)) {
        ++suppress_continuous_hotkey_count_;
    }
    if (HotkeyShouldTrackHandledKeyUp(true, vk)) {
        hook_hotkey_down_ = true;
        hook_hotkey_vk_ = vk;
    }
    swallow_alt_release_ = buffered_alt_down_;
    buffered_alt_down_ =
        buffered_alt_down_ &&
        HotkeyShouldKeepBufferedAltAfterHandledHotkey(popup_hotkey ? settings_.hotkey_modifiers
                                                                   : settings_.continuous_paste_hotkey_modifiers,
                                                      ctrl_down, shift_down, win_down);
    PostMessageW(hwnd_, WM_CLIPSOUL_HOOK_HOTKEY, popup_hotkey ? HOTKEY_ID_POPUP : HOTKEY_ID_CONTINUOUS_PASTE,
                 reinterpret_cast<LPARAM>(foreground));
    return 1;
}

void App::OnTray(LPARAM lparam) {
    if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) {
        POINT point{};
        GetCursorPos(&point);
        tray_.ShowMenu(hwnd_, point, settings_.paused);
    } else if (LOWORD(lparam) == WM_LBUTTONUP) {
        hotkey_keyboard_invocation_ = false;
        OnHotkey(CurrentInputTarget());
    }
}

void App::OnCommand(WPARAM wparam) {
    switch (LOWORD(wparam)) {
    case ID_TRAY_SHOW:
        hotkey_keyboard_invocation_ = false;
        OnHotkey(CurrentInputTarget());
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
    HWND foreground = GetForegroundWindow();
    if (foreground && foreground != hwnd_ && (!popup_ || foreground != popup_->hwnd())) {
        last_input_target_ = foreground;
    }
    switch (message) {
    case WM_CLIPBOARDUPDATE:
        OnClipboardUpdate();
        return 0;
    case WM_HOTKEY:
        if (ConsumeSuppressedRegisteredHotkey(wparam)) {
            return 0;
        }
        if (wparam == HOTKEY_ID_POPUP) {
            hotkey_keyboard_invocation_ = true;
            OnHotkey(CurrentInputTarget());
            hotkey_keyboard_invocation_ = false;
            return 0;
        }
        if (wparam == HOTKEY_ID_CONTINUOUS_PASTE) {
            hotkey_keyboard_invocation_ = true;
            OnContinuousPasteHotkey(CurrentInputTarget());
            hotkey_keyboard_invocation_ = false;
            return 0;
        }
        break;
    case WM_CLIPSOUL_HOOK_HOTKEY:
        if (wparam == HOTKEY_ID_POPUP) {
            hotkey_keyboard_invocation_ = true;
            OnHotkey(reinterpret_cast<HWND>(lparam));
            hotkey_keyboard_invocation_ = false;
            return 0;
        }
        if (wparam == HOTKEY_ID_CONTINUOUS_PASTE) {
            hotkey_keyboard_invocation_ = true;
            OnContinuousPasteHotkey(reinterpret_cast<HWND>(lparam));
            hotkey_keyboard_invocation_ = false;
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
        UninstallKeyboardHook();
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

LRESULT CALLBACK App::KeyboardProc(int code, WPARAM wparam, LPARAM lparam) {
    if (!keyboard_hook_app_) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }
    return keyboard_hook_app_->HandleKeyboardHook(code, wparam, lparam);
}

} // namespace ClipSoul
