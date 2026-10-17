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

void MarkClipboardCapturePlanUnavailable(ClipboardFormatAvailability& formats, ClipboardCapturePlan plan) {
    switch (plan) {
    case ClipboardCapturePlan::Files: formats.has_files = false; break;
    case ClipboardCapturePlan::Html: formats.has_html = false; break;
    case ClipboardCapturePlan::UnicodeText: formats.has_unicode_text = false; break;
    case ClipboardCapturePlan::AnsiText: formats.has_ansi_text = false; break;
    case ClipboardCapturePlan::OemText: formats.has_oem_text = false; break;
    case ClipboardCapturePlan::Rtf: formats.has_rtf = false; break;
    case ClipboardCapturePlan::Dib:
        formats.has_dib_v5 = false;
        formats.has_dib = false;
        break;
    case ClipboardCapturePlan::Bitmap: formats.has_bitmap = false; break;
    case ClipboardCapturePlan::Png: formats.has_png = false; break;
    case ClipboardCapturePlan::None: break;
    }
}

} // namespace ClipSoul
