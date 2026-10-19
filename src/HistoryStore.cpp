#include "ClipSoul/HistoryStore.h"

#include "ClipSoul/TextUtil.h"

#include <winsqlite/winsqlite3.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ClipSoul {
namespace {

int64_t NowUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::wstring DbText(sqlite3_stmt* statement, int column) {
    const auto* text = sqlite3_column_text(statement, column);
    if (!text) {
        return {};
    }
    return Utf8ToWide(reinterpret_cast<const char*>(text));
}

std::string DbUtf8(sqlite3_stmt* statement, int column) {
    const auto* text = sqlite3_column_text(statement, column);
    if (!text) {
        return {};
    }
    return reinterpret_cast<const char*>(text);
}

std::vector<std::wstring> SplitFiles(std::wstring_view value) {
    std::vector<std::wstring> files;
    size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(L'\n', start);
        auto item = value.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start);
        if (!item.empty() && item.back() == L'\r') {
            item.remove_suffix(1);
        }
        if (!item.empty()) {
            files.emplace_back(item);
        }
        if (end == std::wstring_view::npos) {
            break;
        }
        start = end + 1;
    }
    return files;
}

std::wstring JoinFiles(const std::vector<std::wstring>& files) {
    std::wstring result;
    for (size_t i = 0; i < files.size(); ++i) {
        if (i != 0) {
            result.push_back(L'\n');
        }
        result += files[i];
    }
    return result;
}

[[noreturn]] void ThrowSqlite(sqlite3* db, const char* action) {
    const char* message = db ? sqlite3_errmsg(db) : "no database";
    throw std::runtime_error(std::string(action) + ": " + message);
}

class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db), stmt_(nullptr) {
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            ThrowSqlite(db_, "sqlite prepare failed");
        }
    }

    ~Statement() {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    sqlite3_stmt* get() const { return stmt_; }

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_;
};

void Exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : "unknown sqlite error";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

void TryExec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        sqlite3_free(error);
    }
}

void BindText(sqlite3_stmt* stmt, int index, std::wstring_view value) {
    const std::string utf8 = WideToUtf8(value);
    sqlite3_bind_text(stmt, index, utf8.c_str(), static_cast<int>(utf8.size()), SQLITE_TRANSIENT);
}

void BindText(sqlite3_stmt* stmt, int index, const std::string& value) {
    sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void BindOptionalInt64(sqlite3_stmt* stmt, int index, std::optional<int64_t> value) {
    if (value) {
        sqlite3_bind_int64(stmt, index, *value);
    } else {
        sqlite3_bind_null(stmt, index);
    }
}

// Image items store their pixels in a cache file (payload_path). Rows are deleted in several places
// (limit rotation, delete, clear, dedup); collect their payload paths first, then remove the files
// that are no longer referenced by any remaining row. Previously these files were never deleted,
// so the cache directory grew without bound.
template <typename Binder>
std::vector<std::wstring> CollectPayloadPaths(sqlite3* db, const char* sql, Binder bind) {
    std::vector<std::wstring> paths;
    Statement stmt(db, sql);
    bind(stmt.get());
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        auto path = DbText(stmt.get(), 0);
        if (!path.empty()) {
            paths.push_back(std::move(path));
        }
    }
    return paths;
}

std::vector<std::wstring> CollectPayloadPaths(sqlite3* db, const char* sql) {
    return CollectPayloadPaths(db, sql, [](sqlite3_stmt*) {});
}

void RemoveUnreferencedPayloads(sqlite3* db, const std::vector<std::wstring>& paths) {
    if (paths.empty()) {
        return;
    }
    Statement stmt(db, "SELECT 1 FROM history_items WHERE payload_path = ? LIMIT 1;");
    for (const auto& path : paths) {
        sqlite3_reset(stmt.get());
        sqlite3_clear_bindings(stmt.get());
        BindText(stmt.get(), 1, path);
        if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            continue; // still referenced (favorite, or the re-inserted duplicate)
        }
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(path), ec);
    }
}

// MAX(sort_order) walks the sort index on every insert; one copy per clipboard event made that a
// hot query. The high-water mark is cached and only re-queried when the cheap fast path cannot be
// trusted (first use, or after an operation that can lower the maximum, e.g. EnforceLimit/Delete).
int64_t NextSortOrder(sqlite3* db, int64_t& cached_max) {
    if (cached_max > 0) {
        return ++cached_max;
    }
    Statement stmt(db, "SELECT COALESCE(MAX(sort_order), 0) FROM history_items;");
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        cached_max = sqlite3_column_int64(stmt.get(), 0);
    }
    return ++cached_max;
}

// v1: HTML entries used to hash the raw CF_HTML block (header offsets / SourceURL included), so the
// same content copied twice never matched. Re-hash them by visible text once, like new captures do.
void MigrateLegacyHtmlHashes(sqlite3* db) {
    int version = 0;
    {
        Statement stmt(db, "PRAGMA user_version;");
        if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            version = sqlite3_column_int(stmt.get(), 0);
        }
    }
    if (version >= 1) {
        return;
    }
    Exec(db, "BEGIN IMMEDIATE;");
    try {
        std::vector<std::pair<int64_t, std::wstring>> rows;
        {
            Statement read(db, "SELECT id, text FROM history_items WHERE kind = ? AND is_phrase = 0;");
            sqlite3_bind_int(read.get(), 1, static_cast<int>(ClipboardKind::Html));
            while (sqlite3_step(read.get()) == SQLITE_ROW) {
                rows.emplace_back(sqlite3_column_int64(read.get(), 0), DbText(read.get(), 1));
            }
        }
        Statement update(db, "UPDATE history_items SET content_hash = ? WHERE id = ?;");
        for (const auto& [id, text] : rows) {
            if (text.empty()) {
                continue;
            }
            sqlite3_reset(update.get());
            BindText(update.get(), 1, StableHash(text));
            sqlite3_bind_int64(update.get(), 2, id);
            if (sqlite3_step(update.get()) != SQLITE_DONE) {
                ThrowSqlite(db, "sqlite migrate html hash failed");
            }
        }
        Exec(db, "PRAGMA user_version = 1;");
        Exec(db, "COMMIT;");
    } catch (...) {
        TryExec(db, "ROLLBACK;");
        throw;
    }
}

