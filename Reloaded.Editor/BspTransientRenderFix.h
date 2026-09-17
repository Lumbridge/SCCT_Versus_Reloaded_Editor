#pragma once
#include "MemoryWriter.h"

namespace BspTransientRenderFix
{
    inline void Initialize()
    {
        // The single-node path in RenderLevel constructs a temporary BSP batch
        // at 111A9805. Its node list aliases the scene-arena entry at EBX+4;
        // light lists created by 111BA2C0 also belong to the frame arena.
        // The ordinary batch destructor at 111A3BC0 nevertheless sends both
        // pointers to GMalloc::Free. This releases/corrupts a live arena chunk,
        // causing either FMallocWindows::Free assertions or later RenderLevel
        // and Draw3DAxis faults. The frame mark owns these allocations.
        // Omit only this non-owning local's normal-path destructor call. Keep
        // the owning batch destructor and all other cleanup sites unchanged.
        constexpr uintptr_t site = 0x111A9888;
        const unsigned char expected[] = {0xE8, 0x33, 0xA3, 0xFF, 0xFF};
        const unsigned char replacement[] = {0x90, 0x90, 0x90, 0x90, 0x90};
        if (memcmp(reinterpret_cast<void*>(site), expected, sizeof(expected)))
        {
            Logger::log("BspTransientRenderFix: cleanup byte mismatch; patch not installed.");
            return;
        }
        if (MemoryWriter::WriteBytes(site, replacement, sizeof(replacement)))
            Logger::log("BspTransientRenderFix: preserve frame-owned single-node render buffers.");
    }
}
