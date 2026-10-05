#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// Render Budget: what each zone asks the renderer to draw, and what draws
// from the perspective viewport's camera.
//
// The counts come from the editor's own data (RenderBudgetNative.inl):
// - Static meshes: actors drawn as DT_StaticMesh (DrawType 8) with a mesh and
//   not bHidden. A mesh's triangles are its render index buffer
//   (UStaticMesh +0xC4, three indices a triangle), or the "Triangles : %d"
//   count the Static Mesh browser draws (RawTriangles, +0x194) when larger.
// - BSP: nodes with a polygon, attributed to the zone in front of them
//   (FBspNode iZone[1] at +0x59, else iZone[0]); a polygon of n vertices is
//   n - 2 triangles. Invisible and portal surfaces are not drawn.
// - Materials: actor Skins, each static mesh's Materials and each BSP
//   surface's material; textures: every Texture those materials reach.
// - Zones visible from a camera: the zone the camera is in (found by walking
//   the BSP the way UModel::PointRegion does) and every zone reached through
//   a zone portal the view frustum, narrowed portal by portal, still passes.
//   The engine itself culls the same way; the window adds every zone in which
//   the engine actually drew an actor on a fresh repaint of that viewport.
namespace RenderBudget
{
    struct Vec3
    {
        double x = 0, y = 0, z = 0;
    };
    inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
    inline double Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    inline double Length(Vec3 a) { return std::sqrt(Dot(a, a)); }

    // A plane n.x = d; "inside" is n.x - d >= 0.
    struct Plane
    {
        Vec3 n;
        double d = 0;
        double Side(Vec3 p) const { return Dot(n, p) - d; }
    };

    enum class Kind { Other, StaticMesh, Emitter };

    // One actor.
    struct Item
    {
        std::string name;
        int zone = 0;
        Kind kind = Kind::Other;
        bool drawn = false;          // drawn in game: not bHidden, DrawType not None
        int triangles = 0;           // static mesh triangles
        std::vector<int> materials;  // indices into Scene::materials
    };

    // One BSP node with a polygon.
    struct Node
    {
        int zone = 0;
        int surface = -1;
        int triangles = 0;
    };

    struct Surface
    {
        int material = -1;
        bool drawn = true;           // false for invisible and portal surfaces
    };

    struct Material
    {
        std::string name;
        std::vector<int> textures;   // indices into Scene::textures
    };

    struct Scene
    {
        std::vector<Item> items;
        std::vector<Node> nodes;
        std::vector<Surface> surfaces;
        std::vector<Material> materials;
        std::vector<std::string> textures;
        std::map<int, std::string> zoneNames;
    };

    enum Metric { StaticMeshes, Triangles, Actors, BspNodes, Materials, Textures, Emitters, MetricCount };

    inline const char* MetricName(int metric)
    {
        static const char* names[] = {"static meshes", "triangles", "actors", "BSP nodes", "materials", "textures", "emitters"};
        return metric >= 0 && metric < MetricCount ? names[metric] : "";
    }

    // The ini keys, in Metric order.
    inline const char* MetricKey(int metric)
    {
        static const char* keys[] = {"MaxStaticMeshesPerZone", "MaxTrianglesPerZone", "MaxActorsPerZone", "MaxBspNodesPerZone",
                                     "MaxMaterialsPerZone", "MaxTexturesPerZone", "MaxEmittersPerZone"};
        return metric >= 0 && metric < MetricCount ? keys[metric] : "";
    }