std::wstring NormalizeSearchNeedle(std::wstring_view input) {
    std::wstring result;
    result.reserve(input.size());
    for (const wchar_t ch : NormalizeWhitespace(input)) {
        if (std::iswspace(ch) != 0) {
            continue;
        }
        result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return result;
}

bool FuzzySubsequenceMatches(std::wstring_view needle, std::wstring_view haystack) {
    if (needle.empty()) {
        return true;
    }
    size_t needle_index = 0;
    for (const wchar_t ch : haystack) {
        if (ch == needle[needle_index]) {
            ++needle_index;
            if (needle_index == needle.size()) {
                return true;
            }
        }
    }
    return false;
}

bool SearchFieldMatches(std::wstring_view field, std::wstring_view normalized_query) {
    if (normalized_query.empty()) {
        return true;
    }
    const auto normalized_field = NormalizeSearchNeedle(field);
    if (normalized_field.empty()) {
        return false;
    }
    return normalized_field.find(normalized_query) != std::wstring::npos ||
           FuzzySubsequenceMatches(normalized_query, normalized_field);
}

bool HistoryItemMatchesSearch(const HistoryItem& item, std::wstring_view normalized_query) {
    return SearchFieldMatches(item.search_text, normalized_query) || SearchFieldMatches(item.text, normalized_query) ||
           SearchFieldMatches(item.note, normalized_query);
}

} // namespace

void ApplyAppSettingsDefaults(AppSettings& settings) {
    constexpr unsigned legacy_modifiers = 0x0002 | 0x0004; // MOD_CONTROL | MOD_SHIFT
    constexpr unsigned legacy_vk = 'V';
    const bool missing_hotkey = settings.hotkey_modifiers == 0 || settings.hotkey_vk == 0;
    const bool legacy_hotkey = settings.hotkey_modifiers == legacy_modifiers && settings.hotkey_vk == legacy_vk;
    if (missing_hotkey || legacy_hotkey) {
        settings.hotkey_modifiers = kDefaultHotkeyModifiers;
        settings.hotkey_vk = kDefaultHotkeyVk;
    }
    if (settings.continuous_paste_hotkey_modifiers == 0 || settings.continuous_paste_hotkey_vk == 0) {
        settings.continuous_paste_hotkey_modifiers = kDefaultContinuousPasteHotkeyModifiers;
        settings.continuous_paste_hotkey_vk = kDefaultContinuousPasteHotkeyVk;
    }
}

std::wstring FormatHotkey(unsigned modifiers, unsigned vk) {
    std::wstring label;
    auto append = [&](std::wstring_view part) {
        if (!label.empty()) {
            label += L"+";
        }
        label += part;
    };
    if ((modifiers & 0x0002) != 0) append(L"Ctrl");
    if ((modifiers & 0x0001) != 0) append(L"Alt");
    if ((modifiers & 0x0004) != 0) append(L"Shift");
    if ((modifiers & 0x0008) != 0) append(L"Win");
    if (vk >= L'A' && vk <= L'Z') {
        append(std::wstring(1, static_cast<wchar_t>(vk)));
    } else if (vk >= L'0' && vk <= L'9') {
        append(std::wstring(1, static_cast<wchar_t>(vk)));
    } else {
        wchar_t value[16]{};
        swprintf_s(value, L"VK_%u", vk);
        append(value);
    }
    return label.empty() ? L"未设置" : label;
}

struct HistoryStore::Impl {
    sqlite3* db = nullptr;
    std::filesystem::path path;
    // High-water mark for sort_order; 0 means "unknown, re-read from the table".
    int64_t sort_order_high_water = 0;

    ~Impl() {
        if (db) {
            sqlite3_close(db);
        }
    }
};

HistoryStore::HistoryStore() : impl_(new Impl()) {}

HistoryStore::~HistoryStore() {
    delete impl_;
}

