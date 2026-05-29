#include "CoreIPC/protocol.h"

namespace LlaShell::IPC {

// EncodeFrame and DecodeFrame are inline in protocol.h.
// This translation unit exists for:
// 1. Ensuring the protocol symbols get compiled into the DLL
// 2. Future non-trivial codec logic (compression, encryption)

} // namespace LlaShell::IPC
