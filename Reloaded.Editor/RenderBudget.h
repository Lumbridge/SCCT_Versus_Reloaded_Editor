#pragma once
#include <windows.h>

// Build > Render Budget...: per zone, the static meshes, triangles, actors,
// BSP nodes and surfaces, materials, textures and emitters, flagged against
// the densest shipped Versus map (RenderBudgetModel.h), and what renders
// from the perspective viewport's camera: the zones visible through the
// portals and the actors the engine draws, with totals for that view.
namespace RenderBudget
{
    constexpr UINT kOpenCommand = 41280;
    // Opens the window if needed and measures the perspective camera's view.
    constexpr UINT kViewCommand = 41281;

    bool HandleCommand(UINT command);
    void Open(HWND owner);
}