void HistoryStore::Open(const std::filesystem::path& database_path) {
    if (impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }

    std::filesystem::create_directories(database_path.parent_path());
    const std::string utf8_path = WideToUtf8(database_path.wstring());
    if (sqlite3_open_v2(utf8_path.c_str(), &impl_->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        ThrowSqlite(impl_->db, "sqlite open failed");
    }
    impl_->path = database_path;
    impl_->sort_order_high_water = 0;

    Exec(impl_->db, "PRAGMA journal_mode=WAL;");
    Exec(impl_->db, "PRAGMA synchronous=NORMAL;");
    // 常驻的剪贴板历史是小库：把临时表放内存、给页缓存一个合理上限，避免每次启动/查询都碰磁盘。
    TryExec(impl_->db, "PRAGMA temp_store=MEMORY;");
    TryExec(impl_->db, "PRAGMA cache_size=-2048;");   // 2 MB
    TryExec(impl_->db, "PRAGMA busy_timeout=2000;");  // 遇到外部进程短暂占用时等待而不是立刻失败
    Exec(impl_->db,
         "CREATE TABLE IF NOT EXISTS history_items ("
         "id INTEGER PRIMARY KEY AUTOINCREMENT,"
         "kind INTEGER NOT NULL,"
         "created_at INTEGER NOT NULL,"
         "text TEXT NOT NULL DEFAULT '',"
         "html TEXT NOT NULL DEFAULT '',"
         "files TEXT NOT NULL DEFAULT '',"
         "payload_path TEXT NOT NULL DEFAULT '',"
         "preview TEXT NOT NULL DEFAULT '',"
         "search_text TEXT NOT NULL DEFAULT '',"
         "content_hash TEXT NOT NULL,"
         "sort_order INTEGER NOT NULL DEFAULT 0,"
         "is_pinned INTEGER NOT NULL DEFAULT 0,"
         "is_favorite INTEGER NOT NULL DEFAULT 0,"
         "is_phrase INTEGER NOT NULL DEFAULT 0,"
         "favorite_group_id INTEGER,"
         "note TEXT NOT NULL DEFAULT ''"
         ");");
    TryExec(impl_->db, "ALTER TABLE history_items ADD COLUMN is_pinned INTEGER NOT NULL DEFAULT 0;");
    TryExec(impl_->db, "ALTER TABLE history_items ADD COLUMN is_favorite INTEGER NOT NULL DEFAULT 0;");
    TryExec(impl_->db, "ALTER TABLE history_items ADD COLUMN is_phrase INTEGER NOT NULL DEFAULT 0;");
    TryExec(impl_->db, "ALTER TABLE history_items ADD COLUMN favorite_group_id INTEGER;");
    TryExec(impl_->db, "ALTER TABLE history_items ADD COLUMN note TEXT NOT NULL DEFAULT '';");
    TryExec(impl_->db, "ALTER TABLE history_items ADD COLUMN sort_order INTEGER NOT NULL DEFAULT 0;");
    // 只在真有 sort_order=0 的遗留行时才回填；之前每次启动都写一遍整表。
    {
        Statement legacy(impl_->db, "SELECT 1 FROM history_items WHERE sort_order = 0 LIMIT 1;");
        if (sqlite3_step(legacy.get()) == SQLITE_ROW) {
            Exec(impl_->db, "UPDATE history_items SET sort_order = id WHERE sort_order = 0;");
        }
    }
    Exec(impl_->db,
         "CREATE INDEX IF NOT EXISTS idx_history_sort ON history_items(is_pinned DESC, sort_order DESC, id DESC);");
    // Used by the per-copy duplicate checks in Add(); without them every copy scans the whole table.
    Exec(impl_->db, "CREATE INDEX IF NOT EXISTS idx_history_hash ON history_items(content_hash);");
    Exec(impl_->db, "CREATE INDEX IF NOT EXISTS idx_history_created ON history_items(created_at);");
    // The 3-second "same text twice" dedup in Add() restricts on created_at first, so a composite
    // index lets SQLite cut the table down to a handful of rows before comparing the (large) text.
    Exec(impl_->db,
         "CREATE INDEX IF NOT EXISTS idx_history_created_text ON history_items(created_at, text);");
    MigrateLegacyHtmlHashes(impl_->db);
    Exec(impl_->db,
         "CREATE TABLE IF NOT EXISTS favorite_groups ("
         "id INTEGER PRIMARY KEY AUTOINCREMENT,"
         "name TEXT NOT NULL UNIQUE,"
         "created_at INTEGER NOT NULL"
         ");");
    Exec(impl_->db,
         "INSERT OR IGNORE INTO favorite_groups(name, created_at) VALUES('默认', 0);");
    Exec(impl_->db,
         "CREATE TABLE IF NOT EXISTS settings ("
         "key TEXT PRIMARY KEY,"
         "value TEXT NOT NULL"
         ");");
}

bool HistoryStore::IsOpen() const {
    return impl_->db != nullptr;
}

AppSettings HistoryStore::LoadSettings() const {
    if (!impl_->db) {
        throw std::runtime_error("database is not open");
    }

    AppSettings settings;
    Statement stmt(impl_->db, "SELECT key, value FROM settings;");
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const auto key = DbUtf8(stmt.get(), 0);
        const auto value = DbUtf8(stmt.get(), 1);
        if (key == "history_limit") {
            settings.history_limit = std::max(1, std::stoi(value));
        } else if (key == "paused") {
            settings.paused = value == "1";
        } else if (key == "start_with_windows") {
            settings.start_with_windows = value == "1";
        } else if (key == "hotkey_modifiers") {
            settings.hotkey_modifiers = static_cast<unsigned>(std::stoul(value));
        } else if (key == "hotkey_vk") {
            settings.hotkey_vk = static_cast<unsigned>(std::stoul(value));
        } else if (key == "continuous_paste_hotkey_modifiers") {
            settings.continuous_paste_hotkey_modifiers = static_cast<unsigned>(std::stoul(value));
        } else if (key == "continuous_paste_hotkey_vk") {
            settings.continuous_paste_hotkey_vk = static_cast<unsigned>(std::stoul(value));
        } else if (key == "theme_mode") {
            settings.theme_mode = std::clamp(std::stoi(value), 0, 2);
        } else if (key == "popup_resizable") {
            settings.popup_resizable = value == "1";
        }
    }
    ApplyAppSettingsDefaults(settings);
    return settings;
}

void HistoryStore::SaveSettings(const AppSettings& settings) {
    if (!impl_->db) {
        throw std::runtime_error("database is not open");
    }

    Statement stmt(impl_->db, "INSERT INTO settings(key, value) VALUES(?, ?) "
                              "ON CONFLICT(key) DO UPDATE SET value = excluded.value;");
    auto save = [&](const char* key, std::string value) {
        sqlite3_reset(stmt.get());
        sqlite3_clear_bindings(stmt.get());
        sqlite3_bind_text(stmt.get(), 1, key, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt.get(), 2, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
        if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
            ThrowSqlite(impl_->db, "sqlite save setting failed");
        }
    };

    save("history_limit", std::to_string(std::max(1, settings.history_limit)));
    save("paused", settings.paused ? "1" : "0");
    save("start_with_windows", settings.start_with_windows ? "1" : "0");
    save("hotkey_modifiers", std::to_string(settings.hotkey_modifiers));
    save("hotkey_vk", std::to_string(settings.hotkey_vk));
    save("continuous_paste_hotkey_modifiers", std::to_string(settings.continuous_paste_hotkey_modifiers));
    save("continuous_paste_hotkey_vk", std::to_string(settings.continuous_paste_hotkey_vk));
    save("theme_mode", std::to_string(std::clamp(settings.theme_mode, 0, 2)));
    save("popup_resizable", settings.popup_resizable ? "1" : "0");

    EnforceLimit();
}

