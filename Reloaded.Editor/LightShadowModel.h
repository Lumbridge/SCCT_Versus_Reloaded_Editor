#pragma once
#include "LocalLightingMatch.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Light and Shadow map: how lit each patch of walkable floor is, as an
// estimate of where a spy can hide. Pure maths, no editor calls; the native
// reads are in LightShadowNative.inl and the drawing in LightShadowMap.cpp.
//
// What it estimates, and what it does not:
// - Baked: the floor's own lightmap texel at the patch (Build Lighting's static
//   lights and zone ambient), as luminance. UE2 lightmaps are drawn at double
//   brightness (modulate 2x), so a stored 128 reads as 255 on screen.
// - Lights: the in-game and dynamic lights (the ones the game draws on actors,
//   the Lighting Budget's "in game" set) reaching a point at spy chest height
//   above the patch: brightness times the light's colour, falling off with the
//   square of the distance left to its reach, and only when the BSP does not block the line to the light.
//   Static meshes, movers, spotlight cones, projectors and light animation are
//   not taken into account.
// - The game's own spy visibility formula is not known; the bands are a
//   guide for where to look, not what a merc will see. Playtest to confirm.
namespace LightShadow
{
using Point = LocalLightingMatch::Point;

enum class Source { Combined = 0, Lights = 1, Baked = 2 };
enum class Band { Hidden = 0, Partial = 1, Exposed = 2 };

inline const char* SourceName(Source source)
{
    switch (source)
    {
    case Source::Lights: return "in-game lights only";
    case Source::Baked: return "baked lightmaps only";
    default: return "brighter of baked lightmaps and in-game lights";
    }
}
inline const char* BandName(Band band)
{
    switch (band)
    {
    case Band::Hidden: return "hidden";
    case Band::Partial: return "partly visible";
    default: return "exposed";
    }
}

// [LightShadowMap] in Reloaded_Editor.ini. Brightness runs 0 (black) to 255.
struct Settings
{
    int hiddenBelow = 40;    // darker than this: good spy cover
    int exposedFrom = 100;   // this bright or more: exposed
    int cell = 64;           // floor patch size in units
    Source source = Source::Combined;
    int chest = 48;          // height above the floor lights are measured at
};
inline Settings Clamp(Settings s)
{
    s.hiddenBelow = std::clamp(s.hiddenBelow, 1, 254);
    s.exposedFrom = std::clamp(s.exposedFrom, s.hiddenBelow + 1, 255);
    s.cell = std::clamp(s.cell, 16, 512);
    if (s.source != Source::Lights && s.source != Source::Baked) s.source = Source::Combined;
    s.chest = std::clamp(s.chest, 0, 160);
    return s;
}
inline Band Classify(double value, const Settings& s)
{
    if (!(value >= s.hiddenBelow)) return Band::Hidden; // NaN counts as dark
    return value >= s.exposedFrom ? Band::Exposed : Band::Partial;
}

// A lightmap atlas as the editor stores it: TEXF_RGBA8, B,G,R,A in memory.
struct Image
{
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;
};
inline double Luminance(uint32_t bgra)
{
    const double b = bgra & 255, g = (bgra >> 8) & 255, r = (bgra >> 16) & 255;
    return 0.299 * r + 0.587 * g + 0.114 * b;
}
// Bilinear luminance at a lightmap UV (0..1 across the atlas), texel centres
// at half-texel offsets, clamped at the edges.
inline double SampleLuminance(const Image& image, double u, double v)
{
    if (image.width <= 0 || image.height <= 0 || image.pixels.size() < size_t(image.width) * image.height) return -1;
    if (!std::isfinite(u) || !std::isfinite(v)) return -1;
    const double x = std::clamp(u * image.width - 0.5, 0.0, double(image.width - 1));
    const double y = std::clamp(v * image.height - 0.5, 0.0, double(image.height - 1));
    const int x0 = int(x), y0 = int(y);
    const int x1 = (std::min)(x0 + 1, image.width - 1), y1 = (std::min)(y0 + 1, image.height - 1);
    const double fx = x - x0, fy = y - y0;
    auto at = [&](int px, int py) { return Luminance(image.pixels[size_t(py) * image.width + px]); };
    const double top = at(x0, y0) * (1 - fx) + at(x1, y0) * fx;
    const double bottom = at(x0, y1) * (1 - fx) + at(x1, y1) * fx;
    return top * (1 - fy) + bottom * fy;
}
// Stored lightmap luminance to on-screen brightness (modulate 2x).
inline double BakedBrightness(double stored) { return stored < 0 ? -1 : (std::min)(255.0, stored * 2); }

// UE2's light colour (FGetHSV's hue and saturation; saturation 255 is white),
// as the share of white's luminance it keeps: a pure blue light is dimmer.
inline double ColourShare(int hue, int saturation)
{
    hue = std::clamp(hue, 0, 255);
    const double s = std::clamp(saturation, 0, 255) / 255.0;
    double r, g, b;
    if (hue < 86) { r = (85 - hue) / 85.0; g = hue / 85.0; b = 0; }
    else if (hue < 171) { r = 0; g = (170 - hue) / 85.0; b = (hue - 85) / 85.0; }
    else { r = (hue - 170) / 85.0; g = 0; b = (255 - hue) / 84.0; }
    r += s * (1 - r); g += s * (1 - g); b += s * (1 - b);
    return std::clamp(0.299 * r + 0.587 * g + 0.114 * b, 0.0, 1.0);
}

struct Light
{
    Point at;
    double radius = 0;      // reach in units (the in-game render radius)
    double brightness = 0;  // LightBrightness 0..255 times ColourShare
};
// One light's brightness at a point, before line of sight: falling off with
// the square of the share of its reach left, to zero at the edge (a quarter
// of full brightness half way out).
inline double LightAt(const Light& light, const Point& p)
{
    if (!(light.radius > 0) || !(light.brightness > 0)) return 0;
    const Point d = p - light.at;
    const double distance = std::sqrt(Dot(d, d));
    if (!std::isfinite(distance) || distance >= light.radius) return 0;
    const double left = 1 - distance / light.radius;
    return light.brightness * left * left;
}

// A walkable BSP floor triangle with its lightmap UVs; atlas -1 when the
// surface has no baked lighting (not built, or unlit).
struct Floor
{
    Point a, b, c;
    double u[3]{}, v[3]{};
    int atlas = -1;
};
// Steeper than this is a wall or a ramp a spy cannot stand on (UE2's walkable
// floor normal).
constexpr double kWalkable = 0.7;

inline Point Normal(const Floor& f)
{
    const Point e = f.b - f.a, g = f.c - f.a;
    Point n{ e.y * g.z - e.z * g.y, e.z * g.x - e.x * g.z, e.x * g.y - e.y * g.x };
    const double length = std::sqrt(Dot(n, n));
    if (!(length > 1e-9)) return {};
    return n * (1 / length);
}
// Faces up, either winding: BSP render vertices wind either way.
inline bool Walkable(const Floor& f) { return std::abs(Normal(f).z) >= kWalkable; }

struct Scene
{
    std::vector<Floor> floors;
    std::vector<Image> atlases;
    std::vector<Light> lights;
    std::vector<LocalLightingMatch::BspNode> bsp; // empty: no line-of-sight test
    bool rootOutside = true;
};

struct Cell
{
    Point at;            // on the floor, at the patch centre
    double baked = -1;   // on-screen brightness of the floor's lightmap, -1 unknown
    double lights = 0;   // in-game lights at chest height
    double value = 0;    // what the band is decided by
    Band band = Band::Hidden;
};

// Whether a light reaches a point: the BSP must not block the line. The light
// end stops short, since lamps often sit inside their fitting's geometry.
inline bool Reaches(const Scene& scene, const Light& light, const Point& p)
{
    if (scene.bsp.empty()) return true;
    Point d = light.at - p;
    const double length = std::sqrt(Dot(d, d));
    if (length < 1e-6) return true;
    const double keep = (std::max)(0.0, length - (std::min)(16.0, length * 0.5));
    return LocalLightingMatch::ClearSegment(scene.bsp, scene.rootOutside, p, p + d * (keep / length));
}
inline double LightsAt(const Scene& scene, const Point& p)
{
    double total = 0;
    for (const auto& light : scene.lights)
    {
        const double amount = LightAt(light, p);
        if (amount > 0 && Reaches(scene, light, p)) total += amount;
    }
    return (std::min)(total, 255.0);
}
inline double Combine(double baked, double lights, Source source)
{
    if (source == Source::Lights) return lights;
    if (source == Source::Baked) return baked;
    return (std::max)(baked, lights);
}

// Bands sampled patches (Sample's caller does this, and again for new
// thresholds or source on the same geometry). (new thresholds or source, same geometry).
// Patches without baked lighting count as dark in the baked-only view.
inline void ApplyBands(std::vector<Cell>& cells, Settings settings)
{
    settings = Clamp(settings);
    for (auto& cell : cells)
    {
        cell.value = Combine((std::max)(cell.baked, 0.0), cell.lights, settings.source);
        cell.band = Classify(cell.value, settings);
    }
}

struct Result
{
    std::vector<Cell> cells;
    bool truncated = false;
};
constexpr size_t kMaxCells = 200000;

// Samples every walkable floor triangle on a world grid of settings.cell:
// one patch per grid square per floor height (stacked storeys each get theirs).
inline Result Sample(const Scene& scene, Settings settings)
{
    settings = Clamp(settings);
    Result result;
    const double size = settings.cell;
    // Grid square and height band to the patch already sampled there.
    struct Key { long long x, y, z; bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; } };
    struct Hash { size_t operator()(const Key& k) const { return size_t(k.x * 73856093LL) ^ size_t(k.y * 19349663LL) ^ size_t(k.z * 83492791LL); } };
    std::unordered_map<Key, size_t, Hash> seen;
    // Smallest floors first: when a map has more patches than the cap, the
    // ones left out are on its vast outer slabs rather than in its rooms.
    std::vector<std::pair<double, size_t>> order;
    order.reserve(scene.floors.size());
    for (size_t i = 0; i < scene.floors.size(); ++i)
    {
        const auto& f = scene.floors[i];
        order.push_back({ std::abs((f.b.x - f.a.x) * (f.c.y - f.a.y) - (f.b.y - f.a.y) * (f.c.x - f.a.x)), i });
    }
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [area, index] : order)
    {
        const auto& f = scene.floors[index];
        if (!Walkable(f)) continue;
        const double minX = (std::min)({ f.a.x, f.b.x, f.c.x }), maxX = (std::max)({ f.a.x, f.b.x, f.c.x });
        const double minY = (std::min)({ f.a.y, f.b.y, f.c.y }), maxY = (std::max)({ f.a.y, f.b.y, f.c.y });
        if (!std::isfinite(minX + maxX + minY + maxY) || maxX - minX > 1e6 || maxY - minY > 1e6) continue;
        // 2D barycentrics in XY.
        const double det = (f.b.y - f.c.y) * (f.a.x - f.c.x) + (f.c.x - f.b.x) * (f.a.y - f.c.y);
        if (std::abs(det) < 1e-9) continue;
        const long long x0 = (long long)std::ceil(minX / size - 0.5), x1 = (long long)std::floor(maxX / size - 0.5);
        const long long y0 = (long long)std::ceil(minY / size - 0.5), y1 = (long long)std::floor(maxY / size - 0.5);
        for (long long iy = y0; iy <= y1; ++iy)
            for (long long ix = x0; ix <= x1; ++ix)
            {
                const double x = (ix + 0.5) * size, y = (iy + 0.5) * size;
                const double wa = ((f.b.y - f.c.y) * (x - f.c.x) + (f.c.x - f.b.x) * (y - f.c.y)) / det;
                const double wb = ((f.c.y - f.a.y) * (x - f.c.x) + (f.a.x - f.c.x) * (y - f.c.y)) / det;
                const double wc = 1 - wa - wb;
                const double e = -1e-9;
                if (wa < e || wb < e || wc < e) continue;
                const double z = f.a.z * wa + f.b.z * wb + f.c.z * wc;
                const Key key{ ix, iy, (long long)std::floor(z / 32) };
                if (seen.count(key)) continue;
                if (result.cells.size() >= kMaxCells) { result.truncated = true; ApplyBands(result.cells, settings); return result; }
                seen.emplace(key, result.cells.size());
                Cell cell;
                cell.at = { x, y, z };
                if (f.atlas >= 0 && size_t(f.atlas) < scene.atlases.size())
                    cell.baked = BakedBrightness(SampleLuminance(scene.atlases[f.atlas],
                        f.u[0] * wa + f.u[1] * wb + f.u[2] * wc, f.v[0] * wa + f.v[1] * wb + f.v[2] * wc));
                cell.lights = LightsAt(scene, { x, y, z + settings.chest });
                result.cells.push_back(cell);
            }
    }
    ApplyBands(result.cells, settings);
    return result;
}
struct Summary
{
    std::array<size_t, 3> bands{};
    size_t total = 0, unbaked = 0;
};
inline Summary Summarise(const std::vector<Cell>& cells)
{
    Summary s;
    for (const auto& cell : cells)
    {
        ++s.bands[size_t(cell.band)];
        ++s.total;
        s.unbaked += cell.baked < 0;
    }
    return s;
}
inline int Percent(size_t part, size_t total)
{
    return total ? int((part * 100 + total / 2) / total) : 0;
}

