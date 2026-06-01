# ClipSoul v1.2.0

## 本版更新

### 新增

- 添加设置页访问项目仓库链接。
- 添加右键记录上移、下移功能，可手动调整记录排序。

### 修复

- 修复部分已知 bug。

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
