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

// ---- Command Palette: Help > Command Palette lists the menu bar's commands; typing finds one, Enter or a
// click runs it, and the command run last is listed first the next time. Ctrl+Shift+P opens and closes it.
constexpr UINT kPalette = 41580;
std::vector<std::string> PaletteRows(HWND palette)
{
    std::vector<std::string> rows;
    HWND list = GetDlgItem(palette, 101);
    const auto count = SendMessageA(list, LB_GETCOUNT, 0, 0);
    for (LRESULT i = 0; i < count; ++i)
    {
        std::string text(static_cast<size_t>(SendMessageA(list, LB_GETTEXTLEN, i, 0)) + 1, '\0');
        SendMessageA(list, LB_GETTEXT, i, reinterpret_cast<LPARAM>(text.data()));
        text.resize(strlen(text.c_str()));
        rows.push_back(text);
    }
    return rows;
}
HWND OpenPalette()
{
    Command(kPalette);
    Pump(300);
    HWND palette = TopOfClass("ReloadedCommandPalette");
    return palette && IsWindowVisible(palette) ? palette : nullptr;
}
void TypeInPalette(HWND palette, const char* text)
{
    SetWindowTextA(GetDlgItem(palette, 100), text);
    Pump(100);
}
// Pump with the message box answerer running: a command the palette posted runs inside the pump.
void PumpAnswering(DWORD ms)
{
    boxes.clear();
    UINT_PTR timer = SetTimer(nullptr, 0, 100, AnswerBoxes);
    Pump(ms);
    KillTimer(nullptr, timer);
}
bool HasRow(const std::vector<std::string>& rows, const std::string& start)
{
    for (const auto& row : rows) if (row.rfind(start, 0) == 0) return true;
    return false;
}
// The editor frame with the palette drawn over it, as a 24-bit BMP (window contents only).
void SavePaletteShot(HWND palette, const std::string& file)
{
    RECT frame{}, popup{};
    GetWindowRect(frameWindow, &frame);
    GetWindowRect(palette, &popup);
    const int w = frame.right - frame.left, h = frame.bottom - frame.top;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen), part = CreateCompatibleDC(screen);
    HBITMAP image = CreateCompatibleBitmap(screen, w, h);
    HBITMAP piece = CreateCompatibleBitmap(screen, popup.right - popup.left, popup.bottom - popup.top);
    HGDIOBJ old = SelectObject(dc, image), oldPart = SelectObject(part, piece);
    PrintWindow(frameWindow, dc, 2 /* PW_RENDERFULLCONTENT */);
    PrintWindow(palette, part, 2);
    BitBlt(dc, popup.left - frame.left, popup.top - frame.top, popup.right - popup.left, popup.bottom - popup.top, part, 0, 0, SRCCOPY);
    BITMAPINFOHEADER header{sizeof(header), w, h, 1, 24, BI_RGB};
    const int stride = (w * 3 + 3) & ~3;
    std::vector<char> pixels(static_cast<size_t>(stride) * h);
    GetDIBits(dc, image, 0, h, pixels.data(), reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS);
    BITMAPFILEHEADER fileHeader{0x4D42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(header) + pixels.size()), 0, 0,
                                sizeof(BITMAPFILEHEADER) + sizeof(header)};
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(pixels.data(), static_cast<std::streamsize>(pixels.size()));
    SelectObject(dc, old);
    SelectObject(part, oldPart);
    DeleteObject(image);
    DeleteObject(piece);
    DeleteDC(dc);
    DeleteDC(part);
    ReleaseDC(nullptr, screen);
    Info("palette-shot", file);
}
// Ctrl+Shift+P as the keyboard would deliver it to the frame's thread: the key state says Ctrl and Shift
// are down while the P press and release are fetched from the queue.
void PressCtrlShiftP()
{
    BYTE keys[256] = {}, held[256] = {};
    GetKeyboardState(keys);
    memcpy(held, keys, sizeof(held));
    held[VK_CONTROL] = held[VK_LCONTROL] = held[VK_SHIFT] = held[VK_LSHIFT] = 0x80;
    SetKeyboardState(held);
    PostMessageA(frameWindow, WM_KEYDOWN, 'P', 0x00190001);
    Pump(150);
    PostMessageA(frameWindow, WM_KEYUP, 'P', 0xC0190001);
    Pump(150);
    SetKeyboardState(keys);
    Pump(200);
}
void PaletteSuite()
{
    std::error_code ignored;
    std::filesystem::remove(directory / "ReloadedEditor" / "command_palette_recent.json", ignored);

    HWND palette = OpenPalette();
    Check(palette != nullptr, "palette-open", "boxes=" + std::to_string(boxes.size()));
    if (!palette) return;
    auto rows = PaletteRows(palette);
    int withKeys = 0;
    for (const auto& row : rows) if (row.find('\t') != std::string::npos) ++withKeys;
    for (size_t i = 0; i < rows.size() && i < 4; ++i) Info("palette-row", rows[i]);
    // Stock commands, RE+ ones and a submenu's, each with its menu path; the palette itself is not listed.
    Check(rows.size() > 100 && HasRow(rows, "File > ") && HasRow(rows, "Build > Map Check...") && HasRow(rows, "Help > RE+ Shortcuts...")
              && HasRow(rows, "RE+ Tools > Camera Bookmarks > Go to Bookmark 1\t1") && !HasRow(rows, "Help > Command Palette"),
          "palette-lists-menus", std::to_string(rows.size()) + " commands, " + std::to_string(withKeys) + " with a shortcut");
    Check(withKeys > 10, "palette-shows-shortcuts", std::to_string(withKeys));
    const auto hint = WindowText(GetDlgItem(palette, 102));
    Check(hint.find(std::to_string(rows.size()) + " of " + std::to_string(rows.size())) == 0, "palette-count", hint);

    TypeInPalette(palette, "rendr budg");
    rows = PaletteRows(palette);
    Check(!rows.empty() && rows[0].rfind("Build > Render Budget...", 0) == 0, "palette-fuzzy-first",
          std::to_string(rows.size()) + " rows, first '" + (rows.empty() ? std::string() : rows[0]) + "'");
    TypeInPalette(palette, "zzqqxx");
    Check(PaletteRows(palette).empty(), "palette-no-match", WindowText(GetDlgItem(palette, 102)));
    // shot=<file.bmp> and shotquery=<typed text> in -Extra: a picture of the palette over the editor.
    if (const auto shot = Ini("shot"); !shot.empty())
    {
        TypeInPalette(palette, Ini("shotquery", "light").c_str());
        Pump(300);
        SavePaletteShot(palette, shot);
    }
    TypeInPalette(palette, "map check");
    rows = PaletteRows(palette);
    Check(!rows.empty() && rows[0].rfind("Build > Map Check...", 0) == 0, "palette-words-first", rows.empty() ? "" : rows[0]);

    // Enter runs the selected row: the palette goes and Map Check opens.
    SendMessageA(GetDlgItem(palette, 100), WM_KEYDOWN, VK_RETURN, 0);
    PumpAnswering(1500);
    HWND check = OpenedWindow("Map Check");
    Check(!IsWindow(palette) && check != nullptr, "palette-enter-runs", "palette open=" + std::to_string(IsWindow(palette) != 0) + " boxes=" + std::to_string(boxes.size()));
    if (check) CloseWindowAndWait(check);

    // A click runs a row too.
    palette = OpenPalette();
    if (!palette) { Fail("palette-reopen"); return; }
    rows = PaletteRows(palette);
    Check(!rows.empty() && rows[0].rfind("Build > Map Check...", 0) == 0, "palette-recent-first", rows.empty() ? "" : rows[0]);
    TypeInPalette(palette, "render budget");
    HWND list = GetDlgItem(palette, 101);
    RECT item{};
    SendMessageA(list, LB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&item));
    const LPARAM at = MAKELPARAM((item.left + item.right) / 2, (item.top + item.bottom) / 2);
    SendMessageA(list, WM_LBUTTONDOWN, MK_LBUTTON, at);
    SendMessageA(list, WM_LBUTTONUP, 0, at);
    PumpAnswering(1500);
    HWND budget = OpenedWindow("Render Budget");
    Check(!IsWindow(palette) && budget != nullptr, "palette-click-runs", "boxes=" + std::to_string(boxes.size()));
    if (budget) CloseWindowAndWait(budget);

    // Most recent first, then the one before; the arrows move; Esc closes.
    palette = OpenPalette();
    if (!palette) { Fail("palette-reopen"); return; }
    rows = PaletteRows(palette);
    Check(rows.size() > 2 && rows[0].rfind("Build > Render Budget...", 0) == 0 && rows[1].rfind("Build > Map Check...", 0) == 0,
          "palette-recent-order", rows.size() > 1 ? rows[0] + " | " + rows[1] : "");
    std::ifstream saved(directory / "ReloadedEditor" / "command_palette_recent.json");
    Check(saved.good(), "palette-recent-saved");
    HWND edit = GetDlgItem(palette, 100);
    SendMessageA(edit, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageA(edit, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageA(edit, WM_KEYDOWN, VK_UP, 0);
    Check(SendMessageA(GetDlgItem(palette, 101), LB_GETCURSEL, 0, 0) == 1, "palette-arrows");
    SendMessageA(edit, WM_KEYDOWN, VK_ESCAPE, 0);
    Pump(200);
    Check(!IsWindow(palette) && !OpenedWindow("Render Budget"), "palette-escape-closes");

    // Ctrl+Shift+P: only while the editor is the foreground application.
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    if (process != GetCurrentProcessId()) { Info("palette-hotkey", "skipped: the editor is not in front"); return; }
    PressCtrlShiftP();
    palette = TopOfClass("ReloadedCommandPalette");
    Check(palette && IsWindowVisible(palette), "palette-hotkey-opens");
    PressCtrlShiftP();
    Check(!TopOfClass("ReloadedCommandPalette"), "palette-hotkey-closes");
// ---- Linked prefabs: two meshes (one Tagged, the other's Event pointing at it) saved as an
// assembly, placed linked twice; one instance edited and the prefab updated from it rebuilds the
// other where it stands, in one Undo step that Undo takes back; the link survives save and reopen;
// Break Link drops an instance.
using Json = nlohmann::json;
Json Request(const Json& request)
{
    using Fn = int(__cdecl*)(const char*, char*, unsigned);
    static Fn call = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleA("Reloaded.Editor.dll"), "ReloadedWorkflowRequest"));
    if (!call) throw std::runtime_error("no workflow bridge");
    std::vector<char> out(16 * 1024 * 1024);
    const auto text = request.dump();
    call(text.c_str(), out.data(), static_cast<unsigned>(out.size()));
    auto reply = Json::parse(out.data());
    if (!reply.value("ok", false)) throw std::runtime_error(request.value("op", std::string()) + ": " + reply.value("error", std::string()));
    return reply.at("result");
}
unsigned char* ActorByPath(const std::string& path)
{
    const auto name = path.substr(path.find_last_of('.') + 1);
    for (auto a : Actors()) if (!_stricmp(Name(a).c_str(), name.c_str())) return a;
    return nullptr;
}
Json Instances() { return Request({{"op", "prefab.instances"}}); }
Json LiveOf(const Json& instances, size_t i) { return i < instances.size() ? instances[i].at("live") : Json::array(); }
// The member of a live list whose name ends in `stem` (the placement prefix differs per copy).
unsigned char* MemberNamed(const Json& live, const std::string& stem)
{
    for (const auto& m : live)
    {
        const auto path = m.at("path").get<std::string>();
        if (path.size() >= stem.size() && !_stricmp(path.substr(path.size() - stem.size()).c_str(), stem.c_str())) return ActorByPath(path);
    }
    return nullptr;
}
std::string TagOf(const std::string& path, const char* key)
{
    for (const auto& a : Request({{"op", "actors"}})) if (a.at("path") == path) return a.value(key, std::string());
    return {};
}
// The editor frame and a tool window over it, for the report (PrintWindow of each, never the desktop).
void SaveWindowBitmap(HWND w, const std::filesystem::path& file)
// ---- Light and Shadow Map: sampling with static meshes, kept work after edits, the settings
// switches, and a picture of the map and its settings window for the report.
std::uintmax_t LogSize()
{
    std::error_code ignored;
    return std::filesystem::file_size(directory / "Reloaded.Editor.dll.log", ignored);
}
// The last "LightShadowMap: ... patches" line the editor wrote after a point in its log.
std::string LastShadowLine(std::uintmax_t from)
{
    std::ifstream log(directory / "Reloaded.Editor.dll.log", std::ios::binary);
    log.seekg(static_cast<std::streamoff>(from));
    std::string line, last;
    while (std::getline(log, line))
        if (line.find("LightShadowMap: ") != std::string::npos && line.find(" patches") != std::string::npos) last = line;
    return last;
}
long long NumberBefore(const std::string& text, const std::string& word)
{
    const auto at = text.find(word);
    if (at == std::string::npos) return -1;
    size_t start = at;
    while (start > 0 && isdigit(static_cast<unsigned char>(text[start - 1]))) --start;
    return start < at ? std::stoll(text.substr(start, at - start)) : -1;
}
// The window as drawn (viewports included), as a 24-bit BMP.
bool SaveWindowPicture(HWND w, const std::filesystem::path& file)
{
    RECT r{};
    GetWindowRect(w, &r);
    const int width = r.right - r.left, height = r.bottom - r.top;
    if (width <= 0 || height <= 0) return;
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto old = SelectObject(dc, bitmap);
    PrintWindow(w, dc, 2);
    BITMAPFILEHEADER header{0x4d42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + width * height * 4), 0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
    out.write(static_cast<const char*>(bits), static_cast<std::streamsize>(width) * height * 4);
    if (width <= 0 || height <= 0) return false;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bitmap);
    BOOL printed = PrintWindow(w, dc, 2 /* PW_RENDERFULLCONTENT */);
    GdiFlush();
    const DWORD stride = (DWORD(width) * 3 + 3) & ~3u, size = stride * DWORD(height);
    // Some windows print nothing; then copy what the window itself shows.
    bool blank = true;
    for (DWORD i = 0; i < size && blank; i += 97) blank = static_cast<const unsigned char*>(bits)[i] == 0;
    if (blank)
    {
        HDC own = GetWindowDC(w);
        printed = BitBlt(dc, 0, 0, width, height, own, 0, 0, SRCCOPY);
        ReleaseDC(w, own);
        GdiFlush();
    }
    BITMAPFILEHEADER header{};
    header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + size;
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    out.write(static_cast<const char*>(bits), size);
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
// ---- Reachability: Build > Reachability Map works out where each team walks, a few steps at a
// time; Map Check shows that it is being worked out and refreshes with its rows when it ends; the
// menu item cancels a run going on.
// A window as it is drawn, saved as a 24-bit BMP (shot=<folder> in -Extra).
void SaveShot(HWND w, const std::string& name)
{
    const auto folder = Ini("shot");
    if (folder.empty() || !w) return;
    RECT r{};
    GetWindowRect(w, &r);
    const int width = r.right - r.left, height = r.bottom - r.top;
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, height, 1, 24, BI_RGB};
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto old = SelectObject(dc, bitmap);
    // The full content first (the Direct3D viewports); a plain print when that comes back black.
    const DWORD stride = (static_cast<DWORD>(width) * 3 + 3) & ~3u, size = stride * static_cast<DWORD>(height);
    auto black = [&] { GdiFlush(); for (DWORD i = 0; i < size; i += 97) if (static_cast<unsigned char*>(bits)[i]) return false; return true; };
    if (!PrintWindow(w, dc, 2) || black()) PrintWindow(w, dc, 0);
    Info("shot", name + (black() ? " black" : " ok"));
    BITMAPFILEHEADER file{0x4D42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + size), 0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
    FILE* f = nullptr;
    if (!fopen_s(&f, (folder + "\\" + name + ".bmp").c_str(), "wb") && f)
    {
        fwrite(&file, sizeof(file), 1, f);
        fwrite(&info.bmiHeader, sizeof(info.bmiHeader), 1, f);
        fwrite(bits, 1, size, f);
        fclose(f);
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
}
void PrefabsSuite()
{
    auto pair = FreeMeshes(2, 5);
    if (pair.size() < 2) { Fail("prefab-meshes", "fewer than two free meshes"); return; }
    const std::string level = Request({{"op", "map"}}).at("level");
    const std::string nameA = Name(pair[0]), nameB = Name(pair[1]);
    const Json idA = {{"path", level + "." + nameA}, {"class", "Engine.StaticMeshActor"}}, idB = {{"path", level + "." + nameB}, {"class", "Engine.StaticMeshActor"}};
    Request({{"op", "light.set"}, {"actor", idA}, {"properties", {{"Tag", "PfSmokeLamp"}}}});
    Request({{"op", "light.set"}, {"actor", idB}, {"properties", {{"Event", "PfSmokeLamp"}}}});
    SelectOnly(pair);
    const auto entry = Request({{"op", "prefab.save"}, {"name", "Smoke Prefab"}});
    Check(entry.at("actors").size() == 2 && entry.at("bindings").empty(), "prefab-saved", "actors=" + std::to_string(entry.at("actors").size()) + " bindings=" + std::to_string(entry.at("bindings").size()));
    const auto id = entry.at("id").get<std::string>();
    const auto a = ReadVector(pair[0], "Location");

    // Two linked copies, the second turned a quarter, one Undo step each.
    auto before = Undo();
    Request({{"op", "prefab.place"}, {"assembly", id}, {"position", {a.x, a.y + 2048, a.z}}, {"rotation", {0, 0, 0}}});
    auto after = Undo();
    Check(after.count == before.count + 1 && after.current == "Place linked prefab", "prefab-place-one-step", "undo '" + after.current + "'");
    Request({{"op", "prefab.place"}, {"assembly", id}, {"position", {a.x + 2048, a.y + 2048, a.z}}, {"rotation", {0, 16384, 0}}});
    auto instances = Instances();
    Check(instances.size() == 2 && LiveOf(instances, 0).size() == 2 && LiveOf(instances, 1).size() == 2, "prefab-two-instances", instances.dump().substr(0, 300));
    if (instances.size() != 2) return;
    const auto counts = Request({{"op", "prefab.counts"}});
    Check(counts.value(id, 0) == 2, "prefab-count", counts.dump());

    // Tags are fresh per copy, and each copy's Event follows its own Tag.
    auto lamp1 = MemberNamed(LiveOf(instances, 0), nameA), lamp2 = MemberNamed(LiveOf(instances, 1), nameA);
    auto switch1 = MemberNamed(LiveOf(instances, 0), nameB), switch2 = MemberNamed(LiveOf(instances, 1), nameB);
    if (!lamp1 || !lamp2 || !switch1 || !switch2) { Fail("prefab-members", "members not found"); return; }
    const auto tag1 = TagOf(level + "." + Name(lamp1), "tag"), tag2 = TagOf(level + "." + Name(lamp2), "tag");
    const auto event1 = TagOf(level + "." + Name(switch1), "event"), event2 = TagOf(level + "." + Name(switch2), "event");
    Check(tag1 != tag2 && !_stricmp(tag1.c_str(), event1.c_str()) && !_stricmp(tag2.c_str(), event2.c_str()) && tag1.find("PfSmokeLamp") != std::string::npos,
          "prefab-fresh-tags", tag1 + "/" + event1 + " " + tag2 + "/" + event2);

    // Select Instance and Select All Instances, from the frame menu.
    SelectOnly({lamp1});
    Command(41540);
    Check(Selected() == 2, "prefab-select-instance", "selected=" + std::to_string(Selected()));
    SelectOnly({lamp1});
    Command(41541);
    Check(Selected() == 4, "prefab-select-all", "selected=" + std::to_string(Selected()));

    // Edit the first copy: its lamp up 64 (an Undo step of its own), then update from it.
    Request({{"op", "design.translate"}, {"members", Json::array({{{"path", level + "." + Name(lamp1)}, {"class", "Engine.StaticMeshActor"}}})}, {"delta", {0, 0, 64}}});
    // The second copy moved as a whole: it is rebuilt where it now stands.
    Request({{"op", "design.translate"}, {"members", Json::array({{{"path", level + "." + Name(lamp2)}, {"class", "Engine.StaticMeshActor"}},
                                                                  {{"path", level + "." + Name(switch2)}, {"class", "Engine.StaticMeshActor"}}})}, {"delta", {128, -64, 0}}});
    const auto lamp2Before = ReadVector(lamp2, "Location"), switch2Before = ReadVector(switch2, "Location");
    const std::string oldLamp2 = Name(lamp2), oldSwitch2 = Name(switch2);
    SelectOnly({switch1});
    before = Undo();
    Command(41542);
    after = Undo();
    Check(after.count == before.count + 1 && after.current == "Update linked prefab" && BoxContains("Update the prefab"), "prefab-update-one-step",
          "undo '" + after.current + "' boxes=" + std::to_string(boxes.size()));
    instances = Instances();
    auto newLamp2 = MemberNamed(LiveOf(instances, 1), nameA), newSwitch2 = MemberNamed(LiveOf(instances, 1), nameB);
    if (!newLamp2 || !newSwitch2) { Fail("prefab-rebuilt", "rebuilt members not found " + instances.dump().substr(0, 300)); return; }
    const auto lampNow = ReadVector(newLamp2, "Location"), switchNow = ReadVector(newSwitch2, "Location");
    Check(Name(newLamp2) != oldLamp2 && std::fabs(lampNow.z - lamp2Before.z - 64) < 0.5f && std::fabs(lampNow.x - lamp2Before.x) < 0.5f && std::fabs(lampNow.y - lamp2Before.y) < 0.5f
          && std::fabs(switchNow.x - switch2Before.x) + std::fabs(switchNow.y - switch2Before.y) + std::fabs(switchNow.z - switch2Before.z) < 0.5f && !ActorByPath(oldLamp2), "prefab-rebuilt-in-place",
          VecText(lamp2Before) + "->" + VecText(lampNow) + " switch " + VecText(switch2Before) + "->" + VecText(switchNow));
    const auto tagNew = TagOf(level + "." + Name(newLamp2), "tag"), eventNew = TagOf(level + "." + Name(newSwitch2), "event");
    Check(tagNew != tag1 && tagNew != tag2 && !_stricmp(tagNew.c_str(), eventNew.c_str()), "prefab-rebuilt-fresh-tags", tagNew + "/" + eventNew);

    // Undo puts the old copy back: its own actors, where they were.
    const std::string newLampName = Name(newLamp2);
    before = Undo();
    Command(40019);
    Pump(500);
    after = Undo();
    instances = Instances();
    auto back = ActorByPath(oldLamp2);
    std::string live;
    for (const auto& m : LiveOf(instances, 1)) live += m.at("path").get<std::string>() + " ";
    Check(back && Same(ReadVector(back, "Location"), lamp2Before) && !ActorByPath(newLampName) && LiveOf(instances, 1).size() == 2, "prefab-undo-restores",
          "undid '" + before.current + "' now '" + after.current + "' back=" + (back ? VecText(ReadVector(back, "Location")) : std::string("none")) + " new still there=" + (ActorByPath(newLampName) ? "yes" : "no") + " live=" + live);

    // Actor Assemblies shows the count; a picture for the report.
    Command(40922);
    Pump(800);
    if (HWND assemblies = OpenedWindow("Actor Assemblies"))
    {
        HWND list = FindWindowExA(assemblies, nullptr, WC_LISTVIEWA, nullptr);
        bool shown = false;
        for (const auto& row : ListRows(list)) if (row.find("Smoke Prefab") != std::string::npos && row.find("2 linked instances in this map") != std::string::npos) shown = true;
        Check(shown, "prefab-list-count");
        SelectOnly({switch1});
        Command(41541);
        Exec("CAMERA ALIGN");
        Pump(800);
        SaveWindowBitmap(frameWindow, directory / "prefabs-frame.bmp");
        SaveWindowBitmap(assemblies, directory / "prefabs-window.bmp");
        CloseWindowAndWait(assemblies);
    }
    else Fail("prefab-list-count", "Actor Assemblies did not open");

    // The link is in the map: saved and opened again, both copies are still instances.
    Command(40007);
    Pump(500);
    ReopenRecent(IDYES);
    instances = Instances();
    Check(instances.size() == 2 && LiveOf(instances, 0).size() == 2 && LiveOf(instances, 1).size() == 2, "prefab-survives-reopen", instances.dump().substr(0, 300));

    // Break Link: the copy's actors stay, unlinked.
    if (auto lamp = MemberNamed(LiveOf(instances, 1), nameA))
    {
        SelectOnly({lamp});
        before = Undo();
        Command(41543);
        after = Undo();
        instances = Instances();
        Check(instances.size() == 1 && ActorByPath(level + "." + Name(lamp)) && after.count == before.count + 1, "prefab-break-link",
              "instances=" + std::to_string(instances.size()) + " undo '" + after.current + "' " + std::to_string(before.count) + "->" + std::to_string(after.count));
        SelectOnly({lamp});
        Command(41542);
        Check(BoxContains("Select an actor of a linked prefab"), "prefab-unlinked-refuses-update");
    return printed && out.good();
}
void LightShadowSuite()
{
    // On: sampled from nothing.
    auto from = LogSize();
    const double onMs = Command(41200);
    Pump(500);
    auto line = LastShadowLine(from);
    Info("light-shadow-on", "command ms=" + Fixed(onMs, 0) + " | " + line);
    Check(NumberBefore(line, " patches") > 0, "light-shadow-sampled", std::to_string(NumberBefore(line, " patches")) + " patches");
    Check(NumberBefore(line, " on static meshes") > 0, "light-shadow-mesh-floors", std::to_string(NumberBefore(line, " on static meshes")) + " patches on static meshes");

    // An in-game light moved: sampled again a moment later, keeping the other lights.
    // One with a static mesh near it, for the step after.
    unsigned char *light = nullptr, *mesh = nullptr;
    const auto meshes = OfClass("StaticMeshActor");
    for (auto a : Actors())
    {
        auto type = FindProperty(a, "LightType");
        if (!type || !a[*reinterpret_cast<unsigned*>(type + 0x3C)]) continue;
        const unsigned flagsA = *reinterpret_cast<unsigned*>(a + 0x2e8), flagsB = *reinterpret_cast<unsigned*>(a + 0x2ec);
        if (!(flagsB & 0x800000) && !(flagsA & 0x20000)) continue;
        if (!light) light = a;
        const auto lamp = ReadVector(a, "Location");
        for (auto m : meshes)
        {
            const auto at = ReadVector(m, "Location");
            const double dx = at.x - lamp.x, dy = at.y - lamp.y, dz = at.z - lamp.z;
            if (dx * dx + dy * dy + dz * dz < 600.0 * 600.0) { light = a; mesh = m; break; }
        }
        if (mesh) break;
    }
    if (light)
    {
        auto at = ReadVector(light, "Location");
        at.x += 48;
        WriteVector(light, "Location", at);
        *reinterpret_cast<unsigned*>(light + 0x2e8) |= 0x100; // bLightChanged: its render data follows
        from = LogSize();
        Exec("TRANSACTION REDO"); // nothing to redo: only marks the map changed
        Pump(4000);
        line = LastShadowLine(from);
        Info("light-shadow-light-moved", Name(light) + " | " + line);
        Check(line.find(" kept ") != std::string::npos, "light-shadow-keeps-other-lights", line.substr(line.find(" in ") == std::string::npos ? 0 : line.find(" in ")));
    }
    else Info("light-shadow-light-moved", "no in-game light in this map");

    // A static mesh near a light moved: only the lights it is near are worked out again.
    if (mesh)
    {
        auto at = ReadVector(mesh, "Location");
        at.x += 40;
        WriteVector(mesh, "Location", at);
        from = LogSize();
        Exec("TRANSACTION REDO");
        Pump(4000);
        line = LastShadowLine(from);
        Info("light-shadow-mesh-moved", Name(mesh) + " | " + line);
        Check(line.find(" kept ") != std::string::npos || line.find("from nothing") == std::string::npos, "light-shadow-mesh-move-incremental", line.substr(line.find(" in ") == std::string::npos ? 0 : line.find(" in ")));
    }

    // Settings: static mesh floors off leaves only BSP patches; on again brings them back.
    Command(41201);
    Pump(500);
    HWND settings = OpenedWindow("Light and Shadow");
    Check(settings != nullptr, "light-shadow-settings-open");
    if (settings)
    {
        Check(IsDlgButtonChecked(settings, 111) == BST_CHECKED && IsDlgButtonChecked(settings, 112) == BST_CHECKED && IsDlgButtonChecked(settings, 113) == BST_CHECKED,
              "light-shadow-switches-on");
        CheckDlgButton(settings, 112, BST_UNCHECKED);
        SendMessageA(settings, WM_COMMAND, 106, 0); // Apply
        Pump(300);
        const auto off = WindowText(GetDlgItem(settings, 109));
        CheckDlgButton(settings, 112, BST_CHECKED);
        SendMessageA(settings, WM_COMMAND, 106, 0);
        Pump(300);
        const auto on = WindowText(GetDlgItem(settings, 109));
        Check(off.find("on static meshes") == std::string::npos && on.find("on static meshes") != std::string::npos, "light-shadow-mesh-floors-switch",
              off.substr(0, off.find('\r')) + " / " + on.substr(0, on.find('\r')));
        char ini[64] = {};
        GetPrivateProfileStringA("LightShadowMap", "MeshFloors", "", ini, sizeof(ini), (directory / "Reloaded_Editor.ini").string().c_str());
        Check(std::string(ini) == "1", "light-shadow-switch-saved", std::string("MeshFloors=") + ini);
        // Pictures: the editor with the map on, and the settings window. The disposable editor
        // starts hidden; it is shown (without taking the focus) only while they are taken.
        const bool wasVisible = IsWindowVisible(frameWindow) != 0, wasIconic = IsIconic(frameWindow) != 0;
        ShowWindow(frameWindow, wasIconic ? SW_SHOWNOACTIVATE : SW_SHOWNA);
        if (IsIconic(frameWindow)) ShowWindow(frameWindow, SW_RESTORE);
        RECT frame{};
        GetWindowRect(frameWindow, &frame);
        SetWindowPos(settings, HWND_TOP, frame.right - 520, frame.top + 80, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        RedrawWindow(frameWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        Pump(2500);
        const bool first = SaveWindowPicture(frameWindow, directory / "lightshadow-1.bmp");
        const bool second = SaveWindowPicture(settings, directory / "lightshadow-2.bmp");
        if (!wasVisible) ShowWindow(frameWindow, SW_HIDE);
        Info("light-shadow-pictures", (directory / "lightshadow-1.bmp").string() + " printed " + std::to_string(first) + std::to_string(second)
             + " visible " + std::to_string(wasVisible) + " iconic " + std::to_string(wasIconic) + " rect " + std::to_string(frame.left) + ","
             + std::to_string(frame.top) + "," + std::to_string(frame.right) + "," + std::to_string(frame.bottom));
        CloseWindowAndWait(settings);
    }
// ---- Map to OBJ: File > Export holds the stock Export... and Map to OBJ...; an export writes an OBJ
// whose faces all point at vertices it wrote, materials the MTL defines and textures that are PNGs on
// disk; Selection only keeps to the selected static mesh actors; the folder is offered again.
struct ObjFile
{
    size_t v = 0, vt = 0, vn = 0, f = 0, badCorners = 0;
    std::vector<std::string> objects;
    std::set<std::string> used;
};
ObjFile ReadObj(const std::filesystem::path& path)
{
    ObjFile o;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);)
    {
        if (line.rfind("v ", 0) == 0) ++o.v;
        else if (line.rfind("vt ", 0) == 0) ++o.vt;
        else if (line.rfind("vn ", 0) == 0) ++o.vn;
        else if (line.rfind("o ", 0) == 0) o.objects.push_back(line.substr(2));
        else if (line.rfind("usemtl ", 0) == 0) o.used.insert(line.substr(7));
        else if (line.rfind("f ", 0) == 0)
        {
            ++o.f;
            std::istringstream corners(line.substr(2));
            int n = 0;
            for (std::string c; corners >> c; ++n)
            {
                long long a = 0, b = 0, d = 0;
                if (sscanf_s(c.c_str(), "%lld/%lld/%lld", &a, &b, &d) != 3 || a < 1 || a > (long long)o.v || b < 1 || b > (long long)o.vt || d < 1 || d > (long long)o.vn) ++o.badCorners;
            }
            if (n < 3) ++o.badCorners;
        }
    }
    return o;
}
// The frame with a tool window drawn over it where it sits, as a 24-bit BMP (PrintWindow, so only these
// two windows are captured, never the desktop).
void SaveWindowShot(HWND tool, const std::string& file)
{
    RECT frame{}, over{};
    GetWindowRect(frameWindow, &frame);
    GetWindowRect(tool, &over);
    const int w = frame.right - frame.left, h = frame.bottom - frame.top;
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen), toolDc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, w, h), toolBitmap = CreateCompatibleBitmap(screen, over.right - over.left, over.bottom - over.top);
    auto old = SelectObject(dc, bitmap);
    auto oldTool = SelectObject(toolDc, toolBitmap);
    PrintWindow(frameWindow, dc, PW_RENDERFULLCONTENT);
    PrintWindow(tool, toolDc, PW_RENDERFULLCONTENT);
    BitBlt(dc, over.left - frame.left, over.top - frame.top, over.right - over.left, over.bottom - over.top, toolDc, 0, 0, SRCCOPY);
    BITMAPINFOHEADER info{sizeof(info), w, h, 1, 24, BI_RGB};
    const int stride = ((w * 3 + 3) / 4) * 4;
    std::vector<char> pixels(static_cast<size_t>(stride) * h);
    SelectObject(dc, old);
    GetDIBits(dc, bitmap, 0, h, pixels.data(), reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS);
    BITMAPFILEHEADER header{0x4D42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(info) + pixels.size()), 0, 0, sizeof(BITMAPFILEHEADER) + sizeof(info)};
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    out.write(pixels.data(), static_cast<std::streamsize>(pixels.size()));
    SelectObject(toolDc, oldTool);
    DeleteObject(bitmap);
    DeleteObject(toolBitmap);
    DeleteDC(dc);
    DeleteDC(toolDc);
    ReleaseDC(nullptr, screen);
}
void ObjExportSuite()
{
    // The menu.
    HMENU file = GetSubMenu(GetMenu(frameWindow), 0);
    HMENU exportMenu = nullptr;
    for (int i = 0; file && i < GetMenuItemCount(file); ++i)
        if (HMENU sub = GetSubMenu(file, i))
            if (GetMenuState(sub, 41660, MF_BYCOMMAND) != UINT(-1)) exportMenu = sub;
    Check(exportMenu && GetMenuState(exportMenu, 40017, MF_BYCOMMAND) != UINT(-1) && GetMenuState(file, 40017, MF_BYCOMMAND) != UINT(-1),
          "objexport-menu", exportMenu ? "File > Export holds Map... and Map to OBJ..." : "no File > Export submenu");

    Command(41660);
    Pump(300);
    HWND w = OpenedWindow("Export Map to OBJ");
    if (!w) { Fail("objexport-open", "the window did not open; boxes=" + std::to_string(boxes.size())); return; }
    const auto root = directory.parent_path() / "ObjExportTest";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    auto run = [&](const std::filesystem::path& folder, bool selection) {
        SetDlgItemTextA(w, 301, folder.string().c_str());
        CheckDlgButton(w, 303, BST_CHECKED);
        CheckDlgButton(w, 304, BST_CHECKED);
        CheckDlgButton(w, 305, selection ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextA(w, 306, "1");
        const double ms = Command(307, w);
        const auto status = WindowText(GetDlgItem(w, 309));
        Info("objexport-status", status + " (" + Fixed(ms / 1000.0) + " s)");
        return status;
    };
    const auto map = std::filesystem::path(Ini("map")).stem().string();
    const auto full = root / "full";
    const auto status = run(full, false);
    const auto obj = ReadObj(full / (map + ".obj"));
    Check(status.rfind("Wrote", 0) == 0 && obj.f > 1000 && obj.badCorners == 0, "objexport-obj",
          std::to_string(obj.v) + " v, " + std::to_string(obj.vt) + " vt, " + std::to_string(obj.vn) + " vn, " + std::to_string(obj.f) + " faces, "
          + std::to_string(obj.objects.size()) + " objects, bad corners " + std::to_string(obj.badCorners));
    Check(!obj.objects.empty() && obj.objects[0] == "BSP" && obj.objects.size() > 10, "objexport-objects", obj.objects.empty() ? "none" : obj.objects[0]);
    if (const auto shot = Ini("shot"); !shot.empty()) { SaveWindowShot(w, shot); Info("objexport-shot", shot); }

    // Every material used is defined, every texture named is a PNG.
    std::set<std::string> defined;
    size_t maps = 0, missing = 0, notPng = 0;
    {
        std::ifstream in(full / (map + ".mtl"));
        for (std::string line; std::getline(in, line);)
        {
            if (line.rfind("newmtl ", 0) == 0) defined.insert(line.substr(7));
            if (line.rfind("map_Kd ", 0) == 0)
            {
                ++maps;
                std::ifstream png(full / line.substr(7), std::ios::binary);
                char signature[8] = {};
                if (!png) ++missing;
                else if (!png.read(signature, 8) || memcmp(signature, "\x89PNG\r\n\x1a\n", 8)) ++notPng;
            }
        }
    }
    size_t undefined = 0;
    for (const auto& m : obj.used) if (!defined.count(m)) ++undefined;
    Check(undefined == 0 && maps > 10 && missing == 0 && notPng == 0, "objexport-mtl",
          std::to_string(obj.used.size()) + " used, " + std::to_string(defined.size()) + " defined, undefined " + std::to_string(undefined) + ", "
          + std::to_string(maps) + " textures, missing " + std::to_string(missing) + ", not PNG " + std::to_string(notPng));

    // Selection only: three visible static mesh actors, no surfaces.
    Exec("SELECT NONE");
    Exec("POLY SELECT NONE");
    std::vector<unsigned char*> picked;
    for (auto a : OfClass("StaticMeshActor"))
    {
        auto hidden = FindProperty(a, "bHidden");
        auto mesh = FindProperty(a, "StaticMesh");
        if (!hidden || !mesh) continue;
        if (*reinterpret_cast<unsigned*>(a + *reinterpret_cast<unsigned*>(hidden + 0x3C)) & *reinterpret_cast<unsigned*>(hidden + 0x64)) continue;
        if (!*reinterpret_cast<void**>(a + *reinterpret_cast<unsigned*>(mesh + 0x3C))) continue;
        SetSelected(a, true);
        picked.push_back(a);
        if (picked.size() == 3) break;
    }
    const auto some = root / "selection";
    const auto selected = run(some, true);
    const auto part = ReadObj(some / (map + ".obj"));
    bool bsp = false;
    for (const auto& o : part.objects) if (o.rfind("BSP", 0) == 0) bsp = true;
    Check(picked.size() == 3 && part.objects.size() == 3 && !bsp && part.badCorners == 0, "objexport-selection-only",
          std::to_string(part.objects.size()) + " objects, " + std::to_string(part.f) + " faces: " + selected);
    for (auto a : picked) SetSelected(a, false);

    // A scale that is no number is refused in the status line.
    SetDlgItemTextA(w, 306, "abc");
    Command(307, w);
    Check(WindowText(GetDlgItem(w, 309)).find("scale") != std::string::npos, "objexport-bad-scale", WindowText(GetDlgItem(w, 309)));
    SetDlgItemTextA(w, 306, "1");

    // Closed and opened again: the last folder is offered.
    SetDlgItemTextA(w, 301, full.string().c_str());
    CloseWindowAndWait(w);
    Command(41660);
    Pump(300);
    w = OpenedWindow("Export Map to OBJ");
    Check(w && WindowText(GetDlgItem(w, 301)) == full.string(), "objexport-folder-remembered", w ? WindowText(GetDlgItem(w, 301)) : "not open");
    if (w) CloseWindowAndWait(w);
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
std::vector<std::string> ReachRows(HWND check)
{
    std::vector<std::string> rows;
    for (const auto& row : ListRows(GetDlgItem(check, 100)))
        if (row.find("| Reachability |") != std::string::npos) rows.push_back(row);
    return rows;
}
bool ReachChecked()
{
    return (GetMenuState(GetMenu(frameWindow), 41480, MF_BYCOMMAND) & MF_CHECKED) != 0;
}
void ReachSuite()
{
    Info("command", "41220 " + Fixed(Command(41220)) + " ms");
    Pump(500);
    HWND check = OpenedWindow("Map Check");
    if (!check) { Fail("reach-mapcheck-open", "Map Check did not open"); return; }
    auto rows = ReachRows(check);
    Check(!rows.empty() && rows[0].find("being worked out") != std::string::npos, "reach-mapcheck-working", rows.empty() ? "no rows" : rows[0]);
    Info("command", "41480 " + Fixed(Command(41480)) + " ms");
    Check(ReachChecked(), "reach-menu-checked");
    const DWORD started = GetTickCount();
    bool done = false;
    while (GetTickCount() - started < 600000)
    {
        Pump(1000);
        rows = ReachRows(check);
        if (!rows.empty() && rows.back().find("Worked out on foot") != std::string::npos) { done = true; break; }
    }
    Check(done, "reach-finished", Fixed((GetTickCount() - started) / 1000.0) + " s; " + std::to_string(rows.size()) + " rows");
    for (const auto& row : rows) Info("reach-row", row);
    if (done)
    {
        const auto& summary = rows.back();
        const auto spies = summary.find("spies "), mercs = summary.find("mercs ");
        Check(spies != std::string::npos && mercs != std::string::npos && Number(summary.substr(spies, 16)) > 100 && Number(summary.substr(mercs, 16)) > 100,
              "reach-both-teams", summary.substr(spies == std::string::npos ? 0 : spies, 60));
        // A row selects what it is about.
        for (size_t i = 0; i + 1 < rows.size(); ++i)
        {
            if (Cells(rows[i]).back() == "-") continue;
            const auto all = ListRows(GetDlgItem(check, 100));
            const auto at = std::find(all.begin(), all.end(), rows[i]) - all.begin();
            Exec("SELECT NONE");
            ClickRow(check, 100, static_cast<int>(at), false);
            Check(Selected() >= 1, "reach-row-selects", Cells(rows[i]).back() + " selected=" + std::to_string(Selected()));
            Exec("SELECT NONE");
            break;
        }
        Pump(1500);
        SaveShot(frameWindow, "reach-viewports");
        SaveShot(check, "reach-mapcheck");
    }
    // Off hides it; on again with the map unchanged shows the same result at once.
    Info("command", "41480 " + Fixed(Command(41480)) + " ms");
    Check(!ReachChecked(), "reach-menu-unchecked");
    Info("command", "41480 " + Fixed(Command(41480)) + " ms");
    Pump(300);
    rows = ReachRows(check);
    Check(ReachChecked() && !rows.empty(), "reach-on-again");
    // The second map (ShipD): on starts a run, off cancels it and Map Check says so; on again
    // works it out, and Map Check refreshes with its rows.
    if (Ini("map2").empty()) { Info("reach-map2", "no map2: cancel and second map skipped"); CloseWindowAndWait(check); return; }
    boxAnswer = IDNO;
    Command(40971);
    boxAnswer = IDOK;
    Pump(1500);
    Info("reach-map2", "actors=" + std::to_string(Actors().size()));
    if (ReachChecked()) Command(41480);
    Info("command", "41480 on " + Fixed(Command(41480)) + " ms");
    Pump(1500);
    Info("command", "41480 off " + Fixed(Command(41480)) + " ms");
    Info("command", "41220 " + Fixed(Command(41220)) + " ms");
    Pump(300);
    rows = ReachRows(check);
    Check(!rows.empty() && rows.back().find("Cancelled") != std::string::npos, "reach-cancel", rows.empty() ? "no rows" : rows.back());
    Info("command", "41480 on " + Fixed(Command(41480)) + " ms");
    Info("command", "41220 " + Fixed(Command(41220)) + " ms");
    const DWORD second = GetTickCount();
    done = false;
    while (GetTickCount() - second < 600000)
    {
        Pump(1000);
        rows = ReachRows(check);
        if (!rows.empty() && rows.back().find("Worked out on foot") != std::string::npos) { done = true; break; }
    }
    Check(done, "reach-map2-finished", Fixed((GetTickCount() - second) / 1000.0) + " s; " + std::to_string(rows.size()) + " rows");
    for (const auto& row : rows) Info("reach-map2-row", row);
    Pump(2000);
    SaveShot(frameWindow, "reach-map2-viewports");
    SaveShot(check, "reach-map2-mapcheck");
    // Map Design's plan layer: Tools > Show reachability map (its window's command 777).
    Info("command", "40948 " + Fixed(Command(40948)) + " ms");
    Pump(1500);
    if (HWND design = OpenedWindow("Map Design"))
    {
        SendMessageA(design, WM_COMMAND, 777, 0);
        Pump(1500);
        const auto status = WindowText(GetDlgItem(design, 720));
        const bool checked = (GetMenuState(GetMenu(design), 777, MF_BYCOMMAND) & MF_CHECKED) != 0;
        Check(checked && status.find("Reachability map: green both teams") != std::string::npos, "reach-map-design-layer", status.substr(0, 160));
        SaveShot(design, "reach-map2-design");
    }
    else Fail("reach-map-design-layer", "Map Design did not open");
    CloseWindowAndWait(check);
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
        else if (name == "palette") PaletteSuite();
        else if (name == "prefabs") PrefabsSuite();
        else if (name == "lightshadow") LightShadowSuite();
        else if (name == "objexport") ObjExportSuite();
        else if (name == "objimport") ObjImportSuite();
        else if (name == "reach") ReachSuite();
        else Fail("suite", "no suite called " + name);
    }
}
