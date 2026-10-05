#include "pch.h"
#undef min
#undef max
#include "RenderBudget.h"
#include "RenderBudgetModel.h"
#include "BudgetWindow.h"
#include "WorkflowEditor.h"
#include "logger.h"
#include <commctrl.h>
#include <stdexcept>
#pragma comment(lib, "comctl32.lib")

namespace RenderBudget
{
namespace
{
const char kTitle[] = "Render Budget";
const char kSection[] = "RenderBudget";

enum : int
{
    kZoneList = 100, kHotspotList, kStatus, kRefresh, kSelect, kFrame, kIntro, kView, kHotspotLabel = 110
};

// Row data in the zone list: zones by index, then these.
constexpr LPARAM kMapRow = -1, kViewRow = -2;

Guidelines LoadGuidelines()
{
    Guidelines g;
    for (int metric = 0; metric < MetricCount; ++metric)
        g.perZone[metric] = Budget::IniInt(kSection, MetricKey(metric), g.perZone[metric]);
    g.meshTriangles = Budget::IniInt(kSection, "MaxTrianglesPerMesh", g.meshTriangles);
    return ClampGuidelines(g);
}

struct Snapshot
{
    Scene scene;
    Report report;
    std::vector<std::string> paths;
    unsigned generation = 0;
    bool hasView = false;
    ViewReport view;
    std::string cameraName;
};

Snapshot Measure(const Guidelines& guidelines)
{
    const auto json = Workflow::Editor::RenderBudgetScene();
    Snapshot snapshot;
    auto& scene = snapshot.scene;
    for (const auto& row : json.at("items"))
    {
        Item item;
        item.name = row.at("name").get<std::string>();
        item.zone = row.at("zone").get<int>();
        item.kind = static_cast<Kind>(row.at("kind").get<int>());
        item.drawn = row.at("drawn").get<bool>();
        item.triangles = row.at("triangles").get<int>();
        item.materials = row.at("materials").get<std::vector<int>>();
        scene.items.push_back(std::move(item));
    }
    snapshot.paths = json.at("paths").get<std::vector<std::string>>();
    for (const auto& row : json.at("nodes"))
        scene.nodes.push_back({row.at(0).get<int>(), row.at(1).get<int>(), row.at(2).get<int>()});
    for (const auto& row : json.at("surfaces"))
        scene.surfaces.push_back({row.at("material").get<int>(), row.at("drawn").get<bool>()});
    for (const auto& row : json.at("materials"))
        scene.materials.push_back({row.at("name").get<std::string>(), row.at("textures").get<std::vector<int>>()});
    scene.textures = json.at("textures").get<std::vector<std::string>>();
    for (const auto& [number, name] : json.at("zones").items())
        scene.zoneNames[std::stoi(number)] = name.get<std::string>();
    snapshot.report = Analyse(scene, guidelines);
    snapshot.generation = json.at("generation").get<unsigned>();

    // The worst zone of each kind and the heaviest mesh: how the guideline
    // defaults were read off the shipped maps.
    std::array<int, MetricCount> worst{};
    int heaviest = 0;
    for (const auto& zone : snapshot.report.zones)
        for (int metric = 0; metric < MetricCount; ++metric) worst[metric] = std::max(worst[metric], zone.totals.Value(metric));
    for (const auto& item : scene.items)
        if (item.kind == Kind::StaticMesh && item.drawn) heaviest = std::max(heaviest, item.triangles);
    std::string text = "RenderBudget: " + json.at("map").get<std::string>() + " worst zone:";
    for (int metric = 0; metric < MetricCount; ++metric) text += std::string(" ") + MetricName(metric) + " " + std::to_string(worst[metric]) + ";";
    const auto& map = snapshot.report.map.totals;
    text += " heaviest mesh " + std::to_string(heaviest) + "; map " + std::to_string(map.staticMeshes) + " meshes, "
        + std::to_string(map.Triangles()) + " triangles, " + std::to_string(snapshot.report.hotspots.size()) + " hotspot(s)";
    Logger::log(text);
    return snapshot;
}

std::string Intro(const Guidelines& g)
{
    return "What each zone asks the renderer to draw. Static meshes and their triangles count actors drawn in game; BSP counts "
           "the polygons in front of which the zone lies; materials and textures are the unique ones used there. Zones are "
           "flagged above the densest zone of any shipped Versus map (" + Thousands(g.perZone[StaticMeshes]) + " meshes, "
           + Thousands(g.perZone[Triangles]) + " triangles, " + Thousands(g.perZone[Emitters]) + " emitters...), meshes above "
           + Thousands(g.meshTriangles) + " triangles; change them in Reloaded_Editor.ini [RenderBudget]. What Renders From "
           "Camera repaints the perspective viewport: View marks the camera's zone, the zones Visible through the portals "
           "and those no portal shows where the engine still Drew an actor. Click a row to select its actors; "
           "double-click to frame them too.";
}

std::string Status(const ZoneReport& zone)
{
    if (!zone.Over()) return "OK";
    std::string text = "Over:";
    bool first = true;
    for (int metric = 0; metric < MetricCount; ++metric)
        if (zone.over[metric]) { text += std::string(first ? " " : ", ") + MetricName(metric); first = false; }
    return text;
}

struct Window
{
    HWND window = nullptr;
    Snapshot snapshot;
    bool measured = false;
    int lastList = 0, lastRow = -1;
} budget;

void SetStatus(const std::string& text)
{
    if (IsWindow(budget.window)) SetDlgItemTextA(budget.window, kStatus, text.c_str());
}

std::string ReportStatus()
{
    const auto& report = budget.snapshot.report;
    size_t over = 0;
    for (const auto& zone : report.zones) over += zone.Over();
    const auto& map = report.map.totals;
    std::string text = Thousands(map.staticMeshes) + " static meshes, " + Thousands(map.Triangles()) + " triangles ("
        + Thousands(map.meshTriangles) + " mesh, " + Thousands(map.bspTriangles) + " BSP), " + Thousands(map.materials)
        + " materials in " + std::to_string(report.zones.size()) + " zone(s); " + std::to_string(over) + " zone(s) over the guidelines.";
    return text;
}

std::string ViewStatus()
{
    const auto& snapshot = budget.snapshot;
    const auto& view = snapshot.view;
    std::string text = "From " + snapshot.cameraName + ": ";
    if (view.visibility.allZones) text += "the camera is outside every zone, so nothing is culled by portals; ";
    else text += std::to_string(view.zones.size()) + " zone(s) visible (camera in zone " + std::to_string(view.visibility.cameraZone) + "); ";
    text += Thousands(view.totals.staticMeshes) + " static meshes and " + Thousands(view.totals.Triangles()) + " triangles drawn, "
        + Percent(view.totals.Triangles(), view.mapTriangles) + " of the map.";
    if (!view.visibility.exact) text += " The portal search stopped early; more zones may be visible.";
    return text;
}

void Fill()
{
    auto zones = GetDlgItem(budget.window, kZoneList), hotspots = GetDlgItem(budget.window, kHotspotList);
    ListView_DeleteAllItems(zones);
    ListView_DeleteAllItems(hotspots);
    const auto& snapshot = budget.snapshot;
    const auto& report = snapshot.report;
    auto zoneRow = [&](int row, LPARAM data, const std::string& name, const Totals& t, const std::string& status, const std::string& inView)
    {
        Budget::Row(zones, row, data, {name, Thousands(t.actors), Thousands(t.drawnActors), Thousands(t.staticMeshes), Thousands(t.meshTriangles),
            Thousands(t.bspNodes), Thousands(t.bspSurfaces), Thousands(t.bspTriangles), Thousands(t.Triangles()),
            Thousands(t.materials), Thousands(t.textures), Thousands(t.emitters), status, inView});
    };
    int row = 0;
    zoneRow(row++, kMapRow, report.map.name, report.map.totals, report.Over() ? "Over" : "OK", "");
    if (snapshot.hasView) zoneRow(row++, kViewRow, "From camera (" + snapshot.cameraName + ")", snapshot.view.totals, "", "");
    for (size_t i = 0; i < report.zones.size(); ++i)
    {
        const auto& zone = report.zones[i];
        std::string inView;
        if (snapshot.hasView && snapshot.view.zones.count(zone.zone))
            inView = zone.zone == snapshot.view.visibility.cameraZone ? "Camera" : snapshot.view.visibility.zones.count(zone.zone) ? "Visible" : "Drawn";
        zoneRow(row++, static_cast<LPARAM>(i), zone.name, zone.totals, Status(zone), inView);
    }
    for (size_t i = 0; i < report.hotspots.size(); ++i)
    {
        const auto& hotspot = report.hotspots[i];
        std::string zone = "Zone " + std::to_string(hotspot.zone), what;
        for (const auto& z : report.zones) if (z.zone == hotspot.zone) zone = z.name;
        if (hotspot.kind == Hotspot::Kind::Mesh && !hotspot.items.empty()) what = snapshot.scene.items.at(static_cast<size_t>(hotspot.items[0])).name;
        else what = std::to_string(hotspot.items.size()) + " actor(s)";
        Budget::Row(hotspots, static_cast<int>(i), static_cast<LPARAM>(i), {HotspotLabel(hotspot), zone, HotspotText(hotspot), what});
    }
    SetDlgItemTextA(budget.window, kIntro, Intro(report.guidelines).c_str());
    SetStatus(snapshot.hasView ? ViewStatus() : ReportStatus());
}

void Refresh()
{
    budget.snapshot = Measure(LoadGuidelines());
    budget.measured = true;
    budget.lastRow = -1;
    if (IsWindow(budget.window)) Fill();
}

void MeasureView()
{
    if (!budget.measured || budget.snapshot.generation != Workflow::Editor::MapGeneration()) Refresh();
    const auto json = Workflow::Editor::RenderBudgetView();
    std::vector<BspNode> nodes;
    for (const auto& row : json.at("bsp"))
    {
        BspNode node;
        node.plane = {{row.at(0).get<double>(), row.at(1).get<double>(), row.at(2).get<double>()}, row.at(3).get<double>()};
        node.back = row.at(4).get<int>();
        node.front = row.at(5).get<int>();
        node.zone[0] = row.at(6).get<int>();
        node.zone[1] = row.at(7).get<int>();
        nodes.push_back(node);
    }
    std::vector<Portal> portals;
    for (const auto& row : json.at("portals"))
    {
        Portal portal;
        portal.zones[0] = row.at("zones").at(0).get<int>();
        portal.zones[1] = row.at("zones").at(1).get<int>();
        for (const auto& p : row.at("polygon")) portal.polygon.push_back({p.at(0).get<double>(), p.at(1).get<double>(), p.at(2).get<double>()});
        portals.push_back(std::move(portal));
    }
    const auto& c = json.at("camera");
    Camera camera;
    camera.position = {c.at("position").at(0).get<double>(), c.at("position").at(1).get<double>(), c.at("position").at(2).get<double>()};
    camera.pitch = c.at("rotation").at(0).get<int>();
    camera.yaw = c.at("rotation").at(1).get<int>();
    camera.roll = c.at("rotation").at(2).get<int>();
    camera.fov = c.at("fov").get<double>();
    camera.aspect = c.at("aspect").get<double>();
    const int cameraZone = PointZone(nodes, camera.position);
    const auto visibility = VisibleZones(camera, cameraZone, portals);
    // The engine's drawn actors, by path, as indices into the scene.
    std::map<std::string, int> byPath;
    for (int i = 0; i < static_cast<int>(budget.snapshot.paths.size()); ++i) byPath[Workflow::Fold(budget.snapshot.paths[i])] = i;
    std::vector<int> drawn;
    for (const auto& path : json.at("drawn"))
        if (auto found = byPath.find(Workflow::Fold(path.get<std::string>())); found != byPath.end()) drawn.push_back(found->second);
    auto& snapshot = budget.snapshot;
    snapshot.view = AnalyseView(snapshot.scene, visibility, drawn);
    snapshot.hasView = true;
    snapshot.cameraName = c.at("name").get<std::string>();
    std::string zones;
    for (int zone : snapshot.view.zones) zones += (zones.empty() ? "" : ",") + std::to_string(zone);
    Logger::log("RenderBudget: from " + snapshot.cameraName + " camera zone " + std::to_string(cameraZone) + ", "
                + std::to_string(portals.size()) + " portal polygons, zones through portals " + std::to_string(visibility.zones.size())
                + ", engine drew " + std::to_string(drawn.size()) + " actors" + (json.at("repainted").get<bool>() ? "" : " (repaint faulted)")
                + "; view zones " + zones + "; " + std::to_string(snapshot.view.totals.Triangles()) + " triangles");
    if (IsWindow(budget.window))
    {
        Fill();
        Budget::SelectRowByData(GetDlgItem(budget.window, kZoneList), kViewRow);
        budget.lastList = kZoneList;
        budget.lastRow = 1;
    }
}

std::vector<std::string> RowPaths(int list, LPARAM data, std::string& what)
{
    const auto& snapshot = budget.snapshot;
    const auto& report = snapshot.report;
    std::vector<int> items;
    auto drawnOnly = [&](const std::vector<int>& from)
    {
        for (int i : from)
        {
            const auto& item = snapshot.scene.items.at(static_cast<size_t>(i));
            if (item.drawn || item.kind == Kind::Emitter) items.push_back(i);
        }
    };
    if (list == kZoneList)
    {
        if (data == kViewRow) { drawnOnly(snapshot.view.drawnItems); what = "the camera's view"; }
        else
        {
            const auto& zone = data == kMapRow ? report.map : report.zones.at(static_cast<size_t>(data));
            drawnOnly(zone.items);
            what = zone.name;
        }
    }
    else
    {
        const auto& hotspot = report.hotspots.at(static_cast<size_t>(data));
        items = hotspot.items;
        what = HotspotLabel(hotspot) + " hotspot";
    }
    std::vector<std::string> paths;
    for (int i : items) paths.push_back(snapshot.paths.at(static_cast<size_t>(i)));
    return paths;
}

void Choose(int list, int row, bool frame)
{
    if (row < 0 || !budget.measured) return;
    if (budget.snapshot.generation != Workflow::Editor::MapGeneration())
    {
        Refresh();
        SetStatus("Another map was opened, so the budget was measured again. Click the row again.");
        return;
    }
    LPARAM data = 0;
    if (!Budget::RowData(GetDlgItem(budget.window, list), row, data)) return;
    budget.lastList = list;
    budget.lastRow = row;
    std::string what;
    const auto paths = RowPaths(list, data, what);
    const auto found = Workflow::Editor::SelectActorPaths(paths, frame);
    std::string text = "Selected " + std::to_string(found) + " actor(s) in " + what + (frame && found ? ", framed in the viewports." : ".");
    if (found < paths.size()) text += " " + std::to_string(paths.size() - found) + " no longer exist; Refresh.";
    SetStatus(text);
}

void Layout(HWND window)
{
    RECT r{};
    GetClientRect(window, &r);
    const int w = r.right - 24, h = r.bottom;
    const int top = 96, listsHeight = h - top - 48 - 34;
    const int zonesHeight = listsHeight * 3 / 5, hotspotTop = top + zonesHeight + 22;
    MoveWindow(GetDlgItem(window, kIntro), 12, 8, w, top - 12, TRUE);
    MoveWindow(GetDlgItem(window, kZoneList), 12, top, w, zonesHeight, TRUE);
    MoveWindow(GetDlgItem(window, kHotspotLabel), 12, top + zonesHeight + 4, w, 16, TRUE);
    MoveWindow(GetDlgItem(window, kHotspotList), 12, hotspotTop, w, h - 82 - hotspotTop, TRUE);
    MoveWindow(GetDlgItem(window, kStatus), 12, h - 76, w, 34, TRUE);
    MoveWindow(GetDlgItem(window, kRefresh), 12, h - 36, 90, 26, TRUE);
    MoveWindow(GetDlgItem(window, kView), 110, h - 36, 170, 26, TRUE);
    MoveWindow(GetDlgItem(window, kSelect), 288, h - 36, 110, 26, TRUE);
    MoveWindow(GetDlgItem(window, kFrame), 406, h - 36, 130, 26, TRUE);
    MoveWindow(GetDlgItem(window, IDCANCEL), r.right - 102, h - 36, 90, 26, TRUE);
}

LRESULT CALLBACK BudgetProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    try
    {
        switch (message)
        {
        case WM_CREATE:
        {
            Budget::Control(window, "STATIC", "", 0, kIntro, 0, 0, 0, 0);
            Budget::Columns(Budget::ReportList(window, kZoneList),
                {{"Zone", 170}, {"Actors", 52}, {"Drawn", 50}, {"Meshes", 54}, {"Mesh tris", 70}, {"BSP nodes", 66}, {"Surfaces", 58},
                 {"BSP tris", 62}, {"Triangles", 70}, {"Materials", 62}, {"Textures", 58}, {"Emitters", 56}, {"Status", 190}, {"View", 56}},
                {"Status", "View"});
            Budget::Control(window, "STATIC", "Hotspots over the guidelines (worst first): zones and single heavy meshes", 0, kHotspotLabel, 0, 0, 0, 0);
            Budget::Columns(Budget::ReportList(window, kHotspotList), {{"Hotspot", 110}, {"Zone", 170}, {"Measure", 300}, {"Actors", 260}},
                {"Zone", "Measure", "Actors"});
            Budget::Control(window, "STATIC", "", 0, kStatus, 0, 0, 0, 0);
            Budget::Control(window, "BUTTON", "&Refresh", WS_TABSTOP, kRefresh, 0, 0, 0, 0);
            Budget::Control(window, "BUTTON", "What Renders From &Camera", WS_TABSTOP, kView, 0, 0, 0, 0);
            Budget::Control(window, "BUTTON", "&Select Actors", WS_TABSTOP, kSelect, 0, 0, 0, 0);
            Budget::Control(window, "BUTTON", "Select and &Frame", WS_TABSTOP, kFrame, 0, 0, 0, 0);
            Budget::Control(window, "BUTTON", "Close", WS_TABSTOP, IDCANCEL, 0, 0, 0, 0);
            Layout(window);
            return 0;
        }
        case WM_SIZE:
            Layout(window);
            return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize = {700, 440};
            return 0;
        case WM_NOTIFY:
        {
            auto header = reinterpret_cast<NMHDR*>(l);
            if ((header->idFrom == kZoneList || header->idFrom == kHotspotList) && (header->code == NM_CLICK || header->code == NM_DBLCLK))
            {
                auto activate = reinterpret_cast<NMITEMACTIVATE*>(l);
                Choose(static_cast<int>(header->idFrom), activate->iItem, header->code == NM_DBLCLK);
            }
            else if ((header->idFrom == kZoneList || header->idFrom == kHotspotList) && header->code == LVN_ITEMCHANGED)
            {
                // Keyboard moves pick the row for the buttons without selecting actors.
                auto change = reinterpret_cast<NMLISTVIEW*>(l);
                if ((change->uNewState & LVIS_SELECTED) && !(change->uOldState & LVIS_SELECTED))
                {
                    budget.lastList = static_cast<int>(header->idFrom);
                    budget.lastRow = change->iItem;
                }
            }
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(w))
            {
            case kRefresh: Refresh(); return 0;
            case kView: MeasureView(); return 0;
            case kSelect: case kFrame:
                if (budget.lastRow < 0) SetStatus("Click a zone or a hotspot first.");
                else Choose(budget.lastList, budget.lastRow, LOWORD(w) == kFrame);
                return 0;
            case IDCANCEL: DestroyWindow(window); return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            budget.window = nullptr;
            return 0;
        }
    }
    catch (const std::exception& e)
    {
        // A modal box here would stall the in-editor tests; report in the status line.
        SetStatus(e.what());
        if (message == WM_CREATE) return -1;
        return 0;
    }
    return DefWindowProcA(window, message, w, l);
}

void Show(HWND owner)
{
    if (IsWindow(budget.window))
    {
        ShowWindow(budget.window, SW_RESTORE);
        SetForegroundWindow(budget.window);
        Fill();
        return;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    Budget::RegisterClassOnce("ReloadedRenderBudget", BudgetProc);
    budget.window = CreateWindowExA(WS_EX_TOOLWINDOW, "ReloadedRenderBudget", kTitle, WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX,
                                    CW_USEDEFAULT, CW_USEDEFAULT, 1080, 680, owner, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!budget.window) throw std::runtime_error("Could not open the Render Budget window.");
    Fill();
    ShowWindow(budget.window, SW_SHOW);
}
} // namespace

void Open(HWND owner)
{
    Refresh();
    Show(owner);
}

bool HandleCommand(UINT command)
{
    if (command != kOpenCommand && command != kViewCommand) return false;
    try
    {
        if (command == kOpenCommand || !IsWindow(budget.window)) Open(GetActiveWindow());
        if (command == kViewCommand) MeasureView();
    }
    catch (const std::exception& e)
    {
        // Through the window's status line when it is open: the view command
        // is also driven by tests, which a modal box would stall.
        if (IsWindow(budget.window)) SetStatus(e.what());
        else MessageBoxA(GetActiveWindow(), e.what(), kTitle, MB_OK | MB_ICONINFORMATION);
    }
    return true;
}
}
