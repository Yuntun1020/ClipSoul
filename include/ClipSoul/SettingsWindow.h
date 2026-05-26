#pragma once

#include <Windows.h>

#include <string>

namespace ClipSoul {

class App;

class SettingsWindow {
public:
    SettingsWindow(HINSTANCE instance, App& app);
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
    void TogglePopupResizable();
    void ResetPopupSize();
    void BrowseStorageDirectory();
    void SetThemeMode(int mode);
    void ResetHotkeyToDefault();
    void ResetContinuousPasteHotkeyToDefault();
    void StartHotkeyCapture(bool continuous_paste);
    void SetCapturedHotkey(WPARAM vk);
    bool HotkeyAvailableForSelection(unsigned modifiers, unsigned vk, bool continuous_paste) const;
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    std::wstring limit_text_;
    std::wstring storage_path_;
    bool storage_save_failed_ = false;
    bool paused_ = false;
    bool startup_ = false;
    bool popup_resizable_ = false;
    bool capturing_hotkey_ = false;
    bool capturing_continuous_paste_hotkey_ = false;
    bool editing_limit_ = false;
    bool replace_limit_on_next_digit_ = false;
    unsigned hotkey_modifiers_ = 0;
    unsigned hotkey_vk_ = 0;
    unsigned continuous_paste_hotkey_modifiers_ = 0;
    unsigned continuous_paste_hotkey_vk_ = 0;
    bool hotkey_conflict_ = false;
    bool continuous_paste_hotkey_conflict_ = false;
    int theme_mode_ = 0;
    bool tracking_mouse_ = false;
    int hover_target_ = 0;
    float hover_progress_ = 0.0f;
    App& app_;
};

} // namespace ClipSoul
