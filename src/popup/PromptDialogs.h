#pragma once

// Small modal dialogs used by the popup window: "add favorite phrase" and the generic one-line text
// prompt (new favorite group, edit note). Internal to the popup window.

#include <Windows.h>

#include <optional>
#include <string>

namespace ClipSoul::popup_detail {

struct FavoritePhraseInput {
    std::wstring text;
    std::wstring note;
};

// theme_mode uses the popup setting: 0 = system, 1 = light, 2 = dark.
std::optional<FavoritePhraseInput> PromptFavoritePhrase(HWND owner, HINSTANCE instance, int theme_mode = 0);
// A saved empty note is an engaged optional; an empty favorite group name cannot be saved.
std::optional<std::wstring> PromptText(HWND owner, HINSTANCE instance, std::wstring title, std::wstring subtitle,
                                       std::wstring initial_text = L"", int theme_mode = 0);

} // namespace ClipSoul::popup_detail
