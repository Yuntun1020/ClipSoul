#include "TestHarness.h"

#include "ClipSoul/HistoryStore.h"
#include "ClipSoul/TextUtil.h"

#include <filesystem>

namespace {
std::filesystem::path TempDbPath(const wchar_t* name) {
    auto path = std::filesystem::temp_directory_path() / L"ClipSoulTests" / name;
    std::filesystem::create_directories(path.parent_path());
    std::filesystem::remove(path);
    return path;
}

ClipSoul::CapturedContent TextContent(std::wstring text) {
    ClipSoul::CapturedContent content;
    content.kind = ClipSoul::ClipboardKind::Text;
    content.text = std::move(text);
    content.preview = content.text;
    content.search_text = content.text;
    content.content_hash = ClipSoul::StableHash(content.text);
    return content;
}
} // namespace

TEST_CASE(HistoryStoreDefaultsToSixtyItems) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"default-limit.db"));

    REQUIRE_EQ(store.LoadSettings().history_limit, 60);
}

TEST_CASE(HistoryStoreDeduplicatesConsecutiveCopies) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"dedupe.db"));

    REQUIRE(store.Add(TextContent(L"alpha")));
    REQUIRE(store.Add(TextContent(L"alpha")));

    const auto items = store.Recent(10, L"");
    REQUIRE_EQ(items.size(), static_cast<size_t>(1));
    REQUIRE_EQ(items.front().text, std::wstring(L"alpha"));
}

TEST_CASE(HistoryStoreEnforcesConfiguredLimit) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"limit.db"));
    auto settings = store.LoadSettings();
    settings.history_limit = 2;
    store.SaveSettings(settings);

    REQUIRE(store.Add(TextContent(L"one")));
    REQUIRE(store.Add(TextContent(L"two")));
    REQUIRE(store.Add(TextContent(L"three")));

    const auto items = store.Recent(10, L"");
    REQUIRE_EQ(items.size(), static_cast<size_t>(2));
    REQUIRE_EQ(items[0].text, std::wstring(L"three"));
    REQUIRE_EQ(items[1].text, std::wstring(L"two"));
}

TEST_CASE(HistoryStoreSearchesTextAndFileNames) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"search.db"));

    auto files = TextContent(L"");
    files.kind = ClipSoul::ClipboardKind::Files;
    files.files = {L"C:\\Work\\proposal.docx", L"C:\\Work\\image.png"};
    files.preview = L"proposal.docx, image.png";
    files.search_text = L"proposal.docx image.png";
    files.content_hash = ClipSoul::StableHash(files.search_text);

    REQUIRE(store.Add(TextContent(L"plain alpha")));
    REQUIRE(store.Add(files));

    const auto items = store.Recent(10, L"proposal");
    REQUIRE_EQ(items.size(), static_cast<size_t>(1));
    REQUIRE_EQ(items.front().kind, ClipSoul::ClipboardKind::Files);
}

TEST_CASE(HistoryStoreClearRemovesAllEntries) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"clear.db"));

    REQUIRE(store.Add(TextContent(L"alpha")));
    store.Clear();

    REQUIRE_EQ(store.Recent(10, L"").size(), static_cast<size_t>(0));
}

TEST_CASE(HistoryStoreFiltersByKindAndFavorite) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"filters.db"));

    REQUIRE(store.Add(TextContent(L"plain text")));
    auto link = TextContent(L"https://example.com");
    link.kind = ClipSoul::ClipboardKind::Link;
    REQUIRE(store.Add(link));

    const auto all = store.Query(ClipSoul::HistoryQuery{});
    REQUIRE_EQ(all.size(), static_cast<size_t>(2));

    ClipSoul::HistoryQuery link_query;
    link_query.kinds.insert(ClipSoul::ClipboardKind::Link);
    const auto links = store.Query(link_query);
    REQUIRE_EQ(links.size(), static_cast<size_t>(1));
    REQUIRE_EQ(links.front().kind, ClipSoul::ClipboardKind::Link);

    REQUIRE(store.SetFavorite(links.front().id, true));
    ClipSoul::HistoryQuery favorite_query;
    favorite_query.favorites_only = true;
    const auto favorites = store.Query(favorite_query);
    REQUIRE_EQ(favorites.size(), static_cast<size_t>(1));
    REQUIRE(favorites.front().is_favorite);
}

