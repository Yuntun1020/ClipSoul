#include "ClipSoul/ClipboardCapture.h"

#include "ClipSoul/ClipboardFormats.h"
#include "ClipSoul/TextUtil.h"

#include <Windows.h>
#include <wincodec.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>

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

std::vector<char> ReadGlobalBytes(UINT format) {
    HGLOBAL handle = GetClipboardData(format);
    if (!handle) {
        return {};
    }
    const auto size = GlobalSize(handle);
    const char* data = static_cast<const char*>(GlobalLock(handle));
    if (!data) {
        return {};
    }
    std::vector<char> result(data, data + size);
    GlobalUnlock(handle);
    return result;
}

std::string ReadGlobalBytesAsString(UINT format) {
    auto bytes = ReadGlobalBytes(format);
    std::string result(bytes.begin(), bytes.end());
    const auto null_pos = result.find('\0');
    if (null_pos != std::string::npos) {
        result.resize(null_pos);
    }
    return result;
}

std::wstring ReadGlobalText(UINT format, UINT code_page) {
    const auto bytes = ReadGlobalBytes(format);
    if (bytes.empty()) {
        return {};
    }

    auto end = std::find(bytes.begin(), bytes.end(), '\0');
    const int length = static_cast<int>(std::distance(bytes.begin(), end));
    if (length <= 0) {
        return {};
    }

    const int wide_length = MultiByteToWideChar(code_page, 0, bytes.data(), length, nullptr, 0);
    if (wide_length <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(wide_length), L'\0');
    MultiByteToWideChar(code_page, 0, bytes.data(), length, result.data(), wide_length);
    return result;
}

std::wstring DecodeRtfEscapedByte(unsigned value) {
    const char byte = static_cast<char>(value);
    int size = MultiByteToWideChar(CP_ACP, 0, &byte, 1, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_ACP, 0, &byte, 1, result.data(), size);
    return result;
}

int HexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

