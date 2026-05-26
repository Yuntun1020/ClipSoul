#include "TestHarness.h"

#include "ClipSoul/HistoryStore.h"
#include "ClipSoul/StorageConfig.h"
#include "ClipSoul/Version.h"

#include <Windows.h>

#include <filesystem>

TEST_CASE(AppSettingsDefaultsUseAltC) {
    ClipSoul::AppSettings settings;

    ClipSoul::ApplyAppSettingsDefaults(settings);

    REQUIRE_EQ(settings.hotkey_modifiers, static_cast<unsigned>(MOD_ALT));
    REQUIRE_EQ(settings.hotkey_vk, static_cast<unsigned>('C'));
    REQUIRE_EQ(settings.continuous_paste_hotkey_modifiers, static_cast<unsigned>(MOD_CONTROL | MOD_ALT));
    REQUIRE_EQ(settings.continuous_paste_hotkey_vk, static_cast<unsigned>('V'));
}

TEST_CASE(AppVersionLabelIsCurrentRelease) {
    REQUIRE_EQ(std::wstring(ClipSoul::kClipSoulVersion), std::wstring(L"v1.1.3"));
}

TEST_CASE(AppSettingsDefaultsMigrateLegacyCtrlShiftV) {
    ClipSoul::AppSettings settings;
    settings.hotkey_modifiers = MOD_CONTROL | MOD_SHIFT;
    settings.hotkey_vk = 'V';

    ClipSoul::ApplyAppSettingsDefaults(settings);

    REQUIRE_EQ(settings.hotkey_modifiers, static_cast<unsigned>(MOD_ALT));
    REQUIRE_EQ(settings.hotkey_vk, static_cast<unsigned>('C'));
}

TEST_CASE(AppSettingsFormatHotkeyLabel) {
    REQUIRE_EQ(ClipSoul::FormatHotkey(MOD_ALT, 'C'), std::wstring(L"Alt+C"));
    REQUIRE_EQ(ClipSoul::FormatHotkey(MOD_CONTROL | MOD_SHIFT, 'V'), std::wstring(L"Ctrl+Shift+V"));
    REQUIRE_EQ(ClipSoul::FormatHotkey(MOD_CONTROL | MOD_ALT, 'V'), std::wstring(L"Ctrl+Alt+V"));
}

TEST_CASE(AppSettingsDefaultsThemeToSystem) {
    ClipSoul::AppSettings settings;

    ClipSoul::ApplyAppSettingsDefaults(settings);

    REQUIRE_EQ(settings.theme_mode, 0);
    REQUIRE(!settings.popup_resizable);
}

TEST_CASE(AppSettingsPersistsThemeModeAndPopupResizeToggle) {
    ClipSoul::HistoryStore store;
    const auto path = std::filesystem::temp_directory_path() / L"ClipSoulTests" / L"theme-resize-settings.db";
    std::filesystem::create_directories(path.parent_path());
    std::filesystem::remove(path);
    store.Open(path);

    auto settings = store.LoadSettings();
    settings.theme_mode = 2;
    settings.popup_resizable = true;
    store.SaveSettings(settings);

    const auto saved = store.LoadSettings();
    REQUIRE_EQ(saved.theme_mode, 2);
    REQUIRE(saved.popup_resizable);
}

TEST_CASE(AppSettingsPersistsHistoryLimitAndDefaultHotkey) {
    ClipSoul::HistoryStore store;
    const auto path = std::filesystem::temp_directory_path() / L"ClipSoulTests" / L"settings-save.db";
    std::filesystem::create_directories(path.parent_path());
    std::filesystem::remove(path);
    store.Open(path);

    auto settings = store.LoadSettings();
    settings.history_limit = 120;
    settings.hotkey_modifiers = MOD_ALT;
    settings.hotkey_vk = 'C';
    settings.continuous_paste_hotkey_modifiers = MOD_CONTROL | MOD_ALT;
    settings.continuous_paste_hotkey_vk = 'V';
    store.SaveSettings(settings);

    const auto saved = store.LoadSettings();
    REQUIRE_EQ(saved.history_limit, 120);
    REQUIRE_EQ(saved.hotkey_modifiers, static_cast<unsigned>(MOD_ALT));
    REQUIRE_EQ(saved.hotkey_vk, static_cast<unsigned>('C'));
    REQUIRE_EQ(saved.continuous_paste_hotkey_modifiers, static_cast<unsigned>(MOD_CONTROL | MOD_ALT));
    REQUIRE_EQ(saved.continuous_paste_hotkey_vk, static_cast<unsigned>('V'));
}

TEST_CASE(StorageConfigUsesDefaultWhenCustomPathIsEmpty) {
    const std::filesystem::path fallback = L"F:\\ClipSoul";

    REQUIRE_EQ(ClipSoul::ResolveStorageDirectory(L"", fallback), fallback);
    REQUIRE_EQ(ClipSoul::ResolveStorageDirectory(L"   \t\r\n", fallback), fallback);
}

TEST_CASE(StorageConfigTrimsAndKeepsCustomPath) {
    const std::filesystem::path fallback = L"F:\\ClipSoul";
    const auto custom = ClipSoul::ResolveStorageDirectory(L"  D:\\ClipSoulData  \r\n", fallback);

    REQUIRE_EQ(custom, std::filesystem::path(L"D:\\ClipSoulData"));
    REQUIRE_EQ(ClipSoul::SerializeStorageDirectory(custom), std::wstring(L"D:\\ClipSoulData"));
}
