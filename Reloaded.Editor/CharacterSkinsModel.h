#pragma once
#include <array>
#include <cctype>
#include <stdexcept>
#include <string>

// Character Skins: a map's own materials for spies and mercs. The editor compiles
// ReloadedCharacterSkins into the map package (MyLevel) and places one actor of it.
// In game every machine runs its own copy of the actor (RemoteRole=ROLE_None,
// bNoDelete), so nothing is replicated and the game and Reloaded patch are untouched.
namespace CharacterSkins
{
constexpr const char* ClassName = "ReloadedCharacterSkins";
// Raise with every change to Script(); the panel recompiles maps that carry an older one.
constexpr int Version = 1;

struct Slot
{
    const char* property; // var() on ReloadedCharacterSkins
    const char* label;    // panel row
    const char* stock;    // stock heat layer whose heat and EMF masks are reused
};
// Slot 0 is the body and slot 1 the head on both stock character meshes
// (SPerso.ATT_01, SPerso.DEF_01); see the chains in SPersoTextures.
inline constexpr std::array<Slot, 4> Slots = {{
    {"SpyBody", "Spy body", "SPersoTextures.alpha_ATT02.HEAT_SN02"},
    {"SpyHead", "Spy head", "SPersoTextures.alpha_ATT02.HEAT_SN02_2"},
    {"MercBody", "Merc body", "SPersoTextures.alpha_DEF01.DEF01_heat_bodu"},
    {"MercHead", "Merc head", "SPersoTextures.alpha_DEF01.def01_heat_face"},
}};

// The UnrealScript compiled into the map. The stock skins are heat vision layer ->
// EMF vision layer -> diffuse; Wrap rebuilds both layers around the map's material
// so thermal and EMF vision still show the stock masks. A slot the game has given
// another material (wet, sticky cam, phospho) is left alone until the game clears it.
inline std::string Script()
{
    std::string s =
        "//=============================================================================\r\n"
        "// ReloadedCharacterSkins: this map's spy and merc materials, written by\r\n"
        "// RE+ Tools > Character Skins. Each machine runs its own copy; nothing replicates.\r\n"
        "//=============================================================================\r\n"
        "class ReloadedCharacterSkins extends Info\r\n"
        "\tplaceable;\r\n"
        "\r\n";
    for (const auto& slot : Slots)
        s += std::string("var() Material ") + slot.property + ";\r\n";
    s +=
        "var const int Version;\r\n"
        "var Material Dressed[4];\r\n"
        "var Pawn Known[32];\r\n"
        "var Material Stock[64];\r\n"
        "\r\n"
        "function PostBeginPlay()\r\n"
        "{\r\n";
    for (size_t i = 0; i < Slots.size(); ++i)
        s += "\tDressed[" + std::to_string(i) + "] = Wrap(" + Slots[i].property + ", HeatTextureModifier'" +
             Slots[i].stock + "');\r\n";
    s +=
        "}\r\n"
        "\r\n"
        "function Material Wrap(Material Base, HeatTextureModifier Stock)\r\n"
        "{\r\n"
        "\tlocal HeatTextureModifier Heat;\r\n"
        "\tlocal EMFTextureModifier EMF, StockEMF;\r\n"
        "\r\n"
        "\tif (Base == None || Stock == None)\r\n"
        "\t\treturn Base;\r\n"
        "\tStockEMF = EMFTextureModifier(Stock.Material);\r\n"
        "\tHeat = new class'HeatTextureModifier';\r\n"
        "\tHeat.HeatTexture = Stock.HeatTexture;\r\n"
        "\tHeat.bUseTextureAsHeat = Stock.bUseTextureAsHeat;\r\n"
        "\tHeat.HeatTextureType = Stock.HeatTextureType;\r\n"
        "\tHeat.Material = Base;\r\n"
        "\tif (StockEMF != None)\r\n"
        "\t{\r\n"
        "\t\tEMF = new class'EMFTextureModifier';\r\n"
        "\t\tEMF.EMFTexture = StockEMF.EMFTexture;\r\n"
        "\t\tEMF.EMFIsOn = StockEMF.EMFIsOn;\r\n"
        "\t\tEMF.Material = Base;\r\n"
        "\t\tHeat.Material = EMF;\r\n"
        "\t}\r\n"
        "\treturn Heat;\r\n"
        "}\r\n"
        "\r\n"
        "function Tick(float DeltaTime)\r\n"
        "{\r\n"
        "\tlocal Pawn P;\r\n"
        "\tlocal int i;\r\n"
        "\r\n"
        "\tforeach DynamicActors(class'Pawn', P)\r\n"
        "\t{\r\n"
        "\t\tif (P.IsA('SPawnAttaque') || P.IsA('SPawnAttaque_1Mesh'))\r\n"
        "\t\t{\r\n"
        "\t\t\ti = PawnIndex(P);\r\n"
        "\t\t\tDress(P, i, 0, Dressed[0]);\r\n"
        "\t\t\tDress(P, i, 1, Dressed[1]);\r\n"
        "\t\t}\r\n"
        "\t\telse if (P.IsA('SPAWNDEFENSE') || P.IsA('SPawnDefense_1Mesh'))\r\n"
        "\t\t{\r\n"
        "\t\t\ti = PawnIndex(P);\r\n"
        "\t\t\tDress(P, i, 0, Dressed[2]);\r\n"
        "\t\t\tDress(P, i, 1, Dressed[3]);\r\n"
        "\t\t}\r\n"
        "\t}\r\n"
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
        "\tif (M == None || Index < 0)\r\n"
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
// characters the engine allows in names. Empty means the stock material.
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
    if (!ValidPath(path) || !ValidPath(className + ".x")) throw std::runtime_error("Invalid material path: " + path);
    return className + "'" + path + "'";
}
} // namespace CharacterSkins
