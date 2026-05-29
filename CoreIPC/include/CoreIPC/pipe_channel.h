#pragma once

#ifndef LLASHELL_COREIPC_PIPE_CHANNEL_H
#define LLASHELL_COREIPC_PIPE_CHANNEL_H

#include "types.h"
#include "protocol.h"
#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

namespace LlaShell::IPC {

struct PeerInfo {
    SessionId   sessionId;
    ProcessId   pid;
    wchar_t     processName[64];
    HANDLE      processHandle;   // for crash detection
    int64_t     lastHeartbeat;   // QueryPerformanceCounter value
    bool        alive;
};

class PipeChannel {
public:
    PipeChannel();
    ~PipeChannel();

    PipeChannel(const PipeChannel&) = delete;
    PipeChannel& operator=(const PipeChannel&) = delete;

    Result StartServer(const wchar_t* pipeName, const IPCConfig* config);
    Result ConnectToServer(const wchar_t* pipeName, const IPCConfig* config, SessionId* outSession);
    void   Shutdown();

    Result Send(SessionId to, MsgType type, const void* data, uint32_t size);
    Result Broadcast(MsgType type, const void* data, uint32_t size);
    Result Disconnect(SessionId session);

    const PeerInfo* GetPeer(SessionId session) const;
    SessionId       GetSessionByPid(ProcessId pid) const;

private:
    static constexpr uint32_t kReadBufferSize = kMaxMessageSize;

    struct PendingWrite {
        OVERLAPPED  ov;
        std::vector<uint8_t> buffer;
    };

    struct PipeInstance {
        HANDLE      hPipe;
        OVERLAPPED  readOv;
        OVERLAPPED  connectOv;
        uint8_t     readBuffer[kReadBufferSize];
        bool        connected;
        SessionId   sessionId;
        ProcessId   peerPid;
    };

    enum class IoOp : uint8_t {
        Connect,
        Read,
        Write,
    };

    struct OverlappedEx {
        OVERLAPPED  ov;
        IoOp        op;
        PipeInstance* instance;
    };

    void   IocpWorkerThread();
    bool   PostRead(PipeInstance* inst);
    void   HandleConnect(PipeInstance* inst, DWORD bytesTransferred);
    void   HandleRead(PipeInstance* inst, DWORD bytesTransferred);
    void   HandleDisconnect(PipeInstance* inst);
    void   ProcessHandshake(PipeInstance* inst, const FrameHeader& hdr, const void* payload);
    void   ProcessHeartbeat(PipeInstance* inst);

    SECURITY_ATTRIBUTES* GetSecurityAttributes();

    HANDLE                                      m_iocp;
    std::vector<HANDLE>                         m_workerThreads;
    std::vector<PipeInstance*>                  m_pipeInstances;
    mutable std::mutex                          m_peersMutex;
    std::unordered_map<SessionId, PeerInfo>     m_peers;
    SessionId                                   m_nextSessionId;
    const IPCConfig*                            m_config;
    bool                                        m_running;
    std::wstring                                m_pipeName;
    SECURITY_DESCRIPTOR                         m_securityDesc;
    SECURITY_ATTRIBUTES                         m_securityAttr;
    bool                                        m_securityInitialized;
};

} // namespace LlaShell::IPC

#endif // LLASHELL_COREIPC_PIPE_CHANNEL_H
