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
    // Close early so the caller can do heavy local work without holding the clipboard.
    void Close() {
        if (opened_) {
            CloseClipboard();
            opened_ = false;
        }
    }

private:
    bool opened_;
};

std::wstring ReadGlobalWideText(UINT format) {
    HGLOBAL handle = GetClipboardData(format);
    if (!handle) {
        return {};
    }
    const SIZE_T bytes = GlobalSize(handle);
    const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle));
    if (!text) {
        return {};
    }
    // Do not trust the source app to null-terminate: bound the read by the allocation size.
    const wchar_t* end = text + bytes / sizeof(wchar_t);
    std::wstring result(text, std::find(text, end, L'\0'));
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

std::wstring TruncatePreview(std::wstring preview) {
    constexpr size_t kPreviewChars = 180;
    if (preview.size() > kPreviewChars) {
        preview.resize(kPreviewChars);
        preview += L"...";
    }
    return preview;
}

void FinalizeTextContent(CapturedContent& content) {
    content.kind = LooksLikeUrl(content.text) ? ClipboardKind::Link : ClipboardKind::Text;
    content.search_text = NormalizeWhitespace(content.text);
    content.preview = TruncatePreview(content.search_text);
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

// 把剪贴板里的 DIB 复制到本地缓冲（必须在剪贴板仍然打开时做），然后立刻解锁。
// 哈希和写盘是这一步之后的纯本地工作，不需要再占着剪贴板不放。
std::vector<char> CopyGlobalBytes(HGLOBAL handle) {
    const SIZE_T size = GlobalSize(handle);
    if (size < sizeof(BITMAPINFOHEADER)) {
        return {};
    }
    const void* data = GlobalLock(handle);
    if (!data) {
        return {};
    }
    std::vector<char> bytes(static_cast<size_t>(size));
    memcpy(bytes.data(), data, static_cast<size_t>(size));
    GlobalUnlock(handle);
    return bytes;
}

std::filesystem::path SaveDibPayload(const std::filesystem::path& cache_dir, HGLOBAL handle) {
    const SIZE_T size = GlobalSize(handle);
    const void* data = GlobalLock(handle);
    if (!data) {
        return {};
    }
    if (size < sizeof(BITMAPINFOHEADER)) {
        GlobalUnlock(handle);
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

std::vector<char> BitmapToDibBytes(HBITMAP bitmap) {
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

    return dib;
}

std::vector<char> DecodePngToDib(HGLOBAL handle) {
    if (!handle) {
        return {};
    }
    const SIZE_T size = GlobalSize(handle);
    const void* data = GlobalLock(handle);
    if (!data) {
        return {};
    }
    if (size == 0) {
        GlobalUnlock(handle);
        return {};
    }

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    std::vector<char> dib;

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
            dib.resize(sizeof(BITMAPINFOHEADER) + static_cast<size_t>(stride) * height);
            auto* header = reinterpret_cast<BITMAPINFOHEADER*>(dib.data());
            header->biSize = sizeof(BITMAPINFOHEADER);
            header->biWidth = static_cast<LONG>(width);
            header->biHeight = -static_cast<LONG>(height);
            header->biPlanes = 1;
            header->biBitCount = 32;
            header->biCompression = BI_RGB;
            header->biSizeImage = stride * height;

            if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(dib.size() - sizeof(BITMAPINFOHEADER)),
                                             reinterpret_cast<BYTE*>(dib.data() + sizeof(BITMAPINFOHEADER))))) {
                dib.clear();
            }
        }
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    GlobalUnlock(handle);
    return dib;
}

} // namespace

ClipboardCapture::ClipboardCapture(std::filesystem::path cache_dir)
    : cache_dir_(std::move(cache_dir)) {
    std::filesystem::create_directories(cache_dir_);
}

