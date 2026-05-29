#include "CoreIPC/pipe_channel.h"
#include <algorithm>
#include <Aclapi.h>
#include <Sddl.h>

#pragma comment(lib, "advapi32.lib")

namespace LlaShell::IPC {

static constexpr uint32_t kPipeInstances = 4;
static constexpr wchar_t kPipePrefix[] = L"\\\\.\\pipe\\LlaShell\\IPC\\v1\\";

PipeChannel::PipeChannel()
    : m_iocp(nullptr)
    , m_nextSessionId(1)
    , m_config(nullptr)
    , m_running(false)
    , m_securityInitialized(false) {
    ZeroMemory(&m_securityDesc, sizeof(m_securityDesc));
    ZeroMemory(&m_securityAttr, sizeof(m_securityAttr));
}

PipeChannel::~PipeChannel() {
    Shutdown();
}

SECURITY_ATTRIBUTES* PipeChannel::GetSecurityAttributes() {
    if (m_securityInitialized) return &m_securityAttr;

    if (!InitializeSecurityDescriptor(&m_securityDesc, SECURITY_DESCRIPTOR_REVISION))
        return nullptr;
    if (!SetSecurityDescriptorDacl(&m_securityDesc, TRUE, NULL, FALSE))
        return nullptr;

    m_securityAttr.nLength              = sizeof(SECURITY_ATTRIBUTES);
    m_securityAttr.lpSecurityDescriptor = &m_securityDesc;
    m_securityAttr.bInheritHandle       = FALSE;
    m_securityInitialized = true;
    return &m_securityAttr;
}

Result PipeChannel::StartServer(const wchar_t* pipeName, const IPCConfig* config) {
    if (!pipeName || !config) return Result::InvalidArg;
    if (m_running) return Result::AlreadyInit;

    m_config   = config;
    m_running  = true;
    m_pipeName = kPipePrefix;
    m_pipeName += pipeName;

    SECURITY_ATTRIBUTES* sa = GetSecurityAttributes();

    m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!m_iocp) {
        m_running = false;
        return Result::InternalError;
    }

    for (uint32_t i = 0; i < kPipeInstances; i++) {
        auto* inst = new PipeInstance{};
        inst->hPipe = CreateNamedPipeW(
            m_pipeName.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES,
            kMaxMessageSize, kMaxMessageSize,
            0, sa);

        if (inst->hPipe == INVALID_HANDLE_VALUE) {
            delete inst;
            continue;
        }

        ZeroMemory(&inst->connectOv, sizeof(OVERLAPPED));
        auto* ovEx = new OverlappedEx{};
        ovEx->ov      = inst->connectOv;
        ovEx->op       = IoOp::Connect;
        ovEx->instance = inst;

        CreateIoCompletionPort(inst->hPipe, m_iocp, reinterpret_cast<ULONG_PTR>(inst), 0);

        if (!ConnectNamedPipe(inst->hPipe, &ovEx->ov)) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                // waiting for client
            } else if (err == ERROR_PIPE_CONNECTED) {
                // client already connected before we called ConnectNamedPipe
                PostQueuedCompletionStatus(m_iocp, 0, reinterpret_cast<ULONG_PTR>(inst), &ovEx->ov);
            } else {
                CloseHandle(inst->hPipe);
                delete ovEx;
                delete inst;
                continue;
            }
        }

        m_pipeInstances.push_back(inst);
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uint32_t threadCount = std::min(std::max(si.dwNumberOfProcessors, static_cast<DWORD>(2)), static_cast<DWORD>(8));

    for (uint32_t i = 0; i < threadCount; i++) {
        HANDLE hThread = CreateThread(nullptr, 0,
            [](LPVOID param) -> DWORD {
                static_cast<PipeChannel*>(param)->IocpWorkerThread();
                return 0;
            }, this, 0, nullptr);
        if (hThread) m_workerThreads.push_back(hThread);
    }

    return Result::Success;
}

