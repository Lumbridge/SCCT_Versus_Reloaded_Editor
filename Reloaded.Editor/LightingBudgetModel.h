#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

// Lighting Budget: how many lights the game has to draw at once in each zone,
// measured the way the editor's own light checks measure it.
//
// What counts is decided by the stock editor (ChaosTheory_Editor.exe):
// - A light whose LightType is not None counts in game when it has
//   bApplyToInGameLighting (+0x2EC bit 0x800000) or is dynamic (0x10EDACF0:
//   bDynamicLight +0x2E8 bit 0x20000, LightEffect 22, or both HeatRadius +0x174
//   and HeatIntensity +0x178 non-zero). Lights with only bApplyToStaticLighting
//   (+0x2EC bit 0x400000) are baked into lightmaps by Build Lighting and cost
//   nothing in game.
// - Tools > Check InGame / Dynamic Lights in Leaves (40165) regenerates the BSP
//   leaf light lists for in-game lights and lists every leaf reached by MORE
//   THAN 4 of them (cmp 4 / jle at 0x10E5D36A).
// - Tools > Check InGame / Dynamic Lights Intersections (243) lists groups of
//   3 OR MORE in-game lights whose spheres all overlap each other
//   (cmp 3 / jl at 0x10E695D4); two lights overlap when the distance between
//   their render centres is less than the sum of their render radii, except
//   that two bAffectOwnZoneOnly lights (+0x2E8 bit 0x200) in different zones never do
//   (0x10E69720).
// The renderer itself only asserts at 256 lights per scene node (MaxNbLights,
// 0x111AC4A8), so these are the editor's guidelines, not a hard limit, and
// both are configurable.
namespace LightingBudget
{
    struct LightFlags
    {
        uint8_t type = 0;        // LightType; 0 is LT_None
        uint8_t effect = 0;      // LightEffect
        bool staticFlag = false; // baked by Build Lighting
        bool inGameFlag = false; // drawn per frame in game
        bool dynamicFlag = false;
        bool zoneLimited = false;
        float animA = 0, animB = 0; // HeatRadius, HeatIntensity
    };

    enum class Usage { Off, Unflagged, Static, InGame, StaticInGame, Dynamic };

    inline const char* UsageName(Usage usage)
    {
        switch (usage)
        {
        case Usage::Off: return "Off";
        case Usage::Unflagged: return "Unflagged";
        case Usage::Static: return "Static";
        case Usage::InGame: return "InGame";
        case Usage::StaticInGame: return "Static/InGame";
        case Usage::Dynamic: return "Dynamic";
        }
        return "";
    }

    // The editor's own IsDynamicLight (0x10EDACF0).
    inline bool IsDynamic(const LightFlags& f)
    {
        if (!f.type) return false;
        return f.dynamicFlag || f.effect == 22 || (f.animA != 0 && f.animB != 0);
    }

    // The filter both stock in-game checks apply.
    inline bool CountsInGame(const LightFlags& f)
    {
        return f.type != 0 && (f.inGameFlag || IsDynamic(f));
    }

    // The label the editor draws next to a light (0x111914A2): dynamic first,
    // then the static / in-game flags. A light with LightType None is "Off".
    inline Usage Classify(const LightFlags& f)
    {
        if (!f.type) return Usage::Off;
        if (IsDynamic(f)) return Usage::Dynamic;
        if (f.staticFlag && f.inGameFlag) return Usage::StaticInGame;
        if (f.staticFlag) return Usage::Static;
        if (f.inGameFlag) return Usage::InGame;
        return Usage::Unflagged;
    }

    struct Light
    {
        std::string name;
        int zone = 0;
        LightFlags flags;
        double x = 0, y = 0, z = 0, radius = 0; // the render sphere; only read for in-game lights
    };

    // One BSP leaf: its zone and the lights in its generated light list
    // (indices into the light list; unknown actors are left out by the caller).
    struct Leaf
    {
        int zone = 0;
        std::vector<int> lights;
        int index = -1;         // the BSP's own leaf number; the list position when unset
    };