bool HistoryStore::Add(const CapturedContent& content) {
    if (!impl_->db) {
        throw std::runtime_error("database is not open");
    }
    if (content.content_hash.empty()) {
        return false;
    }

    const int64_t created_at = content.created_at_unix.value_or(NowUnixSeconds());
    Exec(impl_->db, "BEGIN IMMEDIATE;");
    std::vector<std::wstring> replaced_payloads;
    try {
        // Copying something that is already in the history moves that entry to the top (newest copy wins)
        // instead of being ignored. The existing row is updated in place so its id, pin state and note
        // survive; any further duplicates are removed.
        std::optional<int64_t> existing_id;
        {
            Statement find(impl_->db,
                           "SELECT id FROM history_items WHERE kind = ? AND content_hash = ? "
                           "AND is_favorite = 0 AND is_phrase = 0 ORDER BY sort_order DESC, id DESC LIMIT 1;");
            sqlite3_bind_int(find.get(), 1, static_cast<int>(content.kind));
            BindText(find.get(), 2, content.content_hash);
            if (sqlite3_step(find.get()) == SQLITE_ROW) {
                existing_id = sqlite3_column_int64(find.get(), 0);
            }
        }
        const int64_t keep_id = existing_id.value_or(0);

        // Payload files of rows we are about to drop or overwrite (removed later if unreferenced).
        replaced_payloads = CollectPayloadPaths(
            impl_->db,
            "SELECT payload_path FROM history_items WHERE is_favorite = 0 AND is_phrase = 0 AND ("
            "(kind = ? AND content_hash = ?) OR (? <> '' AND text = ? AND created_at >= ?));",
            [&](sqlite3_stmt* stmt) {
                sqlite3_bind_int(stmt, 1, static_cast<int>(content.kind));
                BindText(stmt, 2, content.content_hash);
                BindText(stmt, 3, content.text);
                BindText(stmt, 4, content.text);
                sqlite3_bind_int64(stmt, 5, created_at - 3);
            });

        {
            Statement remove_existing(impl_->db,
                                      "DELETE FROM history_items WHERE kind = ? AND content_hash = ? "
                                      "AND is_favorite = 0 AND is_phrase = 0 AND id <> ?;");
            sqlite3_bind_int(remove_existing.get(), 1, static_cast<int>(content.kind));
            BindText(remove_existing.get(), 2, content.content_hash);
            sqlite3_bind_int64(remove_existing.get(), 3, keep_id);
            if (sqlite3_step(remove_existing.get()) != SQLITE_DONE) {
                ThrowSqlite(impl_->db, "sqlite delete duplicate history failed");
            }
        }
        // Secondary deduplication: same text within 3 seconds handles different hashes from rapid clipboard events.
        // 这里的 `WHERE text = ?` 没有索引，每次复制都要把整张表的长文本比一遍（text 是最大的列）。
        // 限定在最近 3 秒内，走 idx_history_created 先裁出很小的一段再比文本。
        if (!content.text.empty()) {
            Statement remove_text_dup(impl_->db,
                                      "DELETE FROM history_items WHERE created_at >= ? AND text = ? "
                                      "AND is_favorite = 0 AND is_phrase = 0 AND id <> ?;");
            sqlite3_bind_int64(remove_text_dup.get(), 1, created_at - 3);
            BindText(remove_text_dup.get(), 2, content.text);
            sqlite3_bind_int64(remove_text_dup.get(), 3, keep_id);
            if (sqlite3_step(remove_text_dup.get()) != SQLITE_DONE) {
                ThrowSqlite(impl_->db, "sqlite delete text duplicate history failed");
            }
        }

        if (existing_id) {
            Statement stmt(impl_->db,
                           "UPDATE history_items SET created_at = ?, text = ?, html = ?, files = ?, payload_path = ?, "
                           "preview = ?, search_text = ?, sort_order = ? WHERE id = ?;");
            sqlite3_bind_int64(stmt.get(), 1, created_at);
            BindText(stmt.get(), 2, content.text);
            BindText(stmt.get(), 3, content.html);
            BindText(stmt.get(), 4, JoinFiles(content.files));
            BindText(stmt.get(), 5, content.payload_path.wstring());
            BindText(stmt.get(), 6, content.preview);
            BindText(stmt.get(), 7, content.search_text);
            sqlite3_bind_int64(stmt.get(), 8, NextSortOrder(impl_->db, impl_->sort_order_high_water));
            sqlite3_bind_int64(stmt.get(), 9, *existing_id);
            if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
                ThrowSqlite(impl_->db, "sqlite move history item to top failed");
            }
        } else {
            Statement stmt(impl_->db,
                           "INSERT INTO history_items(kind, created_at, text, html, files, payload_path, preview, "
                           "search_text, content_hash, sort_order) "
                           "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
            sqlite3_bind_int(stmt.get(), 1, static_cast<int>(content.kind));
            sqlite3_bind_int64(stmt.get(), 2, created_at);
            BindText(stmt.get(), 3, content.text);
            BindText(stmt.get(), 4, content.html);
            BindText(stmt.get(), 5, JoinFiles(content.files));
            BindText(stmt.get(), 6, content.payload_path.wstring());
            BindText(stmt.get(), 7, content.preview);
            BindText(stmt.get(), 8, content.search_text);
            BindText(stmt.get(), 9, content.content_hash);
            sqlite3_bind_int64(stmt.get(), 10, NextSortOrder(impl_->db, impl_->sort_order_high_water));
            if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
                ThrowSqlite(impl_->db, "sqlite insert history failed");
            }
        }
        Exec(impl_->db, "COMMIT;");
    } catch (...) {
        TryExec(impl_->db, "ROLLBACK;");
        throw;
    }

    EnforceLimit();
    RemoveUnreferencedPayloads(impl_->db, replaced_payloads);
    return true;
}

