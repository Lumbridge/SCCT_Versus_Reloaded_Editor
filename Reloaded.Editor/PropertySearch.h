#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

// The native Properties windows (Actor, Level and the other WObjectProperties
// windows): a filter box over the property list, "(multiple values)" where a
// selection disagrees, and Undo for property edits.
class PropertySearch
{
public:
    static void Initialize();

    // A strip under the filter box that another tool fills for the objects a
    // window edits (the objective player counts). update builds, refreshes or
    // removes its controls (children of window) and returns their height, 0 for
    // none; layout places them from top across width; command takes their
    // WM_COMMAND and says whether it was theirs; closed forgets the window.
    // Control ids 0x5300-0x531f are the strip's.
    struct Strip
    {
        int (*update)(HWND window, const std::vector<uintptr_t>& objects) = nullptr;
        void (*layout)(HWND window, int top, int width) = nullptr;
        bool (*command)(HWND window, WPARAM wParam, LPARAM lParam) = nullptr;
        void (*closed)(HWND window) = nullptr;
    };
    static void SetStrip(const Strip& strip);
};
