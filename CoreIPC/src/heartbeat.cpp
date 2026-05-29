#include "CoreIPC/pipe_channel.h"
#include <thread>
#include <chrono>

namespace LlaShell::IPC {

// Heartbeat logic is integrated into the IOCP worker thread model.
// This file documents the heartbeat design and provides the
// heartbeat sender thread that clients use to maintain liveness.

// The heartbeat flow:
//   Client → Server:  MsgType::Heartbeat every heartbeatMs
//   Server → Client:  MsgType::HeartbeatAck
//
// Server-side detection:
//   If no Heartbeat received within (heartbeatMs * kHeartbeatTimeoutMultiplier),
//   the peer is considered dead and its session is cleaned up.

// A dedicated heartbeat sender thread can be started per-session.
// For now, the client side is responsible for calling Send(Heartbeat)
// periodically. The integration point is in coreipc.cpp.

} // namespace LlaShell::IPC
