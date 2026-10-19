// Smaller suites, chosen with suite=<name>[,<name>...] in -Extra (run_smoke.ps1 -Extra @{ suite = 'storeys' }).
// Each one expects the map from smoke.ini to be open, from Open Recent.

HWND TopOfClass(const char* cls)
{
    struct S { const char* c; HWND r; } s{cls, nullptr};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND w, LPARAM p) -> BOOL {
        auto s = reinterpret_cast<S*>(p);
        char c[128] = {};
        GetClassNameA(w, c, sizeof(c));
        if (!_stricmp(c, s->c)) { s->r = w; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&s));
    return s.r;
}
// File > Open Recent's first entry again, keeping (Yes) or not the open map's changes when asked.
void ReopenRecent(int answer = IDYES)
{
    boxAnswer = answer;
    Command(40970);
    boxAnswer = IDOK;
    Pump(2500);
}

// ---- Storeys: stepping to a floor hides the others in the viewports only. A save must not write
// that view into the map: reopened, nothing is hidden that was not hidden before.
void StoreysSuite()
{
    const auto map = Ini("map");
    const auto folder = std::filesystem::path(map).parent_path();
    const int hiddenAtStart = Hidden();
    Command(40966);
    Pump(2000);
    HWND palette = TopOfClass("ReloadedStoreyFilter");
    Check(palette != nullptr, "storeys-open", "hidden=" + std::to_string(hiddenAtStart));
    if (!palette) return;
    SendMessageA(palette, WM_KEYDOWN, VK_NEXT, 0);
    Pump(500);
    const int onFloor = Hidden();
    Check(onFloor > hiddenAtStart, "storeys-floor-hides", "hidden " + std::to_string(hiddenAtStart) + "->" + std::to_string(onFloor));

    // File > Save, in place (the map came from Open Recent), then the map opened again from Open Recent.
    Command(40007);
    Pump(500);
    Check(Hidden() == onFloor, "storeys-save-keeps-floor", "hidden after save=" + std::to_string(Hidden()) + " boxes=" + std::to_string(boxes.size()));
    ReopenRecent(IDYES);
    const int reopened = Hidden();
    Check(reopened == hiddenAtStart, "storeys-saved-map-not-hidden", "hidden=" + std::to_string(reopened) + " (at start " + std::to_string(hiddenAtStart) + ")");

    // The palette follows the reopened map; step down a floor again.
    Command(40966);
    Pump(1500);
    SendMessageA(palette, WM_KEYDOWN, VK_NEXT, 0);
    Pump(500);
    const int again = Hidden();
    Info("storeys-floor-again", "hidden=" + std::to_string(again));

    // The stock autosave's own command, into a spare slot.
    const auto autosave = folder / "Auto7.sdc";
    std::error_code ignored;
    std::filesystem::remove(autosave, ignored);
    Exec("MAP SAVE AUTOSAVE=1 FILE=\"" + autosave.string() + "\"");
    Pump(500);
    Check(Hidden() == again && std::filesystem::exists(autosave), "storeys-autosave-keeps-floor", "hidden after autosave=" + std::to_string(Hidden()));

    // Save As to a copy.
    const auto copy = folder / "StoreysCopy.sdc";
    std::filesystem::remove(copy, ignored);
    int ticks = 0;
    dialogHandler = [&](HWND w, const std::string& title) {
        char cls[64] = {};
        GetClassNameA(w, cls, sizeof(cls));
        if (strcmp(cls, "#32770") || IsMessageBox(w)) return false;
        Info("save-as-dialog", title);
        return AnswerFile(w, copy.wstring(), ticks);
    };
    Command(40008);
    dialogHandler = nullptr;
    Pump(500);
    Check(std::filesystem::exists(copy) && Hidden() == again, "storeys-save-as-keeps-floor", "hidden after save as=" + std::to_string(Hidden()) + " boxes=" + std::to_string(boxes.size()));

    // Reopened, each file shows every storey.
    auto hiddenIn = [&](const std::filesystem::path& file) {
        Exec("MAP LOAD FILE=\"" + file.string() + "\"");
        Pump(2500);
        return Hidden();
    };
    if (std::filesystem::exists(autosave))
    {
        const int n = hiddenIn(autosave);
        Check(n == hiddenAtStart, "storeys-autosave-file-not-hidden", "hidden=" + std::to_string(n) + " (at start " + std::to_string(hiddenAtStart) + ")");
    }
    if (std::filesystem::exists(copy))
    {
        const int n = hiddenIn(copy);
        Check(n == hiddenAtStart, "storeys-save-as-file-not-hidden", "hidden=" + std::to_string(n) + " (at start " + std::to_string(hiddenAtStart) + ")");
    }
    if ((palette = TopOfClass("ReloadedStoreyFilter")) != nullptr) { PostMessage(palette, WM_CLOSE, 0, 0); Pump(300); }
}