// The brightness below which a share (0..1) of the patches fall, for the log
// and for choosing thresholds.
inline double Percentile(const std::vector<Cell>& cells, double share)
{
    if (cells.empty()) return 0;
    std::vector<double> values;
    values.reserve(cells.size());
    for (const auto& cell : cells) values.push_back(cell.value);
    const size_t at = (std::min)(values.size() - 1, size_t(std::clamp(share, 0.0, 1.0) * double(values.size() - 1) + 0.5));
    std::nth_element(values.begin(), values.begin() + at, values.end());
    return values[at];
}

// The legend drawn in the viewports and shown in the settings window.
inline std::vector<std::string> Legend(const Summary& s, const Settings& settings, bool truncated, bool stale)
{
    std::vector<std::string> lines;
    const char* source = settings.source == Source::Lights ? "in-game lights" : settings.source == Source::Baked ? "baked lighting" : "baked + in-game lights";
    lines.push_back("Light and Shadow (" + std::string(source) + "): " + std::to_string(s.total) + " floor patches of "
        + std::to_string(settings.cell) + " uu" + (stale ? ", map changed, refreshing" : ""));
    lines.push_back("Blue  hidden, darker than " + std::to_string(settings.hiddenBelow) + ": " + std::to_string(Percent(s.bands[0], s.total)) + "%");
    lines.push_back("Yellow  partly visible: " + std::to_string(Percent(s.bands[1], s.total)) + "%");
    lines.push_back("Red  exposed, " + std::to_string(settings.exposedFrom) + " or brighter: " + std::to_string(Percent(s.bands[2], s.total)) + "%");
    if (!s.total) lines.push_back("No walkable BSP floor found: build the geometry. Static mesh floors are not sampled.");
    if (truncated) lines.push_back("Too many patches: only the first " + std::to_string(kMaxCells) + " are shown; use a larger patch size.");
    if (s.total && s.unbaked * 2 > s.total && settings.source != Source::Lights)
        lines.push_back("Most floors have no baked lighting yet: Build > Lighting first.");
    return lines;
}
// The point under a patch readout: brightness and its parts.
inline std::string Describe(const Cell& cell)
{
    auto number = [](double v) { return std::to_string(int(std::lround(v))); };
    return std::string(BandName(cell.band)) + ", brightness " + number(cell.value) + " (baked "
        + (cell.baked < 0 ? std::string("none") : number(cell.baked)) + ", in-game lights " + number(cell.lights) + ")";
}

