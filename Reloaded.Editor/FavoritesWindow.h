#pragma once
#include <windows.h>

// View > Favorites: the favourite materials, static meshes and sounds of the
// three browsers in one tool window, with a type, tag, package and search
// filter, user tags ("Ship interior", "Lights"), and actions to show a
// favourite in its browser, make it the current one, play it or remove it.
// The favourite lists stay in the browsers' own ini sections, so their
// Favorites views and this window always agree.
namespace FavoritesWindow
{
    // View > Favorites..., and the "All Favorites..." button and right-click
    // entry in each browser (they use the same id).
    constexpr UINT kOpen = 41340;

    void Open();
    // From the frame's command dispatcher. True when the command was ours.
    bool HandleCommand(UINT command);
    // Puts View > Favorites... after the browser entries. Idempotent.
    void InstallMenu(HMENU bar);
    // A browser saved its favourites: the open window shows the change.
    void Changed();
}
