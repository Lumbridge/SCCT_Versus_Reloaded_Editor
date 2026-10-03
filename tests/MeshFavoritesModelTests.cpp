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
