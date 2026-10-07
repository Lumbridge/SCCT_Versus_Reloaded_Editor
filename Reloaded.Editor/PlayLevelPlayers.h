#pragma once
#include <windows.h>

// The "Play Level:" list on the editor's top button bar, beside Play Level and
// Story: the lobby size an editor Play Level plays the map's objective player
// counts with (ObjectivePlayersModel.h), or every objective.
namespace PlayLevelPlayers
{
    // Puts the list on the frame's top bar once the bar exists; safe to call
    // again, and puts it back if the bar was rebuilt.
    void Attach(HWND frame);
}
