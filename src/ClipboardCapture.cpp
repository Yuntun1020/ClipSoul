#include "ClipSoul/ClipboardCapture.h"

#include "ClipSoul/TextUtil.h"

#include <Windows.h>
#include <shellapi.h>

#include <fstream>
#include <sstream>

namespace ClipSoul {
namespace {

class OpenClipboardScope {
public:
    explicit OpenClipboardScope(HWND owner) : opened_(OpenClipboard(owner) != FALSE) {}
    ~OpenClipboardScope() {
        if (opened_) {
            CloseClipboard();
        }
    }
    bool opened() const { return opened_; }

private:
    bool opened_;
};

std::wstring ReadGlobalWideText(UINT format) {
    HGLOBAL handle = GetClipboardData(format);
    if (!handle) {
        return {};
    }
    const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle));
    if (!text) {
        return {};
    }
    std::wstring result(text);
    GlobalUnlock(handle);
    return result;
}

std::string ReadGlobalBytesAsString(UINT format) {
    HGLOBAL handle = GetClipboardData(format);
    if (!handle) {
        return {};
    }
    const auto size = GlobalSize(handle);
    const char* data = static_cast<const char*>(GlobalLock(handle));
    if (!data) {
        return {};
    }
    std::string result(data, data + size);
    GlobalUnlock(handle);
    const auto null_pos = result.find('\0');
    if (null_pos != std::string::npos) {
        result.resize(null_pos);
    }
    return result;
}

std::wstring JoinPreviewFiles(const std::vector<std::wstring>& files) {
    std::wstring preview;
    const size_t count = std::min<size_t>(files.size(), 3);
    for (size_t i = 0; i < count; ++i) {
        if (i != 0) {
            preview += L", ";
        }
        preview += FileNameFromPath(files[i]);
    }
    if (files.size() > count) {
        preview += L" ...";
    }
    return preview;
}

} // namespace

ClipboardCapture::ClipboardCapture(std::filesystem::path cache_dir)
    : cache_dir_(std::move(cache_dir)) {
    std::filesystem::create_directories(cache_dir_);
}

std::optional<CapturedContent> ClipboardCapture::Capture(HWND owner) {
    OpenClipboardScope clipboard(owner);
    if (!clipboard.opened()) {
        return std::nullopt;
    }

    const UINT html_format = RegisterClipboardFormatW(L"HTML Format");
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        return CaptureFiles();
    }
    if (html_format && IsClipboardFormatAvailable(html_format)) {
        return CaptureHtml(html_format);
    }
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        return CaptureText();
    }
    if (IsClipboardFormatAvailable(CF_DIBV5) || IsClipboardFormatAvailable(CF_DIB)) {
        return CaptureImage();
    }
    return std::nullopt;
}

std::optional<CapturedContent> ClipboardCapture::CaptureFiles() const {
    HDROP drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    if (!drop) {
        return std::nullopt;
    }

    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    if (count == 0) {
        return std::nullopt;
    }

    CapturedContent content;
    content.kind = ClipboardKind::Files;
    for (UINT i = 0; i < count; ++i) {
        const UINT length = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(length, L'\0');
        DragQueryFileW(drop, i, path.data(), length + 1);
        content.files.push_back(path);
        if (!content.search_text.empty()) {
            content.search_text.push_back(L' ');
        }
        content.search_text += path;
        content.search_text.push_back(L' ');
        content.search_text += FileNameFromPath(path);
    }
    content.preview = JoinPreviewFiles(content.files);
    content.content_hash = StableHash(content.search_text);
    return content;
}

std::optional<CapturedContent> ClipboardCapture::CaptureImage() const {
    const UINT format = IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5 : CF_DIB;
    HGLOBAL handle = GetClipboardData(format);
    if (!handle) {
        return std::nullopt;
    }

    CapturedContent content;
    content.kind = ClipboardKind::Image;
    content.payload_path = SaveDibPayload(handle);
    content.preview = L"Image / screenshot";
    content.search_text = L"image screenshot";
    content.content_hash = StableHash(content.payload_path.wstring());
    return content;
}

std::optional<CapturedContent> ClipboardCapture::CaptureHtml(UINT html_format) const {
    CapturedContent content;
    content.kind = ClipboardKind::Html;
    content.html = ReadGlobalBytesAsString(html_format);
    content.text = ReadGlobalWideText(CF_UNICODETEXT);
    const auto html_text = HtmlClipboardToPlainText(content.html);
    content.search_text = NormalizeWhitespace(content.text.empty() ? html_text : content.text);
    const auto visible_text = NormalizeWhitespace(content.text.empty() ? html_text : content.text);
    if (LooksLikeUrl(visible_text)) {
        content.kind = ClipboardKind::Link;
    }
    content.preview = content.search_text.empty() ? L"HTML content" : content.search_text;
    if (content.preview.size() > 180) {
        content.preview.resize(180);
        content.preview += L"...";
    }
    content.content_hash = StableHash(Utf8ToWide(content.html) + content.text);
    return content.content_hash.empty() ? std::nullopt : std::optional<CapturedContent>(std::move(content));
}

std::optional<CapturedContent> ClipboardCapture::CaptureText() const {
    CapturedContent content;
    content.text = ReadGlobalWideText(CF_UNICODETEXT);
    content.kind = LooksLikeUrl(content.text) ? ClipboardKind::Link : ClipboardKind::Text;
    content.search_text = NormalizeWhitespace(content.text);
    content.preview = content.search_text;
    if (content.preview.size() > 180) {
        content.preview.resize(180);
        content.preview += L"...";
    }
    content.content_hash = StableHash(content.text);
    return content.text.empty() ? std::nullopt : std::optional<CapturedContent>(std::move(content));
}

std::filesystem::path ClipboardCapture::SaveDibPayload(HGLOBAL handle) const {
    const SIZE_T size = GlobalSize(handle);
    const void* data = GlobalLock(handle);
    if (!data || size == 0) {
        return {};
    }

    const auto hash = StableHashBytes(data, size);
    const auto path = cache_dir_ / (hash + L".dib");
    if (!std::filesystem::exists(path)) {
        std::ofstream file(path, std::ios::binary);
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    GlobalUnlock(handle);
    return path;
}

} // namespace ClipSoul