void PipeChannel::IocpWorkerThread() {
    DWORD bytesTransferred = 0;
    ULONG_PTR key = 0;
    OVERLAPPED* ov = nullptr;

    while (m_running) {
        BOOL ok = GetQueuedCompletionStatus(m_iocp, &bytesTransferred, &key, &ov, 500);
        auto* ovEx = reinterpret_cast<OverlappedEx*>(ov);

        if (!ok && ov == nullptr) {
            // Timeout — continue loop to check m_running
            continue;
        }

        if (!ovEx) continue;

        PipeInstance* inst = ovEx->instance;

        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
                HandleDisconnect(inst);
            }
            delete ovEx;
            continue;
        }

        switch (ovEx->op) {
            case IoOp::Connect:
                HandleConnect(inst, bytesTransferred);
                delete ovEx;
                break;
            case IoOp::Read:
                if (bytesTransferred > 0) {
                    HandleRead(inst, bytesTransferred);
                } else {
                    HandleDisconnect(inst);
                }
                delete ovEx;
                break;
            case IoOp::Write:
                delete ovEx;
                break;
        }
    }
}

void PipeChannel::HandleConnect(PipeInstance* inst, DWORD /*bytesTransferred*/) {
    inst->connected = true;
    PostRead(inst);
}

bool PipeChannel::PostRead(PipeInstance* inst) {
    ZeroMemory(&inst->readOv, sizeof(OVERLAPPED));
    auto* ovEx = new OverlappedEx{};
    ovEx->ov      = inst->readOv;
    ovEx->op       = IoOp::Read;
    ovEx->instance = inst;

    DWORD bytesRead = 0;
    BOOL ok = ReadFile(inst->hPipe, inst->readBuffer, kReadBufferSize, &bytesRead, &ovEx->ov);
    if (!ok && GetLastError() != ERROR_IO_PENDING) {
        delete ovEx;
        return false;
    }
    return true;
}

void PipeChannel::HandleRead(PipeInstance* inst, DWORD bytesTransferred) {
    FrameHeader hdr{};
    const void* payload = nullptr;
    Result r = DecodeFrame(inst->readBuffer, bytesTransferred, &hdr, &payload);

    if (IsSuccess(r)) {
        auto msgType = static_cast<MsgType>(hdr.msgType);

        switch (msgType) {
            case MsgType::Handshake:
                ProcessHandshake(inst, hdr, payload);
                break;
            case MsgType::Heartbeat:
                ProcessHeartbeat(inst);
                break;
            case MsgType::HeartbeatAck: {
                std::lock_guard<std::mutex> lock(m_peersMutex);
                for (auto& [id, peer] : m_peers) {
                    if (peer.sessionId == inst->sessionId) {
                        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&peer.lastHeartbeat));
                        break;
                    }
                }
                break;
            }
            default:
                if (m_config && m_config->onMessage) {
                    m_config->onMessage(inst->sessionId, hdr.msgType,
                                        payload, hdr.payloadSize, m_config->userData);
                }
                break;
        }
    }

    PostRead(inst);
}

void PipeChannel::ProcessHandshake(PipeInstance* inst, const FrameHeader& /*hdr*/, const void* payload) {
    if (!payload) return;
    const auto* hs = static_cast<const HandshakePayload*>(payload);

    inst->peerPid = hs->pid;

    SessionId sid;
    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        sid = m_nextSessionId++;
    }

    inst->sessionId = sid;

    PeerInfo peer{};
    peer.sessionId     = sid;
    peer.pid           = hs->pid;
    peer.processHandle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, hs->pid);
    peer.alive         = true;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&peer.lastHeartbeat));
    wcscpy_s(peer.processName, hs->processName);

    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        m_peers[sid] = peer;
    }

    // Send HandshakeAck
    HandshakeAckPayload ack{};
    ack.sessionId = sid;

    uint8_t buf[kFrameHeaderSize + sizeof(HandshakeAckPayload)];
    EncodeFrame(buf, sizeof(buf), MsgType::HandshakeAck, &ack, sizeof(ack));

    DWORD written = 0;
    WriteFile(inst->hPipe, buf, sizeof(buf), &written, nullptr);

    if (m_config && m_config->onConnected) {
        m_config->onConnected(sid, hs->pid, m_config->userData);
    }
}

