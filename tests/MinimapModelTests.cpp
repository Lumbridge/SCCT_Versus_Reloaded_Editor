#include "../Reloaded.Editor/MinimapModel.h"
#include <iostream>
#include <source_location>
#include <string>
using namespace Minimap;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
template <class F> void Reject(F f, std::source_location where = std::source_location::current())
{
    bool caught = false;
    try { f(); } catch (const std::exception&) { caught = true; }
    ++checks;
    if (!caught) throw std::runtime_error("invalid input accepted at line " + std::to_string(where.line()));
}
bool Near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }

// AquaD's own SnapshotCamera, as its LevelInfo stores it.
Camera Aquarium()
{
    Camera c;
    c.position = { 5790.6, 2009, 8857 };
    c.fov = 45;
    c.targetDistance = 8857;
    return c;
}

void Transform()
{
    const auto c = Aquarium();
    const double half = 8857 * std::tan(22.5 * kPi / 180);
    Check(Near(HalfExtent(c), half), "half extent is TargetDistance * tan(FOV / 2)");
    Check(Usable(c) && TopDown(c), "the stock camera is usable and top-down");
    auto centre = Project(c, { 5790.6, 2009, -500 });
    Check(Near(centre.u, 0.5) && Near(centre.v, 0.5), "the camera position is the middle of the picture, at any height");
    auto corner = Project(c, { 5790.6 + half, 2009 + half, 0 });
    Check(Near(corner.u, 1) && Near(corner.v, 1), "+X is right and +Y is down");
    auto other = Project(c, { 5790.6 - half, 2009 - half, 1000 });
    Check(Near(other.u, 0) && Near(other.v, 0), "the opposite corner is the top left");
    auto px = ToPixel(c, { 5790.6 + half / 2, 2009, 0 }, 256);
    Check(Near(px.first, 192) && Near(px.second, 128), "pixels scale with the picture");
    auto back = FromPixel(c, 192, 64, 256);
    Check(Near(back.first, 5790.6 + half / 2, 1e-6) && Near(back.second, 2009 - half / 2, 1e-6), "a pixel maps back to the world");

    // Any rotation goes through the game's own steps: rows, then (b, c, a).
    Camera turned = c;
    turned.rows = { Vec3{ 0, 0, 0 }, Vec3{ 0, 1, 0 }, Vec3{ 0, 0, 0 } };
    turned.rows[0] = { 0, 0, -1 }; // world X feeds the picture's vertical axis
    auto t = Project(turned, { 5790.6 + half, 2009, 0 });
    Check(Near(t.u, 0.5) && Near(t.v, 1), "a turned camera follows the matrix");
    Check(!TopDown(turned), "only the stock rows count as top-down");
    Reject([&] { FromPixel(turned, 0, 0, 256); });
    Camera empty;
    Check(!Usable(empty), "a camera with no distance is not usable");
    Reject([&] { Project(empty, { 0, 0, 0 }); });
}

void Framing()
{
    Bounds b;
    Check(b.Empty(), "no points, no bounds");
    Reject([&] { Square(b); });
    b.Add(-1000, 200); b.Add(3000, 1200); b.Add(std::nan(""), 5);
    Check(!b.Empty() && b.minX == -1000 && b.maxY == 1200, "bounds grow and skip NaN");
    const auto f = Square(b, 0.05);
    Check(f.cx == 1000 && f.cy == 700, "centred on the map");
    Check(f.half == 2100, "half the longer side plus the margin");
    const auto c = CameraFor(f);
    Check(c.fov == kStockFov && Near(c.position[2], c.targetDistance) && Near(HalfExtent(c), 2100, 1e-6), "the stock camera form");
    Check(Near(Project(c, { -1100, 700, 0 }).u, 0, 1e-9) && Near(Project(c, { 1000, 2800, 0 }).v, 1, 1e-9), "the frame is the picture's edges");
    const auto g = FrameOf(c);
    Check(Near(g.cx, 1000) && Near(g.half, 2100, 1e-6), "the frame round-trips through the camera");
    Bounds tiny; tiny.Add(10, 10);
    Check(Square(tiny).half == 256, "a single point still gets a picture");
}

