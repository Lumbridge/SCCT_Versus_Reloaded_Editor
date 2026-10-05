#pragma once
#include <windows.h>
#include "WorkflowModel.h"
// Character Skin presets: a library of named skins for one team each (built-in camouflage
// and the user's own), with Spy and Merc lists, applied to the open map one team at a
// time. RE+ Tools > Character Skin Presets (41181), or Presets... in the Character Skins
// window. See CharacterSkinPresetsModel.h.
namespace CharacterSkinPresetsWindow
{
    constexpr unsigned Command = 41181;
    // current: the Character Skins window's values {slots, models, goggles}, which Save
    // Current as Preset keeps; null uses the map's applied skins. True once a preset
    // has been applied to the map.
    bool Open(HWND owner, const Workflow::Json& current = {});
}
