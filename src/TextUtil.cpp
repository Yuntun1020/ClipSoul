#include "ClipSoul/TextUtil.h"

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cwctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace ClipSoul {
namespace {

bool IsWhitespace(wchar_t ch) {
    return std::iswspace(ch) != 0;
}

std::wstring DecodeHtmlEntity(std::wstring_view entity) {
    if (entity == L"amp") return L"&";
    if (entity == L"lt") return L"<";
    if (entity == L"gt") return L">";
    if (entity == L"quot") return L"\"";
    if (entity == L"apos") return L"'";
    if (entity == L"nbsp") return L" ";

    if (entity.size() > 1 && entity[0] == L'#') {
        int base = 10;
        size_t offset = 1;
        if (entity.size() > 2 && (entity[1] == L'x' || entity[1] == L'X')) {
            base = 16;
            offset = 2;
        }

        uint32_t value = 0;
        for (size_t i = offset; i < entity.size(); ++i) {
            const wchar_t ch = entity[i];
            uint32_t digit = 0;
            if (ch >= L'0' && ch <= L'9') {
                digit = static_cast<uint32_t>(ch - L'0');
            } else if (base == 16 && ch >= L'a' && ch <= L'f') {
                digit = static_cast<uint32_t>(ch - L'a' + 10);
            } else if (base == 16 && ch >= L'A' && ch <= L'F') {
                digit = static_cast<uint32_t>(ch - L'A' + 10);
            } else {
                return std::wstring(L"&") + std::wstring(entity) + L";";
            }
            if (digit >= static_cast<uint32_t>(base)) {
                return std::wstring(L"&") + std::wstring(entity) + L";";
            }
            value = value * static_cast<uint32_t>(base) + digit;
        }

        if (value <= 0xFFFF) {
            return std::wstring(1, static_cast<wchar_t>(value));
        }
    }

    return std::wstring(L"&") + std::wstring(entity) + L";";
}

std::wstring ExtractCfHtmlFragment(std::string_view cf_html) {
    const auto header_end = cf_html.find("\r\n\r\n");
    const auto header = header_end == std::string_view::npos ? cf_html : cf_html.substr(0, header_end);

    auto read_offset = [&](std::string_view key) -> std::optional<size_t> {
        const auto key_pos = header.find(key);
        if (key_pos == std::string_view::npos) {
            return std::nullopt;
        }
        size_t value_pos = key_pos + key.size();
        while (value_pos < header.size() && header[value_pos] == ' ') {
            ++value_pos;
        }
        size_t value_end = value_pos;
        while (value_end < header.size() && header[value_end] >= '0' && header[value_end] <= '9') {
            ++value_end;
        }
        size_t value = 0;
        const auto result = std::from_chars(header.data() + value_pos, header.data() + value_end, value);
        if (result.ec != std::errc{}) {
            return std::nullopt;
        }
        return value;
    };

    const auto start = read_offset("StartFragment:");
    const auto end = read_offset("EndFragment:");
    if (start && end && *start <= *end && *end <= cf_html.size()) {
        return Utf8ToWide(cf_html.substr(*start, *end - *start));
    }

    return Utf8ToWide(cf_html);
}

} // namespace

std::wstring NormalizeWhitespace(std::wstring_view input) {
    std::wstring result;
    result.reserve(input.size());
    bool pending_space = false;

    for (const wchar_t ch : input) {
        if (IsWhitespace(ch)) {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) {
            result.push_back(L' ');
            pending_space = false;
        }
        result.push_back(ch);
    }

    return result;
}

std::wstring Utf8ToWide(std::string_view input) {
    if (input.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                         static_cast<int>(input.size()), nullptr, 0);
    if (size <= 0) {
        const int fallback_size = MultiByteToWideChar(CP_ACP, 0, input.data(),
                                                      static_cast<int>(input.size()), nullptr, 0);
        std::wstring fallback(static_cast<size_t>(fallback_size), L'\0');
        MultiByteToWideChar(CP_ACP, 0, input.data(), static_cast<int>(input.size()),
                            fallback.data(), fallback_size);
        return fallback;
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                        static_cast<int>(input.size()), result.data(), size);
    return result;
}

std::string WideToUtf8(std::wstring_view input) {
    if (input.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring StripHtmlToText(std::wstring_view html) {
    std::wstring result;
    result.reserve(html.size());
    bool in_tag = false;

    for (size_t i = 0; i < html.size(); ++i) {
        const wchar_t ch = html[i];
        if (ch == L'<') {
            in_tag = true;
            result.push_back(L' ');
            continue;
        }
        if (ch == L'>') {
            in_tag = false;
            continue;
        }
        if (in_tag) {
            continue;
        }
        if (ch == L'&') {
            const auto semicolon = html.substr(i + 1).find(L';');
            if (semicolon != std::wstring_view::npos && semicolon < 16) {
                result += DecodeHtmlEntity(html.substr(i + 1, semicolon));
                i += semicolon + 1;
                continue;
            }
        }
        result.push_back(ch);
    }

    return NormalizeWhitespace(result);
}

std::wstring HtmlClipboardToPlainText(std::string_view cf_html) {
    return StripHtmlToText(ExtractCfHtmlFragment(cf_html));
}

std::wstring StableHash(std::wstring_view input) {
    uint64_t hash = 1469598103934665603ull;
    for (const wchar_t ch : input) {
        const auto value = static_cast<uint32_t>(ch);
        hash ^= value & 0xFFu;
        hash *= 1099511628211ull;
        hash ^= (value >> 8u) & 0xFFu;
        hash *= 1099511628211ull;
    }

    std::wstringstream stream;
    stream << std::hex << std::setw(16) << std::setfill(L'0') << hash;
    return stream.str();
}

std::wstring StableHashBytes(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }

    std::wstringstream stream;
    stream << std::hex << std::setw(16) << std::setfill(L'0') << hash;
    return stream.str();
}

std::wstring FileNameFromPath(std::wstring_view path) {
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring_view::npos) {
        return std::wstring(path);
    }
    return std::wstring(path.substr(slash + 1));
}

bool LooksLikeUrl(std::wstring_view input) {
    const auto value = NormalizeWhitespace(input);
    if (value.rfind(L"https://", 0) == 0 || value.rfind(L"http://", 0) == 0) {
        return value.find(L'.') != std::wstring::npos || value.find(L"localhost") != std::wstring::npos;
    }
    return false;
}

} // namespace ClipSoul
