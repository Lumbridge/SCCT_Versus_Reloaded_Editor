// Character Skin presets: the built-in camouflage recipes, the painting over a stock
// picture, team presets (a spy preset never touches the mercs), the user library
// file and its migration from both-team presets, and the shared preset file.
// Applying a preset in the editor (import and compile) is verified natively.
#include "../Reloaded.Editor/CharacterSkinPresetsModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace CharacterSkins;
using namespace CharacterSkins::Presets;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
template <class F> bool Throws(F f)
{
    try { f(); }
    catch (const std::exception&) { return true; }
    return false;
}
bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// A grey stock picture with a lit green lamp in the top-left corner and a skin patch.
Image Stock(int side)
{
    Image image{side, side, std::vector<std::uint8_t>(static_cast<size_t>(side) * side * 4)};
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
        {
            auto* p = &image.rgba[(static_cast<size_t>(y) * side + x) * 4];
            const std::uint8_t grey = static_cast<std::uint8_t>(20 + (x + y) % 20);
            p[0] = p[1] = p[2] = grey;
            p[3] = 200;
            if (x < 4 && y < 4) { p[0] = 120; p[1] = 230; p[2] = 30; }       // lamp
            if (x >= side - 4 && y < 4) { p[0] = 200; p[1] = 150; p[2] = 120; } // skin
        }
    return image;
}
Json UserEntry()
{
    return {{"team", "spy"}, {"name", "Mine"}, {"category", "snow & arctic"}, {"description", "  my  skins "},
            {"slots", {{"SpyBody", {{"image", "SpyBody.tga"}}}, {"SpyHead", {{"path", "Arctic_TXT.Spy.Head"}}}}},
            {"models", {{"SpyModel", "SPerso.DEF_01"}}}, {"goggles", {{"SpyGoggleOffset", {4, 0, 0}}}}};
}
// A version 1 entry: one preset dressing both teams.
Json TwoTeamEntry()
{
    return {{"id", "0123456789abcdef0123456789abcdef"}, {"name", "Old pair"}, {"category", "Desert"}, {"description", "both"}, {"modified", "1700000000000"},
            {"slots", {{"SpyBody", {{"image", "SpyBody.tga"}}}, {"SpyHead", {{"path", "Sand_TXT.Spy.Head"}}}, {"MercBody", {{"image", "MercBody.tga"}}}, {"MercHead", {{"image", "MercHead.tga"}}}}},
            {"models", {{"SpyModel", ""}, {"MercModel", "MyLevel.HazmatMerc"}}}, {"goggles", {{"SpyGoggleOffset", {4, 0, 0}}, {"MercGoggleOffset", {-0.2, -1.4, 0}}}}};
}