    // A zone is flagged when it holds more than the densest zone of any
    // shipped Versus map, measured with this window on 18 stock *D maps
    // (AquaD to ZiopD; TermD's MapsEd copy would not load): the most meshes
    // and actors are StroD's (538, 700), triangles, materials and textures
    // PolaD's (42,735, 81, 92), BSP nodes SancD's (2,030) and emitters
    // FactD's (37). A single mesh is flagged above the heaviest placed in
    // them (StroD, 4,200 triangles).
    struct Guidelines
    {
        std::array<int, MetricCount> perZone{};
        int meshTriangles = 0;
        static constexpr std::array<int, MetricCount> Defaults()
        {
            //       meshes  triangles  actors  nodes  materials textures emitters
            return {{ 538,   42735,     700,    2030,  81,       92,      37 }};
        }
        static constexpr int MeshTrianglesDefault = 4200;
        Guidelines() : perZone(Defaults()), meshTriangles(MeshTrianglesDefault) {}
    };

    inline Guidelines ClampGuidelines(Guidelines g)
    {
        for (auto& value : g.perZone) value = std::clamp(value, 1, 100000000);
        g.meshTriangles = std::clamp(g.meshTriangles, 1, 100000000);
        return g;
    }

    struct Totals
    {
        int actors = 0, drawnActors = 0, staticMeshes = 0, meshTriangles = 0;
        int bspNodes = 0, bspSurfaces = 0, bspTriangles = 0;
        int materials = 0, textures = 0, emitters = 0;
        int Triangles() const { return meshTriangles + bspTriangles; }
        int Value(int metric) const
        {
            switch (metric)
            {
            case StaticMeshes: return staticMeshes;
            case RenderBudget::Triangles: return Triangles();
            case Actors: return actors;
            case BspNodes: return bspNodes;
            case Materials: return materials;
            case Textures: return textures;
            case Emitters: return emitters;
            }
            return 0;
        }
    };

    struct ZoneReport
    {
        int zone = 0;
        std::string name;
        Totals totals;
        std::vector<int> items;      // every actor in the zone
        std::array<bool, MetricCount> over{};
        bool Over() const
        {
            for (bool flag : over) if (flag) return true;
            return false;
        }
    };

    struct Hotspot
    {
        enum class Kind { Zone, Mesh } kind = Kind::Zone;
        int zone = 0;
        int metric = Triangles;      // for a zone hotspot
        int value = 0, limit = 0;
        std::vector<int> items;      // the actors to select
        double Ratio() const { return limit > 0 ? double(value) / limit : 0; }
    };

    struct Report
    {
        std::vector<ZoneReport> zones; // in zone number order
        ZoneReport map;                // whole-map totals
        std::vector<Hotspot> hotspots; // worst first
        Guidelines guidelines;
        bool Over() const { return !hotspots.empty(); }
    };

    namespace Detail
    {
        // Counts unique materials and the textures they reach.
        struct Uniques
        {
            std::set<int> materials, textures;
            void Add(const Scene& scene, int material)
            {
                if (material < 0 || material >= static_cast<int>(scene.materials.size())) return;
                if (!materials.insert(material).second) return;
                for (int texture : scene.materials[material].textures) textures.insert(texture);
            }
        };

        inline std::string ZoneName(const Scene& scene, int zone)
        {
            auto named = scene.zoneNames.find(zone);
            return named != scene.zoneNames.end() && !named->second.empty() ? named->second : "Zone " + std::to_string(zone);
        }