// ---- Unsaved changes: Open Recent warns exactly when the open map has edits its file does not.
// Every probe of the warning answers No, so a warning keeps the map open and no warning opens it.
bool WarnsBeforeOpenRecent()
{
    ReopenRecent(IDNO);
    return BoxContains("Unsaved changes");
}
void ExpectWarning(bool expected, const std::string& name, bool also = true, const std::string& detail = "")
{
    const bool warned = WarnsBeforeOpenRecent();
    Check(also && warned == expected, name, std::string(warned ? "warned" : "no warning") + " boxes=" + std::to_string(boxes.size()) + (detail.empty() ? "" : " " + detail));
}
// One edit: Hide selected on the first visible free mesh, an Undo step of its own.
void EditOnce()
{
    SelectOnly(FreeMeshes(1));
    Command(40957);
    Exec("SELECT NONE");
    Pump(200);
}
// The frame's File > Open, found by its text.
UINT FileOpenCommand()
{
    HMENU bar = GetMenu(frameWindow);
    HMENU file = bar ? GetSubMenu(bar, 0) : nullptr;
    for (int i = 0; file && i < GetMenuItemCount(file); ++i)
    {
        char text[256] = {};
        GetMenuStringA(file, i, text, sizeof(text), MF_BYPOSITION);
        std::string plain;
        for (char* c = text; *c && *c != '\t'; ++c) if (*c != '&') plain += *c;
        const UINT id = GetMenuItemID(file, i);
        if (plain.rfind("Open", 0) == 0 && plain.find("Recent") == std::string::npos && plain.find("Backup") == std::string::npos
            && plain.find("Autosave") == std::string::npos && id != UINT(-1))
        {
            Info("file-open", plain + " id=" + std::to_string(id));
            return id;
        }
    }
    return 0;
}
// Answers the next file dialog with this path (Save As, File > Open), or cancels it.
void AnswerFileDialogs(const std::filesystem::path& path, bool cancel)
{
    static int ticks;
    ticks = 0;
    dialogHandler = [path, cancel](HWND w, const std::string& title) {
        char cls[64] = {};
        GetClassNameA(w, cls, sizeof(cls));
        if (strcmp(cls, "#32770") || IsMessageBox(w)) return false;
        if (cancel) { if (++ticks == 2) { Info("cancel-file-dialog", title); PostMessage(w, WM_COMMAND, IDCANCEL, 0); } return true; }
        return AnswerFile(w, path.wstring(), ticks);
    };
}

void UnsavedSuite()
{
    const auto map = std::filesystem::path(Ini("map"));
    const auto other = std::filesystem::path(Ini("map2"));
    ExpectWarning(false, "unsaved-open-recent-clean");

    EditOnce();
    ExpectWarning(true, "unsaved-edit-warns");
    ExpectWarning(true, "unsaved-still-warns-after-no");

    // The stock autosave writes a copy: still unsaved.
    Exec("MAP SAVE AUTOSAVE=1 FILE=\"" + (map.parent_path() / "Auto6.sdc").string() + "\"");
    Pump(300);
    ExpectWarning(true, "unsaved-autosave-is-not-a-save");

    // File > Save As, cancelled.
    AnswerFileDialogs({}, true);
    Command(40008);
    dialogHandler = nullptr;
    ExpectWarning(true, "unsaved-save-as-cancelled");

    // File > Save refused: the map file is read-only.
    SetFileAttributesW(map.c_str(), FILE_ATTRIBUTE_READONLY);
    boxAnswer = IDNO;
    Command(40007);
    boxAnswer = IDOK;
    std::string refusal;
    for (auto& b : boxes) refusal += b + " ";
    SetFileAttributesW(map.c_str(), FILE_ATTRIBUTE_NORMAL);
    Info("unsaved-refused-save", refusal);
    ExpectWarning(true, "unsaved-save-refused");

    // Ctrl+L's route: the stock FileSave, called directly rather than from the menu.
    EditOnce();
    using FileSave = int(__cdecl*)(HWND);
    boxes.clear();
    const UINT_PTR answering = SetTimer(nullptr, 0, 100, AnswerBoxes);
    const int saved = reinterpret_cast<FileSave>(0x10E03EFA)(frameWindow);
    KillTimer(nullptr, answering);
    Pump(500);
    ExpectWarning(false, "unsaved-file-save-direct-is-a-save", saved != 0, "saved=" + std::to_string(saved));

    // Open Recent opened the map again (no warning); edit, then File > Save from the menu.
    EditOnce();
    Command(40007);
    Pump(500);
    ExpectWarning(false, "unsaved-menu-save-is-a-save");

    // Save As under a new name: clean under that name.
    EditOnce();
    const auto copy = map.parent_path() / "UnsavedCopy.sdc";
    std::error_code ignored;
    std::filesystem::remove(copy, ignored);
    AnswerFileDialogs(copy, false);
    Command(40008);
    dialogHandler = nullptr;
    Pump(500);
    ExpectWarning(false, "unsaved-save-as-is-a-save", std::filesystem::exists(copy), "copy=" + std::to_string(std::filesystem::exists(copy)));

    // The stock File > Open (its "Save changes?" answered No): clean as opened, and an edit straight
    // after it counts even once the ten-second check has seen the new map.
    const UINT open = FileOpenCommand();
    if (!open || other.empty()) { Fail("unsaved-stock-open", "no File > Open or no map2"); return; }
    auto stockOpen = [&](const std::filesystem::path& file) {
        AnswerFileDialogs(file, false);
        boxAnswer = IDNO;
        Command(open);
        boxAnswer = IDOK;
        dialogHandler = nullptr;
        Pump(500);
        const bool now = WindowText(frameWindow).find(file.filename().string()) != std::string::npos;
        Info("unsaved-stock-opened", WindowText(frameWindow) + " actors=" + std::to_string(Actors().size()));
        return now;
    };
    const bool opened = stockOpen(other);
    Pump(11000);
    ExpectWarning(false, "unsaved-stock-open-clean", opened);
    const bool back = stockOpen(map);
    EditOnce();
    Pump(11000);
    ExpectWarning(true, "unsaved-stock-open-edit-counts", back);
}