std::wstring RtfToPlainText(std::string_view rtf) {
    std::wstring plain;
    plain.reserve(rtf.size());

    for (size_t i = 0; i < rtf.size(); ++i) {
        const char ch = rtf[i];
        if (ch == '{' || ch == '}') {
            continue;
        }
        if (ch != '\\') {
            if (static_cast<unsigned char>(ch) >= 0x20 || ch == '\n' || ch == '\r' || ch == '\t') {
                plain += DecodeRtfEscapedByte(static_cast<unsigned char>(ch));
            }
            continue;
        }

        if (i + 1 >= rtf.size()) {
            break;
        }
        const char next = rtf[++i];
        if (next == '\\' || next == '{' || next == '}') {
            plain.push_back(static_cast<wchar_t>(next));
            continue;
        }
        if (next == '\'' && i + 2 < rtf.size()) {
            const int hi = HexValue(rtf[i + 1]);
            const int lo = HexValue(rtf[i + 2]);
            if (hi >= 0 && lo >= 0) {
                plain += DecodeRtfEscapedByte(static_cast<unsigned>((hi << 4) | lo));
                i += 2;
            }
            continue;
        }
        if (!std::isalpha(static_cast<unsigned char>(next))) {
            if (next == '~') {
                plain.push_back(L' ');
            } else if (next == '-') {
                plain.push_back(L'\u00ad');
            } else if (next == '_') {
                plain.push_back(L'\u2011');
            }
            continue;
        }

        std::string word;
        word.push_back(next);
        while (i + 1 < rtf.size() && std::isalpha(static_cast<unsigned char>(rtf[i + 1]))) {
            word.push_back(rtf[++i]);
        }

        int sign = 1;
        int value = 0;
        bool has_value = false;
        if (i + 1 < rtf.size() && rtf[i + 1] == '-') {
            sign = -1;
            ++i;
        }
        while (i + 1 < rtf.size() && std::isdigit(static_cast<unsigned char>(rtf[i + 1]))) {
            has_value = true;
            value = value * 10 + (rtf[++i] - '0');
        }
        value *= sign;

        if (i + 1 < rtf.size() && rtf[i + 1] == ' ') {
            ++i;
        }

        if (word == "par" || word == "line") {
            plain.push_back(L'\n');
        } else if (word == "tab") {
            plain.push_back(L'\t');
        } else if (word == "u" && has_value) {
            plain.push_back(static_cast<wchar_t>(value < 0 ? value + 65536 : value));
            if (i + 1 < rtf.size() && rtf[i + 1] != '\\' && rtf[i + 1] != '{' && rtf[i + 1] != '}') {
                ++i;
            }
        }
    }

    return NormalizeWhitespace(plain);
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

void FinalizeTextContent(CapturedContent& content) {
    content.kind = LooksLikeUrl(content.text) ? ClipboardKind::Link : ClipboardKind::Text;
    content.search_text = NormalizeWhitespace(content.text);
    content.preview = content.search_text;
    if (content.preview.size() > 180) {
        content.preview.resize(180);
        content.preview += L"...";
    }
    content.content_hash = StableHash(content.text);
}

std::filesystem::path SaveDibBytes(const std::filesystem::path& cache_dir, const std::vector<char>& dib) {
    if (dib.empty()) {
        return {};
    }
    const auto hash = StableHashBytes(dib.data(), dib.size());
    const auto path = cache_dir / (hash + L".dib");
    if (!std::filesystem::exists(path)) {
        std::ofstream file(path, std::ios::binary);
        file.write(dib.data(), static_cast<std::streamsize>(dib.size()));
    }
    return path;
}

std::filesystem::path SaveDibPayload(const std::filesystem::path& cache_dir, HGLOBAL handle) {
    const SIZE_T size = GlobalSize(handle);
    const void* data = GlobalLock(handle);
    if (!data || size == 0) {
        return {};
    }

    const auto hash = StableHashBytes(data, size);
    const auto path = cache_dir / (hash + L".dib");
    if (!std::filesystem::exists(path)) {
        std::ofstream file(path, std::ios::binary);
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    GlobalUnlock(handle);
    return path;
}

std::filesystem::path SaveBitmapPayload(const std::filesystem::path& cache_dir, HBITMAP bitmap) {
    BITMAP bitmap_info{};
    if (GetObjectW(bitmap, sizeof(bitmap_info), &bitmap_info) == 0 || bitmap_info.bmWidth <= 0 ||
        bitmap_info.bmHeight <= 0) {
        return {};
    }

    BITMAPINFO dib_info{};
    dib_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dib_info.bmiHeader.biWidth = bitmap_info.bmWidth;
    dib_info.bmiHeader.biHeight = -bitmap_info.bmHeight;
    dib_info.bmiHeader.biPlanes = 1;
    dib_info.bmiHeader.biBitCount = 32;
    dib_info.bmiHeader.biCompression = BI_RGB;

    const size_t stride = static_cast<size_t>(bitmap_info.bmWidth) * 4u;
    const size_t pixel_bytes = stride * static_cast<size_t>(bitmap_info.bmHeight);
    std::vector<char> dib(sizeof(BITMAPINFOHEADER) + pixel_bytes);
    memcpy(dib.data(), &dib_info.bmiHeader, sizeof(BITMAPINFOHEADER));

    HDC dc = GetDC(nullptr);
    if (!dc) {
        return {};
    }
    const int rows = GetDIBits(dc, bitmap, 0, static_cast<UINT>(bitmap_info.bmHeight),
                               dib.data() + sizeof(BITMAPINFOHEADER), &dib_info, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (rows == 0) {
        return {};
    }

    return SaveDibBytes(cache_dir, dib);
}

std::filesystem::path SavePngPayload(const std::filesystem::path& cache_dir, HGLOBAL handle) {
    if (!handle) {
        return {};
    }
    const SIZE_T size = GlobalSize(handle);
    const void* data = GlobalLock(handle);
    if (!data || size == 0) {
        return {};
    }

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    std::filesystem::path path;

    const HRESULT hr_factory =
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr_factory) && SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), static_cast<DWORD>(size))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeMedianCut))) {
        UINT width = 0;
        UINT height = 0;
        if (SUCCEEDED(converter->GetSize(&width, &height)) && width > 0 && height > 0) {
            const UINT stride = width * 4u;
            std::vector<char> dib(sizeof(BITMAPINFOHEADER) + static_cast<size_t>(stride) * height);
            auto* header = reinterpret_cast<BITMAPINFOHEADER*>(dib.data());
            header->biSize = sizeof(BITMAPINFOHEADER);
            header->biWidth = static_cast<LONG>(width);
            header->biHeight = -static_cast<LONG>(height);
            header->biPlanes = 1;
            header->biBitCount = 32;
            header->biCompression = BI_RGB;
            header->biSizeImage = stride * height;

            if (SUCCEEDED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(dib.size() - sizeof(BITMAPINFOHEADER)),
                                                reinterpret_cast<BYTE*>(dib.data() + sizeof(BITMAPINFOHEADER))))) {
                path = SaveDibBytes(cache_dir, dib);
            }
        }
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    GlobalUnlock(handle);
    return path;
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
    const UINT rtf_format = RegisterClipboardFormatW(L"Rich Text Format");
    const UINT png_format = RegisterClipboardFormatW(L"PNG");

    ClipboardFormatAvailability formats;
    formats.has_files = IsClipboardFormatAvailable(CF_HDROP) != FALSE;
    formats.has_html = html_format != 0 && IsClipboardFormatAvailable(html_format) != FALSE;
    formats.has_unicode_text = IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
    formats.has_ansi_text = IsClipboardFormatAvailable(CF_TEXT) != FALSE;
    formats.has_oem_text = IsClipboardFormatAvailable(CF_OEMTEXT) != FALSE;
    formats.has_rtf = rtf_format != 0 && IsClipboardFormatAvailable(rtf_format) != FALSE;
    formats.has_dib_v5 = IsClipboardFormatAvailable(CF_DIBV5) != FALSE;
    formats.has_dib = IsClipboardFormatAvailable(CF_DIB) != FALSE;
    formats.has_bitmap = IsClipboardFormatAvailable(CF_BITMAP) != FALSE;
    formats.has_png = png_format != 0 && IsClipboardFormatAvailable(png_format) != FALSE;

    switch (ChooseClipboardCapturePlan(formats)) {
    case ClipboardCapturePlan::Files:
        return CaptureFiles();
    case ClipboardCapturePlan::Html:
        return CaptureHtml(html_format);
    case ClipboardCapturePlan::UnicodeText:
        return CaptureText();
    case ClipboardCapturePlan::AnsiText:
        return CaptureText(CF_TEXT, CP_ACP);
    case ClipboardCapturePlan::OemText:
        return CaptureText(CF_OEMTEXT, CP_OEMCP);
    case ClipboardCapturePlan::Rtf:
        return CaptureRtf(rtf_format);
    case ClipboardCapturePlan::Dib:
        return CaptureImage();
    case ClipboardCapturePlan::Bitmap:
        return CaptureBitmap();
    case ClipboardCapturePlan::Png:
        return CapturePng(png_format);
    case ClipboardCapturePlan::None:
        return std::nullopt;
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
    content.payload_path = SaveDibPayload(cache_dir_, handle);
    if (content.payload_path.empty()) {
        return std::nullopt;
    }
    content.preview = L"Image / screenshot";
    content.search_text = L"image screenshot";
    content.content_hash = StableHash(content.payload_path.wstring());
    return content;
}

