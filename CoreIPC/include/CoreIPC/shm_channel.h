#pragma once

#ifndef LLASHELL_COREIPC_SHM_CHANNEL_H
#define LLASHELL_COREIPC_SHM_CHANNEL_H

#include "types.h"
#include <cstdint>
#include <string>
#include <mutex>
#include <unordered_map>

namespace LlaShell::IPC {

static constexpr uint32_t kShmMagic   = 0x4C53484D; // "LSHM"
static constexpr uint32_t kShmVersion = 0x0001;

static constexpr uint32_t SLOT_FLAG_VALID     = 0x0001;
static constexpr uint32_t SLOT_FLAG_READ_DONE = 0x0002;

#pragma pack(push, 1)
struct ShmHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t segmentSize;
    uint32_t slotCount;
    volatile LONG writeIndex;
    volatile LONG readIndex;
    uint32_t flags;
    uint32_t generation;
};

struct SlotDescriptor {
    uint32_t offset;
    uint32_t capacity;
    volatile LONG dataSize;
    volatile LONG dataType;
    int64_t  timestamp;
    uint32_t checksum;
    volatile LONG flags;
};
#pragma pack(pop)

static constexpr uint32_t kShmHeaderSize     = sizeof(ShmHeader);
static constexpr uint32_t kSlotDescriptorSize = sizeof(SlotDescriptor);

class ShmChannel {
public:
    ShmChannel();
    ~ShmChannel();

    ShmChannel(const ShmChannel&) = delete;
    ShmChannel& operator=(const ShmChannel&) = delete;

    Result CreateSegment(const wchar_t* name, uint32_t totalSize, uint32_t slotCount,
                         uint32_t slotCapacity, ShmHandle* outHandle);
    Result OpenSegment(const wchar_t* name, ShmHandle* outHandle);
    Result CloseSegment(ShmHandle handle);

    Result WriteSlot(ShmHandle handle, const void* data, uint32_t size, uint32_t dataType);
    Result ReadSlotPtr(ShmHandle handle, uint32_t slotIndex,
                       const void** outPtr, uint32_t* outSize, uint32_t* outType);
    Result MarkSlotRead(ShmHandle handle, uint32_t slotIndex);

    const wchar_t* GetSegmentName(ShmHandle handle) const;

    static uint32_t CRC32(const void* data, uint32_t size);

private:
    struct SegmentContext {
        HANDLE       fileMapping;
        void*        baseAddress;
        uint32_t     segmentSize;
        std::wstring name;
    };

    ShmHeader*      GetHeader(void* base);
    SlotDescriptor* GetSlots(void* base);
    void*           GetDataRegion(void* base);
    SegmentContext* FindContext(ShmHandle handle) const;

    mutable std::mutex                                          m_mutex;
    std::unordered_map<ShmHandle, std::unique_ptr<SegmentContext>> m_segments;
    SECURITY_DESCRIPTOR                                         m_securityDesc;
    SECURITY_ATTRIBUTES                                         m_securityAttr;
    bool                                                        m_securityInitialized;

    void InitSecurity();
};

} // namespace LlaShell::IPC

#endif // LLASHELL_COREIPC_SHM_CHANNEL_H