std::optional<CapturedContent> ClipboardCapture::Capture(HWND owner, bool* clipboard_busy) {
    if (clipboard_busy) {
        *clipboard_busy = false;
    }

    static const UINT html_format = RegisterClipboardFormatW(L"HTML Format");
    static const UINT rtf_format = RegisterClipboardFormatW(L"Rich Text Format");
    static const UINT png_format = RegisterClipboardFormatW(L"PNG");
    // Convention used by password managers etc. to ask clipboard tools not to record the content.
    static const UINT exclude_format = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");

    // IsClipboardFormatAvailable does not require the clipboard to be open, so check the cheap
    // things first and only open the clipboard when there is something worth reading.
    if (exclude_format != 0 && IsClipboardFormatAvailable(exclude_format)) {
        return std::nullopt;
    }

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

    if (ChooseClipboardCapturePlan(formats) == ClipboardCapturePlan::None) {
        return std::nullopt;
    }

    OpenClipboardScope clipboard(owner);
    if (!clipboard.opened()) {
        // Another process still holds the clipboard (very common right after a copy, e.g. the
        // source app or another clipboard tool). Let the caller retry instead of dropping the copy.
        if (clipboard_busy) {
            *clipboard_busy = true;
        }
        return std::nullopt;
    }

    // Try the preferred format first; if reading it fails (delayed rendering failed, empty data,
    // HTML without visible text...), fall back to the next available format instead of losing the copy.
    for (auto plan = ChooseClipboardCapturePlan(formats); plan != ClipboardCapturePlan::None;
         plan = ChooseClipboardCapturePlan(formats)) {
        std::optional<CapturedContent> content;
        switch (plan) {
        case ClipboardCapturePlan::Files:
            content = CaptureFiles();
            break;
        case ClipboardCapturePlan::Html:
            content = CaptureHtml(html_format);
            break;
        case ClipboardCapturePlan::UnicodeText:
            content = CaptureText();
            break;
        case ClipboardCapturePlan::AnsiText:
            content = CaptureText(CF_TEXT, CP_ACP);
            break;
        case ClipboardCapturePlan::OemText:
            content = CaptureText(CF_OEMTEXT, CP_OEMCP);
            break;
        case ClipboardCapturePlan::Rtf:
            content = CaptureRtf(rtf_format);
            break;
        case ClipboardCapturePlan::Dib:
            content = CaptureImage();
            break;
        case ClipboardCapturePlan::Bitmap:
            content = CaptureBitmap();
            break;
        case ClipboardCapturePlan::Png:
            content = CapturePng(png_format);
            break;
        case ClipboardCapturePlan::None:
            break;
        }
        if (content) {
            // 图片先把 DIB 字节复制到内存（此时剪贴板还锁着），然后在下面的 scope 析构、
            // 剪贴板释放之后再哈希 + 写盘：不把一个 20~30 MB 的截图全文哈希和 IO
            // 都压在“别的程序打不开剪贴板”的这段窗口里。
            if (!content->pending_dib.empty()) {
                std::vector<char> dib = std::move(content->pending_dib);
                content->pending_dib.clear();
                clipboard.Close();
                content->payload_path = SaveDibBytes(cache_dir_, dib);
                if (content->payload_path.empty()) {
                    return std::nullopt;
                }
                content->content_hash = StableHash(content->payload_path.wstring());
            }
            return content;
        }
        MarkClipboardCapturePlanUnavailable(formats, plan);
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
    content.pending_dib = CopyGlobalBytes(handle);
    if (content.pending_dib.empty()) {
        return std::nullopt;
    }
    content.preview = L"Image / screenshot";
    content.search_text = L"image screenshot";
    return content;
}

std::optional<CapturedContent> ClipboardCapture::CaptureBitmap() const {
    HBITMAP bitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
    if (!bitmap) {
        return std::nullopt;
    }

    CapturedContent content;
    content.kind = ClipboardKind::Image;
    content.pending_dib = BitmapToDibBytes(bitmap);
    if (content.pending_dib.empty()) {
        return std::nullopt;
    }
    content.preview = L"Image / screenshot";
    content.search_text = L"image screenshot bitmap";
    return content;
}

std::optional<CapturedContent> ClipboardCapture::CapturePng(UINT png_format) const {
    CapturedContent content;
    content.kind = ClipboardKind::Image;
    content.pending_dib = DecodePngToDib(GetClipboardData(png_format));
    if (content.pending_dib.empty()) {
        return std::nullopt;
    }
    content.preview = L"Image / screenshot";
    content.search_text = L"image screenshot png";
    return content;
}

std::optional<CapturedContent> ClipboardCapture::CaptureHtml(UINT html_format) const {
    CapturedContent content;
    content.kind = ClipboardKind::Html;
    content.html = ReadGlobalBytesAsString(html_format);
    if (content.html.empty()) {
        return std::nullopt; // fall back to plain text
    }
    content.text = ReadGlobalWideText(CF_UNICODETEXT);
    const auto visible_text = NormalizeWhitespace(content.text.empty() ? HtmlClipboardToPlainText(content.html)
                                                                       : content.text);
    if (visible_text.empty()) {
        // e.g. a browser "copy image" puts an <img> fragment plus a bitmap; let the image path handle it.
        return std::nullopt;
    }
    if (content.text.empty()) {
        content.text = visible_text;
    }
    content.search_text = visible_text;
    if (LooksLikeUrl(visible_text)) {
        content.kind = ClipboardKind::Link;
    }
    content.preview = TruncatePreview(content.search_text);
    // Hash only the visible text: the CF_HTML header (offsets / SourceURL) differs between copies of
    // the same content, which used to produce duplicate history entries.
    content.content_hash = StableHash(content.text);
    return content;
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
    return content.search_text.empty() ? std::nullopt : std::optional<CapturedContent>(std::move(content));
}

std::optional<CapturedContent> ClipboardCapture::CaptureText(UINT format, UINT code_page) const {
    CapturedContent content;
    content.text = ReadGlobalText(format, code_page);
    FinalizeTextContent(content);
    return content.search_text.empty() ? std::nullopt : std::optional<CapturedContent>(std::move(content));
}

} // namespace ClipSoul
