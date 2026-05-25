#pragma once

#include "ClipSoul/HistoryStore.h"

#include <optional>
#include <vector>

namespace ClipSoul {

class ContinuousPasteCursor {
public:
    std::optional<HistoryItem> Next(const std::vector<HistoryItem>& items);
    std::optional<HistoryItem> NextFromSelection(const std::vector<HistoryItem>& items,
                                                 std::optional<int64_t> selected_id);
    void Reset();

private:
    size_t next_index_ = 0;
    std::optional<int64_t> active_anchor_id_;
    std::optional<int64_t> last_returned_id_;
};

} // namespace ClipSoul
