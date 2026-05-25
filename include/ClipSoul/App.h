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

private:
    bool Initialize();
    bool RegisterHotkey();
    void OnClipboardUpdate();
    void OnHotkey();
    void OnContinuousPasteHotkey();
    std::optional<int64_t> SelectedPopupItemId() const;
    void OnTray(LPARAM lparam);
    void OnCommand(WPARAM wparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
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
};

} // namespace ClipSoul