TEST_CASE(HistoryStoreAssignsFavoritesToNamedGroupsWithNotes) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"favorite-groups.db"));

    const auto work_group = store.EnsureFavoriteGroup(L"工作");
    const auto personal_group = store.EnsureFavoriteGroup(L"个人");

    REQUIRE(store.Add(TextContent(L"alpha")));
    REQUIRE(store.Add(TextContent(L"beta")));

    const auto history = store.Query(ClipSoul::HistoryQuery{});
    REQUIRE_EQ(history.size(), static_cast<size_t>(2));
    const auto beta_id = history[0].id;
    const auto alpha_id = history[1].id;

    REQUIRE(store.SetFavoriteGroup(alpha_id, work_group));
    REQUIRE(store.SetNote(alpha_id, L"报价模板"));
    REQUIRE(store.SetFavoriteGroup(beta_id, personal_group));

    ClipSoul::HistoryQuery query;
    query.favorites_only = true;
    query.favorite_group_id = work_group;
    const auto favorites = store.Query(query);

    REQUIRE_EQ(favorites.size(), static_cast<size_t>(1));
    REQUIRE_EQ(favorites.front().text, std::wstring(L"alpha"));
    REQUIRE_EQ(favorites.front().favorite_group_id.value(), work_group);
    REQUIRE_EQ(favorites.front().note, std::wstring(L"报价模板"));
}

TEST_CASE(HistoryStoreDeletesFavoriteGroupAndItsFavorites) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"favorite-group-delete.db"));

    const auto work_group = store.EnsureFavoriteGroup(L"工作");
    REQUIRE(store.Add(TextContent(L"alpha")));

    const auto history = store.Query(ClipSoul::HistoryQuery{});
    REQUIRE_EQ(history.size(), static_cast<size_t>(1));
    REQUIRE(store.SetFavoriteGroup(history.front().id, work_group));
    REQUIRE(store.DeleteFavoriteGroup(work_group));

    const auto groups = store.FavoriteGroups();
    REQUIRE(std::none_of(groups.begin(), groups.end(), [work_group](const ClipSoul::FavoriteGroup& group) {
        return group.id == work_group;
    }));

    ClipSoul::HistoryQuery all_favorites;
    all_favorites.favorites_only = true;
    REQUIRE(store.Query(all_favorites).empty());
    REQUIRE(store.Query(ClipSoul::HistoryQuery{}).empty());

    ClipSoul::HistoryQuery deleted_group_query;
    deleted_group_query.favorites_only = true;
    deleted_group_query.favorite_group_id = work_group;
    REQUIRE(store.Query(deleted_group_query).empty());
}

TEST_CASE(HistoryStoreAddsFavoritePhraseWithNoteAndGroup) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"favorite-phrase-note-group.db"));

    const auto support_group = store.EnsureFavoriteGroup(L"客服");
    REQUIRE(store.AddFavoritePhrase(L"support@example.com", L"客服邮箱", support_group));

    ClipSoul::HistoryQuery query;
    query.favorites_only = true;
    query.favorite_group_id = support_group;
    const auto favorites = store.Query(query);

    REQUIRE_EQ(favorites.size(), static_cast<size_t>(1));
    REQUIRE_EQ(favorites.front().text, std::wstring(L"support@example.com"));
    REQUIRE_EQ(favorites.front().note, std::wstring(L"客服邮箱"));
    REQUIRE_EQ(favorites.front().favorite_group_id.value(), support_group);
    REQUIRE(favorites.front().is_phrase);
}

TEST_CASE(HistoryStorePreservesNoteLineBreaks) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"multiline-note.db"));

    REQUIRE(store.Add(TextContent(L"alpha")));
    const auto history = store.Query(ClipSoul::HistoryQuery{});
    REQUIRE_EQ(history.size(), static_cast<size_t>(1));

    const std::wstring note = L"第一行\n第二行\n第三行";
    REQUIRE(store.SetNote(history.front().id, note));
    const auto saved = store.Get(history.front().id);
    REQUIRE(saved.has_value());
    REQUIRE_EQ(saved->note, note);

    REQUIRE(store.AddFavoritePhrase(L"phrase", L"备注一\n备注二", std::nullopt));
    ClipSoul::HistoryQuery query;
    query.favorites_only = true;
    const auto favorites = store.Query(query);
    REQUIRE_EQ(favorites.front().note, std::wstring(L"备注一\n备注二"));
}

