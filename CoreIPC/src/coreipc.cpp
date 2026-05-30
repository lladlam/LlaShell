#include "CoreIPC/CoreIPC.h"
#include <memory>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <atomic>

namespace {

struct IPCState {
    LlaShell::IPC::IPCConfig config;
    std::unique_ptr<LlaShell::IPC::PipeChannel> pipeChannel;
    std::unique_ptr<LlaShell::IPC::ShmChannel>  shmChannel;
    std::unique_ptr<LlaShell::IPC::Guardian>     guardian;
    std::unordered_map<std::wstring, LlaShell::IPC::SessionId> nameToSession;
    std::thread heartbeatThread;
    std::atomic<bool> initialized;
    std::atomic<bool> running;

    IPCState() : initialized(false), running(false) {}
};

IPCState g_state;
std::mutex g_stateMutex;

void HeartbeatSenderThread() {
    while (g_state.running.load(std::memory_order_relaxed)) {
        uint32_t intervalMs;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            intervalMs = g_state.config.heartbeatMs;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        if (!g_state.running.load(std::memory_order_relaxed)) break;

        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_state.pipeChannel) {
            g_state.pipeChannel->Broadcast(
                LlaShell::IPC::MsgType::Heartbeat, nullptr, 0);
        }
    }
}

} // anonymous namespace

extern "C" {

COREIPC_API LlaShell::IPC::Result IPC_Initialize(const LlaShell::IPC::IPCConfig* config) {
    std::lock_guard<std::mutex> lock(g_stateMutex);

    if (g_state.initialized.load(std::memory_order_relaxed))
        return LlaShell::IPC::Result::AlreadyInit;
    if (!config) return LlaShell::IPC::Result::InvalidArg;

    g_state.config = *config;

    g_state.pipeChannel = std::make_unique<LlaShell::IPC::PipeChannel>();
    g_state.shmChannel  = std::make_unique<LlaShell::IPC::ShmChannel>();
    g_state.guardian    = std::make_unique<LlaShell::IPC::Guardian>();

    g_state.initialized.store(true, std::memory_order_relaxed);
    g_state.running.store(true, std::memory_order_relaxed);

    return LlaShell::IPC::Result::Success;
}

COREIPC_API void IPC_Shutdown() {
    g_state.running.store(false, std::memory_order_relaxed);

    if (g_state.heartbeatThread.joinable()) {
        g_state.heartbeatThread.join();
    }

    if (g_state.guardian) {
        g_state.guardian->Stop();
    }

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_state.pipeChannel.reset();
        g_state.shmChannel.reset();
        g_state.guardian.reset();
        g_state.nameToSession.clear();
        g_state.initialized.store(false, std::memory_order_relaxed);
    }
}

COREIPC_API LlaShell::IPC::Result IPC_StartServer(const wchar_t* pipeName) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_state.initialized.load(std::memory_order_relaxed))
        return LlaShell::IPC::Result::NotInitialized;
    if (!pipeName) return LlaShell::IPC::Result::InvalidArg;
    if (!g_state.pipeChannel) return LlaShell::IPC::Result::NotInitialized;

    return g_state.pipeChannel->StartServer(pipeName, &g_state.config);
}

COREIPC_API LlaShell::IPC::Result IPC_Connect(const wchar_t* targetProcessName,
                                                LlaShell::IPC::SessionId* outSession) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_state.initialized.load(std::memory_order_relaxed))
        return LlaShell::IPC::Result::NotInitialized;
    if (!targetProcessName || !outSession) return LlaShell::IPC::Result::InvalidArg;

    LlaShell::IPC::SessionId session = LlaShell::IPC::kInvalidSession;
    LlaShell::IPC::Result r = g_state.pipeChannel->ConnectToServer(
        targetProcessName, &g_state.config, &session);

    if (IsSuccess(r)) {
        g_state.nameToSession[targetProcessName] = session;
        *outSession = session;

        // Start heartbeat thread on first connection
        if (!g_state.heartbeatThread.joinable()) {
            g_state.heartbeatThread = std::thread(HeartbeatSenderThread);
        }
    }

    return r;
}

COREIPC_API LlaShell::IPC::Result IPC_Disconnect(LlaShell::IPC::SessionId session) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_state.initialized.load(std::memory_order_relaxed))
        return LlaShell::IPC::Result::NotInitialized;
    if (!g_state.pipeChannel) return LlaShell::IPC::Result::NotInitialized;

    // Remove from name map
    for (auto it = g_state.nameToSession.begin(); it != g_state.nameToSession.end(); ++it) {
        if (it->second == session) {
            g_state.nameToSession.erase(it);
            break;
        }
    }

    return g_state.pipeChannel->Disconnect(session);
}

