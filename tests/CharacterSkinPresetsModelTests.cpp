// Character Skin presets: the built-in camouflage recipes, the painting over a stock
// picture, the user library file, and the shared preset file. Applying a preset in
// the editor (import and compile) is verified natively.
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
    return {{"name", "Mine"}, {"category", "snow & arctic"}, {"description", "  my  skins "},
            {"slots", {{"SpyBody", {{"image", "SpyBody.tga"}}}, {"MercHead", {{"path", "Arctic_TXT.Merc.Head"}}}}},
            {"models", {{"SpyModel", "SPerso.DEF_01"}}}, {"goggles", {{"SpyGoggleOffset", {4, 0, 0}}}}};
}

int main()
{
    try
    {
        // Built-ins: the five requested themes and some fun ones, all valid, all
        // painted from recipes so a fresh install needs nothing but the DLL.
        const auto& builtins = Builtins();
        Check(builtins.size() >= 10, "built-in presets");
        std::set<std::string> categories, ids;
        for (const auto& entry : builtins)
        {
            Validate(entry);
            Check(IsBuiltinId(entry.at("id")) && entry.at("readonly").get<bool>(), "built-in ids are read-only");
            Check(ids.insert(entry.at("id")).second, "built-in ids are unique");
            categories.insert(entry.at("category"));
            Check(entry.at("slots").size() == 4, "every built-in dresses all four slots");
            Check(ImageSlots(entry).empty(), "built-ins carry no picture files");
            for (auto& [slot, value] : entry.at("slots").items()) ReadRecipe(value.at("recipe"));
        }
        for (const char* category : {"Woodland & Jungle", "Desert", "Snow & Arctic", "Urban & Night", "Fun"})
            Check(categories.count(category) == 1, "every default theme has presets");
        for (const char* name : {"builtin.woodland", "builtin.desert", "builtin.snow", "builtin.urban_night", "builtin.jungle_tiger_stripe"})
            Check(ids.count(name) == 1, "requested themes");

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
        const auto small = Shrink(stock, 16, 16);
        Check(small.width == 16 && small.rgba.size() == 16 * 16 * 4, "shrunk");
        Check(small.rgba[0] == 120 && small.rgba[1] == 230, "a uniform block averages to itself");

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
        auto saved = Save(file, UserEntry());
        Check(IsUserId(saved.at("id")) && !saved.at("modified").get<std::string>().empty(), "saved with a user id");
        Check(saved.at("category") == "Snow & Arctic" && saved.at("description") == "my skins", "texts cleaned");
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
        bad["slots"]["MercHead"] = {{"path", "MyLevel.CharacterSkins.SpyBody"}};
        Check(Throws([&] { Json f = EmptyDocument(); Save(f, bad); }), "map textures must be pictures");
        bad = {{"name", "Empty"}, {"category", "Other"}, {"slots", Json::object()}};
        Check(Throws([&] { Json f = EmptyDocument(); Save(f, bad); }), "an empty preset is refused");
        bad = UserEntry();
        bad["goggles"]["SpyGoggleOffset"] = {100, 0, 0};
        Check(Throws([&] { Json f = EmptyDocument(); Save(f, bad); }), "goggle range");
        // A damaged entry stays in the file but is not listed.
        auto damaged = file;
        damaged["presets"].push_back({{"id", "0123456789abcdef0123456789abcdef"}, {"name", "Broken"}});
        Check(Merge(Builtins(), damaged).size() == merged.size() && Problems(damaged).size() == 1, "damaged entry reported");
        Check(Throws([] { Document({{"version", 2}}); }) && Throws([] { Document({{"version", 1}, {"presets", 3}}); }), "damaged file refused");

        auto updated = Update(file, saved.at("id"), {{"name", "Renamed"}, {"category", "Fun"}});
        Check(updated.at("name") == "Renamed" && updated.at("category") == "Fun", "renamed");
        Check(Throws([&] { Update(file, "builtin.snow", {{"name", "x"}}); }), "built-ins never change");
        Check(Throws([&] { Update(file, saved.at("id"), {{"slots", "x"}}); }), "only texts change");
        Check(!Delete(file, Builtins(), "builtin.snow"), "a built-in is hidden");
        Check(Merge(Builtins(), file).size() == Builtins().size(), "hidden built-in not listed");
        RestoreBuiltins(file);
        Check(Merge(Builtins(), file).size() == Builtins().size() + 1, "restored");
        Check(Delete(file, Builtins(), saved.at("id")) && file.at("presets").empty(), "a user preset is deleted");
        Check(Throws([&] { Delete(file, Builtins(), saved.at("id")); }), "gone");

        // Capture from the Character Skins window's values.
        const Json slots = {{"SpyBody", "MyLevel.CharacterSkins.SpyBody"}, {"SpyHead", ""}, {"MercBody", "Arctic_TXT.Merc.Body"}, {"MercHead", ""}};
        const Json models = {{"SpyModel", ""}, {"MercModel", "SPerso.DEF_01"}};
        const Json goggles = {{"SpyGoggleOffset", {0, 0, 0}}, {"MercGoggleOffset", {1.5, 0, 0}}};
        auto captured = Capture(slots, models, goggles, {"SpyBody"});
        Check(captured.at("slots").at("SpyBody") == Json{{"image", "SpyBody.tga"}} && captured.at("slots").at("MercBody") == Json{{"path", "Arctic_TXT.Merc.Body"}}, "slots captured");
        Check(!captured.at("slots").contains("SpyHead") && captured.at("models").at("MercModel") == "SPerso.DEF_01", "stock left out");
        Check(!captured.at("goggles").contains("SpyGoggleOffset") && captured.at("goggles").at("MercGoggleOffset")[0] == 1.5, "zero offsets left out");
        Check(Throws([] { Capture({{"SpyBody", ""}}, Json::object(), Json::object(), {}); }), "nothing to save");
        Json f2 = EmptyDocument();
        Save(f2, captured);

        // Sharing: pictures travel inside the file and are checked on the way back.
        auto entry = Save(file, UserEntry());
        entry["builtin"] = false;
        entry["readonly"] = false;
        Check(Throws([&] { ShareDocument(entry, {}); }), "a missing picture refuses");
        const auto shared = ShareDocument(entry, {{"SpyBody", tga}});
        Check(shared.at("format") == ShareFormat && !shared.at("preset").contains("id") && !shared.at("preset").contains("readonly"), "identity left out");
        auto read = ReadShared(Json::parse(shared.dump()));
        Check(read.images.at("SpyBody") == tga && read.entry.at("name") == "Mine", "read back");
        Json f3 = EmptyDocument();
        auto imported = Save(f3, read.entry);
        Check(IsUserId(imported.at("id")) && imported.at("id") != entry.at("id"), "imported under a new id");
        auto broken = shared;
        broken["images"]["SpyBody"] = Base64(Bytes(tga.begin(), tga.begin() + 30));
        Check(Throws([&] { ReadShared(broken); }), "damaged picture refused");
        broken = shared;
        broken["format"] = "something else";
        Check(Throws([&] { ReadShared(broken); }), "other files refused");
        broken = shared;
        broken["version"] = 2;
        Check(Throws([&] { ReadShared(broken); }), "newer files refused");
        // A built-in shared as it is keeps its recipes, which the editor paints on import.
        Json builtin = Builtins()[0];
        auto sharedBuiltin = ShareDocument(builtin, {});
        Check(ReadShared(sharedBuiltin).entry.at("slots").at("SpyBody").contains("recipe"), "built-in shares its recipes");
        Check(ShareFileName("Snow: \"Arctic\"/2?") == "Snow Arctic2.skinpreset" && ShareFileName("...") == "Skin preset.skinpreset", "file names");

        Check(Contains(Summary(Builtins()[0]), "Spy body: camouflage patches") && Contains(Summary(saved), "Spy model: SPerso.DEF_01 (goggle lights 4, 0, 0)"), "summary");
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
