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

std::optional<FavoritePhraseInput> PromptFavoritePhrase(HWND owner, HINSTANCE instance);
std::optional<std::wstring> PromptText(HWND owner, HINSTANCE instance, std::wstring title, std::wstring subtitle,
                                       std::wstring initial_text = L"");

} // namespace ClipSoul::popup_detail
