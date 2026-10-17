#pragma once
#include <windows.h>

// Placement tools for the selected actors: drop onto the floor (or ceiling,
// or the wall ahead, or the wall along X or Y), align to the last-selected
// actor, distribute evenly, snap pivots, box edges or yaw to the grid, and
// copies in a line, round a centre or along a path with a live preview.
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
    // Reserved block 41420-41439 (41431-41439 spare): snap pivots or box edges
    // on X and Y or all axes, yaw to 90, 45 or 15 degrees, push along an axis.
    constexpr UINT kSnapPivotsXY = 41420, kSnapPivotsXYZ = 41421, kSnapEdgesXY = 41422, kSnapEdgesXYZ = 41423;
    constexpr UINT kSnapYaw90 = 41424, kSnapYaw45 = 41425, kSnapYaw15 = 41426;
    constexpr UINT kPushPlusX = 41427, kPushMinusX = 41428, kPushPlusY = 41429, kPushMinusY = 41430;

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
