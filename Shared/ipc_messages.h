#pragma once

#ifndef LLASHELL_SHARED_IPC_MESSAGES_H
#define LLASHELL_SHARED_IPC_MESSAGES_H

#include <cstdint>

namespace LlaShell {

enum class IPCMessageType : uint16_t {
    None = 0,

    DesktopToTaskbar = 100,
    DesktopFolderDoubleClick,
    DesktopIconRefresh,

    TaskbarToMainUI = 200,
    TaskbarOpenNewWindow,
    TaskbarWindowCreated,
    TaskbarWindowDestroyed,
    TaskbarToggleDesktop,

    MainUIToTaskbar = 300,
    MainUIWindowCreated,
    MainUIWindowDestroyed,
    MainUITabChanged,

    MainUIToShellHost = 400,
    RequestContextMenu,
    ContextMenuResult,

    MainUIToMediaParser = 500,
    RequestThumbnail,
    ThumbnailReady,
    CancelThumbnail,

    SystemBroadcast = 900,
    FullscreenDetected,
    FullscreenExited,
    ShellReady,
    ShellShutdown,
};

struct IPCMessageHeader {
    uint32_t        sourcePid;
    uint32_t        targetPid;
    IPCMessageType  type;
    uint32_t        dataSize;
};

struct FolderOpenRequest {
    wchar_t path[260];
};

struct WindowNotification {
    uint32_t windowPid;
    uint64_t hwnd;
    wchar_t title[128];
};

struct ThumbnailRequest {
    uint32_t requestId;
    wchar_t filePath[260];
    uint32_t maxWidth;
    uint32_t maxHeight;
};

struct ThumbnailResult {
    uint32_t requestId;
    uint32_t shmSlotIndex;
    uint32_t width;
    uint32_t height;
    bool     success;
};

struct ContextMenuRequest {
    uint64_t hwndParent;
    int32_t  screenX;
    int32_t  screenY;
    wchar_t filePath[260];
};

struct ContextMenuResult {
    uint32_t commandId;
};

} // namespace LlaShell

#endif // LLASHELL_SHARED_IPC_MESSAGES_H
