#pragma once

#ifndef LLASHELL_COREIPC_H
#define LLASHELL_COREIPC_H

#include "types.h"
#include "protocol.h"
#include "pipe_channel.h"
#include "shm_channel.h"
#include "guardian.h"

#ifndef COREIPC_API
#ifdef COREIPC_EXPORTS
#define COREIPC_API __declspec(dllexport)
#else
#define COREIPC_API __declspec(dllimport)
#endif
#endif

extern "C" {

COREIPC_API LlaShell::IPC::Result IPC_Initialize(const LlaShell::IPC::IPCConfig* config);
COREIPC_API void                  IPC_Shutdown();

COREIPC_API LlaShell::IPC::Result IPC_StartServer(const wchar_t* pipeName);

COREIPC_API LlaShell::IPC::Result IPC_Connect(const wchar_t* targetProcessName,
                                               LlaShell::IPC::SessionId* outSession);
COREIPC_API LlaShell::IPC::Result IPC_Disconnect(LlaShell::IPC::SessionId session);

COREIPC_API LlaShell::IPC::Result IPC_Send(LlaShell::IPC::SessionId to,
                                            LlaShell::IPC::MsgType type,
                                            const void* data, uint32_t size);

COREIPC_API LlaShell::IPC::Result IPC_Broadcast(LlaShell::IPC::MsgType type,
                                                 const void* data, uint32_t size);

COREIPC_API LlaShell::IPC::Result IPC_WatchProcess(LlaShell::IPC::SessionId session,
                                                     LlaShell::IPC::ProcessId pid,
                                                     const wchar_t* exePath,
                                                     const wchar_t* arguments,
                                                     int autoRestart,
                                                     uint32_t maxRestarts);

COREIPC_API LlaShell::IPC::Result IPC_UnwatchProcess(LlaShell::IPC::SessionId session);

COREIPC_API LlaShell::IPC::Result IPC_ShmCreate(const wchar_t* name,
                                                  uint32_t totalSize,
                                                  uint32_t slotCount,
                                                  uint32_t slotCapacity,
                                                  LlaShell::IPC::ShmHandle* outHandle);

COREIPC_API LlaShell::IPC::Result IPC_ShmOpen(const wchar_t* name,
                                                LlaShell::IPC::ShmHandle* outHandle);

COREIPC_API LlaShell::IPC::Result IPC_ShmClose(LlaShell::IPC::ShmHandle handle);

COREIPC_API LlaShell::IPC::Result IPC_ShmWrite(LlaShell::IPC::ShmHandle handle,
                                                 const void* data,
                                                 uint32_t size,
                                                 uint32_t dataType);

COREIPC_API LlaShell::IPC::Result IPC_ShmReadPtr(LlaShell::IPC::ShmHandle handle,
                                                   uint32_t slotIndex,
                                                   const void** outPtr,
                                                   uint32_t* outSize,
                                                   uint32_t* outType);

COREIPC_API LlaShell::IPC::Result IPC_ShmMarkRead(LlaShell::IPC::ShmHandle handle,
                                                    uint32_t slotIndex);

COREIPC_API uint32_t IPC_CRC32(const void* data, uint32_t size);

} // extern "C"

#endif // LLASHELL_COREIPC_H
