# LlaShell

Windows 原生进程间通信 (IPC) 库，提供双通道架构，支持高性能消息传递与大块数据传输。

## 架构

```
┌─────────────────────────────────────────────────────────┐
│                    公共 C API (CoreIPC.h)                 │
│  IPC_Initialize / IPC_Connect / IPC_Send / IPC_Shm*     │
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

### 控制通道 — PipeChannel

基于命名管道 (`\\.\pipe\LlaShell\IPC\v1\<name>`) 和 IOCP 异步 I/O，用于小消息 (< 64KB)、握手协商、心跳和协调消息。服务端预创建 4 个管道实例，启动 2–8 个 IOCP 工作线程。

### 数据通道 — ShmChannel

基于命名共享内存段 (`Local\LlaShmem.v1.<name>`) 的环形缓冲区架构，支持零拷贝大数据传输（图片、文件列表、缩略图）。通过 CRC32 校验和验证数据完整性，使用原子操作（`InterlockedIncrement`、`InterlockedOr`、`InterlockedAnd`、`MemoryBarrier`）实现无锁协调。

### 进程守护 — Guardian

通过 `WaitForMultipleObjects` 监控已连接的对等进程。检测到崩溃时触发回调，并可选地自动重启进程（支持配置最大重启次数）。

## 公共 API

所有函数以 `extern "C"` 从 `CoreIPC.dll` 导出：

| 类别 | 函数 | 说明 |
|---|---|---|
| 生命周期 | `IPC_Initialize` | 使用回调和配置初始化库 |
| | `IPC_Shutdown` | 关闭所有通道和线程 |
| 消息传递 | `IPC_Connect` | 连接到命名管道服务器 |
| | `IPC_Disconnect` | 优雅断开会话 |
| | `IPC_Send` | 向指定对等方发送类型化消息 (≤ 64KB) |
| | `IPC_Broadcast` | 向所有已连接对等方广播消息 |
| 进程守护 | `IPC_WatchProcess` | 开始监控进程 |
| | `IPC_UnwatchProcess` | 停止监控进程 |
| 共享内存 | `IPC_ShmCreate` | 创建命名共享内存段 |
| | `IPC_ShmOpen` | 打开已有的共享内存段 |
| | `IPC_ShmClose` | 关闭（取消映射）共享内存段 |
| | `IPC_ShmWrite` | 将数据写入下一个环形缓冲区槽位 |
| | `IPC_ShmReadPtr` | 零拷贝读取，带 CRC32 校验 |
| | `IPC_ShmMarkRead` | 标记槽位已被消费 |
| 工具 | `IPC_CRC32` | 计算 CRC32 校验和 |

## 环境要求

- **操作系统**: Windows 10 或更高版本
- **编译器**: MSVC 2019+（推荐）或 MinGW-w64 GCC 12+
- **构建系统**: CMake 3.20+

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
  CoreIPC.dll    # 动态链接库
  CoreIPC.dll.a  # 导入库 (MinGW)
  CoreIPC.lib    # 导入库 (MSVC)
```

## 项目结构

```
LlaShell/
├── CMakeLists.txt              # 构建配置
├── LlaShell.sln                # Visual Studio 解决方案
├── CoreIPC/
│   ├── coreipc.def             # DLL 导出定义
│   ├── coreipc.vcxproj         # VS 项目文件
│   ├── include/CoreIPC/
│   │   ├── CoreIPC.h           # 公共总头文件
│   │   ├── types.h             # 类型、常量、错误码、回调
│   │   ├── protocol.h          # 线协议: FrameHeader, MsgType, 载荷
│   │   ├── pipe_channel.h      # PipeChannel 类 (IOCP 命名管道)
│   │   ├── shm_channel.h       # ShmChannel 类 (共享内存环形缓冲区)
│   │   └── guardian.h          # Guardian 类 (进程守护)
│   └── src/
│       ├── coreipc.cpp         # DLL 入口, IPC_* 函数实现
│       ├── pipe_server.cpp     # IOCP 服务器 + 客户端连接逻辑
│       ├── pipe_client.cpp     # 客户端存根 (逻辑在 pipe_server.cpp)
│       ├── pipe_security.cpp   # NULL DACL 安全描述符 (跨 UAC 访问)
│       ├── protocol_codec.cpp  # 编解码器占位
│       ├── heartbeat.cpp       # 心跳设计文档
│       ├── shm_manager.cpp     # 共享内存生命周期 (创建/打开/关闭)
│       ├── shm_slot.cpp        # 环形缓冲区槽位 I/O + CRC32
│       ├── crc32.cpp           # CRC32 实现
│       └── guardian.cpp        # 进程守护实现
```

## 许可证

见 [LICENSE](LICENSE)。
