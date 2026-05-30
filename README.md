<p align="center">
  <img src="LlaShell.png" width="256" height="256" alt="LlaShell Logo">
</p>

<h3 align="center" style="color:#888888">一款开源，好用，占用低的第三方资源管理器</h3>

<p align="center" style="color:#666666">由 lladlam 开发</p>

<p align="center">
  <a href="https://github.com/lladlam/LlaShell/releases">
    <img src="https://img.shields.io/badge/正式版-0.0.0.1-blue?style=flat-square" alt="Release">
  </a>
  <a href="https://github.com/lladlam/LlaShell/releases">
    <img src="https://img.shields.io/badge/测试版-0.0.0.1--beta-orange?style=flat-square" alt="Pre-release">
  </a>
</p>

<p align="center">
  <a href="https://github.com/lladlam/LlaShell/blob/main/LICENSE">
    <img src="https://img.shields.io/badge/许可证-Apache%202.0-green?style=flat-square" alt="License">
  </a>
</p>

---

## LlaShell

Windows Shell 替换 — 多进程架构，纯 Win32 SDK + C++20，无 COM、无 UWP。

## 组件

| 组件 | 类型 | 说明 |
|------|------|------|
| `LlaShell.exe` | WIN32 | 主应用：启动所有进程、托盘图标、设置窗口 |
| `CoreIPC.dll` | DLL | 进程间通信库（命名管道 IOCP + 共享内存环形缓冲区） |
| `Desktop.exe` | WIN32 | 桌面窗口，显示桌面图标，双击文件夹通过 IPC 通知 MainUI |
| `Taskbar.exe` | WIN32 | Win10 风格任务栏，EnumWindows 动态识别窗口，固定程序，系统托盘 |
| `MainUI.exe` | WIN32 | 文件管理器，标签页、目录树、异步扫描、缩略图 |
| `ShellHost.exe` | Console | Shell 扩展宿主，通过 IPC 接收右键菜单请求 |
| `MediaParser.exe` | Console | 媒体解析服务，调用 FFmpeg 生成视频/图片缩略图 |
| `Updater.exe` | WIN32 | 静默更新器，下载 zip/rar 发布包并替换文件 |

## IPC 架构

```
┌─────────────────────────────────────────────────────────┐
│                    公共 C API (CoreIPC.h)                 │
│  IPC_Initialize / IPC_StartServer / IPC_Connect / ...   │
└──────────────────────────┬──────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────┐
│  PipeChannel (IOCP 命名管道)  │  ShmChannel (共享内存)    │
│  \\.\pipe\LlaShell\IPC\v1\   │  Local\LlaShmem.v1.      │
└───────────────────────────────┴──────────────────────────┘
```

连接拓扑:

```
LlaShell.exe (主应用/启动器)
    ├── Desktop.exe  ←→  MainUI.exe  ←→  Taskbar.exe
    │                      ↕
    │                  ShellHost.exe
    │                      ↕
    │                 MediaParser.exe
    └── Updater.exe (定时触发)
```

## 环境要求

- **操作系统**: Windows 10 或更高版本
- **编译器**: MSVC 2019+（推荐）或 MinGW-w64 GCC 12+
- **构建系统**: CMake 3.20+
- **运行时（可选）**: `ffmpeg.exe` — 用于视频/图片缩略图生成，需在 PATH 中或放在程序目录

## 构建

### MSVC

```powershell
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### MinGW-w64

```powershell
cmake -B build -S . -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 构建输出

```
build/
  LlaShell.exe    # 主应用（带 ICO 图标 + 版本信息）
  CoreIPC.dll     # IPC 库
  Desktop.exe     # 桌面
  Taskbar.exe     # 任务栏
  MainUI.exe      # 文件管理器
  ShellHost.exe   # Shell 扩展宿主
  MediaParser.exe # 媒体解析
  Updater.exe     # 更新器
```

## 项目结构

```
LlaShell/
├── CMakeLists.txt
├── LlaShell.sln
├── LICENSE                          # Apache License 2.0
├── THIRD_PARTY_LICENSES.txt         # 第三方组件许可说明
├── LlaShell.png                     # 应用图标源文件
├── resources/
│   ├── LlaShell.ico                 # 多尺寸 ICO (256/128/64/32/16)
│   ├── LlaShell.rc                  # 主应用资源（图标+版本信息）
│   └── version_only.rc              # 其他 exe 版本信息
├── LlaShellApp/src/main.cpp         # 主应用（启动器+托盘+设置）
├── CoreIPC/                         # IPC 库
│   ├── coreipc.def
│   ├── include/CoreIPC/
│   │   ├── CoreIPC.h
│   │   ├── types.h
│   │   ├── protocol.h
│   │   ├── pipe_channel.h
│   │   ├── shm_channel.h
│   │   └── guardian.h
│   └── src/
│       ├── coreipc.cpp
│       ├── pipe_server.cpp
│       ├── shm_manager.cpp
│       ├── shm_slot.cpp
│       ├── crc32.cpp
│       └── guardian.cpp
├── Desktop/
│   ├── include/desktop.h
│   └── src/desktop_main.cpp
├── Taskbar/
│   ├── include/taskbar.h
│   └── src/taskbar_main.cpp
├── MainUI/
│   ├── include/mainui.h
│   └── src/mainui_main.cpp
├── ShellHost/src/shellhost_main.cpp
├── MediaParser/src/mediaparser_main.cpp
├── Updater/src/updater_main.cpp
└── Shared/
    ├── ipc_messages.h
    └── shell_utils.h
```

## 第三方开源软件

### FFmpeg

MediaParser 使用 [FFmpeg](https://ffmpeg.org/) 生成视频和图片缩略图。

**调用方式**: 以独立进程方式调用 `ffmpeg.exe` CLI，不进行任何静态或动态链接。

**调用位置**:
- 文件: `MediaParser/src/mediaparser_main.cpp`
- 函数: `MediaParserService::TryFFmpegThumbnail()`
- 机制: `CreateProcessW("ffmpeg.exe -i <input> -vframes 1 -vf scale=W:H output.bmp")`
- 回退: 若 ffmpeg.exe 不存在，自动降级为 SHGetFileInfoW 系统图标

**许可证合规说明**:
- LlaShell **不修改** FFmpeg 源代码
- LlaShell **不链接** FFmpeg 库（静态或动态）
- FFmpeg 作为独立进程运行，仅通过磁盘临时文件通信
- 若您分发 ffmpeg.exe，须遵守 FFmpeg 许可证条款
- 若您的 ffmpeg.exe 包含 GPL 编解码器（如 x264/x265），须遵守 GPL v2
- 若为纯 LGPL 构建，遵守 LGPL v2.1 即可
- FFmpeg 源码: https://github.com/FFmpeg/FFmpeg

详见 [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt)

## 捐赠

如果 LlaShell 对你有帮助，欢迎通过 [afdian.lladlam.top](https://afdian.lladlam.top) 支持开发。

## 许可证

Copyright (C) lladlam. All rights reserved.

本项目基于 [Apache License 2.0](LICENSE) 发布。