    struct Thresholds
    {
        int leafLights = 4;  // a leaf may be reached by this many in-game lights
        int overlap = 2;     // this many in-game lights may all overlap one another
        // The build warning's own, looser limits. Every shipped Versus map
        // exceeds the guideline above (17 measured: worst leaf 13 to 44, MissD
        // the most; largest overlap 7 to 33, StroD the most), so a warning at
        // it would fire on every build. The warning fires only for a map lit
        // more densely than any of them.
        int warnLeafLights = WarnLeafDefault;
        int warnOverlap = WarnOverlapDefault;
        bool warnBeforeBuild = true;
        static constexpr int WarnLeafDefault = 44, WarnOverlapDefault = 33;
    };

    inline Thresholds ClampThresholds(Thresholds t)
    {
        t.leafLights = std::clamp(t.leafLights, 0, 255);
        t.overlap = std::clamp(t.overlap, 1, 255);
        t.warnLeafLights = std::clamp(t.warnLeafLights, 0, 255);
        t.warnOverlap = std::clamp(t.warnOverlap, 1, 255);
        return t;
    }

    // The limits the pre-build warning measures against.
    inline Thresholds ForBuildWarning(Thresholds t)
    {
        t.leafLights = t.warnLeafLights;
        t.overlap = t.warnOverlap;
        return t;
    }

