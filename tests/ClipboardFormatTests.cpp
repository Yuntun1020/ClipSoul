#include "TestHarness.h"

#include "ClipSoul/ClipboardFormats.h"

TEST_CASE(ClipboardCapturePlanPrefersStructuredFormatsBeforePlainText) {
    ClipSoul::ClipboardFormatAvailability formats;
    formats.has_files = true;
    formats.has_html = true;
    formats.has_unicode_text = true;
    formats.has_dib = true;

    REQUIRE_EQ(ClipSoul::ChooseClipboardCapturePlan(formats), ClipSoul::ClipboardCapturePlan::Files);

    formats.has_files = false;
    REQUIRE_EQ(ClipSoul::ChooseClipboardCapturePlan(formats), ClipSoul::ClipboardCapturePlan::Html);

    formats.has_html = false;
    REQUIRE_EQ(ClipSoul::ChooseClipboardCapturePlan(formats), ClipSoul::ClipboardCapturePlan::UnicodeText);
}

TEST_CASE(ClipboardCapturePlanAcceptsLegacyTextAndRtfWhenUnicodeIsMissing) {
    ClipSoul::ClipboardFormatAvailability formats;
    formats.has_ansi_text = true;
    formats.has_oem_text = true;
    formats.has_rtf = true;

    REQUIRE_EQ(ClipSoul::ChooseClipboardCapturePlan(formats), ClipSoul::ClipboardCapturePlan::AnsiText);

    formats.has_ansi_text = false;
    REQUIRE_EQ(ClipSoul::ChooseClipboardCapturePlan(formats), ClipSoul::ClipboardCapturePlan::OemText);

    formats.has_oem_text = false;
    REQUIRE_EQ(ClipSoul::ChooseClipboardCapturePlan(formats), ClipSoul::ClipboardCapturePlan::Rtf);
}

TEST_CASE(ClipboardCapturePlanAcceptsBitmapAndPngImageFormats) {
    ClipSoul::ClipboardFormatAvailability formats;
    formats.has_dib_v5 = true;
    formats.has_dib = true;
    formats.has_bitmap = true;
    formats.has_png = true;

    REQUIRE_EQ(ClipSoul::ChooseClipboardCapturePlan(formats), ClipSoul::ClipboardCapturePlan::Dib);

    formats.has_dib_v5 = false;
    formats.has_dib = false;
    REQUIRE_EQ(ClipSoul::ChooseClipboardCapturePlan(formats), ClipSoul::ClipboardCapturePlan::Bitmap);

    formats.has_bitmap = false;
    REQUIRE_EQ(ClipSoul::ChooseClipboardCapturePlan(formats), ClipSoul::ClipboardCapturePlan::Png);
}

TEST_CASE(ClipboardCapturePlanIgnoresUnknownOnlyFormats) {
    ClipSoul::ClipboardFormatAvailability formats;

    REQUIRE_EQ(ClipSoul::ChooseClipboardCapturePlan(formats), ClipSoul::ClipboardCapturePlan::None);
}