int main()
{
    try
    {
        // Built-ins: the five requested themes and some fun ones, all valid, all
        // painted from recipes so a fresh install needs nothing but the DLL. Every
        // look comes as a spy preset and a merc preset.
        const auto& builtins = Builtins();
        Check(builtins.size() >= 20, "built-in presets");
        std::set<std::string> categories, ids;
        size_t spies = 0, mercs = 0;
        for (const auto& entry : builtins)
        {
            Validate(entry);
            Check(IsBuiltinId(entry.at("id")) && entry.at("readonly").get<bool>(), "built-in ids are read-only");
            Check(ids.insert(entry.at("id")).second, "built-in ids are unique");
            categories.insert(entry.at("category"));
            const auto& team = TeamOf(entry);
            ++(std::string(team.id) == "spy" ? spies : mercs);
            Check(entry.at("slots").size() == 2 && entry.at("slots").contains(team.body) && entry.at("slots").contains(team.head), "every built-in dresses its team's two slots");
            Check(ImageSlots(entry).empty(), "built-ins carry no picture files");
            for (auto& [slot, value] : entry.at("slots").items()) ReadRecipe(value.at("recipe"));
        }
        Check(spies == mercs, "every look for both teams");
        for (const char* category : {"Woodland & Jungle", "Desert", "Snow & Arctic", "Urban & Night", "Fun"})
            Check(categories.count(category) == 1, "every default theme has presets");
        for (const char* name : {"woodland", "desert", "snow", "urban_night", "jungle_tiger_stripe", "team_red", "team_blue"})
            Check(ids.count(std::string("builtin.spy.") + name) == 1 && ids.count(std::string("builtin.merc.") + name) == 1, "requested themes for both teams");
        // Team rules: an entry holds only its own team's values.
        Check(FindTeam("spy") == &Teams[0] && FindTeam("merc") == &Teams[1] && !FindTeam("both"), "teams");
        Check(TeamOfProperty("MercGoggleOffset") == &Teams[1] && TeamOfProperty("SpyHead") == &Teams[0] && !TeamOfProperty("Cape"), "team of a property");
        Check(Throws([] { auto e = Builtins()[0]; e["slots"]["MercBody"] = e.at("slots").at("SpyBody"); Validate(e); }), "a spy preset with a merc slot is refused");
        Check(Throws([] { auto e = Builtins()[0]; e["models"] = {{"MercModel", "SPerso.ATT_01"}}; Validate(e); }), "a spy preset with a merc model is refused");
        Check(Throws([] { auto e = Builtins()[0]; e.erase("team"); Validate(e); }), "a preset without a team is refused");
        Check(Throws([] { auto e = Builtins()[0]; e["team"] = "merc"; Validate(e); }), "a built-in id names its team");
        Check(ReplacedBuiltin("builtin.desert") == std::vector<std::string>{"builtin.spy.desert", "builtin.merc.desert"} && ReplacedBuiltin("builtin.spy.desert").empty(),
              "old built-in ids");

        // Recipes.
        Check(Throws([] { ParseColour("#12345"); }) && Throws([] { ParseColour("#12345g"); }) && Throws([] { ParseColour("123456 "); }), "bad colours refused");
        const auto red = ParseColour("#FF8000");
        Check(red.r == 1 && std::abs(red.g - 128 / 255.0) < 1e-9 && red.b == 0, "colour parsed");
        Check(Throws([] { ReadRecipe({{"pattern", "plaid"}, {"colours", {"#000000"}}}); }), "unknown pattern");
        Check(Throws([] { ReadRecipe({{"pattern", "blotch"}, {"colours", Json::array()}}); }), "no colours");
        Check(Throws([] { ReadRecipe({{"pattern", "blotch"}, {"colours", {"#000000"}}, {"scale", 0}}); }), "scale range");
        auto recipe = ReadRecipe({{"pattern", "blotch"}, {"colours", {"#102030", "#405060", "#708090", "#a0b0c0"}}, {"scale", 40}, {"seed", 7}});
        std::set<size_t> used;
        for (int y = 0; y < 512; y += 4)
            for (int x = 0; x < 512; x += 4) used.insert(PatternIndex(recipe, x, y));
        Check(used.size() == 4, "every colour of a blotch pattern shows");
        Check(PatternIndex(recipe, 100.5, 37.25) == PatternIndex(recipe, 100.5, 37.25), "patterns are deterministic");
        auto digital = recipe;
        digital.pattern = "digital";
        digital.cell = 8;
        Check(PatternIndex(digital, 16.1, 16.1) == PatternIndex(digital, 23.9, 23.9), "digital squares are whole cells");
        auto stripes = ReadRecipe({{"pattern", "stripes"}, {"colours", {"#000000", "#ffffff"}}, {"scale", 10}});
        Check(PatternIndex(stripes, 1, 1) == 0 && PatternIndex(stripes, 11, 1) == 1 && PatternIndex(stripes, -1, -1) == 1, "stripes alternate");
        auto solid = ReadRecipe({{"pattern", "solid"}, {"colours", {"#d8aa3c", "#000000"}}});
        Check(PatternIndex(solid, 300, 200) == 0, "solid uses its first colour");

        // Painting: lit parts and skin kept, cloth recoloured with its shading, alpha kept.
        const auto stock = Stock(64);
        const auto painted = Recolour(stock, solid);
        Check(painted.width == 64 && painted.rgba.size() == stock.rgba.size(), "same size");
        Check(painted.rgba[0] == 120 && painted.rgba[1] == 230 && painted.rgba[2] == 30, "a lit lamp is kept");
        const size_t skin = (63) * 4;
        Check(painted.rgba[skin] == 200 && painted.rgba[skin + 1] == 150, "skin is kept");
        const size_t cloth = (static_cast<size_t>(32) * 64 + 32) * 4;
        Check(painted.rgba[cloth] > painted.rgba[cloth + 2] && painted.rgba[cloth + 1] > painted.rgba[cloth + 2], "cloth takes the gold");
        Check(painted.rgba[cloth + 3] == 200, "alpha kept");
        // A darker stock texel stays darker: the folds show through.
        const size_t dark = (static_cast<size_t>(10) * 64 + 10) * 4, light = (static_cast<size_t>(10) * 64 + 29) * 4; // grey 20 and 39
        Check(painted.rgba[dark] < painted.rgba[light], "stock shading shows");
        Check(Throws([&] { Recolour(Image{}, solid); }), "empty stock refused");
        // Thumbnails paint the same pattern on a shrunk picture.
        const auto shrunk = Shrink(stock, 16, 16);
        Check(shrunk.width == 16 && shrunk.rgba.size() == 16 * 16 * 4, "shrunk");
        Check(shrunk.rgba[0] == 120 && shrunk.rgba[1] == 230, "a uniform block averages to itself");

        // TGA round trip and import names.
        const auto tga = Tga(painted);
        const auto back = ReadTga(tga);
        Check(back.width == 64 && back.rgba == painted.rgba, "TGA round trip");
        Bytes rgb(18 + 2 * 2 * 3, 0);
        rgb[2] = 2; rgb[12] = 2; rgb[14] = 2; rgb[16] = 24; rgb[17] = 0x20; // top-down
        rgb[18] = 1; rgb[19] = 2; rgb[20] = 3;
        const auto three = ReadTga(rgb);
        Check(three.rgba[0] == 3 && three.rgba[1] == 2 && three.rgba[2] == 1 && three.rgba[3] == 255, "24-bit top-down TGA");
        auto odd = rgb;
        odd[12] = 3;
        Check(Throws([&] { ReadTga(odd); }), "sides must be powers of two");
        auto rle = rgb;
        rle[2] = 10;
        Check(Throws([&] { ReadTga(rle); }), "compressed TGA refused");
        Check(Throws([&] { ReadTga(Bytes(tga.begin(), tga.begin() + 40)); }), "cut short");
        const auto name = ImportName("SpyBody", tga);
        Check(name.size() == std::string("SpyBody_").size() + 8 && name.rfind("SpyBody_", 0) == 0, "import name");
        Check(ValidPath("MyLevel.CharacterSkins." + name), "import names are valid object names");
        Check(name == ImportName("SpyBody", Tga(back)), "the same picture reuses its texture");
        Check(name != ImportName("SpyBody", Tga(Recolour(stock, stripes))), "another picture gets another texture");
        Check(Throws([&] { ImportName("Cape", tga); }), "unknown slot");

        // Base64.
        for (const std::string text : {"", "a", "ab", "abc", "abcd", "hello world"})
        {
            Bytes data(text.begin(), text.end());
            Check(FromBase64(Base64(data)) == data, "base64 round trip");
        }
        Check(Base64(Bytes{'M', 'a', 'n'}) == "TWFu" && Base64(Bytes{'M'}) == "TQ==", "base64 text");
        Check(Throws([] { FromBase64("abc"); }) && Throws([] { FromBase64("ab!d"); }), "damaged base64");

        // The user file.
        Json file = EmptyDocument();
        Check(file.at("version") == 2 && file.at("imports").is_object(), "a new file is version 2");
        auto saved = Save(file, UserEntry());
        Check(IsUserId(saved.at("id")) && !saved.at("modified").get<std::string>().empty(), "saved with a user id");
        Check(saved.at("category") == "Snow & Arctic" && saved.at("description") == "my skins" && saved.at("team") == "spy", "texts cleaned, team kept");
        Check(!saved.contains("builtin") && file.at("presets").size() == 1, "stored without flags");
        auto merged = Merge(Builtins(), file);
        Check(merged.size() == Builtins().size() + 1 && merged.back().at("readonly") == false, "built-ins then yours");
        Check(Find(merged, saved.at("id")).at("name") == "Mine", "found by id");
        Check(Throws([&] { Find(merged, "nope"); }), "unknown id");
        // A recipe in a user entry, a built-in id, an unknown slot or nothing to change is refused.
        auto bad = UserEntry();
        bad["slots"]["SpyHead"] = {{"recipe", Builtins()[0].at("slots").at("SpyHead").at("recipe")}};
        Check(Throws([&] { Json f = EmptyDocument(); Save(f, bad); }), "user entries hold no recipes");
        bad = UserEntry();
        bad["slots"]["Cape"] = {{"path", "A.B"}};
        Check(Throws([&] { Json f = EmptyDocument(); Save(f, bad); }), "unknown slot refused");
        bad = UserEntry();
        bad["slots"]["SpyBody"] = {{"image", "../../evil.tga"}};
        Check(Throws([&] { Json f = EmptyDocument(); Save(f, bad); }), "image names are fixed");
        bad = UserEntry();
        bad["slots"]["SpyHead"] = {{"path", "MyLevel.CharacterSkins.SpyBody"}};
        Check(Throws([&] { Json f = EmptyDocument(); Save(f, bad); }), "map textures must be pictures");
        bad = {{"team", "spy"}, {"name", "Empty"}, {"category", "Other"}, {"slots", Json::object()}};
        Check(Throws([&] { Json f = EmptyDocument(); Save(f, bad); }), "an empty preset is refused");
        bad = UserEntry();
        bad["goggles"]["SpyGoggleOffset"] = {100, 0, 0};
        Check(Throws([&] { Json f = EmptyDocument(); Save(f, bad); }), "goggle range");
        bad = UserEntry();
        bad["slots"]["MercHead"] = {{"path", "Arctic_TXT.Merc.Head"}};
        Check(Throws([&] { Json f = EmptyDocument(); Save(f, bad); }), "both teams in one user entry refused");
        bad = {{"team", "merc"}, {"name", "Lights"}, {"category", "Other"}, {"slots", Json::object()}, {"goggles", {{"MercGoggleOffset", {1, 0, 0}}}}};
        Check(!Throws([&] { Json f = EmptyDocument(); Save(f, bad); }), "a goggle offset alone is a change");
        // A damaged entry stays in the file but is not listed.
        auto damaged = file;
        damaged["presets"].push_back({{"id", "0123456789abcdef0123456789abcdef"}, {"team", "spy"}, {"name", "Broken"}});
        Check(Merge(Builtins(), damaged).size() == merged.size() && Problems(damaged).size() == 1, "damaged entry reported");
        Check(Throws([] { Document({{"version", 3}}); }) && Throws([] { Document({{"version", 2}, {"presets", 3}}); }), "damaged file refused");

        auto updated = Update(file, saved.at("id"), {{"name", "Renamed"}, {"category", "Fun"}});
        Check(updated.at("name") == "Renamed" && updated.at("category") == "Fun" && updated.at("team") == "spy", "renamed");
        Check(Throws([&] { Update(file, "builtin.spy.snow", {{"name", "x"}}); }), "built-ins never change");
        Check(Throws([&] { Update(file, saved.at("id"), {{"slots", "x"}}); }), "only texts change");
        Check(Throws([&] { Update(file, saved.at("id"), {{"team", "merc"}}); }), "the team never changes");
        Check(!Delete(file, Builtins(), "builtin.spy.snow"), "a built-in is hidden");
        auto afterHide = Merge(Builtins(), file);
        Check(afterHide.size() == Builtins().size(), "hidden built-in not listed");
        Check(std::any_of(afterHide.begin(), afterHide.end(), [](const Json& e) { return e.at("id") == "builtin.merc.snow"; }), "hiding the spy preset keeps the merc one");
        RestoreBuiltins(file);
        Check(Merge(Builtins(), file).size() == Builtins().size() + 1, "restored");
        Check(Delete(file, Builtins(), saved.at("id")) && file.at("presets").empty(), "a user preset is deleted");
        Check(Throws([&] { Delete(file, Builtins(), saved.at("id")); }), "gone");

        // Capture one team from the Character Skins window's values.
        const Json slots = {{"SpyBody", "MyLevel.CharacterSkins.SpyBody"}, {"SpyHead", ""}, {"MercBody", "Arctic_TXT.Merc.Body"}, {"MercHead", ""}};
        const Json models = {{"SpyModel", ""}, {"MercModel", "SPerso.DEF_01"}};
        const Json goggles = {{"SpyGoggleOffset", {0, 0, 0}}, {"MercGoggleOffset", {1.5, 0, 0}}};
        auto captured = Capture(Teams[0], slots, models, goggles, {"SpyBody"});
        Check(captured.at("team") == "spy" && captured.at("slots") == Json{{"SpyBody", {{"image", "SpyBody.tga"}}}}, "spy slots captured, mercs left out");
        Check(captured.at("models").empty() && captured.at("goggles").empty(), "stock spy model and zero offset left out");
        auto capturedMerc = Capture(Teams[1], slots, models, goggles, {"SpyBody"});
        Check(capturedMerc.at("team") == "merc" && capturedMerc.at("slots") == Json{{"MercBody", {{"path", "Arctic_TXT.Merc.Body"}}}}, "merc slots captured");
        Check(capturedMerc.at("models").at("MercModel") == "SPerso.DEF_01" && capturedMerc.at("goggles").at("MercGoggleOffset")[0] == 1.5, "merc model and offset captured");
        Check(Throws([] { Capture(Teams[0], {{"SpyBody", ""}, {"MercBody", "A.B"}}, Json::object(), Json::object(), {}); }), "nothing of that team to save");
        Json f2 = EmptyDocument();
        Save(f2, captured);
        const auto savedMerc = Save(f2, capturedMerc);

        // Dressing one team leaves the other exactly as the map has it.
        const Json map = {{"slots", {{"SpyBody", "ShipD.CharacterSkins.SpyBody_1"}, {"SpyHead", ""}, {"MercBody", "ShipD.CharacterSkins.MercBody_aa"}, {"MercHead", "Snow_TXT.Merc.Head"}}},
                          {"models", {{"SpyModel", ""}, {"MercModel", "MyLevel.HazmatMerc"}}},
                          {"goggles", {{"SpyGoggleOffset", {0, 0, 0}}, {"MercGoggleOffset", {-0.2, -1.4, 0}}}}};
        const auto& desertSpy = Find(Builtins(), "builtin.spy.desert");
        auto dressed = Dress(desertSpy, map, {{"SpyBody", "ShipD.CharacterSkins.SpyBody_d1"}, {"SpyHead", "ShipD.CharacterSkins.SpyHead_d2"}});
        Check(dressed.at("slots").at("SpyBody") == "ShipD.CharacterSkins.SpyBody_d1" && dressed.at("slots").at("SpyHead") == "ShipD.CharacterSkins.SpyHead_d2", "spies dressed");
        for (const char* key : {"MercBody", "MercHead"}) Check(dressed.at("slots").at(key) == map.at("slots").at(key), "merc slots untouched");
        Check(dressed.at("models").at("MercModel") == "MyLevel.HazmatMerc" && dressed.at("goggles").at("MercGoggleOffset") == map.at("goggles").at("MercGoggleOffset"), "merc model and offset untouched");
        Check(dressed.at("models").at("SpyModel") == "" && dressed.at("goggles").at("SpyGoggleOffset") == Json::array({0, 0, 0}), "spy model back to stock");
        const auto& snowMerc = Find(Builtins(), "builtin.merc.snow");
        auto both = Dress(snowMerc, dressed, {{"MercBody", "ShipD.CharacterSkins.MercBody_s1"}, {"MercHead", "ShipD.CharacterSkins.MercHead_s2"}});
        Check(both.at("slots").at("SpyBody") == "ShipD.CharacterSkins.SpyBody_d1" && both.at("slots").at("MercHead") == "ShipD.CharacterSkins.MercHead_s2", "desert spies, snow mercs");
        Check(both.at("models").at("MercModel") == "" && both.at("goggles").at("MercGoggleOffset") == Json::array({0, 0, 0}), "a merc preset without a model puts the stock merc back");
        Check(both.at("slots").at("SpyHead") == dressed.at("slots").at("SpyHead") && both.at("models").at("SpyModel") == dressed.at("models").at("SpyModel"), "spies untouched by the merc preset");
        Check(Throws([&] { Dress(desertSpy, map, {{"SpyBody", "A.B"}}); }), "a slot without a material refuses");
        auto userMerc = Find(Merge(Builtins(), f2), savedMerc.at("id"));
        auto withModel = Dress(userMerc, map, {{"MercBody", "Arctic_TXT.Merc.Body"}});
        Check(withModel.at("slots").at("MercHead") == "" && withModel.at("models").at("MercModel") == "SPerso.DEF_01" && withModel.at("goggles").at("MercGoggleOffset")[0] == 1.5,
              "a merc preset sets every merc value");
        Check(withModel.at("slots").at("SpyBody") == map.at("slots").at("SpyBody"), "and no spy value");

        // What each team wears, in words.
        Json imports = {{"SpyBody_d1", "builtin.spy.desert"}, {"SpyHead_d2", "builtin.spy.desert"}, {"MercBody_s1", "builtin.merc.snow"}, {"MercHead_s2", "builtin.merc.snow"}};
        Check(Wearing(Teams[0], both, Builtins(), imports) == "Desert" && Wearing(Teams[1], both, Builtins(), imports) == "Snow", "mixed teams named");
        Check(Wearing(Teams[1], both, Builtins(), Json::object()) == "their own skins", "unknown textures");
        Check(Wearing(Teams[0], Json{{"slots", Json::object()}, {"models", {{"SpyModel", "SPerso.ATT_01"}}}}, Builtins(), imports) == "the stock look", "stock");
        Check(Wearing(Teams[1], withModel, Merge(Builtins(), f2), imports) == "New preset", "a path preset matches by path");
        Check(ObjectName("ShipD.CharacterSkins.SpyBody_d1") == "SpyBody_d1", "object name");
        RecordImports(f2, {{"SpyBody_d1", "builtin.spy.desert"}});
        Check(f2.at("imports").at("SpyBody_d1") == "builtin.spy.desert", "imports recorded");

        // Migration: a version 1 file's both-team presets become a spy and a merc preset.
        Json mercOnly = {{"id", "fedcba9876543210fedcba9876543210"}, {"name", "Merc only"}, {"category", "Other"}, {"modified", "1"},
                         {"slots", {{"MercBody", {{"path", "Snow_TXT.Merc.Body"}}}}}};
        Json v1 = {{"version", 1}, {"presets", {TwoTeamEntry(), mercOnly, {{"id", "broken"}}}}, {"hiddenBuiltins", {"builtin.snow", "builtin.red_vs_blue"}}};
        auto migration = Migrate(v1);
        Check(migration.changed && migration.document.at("version") == 2, "migrated to version 2");
        const auto& presets = migration.document.at("presets");
        Check(presets.size() == 4, "two parts, one merc-only preset and the damaged entry kept");
        Check(presets[0].at("id") == TwoTeamEntry().at("id") && presets[0].at("team") == "spy", "the spy part keeps the old id and folder");
        Check(presets[0].at("slots").size() == 2 && presets[0].at("slots").contains("SpyHead") && !presets[0].contains("models") &&
              presets[0].at("goggles") == Json{{"SpyGoggleOffset", {4, 0, 0}}}, "spy values kept");
        Check(presets[1].at("team") == "merc" && IsUserId(presets[1].at("id")) && presets[1].at("id") != presets[0].at("id"), "the merc part gets its own id");
        Check(presets[1].at("models").at("MercModel") == "MyLevel.HazmatMerc" && presets[1].at("goggles").at("MercGoggleOffset")[1] == -1.4 && presets[1].at("slots").size() == 2,
              "merc values kept");
        for (const auto& part : {presets[0], presets[1]})
            Check(part.at("name") == "Old pair" && part.at("category") == "Desert" && part.at("description") == "both" && part.at("modified") == "1700000000000", "texts and date kept");
        Check(migration.copies.size() == 2 && Json(migration.copies[0].from) == TwoTeamEntry().at("id") && Json(migration.copies[0].to) == presets[1].at("id") &&
              migration.copies[0].property == "MercBody" && migration.copies[1].property == "MercHead", "merc pictures copied to the merc folder");
        Check(presets[2].at("id") == "fedcba9876543210fedcba9876543210" && presets[2].at("team") == "merc", "a one-team preset keeps its id");
        Check(presets[3] == Json{{"id", "broken"}}, "a damaged entry is kept as it was");
        Check(migration.document.at("hiddenBuiltins") == Json({"builtin.spy.snow", "builtin.merc.snow", "builtin.spy.team_red", "builtin.merc.team_blue"}), "hidden built-ins follow");
        Check(Migrate(v1).document == migration.document, "migration is repeatable");
        Check(!Migrate(migration.document).changed, "a version 2 file is left alone");
        auto listed = Merge(Builtins(), v1);
        Check(listed.size() == Builtins().size() - 4 + 3, "an old file lists its split presets");
        Check(Problems(v1).size() == 1, "only the damaged entry is a problem");

        // Sharing: pictures travel inside the file and are checked on the way back.
        auto entry = Save(file, UserEntry());
        entry["builtin"] = false;
        entry["readonly"] = false;
        Check(Throws([&] { ShareDocument(entry, {}); }), "a missing picture refuses");
        const auto shared = ShareDocument(entry, {{"SpyBody", tga}});
        Check(shared.at("format") == ShareFormat && shared.at("version") == 2 && !shared.at("preset").contains("id") && !shared.at("preset").contains("readonly"), "identity left out");
        auto reads = ReadShared(Json::parse(shared.dump()));
        Check(reads.size() == 1 && reads[0].entry.at("team") == "spy", "one team preset per file");
        auto read = reads[0];
        Check(read.images.at("SpyBody") == tga && read.entry.at("name") == "Mine", "read back");
        Json f3 = EmptyDocument();
        auto imported = Save(f3, read.entry);
        Check(IsUserId(imported.at("id")) && imported.at("id") != entry.at("id"), "imported under a new id");
        // A version 1 file (both teams) reads back as a spy and a merc preset, pictures split.
        auto old = TwoTeamEntry();
        old.erase("id");
        old.erase("modified");
        Json oldFile = {{"version", 1}, {"format", ShareFormat}, {"preset", old}, {"images", {{"SpyBody", Base64(tga)}, {"MercBody", Base64(tga)}, {"MercHead", Base64(tga)}}}};
        auto parts = ReadShared(oldFile);
        Check(parts.size() == 2 && parts[0].entry.at("team") == "spy" && parts[1].entry.at("team") == "merc", "an old file gives two presets");
        Check(parts[0].images.size() == 1 && parts[1].images.size() == 2 && parts[1].images.count("MercHead"), "pictures go with their team");
        Check(parts[1].entry.at("models").at("MercModel") == "MyLevel.HazmatMerc" && parts[0].entry.at("name") == "Old pair", "values and names kept");
        for (auto& part : parts) Save(f3, part.entry);
        oldFile["images"].erase("MercHead");
        Check(Throws([&] { ReadShared(oldFile); }), "a missing picture in an old file refuses");
        auto mixed = shared;
        mixed["preset"]["slots"]["MercBody"] = {{"path", "A.B"}};
        Check(Throws([&] { ReadShared(mixed); }), "a version 2 file with both teams refuses");
        auto broken = shared;
        broken["images"]["SpyBody"] = Base64(Bytes(tga.begin(), tga.begin() + 30));
        Check(Throws([&] { ReadShared(broken); }), "damaged picture refused");
        broken = shared;
        broken["format"] = "something else";
        Check(Throws([&] { ReadShared(broken); }), "other files refused");
        broken = shared;
        broken["version"] = 3;
        Check(Throws([&] { ReadShared(broken); }), "newer files refused");
        // A built-in shared as it is keeps its recipes, which the editor paints on import.
        Json builtin = Builtins()[0];
        auto sharedBuiltin = ShareDocument(builtin, {});
        Check(ReadShared(sharedBuiltin)[0].entry.at("slots").at("SpyBody").contains("recipe"), "built-in shares its recipes");
        Check(ShareFileName("Snow: \"Arctic\"/2?") == "Snow Arctic2.skinpreset" && ShareFileName("...") == "Skin preset.skinpreset", "file names");

        Check(Contains(Summary(Builtins()[0]), "Spy body: camouflage patches") && !Contains(Summary(Builtins()[0]), "Merc") &&
              Contains(Summary(saved), "Spy model: SPerso.DEF_01 (goggle lights 4, 0, 0)"), "summary shows the preset's team only");
        Check(Contains(Summary(parts[1].entry), "Merc model: MyLevel.HazmatMerc (goggle lights -0.2, -1.4, 0)"), "summary model");
        Check(Contains(Summary(Json{{"team", "merc"}, {"goggles", {{"MercGoggleOffset", {1, 0, 0}}}}}), "Merc model: stock (goggle lights 1, 0, 0)"), "an offset without a model shows");
        auto cats = Categories(Merge(Builtins(), f2));
        Check(cats.front() == "Woodland & Jungle" && cats.back() == "Other", "categories");
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    std::cout << "CharacterSkinPresetsModelTests: " << checks << " checks passed\n";
    return 0;
}
