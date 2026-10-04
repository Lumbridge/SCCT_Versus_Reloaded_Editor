#include "pch.h"
#undef min
#undef max
#include "LightingBudget.h"
#include "LightingBudgetModel.h"
#include "RebuildAllMaps.h"
#include "WorkflowEditor.h"
#include "logger.h"
#include <commctrl.h>
#include <filesystem>
#include <set>
#include <stdexcept>
#pragma comment(lib, "comctl32.lib")

namespace LightingBudget
{
namespace
{
const char kTitle[] = "Lighting Budget";
const char kSection[] = "LightingBudget";

enum : int
{
    kZoneList = 100, kHotspotList, kStatus, kRefresh, kSelect, kFrame, kIntro,
    kBuildAnyway = 201, kOpenBudget, kQuiet, kPromptText
};

std::string IniPath()
{
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    return (std::filesystem::path(exe).parent_path() / "Reloaded_Editor.ini").string();
}

// [LightingBudget] in Reloaded_Editor.ini; the defaults are the stock checks'
// own limits and are written once so the section can be found and edited.
Thresholds LoadThresholds()
{
    const auto ini = IniPath();
    const Thresholds defaults;
    const struct { const char* key; int value; } keys[] = {
        {"MaxLightsPerLeaf", defaults.leafLights},
        {"MaxOverlappingLights", defaults.overlap},
        {"WarnBeforeBuild", defaults.warnBeforeBuild ? 1 : 0},
        {"WarnLightsPerLeaf", defaults.warnLeafLights},
        {"WarnOverlappingLights", defaults.warnOverlap}};
    for (const auto& key : keys)
    {
        char value[32] = {};
        GetPrivateProfileStringA(kSection, key.key, "", value, sizeof(value), ini.c_str());
        if (!value[0])
            WritePrivateProfileStringA(kSection, key.key, std::to_string(key.value).c_str(), ini.c_str());
    }
    Thresholds t;
    t.leafLights = GetPrivateProfileIntA(kSection, "MaxLightsPerLeaf", defaults.leafLights, ini.c_str());
    t.overlap = GetPrivateProfileIntA(kSection, "MaxOverlappingLights", defaults.overlap, ini.c_str());
    t.warnBeforeBuild = GetPrivateProfileIntA(kSection, "WarnBeforeBuild", 1, ini.c_str()) != 0;
    t.warnLeafLights = GetPrivateProfileIntA(kSection, "WarnLightsPerLeaf", defaults.warnLeafLights, ini.c_str());
    t.warnOverlap = GetPrivateProfileIntA(kSection, "WarnOverlappingLights", defaults.warnOverlap, ini.c_str());
    return ClampThresholds(t);
}

struct Snapshot
{
    Report report;
    std::vector<std::string> paths, names;
    std::string map;
    unsigned generation = 0;
};

Snapshot Measure(const Thresholds& thresholds)
{
    const auto scene = Workflow::Editor::LightingBudgetScene();
    Snapshot snapshot;
    std::vector<Light> lights;
    for (const auto& item : scene.at("lights"))
    {
        Light light;
        light.name = item.at("name").get<std::string>();
        light.zone = item.at("zone").get<int>();
        light.flags.type = item.at("type").get<uint8_t>();
        light.flags.effect = item.at("effect").get<uint8_t>();
        light.flags.staticFlag = item.at("static").get<bool>();
        light.flags.inGameFlag = item.at("inGame").get<bool>();
        light.flags.dynamicFlag = item.at("dynamic").get<bool>();
        light.flags.zoneLimited = item.at("zoneLimited").get<bool>();
        light.flags.animA = item.at("animA").get<float>();
        light.flags.animB = item.at("animB").get<float>();
        if (item.contains("position"))
        {
            const auto& p = item.at("position");
            light.x = p.at(0).get<double>(); light.y = p.at(1).get<double>(); light.z = p.at(2).get<double>();
            light.radius = item.at("radius").get<double>();
        }
        snapshot.paths.push_back(item.at("path").get<std::string>());
        snapshot.names.push_back(light.name);
        lights.push_back(light);
    }
    std::vector<Leaf> leaves;
    for (const auto& item : scene.at("leaves"))
    {
        Leaf leaf;
        leaf.zone = item.at("zone").get<int>();
        leaf.index = item.at("index").get<int>();
        leaf.lights = item.at("lights").get<std::vector<int>>();
        leaves.push_back(std::move(leaf));
    }
    std::map<int, std::string> zones;
    for (const auto& [number, name] : scene.at("zones").items())
        zones[std::stoi(number)] = name.get<std::string>();
    snapshot.report = Analyse(lights, leaves, zones, thresholds, scene.at("leavesKnown").get<bool>());
    snapshot.map = scene.at("map").get<std::string>();
    snapshot.generation = scene.at("generation").get<unsigned>();
    const auto& map = snapshot.report.map;
    size_t over = 0;
    for (const auto& zone : snapshot.report.zones) over += zone.Over();
    Logger::log("LightingBudget: " + std::to_string(map.total) + " lights (" + std::to_string(map.inGame) + " in game) in "
                + std::to_string(snapshot.report.zones.size()) + " zones, " + std::to_string(leaves.size())
                + " lit leaves; worst leaf " + std::to_string(map.worstLeaf) + ", largest overlap " + std::to_string(map.overlap)
                + "; " + std::to_string(over) + " zone(s) over");
    return snapshot;
}

HWND Control(HWND window, const char* type, const char* text, DWORD style, int id, int x, int y, int width, int height)
{
    auto control = CreateWindowExA(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, window,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
    SendMessage(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return control;
}

void Columns(HWND list, const std::vector<std::pair<const char*, int>>& columns)
{
    for (int i = 0; i < static_cast<int>(columns.size()); ++i)
    {
        LVCOLUMNA column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        // Counts read better right-aligned; names and status stay left.
        const std::string title = columns[i].first;
        const bool text = !i || title == "Zone" || title == "Status" || title == "Light names";
        column.fmt = text ? LVCFMT_LEFT : LVCFMT_RIGHT;
        column.cx = columns[i].second;
        column.pszText = const_cast<char*>(columns[i].first);
        SendMessageA(list, LVM_INSERTCOLUMNA, i, reinterpret_cast<LPARAM>(&column));
    }
}

void Row(HWND list, int row, LPARAM data, const std::vector<std::string>& cells)
{
    LVITEMA item{};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = row;
    item.lParam = data;
    item.pszText = const_cast<char*>(cells[0].c_str());
    SendMessageA(list, LVM_INSERTITEMA, 0, reinterpret_cast<LPARAM>(&item));
    for (int i = 1; i < static_cast<int>(cells.size()); ++i)
    {
        LVITEMA cell{};
        cell.iSubItem = i;
        cell.pszText = const_cast<char*>(cells[i].c_str());
        SendMessageA(list, LVM_SETITEMTEXTA, row, reinterpret_cast<LPARAM>(&cell));
    }
}

std::string Intro(const Thresholds& t)
{
    return "Only in-game lights cost frame time: InGame and Dynamic lights are drawn every frame, Static ones are baked by "
           "Build Lighting. Worst leaf: the most in-game lights reaching one BSP leaf of the zone. Overlap: the largest "
           "group of the zone's own in-game lights that all overlap one another. The editor's Tools > Check InGame / "
           "Dynamic Lights checks flag more than 4 per leaf and groups of 3; the renderer only stops at 256, so these "
           "are guidelines, which every shipped Versus map exceeds (they reach 44 and 33). Zones are flagged above " + std::to_string(t.leafLights) + " per leaf or "
           + std::to_string(t.overlap) + " overlapping; builds warn above " + std::to_string(t.warnLeafLights) + " or "
           + std::to_string(t.warnOverlap) + ". Change them in Reloaded_Editor.ini [LightingBudget]. Click a row to "
           "select its lights; double-click to frame them too.";
}

std::string Status(const ZoneReport& zone)
{
    if (!zone.Over()) return "OK";
    std::string text = "Over:";
    if (zone.overLeaf) text += " leaf";
    if (zone.overLeaf && zone.overOverlap) text += ",";
    if (zone.overOverlap) text += " overlap";
    return text;
}

// The budget window. One at a time; it keeps the last measurement.
struct Window
{
    HWND window = nullptr;
    Snapshot snapshot;
    bool measured = false;
    int lastList = 0, lastRow = -1;
} budget;

std::vector<std::string> RowPaths(int list, LPARAM data, std::string& what)
{
    const auto& report = budget.snapshot.report;
    std::vector<int> lights;
    if (list == kZoneList)
    {
        const auto& zone = data < 0 ? report.map : report.zones.at(static_cast<size_t>(data));
        lights = zone.lights;
        what = zone.name;
    }
    else
    {
        const auto& hotspot = report.hotspots.at(static_cast<size_t>(data));
        lights = hotspot.lights;
        what = HotspotLabel(hotspot);
    }
    std::vector<std::string> paths;
    for (int light : lights) paths.push_back(budget.snapshot.paths.at(static_cast<size_t>(light)));
    return paths;
}

void SetStatus(const std::string& text)
{
    if (IsWindow(budget.window)) SetDlgItemTextA(budget.window, kStatus, text.c_str());
}

std::string ReportStatus()
{
    const auto& report = budget.snapshot.report;
    size_t over = 0;
    for (const auto& zone : report.zones) over += zone.Over();
    std::string text = std::to_string(report.map.total) + " lights, " + std::to_string(report.map.inGame)
        + " drawn in game, in " + std::to_string(report.zones.size()) + " zone(s); " + std::to_string(over)
        + " zone(s) over budget.";
    text += report.leavesKnown ? " Leaf counts use the BSP as last built." : " The BSP has not been built, so leaf counts are unavailable.";
    if (!report.overlapExact) text += " The overlap search stopped early on this dense map; overlap figures are lower bounds.";
    return text;
}

void Fill()
{
    auto zones = GetDlgItem(budget.window, kZoneList), hotspots = GetDlgItem(budget.window, kHotspotList);
    ListView_DeleteAllItems(zones);
    ListView_DeleteAllItems(hotspots);
    const auto& report = budget.snapshot.report;
    auto count = [](const ZoneReport& zone, Usage usage)
    {
        auto found = zone.usage.find(usage);
        return std::to_string(found == zone.usage.end() ? 0 : found->second);
    };
    auto zoneRow = [&](int row, LPARAM data, const ZoneReport& zone)
    {
        Row(zones, row, data, {zone.name, std::to_string(zone.total), std::to_string(zone.inGame),
            count(zone, Usage::Static), count(zone, Usage::InGame), count(zone, Usage::StaticInGame),
            count(zone, Usage::Dynamic), count(zone, Usage::Unflagged), count(zone, Usage::Off),
            report.leavesKnown ? std::to_string(zone.worstLeaf) : "-", std::to_string(zone.overlap), Status(zone)});
    };
    zoneRow(0, -1, report.map);
    for (size_t i = 0; i < report.zones.size(); ++i) zoneRow(static_cast<int>(i) + 1, static_cast<LPARAM>(i), report.zones[i]);
    for (size_t i = 0; i < report.hotspots.size(); ++i)
    {
        const auto& hotspot = report.hotspots[i];
        std::string zone = "Zone " + std::to_string(hotspot.zone), names;
        for (const auto& z : report.zones) if (z.zone == hotspot.zone) zone = z.name;
        for (int light : hotspot.lights) names += (names.empty() ? "" : ", ") + budget.snapshot.names.at(static_cast<size_t>(light));
        Row(hotspots, static_cast<int>(i), static_cast<LPARAM>(i), {HotspotLabel(hotspot), zone, std::to_string(hotspot.lights.size()), names});
    }
    SetDlgItemTextA(budget.window, kIntro, Intro(report.thresholds).c_str());
    SetStatus(ReportStatus());
}

void Refresh()
{
    budget.snapshot = Measure(LoadThresholds());
    budget.measured = true;
    budget.lastRow = -1;
    if (IsWindow(budget.window)) Fill();
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
    LVITEMA item{};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if (!SendMessageA(GetDlgItem(budget.window, list), LVM_GETITEMA, 0, reinterpret_cast<LPARAM>(&item))) return;
    budget.lastList = list;
    budget.lastRow = row;
    std::string what;
    const auto paths = RowPaths(list, item.lParam, what);
    const auto found = Workflow::Editor::SelectActorPaths(paths, frame);
    std::string text = "Selected " + std::to_string(found) + " light(s) in " + what + (frame && found ? ", framed in the viewports." : ".");
    if (found < paths.size()) text += " " + std::to_string(paths.size() - found) + " no longer exist; Refresh.";
    SetStatus(text);
}

void Layout(HWND window)
{
    RECT r{};
    GetClientRect(window, &r);
    const int w = r.right - 24, h = r.bottom;
    const int top = 82, listsHeight = h - top - 48 - 34;
    const int zonesHeight = listsHeight * 3 / 5, hotspotTop = top + zonesHeight + 22;
    MoveWindow(GetDlgItem(window, kIntro), 12, 8, w, top - 12, TRUE);
    MoveWindow(GetDlgItem(window, kZoneList), 12, top, w, zonesHeight, TRUE);
    MoveWindow(GetDlgItem(window, 110), 12, top + zonesHeight + 4, w, 16, TRUE);
    MoveWindow(GetDlgItem(window, kHotspotList), 12, hotspotTop, w, h - 82 - hotspotTop, TRUE);
    MoveWindow(GetDlgItem(window, kStatus), 12, h - 76, w, 34, TRUE);
    MoveWindow(GetDlgItem(window, kRefresh), 12, h - 36, 90, 26, TRUE);
    MoveWindow(GetDlgItem(window, kSelect), 110, h - 36, 110, 26, TRUE);
    MoveWindow(GetDlgItem(window, kFrame), 228, h - 36, 130, 26, TRUE);
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
            Control(window, "STATIC", "", 0, kIntro, 0, 0, 0, 0);
            auto zones = Control(window, WC_LISTVIEWA, "", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, kZoneList, 0, 0, 0, 0);
            ListView_SetExtendedListViewStyle(zones, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            Columns(zones, {{"Zone", 170}, {"Lights", 50}, {"In game", 58}, {"Static", 50}, {"InGame", 55}, {"Static/InGame", 84},
                            {"Dynamic", 58}, {"Unflagged", 64}, {"Off", 36}, {"Worst leaf", 66}, {"Overlap", 56}, {"Status", 100}});
            Control(window, "STATIC", "Hotspots over budget (worst first): BSP leaves and groups of overlapping in-game lights", 0, 110, 0, 0, 0, 0);
            auto hotspots = Control(window, WC_LISTVIEWA, "", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, kHotspotList, 0, 0, 0, 0);
            ListView_SetExtendedListViewStyle(hotspots, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            Columns(hotspots, {{"Hotspot", 110}, {"Zone", 160}, {"Lights", 50}, {"Light names", 520}});
            Control(window, "STATIC", "", 0, kStatus, 0, 0, 0, 0);
            Control(window, "BUTTON", "&Refresh", WS_TABSTOP, kRefresh, 0, 0, 0, 0);
            Control(window, "BUTTON", "&Select Lights", WS_TABSTOP, kSelect, 0, 0, 0, 0);
            Control(window, "BUTTON", "Select and &Frame", WS_TABSTOP, kFrame, 0, 0, 0, 0);
            Control(window, "BUTTON", "Close", WS_TABSTOP, IDCANCEL, 0, 0, 0, 0);
            Layout(window);
            return 0;
        }
        case WM_SIZE:
            Layout(window);
            return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize = {640, 420};
            return 0;
        case WM_NOTIFY:
        {
            auto header = reinterpret_cast<NMHDR*>(l);
            if ((header->idFrom == kZoneList || header->idFrom == kHotspotList) && (header->code == NM_CLICK || header->code == NM_DBLCLK))
            {
                auto activate = reinterpret_cast<NMITEMACTIVATE*>(l);
                Choose(static_cast<int>(header->idFrom), activate->iItem, header->code == NM_DBLCLK);
            }
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(w))
            {
            case kRefresh: Refresh(); return 0;
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
    WNDCLASSA wc{};
    wc.lpfnWndProc = BudgetProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "ReloadedLightingBudget";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);
    budget.window = CreateWindowExA(WS_EX_TOOLWINDOW, wc.lpszClassName, kTitle, WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX,
                                    CW_USEDEFAULT, CW_USEDEFAULT, 900, 640, owner, nullptr, wc.hInstance, nullptr);
    if (!budget.window) throw std::runtime_error("Could not open the Lighting Budget window.");
    Fill();
    ShowWindow(budget.window, SW_SHOW);
}

// The pre-build warning: Build anyway / Open Lighting Budget / Cancel, and a
// "don't warn again for this map this session" box.
struct Prompt
{
    std::string text;
    int choice = IDCANCEL;
    bool quiet = false, done = false;
};

LRESULT CALLBACK PromptProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto prompt = reinterpret_cast<Prompt*>(GetWindowLongPtr(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        prompt = static_cast<Prompt*>(reinterpret_cast<CREATESTRUCT*>(l)->lpCreateParams);
        SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(prompt));
    }
    if (!prompt) return DefWindowProcA(window, message, w, l);
    switch (message)
    {
    case WM_CREATE:
    {
        RECT r{};
        GetClientRect(window, &r);
        Control(window, "STATIC", prompt->text.c_str(), 0, kPromptText, 14, 12, r.right - 28, r.bottom - 92);
        Control(window, "BUTTON", "Don't warn again for this map this session", WS_TABSTOP | BS_AUTOCHECKBOX, kQuiet, 14, r.bottom - 74, r.right - 28, 20);
        Control(window, "BUTTON", "&Build anyway", WS_TABSTOP | BS_DEFPUSHBUTTON, kBuildAnyway, r.right - 384, r.bottom - 40, 110, 28);
        Control(window, "BUTTON", "&Open Lighting Budget", WS_TABSTOP, kOpenBudget, r.right - 266, r.bottom - 40, 150, 28);
        Control(window, "BUTTON", "Cancel", WS_TABSTOP, IDCANCEL, r.right - 108, r.bottom - 40, 94, 28);
        SetFocus(GetDlgItem(window, kBuildAnyway));
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == kBuildAnyway || LOWORD(w) == kOpenBudget || LOWORD(w) == IDCANCEL)
        {
            prompt->choice = LOWORD(w);
            prompt->quiet = IsDlgButtonChecked(window, kQuiet) == BST_CHECKED;
            prompt->done = true;
            return 0;
        }
        break;
    case WM_CLOSE:
        prompt->choice = IDCANCEL;
        prompt->done = true;
        return 0;
    }
    return DefWindowProcA(window, message, w, l);
}

int Ask(HWND owner, Prompt& prompt)
{
    WNDCLASSA wc{};
    wc.lpfnWndProc = PromptProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "ReloadedLightingBudgetPrompt";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);
    // Size the text area for the summary.
    RECT text{0, 0, 520, 0};
    if (auto dc = GetDC(nullptr))
    {
        auto old = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        DrawTextA(dc, prompt.text.c_str(), -1, &text, DT_CALCRECT | DT_WORDBREAK);
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
    }
    RECT rect{0, 0, 548, std::min(560L, text.bottom + 112)};
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&rect, style, FALSE, WS_EX_DLGMODALFRAME);
    RECT ownerRect{};
    GetWindowRect(owner ? owner : GetDesktopWindow(), &ownerRect);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    auto window = CreateWindowExA(WS_EX_DLGMODALFRAME, wc.lpszClassName, "Lighting Budget - Build", style,
                                  (ownerRect.left + ownerRect.right - width) / 2, (ownerRect.top + ownerRect.bottom - height) / 2,
                                  width, height, owner, nullptr, wc.hInstance, &prompt);
    if (!window) return kBuildAnyway; // never block a build on a UI failure
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    MSG message{};
    bool quit = false;
    while (!prompt.done)
    {
        const auto got = GetMessageA(&message, nullptr, 0, 0);
        if (got <= 0) { quit = got == 0; break; }
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE && (message.hwnd == window || IsChild(window, message.hwnd)))
        {
            prompt.done = true;
            prompt.choice = IDCANCEL;
            break;
        }
        if (!IsDialogMessageA(window, &message))
        {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    }
    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(window);
    if (owner) SetActiveWindow(owner);
    if (quit) PostQuitMessage(static_cast<int>(message.wParam));
    return prompt.choice;
}

std::set<std::string> quietMaps;
} // namespace

void Open(HWND owner)
{
    Refresh();
    Show(owner);
}

bool HandleCommand(UINT command)
{
    if (command != kOpenCommand) return false;
    try
    {
        Open(GetActiveWindow());
    }
    catch (const std::exception& e)
    {
        MessageBoxA(GetActiveWindow(), e.what(), kTitle, MB_OK | MB_ICONINFORMATION);
    }
    return true;
}

bool AllowBuild(UINT command)
{
    // Rebuild All Maps calls the build directly, but never prompt during it.
    if (RebuildAllMaps::Running()) return true;
    try
    {
        const auto thresholds = LoadThresholds();
        if (!thresholds.warnBeforeBuild) return true;
        auto snapshot = Measure(ForBuildWarning(thresholds));
        if (!snapshot.report.Over()) return true;
        const auto key = snapshot.map.empty() ? "untitled#" + std::to_string(snapshot.generation) : FoldKey(snapshot.map);
        if (quietMaps.count(key)) return true;
        Prompt prompt;
        const char* build = command == kBuildAll ? "Build All" : command == kRebuildLighting ? "Rebuild Lighting Only" : "Rebuild Changed Lighting Only";
        prompt.text = Summary(snapshot.report) + "\r\n\r\nRun " + build + " anyway?";
        auto owner = GetActiveWindow();
        const int choice = Ask(owner, prompt);
        if (prompt.quiet) quietMaps.insert(key);
        Logger::log("LightingBudget: over budget before " + std::string(build) + "; "
                    + (choice == kBuildAnyway ? "built anyway" : choice == kOpenBudget ? "opened the budget" : "cancelled"));
        if (choice == kOpenBudget)
        {
            // The window flags zones against the guideline, not the warning's limits.
            Refresh();
            Show(owner);
            return false;
        }
        return choice == kBuildAnyway;
    }
    catch (const std::exception& e)
    {
        // The check must never stand in the way of a build.
        Logger::log(std::string("LightingBudget: build check skipped: ") + e.what());
        return true;
    }
}
}
