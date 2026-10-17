#include "../Reloaded.Editor/MeshFavoritesModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace MeshFavorites;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
std::vector<std::string> Labels(const std::vector<Row>& rows)
{
    std::vector<std::string> out;
    for (const auto& r : rows) out.push_back(r.label);
    return out;
}

int main()
{
    try
    {
        // Paths.
        auto p = Split("Generic_Props.Crates.Crate01");
        Check(p.package == "Generic_Props" && p.group == "Crates" && p.name == "Crate01", "three parts");
        p = Split("Pkg.Sub.Deeper.Mesh");
        Check(p.package == "Pkg" && p.group == "Sub.Deeper" && p.name == "Mesh", "nested groups");
        p = Split("Pkg.Mesh");
        Check(p.package == "Pkg" && p.group.empty() && p.name == "Mesh", "no group");
        p = Split("Lonely");
        Check(p.package == "Lonely" && p.name == "Lonely", "no dots");

        // Favourites in the order they were added.
        const std::vector<Favorite> favorites = {
            {"zeta_meshes.Pipes.PipeB", true},
            {"Alpha_Props.Crates.crate02", true},
            {"alpha_props.Barrels.Barrel", true},
            {"CpusS.MyLevel.Embedded", false},
            {"Zeta_Meshes.Pipes.PipeA", true},
            {"Alpha_Props.Crates.Crate01", false},
        };

        // Packages: one entry per package regardless of case, in name order.
        const auto packages = Packages(favorites);
        Check(packages.size() == 3, "three packages");
        Check(packages[0].name == "Alpha_Props" && packages[0].count == 3 && packages[0].loaded == 2, "alpha counts");
        Check(packages[1].name == "CpusS" && packages[1].count == 1 && packages[1].loaded == 0, "map package");
        Check(packages[2].name == "zeta_meshes" && packages[2].count == 2, "zeta keeps first spelling");
        Check(PackageLabel(packages[0]) == "Alpha_Props (3)" && AllLabel(6) == "All packages (6)", "labels");
        Check(Packages({}).empty(), "no favourites");

        // Package order: grouped by package, then group, then name; unloaded last.
        Check(Labels(Rows(favorites, "", Sort::Package)) == std::vector<std::string>{
                  "alpha_props.Barrels.Barrel", "Alpha_Props.Crates.crate02",
                  "Zeta_Meshes.Pipes.PipeA", "zeta_meshes.Pipes.PipeB",
                  "Alpha_Props.Crates.Crate01  [not loaded]", "CpusS.MyLevel.Embedded  [not loaded]"},
              "package order");

        // Name order leads with the mesh name.
        Check(Labels(Rows(favorites, "", Sort::Name)) == std::vector<std::string>{
                  "Barrel  (alpha_props.Barrels)", "crate02  (Alpha_Props.Crates)",
                  "PipeA  (Zeta_Meshes.Pipes)", "PipeB  (zeta_meshes.Pipes)",
                  "Crate01  (Alpha_Props.Crates)  [not loaded]", "Embedded  (CpusS.MyLevel)  [not loaded]"},
              "name order");

        // Added order, both ways.
        auto newest = Rows(favorites, "", Sort::Newest);
        Check(newest.size() == 6 && newest[0].favorite == 4 && newest[1].favorite == 2 && newest[2].favorite == 1 && newest[3].favorite == 0
                  && newest[4].favorite == 5 && newest[5].favorite == 3, "newest first");
        auto oldest = Rows(favorites, "", Sort::Oldest);
        Check(oldest[0].favorite == 0 && oldest[1].favorite == 1 && oldest[2].favorite == 2 && oldest[3].favorite == 4, "oldest first");

        // A package filter, case-insensitive.
        auto alpha = Rows(favorites, "ALPHA_PROPS", Sort::Package);
        Check(alpha.size() == 3 && alpha[0].favorite == 2 && alpha[1].favorite == 1 && alpha[2].favorite == 5, "filter by package");
        Check(Rows(favorites, "Missing", Sort::Package).empty(), "filter with no matches");

        // Each row points back at its favourite.
        for (const auto& row : Rows(favorites, "", Sort::Name))
            Check(row.favorite < favorites.size() && row.label.find(Split(favorites[row.favorite].path).name) == 0, "row index");

        // Search rows over every loaded mesh, with a gloss standing in for
        // the editor's English for French names.
        const std::vector<std::string> meshes = {
            "Ind03_Props.Portes.Porte_Metal01", "Generic_Props.Crates.Crate01", "ind03_props.Portes.Porte_Bois01",
            "Ind03_Props.Tuyaux.Tuyau01", "Ind03_Props.Portes.Garage.Porte_Garage", "Lobby.MyLevel.Porte_Metal02"};
        SearchText() = [](const std::string& path) {
            std::string text = path;
            if (path.find("Porte") != std::string::npos) text += " door";
            if (path.find("Metal") != std::string::npos) text += " metal";
            if (path.find("Tuyau") != std::string::npos) text += " pipe";
            return text;
        };
        Check(Labels(SearchRows(meshes, "", "door metal", Sort::Package)) == std::vector<std::string>{
                  "Ind03_Props.Portes.Porte_Metal01", "Lobby.MyLevel.Porte_Metal02"},
              "English words, every package, full paths");
        Check(Labels(SearchRows(meshes, "", "metal door", Sort::Name)) == std::vector<std::string>{
                  "Porte_Metal01  (Ind03_Props.Portes)", "Porte_Metal02  (Lobby.MyLevel)"},
              "by name");
        auto search = SearchRows(meshes, "", "PIPE", Sort::Package);
        Check(search.size() == 1 && meshes[search[0].favorite] == "Ind03_Props.Tuyaux.Tuyau01", "row points at its mesh");
        Check(SearchRows(meshes, "", "door wood", Sort::Package).empty(), "no match, no rows");

        // One package, whatever its case, then one group and those inside it.
        Check(Labels(SearchRows(meshes, "IND03_PROPS", "door", Sort::Package)) == std::vector<std::string>{
                  "ind03_props.Portes.Porte_Bois01", "Ind03_Props.Portes.Porte_Metal01", "Ind03_Props.Portes.Garage.Porte_Garage"},
              "one package");
        Check(SearchRows(meshes, "Ind03_Props", "door", Sort::Package, "portes").size() == 3, "a group holds the groups inside it");
        Check(Labels(SearchRows(meshes, "Ind03_Props", "door", Sort::Package, "Portes.Garage")) == std::vector<std::string>{
                  "Ind03_Props.Portes.Garage.Porte_Garage"},
              "a nested group");
        Check(SearchRows(meshes, "Ind03_Props", "", Sort::Package, "Port").empty(), "a group name is matched whole");
        Check(SearchRows(meshes, "Ind03_Props", "", Sort::Package, "Tuyaux").size() == 1, "blank search, one group");
        Check(SearchRows(meshes, "Missing", "door", Sort::Package).empty(), "package with no meshes");
        SearchText() = nullptr;
        Check(SearchRows(meshes, "", "door", Sort::Package).empty(), "no English without the gloss");

        // The search row under the list: 21 pixels high, 3 below the list.
        // The native layout gives the list 10..300 in a pane 306 high.
        const Box pane{0, 0, 200, 306};
        SearchRowPlace place, first;
        Check(LayOutSearchRow({4, 10, 196, 300}, {}, {}, pane, 21, 3, first), "a new list is laid out");
        Check(first.list == Box{4, 10, 196, 276} && first.row == Box{4, 279, 196, 300}, "the row takes the list's bottom");
        Check(!LayOutSearchRow(first.list, first.list, first.row, pane, 21, 3, place), "laid out, it stays");
        Check(LayOutSearchRow({4, 10, 196, 400}, first.list, first.row, {0, 0, 200, 406}, 21, 3, place)
                  && place.list == Box{4, 10, 196, 376} && place.row == Box{4, 379, 196, 400},
              "a list the native layout places again gives up its bottom again");

        // The pane shrinks by the row's height and the native layout puts the
        // list back exactly where the last layout left it: the row, still
        // right under it, is now past the pane's bottom edge.
        const Box shrunk{0, 0, 200, 282};
        Check(LayOutSearchRow(first.list, first.list, first.row, shrunk, 21, 3, place)
                  && place.list == Box{4, 10, 196, 252} && place.row == Box{4, 255, 196, 276} && Inside(place.row, shrunk),
              "a list put back where it was left gives up its bottom again");
        const SearchRowPlace again = place;
        Check(!LayOutSearchRow(again.list, again.list, again.row, shrunk, 21, 3, place), "and then stays");

        // A row moved away from the list goes back under it; the list keeps its height.
        Check(LayOutSearchRow(first.list, first.list, {4, 100, 196, 121}, pane, 21, 3, place)
                  && place.list == first.list && place.row == first.row,
              "a row no longer under the list goes back");

        // A list the pane cannot hold is not shortened again and again.
        const Box tiny{0, 0, 200, 250};
        Check(LayOutSearchRow({4, 10, 196, 300}, first.list, first.row, tiny, 21, 3, place) && place.list.bottom == 276,
              "a list too long for the pane is laid out");
        const SearchRowPlace overflow = place;
        Check(!LayOutSearchRow(overflow.list, overflow.list, overflow.row, tiny, 21, 3, place), "but only once");

        // A short list keeps 20 pixels with the row under it, even past the pane.
        const Box low{0, 0, 200, 46};
        Check(LayOutSearchRow({4, 10, 196, 40}, first.list, first.row, low, 21, 3, place)
                  && place.list == Box{4, 10, 196, 30} && place.row == Box{4, 33, 196, 54},
              "the list keeps 20 pixels");
        const SearchRowPlace shortest = place;
        Check(!LayOutSearchRow(shortest.list, shortest.list, shortest.row, low, 21, 3, place), "and stays at that");

        // Sort keys round-trip, and an unknown key falls back to package order.
        for (Sort s : kSorts) Check(SortFromKey(SortKey(s)) == s && std::string(SortLabel(s)).rfind("Sort: ", 0) == 0, "sort key");
        Check(SortFromKey("newest") == Sort::Newest && SortFromKey("") == Sort::Package && SortFromKey("bogus") == Sort::Package, "sort fallback");

        std::cout << "MeshFavoritesModelTests: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "MeshFavoritesModelTests FAILED: " << e.what() << "\n";
        return 1;
    }
}
