#pragma once
#include <windows.h>

// Measure in the editor's viewports: right-click > Measure > Start Here / To
// Here sets the two ends from the clicked point (grid-snapped like Builder
// Brush > Place Here), and M over a viewport measures on from the last end to
// the mouse. The line, its axis legs and a label are drawn into every level
// viewport by UUnrealEdEngine::Draw (MeasureModel.h holds the maths).
namespace MeasureTool
{
    // Reserved block 41090-41104.
    constexpr UINT kStart = 41090, kTo = 41091, kClear = 41092;

    void Initialize();
    // The viewport right-click popups (resources 106-108; surface is 108):
    // adds the Measure submenu.
    void AddMenu(HMENU menu, bool surface);
    // From the frame's command dispatcher. True when the command was ours.
    bool HandleCommand(UINT command);
    // Every level-viewport message, from the ViewportWndProc hook. Notes the
    // viewport a right-click came from, and takes M. True when consumed.
    bool ViewportMessage(void* viewport, UINT message, WPARAM wParam, LPARAM lParam);
}
