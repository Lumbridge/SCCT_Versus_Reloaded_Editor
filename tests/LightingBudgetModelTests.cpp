#include "../Reloaded.Editor/LightingBudgetModel.h"
#include <cassert>
#include <string>

using namespace LightingBudget;

namespace
{
    LightFlags InGameFlags() { LightFlags f; f.type = 1; f.inGameFlag = true; return f; }
    LightFlags StaticFlags() { LightFlags f; f.type = 1; f.staticFlag = true; return f; }
    Light At(const char* name, int zone, double x, double radius, LightFlags flags = InGameFlags())
    {
        Light light; light.name = name; light.zone = zone; light.x = x; light.radius = radius; light.flags = flags; return light;
    }
}

int main()
{
    // Classification mirrors the editor's light labels and its dynamic test.
    LightFlags f;
    assert(Classify(f) == Usage::Off && !CountsInGame(f));
    f.inGameFlag = true;
    assert(Classify(f) == Usage::Off && !CountsInGame(f)); // LightType None never counts
    f.type = 1;
    assert(Classify(f) == Usage::InGame && CountsInGame(f));
    f.staticFlag = true;
    assert(Classify(f) == Usage::StaticInGame && CountsInGame(f));
    f.inGameFlag = false;
    assert(Classify(f) == Usage::Static && !CountsInGame(f));
    f.staticFlag = false;
    assert(Classify(f) == Usage::Unflagged && !CountsInGame(f));
    f.dynamicFlag = true;
    assert(Classify(f) == Usage::Dynamic && CountsInGame(f));
    f.dynamicFlag = false; f.effect = 22;
    assert(IsDynamic(f) && CountsInGame(f));
    f.effect = 0; f.animA = 1;
    assert(!IsDynamic(f));            // both animation values must be set
    f.animB = 2;
    assert(IsDynamic(f));
    f.staticFlag = true;              // dynamic wins over the static flag, as in the editor's label
    assert(Classify(f) == Usage::Dynamic);
    assert(std::string(UsageName(Usage::StaticInGame)) == "Static/InGame");

    // Overlap: strict sphere test; zone-limited pairs in different zones never overlap.
    auto a = At("A", 1, 0, 100), b = At("B", 1, 199, 100), c = At("C", 2, 200, 100);
    assert(Overlap(a, b) && !Overlap(a, c));
    b.zone = 2;
    assert(Overlap(a, b));
    a.flags.zoneLimited = b.flags.zoneLimited = true;
    assert(!Overlap(a, b));
    b.flags.zoneLimited = false;
    assert(Overlap(a, b));

    // Thresholds are clamped to usable values.
    Thresholds t; t.leafLights = -3; t.overlap = 0;
    t = ClampThresholds(t);
    assert(t.leafLights == 0 && t.overlap == 1);
    t.warnLeafLights = 999; t.warnOverlap = 0;
    t = ClampThresholds(t);
    assert(t.warnLeafLights == 255 && t.warnOverlap == 1);
    // The build warning swaps in its own limits.
    Thresholds guideline;
    auto warning = ForBuildWarning(guideline);
    assert(warning.leafLights == Thresholds::WarnLeafDefault && warning.overlap == Thresholds::WarnOverlapDefault);
    assert(guideline.leafLights == 4 && guideline.overlap == 2);

    // A map: zone 1 has three in-game lights all overlapping, one static light
    // and a leaf reached by five in-game lights (one of them from zone 2);
    // zone 2 has two separate in-game lights; zone 3 has no lights.
    std::vector<Light> lights{
        At("L0", 1, 0, 100), At("L1", 1, 50, 100), At("L2", 1, 100, 100),
        At("S3", 1, 0, 5000, StaticFlags()),
        At("L4", 2, 5000, 10), At("L5", 2, 9000, 10)};
    std::vector<Leaf> leaves{
        {1, {0, 1, 2, 3, 4, 5}},   // five in-game lights and a static one
        {1, {0, 1}},
        {2, {4, 4, 99}},           // duplicates and unknown entries are ignored
        {3, {}}};
    std::map<int, std::string> names{{1, "Lobby"}, {3, ""}};
    Thresholds limits;             // 4 per leaf, 2 overlapping
    auto report = Analyse(lights, leaves, names, limits, true);
    assert(report.zones.size() == 3);
    const auto& lobby = report.zones[0];
    assert(lobby.zone == 1 && lobby.name == "Lobby");
    assert(lobby.total == 4 && lobby.inGame == 3);
    assert(lobby.usage.at(Usage::InGame) == 3 && lobby.usage.at(Usage::Static) == 1);
    assert(lobby.worstLeaf == 5 && lobby.worstLeafIndex == 0 && lobby.overLeaf);
    assert(lobby.overlap == 3 && lobby.overOverlap);
    assert((lobby.overlapLights == std::vector<int>{0, 1, 2}));
    const auto& second = report.zones[1];
    assert(second.name == "Zone 2" && second.total == 2 && second.worstLeaf == 1);
    assert(second.overlap == 1 && !second.Over());
    assert(report.zones[2].name == "Zone 3" && report.zones[2].total == 0 && !report.zones[2].Over());
    assert(report.map.total == 6 && report.map.inGame == 5 && report.map.worstLeaf == 5 && report.map.overlap == 3);
    assert(report.Over() && report.overlapExact);
    // Hotspots: the leaf (5 lights) before the overlap group (3).
    assert(report.hotspots.size() == 2);
    assert(report.hotspots[0].kind == Hotspot::Kind::Leaf && report.hotspots[0].leaf == 0);
    assert((report.hotspots[0].lights == std::vector<int>{0, 1, 2, 4, 5}));
    assert(report.hotspots[1].kind == Hotspot::Kind::Overlap && report.hotspots[1].zone == 1);
    assert(HotspotLabel(report.hotspots[0]) == "BSP leaf 0");

    auto summary = Summary(report);
    assert(summary.find("Lobby: 5 lights reach one BSP leaf (budget 4); 3 lights overlap (budget 2)") != std::string::npos);
    assert(summary.find("Zone 2") == std::string::npos);
    assert(summary.find("6 lights in the map, 5 of them drawn in game") != std::string::npos);

    // Raising the budget clears the flags; an unbuilt BSP never flags leaves.
    limits.leafLights = 5; limits.overlap = 3;
    assert(!Analyse(lights, leaves, names, limits, true).Over());
    limits.overlap = 9; limits.leafLights = 0;
    auto unbuilt = Analyse(lights, {}, names, limits, false);
    assert(!unbuilt.Over() && unbuilt.map.worstLeaf == 0);
    assert(Summary(unbuilt).find("not been built") != std::string::npos);

    // Identical leaf lists are one hotspot.
    limits = Thresholds{};
    auto twice = Analyse(lights, {{1, {0, 1, 2, 4, 5}, 7}, {1, {5, 4, 2, 1, 0}, 9}}, names, limits, true);
    assert(twice.hotspots.size() == 2);
    // Leaves are reported by the BSP's own leaf number.
    assert(twice.hotspots[0].leaf == 7 && twice.zones[0].worstLeafIndex == 7);

    // A chain A-B-C where A and C do not overlap has groups of two only.
    std::vector<Light> chain{At("A", 1, 0, 60), At("B", 1, 100, 60), At("C", 1, 200, 60)};
    auto chained = Analyse(chain, {}, {}, limits, true);
    assert(chained.map.overlap == 2 && !chained.Over() && chained.hotspots.empty());

    // A dense cluster: twelve lights on one spot form one group of twelve.
    std::vector<Light> cluster;
    for (int i = 0; i < 12; ++i) cluster.push_back(At("X", 4, i, 500));
    auto dense = Analyse(cluster, {}, {}, limits, true);
    assert(dense.map.overlap == 12 && dense.hotspots.size() == 1 && dense.overlapExact);

    // A group straddling zones: each zone counts its own share; the hotspot
    // belongs to the zone holding most of it.
    std::vector<Light> straddle{At("P", 6, 0, 100), At("Q", 5, 10, 100), At("R", 5, 20, 100)};
    auto shared = Analyse(straddle, {}, {}, limits, true);
    assert(shared.map.overlap == 3 && shared.hotspots.size() == 1 && shared.hotspots[0].zone == 5);
    assert(shared.zones[0].zone == 5 && shared.zones[0].overlap == 2 && !shared.zones[0].Over());
    assert(shared.zones[1].zone == 6 && shared.zones[1].overlap == 1);
    assert(shared.Over()); // the map as a whole has a group of three

    assert(FoldKey("Maps\\MyMap.SDC") == "maps\\mymap.sdc");

    // Fixes. Make static clears the in-game and dynamic flags and sets the
    // static one; turning off sets LightType None.
    LightFlags flags = InGameFlags();
    flags.dynamicFlag = true;
    auto made = Applied(flags, Fix::MakeStatic);
    assert(made.type == 1 && made.staticFlag && !made.inGameFlag && !made.dynamicFlag);
    assert(Classify(made) == Usage::Static && !CountsInGame(made));
    auto off = Applied(flags, Fix::TurnOff);
    assert(off.type == 0 && Classify(off) == Usage::Off && !CountsInGame(off));
    // LightEffect 22 keeps a light dynamic however its flags are set.
    flags.effect = 22;
    assert(CountsInGame(Applied(flags, Fix::MakeStatic)));

    std::vector<Light> row{
        At("Dim", 1, 0, 100), At("Bright", 1, 0, 100), At("Baked", 1, 0, 100, StaticFlags()),
        At("Flicker", 1, 0, 100), At("Mid", 1, 0, 100), At("Twin", 1, 0, 100)};
    row[0].brightness = 10; row[0].lightRadius = 10;   // 100
    row[1].brightness = 200; row[1].lightRadius = 64;  // 12800
    row[3].flags.effect = 22;                           // stays dynamic
    row[3].brightness = 1; row[3].lightRadius = 1;      // weakest of all
    row[4].brightness = -50; row[4].lightRadius = 20;  // 1000: negative brightness still counts as strength
    row[5].brightness = 10; row[5].lightRadius = 10;   // 100, ties with Dim
    const std::vector<int> all{0, 1, 2, 3, 4, 5, 5, 99};
    auto plan = PlanMakeStatic(row, all);
    assert(plan.fix == Fix::MakeStatic && (plan.lights == std::vector<int>{0, 1, 4, 5}));
    assert(plan.stillDynamic == 1 && plan.notInGame == 1 && !plan.Empty());
    auto question = Confirmation(plan, "Lobby", row);
    assert(question.find("Make 4 in-game lights in Lobby static") != std::string::npos);
    assert(question.find("Dim, Bright, Mid, Twin.") != std::string::npos);
    assert(question.find("1 light stays dynamic") != std::string::npos);
    auto done = Done(plan, 4, "Lobby", 5, 1);
    assert(done.find("Made 4 lights static in Lobby as one undoable step") != std::string::npos);
    assert(done.find("5 -> 1") != std::string::npos && done.find("Rebuild lighting") != std::string::npos);
    assert(Done(plan, 3, "Lobby", 5, 2).find("1 no longer exist") != std::string::npos);

    // The weakest two in-game lights: Flicker (1), then Dim before Twin on a tie.
    plan = PlanTurnOffWeakest(row, all, 2);
    assert(plan.fix == Fix::TurnOff && (plan.lights == std::vector<int>{3, 0}) && plan.notInGame == 1);
    assert(Confirmation(plan, "Leaf 7", row).find("Turn off the 2 weakest in-game lights in Leaf 7") != std::string::npos);
    assert(Done(plan, 2, "Leaf 7", 5, 3).find("Turned off 2 lights in Leaf 7") != std::string::npos);
    assert(Done(plan, 2, "Leaf 7", 5, 3).find("Rebuild") == std::string::npos);
    assert((PlanTurnOffWeakest(row, all, 9).lights == std::vector<int>{3, 0, 5, 4, 1}));
    assert(PlanTurnOffWeakest(row, all, -1).Empty());
    // Nothing to do: only baked lights, or only effect-22 ones.
    plan = PlanMakeStatic(row, {2});
    assert(plan.Empty() && Confirmation(plan, "Cellar", row).find("No light here is drawn in game") != std::string::npos);
    plan = PlanMakeStatic(row, {3});
    assert(plan.Empty() && Confirmation(plan, "Cellar", row).find("LightEffect 22") != std::string::npos);
    assert(Strength(row[4]) == 1000);
    // Long lists name six lights and count the rest.
    std::vector<Light> many(8, At("L", 1, 0, 100));
    auto longQuestion = Confirmation(PlanMakeStatic(many, {0, 1, 2, 3, 4, 5, 6, 7}), "Hall", many);
    assert(longQuestion.find("L, L, L, L, L, L and 2 more.") != std::string::npos);
    return 0;
}
