# ClipSoul v1.0.2

## 本版更新

- 添加 Windows manifest，声明普通用户权限、DPI 感知、长路径和 Windows 10/11 兼容信息。
- Release 附件增加 `SHA256SUMS.txt`，用于校验下载到的 `ClipSoul.exe`。
- README 增加安全与校验说明。
- 版本号更新为 `v1.0.2`。

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

## 历史功能

v1.0.1:

- 添加自定义存储位置，默认 exe 所在目录。

v1.0.0:

- 自定义呼出热键，默认 `Alt+C`。
- 多选功能。
- 自定义历史条数，默认 60。
- 筛选功能。
- 收藏夹。
- 固定窗口。
- 搜索功能。
- 历史记录持久化保存。
