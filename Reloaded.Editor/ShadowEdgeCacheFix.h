#pragma once
#include "MemoryWriter.h"

namespace ShadowEdgeCacheFix
{
    // Both collision-triangle paths index this generation cache with a signed
    // 16-bit triangle ID. The original static array has only 5000 entries.
    // A valid triangle 6211 in the recovered Casino map overwrites a native
    // UClass at 11AB597C, producing a later UMaterial::ClearFallbacks AV.
    // Cover every nonnegative ID representable by the existing instructions.
    inline DWORD edgeGenerations[32768] = {};

    inline void Initialize()
    {
        struct Site { uintptr_t address; unsigned char bytes[7]; };
        const Site sites[] = {
            {0x111B68E2, {0x8B,0x04,0x8D,0x70,0xF8,0xAA,0x11}},
            {0x111B6BEC, {0x89,0x14,0x85,0x70,0xF8,0xAA,0x11}},
            {0x111B6DCA, {0x8B,0x14,0x85,0x70,0xF8,0xAA,0x11}},
            {0x111B71A5, {0x89,0x0C,0x85,0x70,0xF8,0xAA,0x11}},
        };
        for (const auto& site : sites)
            if (memcmp(reinterpret_cast<void*>(site.address), site.bytes, sizeof(site.bytes)))
            {
                Logger::log("ShadowEdgeCacheFix: instruction mismatch; patch not installed.");
                return;
            }
        const DWORD replacement = reinterpret_cast<DWORD>(edgeGenerations);
        for (const auto& site : sites)
            if (!MemoryWriter::WriteBytes(site.address + 3, &replacement, sizeof(replacement)))
            {
                for (const auto& restore : sites)
                    MemoryWriter::WriteBytes(restore.address + 3, restore.bytes + 3, 4);
                Logger::log("ShadowEdgeCacheFix: write failed; restored original operands.");
                return;
            }
        Logger::log("ShadowEdgeCacheFix: expanded collision triangle generation cache from 5000 to 32768 entries.");
    }
}
