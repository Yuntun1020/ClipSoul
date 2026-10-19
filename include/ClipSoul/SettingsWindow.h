#pragma once

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <memory>
#include <string>
#include <vector>

namespace ClipSoul {

namespace popup_detail {
class InkGlyphCache;
}

struct UiRect;
struct PopupThemePalette;

class App;

class SettingsWindow {
public:
    SettingsWindow(HINSTANCE instance, App& app);
    ~SettingsWindow();
    bool Create(HWND owner);
    void Show();
    void RefreshTheme();
    // `live_preview` is used while the theme switch animates: it must not block on a synchronous
    // RedrawWindow(RDW_UPDATENOW) or re-theme the native EDIT control every frame.
    void RefreshTheme(bool live_preview);
    // Palette being displayed right now; interpolated while the theme switch animates.
    PopupThemePalette CurrentPalette() const;
    HWND hwnd() const { return hwnd_; }
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

private:
    void LoadToControls();
    bool SaveFromControls();
    void ApplyBackdrop();
    void Paint();
    void EnsureDeviceResources();
    void DiscardDeviceResources();
    void UpdateLimitEditPosition();
    void TogglePause();
    void ToggleStartup();
    void TogglePopupResizable();
    void StartToggleMotion(int target, bool from_active, bool to_active);
    float ToggleKnobPosition(int target, bool active) const;
    void AdvanceToggleMotion(float elapsed_ms);
    void ResetPopupSize();
    void BrowseStorageDirectory();
    void SetThemeMode(int mode);
    void ResetHotkeyToDefault();
    void ResetContinuousPasteHotkeyToDefault();
    void StartHotkeyCapture(bool continuous_paste);
    void SetCapturedHotkey(WPARAM vk);
    bool HotkeyAvailableForSelection(unsigned modifiers, unsigned vk, bool continuous_paste) const;
    // Ink Ledger 动画：窗口淡入淡出 + 上浮，与预览缓动一致。
    void StartWindowTransition(bool opening);
    void AdvanceWindowTransition();
    void ApplyWindowTransitionFrame();
    void EndWindowTransition();
    void BeginHide();
    void StartSizeResetMotion(bool visible);
    void AdvanceSizeResetMotion(float elapsed_ms);
    void SetSaveMessage(std::wstring message, bool ok);
    void SetHoverTarget(int target);
    void RefreshHoverFromCursor();
    // 外框悬停：只看指针是否落在框内，独立于框内图标按钮的悬停。
    enum BoxHoverIndex { kBoxHoverLimit = 0, kBoxHoverStorage, kBoxHoverHotkey, kBoxHoverContinuous, kBoxHoverCount };
    bool PointerInsideRect(const UiRect& rect, int scroll_offset) const;
    bool BoxHoverTarget(int index) const;
    void AdvanceBoxHover(float elapsed_ms);
    void StartAnimationTimer();
    void AdvanceAnimations();
    bool AnimationActive() const;
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    HWND limit_edit_ = nullptr;
    HFONT limit_edit_font_ = nullptr;
    HBRUSH limit_edit_brush_ = nullptr;
    COLORREF limit_edit_brush_color_ = CLR_INVALID;
    int scroll_offset_ = 0;
    std::wstring limit_text_;
    std::wstring storage_path_;
    std::wstring save_message_;
    bool storage_save_failed_ = false;
    bool save_message_ok_ = false;
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
    int hover_fade_target_ = 0;
    int pressed_target_ = 0;
    float hover_progress_ = 1.0f;
    float box_hover_[kBoxHoverCount] = {0.0f, 0.0f, 0.0f, 0.0f};
    int tooltip_target_ = 0;
    int tooltip_dwell_ms_ = 0;
    ULONGLONG animation_last_tick_ = 0;
    float tooltip_progress_ = 0.0f;
    int toggle_motion_target_ = 0;
    bool toggle_motion_from_active_ = false;
    bool toggle_motion_to_active_ = false;
    float toggle_motion_progress_ = 1.0f;
    int theme_motion_from_ = 0;
    float theme_motion_progress_ = 1.0f;
    bool size_reset_target_visible_ = false;
    float size_reset_progress_ = 1.0f;
    bool window_transition_active_ = false;
    bool window_transition_opening_ = true;
    float window_transition_progress_ = 1.0f;
    RECT window_transition_rect_{};

    ID2D1Factory* d2d_factory_ = nullptr;
    ID2D1HwndRenderTarget* render_target_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    IDWriteFactory* dwrite_factory_ = nullptr;
    IDWriteTextFormat* title_format_ = nullptr;
    IDWriteTextFormat* label_format_ = nullptr;
    IDWriteTextFormat* small_format_ = nullptr;
    IDWriteTextFormat* section_format_ = nullptr;
    IDWriteTextFormat* value_mono_format_ = nullptr;
    IDWriteTextFormat* suffix_format_ = nullptr;
    IDWriteTextFormat* seg_format_ = nullptr;
    IDWriteTextFormat* seg_active_format_ = nullptr;
    IDWriteTextFormat* button_format_ = nullptr;
    IDWriteTextFormat* msg_format_ = nullptr;
    IDWriteTextFormat* link_format_ = nullptr;
    IDWriteTextFormat* ver_format_ = nullptr;
    IDWriteTextFormat* tooltip_format_ = nullptr;
    IDWriteInlineObject* ellipsis_sign_ = nullptr;
    // Text layouts are re-shaping the same strings every frame (40+ per frame); during the theme
    // cross-fade that costs more than the whole paint. Cache them per (text, format, rect).
    struct CachedTextLayout {
        std::wstring text;
        IDWriteTextFormat* format = nullptr;
        float width = 0.0f;
        float height = 0.0f;
        DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
        float letter_spacing = 0.0f;
        IDWriteTextLayout* layout = nullptr;
    };
    std::vector<CachedTextLayout> text_layout_cache_;
    void ClearTextLayoutCache();
    std::unique_ptr<popup_detail::InkGlyphCache> ink_glyph_cache_;
    App& app_;
};

} // namespace ClipSoul
