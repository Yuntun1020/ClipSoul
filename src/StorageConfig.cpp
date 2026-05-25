#include "ClipSoul/StorageConfig.h"

#include <cwctype>

namespace ClipSoul {

std::wstring TrimStoragePath(std::wstring_view value) {
    size_t first = 0;
    while (first < value.size() && std::iswspace(value[first])) {
        ++first;
    }

    size_t last = value.size();
    while (last > first && std::iswspace(value[last - 1])) {
        --last;
    }

    return std::wstring(value.substr(first, last - first));
}

std::filesystem::path ResolveStorageDirectory(std::wstring_view configured_path,
                                              const std::filesystem::path& default_dir) {
    const auto trimmed = TrimStoragePath(configured_path);
    return trimmed.empty() ? default_dir : std::filesystem::path(trimmed);
}

std::wstring SerializeStorageDirectory(const std::filesystem::path& storage_dir) {
    return storage_dir.wstring();
}

} // namespace ClipSoul