void PipeChannel::ProcessHeartbeat(PipeInstance* inst) {
    // Reply with HeartbeatAck
    uint8_t buf[kFrameHeaderSize];
    EncodeFrame(buf, sizeof(buf), MsgType::HeartbeatAck, nullptr, 0);

    DWORD written = 0;
    WriteFile(inst->hPipe, buf, sizeof(buf), &written, nullptr);

    // Update last heartbeat time
    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        for (auto& [id, peer] : m_peers) {
            if (peer.sessionId == inst->sessionId) {
                QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&peer.lastHeartbeat));
                break;
            }
        }
    }
}

void PipeChannel::HandleDisconnect(PipeInstance* inst) {
    DisconnectNamedPipe(inst->hPipe);

    if (inst->sessionId != kInvalidSession) {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        auto it = m_peers.find(inst->sessionId);
        if (it != m_peers.end()) {
            if (it->second.processHandle) {
                CloseHandle(it->second.processHandle);
                it->second.processHandle = nullptr;
            }
            if (m_config && m_config->onDisconnected) {
                m_config->onDisconnected(inst->sessionId, m_config->userData);
            }
            m_peers.erase(it);
        }
    }

    inst->connected = false;
    inst->sessionId = kInvalidSession;
    inst->peerPid   = 0;

    // Re-arm the pipe instance for next connection
    ZeroMemory(&inst->connectOv, sizeof(OVERLAPPED));
    auto* ovEx = new OverlappedEx{};
    ovEx->ov      = inst->connectOv;
    ovEx->op       = IoOp::Connect;
    ovEx->instance = inst;

    if (!ConnectNamedPipe(inst->hPipe, &ovEx->ov)) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            // ok
        } else if (err == ERROR_PIPE_CONNECTED) {
            PostQueuedCompletionStatus(m_iocp, 0, reinterpret_cast<ULONG_PTR>(inst), &ovEx->ov);
        } else {
            delete ovEx;
        }
    }
}

void PipeChannel::Shutdown() {
    m_running = false;

    // Wake all IOCP threads
    for (size_t i = 0; i < m_workerThreads.size(); i++) {
        PostQueuedCompletionStatus(m_iocp, 0, 0, nullptr);
    }

    for (HANDLE h : m_workerThreads) {
        WaitForSingleObject(h, 3000);
        CloseHandle(h);
    }
    m_workerThreads.clear();

    // Close all pipe instances
    for (auto* inst : m_pipeInstances) {
        if (inst->hPipe != INVALID_HANDLE_VALUE) {
            CancelIo(inst->hPipe);
            DisconnectNamedPipe(inst->hPipe);
            CloseHandle(inst->hPipe);
        }
        delete inst;
    }
    m_pipeInstances.clear();

    // Close all peer handles
    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        for (auto& [id, peer] : m_peers) {
            if (peer.processHandle) {
                CloseHandle(peer.processHandle);
            }
        }
        m_peers.clear();
    }

    if (m_iocp) {
        CloseHandle(m_iocp);
        m_iocp = nullptr;
    }
}

Result PipeChannel::Send(SessionId to, MsgType type, const void* data, uint32_t size) {
    uint32_t frameSize = kFrameHeaderSize + size;
    if (frameSize > kMaxMessageSize) return Result::BufferTooSmall;

    std::vector<uint8_t> buf(frameSize);
    Result r = EncodeFrame(buf.data(), frameSize, type, data, size);
    if (!IsSuccess(r)) return r;

    // Find the pipe instance for this session
    PipeInstance* target = nullptr;
    for (auto* inst : m_pipeInstances) {
        if (inst->sessionId == to && inst->connected) {
            target = inst;
            break;
        }
    }

    if (!target) return Result::PeerDisconnected;

    auto* writeBuf = new PendingWrite{};
    writeBuf->buffer = std::move(buf);
    ZeroMemory(&writeBuf->ov, sizeof(OVERLAPPED));

    DWORD written = 0;
    if (!WriteFile(target->hPipe, writeBuf->buffer.data(),
                   static_cast<DWORD>(writeBuf->buffer.size()), &written, &writeBuf->ov)) {
        if (GetLastError() != ERROR_IO_PENDING) {
            delete writeBuf;
            return Result::InternalError;
        }
    }

    return Result::Success;
}

