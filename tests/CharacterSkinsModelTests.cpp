// The Character Skins script the editor compiles into a map, and the slot paths
// the panel accepts. The compile and the in-game result are verified natively.
#include "../Reloaded.Editor/CharacterSkinsModel.h"
#include "../Reloaded.Editor/CharacterSkinsImage.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace CharacterSkins;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

int main()
{
    try
    {
        const auto script = Script();
        // The stock compiler wants the class declaration first and CRLF text.
        Check(Contains(script, "class SCharacterSkins extends Info\r\n\tplaceable;"), "class header");
        Check(std::string(ClassName) == "SCharacterSkins" && Version == 6, "versioned class name");
        Check(std::string(LegacyClassNames[0]) == "ReloadedCharacterSkins5" && std::string(LegacyClassNames[4]) == "ReloadedCharacterSkins",
              "versions 1 to 5 are migrated");
        Check(script.find("\n") == script.find("\r\n") + 1, "CRLF line ends");
        for (const auto& slot : Slots)
        {
            Check(Contains(script, std::string("var() Material ") + slot.property + ";"), "slot variable");
            Check(Contains(script, std::string("HeatTextureModifier'") + slot.stock + "'"), "stock heat layer");
        }
        Check(Contains(script, "Dressed[3] = Wrap(MercHead"), "merc head wraps last");
        Check(Contains(script, "Dress(P, i, 1, Dressed[First + 1])"), "head slots");
        Check(Contains(script, "Outfit(P, SpyModel, MeshAnimation'SPerso.PRO', SpyGoggleOffset, 0)"), "spy outfit");
        Check(Contains(script, "Outfit(P, MercModel, MeshAnimation'SPerso.Def', MercGoggleOffset, 2)"), "merc outfit");
        // LinkMesh alone drops the animation set and stalls the pawn; relink the team's own.
        const auto link = script.find("P.LinkMesh(Model);");
        Check(link != std::string::npos && script.find("P.LinkSkelAnim(Moves);") > link, "animations relinked after the mesh");
        Check(Contains(script, "B.GoggleBeam.SetRelativeLocation(Goggles);"), "goggle lights moved");
        Check(Contains(script, "var() SkeletalMesh SpyModel;") && Contains(script, "var() vector MercGoggleOffset;"), "model variables");
        Check(Contains(script, "Swapped[Free] = 0;"), "a reused pawn slot swaps again");
        // Local per machine, never replicated; game effect skins are left alone.
        Check(Contains(script, "RemoteRole=ROLE_None") && Contains(script, "bNoDelete=True"), "local actor");
        Check(Contains(script, "Now == None || Now == Stock[Index * 3 + Slot]"), "effect skins left alone");
        // The merc's DEF_01 has its body on skins 0 and 2 (skin 2 is most of it); all three
        // stock skins are remembered.
        Check(Contains(script, "\t\tif (First == 2)\r\n\t\t\tDress(P, i, 2, Dressed[2]);\r\n"), "merc body dresses skin 2 too");
        Check(Contains(script, "var Material Stock[96];") && Contains(script, "Stock[Free * 3 + j] = Current(P, j);"), "three stock skins per pawn");
        Check(Contains(script, "Known[i].bDeleteMe"), "slots of gone pawns reused");
        Check(Contains(script, "Version=" + std::to_string(Version)), "version default");
        Check(!Contains(script, "replication"), "no replication block");
        Check(!Contains(script, "var bool Dressed") && !Contains(script, "var bool Swapped"), "no bool arrays: the compiler refuses them");

        // addbot and killbots: editor Play Level sessions only, read off the console by the
        // actor itself (an interaction of the map's crashed a merc's game with the console open).
        Check(Contains(script, "bCommands = InStr(Caps(Level.GetLocalURL()), \"?EDITEUR=TRUE\") >= 0;"), "editor launches only");
        Check(!Contains(script, "Interaction("), "no interaction");
        Check(Contains(script, "C.CmdHistoryTop != Entered") && Contains(script, "Typed = C.TypedStr;"), "console lines watched");
        Check(Contains(script, "BotClass = class'SPawnAttaque_1Mesh'") && Contains(script, "BotClass = class'SPawnDefense_1Mesh'"), "bot classes");
        Check(Contains(script, "Spawn(BotClass, , 'ReloadedTestBot',") && Contains(script, "DynamicActors(class'Pawn', P, 'ReloadedTestBot')"),
              "killbots removes only the tagged bots");
        Check(Contains(script, "GRI.PRIArray[i].PlayerName = \"Test bot\"") && Contains(script, "\tNameBots(PC);\r\n\tControlBots();\r\n\tC = Console("),
              "names kept: the Reloaded patch crashes on an empty one");
        // A merc's game crashes on a character without a controller; Controller is abstract.
        Check(Contains(script, "BotClass.default.Controller = Ctl;") && Contains(script, "BotClass.default.Controller = None;") &&
                  Contains(BotControllerScript(), "class ReloadedBotController extends Controller;\r\n"),
              "bots have a controller");
        Check(Contains(script, "Bot.AttachToBone(Gun, 'B R Hand');"), "a merc bot holds its gun");
        Check(Contains(script, "P.AttachToBone(SPAWNDEFENSE(P).WeaponDummy, 'B R Hand');"), "the gun follows a model swap");
        Check(Contains(script, "\tControlBots();"), "bots keep a controller");
        Check(Contains(script, "only the other team can be added"), "own team refused");

        Check(ValidPath(""), "empty keeps stock");
        Check(ValidPath("MyLevel.Snow.SpyCamo") && ValidPath("Arctic_TXT.SpyCamo"), "package paths");
        Check(!ValidPath("SpyCamo"), "needs a package");
        Check(!ValidPath("MyLevel..SpyCamo") && !ValidPath(".MyLevel.X") && !ValidPath("MyLevel.X."), "empty parts");
        Check(!ValidPath("MyLevel.X'") && !ValidPath("MyLevel.X Y") && !ValidPath("MyLevel.X\""), "no quotes or spaces");

        Check(PropertyText("Texture", "") == "None", "empty slot");
        Check(PropertyText("Texture", "MyLevel.Snow.SpyCamo") == "Texture'MyLevel.Snow.SpyCamo'", "texture reference");
        Check(PropertyText("Shader", "Arctic_TXT.Camo") == "Shader'Arctic_TXT.Camo'", "shader reference");
        bool threw = false;
        try { PropertyText("Texture", "Bad'Path"); } catch (const std::exception&) { threw = true; }
        Check(threw, "invalid path rejected");

        // DXT1 block: red and blue endpoints; index 0 for the first row, 1 below, then 2 and 3.
        Mip dxt1{Snapshot::kFormatDxt1, 4, 4, {0x00, 0xf8, 0x1f, 0x00, 0x00, 0x55, 0xaa, 0xff}};
        auto image = Decode(dxt1);
        Check(image.width == 4 && image.rgba.size() == 64, "DXT1 size");
        Check(image.rgba[0] == 255 && image.rgba[1] == 0 && image.rgba[2] == 0 && image.rgba[3] == 255, "DXT1 first colour");
        Check(image.rgba[16] == 0 && image.rgba[18] == 255, "DXT1 second colour");
        Check(image.rgba[32] == 170 && image.rgba[34] == 85, "DXT1 two-thirds colour");
        // c0 <= c1 makes index 3 transparent black.
        Mip punch{Snapshot::kFormatDxt1, 4, 4, {0x1f, 0x00, 0x00, 0xf8, 0xff, 0xff, 0xff, 0xff}};
        image = Decode(punch);
        Check(image.rgba[3] == 0 && image.rgba[0] == 0, "DXT1 transparent entry");
        // DXT5: alpha endpoints 255 and 0 with every index 1 (alpha 0); a white colour block.
        Mip dxt5{Snapshot::kFormatDxt5, 4, 4, {255, 0, 0x49, 0x92, 0x24, 0x49, 0x92, 0x24, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0}};
        image = Decode(dxt5);
        Check(image.rgba[0] == 255 && image.rgba[3] == 0 && image.rgba[63] == 0, "DXT5 alpha index 1");
        // DXT3: explicit 4-bit alpha, first pixel 0xf, second 0x0.
        Mip dxt3{Snapshot::kFormatDxt3, 4, 4, {0x0f, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0}};
        image = Decode(dxt3);
        Check(image.rgba[3] == 255 && image.rgba[7] == 0, "DXT3 explicit alpha");
        // RGBA8 is stored B G R A.
        Mip rgba{Snapshot::kFormatRgba8, 1, 1, {10, 20, 30, 40}};
        image = Decode(rgba);
        Check(image.rgba == std::vector<std::uint8_t>{30, 20, 10, 40}, "RGBA8 channel order");
        threw = false;
        try { Decode(Mip{0, 4, 4, Bytes(16)}); } catch (const std::exception&) { threw = true; }
        Check(threw, "palettized textures are refused");

        // TGA: 18-byte header, 32 bits with 8 alpha bits, bottom-up B G R A.
        Image two{1, 2, {1, 2, 3, 4, 5, 6, 7, 8}};
        auto tga = Tga(two);
        Check(tga.size() == 18 + 8 && tga[2] == 2 && tga[12] == 1 && tga[14] == 2 && tga[16] == 32 && tga[17] == 8, "TGA header");
        Check(tga[18] == 7 && tga[19] == 6 && tga[20] == 5 && tga[21] == 8 && tga[22] == 3 && tga[25] == 4, "TGA bottom-up BGRA");
        Check(Importable(".TGA") && Importable(".dds") && !Importable(".png") && !Importable(".psd"), "importable types");
        Check(std::string(Slots[0].texture) == "Shadw_agent" && std::string(Slots[3].format) == "DXT5", "stock textures");

        // Goggle light offsets.
        Check(OffsetText({4, 0, 0}) == "(X=4,Y=0,Z=0)" && OffsetText({-2.5, 1, 0.25}) == "(X=-2.5,Y=1,Z=0.25)", "offset text");
        threw = false;
        try { OffsetText({65, 0, 0}); } catch (const std::exception&) { threw = true; }
        Check(threw, "offset range");
        auto suggested = SuggestedGoggles("SpyModel", "SPerso.DEF_01");
        Check(suggested.x == 4 && suggested.y == 0 && suggested.z == 0, "merc model on a spy");
        suggested = SuggestedGoggles("MercModel", "SPerso.DEF_01");
        Check(suggested.x == 0 && suggested.y == 0 && suggested.z == 0, "no suggestion for a merc's own model");
        suggested = SuggestedGoggles("MercModel", "MyLevel.HazmatMerc");
        Check(suggested.x == -0.1 && suggested.y == -2.8 && suggested.z == -0.8, "hazmat suit visor");
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    std::cout << "CharacterSkinsModelTests: " << checks << " checks passed\n";
    return 0;
}
