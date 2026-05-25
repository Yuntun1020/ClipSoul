#include "ClipSoul/PasteModel.h"

namespace ClipSoul {
namespace {
bool IsTextLike(ClipboardKind kind) {
    return kind == ClipboardKind::Text || kind == ClipboardKind::Html || kind == ClipboardKind::Link;
}

bool CanSharePasteOperation(ClipboardKind left, ClipboardKind right) {
    if (IsTextLike(left) && IsTextLike(right)) {
        return true;
    }
    return left == ClipboardKind::Files && right == ClipboardKind::Files;
}
} // namespace

MultiPastePayload BuildMultiPastePayload(const std::vector<HistoryItem>& items) {
    MultiPastePayload payload;
    for (const auto& item : items) {
        if (item.kind == ClipboardKind::Files) {
            payload.files.insert(payload.files.end(), item.files.begin(), item.files.end());
            continue;
        }
        if (item.kind == ClipboardKind::Image) {
            if (!payload.first_image) {
                payload.first_image = item;
            }
            continue;
        }

        const std::wstring text = !item.text.empty() ? item.text : item.search_text;
        if (!text.empty()) {
            if (!payload.text.empty()) {
                payload.text += L"\r\n";
            }
            payload.text += text;
        }
    }
    return payload;
}

std::vector<MultiPasteOperation> BuildMultiPasteOperations(const std::vector<HistoryItem>& items) {
    std::vector<MultiPasteOperation> operations;
    for (const auto& item : items) {
        if (!operations.empty() && !operations.back().items.empty() &&
            CanSharePasteOperation(operations.back().items.back().kind, item.kind)) {
            operations.back().items.push_back(item);
            continue;
        }
        operations.push_back(MultiPasteOperation{{item}});
    }
    return operations;
}

} // namespace ClipSoul
