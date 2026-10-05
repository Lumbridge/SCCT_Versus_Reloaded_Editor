#pragma once
#include <windows.h>
#include <string>

// Sound Browser search box and Favorites. The native WBrowserSound list is
// filtered in place, or replaced by rows naming sounds from every package.
namespace SoundBrowserFavorites
{
    // Add/remove the selected sound: the button, and the context-menu item.
    constexpr UINT kToggleFavorite = 41030;

    void Initialize();
    // True while the list shows Reloaded rows (a search over every package,
    // or Favorites) rather than the native package/group list.
    bool CustomRows();
    // The full path a Reloaded row names; false for a native row.
    bool RowPath(int item, std::string& path);
    // True while a loaded row names this sound.
    bool HasLoadedRow(const std::string& path);
    // Adds the favourite item to the browser's right-click menu.
    void AddContextItems(HMENU context);
    void ToggleSelected();
    // The saved favourites changed elsewhere (the Favorites window): reload them.
    void FavoritesChanged();
    // Switches the browser to Favorites and selects the sound's row, so a
    // property's Use button takes it; false while the browser has no controls yet.
    bool ShowFavorite(const std::string& path);
    // Plays a loaded sound as the browser's Play does (streamed waves included).
    bool Play(void* sound, const std::string& path);
}
