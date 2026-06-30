#pragma once

#include "ClipSoul/HistoryStore.h"

#include <Windows.h>

#include <filesystem>
#include <optional>

namespace ClipSoul {

class ClipboardCapture {
public:
    explicit ClipboardCapture(std::filesystem::path cache_dir);

    std::optional<CapturedContent> Capture(HWND owner);

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
