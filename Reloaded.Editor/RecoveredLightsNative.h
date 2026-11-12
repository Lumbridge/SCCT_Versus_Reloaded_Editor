#pragma once
// Native reads for recovering a compiled map's lost lights
// (RecoveredLightModel.h): a level's lights as the lightmapper sees them, and
// a compiled level's lightmap texels, shadow geometry and zone ambient.
// Implemented in RecoveredLightsNative.inl (WorkflowEditor.cpp), next to the
// Light and Shadow map's reads of the same structures.
#include "RecoveredLightModel.h"

#include <memory>
#include <string>
#include <vector>

namespace RecoveredLightsNative
{
    struct LevelLight
    {
        RecoveredLights::Light light;
        bool lightClass = false;    // a Light actor or one of its subclasses
    };

    // Every live actor of the level with a light type, and the names of all
    // its live actors. The level need not be the one open in the viewports.
    bool ReadLights(void* level, std::vector<LevelLight>& lights, std::vector<std::string>& actorNames,
                    std::string& error);

    // The level's lightmap samples (thinned to `thin` units), its BSP and
    // shadow-casting static meshes, each zone's ambient and
    // LevelInfo.Brightness. The scene owns copies: it outlives the level.
    std::shared_ptr<RecoveredLights::Scene> Capture(void* level, double thin, std::string& error);
}