        // Totals over a set of zones (all when zones is null) and a set of
        // items (all items of those zones when items is null).
        inline Totals Sum(const Scene& scene, const std::set<int>* zones, const std::vector<int>* items)
        {
            Totals totals;
            Uniques uniques;
            auto inZones = [&](int zone) { return !zones || zones->count(zone) != 0; };
            auto addItem = [&](int index)
            {
                if (index < 0 || index >= static_cast<int>(scene.items.size())) return;
                const auto& item = scene.items[index];
                ++totals.actors;
                if (item.drawn) ++totals.drawnActors;
                if (item.kind == Kind::StaticMesh && item.drawn)
                {
                    ++totals.staticMeshes;
                    totals.meshTriangles += item.triangles;
                }
                if (item.kind == Kind::Emitter) ++totals.emitters;
                if (item.drawn) for (int material : item.materials) uniques.Add(scene, material);
            };
            if (items) for (int index : *items) addItem(index);
            else
                for (int i = 0; i < static_cast<int>(scene.items.size()); ++i)
                    if (inZones(scene.items[i].zone)) addItem(i);
            std::set<int> surfaces;
            for (const auto& node : scene.nodes)
            {
                if (!inZones(node.zone)) continue;
                if (node.surface >= 0 && node.surface < static_cast<int>(scene.surfaces.size()) && !scene.surfaces[node.surface].drawn) continue;
                ++totals.bspNodes;
                totals.bspTriangles += node.triangles;
                if (node.surface >= 0 && surfaces.insert(node.surface).second && node.surface < static_cast<int>(scene.surfaces.size()))
                    uniques.Add(scene, scene.surfaces[node.surface].material);
            }
            totals.bspSurfaces = static_cast<int>(surfaces.size());
            totals.materials = static_cast<int>(uniques.materials.size());
            totals.textures = static_cast<int>(uniques.textures.size());
            return totals;
        }
    }

    inline Report Analyse(const Scene& scene, Guidelines guidelines)
    {
        guidelines = ClampGuidelines(guidelines);
        Report report;
        report.guidelines = guidelines;
        std::set<int> zoneNumbers;
        for (const auto& [number, name] : scene.zoneNames) zoneNumbers.insert(number);
        for (const auto& item : scene.items) zoneNumbers.insert(item.zone);
        for (const auto& node : scene.nodes) zoneNumbers.insert(node.zone);
        for (int number : zoneNumbers)
        {
            ZoneReport zone;
            zone.zone = number;
            zone.name = Detail::ZoneName(scene, number);
            const std::set<int> only{number};
            zone.totals = Detail::Sum(scene, &only, nullptr);
            for (int i = 0; i < static_cast<int>(scene.items.size()); ++i)
                if (scene.items[i].zone == number) zone.items.push_back(i);
            for (int metric = 0; metric < MetricCount; ++metric)
            {
                const int value = zone.totals.Value(metric), limit = guidelines.perZone[metric];
                zone.over[metric] = value > limit;
                if (zone.over[metric])
                {
                    Hotspot hotspot{Hotspot::Kind::Zone, number, metric, value, limit, {}};
                    for (int i : zone.items)
                    {
                        const auto& item = scene.items[i];
                        const bool relevant = metric == StaticMeshes || metric == Triangles ? item.kind == Kind::StaticMesh && item.drawn
                                            : metric == Emitters ? item.kind == Kind::Emitter
                                            : metric == Materials || metric == Textures ? item.drawn && !item.materials.empty()
                                            : metric == Actors;
                        if (relevant) hotspot.items.push_back(i);
                    }
                    report.hotspots.push_back(hotspot);
                }
            }
            report.zones.push_back(std::move(zone));
        }
        report.map.zone = -1;
        report.map.name = "Whole map";
        report.map.totals = Detail::Sum(scene, nullptr, nullptr);
        for (int i = 0; i < static_cast<int>(scene.items.size()); ++i) report.map.items.push_back(i);
        for (int i = 0; i < static_cast<int>(scene.items.size()); ++i)
        {
            const auto& item = scene.items[i];
            if (item.kind == Kind::StaticMesh && item.drawn && item.triangles > guidelines.meshTriangles)
                report.hotspots.push_back({Hotspot::Kind::Mesh, item.zone, Triangles, item.triangles, guidelines.meshTriangles, {i}});
        }
        std::stable_sort(report.hotspots.begin(), report.hotspots.end(), [](const Hotspot& a, const Hotspot& b)
        {
            if (a.Ratio() != b.Ratio()) return a.Ratio() > b.Ratio();
            if (a.kind != b.kind) return a.kind == Hotspot::Kind::Zone;
            return a.zone < b.zone;
        });
        if (report.hotspots.size() > 300) report.hotspots.resize(300);
        return report;
    }

