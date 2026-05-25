#include "ClipSoul/ContinuousPaste.h"

#include <algorithm>

namespace ClipSoul {

std::optional<HistoryItem> ContinuousPasteCursor::Next(const std::vector<HistoryItem>& items) {
    if (items.empty()) {
        next_index_ = 0;
        active_anchor_id_.reset();
        last_returned_id_.reset();
        return std::nullopt;
    }
    if (next_index_ >= items.size()) {
        next_index_ = 0;
    }
    auto item = items[next_index_];
    next_index_ = (next_index_ + 1) % items.size();
    last_returned_id_ = item.id;
    return item;
}

std::optional<HistoryItem> ContinuousPasteCursor::NextFromSelection(const std::vector<HistoryItem>& items,
                                                                    std::optional<int64_t> selected_id) {
    if (items.empty()) {
        next_index_ = 0;
        active_anchor_id_.reset();
        last_returned_id_.reset();
        return std::nullopt;
    }

    if (selected_id && active_anchor_id_ != selected_id) {
        const auto found = std::find_if(items.begin(), items.end(), [selected_id](const HistoryItem& item) {
            return item.id == *selected_id;
        });
        next_index_ = found == items.end() ? 0 : static_cast<size_t>(std::distance(items.begin(), found));
        active_anchor_id_ = selected_id;
    } else if (last_returned_id_) {
        const auto found = std::find_if(items.begin(), items.end(), [this](const HistoryItem& item) {
            return item.id == *last_returned_id_;
        });
        if (found != items.end()) {
            next_index_ = (static_cast<size_t>(std::distance(items.begin(), found)) + 1) % items.size();
        }
    } else if (next_index_ >= items.size()) {
        next_index_ = 0;
    }

    auto item = items[next_index_];
    next_index_ = (next_index_ + 1) % items.size();
    last_returned_id_ = item.id;
    return item;
}

void ContinuousPasteCursor::Reset() {
    next_index_ = 0;
    active_anchor_id_.reset();
    last_returned_id_.reset();
}

} // namespace ClipSoul
