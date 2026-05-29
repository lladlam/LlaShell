#include "CoreIPC/shm_channel.h"
#include <intrin.h>

namespace LlaShell::IPC {

Result ShmChannel::WriteSlot(ShmHandle handle, const void* data, uint32_t size, uint32_t dataType) {
    if (!data || size == 0) return Result::InvalidArg;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto* ctx = FindContext(handle);
    if (!ctx) return Result::InvalidArg;

    ShmHeader* hdr = GetHeader(ctx->baseAddress);
    SlotDescriptor* slots = GetSlots(ctx->baseAddress);

    if (hdr->magic != kShmMagic) return Result::InternalError;

    // Get next write slot (single writer — atomic increment)
    LONG idx = InterlockedIncrement(&hdr->writeIndex) - 1;
    idx = idx % static_cast<LONG>(hdr->slotCount);

    SlotDescriptor& slot = slots[idx];

    // Wait for consumer to finish reading (spin + yield)
    int spinCount = 0;
    while (slot.flags & SLOT_FLAG_VALID) {
        if (++spinCount > 1000) return Result::SlotFull;
        SwitchToThread();
    }

    if (size > slot.capacity) return Result::BufferTooSmall;

    // Write data
    void* dest = static_cast<uint8_t*>(ctx->baseAddress) + slot.offset;
    memcpy(dest, data, size);

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    slot.dataSize  = static_cast<LONG>(size);
    slot.dataType  = static_cast<LONG>(dataType);
    slot.timestamp = now.QuadPart;
    slot.checksum  = CRC32(data, size);

    MemoryBarrier();

    slot.flags = SLOT_FLAG_VALID;

    return Result::Success;
}

Result ShmChannel::ReadSlotPtr(ShmHandle handle, uint32_t slotIndex,
                                const void** outPtr, uint32_t* outSize, uint32_t* outType) {
    if (!outPtr || !outSize || !outType) return Result::InvalidArg;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto* ctx = FindContext(handle);
    if (!ctx) return Result::InvalidArg;

    ShmHeader* hdr = GetHeader(ctx->baseAddress);
    if (slotIndex >= hdr->slotCount) return Result::InvalidArg;

    SlotDescriptor* slots = GetSlots(ctx->baseAddress);
    SlotDescriptor& slot = slots[slotIndex];

    if (!(slot.flags & SLOT_FLAG_VALID)) return Result::SlotNotReady;

    const void* src = static_cast<const uint8_t*>(ctx->baseAddress) + slot.offset;
    uint32_t size = static_cast<uint32_t>(slot.dataSize);

    // Validate checksum before exposing pointer
    if (slot.checksum != CRC32(src, size)) return Result::ChecksumError;

    *outPtr  = src;
    *outSize = size;
    *outType = static_cast<uint32_t>(slot.dataType);

    return Result::Success;
}

Result ShmChannel::MarkSlotRead(ShmHandle handle, uint32_t slotIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto* ctx = FindContext(handle);
    if (!ctx) return Result::InvalidArg;

    ShmHeader* hdr = GetHeader(ctx->baseAddress);
    if (slotIndex >= hdr->slotCount) return Result::InvalidArg;

    SlotDescriptor* slots = GetSlots(ctx->baseAddress);
    InterlockedOr(&slots[slotIndex].flags, SLOT_FLAG_READ_DONE);

    // Clear the VALID flag so writer can reuse this slot
    InterlockedAnd(&slots[slotIndex].flags, ~SLOT_FLAG_VALID);

    return Result::Success;
}

} // namespace LlaShell::IPC