std::optional<CapturedContent> ClipboardCapture::CaptureBitmap() const {
    HBITMAP bitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
    if (!bitmap) {
        return std::nullopt;
    }

    CapturedContent content;
    content.kind = ClipboardKind::Image;
    content.payload_path = SaveBitmapPayload(cache_dir_, bitmap);
    if (content.payload_path.empty()) {
        return std::nullopt;
    }
    content.preview = L"Image / screenshot";
    content.search_text = L"image screenshot bitmap";
    content.content_hash = StableHash(content.payload_path.wstring());
    return content;
}

std::optional<CapturedContent> ClipboardCapture::CapturePng(UINT png_format) const {
    CapturedContent content;
    content.kind = ClipboardKind::Image;
    content.payload_path = SavePngPayload(cache_dir_, GetClipboardData(png_format));
    if (content.payload_path.empty()) {
        return std::nullopt;
    }
    content.preview = L"Image / screenshot";
    content.search_text = L"image screenshot png";
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

std::optional<CapturedContent> ClipboardCapture::CaptureRtf(UINT rtf_format) const {
    const auto rtf = ReadGlobalBytesAsString(rtf_format);
    const auto plain = RtfToPlainText(rtf);
    if (plain.empty()) {
        return std::nullopt;
    }

    CapturedContent content;
    content.kind = ClipboardKind::Text;
    content.text = plain;
    FinalizeTextContent(content);
    return content;
}

std::optional<CapturedContent> ClipboardCapture::CaptureText() const {
    CapturedContent content;
    content.text = ReadGlobalWideText(CF_UNICODETEXT);
    FinalizeTextContent(content);
    return content.text.empty() ? std::nullopt : std::optional<CapturedContent>(std::move(content));
}

std::optional<CapturedContent> ClipboardCapture::CaptureText(UINT format, UINT code_page) const {
    CapturedContent content;
    content.text = ReadGlobalText(format, code_page);
    FinalizeTextContent(content);
    return content.text.empty() ? std::nullopt : std::optional<CapturedContent>(std::move(content));
}

} // namespace ClipSoul
