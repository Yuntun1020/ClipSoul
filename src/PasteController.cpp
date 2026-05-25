#include "ClipSoul/PasteController.h"

#include "ClipSoul/ClipboardMonitor.h"
#include "ClipSoul/PasteModel.h"
#include "ClipSoul/TextUtil.h"

#include <fstream>
#include <shlobj_core.h>
#include <shellapi.h>
#include <vector>

namespace ClipSoul {
namespace {

HGLOBAL AllocMoveableBytes(const void* data, size_t size) {
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!handle) {
        return nullptr;
    }
    void* target = GlobalLock(handle);
    if (!target) {
        GlobalFree(handle);
        return nullptr;
    }
    memcpy(target, data, size);
    GlobalUnlock(handle);
    return handle;
}

HGLOBAL AllocWideText(std::wstring_view text) {
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!handle) {
        return nullptr;
    }
    auto* target = static_cast<wchar_t*>(GlobalLock(handle));
    if (!target) {
        GlobalFree(handle);
        return nullptr;
    }
    memcpy(target, text.data(), text.size() * sizeof(wchar_t));
    target[text.size()] = L'\0';
    GlobalUnlock(handle);
    return handle;
}

} // namespace

PasteController::PasteController(ClipboardMonitor& monitor) : monitor_(monitor) {}

bool PasteController::RestoreToClipboard(const HistoryItem& item, HWND owner) {
    monitor_.MarkSelfWrite();
    if (!OpenClipboard(owner)) {
        monitor_.ClearSelfWrite();
        return false;
    }

    EmptyClipboard();
    bool ok = false;
    switch (item.kind) {
    case ClipboardKind::Text:
    case ClipboardKind::Html:
    case ClipboardKind::Link:
        ok = SetText(item);
        break;
    case ClipboardKind::Files:
        ok = SetFiles(item);
        break;
    case ClipboardKind::Image:
        ok = SetImage(item);
        break;
    }
    CloseClipboard();
    return ok;
}

bool PasteController::RestoreMultipleToClipboard(const std::vector<HistoryItem>& items, HWND owner) {
    if (items.empty()) {
        return false;
    }

    monitor_.MarkSelfWrite();
    if (!OpenClipboard(owner)) {
        monitor_.ClearSelfWrite();
        return false;
    }

    EmptyClipboard();
    const auto payload = BuildMultiPastePayload(items);
    bool ok = false;
    if (!payload.text.empty()) {
        ok = SetText(payload.text) || ok;
    }
    if (!payload.files.empty()) {
        ok = SetFiles(payload.files) || ok;
    }
    if (!ok && payload.first_image) {
        ok = SetImage(*payload.first_image);
    }

    CloseClipboard();
    return ok;
}

void PasteController::SendPaste(HWND target) const {
    if (ShouldActivatePasteTarget(target, GetForegroundWindow())) {
        SetForegroundWindow(target);
    }
    std::vector<INPUT> inputs;
    inputs.reserve(16);
    const auto add_key = [&inputs](WORD vk, DWORD flags = 0) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        input.ki.dwFlags = flags;
        inputs.push_back(input);
    };

    constexpr WORD kModifiers[] = {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN};
    std::vector<WORD> physically_down;
    physically_down.reserve(std::size(kModifiers));
    for (const WORD vk : kModifiers) {
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (down) {
            physically_down.push_back(vk);
        }
        if (ShouldReleaseModifierForPaste(vk, down)) {
            add_key(vk, KEYEVENTF_KEYUP);
        }
    }
    add_key(VK_CONTROL);
    add_key('V');
    add_key('V', KEYEVENTF_KEYUP);
    add_key(VK_CONTROL, KEYEVENTF_KEYUP);
    for (const WORD vk : physically_down) {
        if (ShouldRestoreModifierAfterPaste(vk, true)) {
            add_key(vk);
        }
    }
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

bool PasteController::SetText(const HistoryItem& item) const {
    const std::wstring text = !item.text.empty() ? item.text : item.search_text;
    if (!SetText(text)) {
        return false;
    }

    if (item.kind == ClipboardKind::Html && !item.html.empty()) {
        const UINT html_format = RegisterClipboardFormatW(L"HTML Format");
        HGLOBAL html_handle = AllocMoveableBytes(item.html.data(), item.html.size() + 1);
        if (html_handle && !SetClipboardData(html_format, html_handle)) {
            GlobalFree(html_handle);
        }
    }
    return true;
}

bool PasteController::SetText(std::wstring_view text) const {
    HGLOBAL text_handle = AllocWideText(text);
    if (!text_handle) {
        return false;
    }
    if (!SetClipboardData(CF_UNICODETEXT, text_handle)) {
        GlobalFree(text_handle);
        return false;
    }
    return true;
}

bool PasteController::SetFiles(const HistoryItem& item) const {
    return SetFiles(item.files);
}

bool PasteController::SetFiles(const std::vector<std::wstring>& files) const {
    size_t path_chars = 1;
    for (const auto& file : files) {
        path_chars += file.size() + 1;
    }
    const size_t bytes = sizeof(DROPFILES) + path_chars * sizeof(wchar_t);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!handle) {
        return false;
    }

    auto* drop = static_cast<DROPFILES*>(GlobalLock(handle));
    if (!drop) {
        GlobalFree(handle);
        return false;
    }
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    auto* cursor = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES));
    for (const auto& file : files) {
        memcpy(cursor, file.c_str(), file.size() * sizeof(wchar_t));
        cursor += file.size() + 1;
    }
    *cursor = L'\0';
    GlobalUnlock(handle);

    if (!SetClipboardData(CF_HDROP, handle)) {
        GlobalFree(handle);
        return false;
    }
    return true;
}

bool PasteController::SetImage(const HistoryItem& item) const {
    if (item.payload_path.empty()) {
        return false;
    }
    std::ifstream file(item.payload_path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<char> bytes(size);
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));

    HGLOBAL handle = AllocMoveableBytes(bytes.data(), bytes.size());
    if (!handle) {
        return false;
    }
    if (!SetClipboardData(CF_DIB, handle)) {
        GlobalFree(handle);
        return false;
    }
    return true;
}

} // namespace ClipSoul
