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
constexpr UINT_PTR kClipboardCaptureTimer = 1;
// Wait a moment after WM_CLIPBOARDUPDATE before reading: many apps (Office, browsers, IDEs) fire
// several updates per copy and keep the clipboard open while still rendering formats.
constexpr UINT kClipboardCaptureDelayMs = 30;
constexpr UINT kClipboardRetryDelayMs = 50;
constexpr int kClipboardMaxRetries = 10;

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
    debug_log_enabled_ = DebugHotkeyLoggingEnabled();
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

        // A hidden top-level window (not HWND_MESSAGE): message-only windows never receive broadcast
        // messages such as TaskbarCreated (tray icon recovery after Explorer restarts),
        // WM_SETTINGCHANGE or WM_THEMECHANGED.
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kAppWindowClass, L"ClipSoul", WS_POPUP, 0, 0, 0, 0, nullptr,
                                nullptr, instance_, this);
        if (!hwnd_) {
            return fail(L"创建后台消息窗口失败");
        }
        taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");

        storage_dir_ = LoadStorageDir();
        store_.Open(storage_dir_ / L"clipsoul.db");
        settings_ = store_.LoadSettings();
        settings_.start_with_windows = GetStartWithWindows();
        ApplyAppSettingsDefaults(settings_);
        store_.SaveSettings(settings_);

        capture_ = std::make_unique<ClipboardCapture>(storage_dir_ / L"cache");
        popup_ = std::make_unique<PopupWindow>(instance_, store_, paste_controller_);
        if (debug_log_enabled_) {
            popup_->SetDebugLogger([this](std::wstring_view message) { DebugLog(message); });
        }
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
    if (!debug_log_enabled_) {
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
        // Settings may have just been saved; make the popup re-read them instead of using its cache.
        popup_->InvalidateSettingsCache();
        popup_->UpdateBehaviorFromSettings();
        popup_->Refresh();
    }
    if (settings_window_) {
        settings_window_->RefreshTheme();
    }
}

void App::RefreshThemeForSystemChange() {
    // The system preference may have changed; drop the cached registry answer before anything repaints.
    InvalidateSystemDarkTheme();
    if (!ThemeChangeShouldRefreshPalette(settings_.theme_mode)) {
        if (popup_) {
            popup_->RefreshTheme();
        }
        if (settings_window_) {
            settings_window_->RefreshTheme();
        }
        return;
    }
    if (popup_) {
        popup_->RefreshTheme();
    }
    if (settings_window_) {
        settings_window_->RefreshTheme();
    }
}

void App::ResetPopupSize() {
    if (popup_) {
        popup_->ResetManualSize();
        popup_->Refresh();
    }
}

void App::OnClipboardUpdate() {
    // Debounce: restart the timer on every update so bursts of notifications for one copy
    // result in a single capture once the source application is done writing.
    clipboard_retry_count_ = 0;
    SetTimer(hwnd_, kClipboardCaptureTimer, kClipboardCaptureDelayMs, nullptr);
}

