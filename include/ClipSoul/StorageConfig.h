#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ClipSoul {

std::wstring TrimStoragePath(std::wstring_view value);
std::filesystem::path ResolveStorageDirectory(std::wstring_view configured_path,
                                              const std::filesystem::path& default_dir);
std::wstring SerializeStorageDirectory(const std::filesystem::path& storage_dir);

} // namespace ClipSoul