void Floors()
{
    // The game's choice: index 0 below the second floor whatever FloorZ[0] says.
    const std::vector<double> z{ 500, 1000, 2000 };
    Check(FloorIndex(z, -10000) == 0, "below everything is the first floor");
    Check(FloorIndex(z, 999) == 0 && FloorIndex(z, 1000) == 1 && FloorIndex(z, 1999) == 1, "a floor starts at its FloorZ");
    Check(FloorIndex(z, 5000) == 2, "above the top is the top floor");
    Check(FloorIndex({}, 0) == -1 && FloorIndex({ 7 }, -1e6) == 0, "no floors and one floor");

    const auto bands = Bands({ 512, -128, 512 });
    Check(bands.size() == 2, "duplicates merge");
    Check(Near(bands[0].lower, -128.5) && Near(bands[0].upper, 511.5) && std::isinf(bands[1].upper), "bands run floor to floor, the top one open");
    Check(bands[0].Contains(-128) && !bands[0].Contains(512) && bands[1].Contains(1e5), "a band holds its own floor and not the next");
    const auto heights = FloorHeights(bands);
    Check(heights.size() == 2 && heights[0] == -128 && heights[1] == 512, "MapFloors take the floors' heights");
    Check(FloorIndex(heights, -80) == 0 && FloorIndex(heights, 560) == 1, "a pawn standing on each floor sees it");
    Band all;
    Check(all.Whole() && all.Contains(-1e9), "the whole map");
}

Polygon Face(std::vector<Vec3> points, Vec3 normal, std::uint32_t flags = 0) { return { std::move(points), normal, flags }; }

void Classification()
{
    const auto bands = Bands({ 0, 400 });
    const auto floor0 = Face({ { 0, 0, 0 }, { 100, 0, 0 }, { 100, 100, 0 } }, { 0, 0, 1 });
    const auto floor1 = Face({ { 0, 0, 400 }, { 100, 0, 400 }, { 100, 100, 400 } }, { 0, 0, 1 });
    Check(Classify(floor0, bands[0]) == Kind::Floor && Classify(floor1, bands[0]) == Kind::Silhouette, "own floor light, others dark");
    Check(Classify(floor1, bands[1]) == Kind::Floor && Classify(floor0, bands[1]) == Kind::Silhouette, "and the other way up");
    const auto ramp = Face({ { 0, 0, 10 }, { 100, 0, 60 }, { 100, 100, 60 } }, { -0.45, 0, 0.89 });
    Check(Classify(ramp, bands[0]) == Kind::Floor, "a ramp is floor");
    const auto wall = Face({ { 0, 0, 0 }, { 0, 100, 0 }, { 0, 100, 400 }, { 0, 0, 400 } }, { 1, 0, 0 });
    Check(Classify(wall, bands[0]) == Kind::Wall && Classify(wall, bands[1]) == Kind::Skip, "a wall belongs to the floor it rises from");
    const auto ceiling = Face({ { 0, 0, 380 }, { 100, 0, 380 }, { 100, 100, 380 } }, { 0, 0, -1 });
    Check(Classify(ceiling, bands[0]) == Kind::Skip && Classify(ceiling, Band{}) == Kind::Skip, "ceilings are not drawn");
    Check(Classify(Face(floor0.points, { 0, 0, 1 }, kPolyInvisible), bands[0]) == Kind::Skip, "invisible faces are not drawn");
    Check(Classify(Face(floor0.points, { 0, 0, 1 }, kPolyPortal), bands[0]) == Kind::Skip, "portals are not drawn");
    Check(Classify(Face({ { 0, 0, 0 }, { 1, 0, 0 } }, { 0, 0, 1 }), bands[0]) == Kind::Skip, "a degenerate face is skipped");
    Check(Classify(wall, Band{}) == Kind::Wall, "every wall shows on the whole map");

    MeshBox slab{ { { 0, 0, -16 }, { 200, 0, -16 }, { 200, 200, 0 }, { 0, 200, 0 } } };
    MeshBox crate{ { { 0, 0, 0 }, { 64, 0, 0 }, { 64, 64, 96 }, { 0, 64, 96 } } };
    Check(Classify(slab, bands[0]) == Kind::Floor && Classify(slab, bands[1]) == Kind::Silhouette, "a flat mesh is floor");
    Check(Classify(crate, bands[0]) == Kind::Obstacle && Classify(crate, bands[1]) == Kind::Skip, "a tall mesh is in the way on its floor only");

    const auto hull = Footprint({ { 0, 0, 0 }, { 10, 0, 5 }, { 10, 10, 0 }, { 0, 10, 9 }, { 5, 5, 3 }, { 0, 0, 100 } });
    Check(hull.size() == 4, "the footprint drops inner and repeated points");

    const auto extent = Extent({ floor0, ceiling, wall }, { crate }, { { -50, 20, 0 } });
    Check(extent.minX == -50 && extent.maxX == 100 && extent.maxY == 100, "floors, meshes and markers frame the picture, ceilings do not");

    // An outdoor map: a sea bed far past the play area, a deck around the spawns.
    const auto sea = Face({ { -20000, -20000, -1000 }, { 20000, -20000, -1000 }, { 20000, 20000, -1000 } }, { 0, 0, 1 });
    const auto deck = Face({ { -600, -300, 0 }, { 900, -300, 0 }, { 900, 400, 0 } }, { 0, 0, 1 });
    const auto pier = Face({ { 900, 0, 0 }, { 1200, 0, 0 }, { 1200, 200, 0 } }, { 0, 0, 1 });
    const auto shore = Face({ { 900, 0, 0 }, { 3000, 0, 0 }, { 3000, 200, 0 } }, { 0, 0, 1 });
    const std::vector<Vec3> spawns{ { -400, -200, 40 }, { 700, 300, 40 } };
    const auto play = GameplayExtent({ sea, deck, pier, shore }, {}, spawns);
    Check(play.minX == -600 && play.maxX == 1200 && play.minY == -300 && play.maxY == 400, "the play area and the floors near it, not the sea or a far shore");
    Check(Extent({ sea, deck }, {}, spawns).minX == -20000, "everything built does include the sea");
    const auto none = GameplayExtent({ deck }, {}, {});
    Check(none.minX == -600 && none.maxX == 900, "without markers everything built frames the map");
    const auto frame = Square(play);
    Check(Backdrop(sea, frame) && !Backdrop(deck, frame) && !Backdrop(pier, frame), "a face wider than the picture is backdrop");
}

