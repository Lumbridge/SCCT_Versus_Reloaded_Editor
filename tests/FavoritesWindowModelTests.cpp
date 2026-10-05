#include "../Reloaded.Editor/FavoritesWindowModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace FavoritesWindow::Model;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
template <class F> bool Throws(F&& f)
{
    try { f(); }
    catch (const std::runtime_error&) { return true; }
    return false;
}
std::vector<std::string> Names(const std::vector<Row>& rows)
{
    std::vector<std::string> out;
    for (const auto& r : rows) out.push_back(r.name);
    return out;
}

int main()
{
    try
    {
        // The browsers' sections read back as the browsers read them.
        Lines section{{"Count", "4"}, {"Favorite0", "Pkg.A"}, {"favorite1", "pkg.a"}, {"Favorite2", ""}, {"Favorite3", "Pkg.B"}, {"Favorite4", "Pkg.Extra"}};
        Check(ParseList(section) == std::vector<std::string>{"Pkg.A", "Pkg.B"}, "count, repeats and blanks");
        Check(ParseList({}).empty() && ParseList({{"Count", "junk"}}).empty(), "no list");
        Check(ListLines({"X.Y", "Z.W"}) == Lines{{"Count", "2"}, {"Favorite0", "X.Y"}, {"Favorite1", "Z.W"}}, "list lines");
        Check(ParseList(ListLines({"X.Y", "Z.W"})) == std::vector<std::string>{"X.Y", "Z.W"}, "round trip");
        Check(std::string(ListSection(Kind::Material)) == "TextureBrowserFavorites" &&
                  std::string(ListSection(Kind::Mesh)) == "StaticMeshBrowserFavorites" &&
                  std::string(ListSection(Kind::Sound)) == "SoundBrowserFavorites",
              "the browsers' own sections");
        Check(KindFromKey("mesh") == Kind::Mesh && !KindFromKey("Texture"), "kind keys");

        // Tag names.
        Check(CleanTagName("  Ship   interior ") == "Ship interior", "spaces");
        Check(CleanTagName("a|b=c\"d[e]f;g") == "a b c d e f g", "unsafe characters");
        Check(CleanTagName(" \t ").empty(), "blank");
        Check(CleanTagName(std::string(100, 'x')).size() == kMaxTagName, "long names cut");

        // Creating tags.
        Lines tagLines;
        Tags tags = ParseTags(tagLines);
        auto writes = CreateTag(tags, " Lights ");
        Check(writes == Writes{{"Tag:Lights", std::string("1")}}, "create writes one key");
        tagLines = ApplyWrites(tagLines, writes);
        tagLines = ApplyWrites(tagLines, CreateTag(ParseTags(tagLines), "Ship interior"));
        tags = ParseTags(tagLines);
        Check(tags.names == std::vector<std::string>{"Lights", "Ship interior"}, "two tags");
        Check(Throws([&] { CreateTag(tags, "lights"); }), "names compared without case");
        Check(Throws([&] { CreateTag(tags, " | "); }), "empty name refused");

        // Adding favourites to tags.
        const Item lamp{Kind::Mesh, "Props.Lamps.Lamp01"}, glow{Kind::Material, "FX.Glow"}, hum{Kind::Sound, "Amb.Hum"};
        writes = AddToTag(tags, {lamp, glow}, "lights");
        Check(writes.size() == 2 && writes[0] == Write{"Mesh:Props.Lamps.Lamp01", std::string("Lights")}, "add writes each favourite's key");
        tagLines = ApplyWrites(tagLines, writes);
        tags = ParseTags(tagLines);
        Check(TagsOf(tags, Kind::Mesh, "props.lamps.LAMP01") == std::vector<std::string>{"Lights"}, "paths without case");
        Check(AddToTag(tags, {lamp}, "Lights").empty(), "adding again changes nothing");
        tagLines = ApplyWrites(tagLines, AddToTag(tags, {lamp, hum}, "Ship interior"));
        tags = ParseTags(tagLines);
        Check(TagsOf(tags, Kind::Mesh, lamp.path) == std::vector<std::string>{"Lights", "Ship interior"}, "several tags");
        Check(TagsOf(tags, Kind::Sound, hum.path) == std::vector<std::string>{"Ship interior"}, "one tag");
        Check(TagsOf(tags, Kind::Material, lamp.path).empty(), "the kind is part of the key");
        Check(Throws([&] { AddToTag(tags, {lamp}, "Nope"); }), "unknown tag");

        // Two editors: each writes only its own keys, so even an editor that
        // read the tags before the other one saved keeps the other's change.
        const Tags before = ParseTags(tagLines);
        const Item pipe{Kind::Mesh, "Props.Pipes.Pipe"};
        tagLines = ApplyWrites(tagLines, AddToTag(before, {pipe}, "Lights"));     // editor A
        tagLines = ApplyWrites(tagLines, RemoveFromTag(before, {hum}, "Ship interior")); // editor B, stale
        tags = ParseTags(tagLines);
        Check(TagsOf(tags, Kind::Mesh, pipe.path) == std::vector<std::string>{"Lights"}, "A's change kept");
        Check(TagsOf(tags, Kind::Sound, hum.path).empty() && !Find(tagLines, "Sound:Amb.Hum"), "B's change kept, key deleted");
        tagLines = ApplyWrites(tagLines, CreateTag(before, "Doors")); // B again, stale read
        Check(ParseTags(tagLines).names.size() == 3 && TagsOf(ParseTags(tagLines), Kind::Mesh, pipe.path).size() == 1, "new tag alongside");

        // Renaming rewrites the tag key and each member's key, nothing else.
        tags = ParseTags(tagLines);
        writes = RenameTag(tags, "lights", "Lamps");
        Check(writes.front() == Write{"Tag:Lights", std::nullopt} && writes[1] == Write{"Tag:Lamps", std::string("1")}, "tag key renamed");
        Check(writes.size() == 2 + 3, "three members rewritten");
        tagLines = ApplyWrites(tagLines, writes);
        tags = ParseTags(tagLines);
        Check(TagsOf(tags, Kind::Mesh, lamp.path) == std::vector<std::string>{"Ship interior", "Lamps"}, "renamed member, creation order");
        Check(Throws([&] { RenameTag(tags, "Lamps", "doors"); }), "rename onto another tag");
        Check(RenameTag(tags, "Lamps", "Lamps").empty(), "same name");
        tagLines = ApplyWrites(tagLines, RenameTag(tags, "Lamps", "LAMPS"));
        Check(ParseTags(tagLines).Has("lamps") && TagsOf(ParseTags(tagLines), Kind::Material, glow.path) == std::vector<std::string>{"LAMPS"},
              "case-only rename");
        Check(Throws([&] { RenameTag(ParseTags(tagLines), "Gone", "X"); }), "renaming a deleted tag");

        // Deleting drops the tag from its members; a member left with none loses its key.
        tags = ParseTags(tagLines);
        writes = DeleteTag(tags, "lamps");
        tagLines = ApplyWrites(tagLines, writes);
        tags = ParseTags(tagLines);
        Check(!tags.Has("Lamps"), "tag gone");
        Check(!Find(tagLines, "Material:FX.Glow") && !Find(tagLines, "Mesh:Props.Pipes.Pipe"), "empty members deleted");
        Check(TagsOf(tags, Kind::Mesh, lamp.path) == std::vector<std::string>{"Ship interior"}, "other tags kept");

        // A member naming a tag that no longer exists ignores it.
        Lines stray{{"Tag:Kept", "1"}, {"Sound:A.B", "Kept|Gone"}, {"Junk", "x"}, {"Texture:A.B", "Kept"}};
        tags = ParseTags(stray);
        Check(TagsOf(tags, Kind::Sound, "A.B") == std::vector<std::string>{"Kept"}, "stray tag ignored");
        Check(tags.members.size() == 1, "unknown keys ignored");
        Check(AddToTag(tags, {{Kind::Sound, "A.B"}}, "Kept").empty(), "already tagged");
        tags.names.push_back("New");
        Check(AddToTag(tags, {{Kind::Sound, "a.b"}}, "New") == Writes{{"Sound:A.B", std::string("Kept|New")}}, "stray dropped on rewrite, key spelling kept");
        Check(ForgetItems(tags, {{Kind::Sound, "A.B"}, {Kind::Mesh, "X.Y"}}) == Writes{{"Sound:A.B", std::nullopt}}, "forget only tagged");

        // Entries and rows.
        ListsByKind lists{std::vector<std::string>{"FX.Glow", "Walls.Brick.Red"},
                          std::vector<std::string>{"Props.Lamps.Lamp01", "MapPkg.MyLevel.Thing"},
                          std::vector<std::string>{"Amb.Hum", "amb.Alarm"}};
        Lines t{{"Tag:Lights", "1"}, {"Tag:Ship interior", "1"}, {"Material:FX.Glow", "Lights"}, {"Mesh:Props.Lamps.Lamp01", "Lights|Ship interior"},
                {"Sound:Amb.Hum", "Ship interior"}};
        tags = ParseTags(t);
        const auto entries = Entries(lists, tags, [](Kind, const std::string& path) { return path.find("MyLevel") == std::string::npos; });
        Check(entries.size() == 6 && entries[3].order == 1 && !entries[3].loaded, "entries");
        Check(entries[2].tags == std::vector<std::string>{"Lights", "Ship interior"}, "entry tags");

        Filter filter;
        auto rows = Rows(entries, filter);
        Check(Names(rows) == std::vector<std::string>{"Alarm", "Hum", "Glow", "Lamp01", "Red", "Thing  [not loaded]"}, "package order, unloaded last");
        Check(rows[3].type == "Static mesh" && rows[3].location == "Props.Lamps" && rows[3].tags == "Lights, Ship interior", "row columns");
        filter.sort = Sort::Name;
        Check(Names(Rows(entries, filter)) == std::vector<std::string>{"Alarm", "Glow", "Hum", "Lamp01", "Red", "Thing  [not loaded]"}, "name order");
        filter.sort = Sort::Newest;
        Check(Names(Rows(entries, filter)) == std::vector<std::string>{"Red", "Glow", "Lamp01", "Alarm", "Hum", "Thing  [not loaded]"}, "newest per type");
        filter.sort = Sort::Oldest;
        Check(Names(Rows(entries, filter)).front() == "Glow", "oldest");

        filter = {};
        filter.kind = Kind::Sound;
        Check(Names(Rows(entries, filter)) == std::vector<std::string>{"Alarm", "Hum"}, "type filter");
        filter.tag = {TagFilter::Mode::Tag, "ship INTERIOR"};
        Check(Names(Rows(entries, filter)) == std::vector<std::string>{"Hum"}, "tag filter");
        filter.kind.reset();
        Check(Names(Rows(entries, filter)) == std::vector<std::string>{"Hum", "Lamp01"}, "tag across types");
        filter.tag = {TagFilter::Mode::Untagged, {}};
        Check(Names(Rows(entries, filter)) == std::vector<std::string>{"Alarm", "Red", "Thing  [not loaded]"}, "untagged");
        filter.tag = {};
        filter.package = "AMB";
        Check(Names(Rows(entries, filter)) == std::vector<std::string>{"Alarm", "Hum"}, "package filter");
        filter.package.clear();
        filter.query = "lights lamp";
        Check(Names(Rows(entries, filter)) == std::vector<std::string>{"Lamp01"}, "search covers tags");
        filter.query = "sound";
        Check(Rows(entries, filter).size() == 2, "search covers the type");
        filter.query = "  ";
        Check(Rows(entries, filter).size() == 6, "blank search");

        // The search's extra text (the English for French names).
        SoundFavorites::SearchText() = [](const std::string& path) { return path == "amb.Alarm" ? path + " siren" : path; };
        filter.query = "siren";
        Check(Names(Rows(entries, filter)) == std::vector<std::string>{"Alarm"}, "search text hook");
        SoundFavorites::SearchText() = nullptr;

        // Packages for the package filter follow the other filters.
        filter = {};
        size_t total = 0;
        auto packages = Packages(entries, filter, &total);
        Check(total == 6 && packages.size() == 5 && packages[0].name == "Amb" && packages[0].count == 2, "all packages");
        filter.kind = Kind::Mesh;
        packages = Packages(entries, filter, &total);
        Check(total == 2 && packages.size() == 2 && packages[0].name == "MapPkg" && packages[0].loaded == 0, "mesh packages");

        // The tag list counts the chosen type.
        auto tagRows = TagRows(entries, tags, std::nullopt);
        Check(tagRows.size() == 4 && tagRows[0].label == "All favourites (6)" && tagRows[1].label == "Untagged (3)", "tag rows");
        Check(tagRows[2].label == "Lights (2)" && tagRows[3].label == "Ship interior (2)" && tagRows[3].filter.name == "Ship interior", "tag counts");
        tagRows = TagRows(entries, tags, Kind::Sound);
        Check(tagRows[0].label == "All favourites (2)" && tagRows[2].label == "Lights (0)", "counts per type");

        // Packages to load for unloaded favourites, once per type and package.
        std::vector<Entry> missing{{Kind::Material, "Tex.A", false}, {Kind::Material, "tex.B", false}, {Kind::Mesh, "Tex.C", false},
                                   {Kind::Sound, "Snd.A", true}};
        auto loads = MissingPackages(missing);
        Check(loads == std::vector<PackageLoad>{{Kind::Material, "Tex"}, {Kind::Mesh, "Tex"}}, "missing packages");
        Check(MissingPackages(missing, {AttemptKey(loads[0])}) == std::vector<PackageLoad>{{Kind::Mesh, "Tex"}}, "attempted skipped");

        Check(Summary(entries, 4) == "4 of 6 favourites shown: 2 materials, 2 static meshes, 2 sounds; 1 not loaded.", "summary");

        // Removing a favourite starts from the saved list (the browsers' merge).
        std::vector<std::string> saved{"A.B", "C.D"};
        Check(SoundFavorites::Remove(saved, "a.b") == std::vector<std::string>{"C.D"}, "remove merges");

        std::cout << "FavoritesWindowModelTests: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FavoritesWindowModelTests FAILED: " << error.what() << "\n";
        return 1;
    }
}
