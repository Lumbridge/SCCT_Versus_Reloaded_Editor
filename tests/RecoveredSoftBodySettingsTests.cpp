#include "../Reloaded.Editor/RecoveredSoftBodySettings.h"
#include <array>
#include <cstdio>
#include <cstdlib>

static void Check(bool passed, const char* message)
{
    if (!passed) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main()
{
    std::array<unsigned char, 0x15C> original{};
    for (size_t i = 0; i < original.size(); ++i)
        original[i] = static_cast<unsigned char>(i * 13);
    std::memset(original.data() + 0x110, 0, 4);
    const auto expected = RecoveredSoftBodySettings::Capture(original.data(), 0);

    // A stepped curtain can have allocated collision scratch, a different
    // timestep, wind timers and Reloaded's default damping applied.
    auto stepped = original;
    for (size_t i = 0x114; i < 0x134; ++i) stepped[i] ^= 0x5A;
    const uint32_t defaultDamping = 0x3C23D70A;
    std::memcpy(stepped.data() + 0x110, &defaultDamping, 4);
    Check(RecoveredSoftBodySettings::Capture(stepped.data(), 0) == expected,
          "Simulation state rejected a matching curtain");

    // Every compared settings byte must still detect an authored change.
    for (size_t offset = 0xB8; offset < original.size(); ++offset)
    {
        if (offset >= 0x114 && offset < 0x134) continue;
        auto changed = original;
        changed[offset] ^= 1;
        Check(RecoveredSoftBodySettings::Capture(changed.data(), 0) != expected,
              "A physical setting change was ignored");
    }
    Check(RecoveredSoftBodySettings::Capture(stepped.data(), defaultDamping)
              != RecoveredSoftBodySettings::Capture(original.data(), defaultDamping),
          "An authored nonzero damping value was treated as the default");
    const uint32_t otherDamping = 0x3DCCCCCD; // 0.1f
    std::memcpy(stepped.data() + 0x110, &otherDamping, 4);
    Check(RecoveredSoftBodySettings::Capture(stepped.data(), 0) != expected,
          "An unrelated damping change was ignored");
    std::puts("Recovered soft-body settings tests passed");
}
