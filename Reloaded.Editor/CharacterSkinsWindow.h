#pragma once
#include <windows.h>
// Character Skins: the open map's own spy and merc materials (for example snow camo).
// RE+ Tools > Character Skins (41180). See CharacterSkinsModel.h.
namespace CharacterSkinsWindow
{
    constexpr unsigned Command = 41180;
    void Open(HWND owner);
}
