#include "../Reloaded.Editor/LightShadowModel.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using namespace LightShadow;

namespace
{
    bool Near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
    uint32_t Grey(int value) { return 0xFF000000u | uint32_t(value) << 16 | uint32_t(value) << 8 | uint32_t(value); }
    // A flat square floor at height z, as two triangles, lightmapped onto atlas 0.
    void Square(Scene& scene, double x0, double y0, double x1, double y1, double z, int atlas = 0)
    {
        Floor a{ { x0, y0, z }, { x1, y0, z }, { x1, y1, z }, { 0, 1, 1 }, { 0, 0, 1 }, atlas };
        Floor b{ { x0, y0, z }, { x1, y1, z }, { x0, y1, z }, { 0, 1, 0 }, { 0, 1, 1 }, atlas };
        scene.floors.push_back(a);
        scene.floors.push_back(b);
    }
}

int main()
{
    // Settings: clamped into a usable, ordered range.
    {
        Settings s; s.hiddenBelow = 0; s.exposedFrom = -5; s.cell = 3; s.source = Source(9); s.chest = 999;
        s = Clamp(s);
        assert(s.hiddenBelow == 1 && s.exposedFrom == 2 && s.cell == 16 && s.source == Source::Combined && s.chest == 160);
        Settings t; t.hiddenBelow = 300; t.exposedFrom = 10; t.cell = 4096;
        t = Clamp(t);
        assert(t.hiddenBelow == 254 && t.exposedFrom == 255 && t.cell == 512);
        Settings d = Clamp(Settings{});
        assert(d.hiddenBelow == 40 && d.exposedFrom == 100 && d.cell == 64);
    }
    // Bands: below hidden is hidden, from exposed up is exposed; NaN is dark.
    {
        Settings s;
        assert(Classify(0, s) == Band::Hidden && Classify(39.9, s) == Band::Hidden);
        assert(Classify(40, s) == Band::Partial && Classify(99.9, s) == Band::Partial);
        assert(Classify(100, s) == Band::Exposed && Classify(255, s) == Band::Exposed);
        assert(Classify(std::nan(""), s) == Band::Hidden);
        assert(std::string(BandName(Band::Partial)) == "partly visible");
    }
    // Lightmap texels: luminance from B,G,R,A, bilinear between texel centres.
    {
        assert(Near(Luminance(Grey(200)), 200));
        assert(Near(Luminance(0xFFFF0000u), 0.299 * 255)); // red is the third byte
        assert(Near(Luminance(0xFF0000FFu), 0.114 * 255)); // blue the first
        Image image{ 2, 1, { Grey(0), Grey(100) } };
        assert(Near(SampleLuminance(image, 0.25, 0.5), 0));
        assert(Near(SampleLuminance(image, 0.75, 0.5), 100));
        assert(Near(SampleLuminance(image, 0.5, 0.5), 50));
        assert(Near(SampleLuminance(image, -3, 0.5), 0) && Near(SampleLuminance(image, 9, 0.5), 100));
        assert(SampleLuminance(Image{}, 0.5, 0.5) < 0);
        assert(SampleLuminance(image, std::nan(""), 0.5) < 0);
        assert(Near(BakedBrightness(60), 120) && Near(BakedBrightness(200), 255) && BakedBrightness(-1) < 0);
    }
    // Light colour: white keeps everything, a saturated blue keeps little.
    {
        assert(Near(ColourShare(0, 255), 1) && Near(ColourShare(170, 255), 1));
        assert(Near(ColourShare(0, 0), 0.299));          // pure red
        assert(Near(ColourShare(85, 0), 0.587));         // pure green
        assert(Near(ColourShare(170, 0), 0.114));        // pure blue
        assert(ColourShare(160, 90) > ColourShare(160, 0) && ColourShare(160, 90) < 1);
    }
    // Falloff: full at the light, a quarter at half its reach, none at or past it.
    {
        Light light{ { 0, 0, 100 }, 400, 200 };
        assert(Near(LightAt(light, { 0, 0, 100 }), 200));
        assert(Near(LightAt(light, { 200, 0, 100 }), 50));
        assert(Near(LightAt(light, { 0, 300, 100 }), 200.0 / 16));
        assert(Near(LightAt(light, { 0, 0, 500 }), 0) && Near(LightAt(light, { 0, 900, 100 }), 0));
        assert(Near(LightAt(Light{ { 0, 0, 0 }, 0, 200 }, { 0, 0, 0 }), 0));
    }
    // Walkable: up-facing in either winding, walls and steep ramps are not.
    {
        Floor up{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } }, down{ { 0, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 } };
        Floor wall{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 } }, ramp{ { 0, 0, 0 }, { 1, 0, 1.2 }, { 0, 1, 0 } };
        assert(Walkable(up) && Walkable(down) && !Walkable(wall) && !Walkable(ramp));
        assert(!Walkable(Floor{})); // degenerate
    }
    // Sampling a 256-unit floor on a 64-unit grid: 16 patches at grid centres.
    {
        Scene scene;
        scene.atlases.push_back(Image{ 1, 1, { Grey(30) } }); // stored 30 reads as 60
        Square(scene, 0, 0, 256, 256, 10);
        Settings s;
        auto result = Sample(scene, s);
        assert(result.cells.size() == 16 && !result.truncated);
        for (const auto& cell : result.cells)
        {
            assert(std::fmod(cell.at.x - 32, 64) == 0 && std::fmod(cell.at.y - 32, 64) == 0 && cell.at.z == 10);
            assert(Near(cell.baked, 60) && Near(cell.lights, 0) && Near(cell.value, 60) && cell.band == Band::Partial);
        }
        // Bigger patches: fewer of them; the edge shared by the two triangles
        // gives no duplicates.
        s.cell = 128;
        assert(Sample(scene, s).cells.size() == 4);
        // A wall is not floor.
        Scene walls;
        walls.floors.push_back(Floor{ { 0, 0, 0 }, { 256, 0, 0 }, { 0, 0, 256 } });
        assert(Sample(walls, Settings{}).cells.empty());
    }
    // Stacked storeys each get patches; touching coplanar floors do not double up.
    {
        Scene scene;
        scene.atlases.push_back(Image{ 1, 1, { Grey(10) } });
        Square(scene, 0, 0, 128, 128, 0);
        Square(scene, 0, 0, 128, 128, 256);
        Square(scene, 0, 0, 128, 128, 4); // the same floor, a sliver higher
        auto result = Sample(scene, Settings{});
        assert(result.cells.size() == 8);
    }
    // In-game lights: add up, capped at 255, and blocked by solid BSP.
    {
        Scene scene;
        Square(scene, 0, 0, 64, 64, 0, -1); // no baked lighting
        scene.lights.push_back(Light{ { 32, 32, 48 }, 400, 120 });
        scene.lights.push_back(Light{ { 32, 32, 48 }, 400, 60 });
        Settings s;
        auto result = Sample(scene, s);
        assert(result.cells.size() == 1);
        assert(result.cells[0].baked < 0 && Near(result.cells[0].lights, 180) && result.cells[0].band == Band::Exposed);
        scene.lights.push_back(Light{ { 32, 32, 48 }, 400, 200 });
        assert(Near(Sample(scene, s).cells[0].lights, 255));
        // A solid slab between z=100 and z=200 (outside above 200 and below 100):
        // a light above it does not reach the floor, one below does.
        Scene blocked;
        Square(blocked, 0, 0, 64, 64, 0, -1);
        blocked.bsp = { { { 0, 0, 1 }, 200, -1, 1, false }, { { 0, 0, -1 }, -100, -1, -1, true } };
        blocked.rootOutside = true;
        assert(LocalLightingMatch::ClearSegment(blocked.bsp, blocked.rootOutside, { 0, 0, 10 }, { 0, 0, 90 }));
        assert(!LocalLightingMatch::ClearSegment(blocked.bsp, blocked.rootOutside, { 0, 0, 10 }, { 0, 0, 300 }));
        blocked.lights.push_back(Light{ { 32, 32, 300 }, 1000, 200 });
        assert(Near(Sample(blocked, s).cells[0].lights, 0));
        blocked.lights.push_back(Light{ { 32, 32, 90 }, 1000, 100 });
        auto lit = Sample(blocked, s).cells[0];
        assert(lit.lights > 90 && lit.lights < 100);
        // A lamp embedded a little in its fitting still reaches.
        Scene fitting = blocked;
        fitting.lights = { Light{ { 32, 32, 108 }, 1000, 100 } };
        assert(Sample(fitting, s).cells[0].lights > 80);
    }
    // Sources: combined takes the brighter; baked-only counts unbaked floors dark.
    {
        std::vector<Cell> cells(2);
        cells[0].baked = 150; cells[0].lights = 20;
        cells[1].baked = -1; cells[1].lights = 120;
        Settings s;
        ApplyBands(cells, s);
        assert(Near(cells[0].value, 150) && cells[0].band == Band::Exposed);
        assert(Near(cells[1].value, 120) && cells[1].band == Band::Exposed);
        s.source = Source::Lights;
        ApplyBands(cells, s);
        assert(cells[0].band == Band::Hidden && cells[1].band == Band::Exposed);
        s.source = Source::Baked;
        ApplyBands(cells, s);
        assert(cells[0].band == Band::Exposed && cells[1].band == Band::Hidden);
        auto summary = Summarise(cells);
        assert(summary.total == 2 && summary.bands[0] == 1 && summary.bands[2] == 1 && summary.unbaked == 1);
        assert(Percent(1, 3) == 33 && Percent(2, 3) == 67 && Percent(0, 0) == 0);
    }
    // Legend and readout text.
    {
        std::vector<Cell> cells(4);
        for (int i = 0; i < 4; ++i) cells[i].baked = i * 60.0;
        ApplyBands(cells, Settings{});
        auto lines = Legend(Summarise(cells), Settings{}, false, false);
        assert(lines.size() == 4);
        assert(lines[0] == "Light and Shadow (baked + in-game lights): 4 floor patches of 64 uu");
        assert(Near(Percentile(cells, 0), 0) && Near(Percentile(cells, 0.5), 120) && Near(Percentile(cells, 1), 180));
        assert(Near(Percentile({}, 0.5), 0));
        assert(lines[1].find("darker than 40: 25%") != std::string::npos);
        assert(lines[2].find("partly visible: 25%") != std::string::npos);
        assert(lines[3].find("100 or brighter: 50%") != std::string::npos);
        auto empty = Legend(Summary{}, Settings{}, false, false);
        assert(empty.size() == 5 && empty[4].find("No walkable BSP floor") == 0);
        auto warned = Legend(Summarise(std::vector<Cell>(3)), Settings{}, true, true);
        assert(warned[0].find("refreshing") != std::string::npos && warned.size() == 6);
        assert(Describe(cells[1]) == "partly visible, brightness 60 (baked 60, in-game lights 0)");
        Cell none; none.lights = 12; none.value = 12;
        assert(Describe(none) == "hidden, brightness 12 (baked none, in-game lights 12)");
    }
    // Drawing: four sides, plus a diagonal per band step.
    {
        Cell cell; cell.at = { 100, 200, 30 };
        cell.band = Band::Hidden;
        auto lines = Outline(cell, 64);
        assert(lines.size() == 4 && Near(lines[0].first.z, 32));
        assert(Near(lines[0].first.x, 100 - 64 * 0.42) && Near(lines[1].second.y, 200 + 64 * 0.42));
        cell.band = Band::Partial;
        assert(Outline(cell, 64).size() == 5);
        cell.band = Band::Exposed;
        assert(Outline(cell, 64).size() == 6);
    }
    // Readout lookup: within half a patch, the highest floor not above the point.
    {
        std::vector<Cell> cells(3);
        cells[0].at = { 32, 32, 0 }; cells[1].at = { 32, 32, 256 }; cells[2].at = { 96, 32, 0 };
        assert(At(cells, 40, 20, 64) == &cells[1]);
        assert(At(cells, 40, 20, 64, 100) == &cells[0]);
        assert(At(cells, 90, 20, 64) == &cells[2]);
        assert(At(cells, 500, 20, 64) == nullptr);
    }
    // The cap on patches.
    {
        Scene huge;
        Square(huge, 0, 0, 16 * 600, 16 * 600, 0, -1);
        Settings s; s.cell = 16;
        Square(huge, 20000, 0, 20064, 64, 0, -1); // a small room, listed last
        auto result = Sample(huge, s);
        assert(result.truncated && result.cells.size() == kMaxCells);
        // The small floor is sampled before the vast one is cut short.
        assert(std::any_of(result.cells.begin(), result.cells.end(), [](const Cell& c) { return c.at.x > 20000; }));
    }
    std::puts("LightShadowModelTests passed");
    return 0;
}