bool HistoryStore::AddFavoritePhrase(std::wstring_view text) {
    return AddFavoritePhrase(text, L"", std::nullopt);
}

bool HistoryStore::AddFavoritePhrase(std::wstring_view text, std::wstring_view note, std::optional<int64_t> group_id) {
    if (!impl_->db) {
        throw std::runtime_error("database is not open");
    }

    const auto normalized = NormalizeWhitespace(text);
    if (normalized.empty()) {
        return false;
    }

    const auto hash = StableHash(normalized);
    const auto normalized_note = NormalizeEditableNote(note);
    {
        Statement update(impl_->db,
                         "UPDATE history_items SET is_favorite = 1, is_phrase = 1, text = ?, preview = ?, "
                         "search_text = ?, note = ?, favorite_group_id = ? "
                         "WHERE content_hash = ?;");
        BindText(update.get(), 1, normalized);
        BindText(update.get(), 2, normalized);
        BindText(update.get(), 3, normalized);
        BindText(update.get(), 4, normalized_note);
        BindOptionalInt64(update.get(), 5, group_id);
        BindText(update.get(), 6, hash);
        if (sqlite3_step(update.get()) != SQLITE_DONE) {
            ThrowSqlite(impl_->db, "sqlite convert favorite phrase failed");
        }
        if (sqlite3_changes(impl_->db) > 0) {
            return true;
        }
    }

    Statement stmt(impl_->db,
                   "INSERT INTO history_items(kind, created_at, text, preview, search_text, content_hash, "
                   "sort_order, is_favorite, is_phrase, favorite_group_id, note) "
                   "VALUES(?, ?, ?, ?, ?, ?, ?, 1, 1, ?, ?);");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(ClipboardKind::Text));
    sqlite3_bind_int64(stmt.get(), 2, NowUnixSeconds());
    BindText(stmt.get(), 3, normalized);
    BindText(stmt.get(), 4, normalized);
    BindText(stmt.get(), 5, normalized);
    BindText(stmt.get(), 6, hash);
    sqlite3_bind_int64(stmt.get(), 7, NextSortOrder(impl_->db, impl_->sort_order_high_water));
    BindOptionalInt64(stmt.get(), 8, group_id);
    BindText(stmt.get(), 9, normalized_note);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        ThrowSqlite(impl_->db, "sqlite insert favorite phrase failed");
    }

    EnforceLimit();
    return true;
}

int64_t HistoryStore::EnsureFavoriteGroup(std::wstring_view name) {
    if (!impl_->db) {
        throw std::runtime_error("database is not open");
    }
    auto normalized = NormalizeWhitespace(name);
    if (normalized.empty()) {
        normalized = L"默认";
    }

    {
        Statement select(impl_->db, "SELECT id FROM favorite_groups WHERE name = ?;");
        BindText(select.get(), 1, normalized);
        if (sqlite3_step(select.get()) == SQLITE_ROW) {
            return sqlite3_column_int64(select.get(), 0);
        }
    }

    Statement insert(impl_->db, "INSERT INTO favorite_groups(name, created_at) VALUES(?, ?);");
    BindText(insert.get(), 1, normalized);
    sqlite3_bind_int64(insert.get(), 2, NowUnixSeconds());
    if (sqlite3_step(insert.get()) != SQLITE_DONE) {
        ThrowSqlite(impl_->db, "sqlite insert favorite group failed");
    }
    return sqlite3_last_insert_rowid(impl_->db);
}

std::vector<FavoriteGroup> HistoryStore::FavoriteGroups() const {
    if (!impl_->db) {
        throw std::runtime_error("database is not open");
    }
    Statement stmt(impl_->db, "SELECT id, name FROM favorite_groups ORDER BY id ASC;");
    std::vector<FavoriteGroup> groups;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        FavoriteGroup group;
        group.id = sqlite3_column_int64(stmt.get(), 0);
        group.name = DbText(stmt.get(), 1);
        groups.push_back(std::move(group));
    }
    return groups;
}

std::vector<HistoryItem> HistoryStore::Recent(int limit, std::wstring_view query) const {
    HistoryQuery history_query;
    history_query.limit = limit;
    history_query.text = std::wstring(query);
    return Query(history_query);
}

