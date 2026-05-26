#include "ClipSoul/HistoryStore.h"

#include "ClipSoul/TextUtil.h"

#include <winsqlite/winsqlite3.h>

#include <chrono>
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

    Exec(impl_->db, "PRAGMA journal_mode=WAL;");
    Exec(impl_->db, "PRAGMA synchronous=NORMAL;");
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
    Exec(impl_->db,
         "CREATE INDEX IF NOT EXISTS idx_history_sort ON history_items(is_pinned DESC, created_at DESC, id DESC);");
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

    {
    Statement latest(impl_->db, "SELECT content_hash FROM history_items ORDER BY created_at DESC, id DESC LIMIT 1;");
        if (sqlite3_step(latest.get()) == SQLITE_ROW && DbText(latest.get(), 0) == content.content_hash) {
            return true;
        }
    }

    Statement stmt(impl_->db,
                   "INSERT INTO history_items(kind, created_at, text, html, files, payload_path, preview, search_text, content_hash) "
                   "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?);");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(content.kind));
    sqlite3_bind_int64(stmt.get(), 2, content.created_at_unix.value_or(NowUnixSeconds()));
    BindText(stmt.get(), 3, content.text);
    BindText(stmt.get(), 4, content.html);
    BindText(stmt.get(), 5, JoinFiles(content.files));
    BindText(stmt.get(), 6, content.payload_path.wstring());
    BindText(stmt.get(), 7, content.preview);
    BindText(stmt.get(), 8, content.search_text);
    BindText(stmt.get(), 9, content.content_hash);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        ThrowSqlite(impl_->db, "sqlite insert history failed");
    }

    EnforceLimit();
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
                   "is_favorite, is_phrase, favorite_group_id, note) "
                   "VALUES(?, ?, ?, ?, ?, ?, 1, 1, ?, ?);");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(ClipboardKind::Text));
    sqlite3_bind_int64(stmt.get(), 2, NowUnixSeconds());
    BindText(stmt.get(), 3, normalized);
    BindText(stmt.get(), 4, normalized);
    BindText(stmt.get(), 5, normalized);
    BindText(stmt.get(), 6, hash);
    BindOptionalInt64(stmt.get(), 7, group_id);
    BindText(stmt.get(), 8, normalized_note);

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
    sql << "SELECT id, kind, created_at, text, html, files, payload_path, preview, search_text, content_hash, "
           "is_pinned, is_favorite, is_phrase, favorite_group_id, note FROM history_items WHERE 1=1";

    const bool has_query = !NormalizeWhitespace(query.text).empty();
    if (has_query) {
        sql << " AND search_text LIKE ?";
    }
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
        sql << " AND is_phrase = 0 AND is_favorite = 0";
    }
    sql << " ORDER BY is_pinned DESC, created_at DESC, id DESC LIMIT ?;";

    Statement stmt(impl_->db, sql.str().c_str());
    int bind_index = 1;
    if (has_query) {
        BindText(stmt.get(), bind_index++, L"%" + NormalizeWhitespace(query.text) + L"%");
    }
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
    sqlite3_bind_int(stmt.get(), bind_index, std::max(1, query.limit));

    std::vector<HistoryItem> items;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
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
        items.push_back(std::move(item));
    }
    return items;
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
    try {
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

bool HistoryStore::Delete(int64_t id) {
    Statement stmt(impl_->db, "DELETE FROM history_items WHERE id = ?;");
    sqlite3_bind_int64(stmt.get(), 1, id);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        ThrowSqlite(impl_->db, "sqlite delete history failed");
    }
    return sqlite3_changes(impl_->db) > 0;
}

void HistoryStore::Clear() {
    if (!impl_->db) {
        throw std::runtime_error("database is not open");
    }
    Exec(impl_->db, "DELETE FROM history_items WHERE is_favorite = 0;");
}

void HistoryStore::EnforceLimit() {
    const int limit = LoadSettings().history_limit;
    Statement stmt(impl_->db,
                   "DELETE FROM history_items WHERE is_favorite = 0 AND id NOT IN ("
                   "SELECT id FROM history_items WHERE is_favorite = 0 "
                   "ORDER BY is_pinned DESC, created_at DESC, id DESC LIMIT ?"
                   ");");
    sqlite3_bind_int(stmt.get(), 1, std::max(1, limit));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        ThrowSqlite(impl_->db, "sqlite enforce limit failed");
    }
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
