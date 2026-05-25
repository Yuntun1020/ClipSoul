#pragma once

#include "ClipSoul/HistoryStore.h"

#include <optional>
#include <string>
#include <vector>

namespace ClipSoul {

struct MultiPastePayload {
    std::wstring text;
    std::vector<std::wstring> files;
    std::optional<HistoryItem> first_image;
};

struct MultiPasteOperation {
    std::vector<HistoryItem> items;
};

MultiPastePayload BuildMultiPastePayload(const std::vector<HistoryItem>& items);
std::vector<MultiPasteOperation> BuildMultiPasteOperations(const std::vector<HistoryItem>& items);

} // namespace ClipSoul