std::vector<HistoryItem> HistoryStore::Query(const HistoryQuery& query) const {
    if (!impl_->db) {
        throw std::runtime_error("database is not open");
    }

    std::ostringstream sql;
    // html（CF_HTML 原文）可能有几十 KB~MB，列表只需要预览和元信息；它只在真正粘贴单条时用，
    // 由 Get(id) 单独读出。列表查询因此不再把每行的 html 都复制进内存。
    sql << "SELECT id, kind, created_at, text, files, payload_path, preview, search_text, content_hash, "
           "is_pinned, is_favorite, is_phrase, favorite_group_id, note FROM history_items WHERE 1=1";

    const auto normalized_query = NormalizeSearchNeedle(query.text);
    const bool has_query = !normalized_query.empty();
    if (!query.kinds.empty()) {
        sql << " AND kind IN (";
        for (size_t i = 0; i < query.kinds.size(); ++i) {
            if (i != 0) sql << ",";
            sql << "?";
        }
        sql << ")";
    }
    if (query.start_unix) {
        sql << " AND created_at >= ?";
    }
    if (query.end_unix) {
        sql << " AND created_at <= ?";
    }
    if (query.favorites_only) {
        sql << " AND is_favorite = 1";
        if (query.favorite_group_id) {
            sql << " AND favorite_group_id = ?";
        }
    } else {
        sql << " AND is_phrase = 0";
        if (!query.include_favorites) sql << " AND is_favorite = 0";
    }
    sql << " ORDER BY is_pinned DESC, sort_order DESC, id DESC";
    if (!has_query) {
        sql << " LIMIT ?";
    }
    sql << ";";

    Statement stmt(impl_->db, sql.str().c_str());
    int bind_index = 1;
    for (const auto kind : query.kinds) {
        sqlite3_bind_int(stmt.get(), bind_index++, static_cast<int>(kind));
    }
    if (query.start_unix) {
        sqlite3_bind_int64(stmt.get(), bind_index++, *query.start_unix);
    }
    if (query.end_unix) {
        sqlite3_bind_int64(stmt.get(), bind_index++, *query.end_unix);
    }
    if (query.favorites_only && query.favorite_group_id) {
        sqlite3_bind_int64(stmt.get(), bind_index++, *query.favorite_group_id);
    }
    const int limit = std::max(1, query.limit);
    if (!has_query) {
        sqlite3_bind_int(stmt.get(), bind_index, limit);
    }

    std::vector<HistoryItem> items;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        HistoryItem item;
        item.id = sqlite3_column_int64(stmt.get(), 0);
        item.kind = static_cast<ClipboardKind>(sqlite3_column_int(stmt.get(), 1));
        item.created_at_unix = sqlite3_column_int64(stmt.get(), 2);
        item.text = DbText(stmt.get(), 3);
        item.files = SplitFiles(DbText(stmt.get(), 4));
        item.payload_path = DbText(stmt.get(), 5);
        item.preview = DbText(stmt.get(), 6);
        item.search_text = DbText(stmt.get(), 7);
        item.content_hash = DbText(stmt.get(), 8);
        item.is_pinned = sqlite3_column_int(stmt.get(), 9) != 0;
        item.is_favorite = sqlite3_column_int(stmt.get(), 10) != 0;
        item.is_phrase = sqlite3_column_int(stmt.get(), 11) != 0;
        if (sqlite3_column_type(stmt.get(), 12) != SQLITE_NULL) {
            item.favorite_group_id = sqlite3_column_int64(stmt.get(), 12);
        }
        item.note = DbText(stmt.get(), 13);
        if (has_query && !HistoryItemMatchesSearch(item, normalized_query)) {
            continue;
        }
        items.push_back(std::move(item));
        if (has_query && static_cast<int>(items.size()) >= limit) {
            break;
        }
    }
    return items;
}

int HistoryStore::Count(const HistoryQuery& query) const {
    if (!impl_->db) throw std::runtime_error("database is not open");
    std::ostringstream sql;
    sql << "SELECT COUNT(*) FROM history_items WHERE 1=1";
    const auto needle=NormalizeSearchNeedle(query.text);
    const bool has_query=!needle.empty();
    if(!query.kinds.empty()) {
        sql << " AND kind IN (";
        for(size_t i=0;i<query.kinds.size();++i){if(i)sql << ",";sql << "?";}
        sql << ")";
    }
    if(query.start_unix)sql << " AND created_at >= ?";
    if(query.end_unix)sql << " AND created_at <= ?";
    if(query.favorites_only){
        sql << " AND is_favorite = 1";
        if(query.favorite_group_id)sql << " AND favorite_group_id = ?";
    } else {
        sql << " AND is_phrase = 0";
        if(!query.include_favorites)sql << " AND is_favorite = 0";
    }
    if(has_query)sql << " AND (text LIKE ? ESCAPE '\\' OR preview LIKE ? ESCAPE '\\' OR search_text LIKE ? ESCAPE '\\' OR note LIKE ? ESCAPE '\\')";
    sql << ";";
    Statement stmt(impl_->db,sql.str().c_str());
    int bind=1;
    for(auto kind:query.kinds)sqlite3_bind_int(stmt.get(),bind++,static_cast<int>(kind));
    if(query.start_unix)sqlite3_bind_int64(stmt.get(),bind++,*query.start_unix);
    if(query.end_unix)sqlite3_bind_int64(stmt.get(),bind++,*query.end_unix);
    if(query.favorites_only && query.favorite_group_id)sqlite3_bind_int64(stmt.get(),bind++,*query.favorite_group_id);
    if(has_query){
        std::wstring escaped;
        escaped.reserve(needle.size()+2);escaped.push_back(L'%');
        for(wchar_t ch:needle){if(ch==L'%'||ch==L'_'||ch==L'\\')escaped.push_back(L'\\');escaped.push_back(ch);}
        escaped.push_back(L'%');
        for(int i=0;i<4;++i)BindText(stmt.get(),bind++,escaped);
    }
    return sqlite3_step(stmt.get())==SQLITE_ROW?sqlite3_column_int(stmt.get(),0):0;
}

std::map<int64_t,int> HistoryStore::FavoriteGroupCounts() const {
    if (!impl_->db) throw std::runtime_error("database is not open");
    std::map<int64_t,int> counts;
    Statement stmt(impl_->db,"SELECT favorite_group_id, COUNT(*) FROM history_items WHERE is_favorite = 1 GROUP BY favorite_group_id;");
    while(sqlite3_step(stmt.get())==SQLITE_ROW){
        if(sqlite3_column_type(stmt.get(),0)!=SQLITE_NULL)
            counts[sqlite3_column_int64(stmt.get(),0)]=sqlite3_column_int(stmt.get(),1);
    }
    return counts;
}

