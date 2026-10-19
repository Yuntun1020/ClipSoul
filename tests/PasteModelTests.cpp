#include "TestHarness.h"

#include "ClipSoul/PasteModel.h"
#include "ClipSoul/PasteController.h"

namespace {
ClipSoul::HistoryItem TextItem(int64_t id, std::wstring text) {
    ClipSoul::HistoryItem item;
    item.id = id;
    item.kind = ClipSoul::ClipboardKind::Text;
    item.text = std::move(text);
    return item;
}

ClipSoul::HistoryItem LinkItem(int64_t id, std::wstring url) {
    ClipSoul::HistoryItem item;
    item.id = id;
    item.kind = ClipSoul::ClipboardKind::Link;
    item.text = std::move(url);
    return item;
}

ClipSoul::HistoryItem FileItem(int64_t id, std::vector<std::wstring> files) {
    ClipSoul::HistoryItem item;
    item.id = id;
    item.kind = ClipSoul::ClipboardKind::Files;
    item.files = std::move(files);
    return item;
}

ClipSoul::HistoryItem ImageItem(int64_t id) {
    ClipSoul::HistoryItem item;
    item.id = id;
    item.kind = ClipSoul::ClipboardKind::Image;
    item.payload_path = L"C:\\Temp\\image.dib";
    return item;
}
} // namespace

TEST_CASE(MultiPastePayloadCombinesTextAndLinksInListOrder) {
    const auto payload = ClipSoul::BuildMultiPastePayload({
        TextItem(1, L"第一条"),
        LinkItem(2, L"https://example.com/long/path?query=1"),
        TextItem(3, L"第三条"),
    });

    REQUIRE_EQ(payload.text, std::wstring(L"第一条\r\nhttps://example.com/long/path?query=1\r\n第三条"));
    REQUIRE(payload.files.empty());
    REQUIRE(!payload.first_image);
}

TEST_CASE(PasteTargetActivationSkipsAlreadyForegroundWindow) {
    HWND foreground = reinterpret_cast<HWND>(0x1234);
    HWND target = foreground;

    REQUIRE(!ClipSoul::ShouldActivatePasteTarget(target, foreground));
    REQUIRE(!ClipSoul::ShouldActivatePasteTarget(nullptr, foreground));
    REQUIRE(ClipSoul::ShouldActivatePasteTarget(reinterpret_cast<HWND>(0x5678), foreground));
}

TEST_CASE(PasteShortcutReleasesAltLikeModifiersBeforeCtrlV) {
    // Alt 的按下从没送到目标程序（钩子吞掉了），所以既不该「临时松开」也不该「还原」，
    // 否则目标程序会收到没有配对按下的 KEYUP / 没有配对抬起的 KEYDOWN。
    REQUIRE(!ClipSoul::ShouldReleaseModifierForPaste(VK_MENU, true));
    REQUIRE(ClipSoul::ShouldReleaseModifierForPaste(VK_SHIFT, true));
    REQUIRE(ClipSoul::ShouldReleaseModifierForPaste(VK_LWIN, true));
    REQUIRE(!ClipSoul::ShouldReleaseModifierForPaste(VK_CONTROL, true));
    REQUIRE(!ClipSoul::ShouldReleaseModifierForPaste(VK_MENU, false));
}

TEST_CASE(PasteShortcutRestoresHeldModifiersAfterCtrlV) {
    REQUIRE(ClipSoul::ShouldRestoreModifierAfterPaste(VK_CONTROL, true));
    REQUIRE(ClipSoul::ShouldRestoreModifierAfterPaste(VK_MENU, true));
    REQUIRE(ClipSoul::ShouldRestoreModifierAfterPaste(VK_SHIFT, true));
    REQUIRE(!ClipSoul::ShouldRestoreModifierAfterPaste('V', true));
    REQUIRE(!ClipSoul::ShouldRestoreModifierAfterPaste(VK_MENU, false));
}

TEST_CASE(PasteNeverLeavesAltHeldToAvoidStuckModifierState) {
    // 永劫无间里呼出剪贴板后 Alt 卡长按：钩子吞掉了 Alt 的按下，目标程序从没收到过，
    // 而 SendPaste() 事后却注入一个 Alt 按下表示「用户还按着」，于是对方认为 Alt 卡住了。
    // 现在 Alt 一律不还原、也不临时松开；Ctrl / Shift 等保持原样。
    ClipSoul::PasteShortcutOptions normal;
    ClipSoul::PasteShortcutOptions continuous;
    continuous.restore_alt_after_paste = false;

    REQUIRE(!ClipSoul::ShouldRestoreModifierForPaste(VK_MENU, true, normal));
    REQUIRE(!ClipSoul::ShouldRestoreModifierForPaste(VK_MENU, true, continuous));
    REQUIRE(ClipSoul::ShouldRestoreModifierForPaste(VK_CONTROL, true, normal));
    REQUIRE(ClipSoul::ShouldRestoreModifierForPaste(VK_SHIFT, true, normal));
    REQUIRE(!ClipSoul::ShouldRestoreModifierForPaste('V', true, normal));
}

TEST_CASE(MultiPastePayloadKeepsFilesAlongsideCombinedText) {
    const auto payload = ClipSoul::BuildMultiPastePayload({
        TextItem(1, L"说明"),
        FileItem(2, {L"C:\\Work\\会议纪要.docx", L"C:\\Work\\预算表.xlsx"}),
    });

    REQUIRE_EQ(payload.text, std::wstring(L"说明"));
    REQUIRE_EQ(payload.files.size(), static_cast<size_t>(2));
    REQUIRE_EQ(payload.files[0], std::wstring(L"C:\\Work\\会议纪要.docx"));
    REQUIRE_EQ(payload.files[1], std::wstring(L"C:\\Work\\预算表.xlsx"));
}

TEST_CASE(MultiPasteOperationsCombineOnlyHomogeneousTextLikeSelection) {
    const auto operations = ClipSoul::BuildMultiPasteOperations({
        TextItem(1, L"alpha"),
        LinkItem(2, L"https://example.com"),
    });

    REQUIRE_EQ(operations.size(), static_cast<size_t>(1));
    REQUIRE_EQ(operations.front().items.size(), static_cast<size_t>(2));
}

TEST_CASE(MultiPasteOperationsPreserveMixedSelectionOrder) {
    const auto operations = ClipSoul::BuildMultiPasteOperations({
        TextItem(1, L"alpha"),
        FileItem(2, {L"C:\\Work\\a.txt"}),
        ImageItem(3),
    });

    REQUIRE_EQ(operations.size(), static_cast<size_t>(3));
    REQUIRE_EQ(operations[0].items.front().id, static_cast<int64_t>(1));
    REQUIRE_EQ(operations[1].items.front().id, static_cast<int64_t>(2));
    REQUIRE_EQ(operations[2].items.front().id, static_cast<int64_t>(3));
}

TEST_CASE(MultiPasteOperationsPasteMultipleImagesIndividually) {
    const auto operations = ClipSoul::BuildMultiPasteOperations({
        ImageItem(1),
        ImageItem(2),
    });

    REQUIRE_EQ(operations.size(), static_cast<size_t>(2));
    REQUIRE_EQ(operations[0].items.front().id, static_cast<int64_t>(1));
    REQUIRE_EQ(operations[1].items.front().id, static_cast<int64_t>(2));
}