void Markers()
{
    std::vector<Marker> m{ { MarkerKind::Objective, { 0, 0, 40 } }, { MarkerKind::SpySpawn, { 0, 0, 40 } }, { MarkerKind::Objective, { 0, 0, 440 } }, { MarkerKind::Objective, { 0, 0, 0 }, 0, "A" } };
    Number(m);
    Check(m[0].label == "1" && m[1].label.empty() && m[2].label == "2" && m[3].label == "A", "objectives are numbered, named ones keep their name");
    std::vector<Marker> grouped{ { MarkerKind::Objective, {}, 0, "", 1 }, { MarkerKind::Objective, {}, 0, "", -1 }, { MarkerKind::Objective, {}, 0, "", 1 }, { MarkerKind::Objective, {}, 0, "", 0 }, { MarkerKind::Flag } };
    Number(grouped);
    Check(grouped[0].label == "2" && grouped[2].label == "2" && grouped[3].label == "1", "the triggers of one objective share its number");
    Check(grouped[1].label == "3" && grouped[4].label.empty(), "an unlisted trigger comes after the objectives; flags are not numbered");
    const auto bands = Bands({ 0, 400 });
    Check(Shows(bands[0], m[0]) && !Shows(bands[1], m[0]) && Shows(bands[1], m[2]) && Shows(Band{}, m[2]), "markers show on their own floor");
    auto east = Heading(0), south = Heading(16384);
    Check(Near(east.first, 1) && Near(east.second, 0) && Near(south.first, 0, 1e-12) && Near(south.second, 1), "yaw 0 points along +X, a quarter turn along +Y");
}

void Text()
{
    Check(Number(1.5) == "1.5" && Number(-0.0001) == "0" && Number(2000) == "2000" && Number(-128.25) == "-128.25", "plain numbers");
    Reject([] { Number(std::nan("")); });
    const auto c = CameraFor({ 100, -200, 1000 });
    const auto text = CameraText(c);
    Check(text.rfind("(Position=(X=100,Y=-200,Z=", 0) == 0, "the camera text starts with the position");
    Check(text.find("RotationRow0=(X=0,Y=1,Z=0),RotationRow1=(X=0,Y=0,Z=-1),RotationRow2=(X=-1,Y=0,Z=0),FOV=45,TargetDistance=") != std::string::npos, "the stock rows and FOV");
    Reject([] { CameraText(Camera{}); });
    Check(FloorsText({ { -128, "MyLevel.Minimap.Floor0" }, { 512, "MyLevel.Minimap.Floor1" } })
          == "((FloorZ=-128,FloorMapTexture=Texture'MyLevel.Minimap.Floor0'),(FloorZ=512,FloorMapTexture=Texture'MyLevel.Minimap.Floor1'))", "the floors text");
    Reject([] { FloorsText({}); });
    Reject([] { FloorsText({ { 0, "A" }, { 0, "B" } }); });
    Reject([] { FloorsText({ { 0, "Bad Name" } }); });
    Check(FloorTextureName(2) == "Floor2", "texture names");
    Check(FloorList({ -128.4, 512, 1000.6 }) == "-128, 512, 1001", "the floor list as the window shows it");
    const auto typed = ParseFloorList(" 512, -128 ;; 512,  1000 ");
    Check(typed.size() == 3 && typed[0] == -128 && typed[1] == 512 && typed[2] == 1000, "typed floors are sorted and repeats dropped");
    Check(ParseFloorList("").empty() && ParseFloorList(" , ").empty(), "no floors typed");
    Reject([] { ParseFloorList("12, ground"); });
    Reject([] { ParseFloorList("1e9"); });
}

