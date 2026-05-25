# ClipSoul v1.1.0

## 本版更新

- 修复自定义热键捕获，支持两个键和三个键组合。
- 添加历史记录展开显示，并修复历史记录选择高亮。
- 优化设置窗口布局与交互。
- 添加记录展开功能，长按选择记录时不触发粘贴。
- 添加收藏夹分组，可将记录加入不同分组并删除分组及其内容。
- 收藏内容和常用语支持备注。
- 添加连续粘贴快捷键，默认 `Ctrl+Alt+V`。
- 修复多开问题，重复打开 exe 会提示程序已启动。
- 双击 exe 打开已有程序时给出反馈。
- 清理未使用代码，整理发布产物。

## 下载文件

- `ClipSoul.exe`
- `SHA256SUMS.txt`

## 校验方式

下载后在 PowerShell 执行：

```powershell
Get-FileHash -Algorithm SHA256 .\ClipSoul.exe
```

确认输出值与 `SHA256SUMS.txt` 一致后再运行。

## 安全说明

ClipSoul 是开源的本地剪贴板工具，不联网。由于它会常驻后台、监听剪贴板、注册全局热键，并在用户选择历史项时写入剪贴板和触发粘贴，未签名版本可能被 Windows Defender 或 SmartScreen 误报。

如果 Windows 拦截但 SHA256 与 Release 提供的 `SHA256SUMS.txt` 一致，通常是未签名新程序的信誉/误报问题。仍建议只从本 GitHub Release 下载，不要从聊天软件缓存目录或第三方网盘直接运行。
