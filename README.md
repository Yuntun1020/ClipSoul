# ClipSoul

ClipSoul 是一个 Windows 原生剪贴板历史工具，目标是轻量、快速、常驻后台，并突破 Windows 自带剪贴板历史 25 条限制。

## 特性

- C++/Win32 原生应用，MSVC + CMake 构建
- 托盘常驻，默认 `Alt+C` 呼出/隐藏
- 窄小 Win+V 风格弹窗，亚克力/磨砂玻璃视觉
- 历史记录持久化保存
- 支持文本、链接、图片、文件历史
- 搜索、筛选、收藏夹、固定窗口
- 多选、全选、选中删除、选中粘贴
- 自定义历史条数，默认 60 条
- 自定义存储位置，默认 exe 所在目录

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

## 测试

```powershell
cmake --build build --config Debug --target clipsoul_tests
ctest --test-dir build -C Debug --output-on-failure
```