void App::CaptureClipboard() {
    KillTimer(hwnd_, kClipboardCaptureTimer);
    const DWORD sequence = GetClipboardSequenceNumber();
    if (sequence != 0 && sequence == last_captured_sequence_) {
        return;
    }
    if (monitor_.IsSelfWrite()) {
        last_captured_sequence_ = sequence;
        return;
    }
    if (settings_.paused || !capture_) {
        last_captured_sequence_ = sequence;
        return;
    }

    bool clipboard_busy = false;
    try {
        if (auto content = capture_->Capture(hwnd_, &clipboard_busy)) {
            last_captured_sequence_ = sequence;
            // Pasting from history must not reorder it, even if another program re-wrote the
            // clipboard right after our paste (which gives it a new sequence number).
            if (monitor_.IsRecentSelfWriteContent(content->content_hash)) {
                return;
            }
            if (store_.Add(*content) && popup_) {
                popup_->Refresh();
            }
            return;
        }
    } catch (const std::exception& ex) {
        // Never let a single failed capture (locked database, disk full, bad data...) take the
        // whole app down; log it and keep listening.
        DebugLog(L"clipboard capture failed: " + WidenAscii(ex.what()));
        last_captured_sequence_ = sequence;
        return;
    }

    if (clipboard_busy && clipboard_retry_count_ < kClipboardMaxRetries) {
        ++clipboard_retry_count_;
        SetTimer(hwnd_, kClipboardCaptureTimer, kClipboardRetryDelayMs, nullptr);
        return;
    }
    if (clipboard_busy) {
        DebugLog(L"clipboard capture gave up: clipboard stayed locked by another process");
    }
    last_captured_sequence_ = sequence;
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

    // 这里的列表必须和弹窗里看到的是同一份，否则「按一下粘贴的是哪条」会和对不齐：
    // PopupWindow::BuildQuery() 用的是 limit=max(60, history_limit) 且 include_favorites=true，
    // 早先这里用的是 limit=history_limit + include_favorites=false，两边的下标/条目集合不同。
    HistoryQuery query;
    query.limit = std::max(60, settings_.history_limit);
    query.include_favorites = true;
    const auto items = store_.Query(query);
    const auto selected_id = popup_ ? popup_->SelectedItemId() : std::nullopt;
    if (const auto item = continuous_paste_.NextFromSelection(items, selected_id)) {
        auto load_full = [this](int64_t id) { return store_.Get(id); };
        if (paste_controller_.RestoreToClipboard(*item, hwnd_, load_full)) {
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

    HWND foreground = GetForegroundWindow();
    HWND settings_window = settings_window_ ? settings_window_->hwnd() : nullptr;
    if (HotkeyHookShouldBypassSettingsWindow(foreground, settings_window,
                                             settings_window && IsWindowVisible(settings_window))) {
        buffered_alt_down_ = false;
        replayed_alt_down_ = false;
        swallow_alt_release_ = false;
        hook_hotkey_down_ = false;
        hook_hotkey_vk_ = 0;
        return CallNextHookEx(keyboard_hook_, code, wparam, lparam);
    }

    if (wparam == WM_KEYUP || wparam == WM_SYSKEYUP) {
        switch (HotkeyAltReleaseActionFor(swallow_alt_release_, buffered_alt_down_, replayed_alt_down_,
                                          event->vkCode)) {
        case AltReleaseAction::SwallowAndClearAll:
            swallow_alt_release_ = false;
            buffered_alt_down_ = false;
            replayed_alt_down_ = false;
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

    if (!foreground || foreground == hwnd_ || (popup_ && foreground == popup_->hwnd())) {
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
    if (debug_log_enabled_) {
        std::wstring log = popup_hotkey ? L"hook popup hotkey target=" : L"hook continuous hotkey target=";
        log += HexWindow(foreground);
        log += L" vk=";
        log += std::to_wstring(vk);
        DebugLog(log);
    }
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
    if (taskbar_created_message_ != 0 && message == taskbar_created_message_) {
        // Explorer restarted: the notification area lost our icon, add it again.
        tray_.Remove();
        tray_.Add(hwnd_, WM_CLIPSOUL_TRAY);
        return 0;
    }
    HWND foreground = GetForegroundWindow();
    if (foreground && foreground != hwnd_ && (!popup_ || foreground != popup_->hwnd())) {
        last_input_target_ = foreground;
    }
    switch (message) {
    case WM_CLIPBOARDUPDATE:
        OnClipboardUpdate();
        return 0;
    case WM_TIMER:
        if (wparam == kClipboardCaptureTimer) {
            CaptureClipboard();
            return 0;
        }
        break;
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
    case WM_SETTINGCHANGE:
        // Only light/dark theme switches matter here; ignore the many unrelated setting broadcasts.
        if (lparam && lstrcmpiW(reinterpret_cast<const wchar_t*>(lparam), L"ImmersiveColorSet") == 0) {
            RefreshThemeForSystemChange();
        }
        return 0;
    case WM_THEMECHANGED:
        RefreshThemeForSystemChange();
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