COREIPC_API LlaShell::IPC::Result IPC_Send(LlaShell::IPC::SessionId to,
                                             LlaShell::IPC::MsgType type,
                                             const void* data, uint32_t size) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_state.initialized.load(std::memory_order_relaxed) || !g_state.pipeChannel)
        return LlaShell::IPC::Result::NotInitialized;

    return g_state.pipeChannel->Send(to, type, data, size);
}

COREIPC_API LlaShell::IPC::Result IPC_Broadcast(LlaShell::IPC::MsgType type,
                                                  const void* data, uint32_t size) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_state.initialized.load(std::memory_order_relaxed) || !g_state.pipeChannel)
        return LlaShell::IPC::Result::NotInitialized;

    return g_state.pipeChannel->Broadcast(type, data, size);
}

COREIPC_API LlaShell::IPC::Result IPC_WatchProcess(LlaShell::IPC::SessionId session,
                                                     LlaShell::IPC::ProcessId pid,
                                                     const wchar_t* exePath,
                                                     const wchar_t* arguments,
                                                     int autoRestart,
                                                     uint32_t maxRestarts) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_state.initialized.load(std::memory_order_relaxed) || !g_state.guardian)
        return LlaShell::IPC::Result::NotInitialized;

    // Start guardian on first watch
    if (!g_state.guardian->IsAlive(session)) {
        g_state.guardian->Start(&g_state.config, g_state.config.crashDetectMs);
    }

    return g_state.guardian->WatchProcess(session, pid, exePath, arguments,
                                           autoRestart != 0, maxRestarts);
}

COREIPC_API LlaShell::IPC::Result IPC_UnwatchProcess(LlaShell::IPC::SessionId session) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_state.initialized.load(std::memory_order_relaxed) || !g_state.guardian)
        return LlaShell::IPC::Result::NotInitialized;

    return g_state.guardian->UnwatchProcess(session);
}

COREIPC_API LlaShell::IPC::Result IPC_ShmCreate(const wchar_t* name,
                                                   uint32_t totalSize,
                                                   uint32_t slotCount,
                                                   uint32_t slotCapacity,
                                                   LlaShell::IPC::ShmHandle* outHandle) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_state.initialized.load(std::memory_order_relaxed) || !g_state.shmChannel)
        return LlaShell::IPC::Result::NotInitialized;

    return g_state.shmChannel->CreateSegment(name, totalSize, slotCount, slotCapacity, outHandle);
}

COREIPC_API LlaShell::IPC::Result IPC_ShmOpen(const wchar_t* name,
                                                 LlaShell::IPC::ShmHandle* outHandle) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_state.initialized.load(std::memory_order_relaxed) || !g_state.shmChannel)
        return LlaShell::IPC::Result::NotInitialized;

    return g_state.shmChannel->OpenSegment(name, outHandle);
}

COREIPC_API LlaShell::IPC::Result IPC_ShmClose(LlaShell::IPC::ShmHandle handle) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_state.initialized.load(std::memory_order_relaxed) || !g_state.shmChannel)
        return LlaShell::IPC::Result::NotInitialized;

    return g_state.shmChannel->CloseSegment(handle);
}

COREIPC_API LlaShell::IPC::Result IPC_ShmWrite(LlaShell::IPC::ShmHandle handle,
                                                  const void* data,
                                                  uint32_t size,
                                                  uint32_t dataType) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_state.initialized.load(std::memory_order_relaxed) || !g_state.shmChannel)
        return LlaShell::IPC::Result::NotInitialized;

    return g_state.shmChannel->WriteSlot(handle, data, size, dataType);
}

COREIPC_API LlaShell::IPC::Result IPC_ShmReadPtr(LlaShell::IPC::ShmHandle handle,
                                                    uint32_t slotIndex,
                                                    const void** outPtr,
                                                    uint32_t* outSize,
                                                    uint32_t* outType) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_state.initialized.load(std::memory_order_relaxed) || !g_state.shmChannel)
        return LlaShell::IPC::Result::NotInitialized;

    return g_state.shmChannel->ReadSlotPtr(handle, slotIndex, outPtr, outSize, outType);
}

COREIPC_API LlaShell::IPC::Result IPC_ShmMarkRead(LlaShell::IPC::ShmHandle handle,
                                                     uint32_t slotIndex) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (!g_state.initialized.load(std::memory_order_relaxed) || !g_state.shmChannel)
        return LlaShell::IPC::Result::NotInitialized;

    return g_state.shmChannel->MarkSlotRead(handle, slotIndex);
}

COREIPC_API uint32_t IPC_CRC32(const void* data, uint32_t size) {
    return LlaShell::IPC::ShmChannel::CRC32(data, size);
}

} // extern "C"
