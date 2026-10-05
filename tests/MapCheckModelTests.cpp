#include "../Reloaded.Editor/MapCheckModel.h"
#include <cassert>
#include <cstdio>
#include <string>

using namespace MapCheck;

namespace
{
    // A CSG wall plane: in front (the room side) is empty, behind is solid.
    Node Wall(Vec3 normal, double distance, int next, int zone)
    {
        Node n; n.normal = normal; n.distance = distance; n.csg = true;
        n.front = next; n.back = -1;
        if (next == -1) n.zone[1] = static_cast<uint8_t>(zone);
        return n;
    }

    // Appends the five remaining walls of an axis-aligned room [x0,x1] x [0,100] x [0,100]
    // as a chain of nodes ending in an empty leaf of `zone`. Returns the first node.
    int RoomChain(Bsp& bsp, double x0, double x1, int zone, bool lowX, bool highX)
    {
        const int first = static_cast<int>(bsp.nodes.size());
        std::vector<std::pair<Vec3, double>> planes;
        if (lowX) planes.push_back({{1, 0, 0}, x0});
        if (highX) planes.push_back({{-1, 0, 0}, -x1});
        planes.push_back({{0, 1, 0}, 0});
        planes.push_back({{0, -1, 0}, -100});
        planes.push_back({{0, 0, 1}, 0});
        planes.push_back({{0, 0, -1}, -100});
        for (size_t i = 0; i < planes.size(); ++i)
        {
            const bool last = i + 1 == planes.size();
            bsp.nodes.push_back(Wall(planes[i].first, planes[i].second, last ? -1 : first + static_cast<int>(i) + 1, zone));
        }
        return first;
    }

    // Two rooms side by side (x 0-100 and 100-200), open to each other at x = 100,
    // where a non-CSG portal plane splits the tree. When `sealed`, the rooms are
    // zones 1 and 2; otherwise the zone builder found them joined (zone 1).
    Bsp TwoRooms(bool sealed)
    {
        Bsp bsp;
        bsp.rootOutside = false;
        bsp.numZones = sealed ? 3 : 2;
        bsp.nodes.push_back({});                // root: the portal plane x >= 100
        bsp.nodes[0].normal = {1, 0, 0};
        bsp.nodes[0].distance = 100;
        const int b = RoomChain(bsp, 100, 200, sealed ? 2 : 1, false, true);
        const int a = RoomChain(bsp, 0, 100, 1, true, false);
        bsp.nodes[0].front = b;
        bsp.nodes[0].back = a;
        return bsp;
    }

    Scene Base(bool sealed, double portalTop)
    {
        Scene scene;
        scene.bsp = TwoRooms(sealed);
        scene.boundsKnown = true;
        scene.boundsMin = {0, 0, 0};
        scene.boundsMax = {200, 100, 100};
        scene.zoneActors = {"", "MyLevel.ZoneInfo0", sealed ? "MyLevel.ZoneInfo1" : ""};
        Portal portal;
        portal.surface = 7;
        portal.brush = "MyLevel.Sheet0";
        portal.normal = {1, 0, 0};
        portal.fragments.push_back({{100, 0, 0}, {100, portalTop, 0}, {100, portalTop, 100}, {100, 0, 100}});
        scene.portals.push_back(portal);
        return scene;
    }

    Actor Make(const char* name, std::vector<std::string> classes, Vec3 at, double radius = 0, double height = 0)
    {
        Actor a;
        a.name = name;
        a.path = std::string("MyLevel.") + name;
        a.classes = std::move(classes);
        a.location = at;
        a.radius = radius;
        a.height = height;
        return a;
    }

    const std::vector<std::string> kPlayerStart{"SPlayerStart", "PlayerStart", "NavigationPoint", "Actor", "Object"};
    const std::vector<std::string> kZoneInfo{"ZoneInfo", "Info", "Actor", "Object"};
    const std::vector<std::string> kLight{"Light", "Actor", "Object"};

    const Problem* FindCheck(const Report& report, const std::string& check, size_t nth = 0)
    {
        for (const auto& p : report.problems)
            if (p.check == check && nth-- == 0) return &p;
        return nullptr;
    }
    size_t CountCheck(const Report& report, const std::string& check)
    {
        size_t n = 0;
        for (const auto& p : report.problems) n += p.check == check;
        return n;
    }
}

