#include "CoreIPC/shm_channel.h"
#include <Sddl.h>

#pragma comment(lib, "advapi32.lib")

namespace LlaShell::IPC {

static constexpr wchar_t kShmPrefix[] = L"Local\\LlaShmem.v1.";

ShmChannel::ShmChannel() : m_securityInitialized(false) {
    ZeroMemory(&m_securityDesc, sizeof(m_securityDesc));
    ZeroMemory(&m_securityAttr, sizeof(m_securityAttr));
    InitSecurity();
}

ShmChannel::~ShmChannel() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [handle, ctx] : m_segments) {
        if (ctx->baseAddress) UnmapViewOfFile(ctx->baseAddress);
        if (ctx->fileMapping) CloseHandle(ctx->fileMapping);
    }
    m_segments.clear();
}

void ShmChannel::InitSecurity() {
    if (m_securityInitialized) return;

    if (InitializeSecurityDescriptor(&m_securityDesc, SECURITY_DESCRIPTOR_REVISION)) {
        if (SetSecurityDescriptorDacl(&m_securityDesc, TRUE, NULL, FALSE)) {
            m_securityAttr.nLength              = sizeof(SECURITY_ATTRIBUTES);
            m_securityAttr.lpSecurityDescriptor = &m_securityDesc;
            m_securityAttr.bInheritHandle       = FALSE;
            m_securityInitialized = true;
        }
    }
}

ShmHeader* ShmChannel::GetHeader(void* base) {
    return static_cast<ShmHeader*>(base);
}

SlotDescriptor* ShmChannel::GetSlots(void* base) {
    return reinterpret_cast<SlotDescriptor*>(
        static_cast<uint8_t*>(base) + kShmHeaderSize);
}

void* ShmChannel::GetDataRegion(void* base) {
    ShmHeader* hdr = GetHeader(base);
    uint32_t slotsSize = hdr->slotCount * kSlotDescriptorSize;
    return static_cast<uint8_t*>(base) + kShmHeaderSize + slotsSize;
}

ShmChannel::SegmentContext* ShmChannel::FindContext(ShmHandle handle) const {
    auto it = m_segments.find(handle);
    return (it != m_segments.end()) ? it->second.get() : nullptr;
}

Result ShmChannel::CreateSegment(const wchar_t* name, uint32_t totalSize,
                                  uint32_t slotCount, uint32_t slotCapacity,
                                  ShmHandle* outHandle) {
    if (!name || !outHandle || slotCount == 0 || slotCapacity == 0)
        return Result::InvalidArg;

    // Calculate required size: header + slots + (slotCapacity * slotCount)
    uint32_t dataRegionSize = slotCapacity * slotCount;
    uint32_t actualSize = kShmHeaderSize + (slotCount * kSlotDescriptorSize) + dataRegionSize;
    if (totalSize < actualSize) totalSize = actualSize;

    std::wstring fullName = kShmPrefix;
    fullName += name;

    InitSecurity();

    HANDLE hMapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        &m_securityAttr,
        PAGE_READWRITE,
        0, totalSize,
        fullName.c_str());

    if (!hMapping) return Result::ShmCreateFailed;

    void* base = MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 0, 0, totalSize);
    if (!base) {
        CloseHandle(hMapping);
        return Result::ShmMapFailed;
    }

    // Initialize header
    ShmHeader* hdr = GetHeader(base);
    hdr->magic        = kShmMagic;
    hdr->version      = kShmVersion;
    hdr->segmentSize  = totalSize;
    hdr->slotCount    = slotCount;
    hdr->writeIndex   = 0;
    hdr->readIndex    = 0;
    hdr->flags        = 0;
    hdr->generation   = 1;

    // Initialize slot descriptors
    SlotDescriptor* slots = GetSlots(base);
    uint32_t dataOffset = kShmHeaderSize + (slotCount * kSlotDescriptorSize);

    for (uint32_t i = 0; i < slotCount; i++) {
        slots[i].offset    = dataOffset + (i * slotCapacity);
        slots[i].capacity  = slotCapacity;
        slots[i].dataSize  = 0;
        slots[i].dataType  = 0;
        slots[i].timestamp = 0;
        slots[i].checksum  = 0;
        slots[i].flags     = 0;
    }

    auto ctx = std::make_unique<SegmentContext>();
    ctx->fileMapping  = hMapping;
    ctx->baseAddress  = base;
    ctx->segmentSize  = totalSize;
    ctx->name         = fullName;

    ShmHandle handle = reinterpret_cast<ShmHandle>(base); // use base address as handle
    m_segments[handle] = std::move(ctx);

    *outHandle = handle;
    return Result::Success;
}

Result ShmChannel::OpenSegment(const wchar_t* name, ShmHandle* outHandle) {
    if (!name || !outHandle) return Result::InvalidArg;

    std::wstring fullName = kShmPrefix;
    fullName += name;

    HANDLE hMapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, fullName.c_str());
    if (!hMapping) return Result::ShmOpenFailed;

    ShmHeader tempHdr{};
    // First map minimally to read the header and get segment size
    void* tempBase = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, kShmHeaderSize);
    if (!tempBase) {
        CloseHandle(hMapping);
        return Result::ShmMapFailed;
    }
    memcpy(&tempHdr, tempBase, kShmHeaderSize);
    UnmapViewOfFile(tempBase);

    if (tempHdr.magic != kShmMagic) {
        CloseHandle(hMapping);
        return Result::ShmOpenFailed;
    }

    void* base = MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 0, 0, tempHdr.segmentSize);
    if (!base) {
        CloseHandle(hMapping);
        return Result::ShmMapFailed;
    }

    auto ctx = std::make_unique<SegmentContext>();
    ctx->fileMapping  = hMapping;
    ctx->baseAddress  = base;
    ctx->segmentSize  = tempHdr.segmentSize;
    ctx->name         = fullName;

    ShmHandle handle = reinterpret_cast<ShmHandle>(base);
    m_segments[handle] = std::move(ctx);

    *outHandle = handle;
    return Result::Success;
}

Result ShmChannel::CloseSegment(ShmHandle handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_segments.find(handle);
    if (it == m_segments.end()) return Result::InvalidArg;

    if (it->second->baseAddress) UnmapViewOfFile(it->second->baseAddress);
    if (it->second->fileMapping) CloseHandle(it->second->fileMapping);
    m_segments.erase(it);
    return Result::Success;
}

const wchar_t* ShmChannel::GetSegmentName(ShmHandle handle) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto* ctx = FindContext(handle);
    return ctx ? ctx->name.c_str() : nullptr;
}

} // namespace LlaShell::IPC
