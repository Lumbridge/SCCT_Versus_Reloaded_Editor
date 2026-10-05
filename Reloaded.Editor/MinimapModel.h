#pragma once
// The pure part of Build > Generate Minimap: where a point of the world lands
// on the game's map pictures, which floor the game shows, how the picture is
// framed and drawn, and the text the editor's property importer takes. No
// engine and no Windows, so tests/MinimapModelTests.cpp compiles it alone.
//
// What the game does (reverse-engineered from ChaosTheory_Editor.exe, which
// carries the same GUI natives as the game, on 2026-10-05):
//  - The in-game strategic map (GUI StrategicMap, natives 1531
//    UpdateStrategicMapPosition and 1868 UpdateTeamMate at 0x10f3f1c0 and
//    0x10f3ec40) draws LevelInfo.MapFloors[i].FloorMapTexture. Stock maps keep
//    those textures as <Map>_TXT.Minimap.<xxx>_minimap_<n>, 256 x 256 DXT5
//    with alpha: transparent outside the map, dark green for everything,
//    light green for the floor shown, numbered zones.
//  - It picks the floor by the player's Z: starting at 1, the index moves up
//    while MapFloors[index].FloorZ <= Z, then steps back one. MapFloors[0].FloorZ
//    is never read, and the list must rise.
//  - World to picture goes through LevelInfo.SnapshotCamera: translate by
//    -Position, multiply by the matrix whose rows are RotationRow0..2, swap
//    (a,b,c) to (b,c,a), then an orthographic box of half size
//    TargetDistance * tan(FOV/2) on both axes. u = (x+1)/2, v = (1-y)/2 over the
//    whole texture. Every stock map uses rows (0,1,0) (0,0,-1) (-1,0,0) and
//    FOV 45, which make u follow world X and v follow world Y.
//  - The interface package <Map>-i.utc holds Menu (map selection) and
//    Briefing (the lobby's plan of the map, 256 x 256, the same style with zone
//    numbers). The stock editor's SNAPSHOT command only dumped a top-down
//    Minimap#####.bmp and a .log of its corners for an artist to trace.
#include "LevelSnapshotModel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Minimap
{
    using Vec3 = std::array<double, 3>;
    using Bytes = Snapshot::Bytes;

    constexpr int kGameSize = 256;        // The stock map textures, and what the game's map page is laid out for.
    constexpr int kStockFov = 45;
    constexpr double kPi = 3.14159265358979323846;

    // LevelInfo.SnapCameraStruct.
    struct Camera
    {
        Vec3 position{};
        std::array<Vec3, 3> rows{ Vec3{ 0, 1, 0 }, Vec3{ 0, 0, -1 }, Vec3{ -1, 0, 0 } };
        int fov = kStockFov;
        double targetDistance = 0;
    };

    // Half the side of the square the picture covers, in world units.
    inline double HalfExtent(const Camera& c) { return std::tan(c.fov * kPi / 360.0) * c.targetDistance; }

    // A camera the game can use: positive size, finite numbers.
    inline bool Usable(const Camera& c)
    {
        if (c.fov <= 0 || c.fov >= 180 || !std::isfinite(c.targetDistance) || c.targetDistance <= 0) return false;
        for (double v : c.position) if (!std::isfinite(v)) return false;
        for (const auto& row : c.rows) for (double v : row) if (!std::isfinite(v)) return false;
        return HalfExtent(c) > 1;
    }

    // The stock orientation, the only one this tool draws for.
    inline bool TopDown(const Camera& c)
    {
        const Camera stock;
        for (int r = 0; r < 3; ++r) for (int k = 0; k < 3; ++k) if (std::abs(c.rows[r][k] - stock.rows[r][k]) > 1e-4) return false;
        return true;
    }

    struct UV { double u = 0, v = 0; };

    // Where a world point lands on the map texture, 0..1 across it, exactly as
    // the game's native computes it.
    inline UV Project(const Camera& c, const Vec3& world)
    {
        const double half = HalfExtent(c);
        if (!(half > 0)) throw std::runtime_error("The minimap camera has no size.");
        const Vec3 d{ world[0] - c.position[0], world[1] - c.position[1], world[2] - c.position[2] };
        Vec3 a{};
        for (int k = 0; k < 3; ++k) a[k] = d[0] * c.rows[0][k] + d[1] * c.rows[1][k] + d[2] * c.rows[2][k];
        const double x = a[1] / half, y = a[2] / half;
        return { (x + 1) / 2, (1 - y) / 2 };
    }

    // The same in pixels of a size x size picture (pixel centres at +0.5).
    inline std::pair<double, double> ToPixel(const Camera& c, const Vec3& world, int size)
    {
        const auto uv = Project(c, world);
        return { uv.u * size, uv.v * size };
    }

    // The world X/Y under a pixel, for the stock top-down camera.
    inline std::pair<double, double> FromPixel(const Camera& c, double x, double y, int size)
    {
        if (!TopDown(c)) throw std::runtime_error("Only the stock top-down minimap camera can be drawn for.");
        const double half = HalfExtent(c);
        return { c.position[0] + (x / size - 0.5) * 2 * half, c.position[1] + (y / size - 0.5) * 2 * half };
    }

    // An X/Y extent of the map.
    struct Bounds
    {
        double minX = std::numeric_limits<double>::infinity(), minY = std::numeric_limits<double>::infinity();
        double maxX = -std::numeric_limits<double>::infinity(), maxY = -std::numeric_limits<double>::infinity();
        bool Empty() const { return !(minX <= maxX && minY <= maxY); }
        void Add(double x, double y)
        {
            if (!std::isfinite(x) || !std::isfinite(y)) return;
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
        }
    };

    // The square around the map the picture covers: centred, with a margin
    // all round, at least `minimumHalf` across, and rounded to whole units so
    // the same map gives the same camera every time.
    struct Frame { double cx = 0, cy = 0, half = 0; };
    inline Frame Square(const Bounds& b, double margin = 0.04, double minimumHalf = 256)
    {
        if (b.Empty()) throw std::runtime_error("The map has no floors to draw yet: build its geometry first.");
        Frame f;
        f.cx = std::round((b.minX + b.maxX) / 2);
        f.cy = std::round((b.minY + b.maxY) / 2);
        const double reach = std::max({ b.maxX - f.cx, f.cx - b.minX, b.maxY - f.cy, f.cy - b.minY });
        f.half = std::ceil(std::max(reach * (1 + std::max(margin, 0.0)), minimumHalf));
        return f;
    }

    // The SnapshotCamera that shows exactly that square, in the stock form
    // (FOV 45, the camera as high above the origin as it is far from it).
    inline Camera CameraFor(const Frame& f)
    {
        if (!(f.half > 0) || !std::isfinite(f.cx) || !std::isfinite(f.cy)) throw std::runtime_error("The minimap frame is empty.");
        Camera c;
        c.fov = kStockFov;
        c.targetDistance = f.half / std::tan(kStockFov * kPi / 360.0);
        c.position = { f.cx, f.cy, c.targetDistance };
        return c;
    }
    inline Frame FrameOf(const Camera& c) { return { c.position[0], c.position[1], HalfExtent(c) }; }

    // The floor the game shows for a player at height z (see the header).
    inline int FloorIndex(const std::vector<double>& floorZ, double z)
    {
        if (floorZ.empty()) return -1;
        size_t index = 1;
        while (index < floorZ.size() && floorZ[index] <= z) ++index;
        return static_cast<int>(index) - 1;
    }

    // How the map's storeys (Design::Storeys) are gathered into the game's
    // floors: surfaces within kFloorTolerance are one floor (a player is
    // about 170 units tall), and a floor smaller than kFloorShare of the
    // broadest one is a landing, not a page of the map.
    constexpr double kFloorTolerance = 192, kFloorShare = 0.12;
    constexpr size_t kFloorLimit = 8;

    // The floor heights as the window lists them ("-128, 512") and back.
    inline std::string FloorList(const std::vector<double>& floors)
    {
        std::string text;
        for (size_t i = 0; i < floors.size(); ++i) text += (i ? ", " : "") + std::to_string(static_cast<long long>(std::llround(floors[i])));
        return text;
    }
    inline std::vector<double> ParseFloorList(const std::string& text)
    {
        std::vector<double> floors;
        size_t at = 0;
        while (at < text.size())
        {
            const size_t end = text.find_first_of(",;", at);
            std::string item = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            at = end == std::string::npos ? text.size() : end + 1;
            const auto first = item.find_first_not_of(" \t"), last = item.find_last_not_of(" \t");
            if (first == std::string::npos) continue;
            item = item.substr(first, last - first + 1);
            size_t used = 0;
            double value = 0;
            try { value = std::stod(item, &used); } catch (const std::exception&) { used = 0; }
            if (used != item.size() || !std::isfinite(value) || std::abs(value) > 1000000) throw std::runtime_error("Floor heights are numbers separated by commas, for example -128, 512.");
            floors.push_back(value);
        }
        std::sort(floors.begin(), floors.end());
        floors.erase(std::unique(floors.begin(), floors.end()), floors.end());
        if (floors.size() > 32) throw std::runtime_error("A minimap takes at most 32 floors.");
        return floors;
    }

    // The height band one picture shows: [lower, upper).
    struct Band
    {
        double lower = -std::numeric_limits<double>::infinity(), upper = std::numeric_limits<double>::infinity();
        bool Whole() const { return std::isinf(lower) && std::isinf(upper); }
        bool Contains(double z) const { return z >= lower && z < upper; }
    };
    // One band per storey, lowest first: each from just under its floor to
    // just under the next one, the top one open upwards, as the Storeys
    // palette divides a map.
    inline std::vector<Band> Bands(std::vector<double> bases)
    {
        std::sort(bases.begin(), bases.end());
        bases.erase(std::unique(bases.begin(), bases.end()), bases.end());
        std::vector<Band> out;
        for (size_t i = 0; i < bases.size(); ++i)
        {
            Band b;
            b.lower = bases[i] - 0.5;
            b.upper = i + 1 < bases.size() ? bases[i + 1] - 0.5 : std::numeric_limits<double>::infinity();
            out.push_back(b);
        }
        return out;
    }
    // MapFloors for those storeys: each floor's own height, rising. The game
    // compares a pawn's centre with it, which stands above its floor.
    inline std::vector<double> FloorHeights(const std::vector<Band>& bands)
    {
        std::vector<double> z;
        for (const auto& b : bands) z.push_back(std::isinf(b.lower) ? -65536.0 : std::round(b.lower + 0.5));
        for (size_t i = 1; i < z.size(); ++i) if (!(z[i] > z[i - 1])) throw std::runtime_error("The floors must rise one above another.");
        return z;
    }

    // A built BSP face (one node of the level's model), in world space.
    constexpr std::uint32_t kPolyInvisible = 0x00000001, kPolyPortal = 0x04000000, kPolyFakeBackdrop = 0x80000000;
    struct Polygon { std::vector<Vec3> points; Vec3 normal{}; std::uint32_t flags = 0; };
    enum class Kind { Skip, Silhouette, Floor, Wall, Obstacle };

    inline double MeanZ(const std::vector<Vec3>& points)
    {
        double z = 0;
        for (const auto& p : points) z += p[2];
        return points.empty() ? 0 : z / points.size();
    }

    // How a face shows on the plan of `band`. Upward faces are floors: the
    // band's own ones light, every other one part of the dark silhouette of
    // the whole map. Upright faces are walls where they rise from the band's
    // floor (up to `wallHeight`). Ceilings, hidden faces and portals are not
    // drawn.
    inline Kind Classify(const Polygon& p, const Band& band, double wallHeight = 160)
    {
        if (p.points.size() < 3 || (p.flags & (kPolyInvisible | kPolyPortal | kPolyFakeBackdrop))) return Kind::Skip;
        const double nz = p.normal[2];
        if (!std::isfinite(nz)) return Kind::Skip;
        if (nz >= 0.3) return band.Contains(MeanZ(p.points)) ? Kind::Floor : Kind::Silhouette;
        if (std::abs(nz) < 0.3)
        {
            if (band.Whole()) return Kind::Wall;
            double low = p.points[0][2], high = low;
            for (const auto& q : p.points) { low = std::min(low, q[2]); high = std::max(high, q[2]); }
            const double top = std::min(band.upper, band.lower + wallHeight);
            return high > band.lower + 1 && low < top ? Kind::Wall : Kind::Skip;
        }
        return Kind::Skip;
    }

    // A static mesh as the plan sees it: the corners of its box in the world.
    struct MeshBox { std::vector<Vec3> corners; };
    inline std::pair<double, double> HeightOf(const MeshBox& m)
    {
        double low = std::numeric_limits<double>::infinity(), high = -low;
        for (const auto& c : m.corners) { low = std::min(low, c[2]); high = std::max(high, c[2]); }
        return { low, high };
    }
    // A flat mesh whose top is in the band is floor to walk on; one standing
    // in the band's lower part is something in the way; the rest of the
    // mesh floors join the silhouette, like the faces.
    inline Kind Classify(const MeshBox& m, const Band& band, double wallHeight = 160, double slab = 48)
    {
        if (m.corners.size() < 3) return Kind::Skip;
        const auto [low, high] = HeightOf(m);
        if (!std::isfinite(low) || !std::isfinite(high)) return Kind::Skip;
        const bool flat = high - low <= slab;
        if (flat) return band.Contains(high) ? Kind::Floor : Kind::Silhouette;
        if (band.Whole()) return Kind::Obstacle;
        const double top = std::min(band.upper, band.lower + wallHeight);
        return high > band.lower + 1 && low < top ? Kind::Obstacle : Kind::Skip;
    }

    // The X/Y outline of a set of points (convex hull, counter-clockwise).
    inline std::vector<std::pair<double, double>> Footprint(const std::vector<Vec3>& points)
    {
        std::vector<std::pair<double, double>> p;
        for (const auto& v : points) p.emplace_back(v[0], v[1]);
        std::sort(p.begin(), p.end());
        p.erase(std::unique(p.begin(), p.end()), p.end());
        if (p.size() < 3) return p;
        auto cross = [](const auto& o, const auto& a, const auto& b) { return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first); };
        std::vector<std::pair<double, double>> hull(2 * p.size());
        size_t k = 0;
        for (size_t i = 0; i < p.size(); ++i) { while (k >= 2 && cross(hull[k - 2], hull[k - 1], p[i]) <= 0) --k; hull[k++] = p[i]; }
        for (size_t i = p.size() - 1, t = k + 1; i > 0; --i) { while (k >= t && cross(hull[k - 2], hull[k - 1], p[i - 1]) <= 0) --k; hull[k++] = p[i - 1]; }
        hull.resize(k - 1);
        return hull;
    }

    // What frames the picture: every face and mesh that is a floor anywhere,
    // and the markers, so a spawn on a roof is not cut off.
    inline Bounds Extent(const std::vector<Polygon>& faces, const std::vector<MeshBox>& meshes, const std::vector<Vec3>& markers)
    {
        Bounds b;
        const Band all;
        for (const auto& f : faces) if (Classify(f, all) == Kind::Silhouette || Classify(f, all) == Kind::Floor) for (const auto& p : f.points) b.Add(p[0], p[1]);
        for (const auto& m : meshes) if (Classify(m, all) != Kind::Skip) for (const auto& p : m.corners) b.Add(p[0], p[1]);
        for (const auto& p : markers) b.Add(p[0], p[1]);
        return b;
    }

    // What frames the picture when the map has spawns or objectives: those,
    // and the floors around them out to `slack` of their spread on every
    // side. An outdoor map's sea bed or terrain box runs far past where
    // anyone plays and would shrink the play area to a speck.
    inline Bounds GameplayExtent(const std::vector<Polygon>& faces, const std::vector<MeshBox>& meshes, const std::vector<Vec3>& markers, double slack = 0.25, double reach = 512)
    {
        Bounds play;
        for (const auto& p : markers) play.Add(p[0], p[1]);
        if (play.Empty()) return Extent(faces, meshes, markers);
        const double padX = std::max((play.maxX - play.minX) * slack, reach), padY = std::max((play.maxY - play.minY) * slack, reach);
        Bounds around = play;
        auto inside = [&](double x, double y) { return x >= play.minX - padX && x <= play.maxX + padX && y >= play.minY - padY && y <= play.maxY + padY; };
        const Band all;
        for (const auto& f : faces)
        {
            const auto kind = Classify(f, all);
            if (kind != Kind::Floor && kind != Kind::Silhouette) continue;
            bool contained = true;
            for (const auto& p : f.points) contained = contained && inside(p[0], p[1]);
            if (contained) for (const auto& p : f.points) around.Add(p[0], p[1]);
        }
        for (const auto& m : meshes)
        {
            bool contained = Classify(m, all) != Kind::Skip;
            for (const auto& p : m.corners) contained = contained && inside(p[0], p[1]);
            if (contained) for (const auto& p : m.corners) around.Add(p[0], p[1]);
        }
        return around;
    }

    // A face wider or deeper than the whole picture (a sky, a sea, the floor
    // of the box an outdoor map stands in) says nothing about the plan and
    // would paint over all of it.
    inline bool Backdrop(const Polygon& p, const Frame& f)
    {
        Bounds b;
        for (const auto& q : p.points) b.Add(q[0], q[1]);
        return !b.Empty() && (b.maxX - b.minX > 2 * f.half || b.maxY - b.minY > 2 * f.half);
    }

    // Gameplay actors drawn over the plan.
    enum class MarkerKind { Objective, Flag, DropZone, SpySpawn, MercSpawn };
    // `objective` is the place of the SObjective a trigger serves in its
    // mission (0 first), or -1 when no objective lists it.
    struct Marker { MarkerKind kind = MarkerKind::Objective; Vec3 position{}; int yaw = 0; std::string label; int objective = -1; };
    // Which markers a band's picture shows: those standing on its floor.
    // Markers float above their floor by up to a pawn's height.
    inline bool Shows(const Band& band, const Marker& m) { return band.Whole() || (m.position[2] >= band.lower - 16 && m.position[2] < band.upper); }

    // Objective triggers carry their objective's number, as the stock
    // pictures number their zones, so the terminals of one objective share
    // it; triggers no objective lists are numbered after those, in order.
    inline void Number(std::vector<Marker>& markers)
    {
        int next = 1;
        for (const auto& m : markers) if (m.kind == MarkerKind::Objective && m.objective >= 0) next = std::max(next, m.objective + 2);
        for (auto& m : markers)
        {
            if (m.kind != MarkerKind::Objective || !m.label.empty()) continue;
            m.label = std::to_string(m.objective >= 0 ? m.objective + 1 : next++);
        }
    }

    // The yaw (65536 units a turn) as a direction on the picture.
    inline std::pair<double, double> Heading(int yaw)
    {
        const double a = (yaw & 0xffff) * 2 * kPi / 65536.0;
        return { std::cos(a), std::sin(a) };
    }

    struct Rgba { std::uint8_t r = 0, g = 0, b = 0, a = 0; };
    struct Palette
    {
        const char* name;
        Rgba background, silhouette, floor, wall, obstacle, objective, spy, merc, text;
    };
    // The first is the stock look, measured from aqa_minimap_0.
    inline const std::array<Palette, 3>& Palettes()
    {
        static const std::array<Palette, 3> palettes{ {
            { "Stock (green, transparent)", { 0, 0, 0, 0 }, { 24, 34, 18, 255 }, { 132, 160, 120, 255 }, { 214, 226, 204, 255 }, { 88, 110, 80, 255 }, { 240, 240, 230, 255 }, { 90, 170, 255, 255 }, { 255, 110, 80, 255 }, { 250, 250, 245, 255 } },
            { "Blueprint", { 18, 40, 72, 255 }, { 30, 62, 104, 255 }, { 52, 98, 150, 255 }, { 230, 240, 255, 255 }, { 120, 160, 210, 255 }, { 255, 220, 90, 255 }, { 120, 220, 255, 255 }, { 255, 130, 110, 255 }, { 255, 255, 255, 255 } },
            { "Grey", { 0, 0, 0, 0 }, { 40, 40, 40, 255 }, { 170, 170, 170, 255 }, { 245, 245, 245, 255 }, { 110, 110, 110, 255 }, { 255, 255, 255, 255 }, { 120, 180, 255, 255 }, { 255, 120, 100, 255 }, { 255, 255, 255, 255 } },
        } };
        return palettes;
    }

    // A number as the property importer reads it: plain decimals, no
    // exponent, no trailing zeros.
    inline std::string Number(double v)
    {
        if (!std::isfinite(v)) throw std::runtime_error("A minimap value is not a number.");
        char text[64];
        std::snprintf(text, sizeof text, "%.3f", v);
        std::string s = text;
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        if (s == "-0") s = "0";
        return s;
    }
    inline std::string VectorText(const Vec3& v) { return "(X=" + Number(v[0]) + ",Y=" + Number(v[1]) + ",Z=" + Number(v[2]) + ")"; }

    // LevelInfo.SnapshotCamera as ImportText takes it.
    inline std::string CameraText(const Camera& c)
    {
        if (!Usable(c)) throw std::runtime_error("The minimap camera is not usable.");
        return "(Position=" + VectorText(c.position) + ",RotationRow0=" + VectorText(c.rows[0]) + ",RotationRow1=" + VectorText(c.rows[1])
             + ",RotationRow2=" + VectorText(c.rows[2]) + ",FOV=" + std::to_string(c.fov) + ",TargetDistance=" + Number(c.targetDistance) + ")";
    }
    // LevelInfo.MapFloors: (height, texture object path) per floor, rising.
    inline std::string FloorsText(const std::vector<std::pair<double, std::string>>& floors)
    {
        if (floors.empty()) throw std::runtime_error("A minimap needs at least one floor.");
        std::string text = "(";
        for (size_t i = 0; i < floors.size(); ++i)
        {
            const auto& path = floors[i].second;
            if (path.empty() || path.find_first_of(" '\"(),") != std::string::npos) throw std::runtime_error("Invalid minimap texture name: " + path);
            if (i && !(floors[i].first > floors[i - 1].first)) throw std::runtime_error("The floors must rise one above another.");
            text += (i ? ",(" : "(") + std::string("FloorZ=") + Number(floors[i].first) + ",FloorMapTexture=Texture'" + path + "')";
        }
        return text + ")";
    }

    // An uncompressed 32-bit TGA, 8 alpha bits, from top-down RGBA rows:
    // what the editor's importer makes an alpha texture of. The rows are
    // stored bottom first, because that importer ignores the header's
    // top-left flag and turned a top-first picture upside down.
    inline Bytes Tga32(int width, int height, const Bytes& rgbaTopDown)
    {
        if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || rgbaTopDown.size() != static_cast<size_t>(width) * height * 4)
            throw std::runtime_error("Minimap pixels do not match their size.");
        Bytes out(18, 0);
        out[2] = 2;
        out[12] = static_cast<std::uint8_t>(width); out[13] = static_cast<std::uint8_t>(width >> 8);
        out[14] = static_cast<std::uint8_t>(height); out[15] = static_cast<std::uint8_t>(height >> 8);
        out[16] = 32; out[17] = 0x08; // 8 alpha bits, bottom row first.
        out.reserve(out.size() + rgbaTopDown.size());
        for (int y = height - 1; y >= 0; --y)
            for (size_t i = static_cast<size_t>(y) * width * 4, end = i + static_cast<size_t>(width) * 4; i < end; i += 4)
            {
                out.push_back(rgbaTopDown[i + 2]); out.push_back(rgbaTopDown[i + 1]); out.push_back(rgbaTopDown[i]); out.push_back(rgbaTopDown[i + 3]);
            }
        return out;
    }

    // What the editor hands the drawing: the built faces, the static meshes,
    // and what the map's LevelInfo says about its minimap now.
    struct Scene
    {
        std::vector<Polygon> faces;
        std::vector<MeshBox> meshes;
        Camera camera;                                        // LevelInfo.SnapshotCamera
        std::vector<std::pair<double, std::string>> floors;   // LevelInfo.MapFloors: FloorZ and texture path
        std::string package;                                  // the map's own package, where Floor textures go
    };

    // The texture names this tool owns in the map package and the interface package.
    inline std::string FloorTextureName(size_t index) { return "Floor" + std::to_string(index); }
    constexpr const char* kTextureGroup = "Minimap";
    constexpr const char* kBriefingTexture = "Briefing";

    // What the saved interface package must hold after the Briefing picture
    // went in: the texture at its size, and nothing of the package lost.
    inline Snapshot::Texture VerifyTexture(const Bytes& package, const std::string& name, int size, size_t previousExportCount)
    {
        const auto p = Snapshot::Parse(package);
        const auto* e = p.Find(name, "Texture");
        if (!e) throw std::runtime_error("The saved package has no " + name + " texture.");
        const auto t = Snapshot::DescribeTexture(package, p, *e);
        if (t.width != size || t.height != size)
            throw std::runtime_error("The " + name + " texture is " + std::to_string(t.width) + " x " + std::to_string(t.height) + ", not " + std::to_string(size) + " x " + std::to_string(size) + ".");
        if (t.mips < 1) throw std::runtime_error("The " + name + " texture has no image data.");
        if (p.exports.size() < previousExportCount)
            throw std::runtime_error("The saved package holds " + std::to_string(p.exports.size()) + " objects where the previous one held " + std::to_string(previousExportCount) + "; the snapshot, loading screens or map settings would be lost.");
        return t;
    }
}
