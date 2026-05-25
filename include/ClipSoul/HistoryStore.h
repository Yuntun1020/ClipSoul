#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace ClipSoul {

enum class ClipboardKind {
    Text = 1,
    Html = 2,
    Image = 3,
    Files = 4,
    Link = 5,
};

struct CapturedContent {
    ClipboardKind kind = ClipboardKind::Text;
    std::optional<int64_t> created_at_unix;
    std::wstring text;
    std::string html;
    std::vector<std::wstring> files;
    std::filesystem::path payload_path;
    std::wstring preview;
    std::wstring search_text;
    std::wstring content_hash;
};

struct HistoryItem {
    int64_t id = 0;
    ClipboardKind kind = ClipboardKind::Text;
    int64_t created_at_unix = 0;
    std::wstring text;
    std::string html;
    std::vector<std::wstring> files;
    std::filesystem::path payload_path;
    std::wstring preview;
    std::wstring search_text;
    std::wstring content_hash;
    bool is_pinned = false;
    bool is_favorite = false;
    bool is_phrase = false;
    std::optional<int64_t> favorite_group_id;
    std::wstring note;
};

struct HistoryQuery {
    int limit = 60;
    std::wstring text;
    std::set<ClipboardKind> kinds;
    std::optional<int64_t> start_unix;
    std::optional<int64_t> end_unix;
    bool favorites_only = false;
    std::optional<int64_t> favorite_group_id;
};

struct AppSettings {
    int history_limit = 60;
    bool paused = false;
    bool start_with_windows = false;
    unsigned hotkey_modifiers = 0;
    unsigned hotkey_vk = 0;
    unsigned continuous_paste_hotkey_modifiers = 0;
    unsigned continuous_paste_hotkey_vk = 0;
    int theme_mode = 0; // 0 system, 1 light, 2 dark
};

struct FavoriteGroup {
    int64_t id = 0;
    std::wstring name;
};

constexpr unsigned kDefaultHotkeyModifiers = 0x0001; // MOD_ALT
constexpr unsigned kDefaultHotkeyVk = 'C';
constexpr unsigned kDefaultContinuousPasteHotkeyModifiers = 0x0002 | 0x0001; // MOD_CONTROL | MOD_ALT
constexpr unsigned kDefaultContinuousPasteHotkeyVk = 'V';

void ApplyAppSettingsDefaults(AppSettings& settings);
std::wstring FormatHotkey(unsigned modifiers, unsigned vk);

class HistoryStore {
public:
    HistoryStore();
    ~HistoryStore();

    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;

    void Open(const std::filesystem::path& database_path);
    bool IsOpen() const;
    AppSettings LoadSettings() const;
    void SaveSettings(const AppSettings& settings);
    bool Add(const CapturedContent& content);
    bool AddFavoritePhrase(std::wstring_view text);
    bool AddFavoritePhrase(std::wstring_view text, std::wstring_view note, std::optional<int64_t> group_id);
    int64_t EnsureFavoriteGroup(std::wstring_view name);
    std::vector<FavoriteGroup> FavoriteGroups() const;
    std::vector<HistoryItem> Recent(int limit, std::wstring_view query) const;
    std::vector<HistoryItem> Query(const HistoryQuery& query) const;
    std::optional<HistoryItem> Get(int64_t id) const;
    bool SetPinned(int64_t id, bool pinned);
    bool SetFavorite(int64_t id, bool favorite);
    bool SetFavoriteGroup(int64_t id, std::optional<int64_t> group_id);
    bool DeleteFavoriteGroup(int64_t group_id);
    bool SetNote(int64_t id, std::wstring_view note);
    bool Delete(int64_t id);
    void Clear();
    void EnforceLimit();

private:
    struct Impl;
    Impl* impl_;
};

class HistorySelection {
public:
    void Toggle(int64_t id);
    void Clear();
    bool IsSelected(int64_t id) const;
    std::vector<int64_t> SelectedIds() const;

private:
    std::unordered_set<int64_t> ids_;
};

} // namespace ClipSoul
