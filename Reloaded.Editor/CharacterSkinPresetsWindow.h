#pragma once
#include <windows.h>
#include "WorkflowModel.h"
// Character Skin presets: a library of named spy and merc skin sets (built-in camouflage
// and the user's own) applied to the open map. RE+ Tools > Character Skin Presets (41181),
// or Presets... in the Character Skins window. See CharacterSkinPresetsModel.h.
namespace CharacterSkinPresetsWindow
{
    constexpr unsigned Command = 41181;
    // current: the Character Skins window's values {slots, models, goggles}, which Save
    // Current as Preset keeps; null uses the map's applied skins. True once a preset
    // has been applied to the map.
    bool Open(HWND owner, const Workflow::Json& current = {});
}
