#pragma once

#ifndef LLASHELL_COREIPC_PROTOCOL_H
#define LLASHELL_COREIPC_PROTOCOL_H

#include "types.h"
#include <cstring>

namespace LlaShell::IPC {

static constexpr uint32_t kFrameMagic   = 0x4C534950; // "LSIP"
static constexpr uint16_t kFrameVersion = 0x0001;
static constexpr uint32_t kFrameHeaderSize = 12;

enum class MsgType : uint16_t {
    Handshake       = 0x0001,
    HandshakeAck    = 0x0002,
    Heartbeat       = 0x0003,
    HeartbeatAck    = 0x0004,

    ShmOpen         = 0x0100,
    ShmReady        = 0x0101,
    ShmClose        = 0x0102,

    FileListReq     = 0x0200,
    FileListChunk   = 0x0201,
    ThumbnailReq    = 0x0202,
    ThumbnailReady  = 0x0203,

    ProcessReady    = 0x0300,
    ProcessShutdown = 0x0301,
    ProcessCrashed  = 0x0302,
};

#pragma pack(push, 1)
struct FrameHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t msgType;
    uint32_t payloadSize;
};

struct HandshakePayload {
    uint32_t pid;
    wchar_t  processName[64]; // null-terminated, max 63 chars
};

struct HandshakeAckPayload {
    SessionId sessionId;
};

struct ShmOpenPayload {
    wchar_t  segmentName[128];
    uint32_t segmentSize;
    uint32_t slotCount;
};

struct ShmReadyPayload {
    wchar_t  segmentName[128];
    uint32_t slotIndex;    // which slot has new data
    uint32_t dataType;
    uint32_t dataSize;
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) == kFrameHeaderSize, "FrameHeader must be 12 bytes");

inline Result EncodeFrame(uint8_t* buffer, uint32_t bufferSize,
                          MsgType type, const void* payload, uint32_t payloadSize) {
    uint32_t totalSize = kFrameHeaderSize + payloadSize;
    if (bufferSize < totalSize) return Result::BufferTooSmall;

    FrameHeader hdr{};
    hdr.magic      = kFrameMagic;
    hdr.version    = kFrameVersion;
    hdr.msgType    = static_cast<uint16_t>(type);
    hdr.payloadSize = payloadSize;

    memcpy(buffer, &hdr, kFrameHeaderSize);
    if (payload && payloadSize > 0) {
        memcpy(buffer + kFrameHeaderSize, payload, payloadSize);
    }
    return Result::Success;
}

inline Result DecodeFrame(const uint8_t* buffer, uint32_t bufferSize,
                          FrameHeader* outHeader, const void** outPayload) {
    if (bufferSize < kFrameHeaderSize) return Result::BufferTooSmall;

    FrameHeader hdr;
    memcpy(&hdr, buffer, kFrameHeaderSize);

    if (hdr.magic != kFrameMagic) return Result::ChecksumError;
    if (hdr.version != kFrameVersion) return Result::ChecksumError;
    if (kFrameHeaderSize + hdr.payloadSize > bufferSize) return Result::BufferTooSmall;

    *outHeader = hdr;
    *outPayload = (hdr.payloadSize > 0) ? (buffer + kFrameHeaderSize) : nullptr;
    return Result::Success;
}

} // namespace LlaShell::IPC

#endif // LLASHELL_COREIPC_PROTOCOL_H
