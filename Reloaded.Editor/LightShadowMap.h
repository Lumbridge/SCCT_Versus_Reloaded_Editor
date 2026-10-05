#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

namespace LightShadow { struct Cell; }

// RE+ Tools > Light and Shadow Map: colours the walkable BSP floor by how lit
// it is (blue hidden, yellow partly visible, red exposed) in every level
// viewport, with a legend and, in 2D views, a readout of the patch under the
// mouse. The estimate and its limits are described in LightShadowModel.h; the
// thresholds live in Reloaded_Editor.ini [LightShadowMap]. Map Design draws
// the same patches as a plan layer (Tools > Light and shadow map).
namespace LightShadowMap
{
    // Reserved block 41200-41219.
    constexpr UINT kToggle = 41200, kSettings = 41201, kRefresh = 41202;

    bool HandleCommand(UINT command);
    // From Measure's UUnrealEdEngine::Draw hook, before its own overlay.
    void DrawViewportOverlay(uintptr_t viewport, uintptr_t sceneNode);
    // Map Design's plan layer: the current patches (sampled now when there are
    // none or the map changed), their size, and whether the layer is in use so
    // the patches follow the map's edits.
    const std::vector<LightShadow::Cell>& Patches();
    int PatchSize();
    void SetPlanLayer(bool on);
}
