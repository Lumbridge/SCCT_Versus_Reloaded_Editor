#include "../Reloaded.Editor/RecoveredSoftBodySettings.h"
#include <array>
#include <cstdio>
#include <cstdlib>

static void Check(bool passed, const char* message)
{
    if (!passed) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

static void Write32(unsigned char* at, uint32_t value) { std::memcpy(at, &value, sizeof(value)); }

int main()
{
    // The actor's +380 and +384, which Update copies to the simulation's +154 and +158 (bit 2).
    const uint32_t actor380 = 40, actor384 = 0xF0; // bit 2 clear, other bits set
    auto capture = [&](const unsigned char* body, uint32_t energy) {
        return RecoveredSoftBodySettings::Capture(body, energy, actor380, actor384);
    };

    std::array<unsigned char, 0x15C> original{};
    for (size_t i = 0; i < original.size(); ++i)
        original[i] = static_cast<unsigned char>(i * 13);
    std::memset(original.data() + 0x110, 0, 4);
    const auto expected = capture(original.data(), 0);

    // A stepped curtain can have allocated collision scratch, a different
    // timestep, wind timers and Reloaded's default damping applied.
    auto stepped = original;
    for (size_t i = 0x114; i < 0x134; ++i) stepped[i] ^= 0x5A;
    const uint32_t defaultDamping = 0x3C23D70A;
    std::memcpy(stepped.data() + 0x110, &defaultDamping, 4);
    Check(capture(stepped.data(), 0) == expected,
          "Simulation state rejected a matching curtain");

    // A body that never stepped (RE+ saved it before its build step was
    // restored) still holds the constructor's 25 and 1 where a stepped one
    // holds the actor's values. The same actor makes them the same curtain.
    auto unstepped = original, regenerated = stepped;
    Write32(unstepped.data() + 0x154, 25);
    Write32(unstepped.data() + 0x158, 1);
    Write32(regenerated.data() + 0x154, actor380);
    Write32(regenerated.data() + 0x158, (actor384 >> 2) & 1);
    Check(capture(unstepped.data(), 0) == capture(regenerated.data(), 0),
          "A never-stepped original did not match its stepped regeneration");

    // What decides those two fields, the actor's values, is still compared.
    Check(RecoveredSoftBodySettings::Capture(original.data(), 0, actor380 + 1, actor384) != expected,
          "A change to the actor's +380 was ignored");
    Check(RecoveredSoftBodySettings::Capture(original.data(), 0, actor380, actor384 ^ 4) != expected,
          "A change to bit 2 of the actor's +384 was ignored");
    Check(RecoveredSoftBodySettings::Capture(original.data(), 0, actor380, actor384 ^ 0x100) == expected,
          "A bit of the actor's +384 that Update does not copy changed the settings");

    // Every other compared settings byte must still detect an authored change.
    for (size_t offset = 0xB8; offset < original.size(); ++offset)
    {
        if (offset >= 0x114 && offset < 0x134) continue;
        if (offset >= 0x154 && offset < 0x15C) continue;
        auto changed = original;
        changed[offset] ^= 1;
        Check(capture(changed.data(), 0) != expected,
              "A physical setting change was ignored");
    }
    Check(capture(stepped.data(), defaultDamping) != capture(original.data(), defaultDamping),
          "An authored nonzero damping value was treated as the default");
    const uint32_t otherDamping = 0x3DCCCCCD; // 0.1f
    std::memcpy(stepped.data() + 0x110, &otherDamping, 4);
    Check(capture(stepped.data(), 0) != expected,
          "An unrelated damping change was ignored");
    std::puts("Recovered soft-body settings tests passed");
}