int main()
{
    // Point queries follow UModel::PointRegion: zone of the last node's side,
    // solid behind CSG walls.
    {
        const auto bsp = TwoRooms(true);
        auto r = PointRegion(bsp, {50, 50, 50});
        assert(r.valid && !r.solid && r.zone == 1);
        r = PointRegion(bsp, {150, 50, 50});
        assert(r.valid && !r.solid && r.zone == 2);
        r = PointRegion(bsp, {250, 50, 50});
        assert(r.valid && r.solid && r.zone == 0);
        r = PointRegion(bsp, {50, 50, -5});
        assert(r.solid && r.zone == 0);
        // On a plane counts as in front, like the engine's >= 0 test.
        r = PointRegion(bsp, {50, 50, 0});
        assert(!r.solid && r.zone == 1);
        Bsp unbuilt;
        assert(!PointRegion(unbuilt, {0, 0, 0}).valid && !unbuilt.Built());
        // A level that starts empty (RootOutside) with no nodes on a path stays empty.
        Bsp open;
        open.rootOutside = true;
        open.numZones = 2;
        open.nodes.push_back({});
        open.nodes[0].normal = {0, 0, 1};
        open.nodes[0].zone[0] = 1;
        open.nodes[0].zone[1] = 1;
        assert(!PointRegion(open, {0, 0, -50}).solid && PointRegion(open, {0, 0, -50}).zone == 1);
        // A broken child index never walks off the array.
        Bsp broken = bsp;
        broken.nodes[0].front = 999;
        assert(!PointRegion(broken, {150, 50, 50}).valid);
    }

    // Geometry helpers.
    {
        const std::vector<Vec3> square{{0, 0, 0}, {0, 10, 0}, {0, 10, 10}, {0, 0, 10}};
        assert(SegmentCrossesPolygon({-5, 5, 5}, {5, 5, 5}, square));
        assert(SegmentCrossesPolygon({5, 5, 5}, {-5, 5, 5}, square));     // either direction
        assert(!SegmentCrossesPolygon({-5, 15, 5}, {5, 15, 5}, square));  // beside it
        assert(!SegmentCrossesPolygon({1, 5, 5}, {5, 5, 5}, square));     // does not reach the plane
        assert(!SegmentCrossesPolygon({0, 5, 5}, {0, 6, 5}, square));     // along the plane
        const std::vector<Vec3> reversed{square.rbegin(), square.rend()};
        assert(SegmentCrossesPolygon({-5, 5, 5}, {5, 5, 5}, reversed));   // any winding
        Actor a = Make("A", kPlayerStart, {0, 0, 0}, 40, 40);
        const auto samples = CylinderSamples(a, 4);
        assert(samples.size() == 27);
        double lowest = 0, widest = 0;
        for (const auto& p : samples) { lowest = std::min(lowest, p[2]); widest = std::max(widest, p[0]); }
        assert(std::fabs(lowest + 36) < 1e-9 && std::fabs(widest - 36) < 1e-9);
        assert(Units(12345.6) == "12,346" && Units(-1000) == "-1,000" && Units(999) == "999");
        Settings s; s.geometryTolerance = -3; s.leakGridCells = 5;
        s = ClampSettings(s);
        assert(s.geometryTolerance == 0 && s.leakGridCells == 10000);
    }

    // Roles: what Map Check looks at.
    {
        assert(RoleOf(Make("P", kPlayerStart, {})) == Role::Important);
        assert(RoleOf(Make("Z", kZoneInfo, {})) == Role::Zone);
        assert(RoleOf(Make("L", {"LevelInfo", "ZoneInfo", "Info", "Actor"}, {})) == Role::Skip);
        assert(RoleOf(Make("B", {"Mover", "Brush", "Actor"}, {})) == Role::Skip);
        assert(RoleOf(Make("SM", {"SMover", "Mover", "Actor"}, {})) == Role::Skip);
        assert(RoleOf(Make("C", {"Camera", "PlayerController", "Controller", "Actor"}, {})) == Role::Skip);
        assert(RoleOf(Make("S", {"StaticMeshActor", "Actor"}, {})) == Role::Skip);
        assert(RoleOf(Make("M", {"SMission", "Info", "Actor"}, {})) == Role::Skip);
        assert(RoleOf(Make("O", {"SComputerObjectiveTrigger", "Actor"}, {})) == Role::Important);
        assert(RoleOf(Make("I", {"SPickup", "Pickup", "Actor"}, {})) == Role::Important);
        assert(RoleOf(Make("Li", kLight, {})) == Role::Normal);
        assert(!TestsGeometry(Make("P0", kPlayerStart, {}, 0, 0)));
        assert(TestsGeometry(Make("P1", kPlayerStart, {}, 40, 80)));
        assert(!TestsGeometry(Make("L1", kLight, {}, 40, 80)));
        // Touch volumes may cut into walls; only spawn points, pickups and pawns are tested.
        assert(!TestsGeometry(Make("T1", {"SComputerObjectiveTrigger", "Actor"}, {}, 22, 22)));
        assert(!TestsGeometry(Make("D1", {"SFlagDropZone", "Actor"}, {}, 5, 5)));
        assert(TestsGeometry(Make("I1", {"SPickup", "Pickup", "Actor"}, {}, 10, 10)));
    }

    // Actors in the void, far outside, beyond the world, stuck in BSP or a mesh.
    {
        auto scene = Base(true, 100);
        scene.actors.push_back(Make("GoodStart", kPlayerStart, {50, 50, 45}, 40, 40));
        scene.actors.push_back(Make("SunkStart", kPlayerStart, {50, 50, 30}, 40, 40));
        scene.actors.push_back(Make("VoidStart", kPlayerStart, {300, 50, 50}, 40, 40));
        scene.actors.push_back(Make("VoidLight", kLight, {50, 50, 150}));
        scene.actors.push_back(Make("FarLight", kLight, {50, 50, 5000}));
        scene.actors.push_back(Make("LostStart", kPlayerStart, {300000, 0, 0}, 40, 40));
        scene.actors.push_back(Make("Wall", {"StaticMeshActor", "Actor"}, {500, 50, 50}));
        // An origin a couple of units under the floor rests on it; it is not lost.
        scene.actors.push_back(Make("FloorFlag", {"SFlag", "SObjective", "Actor"}, {50, 50, -2}));
        scene.actors.push_back(Make("Terminal", {"SComputerObjectiveTrigger", "Actor"}, {50, 2, 50}, 22, 22));
        auto meshed = Make("MeshedStart", kPlayerStart, {150, 50, 50}, 20, 20);
        meshed.encroachTested = true;
        meshed.encroachingActor = "MyLevel.StaticMeshActor3";
        scene.actors.push_back(meshed);
        auto engineSays = Make("EngineStart", kPlayerStart, {160, 50, 50}, 20, 20);
        engineSays.encroachTested = true;
        engineSays.encroachesBsp = true;
        scene.actors.push_back(engineSays);
        scene.actors.push_back(Make("ZoneA", kZoneInfo, {50, 50, 50}));
        scene.actors.push_back(Make("ZoneB", kZoneInfo, {150, 50, 50}));

        Settings settings;
        const auto report = Analyse(scene, settings);
        assert(report.bspBuilt && report.zones == 2);
        assert(report.actorsChecked == 10);

        auto stuck = FindCheck(report, "Stuck in BSP");
        assert(stuck && stuck->actors == std::vector<std::string>{"MyLevel.SunkStart"} && stuck->severity == Severity::Error);
        auto engine = FindCheck(report, "Stuck in BSP", 1);
        assert(engine && engine->actors[0] == "MyLevel.EngineStart");
        assert(CountCheck(report, "Stuck in BSP") == 2);

        auto voidStart = FindCheck(report, "In the void");
        assert(voidStart && voidStart->actors[0] == "MyLevel.VoidStart" && voidStart->severity == Severity::Error);
        auto voidLight = FindCheck(report, "In the void", 1);
        assert(voidLight && voidLight->actors[0] == "MyLevel.VoidLight" && voidLight->severity == Severity::Note);
        assert(CountCheck(report, "In the void") == 2);

        auto far = FindCheck(report, "Far outside the map");
        assert(far && far->actors[0] == "MyLevel.FarLight" && far->message.find("4,900 units") != std::string::npos);
        assert(far->message.find("in the void") != std::string::npos);
        auto lost = FindCheck(report, "Beyond the world limit");
        assert(lost && lost->actors[0] == "MyLevel.LostStart" && lost->severity == Severity::Error);

        auto mesh = FindCheck(report, "Stuck in a static mesh");
        assert(mesh && mesh->actors == (std::vector<std::string>{"MyLevel.MeshedStart", "MyLevel.StaticMeshActor3"}));

        // The sealed rooms raise no zone problems, and errors sort first.
        assert(!FindCheck(report, "Zones leak into each other") && !FindCheck(report, "Portal does not seal"));
        assert(!FindCheck(report, "Zones without a ZoneInfo"));
        for (size_t i = 1; i < report.problems.size(); ++i) assert(report.problems[i - 1].severity <= report.problems[i].severity);
        assert(report.Count(Severity::Error) == 5 && report.Count(Severity::Warning) == 1 && report.Count(Severity::Note) == 1);

        // A larger tolerance forgives the sunk start; a "far" limit of 0 turns the far check off.
        settings.geometryTolerance = 20;
        settings.farOutside = 0;
        const auto relaxed = Analyse(scene, settings);
        assert(CountCheck(relaxed, "Stuck in BSP") == 1);
        assert(!FindCheck(relaxed, "Far outside the map") && CountCheck(relaxed, "In the void") == 3);
    }

    // A portal that leaves a gap: the rooms are one zone, both ZoneInfos are in
    // it, and the leak path goes round the portal through the gap.
    {
        auto scene = Base(false, 80);
        scene.actors.push_back(Make("ZoneA", kZoneInfo, {50, 50, 50}));
        scene.actors.push_back(Make("ZoneB", kZoneInfo, {150, 50, 50}));
        Settings settings;
        settings.leakGridCells = 200000;
        const auto report = Analyse(scene, settings);
        auto leak = FindCheck(report, "Zones leak into each other");
        assert(leak && leak->severity == Severity::Error);
        assert(leak->actors == (std::vector<std::string>{"MyLevel.ZoneA", "MyLevel.ZoneB"}));
        assert(leak->path.size() >= 3 && !leak->pathThroughPortal);
        assert(leak->path.front() == (Vec3{50, 50, 50}) && leak->path.back() == (Vec3{150, 50, 50}));
        bool throughGap = false;
        for (size_t i = 1; i < leak->path.size(); ++i)
        {
            const auto& a = leak->path[i - 1];
            const auto& b = leak->path[i];
            assert(!SegmentCrossesPolygon(a, b, scene.portals[0].fragments[0]));
            if ((a[0] - 100) * (b[0] - 100) <= 0)
            {
                const double t = a[0] == b[0] ? 0 : (100 - a[0]) / (b[0] - a[0]);
                throughGap = throughGap || a[1] + t * (b[1] - a[1]) > 80;
            }
        }
        assert(throughGap);
        assert(leak->message.find("Leak path") != std::string::npos);

        auto portal = FindCheck(report, "Portal does not seal");
        assert(portal && portal->surfaces == std::vector<int>{7} && portal->actors[0] == "MyLevel.Sheet0");
        assert(portal->severity == Severity::Warning);
        assert(portal->message.find("false alarm") == std::string::npos);
        assert(portal->message.find("zone 1 (ZoneInfo0)") != std::string::npos);

        // When no gap fits the grid (here the sheet fills the opening), the path goes through the portal sheet.
        auto tight = Base(false, 100);
        tight.actors = scene.actors;
        const auto narrow = Analyse(tight, settings);
        auto through = FindCheck(narrow, "Zones leak into each other");
        assert(through && through->pathThroughPortal && through->path.size() >= 2);
        assert(through->message.find("portal sheet") != std::string::npos);
    }

    // Unused and unconnected portals, ZoneInfos in solid, zones without ZoneInfos,
    // the zone limit and the editor's own entries.
    {
        auto scene = Base(true, 100);
        Portal empty;
        empty.surface = 9;
        scene.portals.push_back(empty);
        Portal buried;
        buried.surface = 10;
        buried.brush = "MyLevel.Sheet2";
        buried.fragments.push_back({{300, 0, 0}, {300, 100, 0}, {300, 100, 100}, {300, 0, 100}});
        scene.portals.push_back(buried);
        scene.portalBrushesWithoutFaces.push_back("MyLevel.Sheet3");
        scene.actors.push_back(Make("ZoneA", kZoneInfo, {50, 50, 50}));
        scene.actors.push_back(Make("LostZone", kZoneInfo, {50, 50, -50}));
        scene.stock.push_back({0, "MyLevel.Light4", "Light is in solid"});
        scene.stock.push_back({1, "", "Map has no PlayerStart"});
        scene.stock.push_back({2, "MyLevel.Light5", "A note"});
        const auto report = Analyse(scene, Settings{});
        auto unused = FindCheck(report, "Unused zone portal");
        assert(unused && unused->surfaces == std::vector<int>{9} && unused->actors.empty());
        auto unusedBrush = FindCheck(report, "Unused zone portal", 1);
        assert(unusedBrush && unusedBrush->actors == std::vector<std::string>{"MyLevel.Sheet3"});
        auto unconnected = FindCheck(report, "Unconnected zone portal");
        assert(unconnected && unconnected->surfaces == std::vector<int>{10});
        assert(!FindCheck(report, "Portal does not seal"));
        auto lost = FindCheck(report, "ZoneInfo outside all zones");
        assert(lost && lost->actors[0] == "MyLevel.LostZone");
        auto missing = FindCheck(report, "Zones without a ZoneInfo");
        assert(missing && missing->severity == Severity::Note && missing->message.find("Zone 2 has") == 0);
        assert(CountCheck(report, "Check Map for Errors") == 3);
        auto stock = FindCheck(report, "Check Map for Errors");
        assert(stock->severity == Severity::Error && stock->actors[0] == "MyLevel.Light4" && stock->group == "Editor checks");
        assert(FindCheck(report, "Check Map for Errors", 1)->actors.empty());

        Settings quiet;
        quiet.zonesWithoutZoneInfo = false;
        assert(!FindCheck(Analyse(scene, quiet), "Zones without a ZoneInfo"));

        scene.bsp.numZones = 64;
        assert(FindCheck(Analyse(scene, Settings{}), "Zone limit"));

        // Both faces of one sheet are one row; a face the BSP dropped does not make it unused.
        auto sheet = Base(false, 80);
        Portal back = sheet.portals[0];
        back.surface = 8;
        back.fragments.clear();
        sheet.portals.push_back(back);
        sheet.bsp.numZones = 64;
        const auto merged = Analyse(sheet, Settings{});
        auto row = FindCheck(merged, "Portal does not seal");
        assert(row && row->surfaces == (std::vector<int>{7, 8}) && CountCheck(merged, "Portal does not seal") == 1);
        assert(!FindCheck(merged, "Unused zone portal"));
        assert(row->message.find("false alarm") != std::string::npos);
    }

    // A level open to the outside: space past the bounds is a zone, and the path
    // runs from an actor in that zone out past the geometry.
    {
        Scene scene;
        scene.bsp.rootOutside = false;
        scene.bsp.numZones = 2;
        scene.bsp.nodes.push_back(Wall({0, 0, 1}, 0, -1, 1));   // a floor and nothing else
        scene.boundsKnown = true;
        scene.boundsMin = {0, 0, 0};
        scene.boundsMax = {100, 100, 100};
        scene.actors.push_back(Make("Pickup0", {"SPickup", "Pickup", "Actor"}, {50, 50, 50}));
        Settings settings;
        settings.leakGridCells = 100000;
        const auto report = Analyse(scene, settings);
        auto open = FindCheck(report, "Zone open to the outside");
        assert(open && open->actors[0] == "MyLevel.Pickup0");
        assert(open->path.size() == 2 && open->path.front() == (Vec3{50, 50, 50}));
        assert(open->path.back()[2] > 100);
    }

    // No BSP: one warning, and the zone checks are skipped.
    {
        Scene scene;
        scene.actors.push_back(Make("Start", kPlayerStart, {0, 0, 0}, 40, 40));
        scene.actors.push_back(Make("ZoneA", kZoneInfo, {0, 0, 0}));
        const auto report = Analyse(scene, Settings{});
        assert(!report.bspBuilt);
        assert(FindCheck(report, "BSP not built"));
        assert(report.problems.size() == 1);
    }

    // Check groups are listed for the window and future checks.
    assert(CheckGroups().size() == 3 && std::string(CheckGroups()[0].id) == "actors");

    std::puts("MapCheckModelTests passed");
    return 0;
}
