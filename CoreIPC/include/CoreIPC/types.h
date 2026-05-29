#pragma once

#ifndef LLASHELL_COREIPC_TYPES_H
#define LLASHELL_COREIPC_TYPES_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <cstdint>

namespace LlaShell::IPC {

using SessionId  = uint32_t;
using PipeHandle = HANDLE;
using ShmHandle  = HANDLE;
using ProcessId  = DWORD;

static constexpr SessionId kInvalidSession = 0;
static constexpr uint32_t  kMaxMessageSize = 64 * 1024;   // 64KB — over this, use shm
static constexpr uint32_t  kDefaultHeartbeatMs = 500;
static constexpr uint32_t  kDefaultCrashDetectMs = 1000;
static constexpr uint32_t  kHeartbeatTimeoutMultiplier = 4; // 4 missed heartbeats = dead

enum class Result : int32_t {
    Success          =  0,
    AlreadyInit      = -1,
    NotInitialized   = -2,
    PipeCreateFailed = -3,
    PipeNotFound     = -4,
    PipeBusy         = -5,
    PipeConnectFailed= -6,
    ShmCreateFailed  = -7,
    ShmOpenFailed    = -8,
    ShmMapFailed     = -9,
    SlotFull         = -10,
    SlotNotReady     = -11,
    ChecksumError    = -12,
    Timeout          = -13,
    PeerDisconnected = -14,
    InvalidArg       = -15,
    BufferTooSmall   = -16,
    InternalError    = -17,
};

inline bool IsSuccess(Result r) { return r == Result::Success; }

enum class ChannelRole : uint8_t {
    Server,
    Client,
};

using OnMessageReceived = void(*)(SessionId from, uint16_t msgType, const void* data, uint32_t size, void* userData);
using OnPeerConnected   = void(*)(SessionId peer, ProcessId pid, void* userData);
using OnPeerDisconnected= void(*)(SessionId peer, void* userData);
using OnProcessCrashed  = void(*)(SessionId peer, void* userData);
using OnShmDataReady    = void(*)(SessionId from, uint32_t slotIndex, uint32_t dataType, void* userData);

struct IPCConfig {
    const wchar_t*     processName;
    ProcessId          selfPid;
    OnMessageReceived  onMessage;
    OnPeerConnected    onConnected;
    OnPeerDisconnected onDisconnected;
    OnProcessCrashed   onCrashed;
    OnShmDataReady     onShmData;
    void*              userData;
    uint32_t           heartbeatMs;
    uint32_t           crashDetectMs;
};

inline IPCConfig DefaultConfig() {
    IPCConfig cfg{};
    cfg.heartbeatMs    = kDefaultHeartbeatMs;
    cfg.crashDetectMs  = kDefaultCrashDetectMs;
    cfg.selfPid        = GetCurrentProcessId();
    return cfg;
}

} // namespace LlaShell::IPC

#endif // LLASHELL_COREIPC_TYPES_H
