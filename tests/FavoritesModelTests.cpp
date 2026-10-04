// The Texture Browser's side of the shared favourites model; the path split,
// package counts and row order are covered by MeshFavoritesModelTests.
#include "../Reloaded.Editor/FavoritesModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace Favorites;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}

int main()
{
    try
    {
        // Texture favourites in the order they were added.
        const std::vector<Favorite> favorites = {
            {"Echelon.EMF.EMFInvDarkBlue", true},
            {"GenTex.Walls.Brick", true},
            {"Gone_TXT.Floors.Tile", false},
            {"echelon.Lights.Bulb", true},
            {"Lobby.MyLevel.Poster", false},
            {"GenTex.Walls.Arch", true},
        };

        // Tiles: the loaded favourites only, in the row order; the rest apart.
        auto view = Tiles(favorites, "", Sort::Package);
        Check(view.tiles == std::vector<size_t>{0, 3, 5, 1}, "package order tiles");
        Check(view.notLoaded.size() == 2 && view.notLoaded[0].favorite == 2 && view.notLoaded[1].favorite == 4,
              "not loaded rows in package order");
        Check(view.notLoaded[0].label == "Gone_TXT.Floors.Tile  [not loaded]", "not loaded label");

        view = Tiles(favorites, "", Sort::Name);
        Check(view.tiles == std::vector<size_t>{5, 1, 3, 0}, "name order tiles");
        Check(view.notLoaded[0].favorite == 4 && view.notLoaded[0].label == "Poster  (Lobby.MyLevel)  [not loaded]",
              "not loaded by name");

        view = Tiles(favorites, "", Sort::Newest);
        Check(view.tiles == std::vector<size_t>{5, 3, 1, 0}, "newest first");
        Check(view.notLoaded[0].favorite == 4 && view.notLoaded[1].favorite == 2, "not loaded newest first");
        view = Tiles(favorites, "", Sort::Oldest);
        Check(view.tiles == std::vector<size_t>{0, 1, 3, 5}, "oldest first");

        // A package filter, whatever its case, applies to both.
        view = Tiles(favorites, "ECHELON", Sort::Oldest);
        Check(view.tiles == std::vector<size_t>{0, 3} && view.notLoaded.empty(), "filter loaded");
        view = Tiles(favorites, "gone_txt", Sort::Package);
        Check(view.tiles.empty() && view.notLoaded.size() == 1 && view.notLoaded[0].favorite == 2, "filter not loaded");
        view = Tiles(favorites, "Missing", Sort::Package);
        Check(view.tiles.empty() && view.notLoaded.empty(), "filter with no matches");
        view = Tiles({}, "", Sort::Package);
        Check(view.tiles.empty() && view.notLoaded.empty(), "no favourites");

        // The package list counts loaded and not loaded alike.
        const auto packages = Packages(favorites);
        Check(packages.size() == 4 && packages[0].name == "Echelon" && packages[0].count == 2 && packages[0].loaded == 2,
              "echelon package");
        Check(packages[2].name == "Gone_TXT" && packages[2].count == 1 && packages[2].loaded == 0, "missing package");

        std::cout << "FavoritesModelTests: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FavoritesModelTests FAILED: " << e.what() << "\n";
        return 1;
    }
}