    inline bool Overlap(const Light& a, const Light& b)
    {
        if (a.flags.zoneLimited && b.flags.zoneLimited && a.zone != b.zone) return false;
        const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz) < a.radius + b.radius;
    }

    struct Hotspot
    {
        enum class Kind { Leaf, Overlap } kind = Kind::Leaf;
        int zone = 0;
        int leaf = -1;              // the leaf index for a leaf hotspot
        std::vector<int> lights;    // indices into the light list
    };

    struct ZoneReport
    {
        int zone = 0;
        std::string name;
        int total = 0;
        std::map<Usage, int> usage;
        int inGame = 0;             // lights that count in game
        int worstLeaf = 0;          // most in-game lights reaching one leaf of this zone
        int worstLeafIndex = -1;
        std::vector<int> worstLeafLights;
        int overlap = 0;            // largest group of this zone's own in-game lights that all overlap one another
        std::vector<int> overlapLights;
        std::vector<int> lights;    // every light in the zone
        bool overLeaf = false, overOverlap = false;
        bool Over() const { return overLeaf || overOverlap; }
    };

    struct Report
    {
        std::vector<ZoneReport> zones; // in zone number order
        ZoneReport map;                // totals; worst values across the map
        std::vector<Hotspot> hotspots; // worst first
        bool leavesKnown = false;      // false when the BSP has no leaves (never built)
        bool overlapExact = true;      // false when the overlap search was cut short
        Thresholds thresholds;
        bool Over() const { return map.Over(); }
    };

    namespace Detail
    {
        // Bron-Kerbosch with pivoting over the overlap graph; calls found(clique)
        // for each maximal clique. Returns false when the step budget ran out.
        inline bool MaximalCliques(const std::vector<std::vector<bool>>& adjacent,
                                   const std::vector<int>& vertices,
                                   const std::function<void(const std::vector<int>&)>& found,
                                   size_t budget = 250000)
        {
            size_t steps = 0;
            bool complete = true;
            std::vector<int> r;
            std::function<void(std::vector<int>, std::vector<int>)> expand =
                [&](std::vector<int> p, std::vector<int> x)
            {
                if (++steps > budget) { complete = false; return; }
                if (p.empty())
                {
                    if (x.empty()) found(r);
                    return;
                }
                int pivot = p.front();
                size_t best = 0;
                for (const auto* set : {&p, &x})
                    for (int u : *set)
                    {
                        size_t n = 0;
                        for (int v : p) n += adjacent[u][v];
                        if (n >= best) { best = n; pivot = u; }
                    }
                std::vector<int> candidates;
                for (int v : p) if (!adjacent[pivot][v]) candidates.push_back(v);
                for (int v : candidates)
                {
                    if (!complete) return;
                    std::vector<int> np, nx;
                    for (int u : p) if (adjacent[v][u]) np.push_back(u);
                    for (int u : x) if (adjacent[v][u]) nx.push_back(u);
                    r.push_back(v);
                    expand(std::move(np), std::move(nx));
                    r.pop_back();
                    p.erase(std::find(p.begin(), p.end(), v));
                    x.push_back(v);
                }
            };
            expand(vertices, {});
            return complete;
        }
    }

    // zoneNames: zone number -> display name; zones without a name are "Zone N".
    inline Report Analyse(const std::vector<Light>& lights, const std::vector<Leaf>& leaves,
                          const std::map<int, std::string>& zoneNames, Thresholds thresholds,
                          bool leavesKnown)
    {
        thresholds = ClampThresholds(thresholds);
        Report report;
        report.thresholds = thresholds;
        report.leavesKnown = leavesKnown;
        std::map<int, ZoneReport> zones;
        auto zoneOf = [&](int number) -> ZoneReport&
        {
            auto& zone = zones[number];
            zone.zone = number;
            if (zone.name.empty())
            {
                auto named = zoneNames.find(number);
                zone.name = named != zoneNames.end() && !named->second.empty() ? named->second : "Zone " + std::to_string(number);
            }
            return zone;
        };
        for (const auto& [number, name] : zoneNames) zoneOf(number);
        report.map.name = "Whole map";
        report.map.zone = -1;

        std::vector<int> inGame;
        for (int i = 0; i < static_cast<int>(lights.size()); ++i)
        {
            const auto& light = lights[i];
            const auto usage = Classify(light.flags);
            for (auto* target : {&zoneOf(light.zone), &report.map})
            {
                ++target->total;
                ++target->usage[usage];
                target->lights.push_back(i);
                if (CountsInGame(light.flags)) ++target->inGame;
            }
            if (CountsInGame(light.flags)) inGame.push_back(i);
        }

        // Leaves: the in-game lights in each generated list.
        std::set<std::vector<int>> leafSets;
        std::vector<Hotspot> leafHotspots;
        for (int i = 0; i < static_cast<int>(leaves.size()); ++i)
        {
            std::vector<int> counted;
            for (int light : leaves[i].lights)
                if (light >= 0 && light < static_cast<int>(lights.size()) && CountsInGame(lights[light].flags)
                    && std::find(counted.begin(), counted.end(), light) == counted.end())
                    counted.push_back(light);
            if (counted.empty()) continue;
            std::sort(counted.begin(), counted.end());
            const int leafNumber = leaves[i].index >= 0 ? leaves[i].index : i;
            const int count = static_cast<int>(counted.size());
            for (auto* target : {&zoneOf(leaves[i].zone), &report.map})
                if (count > target->worstLeaf)
                {
                    target->worstLeaf = count;
                    target->worstLeafIndex = leafNumber;
                    target->worstLeafLights = counted;
                }
            if (count > thresholds.leafLights && leafSets.insert(counted).second)
                leafHotspots.push_back({Hotspot::Kind::Leaf, leaves[i].zone, leafNumber, counted});
        }

        // Overlap groups among in-game lights.
        const int n = static_cast<int>(inGame.size());
        std::vector<std::vector<bool>> adjacent(n, std::vector<bool>(n, false));
        for (int a = 0; a < n; ++a)
            for (int b = a + 1; b < n; ++b)
                adjacent[a][b] = adjacent[b][a] = Overlap(lights[inGame[a]], lights[inGame[b]]);
        std::vector<int> vertices;
        for (int a = 0; a < n; ++a) vertices.push_back(a);
        std::vector<Hotspot> overlapHotspots;
        report.overlapExact = Detail::MaximalCliques(adjacent, vertices, [&](const std::vector<int>& clique)
        {
            std::vector<int> members;
            for (int v : clique) members.push_back(inGame[v]);
            std::sort(members.begin(), members.end());
            const int size = static_cast<int>(members.size());
            // A zone's own largest group is its share of some maximal group:
            // every group of its lights extends to one.
            std::map<int, std::vector<int>> shares;
            for (int light : members) shares[lights[light].zone].push_back(light);
            int majority = shares.begin()->first;
            for (const auto& [zone, share] : shares)
            {
                auto& target = zoneOf(zone);
                if (static_cast<int>(share.size()) > target.overlap) { target.overlap = static_cast<int>(share.size()); target.overlapLights = share; }
                if (share.size() > shares[majority].size()) majority = zone;
            }
            if (size > report.map.overlap) { report.map.overlap = size; report.map.overlapLights = members; }
            if (size > thresholds.overlap)
                overlapHotspots.push_back({Hotspot::Kind::Overlap, majority, -1, members});
        });
        // A lone in-game light is a group of one.
        for (int light : inGame)
            for (auto* target : {&zoneOf(lights[light].zone), &report.map})
                if (target->overlap < 1) { target->overlap = 1; target->overlapLights = {light}; }

        auto flag = [&](ZoneReport& zone)
        {
            zone.overLeaf = leavesKnown && zone.worstLeaf > thresholds.leafLights;
            zone.overOverlap = zone.overlap > thresholds.overlap;
        };
        for (auto& [number, zone] : zones) { flag(zone); report.zones.push_back(zone); }
        flag(report.map);

        auto bySize = [](const Hotspot& a, const Hotspot& b)
        {
            if (a.lights.size() != b.lights.size()) return a.lights.size() > b.lights.size();
            if (a.kind != b.kind) return a.kind == Hotspot::Kind::Leaf;
            if (a.zone != b.zone) return a.zone < b.zone;
            return a.lights < b.lights;
        };
        report.hotspots = leafHotspots;
        report.hotspots.insert(report.hotspots.end(), overlapHotspots.begin(), overlapHotspots.end());
        std::sort(report.hotspots.begin(), report.hotspots.end(), bySize);
        if (report.hotspots.size() > 200) report.hotspots.resize(200);
        return report;
    }

    inline std::string HotspotLabel(const Hotspot& hotspot)
    {
        return hotspot.kind == Hotspot::Kind::Leaf ? "BSP leaf " + std::to_string(hotspot.leaf) : "Overlap group";
    }

    // One zone's problems, e.g. "5 lights reach one BSP leaf (budget 4); 3 lights overlap (budget 2)".
    inline std::string Problems(const ZoneReport& zone, const Thresholds& t)
    {
        std::string text;
        if (zone.overLeaf) text += std::to_string(zone.worstLeaf) + " lights reach one BSP leaf (budget " + std::to_string(t.leafLights) + ")";
        if (zone.overLeaf && zone.overOverlap) text += "; ";
        if (zone.overOverlap) text += std::to_string(zone.overlap) + " lights overlap (budget " + std::to_string(t.overlap) + ")";
        return text;
    }

    // The warning shown before a build: the map, then each zone over budget.
    inline std::string Summary(const Report& report, size_t maxZones = 8)
    {
        const auto& t = report.thresholds;
        std::string text = "This map's in-game lights exceed the lighting budget.\r\n  Whole map: " + Problems(report.map, t) + "\r\n";
        size_t listed = 0, over = 0;
        for (const auto& zone : report.zones)
        {
            if (!zone.Over()) continue;
            ++over;
            if (listed >= maxZones) continue;
            ++listed;
            text += "  " + zone.name + ": " + Problems(zone, t) + "\r\n";
        }
        if (over > listed) text += "  ...and " + std::to_string(over - listed) + " more zone(s)\r\n";
        text += "\r\n" + std::to_string(report.map.total) + " lights in the map, " + std::to_string(report.map.inGame)
            + " of them drawn in game. Static lights are baked and do not count.";
        if (!report.leavesKnown) text += " The BSP has not been built, so leaf counts are not checked.";
        return text;
    }

    // A map key for "don't warn again": case-folded.
    inline std::string FoldKey(std::string text)
    {
        for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }
}
