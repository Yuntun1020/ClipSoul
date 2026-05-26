#include "TestHarness.h"

#include "ClipSoul/TextUtil.h"

TEST_CASE(NormalizeWhitespaceCollapsesRuns) {
    REQUIRE_EQ(ClipSoul::NormalizeWhitespace(L"  alpha\r\n\t beta   gamma  "),
               std::wstring(L"alpha beta gamma"));
}

TEST_CASE(NormalizeEditableNotePreservesLineBreaks) {
    REQUIRE_EQ(ClipSoul::NormalizeEditableNote(L"  first line\r\n  second\tline \r third  "),
               std::wstring(L"first line\nsecond line\nthird"));
    REQUIRE_EQ(ClipSoul::NormalizeEditableNote(L"first\r\n\r\nthird"),
               std::wstring(L"first\n\nthird"));
    REQUIRE_EQ(ClipSoul::NormalizeEditableNote(L"first\u2028second\u2029third"),
               std::wstring(L"first\nsecond\nthird"));
}

TEST_CASE(MultilineEditTextRoundTripsStoredNotes) {
    const std::wstring stored = L"first line\nsecond line\nthird";
    const auto edit_text = ClipSoul::TextForMultilineEdit(stored);

    REQUIRE_EQ(edit_text, std::wstring(L"first line\r\nsecond line\r\nthird"));
    REQUIRE_EQ(ClipSoul::NormalizeEditableNote(edit_text), stored);
    REQUIRE_EQ(ClipSoul::TextForMultilineEdit(L"first\u2028second\u2029third"),
               std::wstring(L"first\r\nsecond\r\nthird"));
}

TEST_CASE(HtmlToPlainTextPrefersCfHtmlFragment) {
    const std::string html =
        "Version:0.9\r\n"
        "StartHTML:00000097\r\n"
        "EndHTML:00000195\r\n"
        "StartFragment:00000131\r\n"
        "EndFragment:00000159\r\n"
        "<html><body><!--StartFragment--><b>Hello</b>&nbsp;ClipSoul<!--EndFragment--></body></html>";

    REQUIRE_EQ(ClipSoul::HtmlClipboardToPlainText(html), std::wstring(L"Hello ClipSoul"));
}

TEST_CASE(StableHashChangesWhenInputChanges) {
    REQUIRE(ClipSoul::StableHash(L"same") == ClipSoul::StableHash(L"same"));
    REQUIRE(ClipSoul::StableHash(L"same") != ClipSoul::StableHash(L"different"));
}

TEST_CASE(DetectsHttpLinks) {
    REQUIRE(ClipSoul::LooksLikeUrl(L"https://example.com/path"));
    REQUIRE(ClipSoul::LooksLikeUrl(L"https://blog.csdn.net/libra1_long_article/details/123456789?spm=1001.2014.3001.5501#comments"));
    REQUIRE(ClipSoul::LooksLikeUrl(L" https://example.com/a/b/c?x=1&y=two#frag "));
    REQUIRE(ClipSoul::LooksLikeUrl(L"https://example.com/path."));
    REQUIRE(ClipSoul::LooksLikeUrl(L"http://localhost:3000"));
    REQUIRE(!ClipSoul::LooksLikeUrl(L"not a url"));
    REQUIRE(!ClipSoul::LooksLikeUrl(L"https://example.com/path extra words"));
}

TEST_CASE(FileNameFromPathPreservesExtension) {
    REQUIRE_EQ(ClipSoul::FileNameFromPath(L"C:\\Work\\会议纪要.docx"), std::wstring(L"会议纪要.docx"));
    REQUIRE_EQ(ClipSoul::FileNameFromPath(L"预算表.xlsx"), std::wstring(L"预算表.xlsx"));
}