// ---- Window placement: each RE+ window opens where it was closed, maximised if it was, and a place
// off every monitor is ignored.
std::string PlacementEntry(const char* key)
{
    char text[256] = {};
    GetPrivateProfileStringA("WindowPlacement", key, "", text, sizeof(text), (directory / "Reloaded_Editor.ini").string().c_str());
    return text;
}
std::string RectText(const RECT& r)
{
    return std::to_string(r.left) + "," + std::to_string(r.top) + " " + std::to_string(r.right - r.left) + "x" + std::to_string(r.bottom - r.top);
}
bool SameRect(const RECT& a, const RECT& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
// The newest visible top-level window of this thread whose title starts with `title`.
HWND OpenedWindow(const char* title)
{
    struct S { const char* t; HWND r; } s{title, nullptr};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND w, LPARAM p) -> BOOL {
        auto s = reinterpret_cast<S*>(p);
        if (IsWindowVisible(w) && WindowText(w).rfind(s->t, 0) == 0) { s->r = w; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&s));
    return s.r;
}
RECT NormalRect(HWND w)
{
    WINDOWPLACEMENT p{sizeof(p)};
    GetWindowPlacement(w, &p);
    RECT r = p.rcNormalPosition;
    // Workspace coordinates for a window that is not a tool window.
    if (!(GetWindowLongA(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
    {
        MONITORINFO info{sizeof(info)};
        GetMonitorInfoA(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
        OffsetRect(&r, info.rcWork.left - info.rcMonitor.left, info.rcWork.top - info.rcMonitor.top);
    }
    return r;
}
void CloseWindowAndWait(HWND w)
{
    PostMessage(w, WM_CLOSE, 0, 0);
    for (int i = 0; i < 30 && IsWindow(w); ++i) Pump(100);
}

struct PlacedWindow { const char* name; UINT command; const char* title; const char* key; HWND (*open)(); };
HWND OpenFromConnections()
{
    HWND list = OpenedWindow("Gameplay Connections");
    if (!list) { Command(40920); list = OpenedWindow("Gameplay Connections"); }
    if (!list) return nullptr;
    // Graph... The graph is owned by the list, so the list stays open until the graph is done.
    Command(203, list);
    return OpenedWindow("Gameplay Connection Graph");
}

// Find Usages, as the Static Mesh Browser's right-click menu opens it, for a mesh in the map.
HWND OpenFindUsages()
{
    auto meshes = FreeMeshes(1);
    auto editor = *reinterpret_cast<unsigned char**>(kEditor);
    auto property = meshes.empty() ? nullptr : FindProperty(meshes[0], "StaticMesh");
    if (!editor || !property) return nullptr;
    // The browser's current mesh (GEditor+0x13c), as a click in it sets it.
    *reinterpret_cast<void**>(editor + 0x13c) = *reinterpret_cast<void**>(meshes[0] + *reinterpret_cast<unsigned*>(property + 0x3C));
    HWND search = FindControlAnywhere(41016);
    for (HWND w = search ? GetParent(search) : nullptr; w; w = GetParent(w))
    {
        Command(40925, w);
        Pump(300);
        if (HWND found = OpenedWindow("Asset Find and Replace")) return found;
    }
    return nullptr;
}

void PlacementSuite()
{
    // A free mesh selected, so Find Usages has a mesh to look for.
    SelectOnly(FreeMeshes(1));
    const PlacedWindow windows[] = {
        {"map-check", 41220, "Map Check", "MapCheck", nullptr},
        {"lighting-budget", 41150, "Lighting Budget", "LightingBudget", nullptr},
        {"render-budget", 41280, "Render Budget", "RenderBudget", nullptr},
        {"undo-history", 41105, "Undo History", "UndoHistory", nullptr},
        {"storeys", 40966, "Storeys", "Storeys", nullptr},
        {"emitter-library", 40967, "Emitter Library", "EmitterLibrary", nullptr},
        {"magic-event-workbench", 40927, "SMagicEvent Workbench", "MagicEventWorkbench", nullptr},
        {"camera-networks", 40932, "SCamNetwork Manager", "CameraNetworks", nullptr},
        {"gameplay-connections", 40920, "Gameplay Connections", "GameplayConnections", nullptr},
        {"working-views", 40921, "Working Views", "WorkingViews", nullptr},
        {"actor-assemblies", 40922, "Actor Assemblies", "ActorAssemblies", nullptr},
        {"connection-graph", 0, "Gameplay Connection Graph", "ConnectionGraph", OpenFromConnections},
        {"find-usages", 0, "Asset Find and Replace", "FindUsages", OpenFindUsages},
        {"find-usages-all-maps", 41061, "Find Usages in All Maps", "FindUsagesInAllMaps", nullptr},
        {"light-shadow", 41201, "Light and Shadow", "LightShadow", nullptr},
    };
    MONITORINFO main{sizeof(main)};
    GetMonitorInfoA(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &main);
    int index = 0;
    for (const auto& tool : windows)
    {
        ++index;
        auto open = [&]() -> HWND {
            if (tool.open) return tool.open();
            Command(tool.command);
            Pump(300);
            return OpenedWindow(tool.title);
        };
        HWND w = open();
        if (!w) { Fail("placement-" + std::string(tool.name), "did not open; boxes=" + std::to_string(boxes.size()) + (boxes.empty() ? "" : " " + boxes[0])); continue; }
        const bool resizable = (GetWindowLongA(w, GWL_STYLE) & WS_THICKFRAME) != 0;
        RECT before{};
        GetWindowRect(w, &before);
        // Somewhere new on the main monitor, and a size of its own when it can be resized.
        const int x = main.rcWork.left + 60 + index * 23, y = main.rcWork.top + 40 + index * 17;
        const int width = (before.right - before.left) + (resizable ? 31 : 0), height = (before.bottom - before.top) + (resizable ? 19 : 0);
        SetWindowPos(w, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        Pump(150);
        const RECT moved = NormalRect(w);
        CloseWindowAndWait(w);
        const auto entry = PlacementEntry(tool.key);
        HWND again = open();
        const RECT back = again ? NormalRect(again) : RECT{};
        Check(again && moved.right > moved.left && SameRect(back, moved) && !entry.empty(), "placement-" + std::string(tool.name),
              "moved " + RectText(moved) + " back " + RectText(back) + " entry '" + entry + "'" + (resizable ? "" : " (fixed size)"));
        if (again) CloseWindowAndWait(again);
    }
    if (HWND list = OpenedWindow("Gameplay Connections")) CloseWindowAndWait(list);

    // Maximised, and back to its own size when restored.
    if (HWND w = (Command(41220), Pump(300), OpenedWindow("Map Check")))
    {
        const RECT normal = NormalRect(w);
        ShowWindow(w, SW_MAXIMIZE);
        Pump(200);
        CloseWindowAndWait(w);
        const auto entry = PlacementEntry("MapCheck");
        Command(41220);
        Pump(300);
        HWND again = OpenedWindow("Map Check");
        const bool zoomed = again && IsZoomed(again);
        if (again) { ShowWindow(again, SW_RESTORE); Pump(200); }
        const RECT restored = again ? NormalRect(again) : RECT{};
        Check(zoomed && SameRect(restored, normal), "placement-maximised", "entry '" + entry + "' zoomed=" + std::to_string(zoomed) + " restored " + RectText(restored) + " was " + RectText(normal));
        if (again) CloseWindowAndWait(again);
    }

    // A place on no monitor (one unplugged since) is ignored.
    WritePrivateProfileStringA("WindowPlacement", "RenderBudget", "-30000,-30000,900,600,0", (directory / "Reloaded_Editor.ini").string().c_str());
    Command(41280);
    Pump(300);
    if (HWND w = OpenedWindow("Render Budget"))
    {
        RECT r{};
        GetWindowRect(w, &r);
        Check(MonitorFromRect(&r, MONITOR_DEFAULTTONULL) != nullptr && r.left > -20000, "placement-off-screen-ignored", RectText(r));
        CloseWindowAndWait(w);
    }
    else Fail("placement-off-screen-ignored", "Render Budget did not open");
    // A damaged entry is ignored too.
    WritePrivateProfileStringA("WindowPlacement", "LightingBudget", "nonsense", (directory / "Reloaded_Editor.ini").string().c_str());
    Command(41150);
    Pump(300);
    if (HWND w = OpenedWindow("Lighting Budget")) { Pass("placement-damaged-entry-ignored"); CloseWindowAndWait(w); }
    else Fail("placement-damaged-entry-ignored", "Lighting Budget did not open");
}

// ---- Render Budget, Meshes by cost: one row per static mesh, heaviest first, adding up to the
// zone list's mesh triangles; a click selects every copy, a zone row narrows the list to that zone.
std::vector<std::string> Cells(const std::string& row)
{
    std::vector<std::string> cells;
    size_t at = 0;
    for (size_t next; (next = row.find(" | ", at)) != std::string::npos; at = next + 3) cells.push_back(row.substr(at, next - at));
    cells.push_back(row.substr(at));
    return cells;
}
long long Number(const std::string& text)
{
    std::string digits;
    for (char c : text) if (c >= '0' && c <= '9') digits += c;
    return digits.empty() ? -1 : std::stoll(digits);
}
std::string ObjectPath(unsigned char* object)
{
    std::string path;
    for (int depth = 0; object && depth < 16; ++depth, object = *reinterpret_cast<unsigned char**>(object + 0x18))
        path = std::string(ObjectName(object)) + (path.empty() ? "" : "." + path);
    return path;
}
void ClickRow(HWND window, int id, int row, bool twice)
{
    NMITEMACTIVATE click{};
    click.hdr.hwndFrom = GetDlgItem(window, id);
    click.hdr.idFrom = static_cast<UINT_PTR>(id);
    click.hdr.code = twice ? NM_DBLCLK : NM_CLICK;
    click.iItem = row;
    SendMessageA(window, WM_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&click));
    Pump(300);
}

void MeshCostSuite()
{
    Command(41280);
    Pump(500);
    HWND budget = OpenedWindow("Render Budget");
    if (!budget) { Fail("meshcost-open", "Render Budget did not open; boxes=" + std::to_string(boxes.size())); return; }
    HWND views = GetDlgItem(budget, 110), meshList = GetDlgItem(budget, 111), zoneList = GetDlgItem(budget, 100);
    Check(views && meshList && !IsWindowVisible(meshList), "meshcost-hotspots-first", "views=" + std::to_string(views != nullptr));
    SendMessageA(views, CB_SETCURSEL, 1, 0);
    SendMessageA(budget, WM_COMMAND, MAKEWPARAM(110, CBN_SELCHANGE), reinterpret_cast<LPARAM>(views));
    Pump(300);
    const auto rows = ListRows(meshList);
    const auto zones = ListRows(zoneList);
    long long sum = 0, previous = -1;
    bool ordered = true, shares = true;
    for (const auto& row : rows)
    {
        const auto cells = Cells(row);
        if (cells.size() < 5) { ordered = false; continue; }
        const long long copies = Number(cells[1]), each = Number(cells[2]), total = Number(cells[3]);
        if (previous >= 0 && total > previous) ordered = false;
        if (copies * each < total || total <= 0) ordered = false;
        if (cells[4].empty() || cells[4].back() != '%') shares = false;
        previous = total;
        sum += total;
    }
    const long long mapMeshTriangles = zones.empty() ? -1 : Number(Cells(zones[0])[4]);
    Check(IsWindowVisible(meshList) && !rows.empty() && ordered && shares && sum == mapMeshTriangles, "meshcost-whole-map",
          std::to_string(rows.size()) + " meshes, sum " + std::to_string(sum) + " vs map mesh tris " + std::to_string(mapMeshTriangles)
          + " ordered=" + std::to_string(ordered) + " shares=" + std::to_string(shares) + " scope '" + WindowText(GetDlgItem(budget, 112)) + "'");
    for (size_t i = 0; i < rows.size() && i < 5; ++i) Info("meshcost-row", rows[i]);

    // A click selects every copy; a double-click frames them too.
    if (!rows.empty())
    {
        const auto first = Cells(rows[0]);
        Exec("SELECT NONE");
        ClickRow(budget, 111, 0, false);
        int copies = 0, same = 0;
        for (auto a : Actors())
            if (Flags(a) & 0x40)
            {
                ++copies;
                if (auto p = FindProperty(a, "StaticMesh"))
                    if (ObjectPath(*reinterpret_cast<unsigned char**>(a + *reinterpret_cast<unsigned*>(p + 0x3C))) == first[0]) ++same;
            }
        Check(copies == Number(first[1]) && same == copies, "meshcost-click-selects-copies",
              first[0] + ": selected " + std::to_string(copies) + " of " + first[1] + ", same mesh " + std::to_string(same)
              + " status '" + WindowText(GetDlgItem(budget, 102)) + "'");
        ClickRow(budget, 111, 0, true);
        Check(WindowText(GetDlgItem(budget, 102)).find("framed") != std::string::npos, "meshcost-double-click-frames", WindowText(GetDlgItem(budget, 102)));
    }

    // A zone row: only that zone counts. The busiest zone by mesh triangles, so the list differs.
    int best = -1;
    long long bestTriangles = -1;
    for (size_t i = 1; i < zones.size(); ++i)
    {
        const auto cells = Cells(zones[i]);
        if (cells[0].rfind("From camera", 0) == 0) continue;
        const long long triangles = Number(cells[4]);
        if (triangles > bestTriangles && triangles < mapMeshTriangles) { bestTriangles = triangles; best = static_cast<int>(i); }
    }
    if (best > 0)
    {
        const auto zoneName = Cells(zones[best])[0];
        ClickRow(budget, 100, best, false);
        const auto zoneRows = ListRows(meshList);
        long long zoneSum = 0;
        for (const auto& row : zoneRows) zoneSum += Number(Cells(row)[3]);
        const auto scope = WindowText(GetDlgItem(budget, 112));
        Check(zoneSum == bestTriangles && zoneRows.size() <= rows.size() && scope.find(zoneName) != std::string::npos, "meshcost-zone-only",
              zoneName + ": " + std::to_string(zoneRows.size()) + " meshes, sum " + std::to_string(zoneSum) + " vs " + std::to_string(bestTriangles) + " scope '" + scope + "'");
        // Back to the whole map.
        ClickRow(budget, 100, 0, false);
        long long again = 0;
        for (const auto& row : ListRows(meshList)) again += Number(Cells(row)[3]);
        Check(again == mapMeshTriangles, "meshcost-whole-map-again", std::to_string(again));
    }
    else Fail("meshcost-zone-only", "no zone with meshes");

    // Back to hotspots: the hotspot list shows again.
    SendMessageA(views, CB_SETCURSEL, 0, 0);
    SendMessageA(budget, WM_COMMAND, MAKEWPARAM(110, CBN_SELCHANGE), reinterpret_cast<LPARAM>(views));
    Pump(200);
    Check(IsWindowVisible(GetDlgItem(budget, 101)) && !IsWindowVisible(meshList), "meshcost-back-to-hotspots");
    CloseWindowAndWait(budget);
}

// ---- Import OBJ: a small OBJ with an MTL, a BMP that is not a power of two and a TGA that is, imported with
// hulls and placed; per-polygon and no collision; a mesh over the game's limit split into parts placed together;
// the window from File and from the Static Mesh Browser.
nlohmann::json Request(const nlohmann::json& request)
{
    using Fn = int(__cdecl*)(const char*, char*, unsigned);
    static Fn fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleA("Reloaded.Editor.dll"), "ReloadedWorkflowRequest"));
    if (!fn) return {{"ok", false}, {"error", "no ReloadedWorkflowRequest"}};
    std::vector<char> out(1 << 20);
    const int r = fn(request.dump().c_str(), out.data(), static_cast<unsigned>(out.size()));
    if (r < 0) { out.resize(static_cast<size_t>(-r)); fn(request.dump().c_str(), out.data(), static_cast<unsigned>(out.size())); }
    return nlohmann::json::parse(out.data());
}
void WriteFile(const std::filesystem::path& file, const std::string& bytes)
{
    std::ofstream out(file, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
// A box's OBJ faces (counter-clockwise from outside), its corners from vertex `base` + 1.
std::string ObjBox(double x0, double y0, double z0, double x1, double y1, double z1, int base)
{
    std::ostringstream o;
    for (int i = 0; i < 8; ++i) o << "v " << ((i & 1) ? x1 : x0) << " " << ((i & 2) ? y1 : y0) << " " << ((i & 4) ? z1 : z0) << "\n";
    const int quads[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
    for (const auto& q : quads)
        o << "f " << q[0] + 1 + base << "/1 " << q[1] + 1 + base << "/2 " << q[2] + 1 + base << "/3 " << q[3] + 1 + base << "/4\n";
    return o.str();
}
std::string Bmp24(int w, int h)
{
    const int row = (w * 3 + 3) & ~3;
    std::string b(54 + static_cast<size_t>(row) * h, '\0');
    auto u32 = [&](size_t at, uint32_t v) { for (int k = 0; k < 4; ++k) b[at + k] = static_cast<char>((v >> (8 * k)) & 0xff); };
    b[0] = 'B'; b[1] = 'M';
    u32(2, static_cast<uint32_t>(b.size())); u32(10, 54); u32(14, 40); u32(18, static_cast<uint32_t>(w)); u32(22, static_cast<uint32_t>(h));
    b[26] = 1; b[28] = 24;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            char* p = &b[54 + static_cast<size_t>(y) * row + static_cast<size_t>(x) * 3];
            p[0] = static_cast<char>(40); p[1] = static_cast<char>(90 + (x * 7 % 60)); p[2] = static_cast<char>(140 + (y * 5 % 80));
        }
    return b;
}
std::string Tga32(int w, int h)
{
    std::string t = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, static_cast<char>(w & 0xff), static_cast<char>(w >> 8), static_cast<char>(h & 0xff), static_cast<char>(h >> 8), 32, 0x28};
    for (int i = 0; i < w * h; ++i) { t += static_cast<char>(150); t += static_cast<char>(150); t += static_cast<char>(160); t += static_cast<char>(255); }
    return t;
}
// The window's picture (PrintWindow), with `over` drawn on top where it sits, as a 24-bit BMP.
void CaptureWindows(HWND base, HWND over, const std::filesystem::path& file)
{
    RECT b{}, o{};
    GetWindowRect(base, &b);
    const int w = b.right - b.left, h = b.bottom - b.top;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 24, BI_RGB};
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto old = SelectObject(dc, bitmap);
    {
        // PrintWindow draws into a screen-compatible bitmap, not straight into a DIB section.
        HDC bdc = CreateCompatibleDC(screen);
        HBITMAP bb = CreateCompatibleBitmap(screen, w, h);
        auto bold = SelectObject(bdc, bb);
        PrintWindow(base, bdc, 2);
        BitBlt(dc, 0, 0, w, h, bdc, 0, 0, SRCCOPY);
        SelectObject(bdc, bold);
        DeleteObject(bb);
        DeleteDC(bdc);
    }
    if (over)
    {
        GetWindowRect(over, &o);
        HDC odc = CreateCompatibleDC(screen);
        HBITMAP ob = CreateCompatibleBitmap(screen, o.right - o.left, o.bottom - o.top);
        auto oold = SelectObject(odc, ob);
        PrintWindow(over, odc, 2);
        BitBlt(dc, o.left - b.left, o.top - b.top, o.right - o.left, o.bottom - o.top, odc, 0, 0, SRCCOPY);
        SelectObject(odc, oold);
        DeleteObject(ob);
        DeleteDC(odc);
    }
    GdiFlush();
    const int row = (w * 3 + 3) & ~3;
    std::string out(54, '\0');
    auto u32 = [&](size_t at, uint32_t v) { for (int k = 0; k < 4; ++k) out[at + k] = static_cast<char>((v >> (8 * k)) & 0xff); };
    out[0] = 'B'; out[1] = 'M';
    u32(2, static_cast<uint32_t>(54 + row * h)); u32(10, 54); u32(14, 40); u32(18, static_cast<uint32_t>(w)); u32(22, static_cast<uint32_t>(-h));
    out[26] = 1; out[28] = 24;
    out.append(static_cast<const char*>(bits), static_cast<size_t>(row) * h);
    WriteFile(file, out);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
}
// The window whose menu bar (or one of its popups) holds the command.
HWND MenuOwner(UINT command)
{
    struct S { UINT c; HWND r; } s{command, nullptr};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND w, LPARAM p) -> BOOL {
        auto s = reinterpret_cast<S*>(p);
        std::function<bool(HMENU)> has = [&](HMENU m) {
            for (int i = 0; m && i < GetMenuItemCount(m); ++i)
            {
                if (GetMenuItemID(m, i) == s->c) return true;
                if (HMENU sub = GetSubMenu(m, i); sub && has(sub)) return true;
            }
            return false;
        };
        if (IsWindowVisible(w) && has(GetMenu(w)) && w != frameWindow) { s->r = w; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&s));
    return s.r;
}
bool MenuHas(HWND w, UINT command)
{
    std::function<bool(HMENU)> has = [&](HMENU m) {
        for (int i = 0; m && i < GetMenuItemCount(m); ++i)
        {
            if (GetMenuItemID(m, i) == command) return true;
            if (HMENU sub = GetSubMenu(m, i); sub && has(sub)) return true;
        }
        return false;
    };
    return has(GetMenu(w));
}
void ObjImportSuite()
{
    namespace fs = std::filesystem;
    const auto folder = directory / "objimport test";
    fs::create_directories(folder);
    // A crate (wood, from a BMP 100 x 60), a lid on it (metal, a 64 x 64 TGA) and a sign (a material with no image).
    WriteFile(folder / "wood.bmp", Bmp24(100, 60));
    WriteFile(folder / "metal.tga", Tga32(64, 64));
    WriteFile(folder / "crate.mtl", "newmtl CrateWood\nKd 0.6 0.4 0.2\nmap_Kd wood.bmp\nnewmtl CrateMetal\nmap_Kd -s 1 1 1 metal.tga\nnewmtl Plain\nKd 1 1 1\n");
    std::string obj = "# Import OBJ smoke test\nmtllib crate.mtl\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\no Crate\nusemtl CrateWood\ns 1\n" + ObjBox(0, 0, 0, 1, 0.8, 1, 0) +
                      "o Lid\nusemtl CrateMetal\n" + ObjBox(0.1, 0.8, 0.1, 0.9, 1.0, 0.9, 8) +
                      "o Sign\nusemtl Plain\nv 0 1.1 1.05\nv 1 1.1 1.05\nv 1 1.2 1.05\nv 0 1.2 1.05\nf 17/1 18/2 19/3 20/4\n";
    WriteFile(folder / "crate.obj", obj);
    // A wall of 12000 triangles: three parts.
    {
        std::ostringstream o;
        o << "o Wall\nusemtl Plain\n";
        for (int y = 0; y <= 60; ++y)
            for (int x = 0; x <= 100; ++x) o << "v " << x * 0.1 << " " << y * 0.1 << " 0\n";
        for (int y = 0; y < 60; ++y)
            for (int x = 0; x < 100; ++x)
            {
                const int a = y * 101 + x + 1;
                o << "f " << a << " " << a + 1 << " " << a + 102 << " " << a + 101 << "\n";
            }
        WriteFile(folder / "wall.obj", "mtllib crate.mtl\n" + o.str());
    }
    const auto crate = (folder / "crate.obj").u8string();
    const std::string crateFile(crate.begin(), crate.end());
    const auto wallPath = (folder / "wall.obj").u8string();
    const std::string wallFile(wallPath.begin(), wallPath.end());

    // What it will make.
    auto preview = Request({{"op", "objimport.preview"}, {"file", crateFile}, {"package", "MyLevel"}, {"group", "ObjTest"}, {"name", "SmokeCrate"}, {"scale", 100},
                            {"up", "y"}, {"collision", "parts"}});
    Info("objimport-preview", preview.dump().substr(0, 1500));
    if (!preview.value("ok", false)) { Fail("objimport-preview", preview.value("error", std::string())); return; }
    const auto& p = preview.at("result");
    const auto& slots = p.at("slots");
    Check(p.at("triangles") == 26 && p.at("hulls") == 3 && p.at("hullTriangles") == 36, "objimport-preview-counts",
          "triangles " + p.at("triangles").dump() + " hulls " + p.at("hulls").dump() + " hull triangles " + p.at("hullTriangles").dump());
    Check(slots.size() == 3 && slots[0].at("kind") == "image" && slots[1].at("kind") == "image" && slots[2].at("kind") == "default", "objimport-preview-materials", slots.dump());

    // The import, placed where the viewport was last clicked: one Undo step.
    auto unrealEd = *reinterpret_cast<unsigned char**>(0x117a59b0);
    if (unrealEd) { auto click = reinterpret_cast<float*>(unrealEd + 0x1bc); click[0] = 128; click[1] = -256; click[2] = 64; }
    const auto before = Undo();
    auto imported = Request({{"op", "objimport.import"}, {"file", crateFile}, {"package", "MyLevel"}, {"group", "ObjTest"}, {"name", "SmokeCrate"}, {"scale", 100},
                             {"up", "y"}, {"collision", "parts"}, {"place", true}});
    Info("objimport-import", imported.dump().substr(0, 3000));
    if (!imported.value("ok", false)) { Fail("objimport-import", imported.value("error", std::string())); return; }
    const auto& r = imported.at("result");
    const auto& part = r.at("parts").at(0);
    Check(r.at("parts").size() == 1 && part.at("path").get<std::string>().find(".ObjTest.SmokeCrate") != std::string::npos, "objimport-mesh", part.at("path").dump());
    Check(part.at("hullTriangles") == 36 && part.at("hullNodes").get<int>() > 0 && part.at("hullNodes").get<int>() < 32767, "objimport-hulls-in-editor",
          "hull triangles " + part.at("hullTriangles").dump() + " nodes " + part.at("hullNodes").dump());
    bool boundsWarning = false;
    for (const auto& w : r.at("warnings")) boundsWarning = boundsWarning || w.get<std::string>().find("bounds") != std::string::npos;
    const auto& b = part.at("bounds");
    Check(!boundsWarning && std::abs(b[0].get<double>()) < 0.5 && std::abs(b[3].get<double>() - 100) < 0.5 && std::abs(b[5].get<double>() - 120) < 0.5,
          "objimport-bounds-hulls-on-the-mesh", b.dump());
    const auto& mats = part.at("materials");
    // The engine keeps the case a name was first seen with (a Textures group may read textures).
    auto material = [&](size_t i) { auto m = mats[i].at("material").get<std::string>(); for (auto& c : m) c = static_cast<char>(tolower(static_cast<unsigned char>(c))); return m; };
    Check(mats.size() == 3 && material(0).find("textures.cratewood") != std::string::npos && material(1).find("textures.cratemetal") != std::string::npos &&
          material(2).find("defaulttexture") != std::string::npos, "objimport-materials", mats.dump());
    const auto after = Undo();
    Check(r.contains("placed") && r.at("placed").size() == 1 && after.count == before.count + 1, "objimport-placed-one-undo",
          "undo " + std::to_string(before.count) + " -> " + std::to_string(after.count) + " '" + after.current + "'");
    if (r.contains("placed") && !r.at("placed").empty())
    {
        const auto path = r.at("placed")[0].at("path").get<std::string>();
        auto actor = ActorNamed(path.substr(path.rfind('.') + 1));
        const auto at = actor ? ReadVector(actor, "Location") : Vec{};
        std::string mesh;
        if (actor)
            if (auto prop = FindProperty(actor, "StaticMesh")) mesh = ObjectName(*reinterpret_cast<unsigned char**>(actor + *reinterpret_cast<unsigned*>(prop + 0x3C)));
        Check(actor && std::abs(at.x - 128) < 0.01 && std::abs(at.y + 256) < 0.01 && std::abs(at.z - 64) < 0.01 && mesh == "SmokeCrate", "objimport-actor",
              path + " at " + Fixed(at.x) + "," + Fixed(at.y) + "," + Fixed(at.z) + " mesh " + mesh);
    }
    auto again = Request({{"op", "objimport.import"}, {"file", crateFile}, {"package", "MyLevel"}, {"group", "ObjTest"}, {"name", "SmokeCrate"}, {"scale", 100}});
    Check(!again.value("ok", true) && again.value("error", std::string()).find("exists already") != std::string::npos, "objimport-name-taken", again.dump());

    // Per polygon and no collision.
    auto poly = Request({{"op", "objimport.import"}, {"file", crateFile}, {"package", "MyLevel"}, {"group", "ObjTest"}, {"name", "SmokePoly"}, {"scale", 100},
                         {"collision", "polygons"}});
    Info("objimport-polygons", poly.dump().substr(0, 1500));
    if (poly.value("ok", false))
    {
        const auto& q = poly.at("result").at("parts").at(0);
        Check(q.at("hullTriangles") == 0 && q.at("polygonCollision").get<int>() > 0, "objimport-per-polygon",
              "hull " + q.at("hullTriangles").dump() + " polygon " + q.at("polygonCollision").dump());
    }
    else Fail("objimport-per-polygon", poly.value("error", std::string()));
    auto none = Request({{"op", "objimport.import"}, {"file", crateFile}, {"package", "MyLevel"}, {"group", "ObjTest"}, {"name", "SmokeNone"}, {"scale", 100},
                         {"collision", "none"}});
    Info("objimport-none", none.dump().substr(0, 1500));
    if (none.value("ok", false))
    {
        const auto& q = none.at("result").at("parts").at(0);
        bool off = !q.at("materials").empty();
        for (const auto& m : q.at("materials")) off = off && m.contains("collision") && !m.at("collision").get<bool>();
        Check(q.at("hullTriangles") == 0 && q.at("polygonCollision") == 0 && off, "objimport-no-collision",
              "hull " + q.at("hullTriangles").dump() + " polygon " + q.at("polygonCollision").dump() + " " + q.at("materials").dump());
    }
    else Fail("objimport-no-collision", none.value("error", std::string()));

    // Over the limit: split into parts, placed together, one Undo step.
    const auto beforeWall = Undo();
    auto wall = Request({{"op", "objimport.import"}, {"file", wallFile}, {"package", "MyLevel"}, {"group", "ObjTest"}, {"name", "SmokeWall"}, {"scale", 100},
                         {"collision", "polygons"}, {"split", true}, {"place", true}});
    Info("objimport-split", wall.dump().substr(0, 3000));
    if (wall.value("ok", false))
    {
        const auto& w = wall.at("result");
        bool within = true;
        size_t sum = 0;
        for (const auto& q : w.at("parts"))
        {
            within = within && q.at("triangles").get<size_t>() <= 5512 && q.at("polygonCollision").get<int>() <= 5512 && q.at("polygonNodes").get<int>() < 32767;
            sum += q.at("triangles").get<size_t>();
        }
        const auto afterWall = Undo();
        Check(w.at("parts").size() == 3 && within && sum == 12000 && w.at("placed").size() == 3 && afterWall.count == beforeWall.count + 1, "objimport-split-placed",
              std::to_string(w.at("parts").size()) + " parts, " + std::to_string(sum) + " triangles, placed " + std::to_string(w.at("placed").size()) + ", undo " +
              std::to_string(beforeWall.count) + " -> " + std::to_string(afterWall.count));
    }
    else Fail("objimport-split-placed", wall.value("error", std::string()));
    auto refused = Request({{"op", "objimport.preview"}, {"file", wallFile}, {"name", "SmokeWall2"}, {"collision", "polygons"}, {"split", false}});
    Check(refused.value("ok", false) && refused.at("result").at("error").get<std::string>().find("crashes the game") != std::string::npos, "objimport-limit-without-split",
          refused.dump().substr(0, 400));

    // The window, from File > Import OBJ Mesh...: the file typed in, its summary read, a picture taken.
    Check(MenuHas(frameWindow, 41640), "objimport-file-menu");
    std::string summary, status;
    int ticks = 0;
    bool opened = false;
    dialogHandler = [&](HWND w, const std::string& title) {
        if (title != "Import OBJ") return false;
        opened = true;
        ++ticks;
        if (ticks == 2)
        {
            SetDlgItemTextA(w, 200, std::string(crate.begin(), crate.end()).c_str());
            SetDlgItemTextA(w, 205, "100");
        }
        if (ticks == 12)
        {
            char text[4096] = {};
            GetDlgItemTextA(w, 210, text, sizeof(text));
            summary = text;
            CaptureWindows(w, nullptr, directory / "objimport-1.bmp");
            // Import from the window too, under another name and not placed.
            SetDlgItemTextA(w, 204, "SmokeDialog");
            CheckDlgButton(w, 209, BST_UNCHECKED);
            PostMessage(w, WM_COMMAND, IDOK, 0);
        }
        if (ticks == 20)
        {
            char text[4096] = {};
            GetDlgItemTextA(w, 211, text, sizeof(text));
            status = text;
            CaptureWindows(w, nullptr, directory / "objimport-2.bmp");
        }
        if (ticks == 21) PostMessage(w, WM_COMMAND, IDCANCEL, 0);
        return true;
    };
    Command(41640);
    dialogHandler = nullptr;
    Check(opened && summary.find("26 triangles") != std::string::npos && summary.find("CrateWood: loaded") != std::string::npos, "objimport-window-summary", summary);
    Check(status.find(".SmokeDialog (26 triangles") != std::string::npos, "objimport-window-import", status);

    // The Static Mesh Browser's File menu has it too, and opens the same window.
    Command(40468);
    Pump(800);
    HWND browser = MenuOwner(41640);
    Check(browser != nullptr, "objimport-browser-menu", browser ? WindowText(browser) : "no window with the command");
    if (browser)
    {
        CaptureWindows(browser, nullptr, directory / "objimport-3.bmp");
        opened = false;
        dialogHandler = [&](HWND w, const std::string& title) {
            if (title != "Import OBJ") return false;
            opened = true;
            PostMessage(w, WM_COMMAND, IDCANCEL, 0);
            return true;
        };
        Command(41640, browser);
        dialogHandler = nullptr;
        Check(opened, "objimport-browser-opens-window");
    }
}

void RunSuites(const std::string& list)
{
    std::stringstream names(list);
    for (std::string name; std::getline(names, name, ',');)
    {
        Info("suite", name);
        if (name == "storeys") StoreysSuite();
        else if (name == "unsaved") UnsavedSuite();
        else if (name == "placement") PlacementSuite();
        else if (name == "meshcost") MeshCostSuite();
        else if (name == "objimport") ObjImportSuite();
        else Fail("suite", "no suite called " + name);
    }
}
