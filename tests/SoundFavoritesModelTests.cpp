#include "../Reloaded.Editor/SoundFavoritesModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace SoundFavorites;
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
        auto p = Split("Amb_Club.Music.Loop01");
        Check(p.package == "Amb_Club" && p.group == "Music" && p.name == "Loop01", "three parts");
        p = Split("Pkg.Sub.Deeper.Sound");
        Check(p.package == "Pkg" && p.group == "Sub.Deeper" && p.name == "Sound", "nested groups");
        p = Split("Pkg.Sound");
        Check(p.package == "Pkg" && p.group.empty() && p.name == "Sound", "no group");
        p = Split("Lonely");
        Check(p.package == "Lonely" && p.name == "Lonely", "no dots");

        // Search: case-insensitive substring over the whole path, every word.
        Check(Matches("Amb_Club.Music.Loop01", "club"), "package part");
        Check(Matches("Amb_Club.Music.Loop01", "MUSIC.loop"), "across the dot");
        Check(Matches("Amb_Club.Music.Loop01", "loop amb"), "two words, any order");
        Check(!Matches("Amb_Club.Music.Loop01", "loop gare"), "every word must match");
        Check(!Matches("Amb_Club.Music.Loop01", "Loop02"), "no match");
        Check(Matches("Amb_Club.Music.Loop01", "") && Matches("Amb_Club.Music.Loop01", "   "), "empty matches all");
        Check(IsBlank("") && IsBlank(" \t ") && !IsBlank(" a "), "blank");

        // The saved list: duplicates and empty entries drop out, first spelling kept.
        Check(Dedupe({"A.B.C", "", "a.b.c", "D.E"}) == std::vector<std::string>{"A.B.C", "D.E"}, "dedupe");
        Check(Contains({"A.B.C"}, "a.B.c") && !Contains({"A.B.C"}, "A.B"), "contains");

        // Adding and removing merge with what is saved: another editor's
        // additions survive this one's, and repeating a change is harmless.
        const std::vector<std::string> mine = {"Amb_Club.Music.Loop01"};
        const std::vector<std::string> savedByOther = {"Amb_Club.Music.Loop01", "Amb_Gare.Train.Horn"};
        auto added = Add(savedByOther, "Amb_Dep.Door.Open", 4096);
        Check(added == std::vector<std::string>{"Amb_Club.Music.Loop01", "Amb_Gare.Train.Horn", "Amb_Dep.Door.Open"}, "add keeps the other's");
        Check(Add(added, "amb_dep.door.OPEN", 4096) == added, "add twice");
        Check(Add(mine, "X.Y", 1) == mine, "limit");
        auto removed = Remove(savedByOther, "AMB_CLUB.music.loop01");
        Check(removed == std::vector<std::string>{"Amb_Gare.Train.Horn"}, "remove keeps the other's");
        Check(Remove(removed, "Amb_Club.Music.Loop01") == removed, "remove twice");
        Check(Add({}, "", 10).empty(), "empty path not added");

        // Favourites in the order they were added.
        const std::vector<Favorite> favorites = {
            {"zeta_snd.Doors.DoorB", true},
            {"Alpha_Snd.Steps.step02", true},
            {"alpha_snd.Alarms.Siren", true},
            {"MyMap.MyLevel.Embedded", false},
            {"Zeta_Snd.Doors.DoorA", true},
            {"Alpha_Snd.Steps.Step01", false},
        };

        // Packages: one entry per package regardless of case, in name order.
        const auto packages = Packages(favorites);
        Check(packages.size() == 3, "three packages");
        Check(packages[0].name == "Alpha_Snd" && packages[0].count == 3 && packages[0].loaded == 2, "alpha counts");
        Check(packages[1].name == "MyMap" && packages[1].count == 1 && packages[1].loaded == 0, "map package");
        Check(packages[2].name == "zeta_snd" && packages[2].count == 2, "zeta keeps first spelling");
        Check(PackageLabel(packages[0]) == "Alpha_Snd (3)" && AllLabel(6) == "All packages (6)", "labels");

        // Package order: grouped by package, then group, then name; unloaded last.
        Check(Labels(FavoriteRows(favorites, "", Sort::Package, "")) == std::vector<std::string>{
                  "alpha_snd.Alarms.Siren", "Alpha_Snd.Steps.step02",
                  "Zeta_Snd.Doors.DoorA", "zeta_snd.Doors.DoorB",
                  "Alpha_Snd.Steps.Step01  [not loaded]", "MyMap.MyLevel.Embedded  [not loaded]"},
              "package order");

        // Name order leads with the sound name.
        Check(Labels(FavoriteRows(favorites, "", Sort::Name, "")) == std::vector<std::string>{
                  "DoorA  (Zeta_Snd.Doors)", "DoorB  (zeta_snd.Doors)",
                  "Siren  (alpha_snd.Alarms)", "step02  (Alpha_Snd.Steps)",
                  "Embedded  (MyMap.MyLevel)  [not loaded]", "Step01  (Alpha_Snd.Steps)  [not loaded]"},
              "name order");

        // Added order, both ways.
        auto newest = FavoriteRows(favorites, "", Sort::Newest, "");
        Check(newest.size() == 6 && newest[0].item == 4 && newest[1].item == 2 && newest[2].item == 1 && newest[3].item == 0
                  && newest[4].item == 5 && newest[5].item == 3, "newest first");
        auto oldest = FavoriteRows(favorites, "", Sort::Oldest, "");
        Check(oldest[0].item == 0 && oldest[1].item == 1 && oldest[2].item == 2 && oldest[3].item == 4, "oldest first");

        // A package filter, case-insensitive, and the search on top of it.
        auto alpha = FavoriteRows(favorites, "ALPHA_SND", Sort::Package, "");
        Check(alpha.size() == 3 && alpha[0].item == 2 && alpha[1].item == 1 && alpha[2].item == 5, "filter by package");
        Check(FavoriteRows(favorites, "Missing", Sort::Package, "").empty(), "filter with no matches");
        auto steps = FavoriteRows(favorites, "", Sort::Package, "STEP");
        Check(steps.size() == 2 && steps[0].item == 1 && steps[1].item == 5, "search favourites, unloaded included");
        Check(FavoriteRows(favorites, "zeta_snd", Sort::Package, "siren").empty(), "search within the package");

        // Search rows over loaded sounds, by package or by name.
        const std::vector<std::string> sounds = {
            "Amb_Gare.Train.Horn", "Amb_Club.Music.Loop01", "Amb_Club.Door", "Amb_Dep.Door.Open", "Amb_Dep.Door.Close"};
        Check(Labels(SearchRows(sounds, "", "door", Sort::Package)) == std::vector<std::string>{
                  "Amb_Club.Door", "Amb_Dep.Door.Close", "Amb_Dep.Door.Open"},
              "search by package");
        Check(Labels(SearchRows(sounds, "", "door", Sort::Name)) == std::vector<std::string>{
                  "Close  (Amb_Dep.Door)", "Door  (Amb_Club)", "Open  (Amb_Dep.Door)"},
              "search by name");
        Check(Labels(SearchRows(sounds, "", "door", Sort::Newest)) == Labels(SearchRows(sounds, "", "door", Sort::Package)),
              "no age for search rows");
        auto all = SearchRows(sounds, "", "", Sort::Package);
        Check(all.size() == 5 && sounds[all[0].item] == "Amb_Club.Door" && sounds[all[4].item] == "Amb_Gare.Train.Horn", "empty search lists all");
        Check(Labels(SearchRows(sounds, "AMB_DEP", "door", Sort::Package)) == std::vector<std::string>{
                  "Amb_Dep.Door.Close", "Amb_Dep.Door.Open"},
              "search within one package");
        const auto matching = Packages(Matching(sounds, "door"));
        Check(matching.size() == 2 && PackageLabel(matching[0]) == "Amb_Club (1)" && PackageLabel(matching[1]) == "Amb_Dep (2)",
              "search packages with counts");

        // Sort keys round-trip, and an unknown key falls back to package order.
        for (Sort s : kSorts) Check(SortFromKey(SortKey(s)) == s && std::string(SortLabel(s)).rfind("Sort: ", 0) == 0, "sort key");
        Check(SortFromKey("newest") == Sort::Newest && SortFromKey("") == Sort::Package && SortFromKey("bogus") == Sort::Package, "sort fallback");

        std::cout << "SoundFavoritesModelTests: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "SoundFavoritesModelTests FAILED: " << e.what() << "\n";
        return 1;
    }
}
