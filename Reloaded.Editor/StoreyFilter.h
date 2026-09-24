#pragma once
#include <windows.h>

// The storey slider for the editor's own viewports. The Map Design plan can
// draw one floor of a map because it draws the map itself; the native
// viewports cannot be told to, so the same storeys are applied by hiding what
// stands on the other floors. Actors, lights, movers, meshes and brush
// wireframes all go; surfaces already built into the BSP stay, so this reads
// best in the wireframe and brush-wireframe views.
namespace StoreyFilter
{
    constexpr UINT kOpen = 40966;

    // The Reloaded Tools entry: brings the palette up beside the viewports.
    void Open();
    // From the frame's command dispatcher. True when the command was ours.
    bool HandleCommand(UINT command);
    // Page Up / Page Down / Home over a viewport, while the palette is open.
    // True when the key was taken.
    bool ViewportKey(WPARAM key);
}