Result PipeChannel::Broadcast(MsgType type, const void* data, uint32_t size) {
    std::lock_guard<std::mutex> lock(m_peersMutex);
    for (const auto& [id, peer] : m_peers) {
        Send(id, type, data, size);
    }
    return Result::Success;
}

Result PipeChannel::ConnectToServer(const wchar_t* pipeName, const IPCConfig* config, SessionId* outSession) {
    if (!pipeName || !config || !outSession) return Result::InvalidArg;

    m_config  = config;
    m_running = true;

    std::wstring fullPath = kPipePrefix;
    fullPath += pipeName;

    HANDLE hPipe = INVALID_HANDLE_VALUE;
    for (int retry = 0; retry < 20; retry++) {
        hPipe = CreateFileW(
            fullPath.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED, nullptr);

        if (hPipe != INVALID_HANDLE_VALUE) break;

        if (GetLastError() == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(fullPath.c_str(), 2000);
        } else {
            Sleep(100);
        }
    }

    if (hPipe == INVALID_HANDLE_VALUE) return Result::PipeNotFound;

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(hPipe, &mode, nullptr, nullptr)) {
        CloseHandle(hPipe);
        return Result::InternalError;
    }

    auto* inst = new PipeInstance{};
    inst->hPipe     = hPipe;
    inst->connected = true;

    m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!m_iocp) {
        CloseHandle(hPipe);
        delete inst;
        return Result::InternalError;
    }
    CreateIoCompletionPort(hPipe, m_iocp, reinterpret_cast<ULONG_PTR>(inst), 0);

    SessionId sid;
    {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        sid = m_nextSessionId++;
    }
    inst->sessionId = sid;
    *outSession = sid;

    m_pipeInstances.push_back(inst);

    // Start IOCP worker
    HANDLE hThread = CreateThread(nullptr, 0,
        [](LPVOID param) -> DWORD {
            static_cast<PipeChannel*>(param)->IocpWorkerThread();
            return 0;
        }, this, 0, nullptr);
    if (hThread) m_workerThreads.push_back(hThread);

    // Send Handshake
    HandshakePayload hs{};
    hs.pid = config->selfPid;
    wcscpy_s(hs.processName, config->processName);

    uint8_t buf[kFrameHeaderSize + sizeof(HandshakePayload)];
    EncodeFrame(buf, sizeof(buf), MsgType::Handshake, &hs, sizeof(hs));

    DWORD written = 0;
    WriteFile(hPipe, buf, sizeof(buf), &written, nullptr);

    // Start reading
    PostRead(inst);

    return Result::Success;
}

Result PipeChannel::Disconnect(SessionId session) {
    for (auto* inst : m_pipeInstances) {
        if (inst->sessionId == session) {
            // Send ProcessShutdown before disconnecting
            uint8_t buf[kFrameHeaderSize];
            EncodeFrame(buf, sizeof(buf), MsgType::ProcessShutdown, nullptr, 0);
            DWORD written = 0;
            WriteFile(inst->hPipe, buf, sizeof(buf), &written, nullptr);

            FlushFileBuffers(inst->hPipe);
            DisconnectNamedPipe(inst->hPipe);
            CloseHandle(inst->hPipe);
            inst->hPipe = INVALID_HANDLE_VALUE;
            inst->connected = false;

            std::lock_guard<std::mutex> lock(m_peersMutex);
            auto it = m_peers.find(session);
            if (it != m_peers.end()) {
                if (it->second.processHandle) CloseHandle(it->second.processHandle);
                m_peers.erase(it);
            }
            return Result::Success;
        }
    }
    return Result::InvalidArg;
}

const PeerInfo* PipeChannel::GetPeer(SessionId session) const {
    std::lock_guard<std::mutex> lock(m_peersMutex);
    auto it = m_peers.find(session);
    return (it != m_peers.end()) ? &it->second : nullptr;
}

SessionId PipeChannel::GetSessionByPid(ProcessId pid) const {
    std::lock_guard<std::mutex> lock(m_peersMutex);
    for (const auto& [id, peer] : m_peers) {
        if (peer.pid == pid) return id;
    }
    return kInvalidSession;
}

} // namespace LlaShell::IPC
