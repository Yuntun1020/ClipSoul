#include "TestHarness.h"

#include "ClipSoul/ContinuousPaste.h"

#include <string>
#include <utility>
#include <vector>

namespace {
ClipSoul::HistoryItem Item(int64_t id, std::wstring text) {
    ClipSoul::HistoryItem item;
    item.id = id;
    item.kind = ClipSoul::ClipboardKind::Text;
    item.text = std::move(text);
    return item;
}
} // namespace

TEST_CASE(ContinuousPasteCursorCyclesThroughHistoryOrder) {
    ClipSoul::ContinuousPasteCursor cursor;
    const std::vector<ClipSoul::HistoryItem> items{
        Item(10, L"first"),
        Item(20, L"second"),
        Item(30, L"third"),
    };

    REQUIRE_EQ(cursor.Next(items)->id, static_cast<int64_t>(10));
    REQUIRE_EQ(cursor.Next(items)->id, static_cast<int64_t>(20));
    REQUIRE_EQ(cursor.Next(items)->id, static_cast<int64_t>(30));
    REQUIRE_EQ(cursor.Next(items)->id, static_cast<int64_t>(10));
}

TEST_CASE(ContinuousPasteCursorRestartsWhenListShrinksPastCursor) {
    ClipSoul::ContinuousPasteCursor cursor;
    REQUIRE_EQ(cursor.Next({Item(10, L"first"), Item(20, L"second")})->id, static_cast<int64_t>(10));
    REQUIRE_EQ(cursor.Next({Item(10, L"first"), Item(20, L"second")})->id, static_cast<int64_t>(20));

    REQUIRE_EQ(cursor.Next({Item(99, L"new first")})->id, static_cast<int64_t>(99));
    REQUIRE(!cursor.Next({}).has_value());
}

TEST_CASE(ContinuousPasteStartsFromSelectedRecordThenMovesForward) {
    ClipSoul::ContinuousPasteCursor cursor;
    const std::vector<ClipSoul::HistoryItem> items{
        Item(10, L"first"),
        Item(20, L"second"),
        Item(30, L"third"),
    };

    REQUIRE_EQ(cursor.NextFromSelection(items, 20)->id, static_cast<int64_t>(20));
    REQUIRE_EQ(cursor.NextFromSelection(items, 20)->id, static_cast<int64_t>(30));
    REQUIRE_EQ(cursor.NextFromSelection(items, 20)->id, static_cast<int64_t>(10));
}

TEST_CASE(ContinuousPasteReseedsWhenSelectedRecordChanges) {
    ClipSoul::ContinuousPasteCursor cursor;
    const std::vector<ClipSoul::HistoryItem> items{
        Item(10, L"first"),
        Item(20, L"second"),
        Item(30, L"third"),
    };

    REQUIRE_EQ(cursor.NextFromSelection(items, 10)->id, static_cast<int64_t>(10));
    REQUIRE_EQ(cursor.NextFromSelection(items, 30)->id, static_cast<int64_t>(30));
    REQUIRE_EQ(cursor.NextFromSelection(items, 30)->id, static_cast<int64_t>(10));
}

TEST_CASE(ContinuousPasteFollowsLastPastedIdWhenListOrderRefreshes) {
    ClipSoul::ContinuousPasteCursor cursor;
    const std::vector<ClipSoul::HistoryItem> items{
        Item(10, L"first"),
        Item(20, L"second"),
        Item(30, L"third"),
    };
    const std::vector<ClipSoul::HistoryItem> refreshed{
        Item(40, L"new"),
        Item(10, L"first"),
        Item(20, L"second"),
        Item(30, L"third"),
    };

    REQUIRE_EQ(cursor.NextFromSelection(items, 20)->id, static_cast<int64_t>(20));
    REQUIRE_EQ(cursor.NextFromSelection(refreshed, 20)->id, static_cast<int64_t>(30));
}
