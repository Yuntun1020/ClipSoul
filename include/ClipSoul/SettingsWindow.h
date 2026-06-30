#pragma once

#include <Windows.h>

#include <string>

namespace ClipSoul {

class App;

class SettingsWindow {
public:
    SettingsWindow(HINSTANCE instance, App& app);
    ~SettingsWindow();
    bool Create(HWND owner);
    void Show();
    void RefreshTheme();
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
    void StartToggleMotion(int target, bool from_active, bool to_active);
    float ToggleKnobPosition(int target, bool active) const;
    void AdvanceToggleMotion();
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
    HWND limit_edit_ = nullptr;
    HBRUSH limit_edit_brush_ = nullptr;
    COLORREF limit_edit_brush_color_ = CLR_INVALID;
    std::wstring limit_text_;
    std::wstring storage_path_;
    bool storage_save_failed_ = false;
    bool paused_ = false;
    bool startup_ = false;
    bool popup_resizable_ = false;
    bool capturing_hotkey_ = false;
    bool capturing_continuous_paste_hotkey_ = false;
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
    int toggle_motion_target_ = 0;
    bool toggle_motion_from_active_ = false;
    bool toggle_motion_to_active_ = false;
    float toggle_motion_progress_ = 1.0f;
    App& app_;
};

} // namespace ClipSoul
