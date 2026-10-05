#pragma once
#include <windows.h>

// Placement tools for the selected actors: drop onto the floor (or ceiling,
// or the wall ahead), align to the last-selected actor, distribute evenly,
// and copies in a line, round a centre or along a path with a live preview.
// Each operation is one Undo step. On the actor right-click menu as
// RE+: Placement and under RE+ Tools > Placement; End drops to the floor.
// PlacementModel.h holds the maths, PlacementNative.inl the editor side.
namespace PlacementTools
{
    // Reserved block 41260-41279.
    constexpr UINT kDropFloor = 41260, kDropFloorAlign = 41261, kDropCeiling = 41262, kDropWall = 41263;
    // Align X min, X centre, X max, Y min, ... Z max.
    constexpr UINT kAlignFirst = 41264, kAlignLast = 41272;
    constexpr UINT kDistributeX = 41273, kDistributeY = 41274, kDistributeZ = 41275, kDistributeLine = 41276;
    constexpr UINT kCopiesLinear = 41277, kCopiesRadial = 41278, kCopiesPath = 41279;

    // The actor right-click popup (resource 107): adds RE+: Placement.
    void AppendActorMenu(HMENU menu);
    // RE+ Tools > Placement on the frame's menu bar; idempotent.
    void InstallMenu(HMENU bar);
    // From the frame's command dispatcher. True when the command was ours.
    bool HandleCommand(UINT command);
    // Every level-viewport message, from the ViewportWndProc hook: follows the
    // order actors are selected in, and takes End. True when consumed.
    bool ViewportMessage(void* viewport, UINT message, WPARAM wParam, LPARAM lParam);
}
