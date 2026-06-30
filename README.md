# ClipSoul

ClipSoul 是一个 Windows 原生剪贴板历史工具，目标是轻量、快速、常驻后台，并突破 Windows 自带剪贴板历史 25 条限制。
<img width="510" height="840" alt="image" src="https://github.com/user-attachments/assets/a1414e99-be75-4937-861f-8c7996222c47" />
<img width="639" height="638" alt="image" src="https://github.com/user-attachments/assets/9b571c73-90d7-4c84-987c-292d5c58fac7" />



## 特性

- C++/Win32 原生应用，MSVC + CMake 构建
- 托盘常驻，默认 `Alt+C` 呼出/隐藏
- 连续粘贴快捷键，默认 `Ctrl+Alt+V`
- 窄小 Win+V 风格弹窗，亚克力/磨砂玻璃视觉
- 历史记录持久化保存，支持展开查看长内容
- 支持文本、链接、图片、文件历史
- 搜索、筛选、收藏夹分组、固定窗口
- 收藏内容和常用语备注
- 多选、全选、选中删除、选中粘贴
- 自定义历史条数，默认 60 条
- 自定义存储位置，默认 exe 所在目录
- 单实例运行，重复打开 exe 会提示程序已启动

## 构建

需要 Windows、Visual Studio 2022/MSVC、CMake 3.25+。

```powershell
cmake -S . -B build
cmake --build build --config Release --target clipsoul
```

生成文件：

```text
build\Release\clipsoul.exe
```

## 分发

发布版本只需要分发单个 exe：

```text
ClipSoul.exe
```

首次启动后，默认会在 `ClipSoul.exe` 所在目录生成：

```text
clipsoul.db
cache\
```

如果在设置中选择了自定义存储位置，会在 exe 所在目录写入：

```text
clipsoul.storage
```

修改存储位置后需要重启程序生效。

## 安全与校验

ClipSoul 是开源的本地剪贴板工具，不联网。它会常驻后台、监听剪贴板变化、注册全局热键，并在用户选择历史项时写入剪贴板和触发粘贴。这些行为和部分自动化/监控类软件相似，未签名版本可能被 Windows Defender 或 SmartScreen 误报。

发布页会同时提供：

```text
ClipSoul.exe
SHA256SUMS.txt
```

下载后建议先校验哈希：

```powershell
Get-FileHash -Algorithm SHA256 .\ClipSoul.exe
```

确认输出值与 `SHA256SUMS.txt` 中的值一致后再运行。如果哈希不一致，请删除文件并重新从 GitHub Release 下载。

当前发布版暂未代码签名。如果 Windows 拦截但哈希一致，通常是未签名新程序的信誉/误报问题；仍建议只从官方 Release 下载，不要从聊天软件缓存目录或第三方网盘直接运行。

## 测试

```powershell
cmake --build build --config Debug --target clipsoul_tests
ctest --test-dir build -C Debug --output-on-failure
```
