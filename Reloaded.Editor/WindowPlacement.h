#pragma once
#include <windows.h>

// RE+ windows open where they were last closed: [WindowPlacement] in
// Reloaded_Editor.ini, one key per window (the format and the rules are in
// WindowPlacementModel.h). Restore right after the window is created, before
// it is shown; Remember while it is being destroyed.
namespace WindowPlacement
{
    // Puts the window back where `key` last left it, when enough of its title
    // bar would be on a monitor. A window that cannot be resized keeps its
    // size, one that cannot be maximised comes back restored. A hidden window
    // stays hidden unless it comes back maximised. False when nothing was
    // stored or the place is off every monitor.
    bool Restore(HWND window, const char* key);
    // Stores where the window is under `key`: its place and size when not
    // maximised, and whether it is maximised.
    void Remember(HWND window, const char* key);
}
