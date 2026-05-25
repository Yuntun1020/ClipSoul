#pragma once

#include "ClipSoul/HistoryStore.h"

#include <Windows.h>

#include <string>

namespace ClipSoul {

class App;

class SettingsWindow {
public:
    SettingsWindow(HINSTANCE instance, HistoryStore& store, App& app);
    bool Create(HWND owner);
    void Show();
    HWND hwnd() const { return hwnd_; }
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

private:
    void LoadToControls();
    bool SaveFromControls();
    void ApplyBackdrop();
    void Paint();
    void TogglePause();
    void ToggleStartup();
    void BrowseStorageDirectory();
    void SetThemeMode(int mode);
    void ResetHotkeyToDefault();
    void StartHotkeyCapture();
    void SetCapturedHotkey(WPARAM vk);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    std::wstring limit_text_;
    std::wstring storage_path_;
    bool storage_save_failed_ = false;
    bool paused_ = false;
    bool startup_ = false;
    bool capturing_hotkey_ = false;
    bool editing_limit_ = false;
    bool replace_limit_on_next_digit_ = false;
    unsigned hotkey_modifiers_ = 0;
    unsigned hotkey_vk_ = 0;
    bool hotkey_conflict_ = false;
    int theme_mode_ = 0;
    bool tracking_mouse_ = false;
    int hover_target_ = 0;
    float hover_progress_ = 0.0f;
    HistoryStore& store_;
    App& app_;
};

} // namespace ClipSoul