// The lines a patch is drawn with, lifted slightly off the floor: a square
// inset from its neighbours, plus one diagonal when partly visible and both
// when exposed, so the bands read without colour too.
inline std::vector<std::pair<Point, Point>> Outline(const Cell& cell, double size, double lift = 2)
{
    const double h = size * 0.42;
    const double z = cell.at.z + lift;
    const Point p[4] = { { cell.at.x - h, cell.at.y - h, z }, { cell.at.x + h, cell.at.y - h, z },
                         { cell.at.x + h, cell.at.y + h, z }, { cell.at.x - h, cell.at.y + h, z } };
    std::vector<std::pair<Point, Point>> lines{ { p[0], p[1] }, { p[1], p[2] }, { p[2], p[3] }, { p[3], p[0] } };
    if (cell.band != Band::Hidden) lines.push_back({ p[0], p[2] });
    if (cell.band == Band::Exposed) lines.push_back({ p[1], p[3] });
    return lines;
}

// The patch nearest a point within half a patch horizontally, preferring the
// floor at or just below it (so a 2D top view picks the highest floor).
inline const Cell* At(const std::vector<Cell>& cells, double x, double y, double size, double z = 1e18)
{
    const Cell* best = nullptr;
    for (const auto& cell : cells)
    {
        if (std::abs(cell.at.x - x) > size / 2 || std::abs(cell.at.y - y) > size / 2) continue;
        if (cell.at.z > z + 1) continue;
        if (!best || cell.at.z > best->at.z) best = &cell;
    }
    return best;
}
}
