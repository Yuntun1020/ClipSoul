#pragma once

#include "ClipSoul/ClipboardCapture.h"
#include "ClipSoul/ClipboardMonitor.h"
#include "ClipSoul/ContinuousPaste.h"
#include "ClipSoul/HistoryStore.h"
#include "ClipSoul/PasteController.h"
#include "ClipSoul/PopupWindow.h"
#include "ClipSoul/SettingsWindow.h"
#include "ClipSoul/TrayIcon.h"

#include <Windows.h>

#include <filesystem>
#include <memory>
#include <string>

namespace ClipSoul {

constexpr UINT WM_CLIPSOUL_TRAY = WM_APP + 1;
constexpr UINT WM_CLIPSOUL_HOOK_HOTKEY = WM_APP + 2;
constexpr UINT HOTKEY_ID_POPUP = 1;
constexpr UINT HOTKEY_ID_CONTINUOUS_PASTE = 2;
constexpr UINT ID_TRAY_SHOW = 1001;
constexpr UINT ID_TRAY_PAUSE = 1002;
constexpr UINT ID_TRAY_SETTINGS = 1003;
constexpr UINT ID_TRAY_CLEAR = 1004;
constexpr UINT ID_TRAY_EXIT = 1005;

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();
    int Run(int show_command);
    void TogglePaused();
    void SaveSettings(const AppSettings& settings);
    void ClearHistory();
    void ShowSettings();
    AppSettings Settings() const;
    std::filesystem::path StorageDirectory() const;
    bool SaveStorageDirectory(const std::filesystem::path& storage_dir);
    bool HotkeyAvailable(unsigned modifiers, unsigned vk) const;
    void RefreshPopupTheme();
    void ResetPopupSize();
    void RefreshThemeForSystemChange();

private:
    bool Initialize();
    bool RegisterHotkey();
    bool InstallKeyboardHook();
    void UninstallKeyboardHook();
    void RefreshHotkeyRegistration();
    void DebugLog(std::wstring_view message) const;
    void OnClipboardUpdate();
    void CaptureClipboard();
    void OnHotkey(HWND target);
    void OnContinuousPasteHotkey(HWND target);
    std::optional<int64_t> SelectedPopupItemId() const;
    void OnTray(LPARAM lparam);
    void OnCommand(WPARAM wparam);
    HWND CurrentInputTarget() const;
    bool ConsumeSuppressedRegisteredHotkey(WPARAM hotkey_id);
    // Sends the buffered Alt as a complete tap (DOWN+UP in one SendInput call) so no
    // unpaired Alt event can be left behind in the target process.
    void ReplayBufferedAltTap();
    LRESULT HandleKeyboardHook(int code, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK KeyboardProc(int code, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    HHOOK keyboard_hook_ = nullptr;
    HistoryStore store_;
    std::unique_ptr<ClipboardCapture> capture_;
    ClipboardMonitor monitor_;
    PasteController paste_controller_;
    std::unique_ptr<PopupWindow> popup_;
    std::unique_ptr<SettingsWindow> settings_window_;
    TrayIcon tray_;
    AppSettings settings_;
    std::filesystem::path storage_dir_;
    std::wstring initialization_error_;
    ContinuousPasteCursor continuous_paste_;
    HWND last_input_target_ = nullptr;
    unsigned suppress_popup_hotkey_count_ = 0;
    unsigned suppress_continuous_hotkey_count_ = 0;
    bool hook_hotkey_down_ = false;
    unsigned hook_hotkey_vk_ = 0;
    bool buffered_alt_down_ = false;
    bool replayed_alt_down_ = false;
    bool swallow_alt_release_ = false;
    bool hotkey_keyboard_invocation_ = false;
    UINT taskbar_created_message_ = 0;
    // Cached once at startup: checking the environment / clipsoul.debug file on every log call
    // costs a filesystem hit even when logging is off.
    bool debug_log_enabled_ = false;
    DWORD last_captured_sequence_ = 0;
    int clipboard_retry_count_ = 0;
    static App* keyboard_hook_app_;
};

} // namespace ClipSoul
