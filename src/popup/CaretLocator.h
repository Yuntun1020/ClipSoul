#pragma once

// Locates the text caret / focused text area of the paste target window so the popup can be placed
// next to it without covering it. Tries Win32 caret, MSAA, console cursor, Java Access Bridge,
// a visual scan for terminal carets and finally UI Automation. Internal to the popup window.

#include <Windows.h>

#include <string>

namespace ClipSoul::popup_detail {

enum class TextAvoidSource {
    None,
    GuiCaret,
    MsaaCaret,
    ConsoleCursor,
    JavaAccessBridgeCaret,
    VisualTerminalCaret,
    AutomationText2Caret,
    AutomationText2Selection,
    AutomationTextSelection,
    AutomationTerminalVisibleLine,
    AutomationElementRect,
};

struct FocusedTextContext {
    bool editable = false;
    bool has_rect = false;
    RECT rect{};
};

const wchar_t* TextAvoidSourceName(TextAvoidSource source);
bool TryGetCaretAnchor(HWND target, RECT& avoid, TextAvoidSource& source);
bool TryGetMsaaCaretAnchor(HWND target, RECT& avoid, TextAvoidSource& source);
bool TryGetConsoleAnchor(HWND target, RECT& avoid, TextAvoidSource& source);
bool TryGetJavaAccessBridgeCaretAnchor(HWND target, RECT& avoid, TextAvoidSource& source, std::wstring& status);
bool TryGetVisualTerminalCaretAnchor(HWND target, RECT& avoid, TextAvoidSource& source, std::wstring& status);
FocusedTextContext GetFocusedTextContext();
bool FocusedTextContextAllowsCaret(const FocusedTextContext& context, RECT avoid, UINT dpi);
bool TryGetAutomationCaretAvoidRect(UINT dpi, RECT& avoid, TextAvoidSource& source);
bool TryGetAutomationAvoidRectFromWindow(HWND target, UINT dpi, RECT& avoid, TextAvoidSource& source);
// Expensive (walks the UIA tree); only call when debug logging is enabled.
std::wstring AutomationFocusedElementDebugInfo();

} // namespace ClipSoul::popup_detail
