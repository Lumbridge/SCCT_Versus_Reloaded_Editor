#pragma once
#include <string>

namespace TextureBrowser {
    void Initialize();
    // The saved favourites changed elsewhere (the Favorites window): reload them.
    void FavoritesChanged();
    // Shows the Favorites tab, where the current material is highlighted;
    // false while the browser has no window yet.
    bool ShowFavorite(const std::string& path);
    // Repaints the browser, after the current material changed.
    void Redraw();
}