TEST_CASE(HistoryStoreNormalizesWindowsEditNoteLineBreaks) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"windows-edit-note.db"));

    REQUIRE(store.Add(TextContent(L"alpha")));
    const auto history = store.Query(ClipSoul::HistoryQuery{});
    REQUIRE_EQ(history.size(), static_cast<size_t>(1));

    REQUIRE(store.SetNote(history.front().id, L"first line\r\nsecond line\r\nthird line"));
    const auto saved = store.Get(history.front().id);
    REQUIRE(saved.has_value());
    REQUIRE_EQ(saved->note, std::wstring(L"first line\nsecond line\nthird line"));

    REQUIRE(store.AddFavoritePhrase(L"phrase", L"note one\r\nnote two", std::nullopt));
    ClipSoul::HistoryQuery query;
    query.favorites_only = true;
    const auto favorites = store.Query(query);
    REQUIRE_EQ(favorites.front().note, std::wstring(L"note one\nnote two"));
}

TEST_CASE(HistoryStoreAddsFavoritePhraseAsFavoriteText) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"favorite-phrase.db"));

    REQUIRE(store.AddFavoritePhrase(L"support@example.com"));

    const auto history = store.Query(ClipSoul::HistoryQuery{});
    REQUIRE_EQ(history.size(), static_cast<size_t>(0));

    ClipSoul::HistoryQuery query;
    query.favorites_only = true;
    const auto favorites = store.Query(query);
    REQUIRE_EQ(favorites.size(), static_cast<size_t>(1));
    REQUIRE_EQ(favorites.front().kind, ClipSoul::ClipboardKind::Text);
    REQUIRE_EQ(favorites.front().text, std::wstring(L"support@example.com"));
    REQUIRE_EQ(favorites.front().preview, std::wstring(L"support@example.com"));
    REQUIRE(favorites.front().is_favorite);
}

TEST_CASE(HistoryStoreConvertsExistingHistoryItemWhenAddingFavoritePhrase) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"favorite-phrase-existing.db"));

    REQUIRE(store.Add(TextContent(L"meeting shortcut")));
    REQUIRE_EQ(store.Query(ClipSoul::HistoryQuery{}).size(), static_cast<size_t>(1));

    REQUIRE(store.AddFavoritePhrase(L"meeting shortcut"));
    REQUIRE_EQ(store.Query(ClipSoul::HistoryQuery{}).size(), static_cast<size_t>(0));

    ClipSoul::HistoryQuery query;
    query.favorites_only = true;
    const auto favorites = store.Query(query);
    REQUIRE_EQ(favorites.size(), static_cast<size_t>(1));
    REQUIRE_EQ(favorites.front().text, std::wstring(L"meeting shortcut"));
    REQUIRE(favorites.front().is_phrase);
}

TEST_CASE(HistoryStorePinnedItemsSortBeforeRecentItems) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"pinned.db"));

    REQUIRE(store.Add(TextContent(L"older")));
    REQUIRE(store.Add(TextContent(L"newer")));
    const auto before = store.Query(ClipSoul::HistoryQuery{});
    REQUIRE_EQ(before.front().text, std::wstring(L"newer"));

    REQUIRE(store.SetPinned(before.back().id, true));
    const auto after = store.Query(ClipSoul::HistoryQuery{});
    REQUIRE_EQ(after.front().text, std::wstring(L"older"));
    REQUIRE(after.front().is_pinned);
}

TEST_CASE(HistoryStoreFiltersByDateRange) {
    ClipSoul::HistoryStore store;
    store.Open(TempDbPath(L"date-filter.db"));

    auto older = TextContent(L"older");
    older.created_at_unix = 100;
    auto newer = TextContent(L"newer");
    newer.created_at_unix = 300;
    REQUIRE(store.Add(older));
    REQUIRE(store.Add(newer));

    ClipSoul::HistoryQuery query;
    query.start_unix = 200;
    query.end_unix = 400;
    const auto items = store.Query(query);
    REQUIRE_EQ(items.size(), static_cast<size_t>(1));
    REQUIRE_EQ(items.front().text, std::wstring(L"newer"));
}

TEST_CASE(HistorySelectionTracksMultiSelectActions) {
    ClipSoul::HistorySelection selection;
    REQUIRE(!selection.IsSelected(42));
    selection.Toggle(42);
    REQUIRE(selection.IsSelected(42));
    selection.Toggle(7);
    REQUIRE_EQ(selection.SelectedIds().size(), static_cast<size_t>(2));
    selection.Clear();
    REQUIRE_EQ(selection.SelectedIds().size(), static_cast<size_t>(0));
}