    // "Triangles: 154,885 (guideline 120,000)" style numbers.
    inline std::string Thousands(long long value)
    {
        std::string digits = std::to_string(value < 0 ? -value : value), text;
        for (size_t i = 0; i < digits.size(); ++i)
        {
            if (i && (digits.size() - i) % 3 == 0) text += ',';
            text += digits[i];
        }
        return value < 0 ? "-" + text : text;
    }

    inline std::string HotspotLabel(const Hotspot& hotspot)
    {
        if (hotspot.kind == Hotspot::Kind::Mesh) return "Heavy mesh";
        std::string label = MetricName(hotspot.metric);
        if (!label.empty()) label[0] = static_cast<char>(label[0] - 'a' + 'A');
        return label;
    }

    inline std::string HotspotText(const Hotspot& hotspot)
    {
        return Thousands(hotspot.value) + (hotspot.kind == Hotspot::Kind::Mesh ? " triangles in one mesh" : std::string(" ") + MetricName(hotspot.metric))
            + " (guideline " + Thousands(hotspot.limit) + ")";
    }

    // ---- What renders from a camera -------------------------------------

    // One BSP node for the point-in-zone walk: its plane, children
    // (iChild[0] back, iChild[1] front; -1 for none) and the zones and leaves
    // on its back and front.
    struct BspNode
    {
        Plane plane;
        int back = -1, front = -1;
        int zone[2] = {0, 0};
    };

    // UModel::PointRegion: descend from node 0 by the side of each plane
    // (front when the plane dot is >= 0) and take the last node's zone on
    // that side. -1 when there is no BSP.
    inline int PointZone(const std::vector<BspNode>& nodes, Vec3 point)
    {
        if (nodes.empty()) return -1;
        int node = 0, parent = 0;
        bool front = false;
        for (size_t steps = 0; node >= 0 && node < static_cast<int>(nodes.size()) && steps <= nodes.size(); ++steps)
        {
            parent = node;
            front = nodes[node].plane.Side(point) >= 0;
            node = front ? nodes[node].front : nodes[node].back;
        }
        return nodes[parent].zone[front ? 1 : 0];
    }

    struct Camera
    {
        Vec3 position;
        int pitch = 0, yaw = 0, roll = 0; // Unreal rotation units, 65536 a turn
        double fov = 90;                  // horizontal, degrees
        double aspect = 4.0 / 3.0;        // width / height
    };

    // Forward, right and up of an Unreal rotation (X forward, Y right, Z up).
    inline std::array<Vec3, 3> Axes(const Camera& camera)
    {
        const double unit = 3.14159265358979323846 * 2 / 65536;
        const double p = camera.pitch * unit, y = camera.yaw * unit, r = camera.roll * unit;
        const double sp = std::sin(p), cp = std::cos(p), sy = std::sin(y), cy = std::cos(y), sr = std::sin(r), cr = std::cos(r);
        // FRotationMatrix's axes.
        const Vec3 forward{cp * cy, cp * sy, sp};
        const Vec3 right{sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp};
        const Vec3 up{-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp};
        return {forward, right, up};
    }

    // The four side planes of the view, through the camera, facing inwards.
    inline std::vector<Plane> Frustum(const Camera& camera)
    {
        const auto [forward, right, up] = Axes(camera);
        const double fov = std::clamp(camera.fov, 1.0, 170.0) * 3.14159265358979323846 / 180;
        const double tanX = std::tan(fov / 2), tanY = tanX / std::max(0.1, camera.aspect);
        const Vec3 corners[4] = {forward + right * tanX + up * tanY, forward - right * tanX + up * tanY,
                                 forward - right * tanX - up * tanY, forward + right * tanX - up * tanY};
        std::vector<Plane> planes;
        for (int i = 0; i < 4; ++i)
        {
            Vec3 n = Cross(corners[i], corners[(i + 1) % 4]);
            if (Dot(n, forward) < 0) n = n * -1;
            const double length = Length(n);
            if (length <= 0) continue;
            n = n * (1 / length);
            planes.push_back({n, Dot(n, camera.position)});
        }
        return planes;
    }