std::optional<HistoryItem> HistoryStore::Get(int64_t id) const {
    if (!impl_->db) {
        throw std::runtime_error("database is not open");
    }

    Statement stmt(impl_->db,
                   "SELECT id, kind, created_at, text, html, files, payload_path, preview, search_text, content_hash, "
                   "is_pinned, is_favorite, is_phrase, favorite_group_id, note "
                   "FROM history_items WHERE id = ?;");
    sqlite3_bind_int64(stmt.get(), 1, id);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    HistoryItem item;
    item.id = sqlite3_column_int64(stmt.get(), 0);
    item.kind = static_cast<ClipboardKind>(sqlite3_column_int(stmt.get(), 1));
    item.created_at_unix = sqlite3_column_int64(stmt.get(), 2);
    item.text = DbText(stmt.get(), 3);
    item.html = DbUtf8(stmt.get(), 4);
    item.files = SplitFiles(DbText(stmt.get(), 5));
    item.payload_path = DbText(stmt.get(), 6);
    item.preview = DbText(stmt.get(), 7);
    item.search_text = DbText(stmt.get(), 8);
    item.content_hash = DbText(stmt.get(), 9);
    item.is_pinned = sqlite3_column_int(stmt.get(), 10) != 0;
    item.is_favorite = sqlite3_column_int(stmt.get(), 11) != 0;
    item.is_phrase = sqlite3_column_int(stmt.get(), 12) != 0;
    if (sqlite3_column_type(stmt.get(), 13) != SQLITE_NULL) {
        item.favorite_group_id = sqlite3_column_int64(stmt.get(), 13);
    }
    item.note = DbText(stmt.get(), 14);
    return item;
}

bool HistoryStore::SetPinned(int64_t id, bool pinned) {
    Statement stmt(impl_->db, "UPDATE history_items SET is_pinned = ? WHERE id = ?;");
    sqlite3_bind_int(stmt.get(), 1, pinned ? 1 : 0);
    sqlite3_bind_int64(stmt.get(), 2, id);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        ThrowSqlite(impl_->db, "sqlite set pinned failed");
    }
    return sqlite3_changes(impl_->db) > 0;
}

bool HistoryStore::SetFavorite(int64_t id, bool favorite) {
    Statement stmt(impl_->db, "UPDATE history_items SET is_favorite = ? WHERE id = ?;");
    sqlite3_bind_int(stmt.get(), 1, favorite ? 1 : 0);
    sqlite3_bind_int64(stmt.get(), 2, id);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        ThrowSqlite(impl_->db, "sqlite set favorite failed");
    }
    return sqlite3_changes(impl_->db) > 0;
}

bool HistoryStore::SetFavoriteGroup(int64_t id, std::optional<int64_t> group_id) {
    Statement stmt(impl_->db, "UPDATE history_items SET is_favorite = 1, favorite_group_id = ? WHERE id = ?;");
    BindOptionalInt64(stmt.get(), 1, group_id);
    sqlite3_bind_int64(stmt.get(), 2, id);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        ThrowSqlite(impl_->db, "sqlite set favorite group failed");
    }
    return sqlite3_changes(impl_->db) > 0;
}

bool HistoryStore::DeleteFavoriteGroup(int64_t group_id) {
    if (!impl_->db) {
        throw std::runtime_error("database is not open");
    }

    Exec(impl_->db, "BEGIN IMMEDIATE;");
    std::vector<std::wstring> removed_payloads;
    try {
        removed_payloads = CollectPayloadPaths(impl_->db,
                                               "SELECT payload_path FROM history_items WHERE favorite_group_id = ?;",
                                               [&](sqlite3_stmt* stmt) { sqlite3_bind_int64(stmt, 1, group_id); });
        {
            Statement delete_items(impl_->db, "DELETE FROM history_items WHERE favorite_group_id = ?;");
            sqlite3_bind_int64(delete_items.get(), 1, group_id);
            if (sqlite3_step(delete_items.get()) != SQLITE_DONE) {
                ThrowSqlite(impl_->db, "sqlite delete favorite group items failed");
            }
        }

        Statement delete_group(impl_->db, "DELETE FROM favorite_groups WHERE id = ?;");
        sqlite3_bind_int64(delete_group.get(), 1, group_id);
        if (sqlite3_step(delete_group.get()) != SQLITE_DONE) {
            ThrowSqlite(impl_->db, "sqlite delete favorite group failed");
        }
        const bool deleted = sqlite3_changes(impl_->db) > 0;
        Exec(impl_->db, "COMMIT;");
        RemoveUnreferencedPayloads(impl_->db, removed_payloads);
        return deleted;
    } catch (...) {
        TryExec(impl_->db, "ROLLBACK;");
        throw;
    }
}

bool HistoryStore::SetNote(int64_t id, std::wstring_view note) {
    Statement stmt(impl_->db, "UPDATE history_items SET note = ? WHERE id = ?;");
    BindText(stmt.get(), 1, NormalizeEditableNote(note));
    sqlite3_bind_int64(stmt.get(), 2, id);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        ThrowSqlite(impl_->db, "sqlite set note failed");
    }
    return sqlite3_changes(impl_->db) > 0;
}

bool HistoryStore::SwapSortOrder(int64_t first_id, int64_t second_id) {
    if (!impl_->db) {
        throw std::runtime_error("database is not open");
    }
    if (first_id == second_id) {
        return false;
    }

    Exec(impl_->db, "BEGIN IMMEDIATE;");
    try {
        Statement read(impl_->db, "SELECT id, sort_order FROM history_items WHERE id IN (?, ?);");
        sqlite3_bind_int64(read.get(), 1, first_id);
        sqlite3_bind_int64(read.get(), 2, second_id);

        std::optional<int64_t> first_order;
        std::optional<int64_t> second_order;
        while (sqlite3_step(read.get()) == SQLITE_ROW) {
            const auto id = sqlite3_column_int64(read.get(), 0);
            const auto order = sqlite3_column_int64(read.get(), 1);
            if (id == first_id) {
                first_order = order;
            } else if (id == second_id) {
                second_order = order;
            }
        }

        if (!first_order || !second_order) {
            Exec(impl_->db, "COMMIT;");
            return false;
        }

        Statement update(impl_->db,
                         "UPDATE history_items SET sort_order = CASE id WHEN ? THEN ? WHEN ? THEN ? ELSE sort_order END "
                         "WHERE id IN (?, ?);");
        sqlite3_bind_int64(update.get(), 1, first_id);
        sqlite3_bind_int64(update.get(), 2, *second_order);
        sqlite3_bind_int64(update.get(), 3, second_id);
        sqlite3_bind_int64(update.get(), 4, *first_order);
        sqlite3_bind_int64(update.get(), 5, first_id);
        sqlite3_bind_int64(update.get(), 6, second_id);
        if (sqlite3_step(update.get()) != SQLITE_DONE) {
            ThrowSqlite(impl_->db, "sqlite swap sort order failed");
        }

        Exec(impl_->db, "COMMIT;");
        return sqlite3_changes(impl_->db) > 0;
    } catch (...) {
        TryExec(impl_->db, "ROLLBACK;");
        throw;
    }
}

