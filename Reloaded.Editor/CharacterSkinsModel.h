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
constexpr int Version = 2;
constexpr const char* ClassName = "ReloadedCharacterSkins2";
// Earlier versions' classes, newest first, whose actors the panel migrates.
inline constexpr std::array<const char*, 1> LegacyClassNames = {{"ReloadedCharacterSkins"}};

struct Slot
{
    const char* property; // var() on the class
    const char* label;    // panel row
    const char* stock;    // stock heat layer whose heat and EMF masks are reused
    const char* texture;  // the stock diffuse in SPersoTextures, exported for painting over
    const char* format;   // its compression, used again for an imported replacement
};
// Slot 0 is the body and slot 1 the head on both stock character meshes
// (SPerso.ATT_01, SPerso.DEF_01); see the chains in SPersoTextures.
inline constexpr std::array<Slot, 4> Slots = {{
    {"SpyBody", "Spy body", "SPersoTextures.alpha_ATT02.HEAT_SN02", "Shadw_agent", "DXT3"},
    {"SpyHead", "Spy head", "SPersoTextures.alpha_ATT02.HEAT_SN02_2", "Shadw_agent_2", "DXT1"},
    {"MercBody", "Merc body", "SPersoTextures.alpha_DEF01.DEF01_heat_bodu", "DEF_01_A_Body", "DXT5"},
    {"MercHead", "Merc head", "SPersoTextures.alpha_DEF01.def01_heat_face", "DEF_01_A_Face", "DXT5"},
}};

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
        "var Material Stock[64];\r\n"
        "var byte Swapped[32];\r\n"
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
        "\t\tDress(P, i, 1, Dressed[First + 1]);\r\n"
        "\t\treturn;\r\n"
        "\t}\r\n"
        "\tif (Swapped[i] == 0)\r\n"
        "\t{\r\n"
        "\t\tSwapped[i] = 1;\r\n"
        "\t\tP.LinkMesh(Model);\r\n"
        "\t\tP.LinkSkelAnim(Moves);\r\n"
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
        "\tlocal int i, Free;\r\n"
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
        "\tStock[Free * 2] = Current(P, 0);\r\n"
        "\tStock[Free * 2 + 1] = Current(P, 1);\r\n"
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
        "\tif (Now == None || Now == Stock[Index * 2 + Slot])\r\n"
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
