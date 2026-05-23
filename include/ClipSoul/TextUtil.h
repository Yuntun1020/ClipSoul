#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ClipSoul {

std::wstring NormalizeWhitespace(std::wstring_view input);
std::wstring Utf8ToWide(std::string_view input);
std::string WideToUtf8(std::wstring_view input);
std::wstring HtmlClipboardToPlainText(std::string_view cf_html);
std::wstring StripHtmlToText(std::wstring_view html);
std::wstring StableHash(std::wstring_view input);
std::wstring StableHashBytes(const void* data, size_t size);
std::wstring FileNameFromPath(std::wstring_view path);
bool LooksLikeUrl(std::wstring_view input);

} // namespace ClipSoul
