#include "CoreIPC/shm_channel.h"
#include <intrin.h>

namespace LlaShell::IPC {

static uint32_t g_crc32Table[256];
static bool     g_crc32TableBuilt = false;

static void BuildCRC32Table() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
        g_crc32Table[i] = crc;
    }
    g_crc32TableBuilt = true;
}

uint32_t ShmChannel::CRC32(const void* data, uint32_t size) {
    if (!g_crc32TableBuilt) BuildCRC32Table();

    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;

    for (uint32_t i = 0; i < size; i++) {
        crc = g_crc32Table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

} // namespace LlaShell::IPC