void Tga()
{
    const Bytes pixels{ 1, 2, 3, 4, 5, 6, 7, 8 };
    const auto tga = Tga32(1, 2, pixels);
    Check(tga.size() == 18 + 8 && tga[2] == 2 && tga[12] == 1 && tga[14] == 2 && tga[16] == 32 && tga[17] == 0x08, "the header: bottom-up, 8 alpha bits");
    Check(tga[18] == 7 && tga[19] == 6 && tga[20] == 5 && tga[21] == 8, "the bottom row first, as BGRA");
    Check(tga[22] == 3 && tga[23] == 2 && tga[24] == 1 && tga[25] == 4, "then the top row");
    Reject([&] { Tga32(3, 1, pixels); });
}

// A minimal v300 package holding one texture export, written the way
// LevelSnapshotModelTests' builder writes one.
struct Builder
{
    Bytes b;
    std::vector<std::string> names{ "None", "Core", "Engine", "Class", "Texture", "Briefing", "USize", "VSize", "Format", "IntProperty", "ByteProperty", "Package", "Menu" };
    void U8(unsigned v) { b.push_back(static_cast<std::uint8_t>(v)); }
    void U16(unsigned v) { U8(v); U8(v >> 8); }
    void U32(std::uint32_t v) { for (int i = 0; i < 4; ++i) U8(v >> (8 * i)); }
    void Compact(int v)
    {
        unsigned a = static_cast<unsigned>(v < 0 ? -v : v);
        std::uint8_t first = static_cast<std::uint8_t>((v < 0 ? 0x80 : 0) | (a & 0x3f));
        a >>= 6;
        if (a) first |= 0x40;
        b.push_back(first);
        while (a) { std::uint8_t c = a & 0x7f; a >>= 7; if (a) c |= 0x80; b.push_back(c); }
    }
    void Name(const std::string& n) { Compact(static_cast<int>(std::find(names.begin(), names.end(), n) - names.begin())); Compact(0); }
    void Patch(size_t at, std::uint32_t v) { for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (8 * i)); }
    Bytes Build(const std::vector<std::string>& textures, int size)
    {
        U32(Snapshot::kPackageMagic); U16(300); U16(0); U32(0);
        const size_t counts = b.size();
        for (int i = 0; i < 6; ++i) U32(0);
        std::vector<std::pair<size_t, size_t>> data;
        for (size_t t = 0; t < textures.size(); ++t)
        {
            const size_t start = b.size();
            Name("USize"); U8(0x22); U32(size);
            Name("VSize"); U8(0x22); U32(size);
            Name("Format"); U8(0x01); U8(5);
            Name("None");
            Compact(1); U32(0); Compact(4); U32(0); U32(size); U32(size); U8(8); U8(8);
            data.emplace_back(start, b.size() - start);
        }
        const size_t nameOffset = b.size();
        for (const auto& n : names)
        {
            Compact(static_cast<int>(n.size() + 1));
            for (size_t j = 0; j <= n.size(); ++j) { const size_t at = b.size(); U8(static_cast<std::uint8_t>((j < n.size() ? n[j] : 0) ^ (at & 255))); }
            U32(0);
        }
        const size_t importOffset = b.size();
        Name("Core"); Name("Package"); U32(0); Name("Engine");
        Name("Core"); Name("Class"); U32(static_cast<std::uint32_t>(-1)); Name("Texture");
        const size_t exportOffset = b.size();
        for (size_t t = 0; t < textures.size(); ++t)
        {
            Compact(-2); Compact(0); U32(0); Name(textures[t]); U32(0); Compact(static_cast<int>(data[t].second)); Compact(static_cast<int>(data[t].first));
        }
        Patch(counts, static_cast<std::uint32_t>(names.size())); Patch(counts + 4, static_cast<std::uint32_t>(nameOffset));
        Patch(counts + 8, static_cast<std::uint32_t>(textures.size())); Patch(counts + 12, static_cast<std::uint32_t>(exportOffset));
        Patch(counts + 16, 2); Patch(counts + 20, static_cast<std::uint32_t>(importOffset));
        return b;
    }
};

void Package()
{
    const auto good = Builder().Build({ "Menu", "Briefing" }, 256);
    const auto t = VerifyTexture(good, "Briefing", 256, 2);
    Check(t.width == 256 && t.mips == 1, "the Briefing texture is found and measured");
    Reject([&] { VerifyTexture(good, "Briefing", 256, 3); });
    Reject([&] { VerifyTexture(Builder().Build({ "Menu" }, 256), "Briefing", 256, 0); });
    Reject([&] { VerifyTexture(Builder().Build({ "Briefing" }, 128), "Briefing", 256, 0); });
}

int main()
{
    try
    {
        Transform();
        Framing();
        Floors();
        Classification();
        Markers();
        Text();
        Tga();
        Package();
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    std::cout << "MinimapModelTests: " << checks << " checks passed\n";
    return 0;
}
