#pragma once
#include <cstdint>

namespace GridSizeShortcut::Detail
{
    // The native level-viewport array has 0x28-byte entries. Each entry's
    // WViewportFrame is at +0x24; that frame's UViewport is at +0x3c.
    // Verified in viewport creation (0x10e43f0f), configuration saving
    // (0x10e43613), and WViewportFrame::SetViewport (0x10f03093).
    // Do not infer membership from HWND parents: level views can be unparented.
    inline bool IsLevelViewport(const void* viewport, const void* configs, int count)
    {
        static_assert(sizeof(void*) == 4, "The native editor layout is Win32-only");
        if (!viewport || !configs || count <= 0)
            return false;
        auto* entry = static_cast<const char*>(configs);
        for (int i = 0; i < count; ++i, entry += 0x28)
        {
            auto* frame = *reinterpret_cast<char* const*>(entry + 0x24);
            if (frame && *reinterpret_cast<void* const*>(frame + 0x3c) == viewport)
                return true;
        }
        return false;
    }

    // Kept independent of Win32 and editor memory for standalone testing.
    struct WheelGesture
    {
        uintptr_t viewport = 0;
        int remainder = 0;

        void Reset()
        {
            viewport = 0;
            remainder = 0;
        }

        void ObserveViewport(uintptr_t target)
        {
            if (viewport != target)
            {
                Reset();
                viewport = target;
            }
        }

        int Consume(uintptr_t target, int delta)
        {
            ObserveViewport(target);
            const int total = remainder + delta;
            const int steps = total / 120; // Win32 WHEEL_DELTA
            remainder = total % 120;
            return steps;
        }
    };

    // Use numeric order, even if the native combo box's display order changes.
    // A custom grid value moves to the nearest preset in the requested direction.
    inline int NextPreset(float current, const int* values, int count, int direction)
    {
        int best = -1;
        for (int i = 0; i < count; ++i)
        {
            if (values[i] <= 0)
                continue;
            if (direction > 0 && values[i] > current
                && (best < 0 || values[i] < values[best]))
                best = i;
            if (direction < 0 && values[i] < current
                && (best < 0 || values[i] > values[best]))
                best = i;
        }
        return best;
    }
}
