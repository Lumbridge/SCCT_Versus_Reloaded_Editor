#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace RecoveredSoftBodySettings
{
    // UESoftBody::Serialize (110D7800) skips 11C..133. Update (110D4610)
    // uses that region for a temporary collision array and timestep state.
    // The wind timers at 114/118 are also simulation state.
    inline std::vector<unsigned char> Capture(const unsigned char* body,
                                              uint32_t authoredEnergyFactor)
    {
        std::vector<unsigned char> settings(body + 0xB8, body + 0x114);
        settings.insert(settings.end(), body + 0x134, body + 0x15C);

        // Reloaded's SoftBodyEnergyFactor hook changes a zero simulation
        // value to 0.01 on its first step. Compare both states as the same
        // default only when the actor's authored EnergyFactor is zero.
        uint32_t energyFactor;
        std::memcpy(&energyFactor, body + 0x110, sizeof(energyFactor));
        if (authoredEnergyFactor == 0 && energyFactor == 0x3C23D70A)
            std::memset(settings.data() + 0x110 - 0xB8, 0, sizeof(energyFactor));
        return settings;
    }
}
