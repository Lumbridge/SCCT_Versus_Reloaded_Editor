#pragma once
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

// Character Skins: a map's own materials and models for spies and mercs. The editor
// compiles the script below into the map package (MyLevel) and places one actor of it.
// In game every machine runs its own copy of the actor (RemoteRole=ROLE_None,
// bNoDelete), so nothing is replicated and the game and Reloaded patch are untouched.
namespace CharacterSkins
{
// Raise with every change to Script(). Each version is its own class, so a map's
// older actor can be read and replaced instead of recompiling a class in use.
constexpr int Version = 5;
constexpr const char* ClassName = "ReloadedCharacterSkins5";
// Earlier versions' classes, newest first, whose actors the panel migrates.
inline constexpr std::array<const char*, 4> LegacyClassNames = {
    {"ReloadedCharacterSkins4", "ReloadedCharacterSkins3", "ReloadedCharacterSkins2", "ReloadedCharacterSkins"}};
// Tag of the characters the addbot console command stands in an editor Play Level.
constexpr const char* BotTag = "ReloadedTestBot";
// Their controller, compiled into the map beside the actor: Controller itself is
// abstract, and a merc's game crashes on a character without one. Rename it with
// any change.
constexpr const char* BotControllerClassName = "ReloadedBotController";
inline std::string BotControllerScript()
{
    return std::string("//=============================================================================\r\n"
                       "// ") + BotControllerClassName + ": the controller of an addbot character. It does nothing.\r\n"
                       "//=============================================================================\r\n"
                       "class " + BotControllerClassName + " extends Controller;\r\n";
}

struct Slot
{
    const char* property; // var() on the class
    const char* label;    // panel row
    const char* stock;    // stock heat layer whose heat and EMF masks are reused
    const char* texture;  // the stock diffuse in SPersoTextures, exported for painting over
    const char* format;   // its compression, used again for an imported replacement
    int section;          // the skin index (mesh material section) it dresses
    int alsoSection = -1; // a second section wearing the same material, or -1
};
// Skin 0 is the body and skin 1 the head on both stock character meshes
// (SPerso.ATT_01, SPerso.DEF_01); see the chains in SPersoTextures. The merc's
// DEF_01 has a third section, skin 2, with the body material again: it covers
// most of the body (1566 of 1942 faces; skin 0 only 238), so the merc body
// dresses both.
inline constexpr std::array<Slot, 4> Slots = {{
    {"SpyBody", "Spy body", "SPersoTextures.alpha_ATT02.HEAT_SN02", "Shadw_agent", "DXT3", 0},
    {"SpyHead", "Spy head", "SPersoTextures.alpha_ATT02.HEAT_SN02_2", "Shadw_agent_2", "DXT1", 1},
    {"MercBody", "Merc body", "SPersoTextures.alpha_DEF01.DEF01_heat_bodu", "DEF_01_A_Body", "DXT5", 0, 2},
    {"MercHead", "Merc head", "SPersoTextures.alpha_DEF01.def01_heat_face", "DEF_01_A_Face", "DXT5", 1},
}};
// The skins a pawn's stock look is remembered for: 0 to SkinCount - 1.
constexpr int SkinCount = 3;

// A team's replacement model. The pawn keeps its team's own animation set (LinkMesh
// alone drops it, and the pawn's moves, stance changes, gun and gadgets then stall),
// and its goggle lights (SGoggleBeam, attached to bone "B Head") move by an offset
// in that bone's axes so they sit on the new head.
struct ModelSlot
{
    const char* property;  // SkeletalMesh var()
    const char* goggles;   // vector var(): goggle light offset
    const char* label;
    const char* animation; // the team's stock MeshAnimation
    const char* stockMesh; // the team's own model, the other team's is offered too
};
inline constexpr std::array<ModelSlot, 2> Models = {{
    {"SpyModel", "SpyGoggleOffset", "Spy model", "SPerso.PRO", "SPerso.ATT_01"},
    {"MercModel", "MercGoggleOffset", "Merc model", "SPerso.Def", "SPerso.DEF_01"},
}};

struct Offset { double x = 0, y = 0, z = 0; };
// A goggle light offset that has been seen to fit; others start at zero. The merc
// model on a spy was matched by eye in game on ShipD (2026-10-04).
inline Offset SuggestedGoggles(const std::string& modelProperty, const std::string& mesh)
{
    if (modelProperty == "SpyModel" && mesh == "SPerso.DEF_01") return {4, 0, 0};
    // The hazmat suit (tools/models/hazmat): the lights sit on its visor.
    if (modelProperty == "MercModel" && mesh == "MyLevel.HazmatMerc") return {-0.2, -1.4, 0};
    return {};
}
inline std::string Number(double v)
{
    std::ostringstream out;
    out << std::setprecision(6) << v;
    return out.str();
}
inline Offset CheckOffset(const Offset& o)
{
    for (double v : {o.x, o.y, o.z})
        if (!std::isfinite(v) || std::fabs(v) > 64)
            throw std::runtime_error("Goggle light offsets must be numbers between -64 and 64.");
    return o;
}
inline std::string OffsetText(const Offset& o)
{
    CheckOffset(o);
    return "(X=" + Number(o.x) + ",Y=" + Number(o.y) + ",Z=" + Number(o.z) + ")";
}

// The UnrealScript compiled into the map. The stock skins are heat vision layer ->
// EMF vision layer -> diffuse; Wrap rebuilds both layers around the map's material
// so thermal and EMF vision still show the stock masks. A slot the game has given
// another material (wet, sticky cam, phospho) is left alone until the game clears it.
// A team with a model shows the model's own materials instead of the skin slots.
inline std::string Script()
{
    std::string s =
        "//=============================================================================\r\n"
        "// " + std::string(ClassName) + ": this map's spy and merc materials and models,\r\n"
        "// written by RE+ Tools > Character Skins. Each machine runs its own copy;\r\n"
        "// nothing replicates.\r\n"
        "//=============================================================================\r\n"
        "class " + ClassName + " extends Info\r\n"
        "\tplaceable;\r\n"
        "\r\n";
    for (const auto& slot : Slots)
        s += std::string("var() Material ") + slot.property + ";\r\n";
    for (const auto& model : Models)
        s += std::string("var() SkeletalMesh ") + model.property + ";\r\nvar() vector " + model.goggles + ";\r\n";
    s +=
        "var const int Version;\r\n"
        "var Material Dressed[4];\r\n"
        "var Pawn Known[32];\r\n"
        "var Material Stock[" + std::to_string(32 * SkinCount) + "];\r\n"
        "var byte Swapped[32];\r\n"
        "var bool bCommandsChecked, bCommands, bWatching;\r\n"
        "var string Typed;\r\n"
        "var int Entered;\r\n"
        "\r\n"
        "function PostBeginPlay()\r\n"
        "{\r\n";
    for (size_t i = 0; i < Slots.size(); ++i)
        s += "\tDressed[" + std::to_string(i) + "] = Wrap(" + Slots[i].property + ", HeatTextureModifier'" +
             Slots[i].stock + "');\r\n";
    s +=
        "}\r\n"
        "\r\n"
        "function Material Wrap(Material Base, HeatTextureModifier StockLayer)\r\n"
        "{\r\n"
        "\tlocal HeatTextureModifier Heat;\r\n"
        "\tlocal EMFTextureModifier EMF, LayerEMF;\r\n"
        "\r\n"
        "\tif (Base == None || StockLayer == None)\r\n"
        "\t\treturn Base;\r\n"
        "\tLayerEMF = EMFTextureModifier(StockLayer.Material);\r\n"
        "\tHeat = new class'HeatTextureModifier';\r\n"
        "\tHeat.HeatTexture = StockLayer.HeatTexture;\r\n"
        "\tHeat.bUseTextureAsHeat = StockLayer.bUseTextureAsHeat;\r\n"
        "\tHeat.HeatTextureType = StockLayer.HeatTextureType;\r\n"
        "\tHeat.Material = Base;\r\n"
        "\tif (LayerEMF != None)\r\n"
        "\t{\r\n"
        "\t\tEMF = new class'EMFTextureModifier';\r\n"
        "\t\tEMF.EMFTexture = LayerEMF.EMFTexture;\r\n"
        "\t\tEMF.EMFIsOn = LayerEMF.EMFIsOn;\r\n"
        "\t\tEMF.Material = Base;\r\n"
        "\t\tHeat.Material = EMF;\r\n"
        "\t}\r\n"
        "\treturn Heat;\r\n"
        "}\r\n"
        "\r\n"
        "function Tick(float DeltaTime)\r\n"
        "{\r\n"
        "\tlocal Pawn P;\r\n"
        "\r\n"
        "\tif (!bCommandsChecked)\r\n"
        "\t\tCheckCommands();\r\n"
        "\tif (bCommands)\r\n"
        "\t\tWatchConsole();\r\n"
        "\tforeach DynamicActors(class'Pawn', P)\r\n"
        "\t{\r\n"
        "\t\tif (P.IsA('SPawnAttaque') || P.IsA('SPawnAttaque_1Mesh'))\r\n"
        "\t\t\tOutfit(P, " + std::string(Models[0].property) + ", MeshAnimation'" + Models[0].animation + "', " +
        Models[0].goggles + ", 0);\r\n"
        "\t\telse if (P.IsA('SPAWNDEFENSE') || P.IsA('SPawnDefense_1Mesh'))\r\n"
        "\t\t\tOutfit(P, " + std::string(Models[1].property) + ", MeshAnimation'" + Models[1].animation + "', " +
        Models[1].goggles + ", 2);\r\n"
        "\t}\r\n"
        "}\r\n"
        "\r\n"
        "// The addbot and killbots console commands, in editor Play Level sessions only, for\r\n"
        "// looking at the other team. The console runs a typed line as a player command,\r\n"
        "// which never reaches a map's script, so the actor watches it: the line it held\r\n"
        "// when its command history moved is the one just entered.\r\n"
        "function CheckCommands()\r\n"
        "{\r\n"
        "\tlocal PlayerController PC;\r\n"
        "\r\n"
        "\tPC = Level.GetLocalPlayerController();\r\n"
        "\tif (PC == None || PC.Player == None || PC.Player.InteractionMaster == None)\r\n"
        "\t\treturn;\r\n"
        "\tbCommandsChecked = true;\r\n"
        "\tbCommands = InStr(Caps(Level.GetLocalURL()), \"?EDITEUR=TRUE\") >= 0;\r\n"
        "}\r\n"
        "\r\n"
        "function WatchConsole()\r\n"
        "{\r\n"
        "\tlocal PlayerController PC;\r\n"
        "\tlocal Console C;\r\n"
        "\tlocal string Line;\r\n"
        "\r\n"
        "\tPC = Level.GetLocalPlayerController();\r\n"
        "\tif (PC == None || PC.Player == None || PC.Player.InteractionMaster == None)\r\n"
        "\t\treturn;\r\n"
        "\tNameBots(PC);\r\n"
        "\tControlBots();\r\n"
        "\tC = Console(PC.Player.InteractionMaster.Console);\r\n"
        "\tif (C == None)\r\n"
        "\t\treturn;\r\n"
        "\tif (bWatching && C.CmdHistoryTop != Entered)\r\n"
        "\t{\r\n"
        "\t\tLine = Typed;\r\n"
        "\t\tif (Caps(Line) == \"ADDBOT\" || Left(Caps(Line), 7) == \"ADDBOT \")\r\n"
        "\t\t\tAddBot(PC, Mid(Line, 7));\r\n"
        "\t\telse if (Caps(Line) == \"KILLBOTS\")\r\n"
        "\t\t\tKillBots(PC);\r\n"
        "\t}\r\n"
        "\tbWatching = true;\r\n"
        "\tEntered = C.CmdHistoryTop;\r\n"
        "\tTyped = C.TypedStr;\r\n"
        "}\r\n"
        "\r\n"
        "// The Reloaded patch reads every name in the player list, and an empty one\r\n"
        "// crashes it: never leave one empty while bots are about.\r\n"
        "function NameBots(PlayerController PC)\r\n"
        "{\r\n"
        "\tlocal GameReplicationInfo GRI;\r\n"
        "\tlocal int i;\r\n"
        "\r\n"
        "\tGRI = PC.GameReplicationInfo;\r\n"
        "\tif (GRI == None)\r\n"
        "\t\treturn;\r\n"
        "\tfor (i = 0; i < GRI.PRIArray.Length; i++)\r\n"
        "\t\tif (GRI.PRIArray[i] != None && GRI.PRIArray[i].PlayerName == \"\")\r\n"
        "\t\t\tGRI.PRIArray[i].PlayerName = \"Test bot\";\r\n"
        "}\r\n"
        "\r\n"
        "// A merc's game crashes on a character without a controller: keep one on each bot.\r\n"
        "function ControlBots()\r\n"
        "{\r\n"
        "\tlocal Pawn P;\r\n"
        "\r\n"
        "\tforeach DynamicActors(class'Pawn', P, '" + std::string(BotTag) + "')\r\n"
        "\t\tif (P.Controller == None)\r\n"
        "\t\t{\r\n"
        "\t\t\tP.Controller = Spawn(class'" + std::string(BotControllerClassName) + "', , '" + std::string(BotTag) + "', P.Location);\r\n"
        "\t\t\tif (P.Controller != None)\r\n"
        "\t\t\t\tP.Controller.Pawn = P;\r\n"
        "\t\t}\r\n"
        "}\r\n"
        "\r\n"
        "// A character of the other team, with a do-nothing controller, standing in front of the\r\n"
        "// player, a little to the right, facing them; a merc holds its gun. The player's own team is refused.\r\n"
        "function AddBot(PlayerController PC, string Team)\r\n"
        "{\r\n"
        "\tlocal class<Pawn> BotClass;\r\n"
        "\tlocal Pawn Bot;\r\n"
        "\tlocal SWeaponDefenseDummy Gun;\r\n"
        "\tlocal Controller Ctl;\r\n"
        "\tlocal rotator Facing;\r\n"
        "\tlocal float Distance;\r\n"
        "\tlocal bool bMerc;\r\n"
        "\r\n"
        "\tif (PC.Pawn == None)\r\n"
        "\t{\r\n"
        "\t\tPC.ClientMessage(\"addbot: spawn into the map first.\");\r\n"
        "\t\treturn;\r\n"
        "\t}\r\n"
        "\tbMerc = SPAWNDEFENSE(PC.Pawn) == None;\r\n"
        "\tif ((Team ~= \"merc\" && !bMerc) || (Team ~= \"spy\" && bMerc))\r\n"
        "\t{\r\n"
        "\t\tPC.ClientMessage(\"addbot: only the other team can be added.\");\r\n"
        "\t\treturn;\r\n"
        "\t}\r\n"
        "\tif (bMerc)\r\n"
        "\t\tBotClass = class'SPawnDefense_1Mesh';\r\n"
        "\telse\r\n"
        "\t\tBotClass = class'SPawnAttaque_1Mesh';\r\n"
        "\tFacing = PC.Rotation;\r\n"
        "\tFacing.Pitch = 0;\r\n"
        "\tFacing.Roll = 0;\r\n"
        "\t// The game looks at a new character's controller while it is being spawned.\r\n"
        "\tCtl = Spawn(class'" + std::string(BotControllerClassName) + "', , '" + std::string(BotTag) + "', PC.Pawn.Location);\r\n"
        "\tif (Ctl == None)\r\n"
        "\t{\r\n"
        "\t\tPC.ClientMessage(\"addbot: the bot's controller could not be made.\");\r\n"
        "\t\treturn;\r\n"
        "\t}\r\n"
        "\tBotClass.default.Controller = Ctl;\r\n"
        "\tfor (Distance = 160; Distance >= 80 && Bot == None; Distance -= 40)\r\n"
        "\t\tBot = Spawn(BotClass, , '" + std::string(BotTag) + "', PC.Pawn.Location + vector(Facing) * Distance + vector(Facing + rot(0, 16384, 0)) * 60, Facing + rot(0, 32768, 0));\r\n"
        "\tBotClass.default.Controller = None;\r\n"
        "\tif (Bot == None)\r\n"
        "\t{\r\n"
        "\t\tCtl.Destroy();\r\n"
        "\t\tPC.ClientMessage(\"addbot: no room in front of you.\");\r\n"
        "\t\treturn;\r\n"
        "\t}\r\n"
        "\tCtl.Pawn = Bot;\r\n"
        "\tif (SPAWNDEFENSE(Bot) != None && SPAWNDEFENSE(Bot).WeaponDummy == None)\r\n"
        "\t{\r\n"
        "\t\tGun = Spawn(class'SWeaponDefenseDummy', Bot, '" + std::string(BotTag) + "');\r\n"
        "\t\tif (Gun != None)\r\n"
        "\t\t{\r\n"
        "\t\t\tSPAWNDEFENSE(Bot).WeaponDummy = Gun;\r\n"
        "\t\t\tBot.AttachToBone(Gun, 'B R Hand');\r\n"
        "\t\t}\r\n"
        "\t}\r\n"

        "\tNameBots(PC);\r\n"
        "\tPC.ClientMessage(\"addbot: added \" $ Bot.Class.Name $ \". killbots removes it.\");\r\n"
        "}\r\n"
        "\r\n"
        "function KillBots(PlayerController PC)\r\n"
        "{\r\n"
        "\tlocal Pawn P;\r\n"
        "\tlocal SWeaponDefenseDummy Gun;\r\n"
        "\tlocal int Count;\r\n"
        "\r\n"
        "\tforeach DynamicActors(class'SWeaponDefenseDummy', Gun, '" + std::string(BotTag) + "')\r\n"
        "\t\tGun.Destroy();\r\n"
        "\tforeach DynamicActors(class'Pawn', P, '" + std::string(BotTag) + "')\r\n"
        "\t{\r\n"
        "\t\tif (P.Controller != None && PlayerController(P.Controller) == None)\r\n"
        "\t\t\tP.Controller.Destroy();\r\n"
        "\t\tP.Destroy();\r\n"
        "\t\tCount++;\r\n"
        "\t}\r\n"
        "\tPC.ClientMessage(\"killbots: removed \" $ Count $ \".\");\r\n"
        "}\r\n"
        "\r\n"
        "// A team's model, keeping the team's own animations so its moves still play, with\r\n"
        "// the goggle lights moved onto the new head; or its materials on the stock model.\r\n"
        "function Outfit(Pawn P, SkeletalMesh Model, MeshAnimation Moves, vector Goggles, int First)\r\n"
        "{\r\n"
        "\tlocal int i;\r\n"
        "\tlocal SBasePawn B;\r\n"
        "\r\n"
        "\ti = PawnIndex(P);\r\n"
        "\tif (i < 0)\r\n"
        "\t\treturn;\r\n"
        "\tif (Model == None)\r\n"
        "\t{\r\n"
        "\t\tDress(P, i, 0, Dressed[First]);\r\n"
        "\t\tDress(P, i, 1, Dressed[First + 1]);\r\n";
    for (size_t k = 0; k < Slots.size(); ++k)
        if (Slots[k].alsoSection >= 0)
            s += "\t\tif (First == " + std::to_string(k / 2 * 2) + ")\r\n\t\t\tDress(P, i, " +
                 std::to_string(Slots[k].alsoSection) + ", Dressed[" + std::to_string(k) + "]);\r\n";
    s +=
        "\t\treturn;\r\n"
        "\t}\r\n"
        "\tif (Swapped[i] == 0)\r\n"
        "\t{\r\n"
        "\t\tSwapped[i] = 1;\r\n"
        "\t\tP.LinkMesh(Model);\r\n"
        "\t\tP.LinkSkelAnim(Moves);\r\n"
        "\t\t// The new mesh drops what hung off the old one's bones.\r\n"
        "\t\tif (SPAWNDEFENSE(P) != None && SPAWNDEFENSE(P).WeaponDummy != None)\r\n"
        "\t\t\tP.AttachToBone(SPAWNDEFENSE(P).WeaponDummy, 'B R Hand');\r\n"
        "\t}\r\n"
        "\tB = SBasePawn(P);\r\n"
        "\tif (B != None && B.GoggleBeam != None && B.GoggleBeam.RelativeLocation != Goggles)\r\n"
        "\t\tB.GoggleBeam.SetRelativeLocation(Goggles);\r\n"
        "}\r\n"
        "\r\n"
        "// A pawn's own skins, remembered the first time it is seen: the look the game\r\n"
        "// restores after an effect, so the map's material may replace it.\r\n"
        "function int PawnIndex(Pawn P)\r\n"
        "{\r\n"
        "\tlocal int i, j, Free;\r\n"
        "\r\n"
        "\tFree = -1;\r\n"
        "\tfor (i = 0; i < 32; i++)\r\n"
        "\t{\r\n"
        "\t\tif (Known[i] == P)\r\n"
        "\t\t\treturn i;\r\n"
        "\t\tif (Free < 0 && (Known[i] == None || Known[i].bDeleteMe))\r\n"
        "\t\t\tFree = i;\r\n"
        "\t}\r\n"
        "\tif (Free < 0)\r\n"
        "\t\treturn -1;\r\n"
        "\tKnown[Free] = P;\r\n"
        "\tSwapped[Free] = 0;\r\n"
        "\tfor (j = 0; j < " + std::to_string(SkinCount) + "; j++)\r\n"
        "\t\tStock[Free * " + std::to_string(SkinCount) + " + j] = Current(P, j);\r\n"
        "\treturn Free;\r\n"
        "}\r\n"
        "\r\n"
        "function Material Current(Pawn P, int Slot)\r\n"
        "{\r\n"
        "\tif (Slot < P.Skins.Length)\r\n"
        "\t\treturn P.Skins[Slot];\r\n"
        "\treturn None;\r\n"
        "}\r\n"
        "\r\n"
        "function Dress(Pawn P, int Index, int Slot, Material M)\r\n"
        "{\r\n"
        "\tlocal Material Now;\r\n"
        "\r\n"
        "\tif (M == None)\r\n"
        "\t\treturn;\r\n"
        "\tNow = Current(P, Slot);\r\n"
        "\tif (Now == None || Now == Stock[Index * " + std::to_string(SkinCount) + " + Slot])\r\n"
        "\t\tP.Skins[Slot] = M;\r\n"
        "}\r\n"
        "\r\n"
        "defaultproperties\r\n"
        "{\r\n"
        "\tbStatic=False\r\n"
        "\tbNoDelete=True\r\n"
        "\tbAlwaysTick=True\r\n"
        "\tRemoteRole=ROLE_None\r\n"
        "\tVersion=" + std::to_string(Version) + "\r\n"
        "}\r\n";
    return s;
}

// An object path the panel may write into a slot: Package[.Group].Name, made of the
// characters the engine allows in names. Empty means the stock material or model.
inline bool ValidPath(const std::string& path)
{
    if (path.empty()) return true;
    if (path.size() > 256 || path.front() == '.' || path.back() == '.' || path.find("..") != std::string::npos ||
        path.find('.') == std::string::npos)
        return false;
    for (unsigned char c : path)
        if (!std::isalnum(c) && c != '_' && c != '.' && c != '-') return false;
    return true;
}

// ObjectProperty import text for a slot: Class'Path', or None for the stock material.
inline std::string PropertyText(const std::string& className, const std::string& path)
{
    if (path.empty()) return "None";
    if (!ValidPath(path) || !ValidPath(className + ".x")) throw std::runtime_error("Invalid object path: " + path);
    return className + "'" + path + "'";
}
} // namespace CharacterSkins
