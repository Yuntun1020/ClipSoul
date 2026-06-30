#pragma once

namespace ClipSoul {

struct ClipboardFormatAvailability {
    bool has_files = false;
    bool has_html = false;
    bool has_unicode_text = false;
    bool has_ansi_text = false;
    bool has_oem_text = false;
    bool has_rtf = false;
    bool has_dib_v5 = false;
    bool has_dib = false;
    bool has_bitmap = false;
    bool has_png = false;
};

enum class ClipboardCapturePlan {
    None,
    Files,
    Html,
    UnicodeText,
    AnsiText,
    OemText,
    Rtf,
    Dib,
    Bitmap,
    Png,
};

ClipboardCapturePlan ChooseClipboardCapturePlan(const ClipboardFormatAvailability& formats);

} // namespace ClipSoul