    // Sutherland-Hodgman: the part of a convex polygon on the inside of a plane.
    inline std::vector<Vec3> Clip(const std::vector<Vec3>& polygon, const Plane& plane, double epsilon = 0.01)
    {
        std::vector<Vec3> out;
        for (size_t i = 0; i < polygon.size(); ++i)
        {
            const Vec3 a = polygon[i], b = polygon[(i + 1) % polygon.size()];
            const double da = plane.Side(a), db = plane.Side(b);
            if (da >= -epsilon) out.push_back(a);
            if ((da < -epsilon && db > epsilon) || (da > epsilon && db < -epsilon))
                out.push_back(a + (b - a) * (da / (da - db)));
        }
        return out;
    }

    // One fragment of a zone portal: a convex polygon between two zones.
    struct Portal
    {
        int zones[2] = {0, 0};
        std::vector<Vec3> polygon;
    };

    struct Visibility
    {
        int cameraZone = -1;
        std::set<int> zones;        // seen through portals, the camera's zone first
        bool allZones = false;      // no zone to start from: nothing is culled
        bool exact = true;          // false when the search was cut short
    };

    // Portal traversal from the camera's zone: a zone is visible when some
    // chain of portals to it stays inside the frustum, each portal narrowing
    // it to the planes through the camera and that portal's clipped edges.
    inline Visibility VisibleZones(const Camera& camera, int cameraZone, const std::vector<Portal>& portals, size_t budget = 200000)
    {
        Visibility result;
        result.cameraZone = cameraZone;
        if (cameraZone <= 0)
        {
            // Zone 0 is outside every zone (in solid space or a map with no
            // zones); the renderer cannot cull by portal from there.
            result.allZones = true;
            return result;
        }
        result.zones.insert(cameraZone);
        std::map<int, std::vector<int>> byZone;
        for (int i = 0; i < static_cast<int>(portals.size()); ++i)
        {
            const auto& portal = portals[i];
            if (portal.polygon.size() < 3 || portal.zones[0] == portal.zones[1]) continue;
            byZone[portal.zones[0]].push_back(i);
            byZone[portal.zones[1]].push_back(i);
        }
        size_t steps = 0;
        std::vector<int> path{cameraZone};
        std::vector<Plane> start = Frustum(camera);
        // Recursion by explicit lambda; depth is bounded by the zone count (64).
        struct Walker
        {
            const Camera& camera;
            const std::vector<Portal>& portals;
            std::map<int, std::vector<int>>& byZone;
            Visibility& result;
            std::vector<int>& path;
            size_t& steps;
            size_t budget;
            void Walk(int zone, const std::vector<Plane>& planes)
            {
                for (int index : byZone[zone])
                {
                    if (++steps > budget) { result.exact = false; return; }
                    const auto& portal = portals[index];
                    const int other = portal.zones[0] == zone ? portal.zones[1] : portal.zones[0];
                    if (other <= 0 || std::find(path.begin(), path.end(), other) != path.end()) continue;
                    // The camera standing in the portal's plane sees through
                    // all of it: keep the planes it came with.
                    const Vec3 normal = Cross(portal.polygon[1] - portal.polygon[0], portal.polygon[2] - portal.polygon[0]);
                    const double area = Length(normal);
                    Vec3 low = portal.polygon[0], high = portal.polygon[0];
                    for (const auto& p : portal.polygon)
                    {
                        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
                    }
                    const Vec3 at = camera.position;
                    const bool within = at.x > low.x - 1 && at.y > low.y - 1 && at.z > low.z - 1 && at.x < high.x + 1 && at.y < high.y + 1 && at.z < high.z + 1;
                    if (within && area > 0 && std::abs(Dot(normal * (1 / area), camera.position - portal.polygon[0])) < 1.0)
                    {
                        result.zones.insert(other);
                        path.push_back(other);
                        Walk(other, planes);
                        path.pop_back();
                        if (!result.exact) return;
                        continue;
                    }
                    std::vector<Vec3> polygon = portal.polygon;
                    for (const auto& plane : planes)
                    {
                        polygon = Clip(polygon, plane);
                        if (polygon.size() < 3) break;
                    }
                    if (polygon.size() < 3) continue;
                    // The planes through the camera and each clipped edge,
                    // facing the polygon's centre.
                    Vec3 centre;
                    for (const auto& p : polygon) centre = centre + p;
                    centre = centre * (1.0 / polygon.size());
                    std::vector<Plane> narrowed;
                    bool degenerate = false;
                    for (size_t i = 0; i < polygon.size(); ++i)
                    {
                        Vec3 n = Cross(polygon[i] - camera.position, polygon[(i + 1) % polygon.size()] - camera.position);
                        const double length = Length(n);
                        if (length < 1e-6) continue;
                        n = n * (1 / length);
                        Plane plane{n, Dot(n, camera.position)};
                        if (plane.Side(centre) < 0) plane = {n * -1, -plane.d};
                        if (std::abs(plane.Side(centre)) < 1e-3) degenerate = true;
                        narrowed.push_back(plane);
                    }
                    // A sliver too thin to give planes: keep the ones it came with.
                    if (degenerate || narrowed.size() < 3) narrowed = planes;
                    result.zones.insert(other);
                    path.push_back(other);
                    Walk(other, narrowed);
                    path.pop_back();
                    if (!result.exact) return;
                }
            }
        } walker{camera, portals, byZone, result, path, steps, budget};
        walker.Walk(cameraZone, start);
        return result;
    }

