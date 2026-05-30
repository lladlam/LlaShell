# LlaShell

Windows Shell 替换 — 多进程架构，纯 Win32 SDK + C++20，无 COM、无 UWP。

## 组件

| 组件 | 类型 | 说明 |
|------|------|------|
| `CoreIPC.dll` | DLL | 进程间通信库（命名管道 IOCP + 共享内存环形缓冲区） |
| `Desktop.exe` | WIN32 | 桌面窗口，显示桌面图标，双击文件夹通过 IPC 通知 MainUI 打开 |
| `Taskbar.exe` | WIN32 | 任务栏，动态跟踪 MainUI 窗口按钮，全屏检测，系统托盘 |
| `MainUI.exe` | WIN32 | 文件管理器，标签页、目录树、异步扫描、缩略图显示 |
| `ShellHost.exe` | Console | Shell 扩展宿主，通过 IPC 接收右键菜单请求 |
| `MediaParser.exe` | Console | 媒体解析服务，通过 FFmpeg 生成视频/图片缩略图 |

## IPC 架构

```
┌─────────────────────────────────────────────────────────┐
│                    公共 C API (CoreIPC.h)                 │
│  IPC_Initialize / IPC_StartServer / IPC_Connect / ...   │
└──────────────────────────┬──────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│              IPCState (全局单例)                          │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ PipeChannel   │  │ ShmChannel   │  │   Guardian    │  │
│  │ (命名管道)     │  │ (共享内存)    │  │ (进程守护)    │  │
│  └──────┬───────┘  └──────┬───────┘  └──────┬────────┘  │
└─────────┼─────────────────┼─────────────────┼───────────┘
          │                 │                 │
          ▼                 ▼                 ▼
   Windows IOCP      CreateFileMapping   WaitForMultipleObjects
   Named Pipes       MapViewOfFile       CreateProcessW
```

### 连接拓扑

```
Desktop  ←→  MainUI  ←→  Taskbar
               ↕
           ShellHost
               ↕
          MediaParser
```

### 消息协议

应用层消息通过 `MsgType::Application` (0x0400) 帧传输，载荷头部为 `IPCMessageHeader`，包含 `IPCMessageType` 枚举标识具体消息类型。详见 `Shared/ipc_messages.h`。

## 环境要求

- **操作系统**: Windows 10 或更高版本
- **编译器**: MSVC 2019+（推荐）或 MinGW-w64 GCC 12+
- **构建系统**: CMake 3.20+
- **运行时**（可选）: `ffmpeg.exe` — 用于视频/图片缩略图生成，需在 PATH 中或放在程序目录

## 构建

### MSVC (Visual Studio)

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
  libCoreIPC.dll   # IPC 动态链接库
  Desktop.exe      # 桌面
  Taskbar.exe      # 任务栏
  MainUI.exe       # 文件管理器
  ShellHost.exe    # Shell 扩展宿主
  MediaParser.exe  # 媒体解析服务
```

## 项目结构

```
LlaShell/
├── CMakeLists.txt
├── LlaShell.sln
├── CoreIPC/                     # IPC 库
│   ├── coreipc.def
│   ├── include/CoreIPC/
│   │   ├── CoreIPC.h            # 公共总头文件
│   │   ├── types.h              # 类型、常量、错误码、回调
│   │   ├── protocol.h           # 线协议: FrameHeader, MsgType
│   │   ├── pipe_channel.h       # PipeChannel (IOCP 命名管道)
│   │   ├── shm_channel.h        # ShmChannel (共享内存环形缓冲区)
│   │   └── guardian.h           # Guardian (进程守护)
│   └── src/
│       ├── coreipc.cpp          # IPC_* 函数实现
│       ├── pipe_server.cpp      # IOCP 服务器 + 客户端
│       ├── pipe_security.cpp    # NULL DACL 安全描述符
│       ├── shm_manager.cpp      # 共享内存生命周期
│       ├── shm_slot.cpp         # 环形缓冲区槽位 I/O + CRC32
│       ├── crc32.cpp            # CRC32 (std::call_once 线程安全)
│       └── guardian.cpp         # 进程守护
├── Desktop/
│   ├── include/desktop.h
│   └── src/desktop_main.cpp
├── Taskbar/
│   ├── include/taskbar.h
│   └── src/taskbar_main.cpp
├── MainUI/
│   ├── include/mainui.h
│   └── src/mainui_main.cpp
├── ShellHost/
│   └── src/shellhost_main.cpp
├── MediaParser/
│   └── src/mediaparser_main.cpp
└── Shared/
    └── ipc_messages.h           # 应用层消息类型定义
```

## 第三方开源软件

### FFmpeg

MediaParser 使用 [FFmpeg](https://ffmpeg.org/) 生成视频和图片缩略图。FFmpeg 以 CLI 子进程方式调用（`ffmpeg.exe`），不进行动态链接。

FFmpeg 采用 **LGPL v2.1+** 或 **GPL v2+** 许可证（取决于编译配置）。本项目以独立进程方式调用 FFmpeg，不链接 FFmpeg 库，因此不受其许可证的链接条款约束。

如需了解更多：
- FFmpeg 许可证: https://www.ffmpeg.org/legal.html
- LGPL v2.1: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html

如果您分集的 FFmpeg 二进制文件包含 GPL 编解码器，请遵守 GPL v2 条款。

## 许可证

见 [LICENSE](LICENSE)。
