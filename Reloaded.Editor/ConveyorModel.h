#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

// Conveyor belts: RE+ Tools > Add Conveyor Belt compiles three classes into the map
// package (MyLevel) and places a ConveyorBelt. Its rotation gives the run's direction
// and slope; Length and Width size it. In game it spawns its rollers (seen only,
// spinning) and the items riding it, loops the items from end to end, shrinking them
// away over FadeDistance at each end, and moves walking players standing on it with
// Pawn.Move: nothing a player stands on turns, so the view is never turned with it.
// The frame's mesh and the rollers' are made here and imported into the map with
// one small texture, so a map needs nothing installed beside it.
//
// The belt is static, so the lighting build lights its frame as any static mesh; at
// round start it spawns a ConveyorMotor, which ticks it. Every machine runs its own
// belt: the map's copy stays on each client, RemoteRole=ROLE_SimulatedProxy with
// simulated functions lets it run there (as Character Skins' actor does), and
// nothing is replicated. Motor, rollers and items are spawned by each machine with
// RemoteRole=ROLE_None. The host moves every player; a client moves only its own
// player, so riding is smooth there.
namespace Conveyor
{
// Raise with every change to a script or mesh: a map's older classes and meshes are
// made again over their old versions, and its placed belts keep their settings.
constexpr int Version = 1;
constexpr const char* BeltClass = "ConveyorBelt";
constexpr const char* MotorClass = "ConveyorMotor";
constexpr const char* ItemClass = "ConveyorItem";
constexpr const char* RollerClass = "ConveyorRoller";
constexpr const char* Group = "Conveyor";
constexpr const char* FrameMesh = "ConveyorFrame";
constexpr const char* RollerMesh = "ConveyorRoller";
constexpr const char* Texture = "ConveyorParts";
constexpr int ItemSlots = 8;
constexpr int MaxItems = 64;
// The meshes are 100 units long (X) and wide (Y); a belt's DrawScale3D is
// (Length / 100, Width / 100, 1), its rollers' Y scale (Width * RollerSpan / 100).
constexpr double Unit = 100.0;
constexpr double RollerRadius = 6.0, RollerGap = 16.0, RollerSpan = 0.88;
// How far the frame reaches under the riding surface (the pivot): a belt added on
// a floor stands on it.
constexpr double FrameDepth = 30.0;

// Stock cases and boxes riding a new belt, as many as fit the slots.
inline constexpr std::array<const char*, 6> DefaultItems = {{
    "Terminus_STM.hangar.Ind03_caisseOutilFerme",
    "Terminus_STM.hangar.Ind03_caisseOutilFerme01",
    "Terminus_STM.hangar.Ind03_caisseOutilFerme02",
    "STM_ORP.Objets.ORP_carton",
    "SLH07_STM.Objects.Boite_Multi",
    "Terminus_STM.Green.IND03_Caisse_Fragile",
}};

// The belt's DrawScale3D for a length and width (never below a unit's hundredth).
inline std::array<float, 3> FrameScale(double length, double width)
{
    return {static_cast<float>(std::max(length, 1.0) / Unit), static_cast<float>(std::max(width, 1.0) / Unit), 1.0f};
}

// Where an item's mesh pivot goes, from the centre of its base on the belt, at full
// size: its bounds' (min X, min Y, min Z, max X, max Y, max Z) centred and lifted.
inline std::array<float, 3> ItemOffset(const std::array<float, 6>& bounds)
{
    return {-(bounds[0] + bounds[3]) / 2.0f, -(bounds[1] + bounds[4]) / 2.0f, -bounds[2]};
}

inline std::string RollerScript(const std::string& package)
{
    return
        "//=============================================================================\r\n"
        "// ConveyorRoller: one of a ConveyorBelt's rollers, spun by the belt. It is only\r\n"
        "// seen: the belt's frame is what players and items stand on.\r\n"
        "//=============================================================================\r\n"
        "class ConveyorRoller extends Actor;\r\n"
        "\r\n"
        "defaultproperties\r\n"
        "{\r\n"
        "\tDrawType=DT_StaticMesh\r\n"
        "\tStaticMesh=StaticMesh'" + package + "." + Group + "." + RollerMesh + "'\r\n"
        "\tPhysics=PHYS_Rotating\r\n"
        "\tbFixedRotationDir=True\r\n"
        "\tbCollideActors=False\r\n"
        "\tbCollideWorld=False\r\n"
        "\tbBlockActors=False\r\n"
        "\tbBlockPlayers=False\r\n"
        "\tRemoteRole=ROLE_None\r\n"
        "}\r\n";
}

inline std::string MotorScript()
{
    return
        "//=============================================================================\r\n"
        "// ConveyorMotor: ticks the ConveyorBelt that spawned it (its owner), which is\r\n"
        "// static so that the map's lighting lights its frame.\r\n"
        "//=============================================================================\r\n"
        "class ConveyorMotor extends Info;\r\n"
        "\r\n"
        "function Tick(float DeltaTime)\r\n"
        "{\r\n"
        "\tif (Owner != None)\r\n"
        "\t\tOwner.Tick(DeltaTime);\r\n"
        "}\r\n"
        "\r\n"
        "defaultproperties\r\n"
        "{\r\n"
        "\tRemoteRole=ROLE_None\r\n"
        "}\r\n";
}

inline std::string ItemScript()
{
    return
        "//=============================================================================\r\n"
        "// ConveyorItem: something riding a ConveyorBelt, moved by the belt. It blocks\r\n"
        "// other actors but not players, so soft-body strip curtains part round it and\r\n"
        "// players walk through it.\r\n"
        "//=============================================================================\r\n"
        "class ConveyorItem extends Actor;\r\n"
        "\r\n"
        "var vector Offset;  // from the centre of its base to its pivot, at full size\r\n"
        "var float Size;     // its full DrawScale\r\n"
        "var rotator Turn;\r\n"
        "\r\n"
        "defaultproperties\r\n"
        "{\r\n"
        "\tDrawType=DT_StaticMesh\r\n"
        "\tbCollideActors=True\r\n"
        "\tbCollideWorld=False\r\n"
        "\tbBlockActors=True\r\n"
        "\tbBlockPlayers=False\r\n"
        "\tbBlockZeroExtentTraces=False\r\n"
        "\tbBlockNonZeroExtentTraces=False\r\n"
        "\tCollisionRadius=30\r\n"
        "\tCollisionHeight=14\r\n"
        "\tSize=1\r\n"
        "\tRemoteRole=ROLE_None\r\n"
        "}\r\n";
}

inline std::string BeltScript(const std::string& package)
{
    const auto items = std::to_string(ItemSlots), most = std::to_string(MaxItems);
    char numbers[160];
    std::snprintf(numbers, sizeof(numbers), "%g", RollerRadius);
    const std::string radius = numbers;
    std::snprintf(numbers, sizeof(numbers), "%g", RollerGap);
    const std::string gap = numbers;
    std::snprintf(numbers, sizeof(numbers), "%g", RollerSpan);
    const std::string span = numbers;
    return
        "//=============================================================================\r\n"
        "// ConveyorBelt: a conveyor, placed by RE+ Tools > Add Conveyor Belt. Its\r\n"
        "// rotation gives the run's direction (X) and slope; Length and Width size it.\r\n"
        "// In game it spawns its rollers and items, loops the items along it and moves\r\n"
        "// walking players standing on it. Each machine runs its own (simulated on\r\n"
        "// clients); nothing replicates.\r\n"
        "//=============================================================================\r\n"
        "class ConveyorBelt extends Actor\r\n"
        "\tplaceable;\r\n"
        "\r\n"
        "var(Conveyor) float Length;        // along the run (X), in units\r\n"
        "var(Conveyor) float Width;\r\n"
        "var(Conveyor) float Speed;         // units a second; below 0 runs backwards\r\n"
        "var(Conveyor) float ItemSpacing;   // between items; 0 for none\r\n"
        "var(Conveyor) float FadeDistance;  // items shrink away over this at each end\r\n"
        "var(Conveyor) float ItemScale;\r\n"
        "var(Conveyor) bool bCarryPlayers;\r\n"
        "var(Conveyor) bool bShowItems;\r\n"
        "var(Conveyor) bool bShowRollers;\r\n"
        "var(Conveyor) StaticMesh ItemMeshes[" + items + "];\r\n"
        "var vector ItemOffsets[" + items + "];  // kept by the editor from the meshes' bounds\r\n"
        "var const int Version;\r\n"
        "var ConveyorItem Items[" + most + "];\r\n"
        "var float Along[" + most + "];\r\n"
        "var int ItemCount;\r\n"
        "\r\n"
        "// The belt is static, so its frame is lit with the map; a motor ticks it.\r\n"
        "simulated function PostBeginPlay()\r\n"
        "{\r\n"
        "\tSuper.PostBeginPlay();\r\n"
        "\tSpawn(class'ConveyorMotor', self);\r\n"
        "\tif (bShowRollers)\r\n"
        "\t\tSpawnRollers();\r\n"
        "\tif (bShowItems)\r\n"
        "\t\tSpawnItems();\r\n"
        "}\r\n"
        "\r\n"
        "simulated function SpawnRollers()\r\n"
        "{\r\n"
        "\tlocal int i, n;\r\n"
        "\tlocal float Gap;\r\n"
        "\tlocal vector X, Y, Z, S;\r\n"
        "\tlocal ConveyorRoller R;\r\n"
        "\r\n"
        "\tGetAxes(Rotation, X, Y, Z);\r\n"
        "\tn = Clamp(int(Length / " + gap + "), 1, 400);\r\n"
        "\tGap = Length / n;\r\n"
        "\tS.X = 1;\r\n"
        "\tS.Y = FMax(Width * " + span + ", 1) / 100.0;\r\n"
        "\tS.Z = 1;\r\n"
        "\tfor (i = 0; i < n; i++)\r\n"
        "\t{\r\n"
        "\t\tR = Spawn(class'ConveyorRoller', self,, Location + X * (Gap * (i + 0.5) - Length / 2) - Z * " + radius + ", Rotation);\r\n"
        "\t\tif (R == None)\r\n"
        "\t\t\tcontinue;\r\n"
        "\t\tR.SetDrawScale3D(S);\r\n"
        "\t\t// A turn in pitch takes a roller's top back along X: run it the other way.\r\n"
        "\t\tR.RotationRate.Pitch = int(-Speed / " + radius + " * 10430.378);\r\n"
        "\t\tR.RotationRate.Yaw = 0;\r\n"
        "\t\tR.RotationRate.Roll = 0;\r\n"
        "\t}\r\n"
        "}\r\n"
        "\r\n"
        "simulated function SpawnItems()\r\n"
        "{\r\n"
        "\tlocal int i, k, n, Kinds;\r\n"
        "\tlocal int Pick[" + items + "];\r\n"
        "\tlocal ConveyorItem Item;\r\n"
        "\r\n"
        "\tfor (k = 0; k < " + items + "; k++)\r\n"
        "\t{\r\n"
        "\t\tif (ItemMeshes[k] != None)\r\n"
        "\t\t{\r\n"
        "\t\t\tPick[Kinds] = k;\r\n"
        "\t\t\tKinds++;\r\n"
        "\t\t}\r\n"
        "\t}\r\n"
        "\tif (Kinds == 0 || ItemSpacing <= 0 || Length <= 0)\r\n"
        "\t\treturn;\r\n"
        "\tn = Min(int(Length / ItemSpacing), " + most + ");\r\n"
        "\tfor (i = 0; i < n; i++)\r\n"
        "\t{\r\n"
        "\t\tItem = Spawn(class'ConveyorItem', self,, Location, Rotation);\r\n"
        "\t\tif (Item == None)\r\n"
        "\t\t\tcontinue;\r\n"
        "\t\tk = Pick[Rand(Kinds)];\r\n"
        "\t\tItem.SetStaticMesh(ItemMeshes[k]);\r\n"
        "\t\tItem.Size = ItemScale;\r\n"
        "\t\tItem.Offset = ItemOffsets[k] * ItemScale;\r\n"
        "\t\tItem.Turn = Rotation;\r\n"
        "\t\tItem.Turn.Yaw += Rand(2001) - 1000;\r\n"
        "\t\tItem.SetRotation(Item.Turn);\r\n"
        "\t\tItems[ItemCount] = Item;\r\n"
        "\t\tAlong[ItemCount] = (i + FRand() * 0.6) * Length / n;\r\n"
        "\t\tItemCount++;\r\n"
        "\t}\r\n"
        "\tMoveItems(0);\r\n"
        "}\r\n"
        "\r\n"
        "simulated function MoveItems(float DeltaTime)\r\n"
        "{\r\n"
        "\tlocal int i;\r\n"
        "\tlocal float A, F;\r\n"
        "\tlocal vector X, Y, Z;\r\n"
        "\r\n"
        "\tGetAxes(Rotation, X, Y, Z);\r\n"
        "\tfor (i = 0; i < ItemCount; i++)\r\n"
        "\t{\r\n"
        "\t\tif (Items[i] == None)\r\n"
        "\t\t\tcontinue;\r\n"
        "\t\tA = Along[i] + Speed * DeltaTime;\r\n"
        "\t\tif (A > Length)\r\n"
        "\t\t\tA -= Length;\r\n"
        "\t\telse if (A < 0)\r\n"
        "\t\t\tA += Length;\r\n"
        "\t\tAlong[i] = A;\r\n"
        "\t\tF = 1;\r\n"
        "\t\tif (FadeDistance > 0)\r\n"
        "\t\t\tF = FClamp(FMin(A, Length - A) / FadeDistance, 0.02, 1.0);\r\n"
        "\t\tItems[i].SetDrawScale(Items[i].Size * F);\r\n"
        "\t\tItems[i].SetLocation(Location + X * (A - Length / 2) + ((Items[i].Offset * F) >> Items[i].Turn));\r\n"
        "\t}\r\n"
        "}\r\n"
        "\r\n"
        "// Whether a walking pawn stands on the belt: over its top, within its length and\r\n"
        "// width, its middle no higher than it stands.\r\n"
        "simulated function bool Riding(Pawn P, vector X, vector Y, vector Z)\r\n"
        "{\r\n"
        "\tlocal vector L;\r\n"
        "\tlocal float H;\r\n"
        "\r\n"
        "\tif (P == None || P.Physics != PHYS_Walking)\r\n"
        "\t\treturn false;\r\n"
        "\tL = P.Location - Location;\r\n"
        "\tif (Abs(L dot X) > Length / 2 || Abs(L dot Y) > Width / 2)\r\n"
        "\t\treturn false;\r\n"
        "\tH = L dot Z;\r\n"
        "\treturn H > 0 && H < P.CollisionHeight + 24;\r\n"
        "}\r\n"
        "\r\n"
        "simulated function CarryPlayers(float DeltaTime)\r\n"
        "{\r\n"
        "\tlocal Controller C;\r\n"
        "\tlocal PlayerController PC;\r\n"
        "\tlocal vector X, Y, Z;\r\n"
        "\r\n"
        "\tGetAxes(Rotation, X, Y, Z);\r\n"
        "\tif (Role < ROLE_Authority)\r\n"
        "\t{\r\n"
        "\t\tPC = Level.GetLocalPlayerController();\r\n"
        "\t\tif (PC != None && Riding(PC.Pawn, X, Y, Z))\r\n"
        "\t\t\tPC.Pawn.Move(X * Speed * DeltaTime);\r\n"
        "\t\treturn;\r\n"
        "\t}\r\n"
        "\tfor (C = Level.ControllerList; C != None; C = C.NextController)\r\n"
        "\t\tif (Riding(C.Pawn, X, Y, Z))\r\n"
        "\t\t\tC.Pawn.Move(X * Speed * DeltaTime);\r\n"
        "}\r\n"
        "\r\n"
        "// Called by the belt's ConveyorMotor every frame.\r\n"
        "simulated function Tick(float DeltaTime)\r\n"
        "{\r\n"
        "\tif (ItemCount > 0)\r\n"
        "\t\tMoveItems(DeltaTime);\r\n"
        "\tif (bCarryPlayers && Speed != 0)\r\n"
        "\t\tCarryPlayers(DeltaTime);\r\n"
        "}\r\n"
        "\r\n"
        "defaultproperties\r\n"
        "{\r\n"
        "\tLength=600\r\n"
        "\tWidth=120\r\n"
        "\tSpeed=150\r\n"
        "\tItemSpacing=140\r\n"
        "\tFadeDistance=60\r\n"
        "\tItemScale=1\r\n"
        "\tbCarryPlayers=True\r\n"
        "\tbShowItems=True\r\n"
        "\tbShowRollers=True\r\n"
        "\tVersion=" + std::to_string(Version) + "\r\n"
        "\tDrawType=DT_StaticMesh\r\n"
        "\tStaticMesh=StaticMesh'" + package + "." + Group + "." + FrameMesh + "'\r\n"
        "\tDrawScale3D=(X=6,Y=1.2,Z=1)\r\n"
        "\tbStatic=True\r\n"
        "\tbNoDelete=True\r\n"
        "\tbCollideActors=True\r\n"
        "\tbBlockActors=True\r\n"
        "\tbBlockPlayers=True\r\n"
        "\tbBlockZeroExtentTraces=True\r\n"
        "\tbBlockNonZeroExtentTraces=True\r\n"
        "\tbEdShouldSnap=True\r\n"
        "\tRemoteRole=ROLE_SimulatedProxy\r\n"
        "}\r\n";
}

// ----------------------------------------------------------------------------
// The meshes, as ASCII Scene Export text the editor's STATICMESH IMPORT reads,
// coloured from one small texture (a row of swatches).

using Point = std::array<double, 3>;
struct Triangle
{
    std::array<Point, 3> points;
    std::array<std::array<double, 2>, 3> uvs;
};
struct Mesh
{
    std::string name;
    std::vector<Triangle> triangles;
    std::vector<std::vector<Point>> hulls; // convex boxes, as their eight corners
};

// The texture: Swatches cells of 16x16 in one row.
constexpr int Swatches = 8, SwatchSize = 16;
enum Swatch { Paint, Plate, RollerDark, RollerLight, RollerRing, Hazard, Steel, SwatchCount };
inline constexpr std::array<std::array<uint8_t, 3>, Swatches> SwatchColours = {{
    {58, 78, 104},    // the frame's channels: slate blue
    {34, 35, 38},     // the bed under the rollers
    {64, 66, 70},     // roller faces, in turn a shade apart so they are seen turning
    {104, 107, 112},
    {196, 196, 188},  // the white bands at a roller's ends
    {206, 164, 32},   // the hazard yellow on the channels' tops
    {150, 154, 160},  // steel
    {128, 128, 128},
}};

// A face's corners mapped onto its swatch, inside a border so filtering never
// reaches the next one.
inline std::array<double, 2> SwatchUV(Swatch s, double u, double v)
{
    const double border = 3.0 / SwatchSize;
    return {(static_cast<double>(s) + border + u * (1 - 2 * border)) / Swatches, border + v * (1 - 2 * border)};
}

// A quad (corners in turn) as two triangles, its corners on its swatch's.
inline void Quad(Mesh& m, const Point& a, const Point& b, const Point& c, const Point& d, Swatch s)
{
    const auto ua = SwatchUV(s, 0, 0), ub = SwatchUV(s, 1, 0), uc = SwatchUV(s, 1, 1), ud = SwatchUV(s, 0, 1);
    m.triangles.push_back({{a, b, c}, {ua, ub, uc}});
    m.triangles.push_back({{a, c, d}, {ua, uc, ud}});
}

inline Point Sub(const Point& a, const Point& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline Point Cross(const Point& a, const Point& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline double Dot(const Point& a, const Point& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// A face whose winding is turned, if need be, so (b - a) x (c - a) points along
// outward (the convention read_ase and the importer agree on).
inline void Face(Mesh& m, Point a, Point b, Point c, Point d, const Point& outward, Swatch s)
{
    if (Dot(Cross(Sub(b, a), Sub(c, a)), outward) < 0) std::swap(b, d);
    Quad(m, a, b, c, d, s);
}

inline void Box(Mesh& m, const Point& lo, const Point& hi, Swatch sides, Swatch top)
{
    const double x0 = lo[0], y0 = lo[1], z0 = lo[2], x1 = hi[0], y1 = hi[1], z1 = hi[2];
    Face(m, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {0, 0, 1}, top);
    Face(m, {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, {0, 0, -1}, sides);
    Face(m, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {0, -1, 0}, sides);
    Face(m, {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}, {0, 1, 0}, sides);
    Face(m, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {-1, 0, 0}, sides);
    Face(m, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, {1, 0, 0}, sides);
}

inline void Hull(Mesh& m, const Point& lo, const Point& hi)
{
    std::vector<Point> corners;
    for (int i = 0; i < 8; ++i) corners.push_back({(i & 1) ? hi[0] : lo[0], (i & 2) ? hi[1] : lo[1], (i & 4) ? hi[2] : lo[2]});
    m.hulls.push_back(corners);
}

// The frame, pivot on the riding surface's centre: a dark bed under the rollers
// between two slate channels capped in yellow. Players stand on a still bed at the
// rollers' tops (its collision), and the channels' tops stand a little above it.
inline Mesh Frame()
{
    Mesh m{FrameMesh, {}, {}};
    const double h = Unit / 2, inner = 45, outer = 50, depth = FrameDepth;
    Box(m, {-h, -inner, -28}, {h, inner, -25}, Plate, Plate);
    for (int s : {-1, 1})
    {
        const double y0 = s < 0 ? -outer : inner, y1 = s < 0 ? -inner : outer;
        Box(m, {-h, y0, -depth}, {h, y1, 3}, Paint, Paint);
        Box(m, {-h, y0, 3}, {h, y1, 5}, Hazard, Hazard);
        Hull(m, {-h, y0, -depth}, {h, y1, 5});
    }
    Hull(m, {-h, -inner, -depth}, {h, inner, 0});
    return m;
}

// A roller along Y, 100 long, pivot on its axis: twelve faces in two shades, a white
// band near each end, and end caps.
inline Mesh Roller()
{
    Mesh m{RollerMesh, {}, {}};
    constexpr int sides = 12;
    const double r = RollerRadius, h = Unit / 2, band = 4;
    std::array<std::array<double, 2>, sides> ring{};
    for (int i = 0; i < sides; ++i)
    {
        const double a = 2 * 3.14159265358979323846 * (i + 0.5) / sides;
        ring[i] = {r * std::sin(a), r * std::cos(a)};
    }
    for (int i = 0; i < sides; ++i)
    {
        const auto& p = ring[i];
        const auto& q = ring[(i + 1) % sides];
        const Point out{(p[0] + q[0]) / 2, 0, (p[1] + q[1]) / 2};
        const Swatch face = i % 2 ? RollerDark : RollerLight;
        for (auto [y0, y1, s] : {std::tuple<double, double, Swatch>{-h, -h + band, RollerRing},
                                 {-h + band, h - band, face}, {h - band, h, RollerRing}})
            Face(m, {p[0], y0, p[1]}, {q[0], y0, q[1]}, {q[0], y1, q[1]}, {p[0], y1, p[1]}, out, s);
    }
    for (int s : {-1, 1})
    {
        const double y = s * h;
        const auto uv = SwatchUV(Steel, 0.5, 0.5);
        for (int i = 1; i + 1 < sides; ++i)
        {
            Point a{ring[0][0], y, ring[0][1]}, b{ring[i][0], y, ring[i][1]}, c{ring[i + 1][0], y, ring[i + 1][1]};
            if (Dot(Cross(Sub(b, a), Sub(c, a)), Point{0, static_cast<double>(s), 0}) < 0) std::swap(b, c);
            m.triangles.push_back({{a, b, c}, {uv, uv, uv}});
        }
    }
    return m;
}

inline std::string Number(double x)
{
    if (std::abs(x) < 5e-5) x = 0;
    char text[32];
    std::snprintf(text, sizeof(text), "%.4f", x);
    return text;
}

// One GEOMOBJECT. Unreal (x, y, z) is written as Max (x, -y, z); the mirror turns
// the winding, so each face lists its corners a, c, b; V is written 1 - v.
inline std::string GeomObject(const std::string& name, const std::vector<Triangle>& triangles)
{
    std::vector<Point> vertices;
    std::vector<std::array<int, 3>> faces;
    auto index = [&](const Point& p) {
        for (size_t i = 0; i < vertices.size(); ++i)
            if (std::abs(vertices[i][0] - p[0]) < 1e-4 && std::abs(vertices[i][1] - p[1]) < 1e-4 && std::abs(vertices[i][2] - p[2]) < 1e-4)
                return static_cast<int>(i);
        vertices.push_back(p);
        return static_cast<int>(vertices.size() - 1);
    };
    for (const auto& t : triangles) faces.push_back({index(t.points[0]), index(t.points[1]), index(t.points[2])});
    std::string out = "*GEOMOBJECT {\r\n\t*NODE_NAME \"" + name + "\"\r\n\t*MESH {\r\n\t\t*TIMEVALUE 0\r\n";
    out += "\t\t*MESH_NUMVERTEX " + std::to_string(vertices.size()) + "\r\n\t\t*MESH_NUMFACES " + std::to_string(faces.size()) + "\r\n";
    out += "\t\t*MESH_VERTEX_LIST {\r\n";
    for (size_t i = 0; i < vertices.size(); ++i)
        out += "\t\t\t*MESH_VERTEX " + std::to_string(i) + "\t" + Number(vertices[i][0]) + "\t" + Number(-vertices[i][1]) + "\t" + Number(vertices[i][2]) + "\r\n";
    out += "\t\t}\r\n\t\t*MESH_FACE_LIST {\r\n";
    for (size_t i = 0; i < faces.size(); ++i)
        out += "\t\t\t*MESH_FACE " + std::to_string(i) + ":\tA: " + std::to_string(faces[i][0]) + " B: " + std::to_string(faces[i][2]) +
               " C: " + std::to_string(faces[i][1]) + " AB: 1 BC: 1 CA: 1\t*MESH_SMOOTHING 0\t*MESH_MTLID 0\r\n";
    out += "\t\t}\r\n\t\t*MESH_NUMTVERTEX " + std::to_string(3 * triangles.size()) + "\r\n\t\t*MESH_TVERTLIST {\r\n";
    for (size_t i = 0; i < triangles.size(); ++i)
        for (int k = 0; k < 3; ++k)
            out += "\t\t\t*MESH_TVERT " + std::to_string(3 * i + k) + "\t" + Number(triangles[i].uvs[k][0]) + "\t" +
                   Number(1.0 - triangles[i].uvs[k][1]) + "\t0.0000\r\n";
    out += "\t\t}\r\n\t\t*MESH_NUMTVFACES " + std::to_string(triangles.size()) + "\r\n\t\t*MESH_TFACELIST {\r\n";
    for (size_t i = 0; i < triangles.size(); ++i)
        out += "\t\t\t*MESH_TFACE " + std::to_string(i) + "\t" + std::to_string(3 * i) + "\t" + std::to_string(3 * i + 2) + "\t" +
               std::to_string(3 * i + 1) + "\r\n";
    out += "\t\t}\r\n\t}\r\n\t*MATERIAL_REF 0\r\n}\r\n";
    return out;
}

// A box hull's twelve outward triangles. The importer turns MCD objects half a turn
// about Z against the rest of the mesh, so they are written turned the other way.
inline std::vector<Triangle> HullTriangles(const std::vector<Point>& corners)
{
    Point lo = corners[0], hi = corners[0];
    for (const auto& p : corners)
        for (int a = 0; a < 3; ++a) { lo[a] = std::min(lo[a], p[a]); hi[a] = std::max(hi[a], p[a]); }
    Mesh box;
    Box(box, {-hi[0], -hi[1], lo[2]}, {-lo[0], -lo[1], hi[2]}, Plate, Plate);
    return box.triangles;
}

inline std::string Ase(const Mesh& m)
{
    std::string out = "*3DSMAX_ASCIIEXPORT\t200\r\n*COMMENT \"RE+ conveyor: " + m.name + "\"\r\n*MATERIAL_LIST {\r\n"
                      "\t*MATERIAL_COUNT 1\r\n\t*MATERIAL 0 {\r\n\t\t*MATERIAL_NAME \"" + m.name + "\"\r\n"
                      "\t\t*MATERIAL_CLASS \"Multi/Sub-Object\"\r\n\t\t*NUMSUBMTLS 1\r\n"
                      "\t\t*SUBMATERIAL 0 {\r\n\t\t\t*MATERIAL_NAME \"" + std::string(Texture) + "\"\r\n"
                      "\t\t\t*MATERIAL_CLASS \"Standard\"\r\n\t\t\t*MATERIAL_DIFFUSE 1.0000\t1.0000\t1.0000\r\n"
                      "\t\t\t*MAP_DIFFUSE {\r\n\t\t\t\t*MAP_NAME \"" + Texture + "\"\r\n\t\t\t\t*MAP_CLASS \"Bitmap\"\r\n"
                      "\t\t\t\t*BITMAP \"" + Texture + ".tga\"\r\n\t\t\t\t*UVW_U_TILING 1.0000\r\n\t\t\t\t*UVW_V_TILING 1.0000\r\n"
                      "\t\t\t}\r\n\t\t}\r\n\t}\r\n}\r\n";
    out += GeomObject(m.name, m.triangles);
    for (size_t i = 0; i < m.hulls.size(); ++i)
    {
        char suffix[8];
        std::snprintf(suffix, sizeof(suffix), "%02d", static_cast<int>(i));
        out += GeomObject("MCDCX_" + m.name + "_" + suffix, HullTriangles(m.hulls[i]));
    }
    return out;
}

// Bounds of the visible faces and hulls: (min X, min Y, min Z, max X, max Y, max Z).
inline std::array<double, 6> Bounds(const Mesh& m)
{
    std::array<double, 6> b{1e9, 1e9, 1e9, -1e9, -1e9, -1e9};
    auto add = [&](const Point& p) { for (int a = 0; a < 3; ++a) { b[a] = std::min(b[a], p[a]); b[a + 3] = std::max(b[a + 3], p[a]); } };
    for (const auto& t : m.triangles) for (const auto& p : t.points) add(p);
    for (const auto& h : m.hulls) for (const auto& p : h) add(p);
    return b;
}

// The swatch texture as a 32-bit TGA (top-left origin), with a faint grain.
inline std::vector<uint8_t> TextureTga()
{
    const int width = Swatches * SwatchSize, height = SwatchSize;
    std::vector<uint8_t> out = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                static_cast<uint8_t>(width & 0xff), static_cast<uint8_t>(width >> 8),
                                static_cast<uint8_t>(height & 0xff), static_cast<uint8_t>(height >> 8), 32, 0x28};
    uint32_t seed = 0x2545f491;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            seed = seed * 1664525u + 1013904223u;
            const int grain = static_cast<int>((seed >> 24) % 9) - 4;
            const auto& c = SwatchColours[x / SwatchSize];
            for (int k : {2, 1, 0}) out.push_back(static_cast<uint8_t>(std::clamp(c[k] + grain, 0, 255)));
            out.push_back(255);
        }
    return out;
}
}