    // The view's totals: BSP and actors of the visible zones as the zone
    // totals count them, plus the actors the engine drew.
    struct ViewReport
    {
        Visibility visibility;
        std::set<int> zones;         // portal zones plus zones the engine drew actors in
        std::vector<int> drawnItems; // actors the engine drew from the camera
        Totals totals;               // drawn actors and the visible zones' BSP
        int mapTriangles = 0;
    };

    inline ViewReport AnalyseView(const Scene& scene, const Visibility& visibility, const std::vector<int>& drawnItems)
    {
        ViewReport view;
        view.visibility = visibility;
        view.drawnItems = drawnItems;
        std::sort(view.drawnItems.begin(), view.drawnItems.end());
        view.drawnItems.erase(std::unique(view.drawnItems.begin(), view.drawnItems.end()), view.drawnItems.end());
        if (visibility.allZones)
        {
            for (const auto& [number, name] : scene.zoneNames) view.zones.insert(number);
            for (const auto& node : scene.nodes) view.zones.insert(node.zone);
            for (const auto& item : scene.items) view.zones.insert(item.zone);
        }
        else view.zones = visibility.zones;
        // Only what was drawn counts among the actors; the BSP counts for
        // the zones seen through the portals, not for those the engine only
        // drew an actor in (a large actor reaching into a visible zone).
        const std::set<int> bspZones = view.zones;
        for (int index : view.drawnItems)
            if (index >= 0 && index < static_cast<int>(scene.items.size())) view.zones.insert(scene.items[index].zone);
        std::vector<int> drawn;
        for (int index : view.drawnItems)
            if (index >= 0 && index < static_cast<int>(scene.items.size()) && scene.items[index].drawn) drawn.push_back(index);
        view.totals = Detail::Sum(scene, &bspZones, &drawn);
        view.mapTriangles = Detail::Sum(scene, nullptr, nullptr).Triangles();
        return view;
    }

    inline std::string Percent(long long part, long long whole)
    {
        if (whole <= 0) return "-";
        const long long tenths = (part * 1000 + whole / 2) / whole;
        return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "%";
    }
}