bool HistoryStore::Delete(int64_t id) {
    const auto payloads = CollectPayloadPaths(impl_->db, "SELECT payload_path FROM history_items WHERE id = ?;",
                                              [&](sqlite3_stmt* stmt) { sqlite3_bind_int64(stmt, 1, id); });
    Statement stmt(impl_->db, "DELETE FROM history_items WHERE id = ?;");
    sqlite3_bind_int64(stmt.get(), 1, id);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        ThrowSqlite(impl_->db, "sqlite delete history failed");
    }
    const bool deleted = sqlite3_changes(impl_->db) > 0;
    if (deleted) {
        impl_->sort_order_high_water = 0;
    }
    RemoveUnreferencedPayloads(impl_->db, payloads);
    return deleted;
}

void HistoryStore::Clear() {
    if (!impl_->db) {
        throw std::runtime_error("database is not open");
    }
    const auto payloads =
        CollectPayloadPaths(impl_->db, "SELECT payload_path FROM history_items WHERE is_favorite = 0;");
    Exec(impl_->db, "DELETE FROM history_items WHERE is_favorite = 0;");
    impl_->sort_order_high_water = 0;
    RemoveUnreferencedPayloads(impl_->db, payloads);
}

void HistoryStore::EnforceLimit() {
    // Read only the one setting we need instead of parsing the whole settings table on every copy.
    int limit = AppSettings{}.history_limit;
    {
        Statement setting(impl_->db, "SELECT value FROM settings WHERE key = 'history_limit';");
        if (sqlite3_step(setting.get()) == SQLITE_ROW) {
            limit = std::max(1, sqlite3_column_int(setting.get(), 0));
        }
    }
    // 旧写法是 DELETE ... WHERE id NOT IN (SELECT id ... ORDER BY ... LIMIT ?)，每次复制都要把
    // 整张表读出来排序再比一遍 id。改成两步：先找出第 limit 条的分界线（走 idx_history_sort，
    // 一条索引扫描），再按 (is_pinned, sort_order, id) 阈值删掉它之后的行。
    int64_t cut_pinned = 0;
    int64_t cut_order = 0;
    int64_t cut_id = 0;
    bool has_cut = false;
    {
        Statement boundary(impl_->db,
                           "SELECT is_pinned, sort_order, id FROM history_items WHERE is_favorite = 0 "
                           "ORDER BY is_pinned DESC, sort_order DESC, id DESC LIMIT 1 OFFSET ?;");
        // The row at index limit-1 is the last one we keep; everything after it is over the limit.
        sqlite3_bind_int(boundary.get(), 1, limit - 1);
        if (sqlite3_step(boundary.get()) == SQLITE_ROW) {
            has_cut = true;
            cut_pinned = sqlite3_column_int64(boundary.get(), 0);
            cut_order = sqlite3_column_int64(boundary.get(), 1);
            cut_id = sqlite3_column_int64(boundary.get(), 2);
        }
    }
    if (!has_cut) {
        return; // 未超出上限：没有要删的行。
    }

    static constexpr char kOverflowFilter[] =
        "FROM history_items WHERE is_favorite = 0 AND (is_pinned < ? OR "
        "(is_pinned = ? AND (sort_order < ? OR (sort_order = ? AND id < ?))));";
    const auto payloads = CollectPayloadPaths(
        impl_->db, (std::string("SELECT payload_path ") + kOverflowFilter).c_str(), [&](sqlite3_stmt* stmt) {
            sqlite3_bind_int64(stmt, 1, cut_pinned);
            sqlite3_bind_int64(stmt, 2, cut_pinned);
            sqlite3_bind_int64(stmt, 3, cut_order);
            sqlite3_bind_int64(stmt, 4, cut_order);
            sqlite3_bind_int64(stmt, 5, cut_id);
        });
    Statement stmt(impl_->db, (std::string("DELETE ") + kOverflowFilter).c_str());
    sqlite3_bind_int64(stmt.get(), 1, cut_pinned);
    sqlite3_bind_int64(stmt.get(), 2, cut_pinned);
    sqlite3_bind_int64(stmt.get(), 3, cut_order);
    sqlite3_bind_int64(stmt.get(), 4, cut_order);
    sqlite3_bind_int64(stmt.get(), 5, cut_id);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        ThrowSqlite(impl_->db, "sqlite enforce limit failed");
    }
    // 删掉的可能是 sort_order 最大的那些行，缓存的水位线要重新读一次。
    impl_->sort_order_high_water = 0;
    RemoveUnreferencedPayloads(impl_->db, payloads);
}

void HistorySelection::Toggle(int64_t id) {
    if (ids_.contains(id)) {
        ids_.erase(id);
    } else {
        ids_.insert(id);
    }
}

void HistorySelection::Clear() {
    ids_.clear();
}

bool HistorySelection::IsSelected(int64_t id) const {
    return ids_.contains(id);
}

std::vector<int64_t> HistorySelection::SelectedIds() const {
    return std::vector<int64_t>(ids_.begin(), ids_.end());
}

} // namespace ClipSoul
