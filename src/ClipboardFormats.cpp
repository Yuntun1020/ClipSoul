#include "ClipSoul/ClipboardFormats.h"

namespace ClipSoul {

ClipboardCapturePlan ChooseClipboardCapturePlan(const ClipboardFormatAvailability& formats) {
    if (formats.has_files) {
        return ClipboardCapturePlan::Files;
    }
    if (formats.has_html) {
        return ClipboardCapturePlan::Html;
    }
    if (formats.has_unicode_text) {
        return ClipboardCapturePlan::UnicodeText;
    }
    if (formats.has_ansi_text) {
        return ClipboardCapturePlan::AnsiText;
    }
    if (formats.has_oem_text) {
        return ClipboardCapturePlan::OemText;
    }
    if (formats.has_rtf) {
        return ClipboardCapturePlan::Rtf;
    }
    if (formats.has_dib_v5 || formats.has_dib) {
        return ClipboardCapturePlan::Dib;
    }
    if (formats.has_bitmap) {
        return ClipboardCapturePlan::Bitmap;
    }
    if (formats.has_png) {
        return ClipboardCapturePlan::Png;
    }
    return ClipboardCapturePlan::None;
}

} // namespace ClipSoul
