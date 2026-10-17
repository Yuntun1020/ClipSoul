#pragma once

#include "ClipSoul/HistoryStore.h"

#include <Windows.h>

#include <filesystem>
#include <optional>

namespace ClipSoul {

class ClipboardCapture {
public:
    explicit ClipboardCapture(std::filesystem::path cache_dir);

    // Returns std::nullopt when nothing could be captured. When the clipboard could not be opened
    // because another process holds it, *clipboard_busy is set so the caller can retry later.
    std::optional<CapturedContent> Capture(HWND owner, bool* clipboard_busy = nullptr);

private:
    std::optional<CapturedContent> CaptureText(UINT format, UINT code_page) const;
    std::optional<CapturedContent> CaptureFiles() const;
    std::optional<CapturedContent> CaptureImage() const;
    std::optional<CapturedContent> CaptureBitmap() const;
    std::optional<CapturedContent> CapturePng(UINT png_format) const;
    std::optional<CapturedContent> CaptureHtml(UINT html_format) const;
    std::optional<CapturedContent> CaptureRtf(UINT rtf_format) const;
    std::optional<CapturedContent> CaptureText() const;

    std::filesystem::path cache_dir_;
};

} // namespace ClipSoul
