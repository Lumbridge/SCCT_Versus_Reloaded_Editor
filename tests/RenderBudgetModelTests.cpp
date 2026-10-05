#include "../Reloaded.Editor/RenderBudgetModel.h"
#include <cassert>
#include <cmath>
#include <string>

using namespace RenderBudget;

namespace
{
    Item Mesh(const char* name, int zone, int triangles, std::vector<int> materials = {})
    {
        Item item; item.name = name; item.zone = zone; item.kind = Kind::StaticMesh; item.drawn = true;
        item.triangles = triangles; item.materials = materials; return item;
    }
    Item Other(const char* name, int zone, Kind kind, bool drawn)
    {
        Item item; item.name = name; item.zone = zone; item.kind = kind; item.drawn = drawn; return item;
    }
    bool Near(double a, double b) { return std::abs(a - b) < 1e-6; }
    // A square portal in the plane x = at, spanning y and z from -size to size.
    Portal Door(int a, int b, double at, double size, double centreY = 0)
    {
        Portal portal; portal.zones[0] = a; portal.zones[1] = b;
        portal.polygon = {{at, centreY - size, -size}, {at, centreY + size, -size}, {at, centreY + size, size}, {at, centreY - size, size}};
        return portal;
    }
}

int main()
{
    // A scene: zone 1 has two meshes (one hidden in game), an emitter and a
    // hidden editor actor; zone 2 one heavy mesh; the BSP has polygons in
    // both zones, an invisible one and a portal.
    Scene scene;
    scene.materials = {{"M0", {0, 1}}, {"M1", {1}}, {"M2", {2}}, {"Wall", {3}}};
    scene.textures = {"T0", "T1", "T2", "T3"};
    scene.items = {
        Mesh("Crate", 1, 100, {0, 1}),
        Mesh("Ghost", 1, 900, {2}),
        Other("Sparks", 1, Kind::Emitter, true),
        Other("PathNode", 1, Kind::Other, false),
        Mesh("Statue", 2, 20000, {1})};
    scene.items[1].drawn = false; // bHidden: neither its triangles nor its materials count
    scene.surfaces = {{3, true}, {3, true}, {0, false}, {-1, false}};
    scene.nodes = {{1, 0, 2}, {1, 0, 1}, {1, 1, 4}, {2, 2, 10}, {2, 3, 2}, {2, 1, 3}};
    scene.zoneNames = {{1, "Hall"}, {2, ""}, {3, "Empty"}};

    Guidelines g;
    assert(g.perZone == Guidelines::Defaults() && g.meshTriangles == Guidelines::MeshTrianglesDefault);
    g.perZone[StaticMeshes] = 1;
    g.perZone[Triangles] = 1000;
    g.perZone[Emitters] = 5;
    g.meshTriangles = 15000;
    auto report = Analyse(scene, g);
    assert(report.zones.size() == 3);
    const auto& hall = report.zones[0];
    assert(hall.zone == 1 && hall.name == "Hall");
    assert(hall.totals.actors == 4 && hall.totals.drawnActors == 2);
    assert(hall.totals.staticMeshes == 1 && hall.totals.meshTriangles == 100);
    assert(hall.totals.bspNodes == 3 && hall.totals.bspSurfaces == 2 && hall.totals.bspTriangles == 7);
    assert(hall.totals.Triangles() == 107);
    // Crate's M0 and M1 plus the walls' material; T0, T1 and T3.
    assert(hall.totals.materials == 3 && hall.totals.textures == 3);
    assert(hall.totals.emitters == 1 && !hall.Over());
    const auto& second = report.zones[1];
    assert(second.name == "Zone 2" && second.totals.staticMeshes == 1 && second.totals.meshTriangles == 20000);
    // The invisible and portal surfaces are not drawn.
    assert(second.totals.bspNodes == 1 && second.totals.bspTriangles == 3);
    assert(second.over[Triangles] && !second.over[StaticMeshes] && second.Over());
    assert(report.zones[2].name == "Empty" && report.zones[2].totals.actors == 0);
    assert(report.map.totals.actors == 5 && report.map.totals.Triangles() == 20110 && report.map.totals.materials == 3);
    // Hotspots, worst first: zone 2's triangles (20003 / 1000) before the
    // heavy mesh (20000 / 15000).
    assert(report.hotspots.size() == 2 && report.Over());
    assert(report.hotspots[0].kind == Hotspot::Kind::Zone && report.hotspots[0].zone == 2 && report.hotspots[0].metric == Triangles);
    assert(report.hotspots[0].value == 20003 && report.hotspots[0].limit == 1000);
    assert((report.hotspots[0].items == std::vector<int>{4}));
    assert(report.hotspots[1].kind == Hotspot::Kind::Mesh && (report.hotspots[1].items == std::vector<int>{4}));
    assert(HotspotLabel(report.hotspots[0]) == "Triangles" && HotspotLabel(report.hotspots[1]) == "Heavy mesh");
    assert(HotspotText(report.hotspots[0]) == "20,003 triangles (guideline 1,000)");
    assert(HotspotText(report.hotspots[1]) == "20,000 triangles in one mesh (guideline 15,000)");
    // Over the mesh count: only drawn static meshes are listed.
    g.perZone[StaticMeshes] = 0; // clamps to 1
    g.perZone[Triangles] = 100000;
    g.meshTriangles = 100000;
    auto clamped = Analyse(scene, g);
    assert(clamped.guidelines.perZone[StaticMeshes] == 1 && clamped.hotspots.empty());
    assert(Thousands(0) == "0" && Thousands(999) == "999" && Thousands(1234567) == "1,234,567" && Thousands(-1500) == "-1,500");
    assert(Percent(1, 3) == "33.3%" && Percent(5, 0) == "-");
    assert(std::string(MetricKey(Emitters)) == "MaxEmittersPerZone");

    // Point in zone: a BSP split at x = 0 (front: zone 2, back: zone 1),
    // the front split again at y = 0 (front zone 3).
    std::vector<BspNode> bsp(2);
    bsp[0].plane = {{1, 0, 0}, 0}; bsp[0].front = 1; bsp[0].zone[0] = 1; bsp[0].zone[1] = 2;
    bsp[1].plane = {{0, 1, 0}, 0}; bsp[1].zone[0] = 2; bsp[1].zone[1] = 3;
    assert(PointZone(bsp, {-5, 0, 0}) == 1);
    assert(PointZone(bsp, {5, -5, 0}) == 2);
    assert(PointZone(bsp, {5, 5, 0}) == 3);
    assert(PointZone({}, {0, 0, 0}) == -1);

    // Camera axes: yaw 16384 (a quarter turn) looks along +Y.
    Camera camera;
    camera.yaw = 16384;
    auto axes = Axes(camera);
    assert(Near(axes[0].x, 0) && Near(axes[0].y, 1) && Near(axes[0].z, 0));
    camera.yaw = 0; camera.pitch = 16384;
    axes = Axes(camera);
    assert(Near(axes[0].z, 1));
    // The frustum: four planes through the camera, the view direction inside.
    camera = Camera{};
    camera.fov = 90; camera.aspect = 1;
    auto planes = Frustum(camera);
    assert(planes.size() == 4);
    for (const auto& plane : planes) assert(plane.Side({100, 0, 0}) > 0);
    auto inside = [&](Vec3 p) { for (const auto& plane : planes) if (plane.Side(p) < -1e-6) return false; return true; };
    assert(inside({100, 90, 0}) && !inside({100, 110, 0}) && !inside({-100, 0, 0}));

    // Clipping a square by a plane through its middle keeps half.
    std::vector<Vec3> square{{0, -1, -1}, {0, 1, -1}, {0, 1, 1}, {0, -1, 1}};
    auto half = Clip(square, {{0, 1, 0}, 0});
    assert(half.size() == 4);
    for (const auto& p : half) assert(p.y >= -1e-9);
    assert(Clip(square, {{0, 1, 0}, 5}).empty());

    // Portals: the camera at the origin looking along +X, in zone 1. A door
    // ahead at x = 100 leads to zone 2 and on through a door at x = 300 to
    // zone 3. A door behind (x = -100) to zone 4 is out of view; a door at
    // x = 200 off to the side (y = 2000) to zone 5 is outside the frustum.
    // Zone 3 has a door to zone 6 at x = 500, far off-axis (y = 600): inside
    // the full frustum but not through the first door.
    std::vector<Portal> portals{
        Door(1, 2, 100, 50), Door(2, 3, 300, 50), Door(4, 1, -100, 50), Door(2, 5, 200, 50, 2000), Door(3, 6, 500, 50, 400)};
    camera = Camera{};
    camera.fov = 90; camera.aspect = 1;
    auto seen = VisibleZones(camera, 1, portals);
    assert(seen.exact && !seen.allZones && seen.cameraZone == 1);
    assert((seen.zones == std::set<int>{1, 2, 3}));
    // Turned round, the camera sees zone 4 only.
    camera.yaw = 32768;
    seen = VisibleZones(camera, 1, portals);
    assert((seen.zones == std::set<int>{1, 4}));
    // Outside every zone, nothing is culled.
    seen = VisibleZones(camera, 0, portals);
    assert(seen.allZones && seen.zones.empty());
    // A camera standing in a portal's plane sees through it.
    camera = Camera{};
    camera.position = {100, 0, 0};
    camera.fov = 90; camera.aspect = 1;
    seen = VisibleZones(camera, 1, portals);
    assert(seen.zones.count(2) && seen.zones.count(3));
    // A cut-short search says so.
    seen = VisibleZones(Camera{}, 1, portals, 1);
    assert(!seen.exact);

    // The view: BSP of the visible zones plus what the engine drew, which
    // also adds the zones those actors stand in.
    Visibility visibility;
    visibility.cameraZone = 1;
    visibility.zones = {1};
    auto view = AnalyseView(scene, visibility, {0, 0, 2, 3, 4});
    assert((view.zones == std::set<int>{1, 2}));
    assert((view.drawnItems == std::vector<int>{0, 2, 3, 4}));
    // Drawn: Crate, Sparks and Statue (PathNode is hidden in game).
    assert(view.totals.actors == 3 && view.totals.staticMeshes == 2 && view.totals.meshTriangles == 20100);
    assert(view.totals.bspTriangles == 7 && view.totals.emitters == 1); // zone 2 only drew an actor
    assert(view.totals.Triangles() == 20107 && view.mapTriangles == 20110);
    assert(Percent(view.totals.Triangles(), view.mapTriangles) == "100.0%" && Percent(7, 20110) == "0.0%");
    Visibility everything;
    everything.allZones = true;
    view = AnalyseView(scene, everything, {});
    assert((view.zones == std::set<int>{1, 2, 3}) && view.totals.bspTriangles == 10 && view.totals.actors == 0);
    return 0;
}
