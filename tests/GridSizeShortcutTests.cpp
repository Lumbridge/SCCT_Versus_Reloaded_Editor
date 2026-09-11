// Standalone: cl /std:c++20 /EHsc tests\GridSizeShortcutTests.cpp
#include "../Reloaded.Editor/GridSizeShortcutState.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

int main()
{
    using namespace GridSizeShortcut::Detail;
    // Match native viewport identities, including a view with no HWND parent.
    // Empty/destroyed entries and browser previews must not qualify.
    int levelViews[4] = {};
    int browserView = 0;
    alignas(void*) char frames[4][0x40] = {};
    alignas(void*) char configs[5][0x28] = {};
    for (int i = 0; i < 4; ++i)
    {
        void* view = &levelViews[i];
        void* frame = frames[i];
        std::memcpy(frames[i] + 0x3c, &view, sizeof(view));
        std::memcpy(configs[i + 1] + 0x24, &frame, sizeof(frame));
    }
    for (int i = 0; i < 4; ++i)
        assert(IsLevelViewport(&levelViews[i], configs, 5));
    assert(!IsLevelViewport(&browserView, configs, 5));
    assert(!IsLevelViewport(nullptr, configs, 5));
    assert(!IsLevelViewport(&levelViews[0], nullptr, 5));
    assert(!IsLevelViewport(&levelViews[0], configs, 0));
    assert(!IsLevelViewport(&levelViews[0], configs, -1));
    std::memset(frames[0] + 0x3c, 0, sizeof(void*));
    assert(!IsLevelViewport(&levelViews[0], configs, 5));

    WheelGesture gesture;
    assert(gesture.Consume(1, 60) == 0);
    assert(gesture.Consume(1, 60) == 1);
    assert(gesture.Consume(1, -40) == 0);
    assert(gesture.Consume(1, -80) == -1);
    assert(gesture.Consume(1, 360) == 3);
    assert(gesture.Consume(1, -240) == -2);
    assert(gesture.Consume(1, 60) == 0);
    assert(gesture.Consume(1, -60) == 0);
    assert(gesture.remainder == 0);
    assert(gesture.Consume(1, 90) == 0);
    assert(gesture.Consume(2, 30) == 0); // New viewport discards the old 90.
    assert(gesture.remainder == 30);
    gesture.Reset(); // Ctrl release, focus loss, or ordinary wheel input.
    assert(gesture.Consume(2, 90) == 0);
    gesture.ObserveViewport(3); // Moving away and back must also discard it.
    assert(gesture.Consume(2, 30) == 0);

    const int values[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };
    for (int i = 0; i < 13; ++i)
    {
        assert(NextPreset(static_cast<float>(values[i]), values, 13, 1) == (i == 12 ? -1 : i + 1));
        assert(NextPreset(static_cast<float>(values[i]), values, 13, -1) == i - 1);
    }
    assert(NextPreset(24, values, 13, 1) == 5);
    assert(NextPreset(24, values, 13, -1) == 4);
    const int unordered[] = { 64, 0, 16, 32, -1 };
    assert(NextPreset(24, unordered, 5, 1) == 3);
    assert(NextPreset(24, unordered, 5, -1) == 2);
    assert(NextPreset(16, nullptr, 0, 1) == -1);
    assert(NextPreset(std::numeric_limits<float>::quiet_NaN(), values, 13, 1) == -1);
    // Clamped full notches never accumulate a debt before reversing direction.
    gesture.Reset();
    assert(gesture.Consume(1, 1200) == 10);
    assert(gesture.Consume(1, -120) == -1);
    std::puts("Grid size shortcut tests passed");
}
