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

// ---- Scatter Props: a circle round a player start and the builder brush's footprint are filled with a small
// mesh from the map, placed as one Undo step in the Scatter group, and one Undo takes them all away again.
namespace scatter
{
constexpr int kList = 100, kAddBrowser = 101, kWeight = 105, kCount = 111, kArea = 120, kRadius = 121, kAlign = 132,
              kScaleMin = 140, kScaleMax = 141, kSpacing = 142, kSeed = 150, kReroll = 151, kStatus = 160, kPlace = 170;
unsigned char* MeshOf(unsigned char* actor)
{
    auto p = FindProperty(actor, "StaticMesh");
    return p ? *reinterpret_cast<unsigned char**>(actor + *reinterpret_cast<unsigned*>(p + 0x3C)) : nullptr;
}
float Extent(unsigned char* mesh)
{
    auto b = reinterpret_cast<float*>(mesh + 0x28);
    return (std::max)({b[3] - b[0], b[4] - b[1], b[5] - b[2]});
}
std::string GroupOf(unsigned char* actor)
{
    auto p = FindProperty(actor, "Group");
    return p ? NameText(*reinterpret_cast<int*>(actor + *reinterpret_cast<unsigned*>(p + 0x3C))) : "";
}
void Click(HWND w, int id) { SendMessageA(w, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(w, id))); Pump(200); }
void Type(HWND w, int id, const std::string& text) { SetWindowTextA(GetDlgItem(w, id), text.c_str()); Pump(50); }
void Choose(HWND w, int id, int index)
{
    SendMessageA(GetDlgItem(w, id), CB_SETCURSEL, index, 0);
    SendMessageA(w, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(GetDlgItem(w, id)));
    Pump(50);
}
std::vector<unsigned char*> Props()
{
    std::vector<unsigned char*> result;
    for (auto a : Actors()) if (ClassName(a) == "StaticMeshActor" && Name(a).rfind("ScatterProp", 0) == 0) result.push_back(a);
    return result;
}
// Place, then check the new props and that one Undo removes them (and Redo brings them back, Undo again).
void PlaceAndUndo(HWND w, const std::string& label, const std::function<bool(Vec)>& inside, unsigned char* mesh)
{
    Pump(1500);
    const auto status = WindowText(GetDlgItem(w, kStatus));
    Info(label + "-preview", status);
    const auto before = Props().size();
    const int actors = static_cast<int>(Actors().size());
    const auto undo = Undo();
    Click(w, kPlace);
    Pump(500);
    const auto after = Props();
    const auto placed = after.size() - before;
    const auto now = Undo();
    bool grouped = true, within = true, meshes = true, shown = true;
    std::string where;
    for (size_t i = before; i < after.size(); ++i)
    {
        grouped = grouped && GroupOf(after[i]) == "Scatter";
        const auto at = ReadVector(after[i], "Location");
        within = within && inside(at);
        meshes = meshes && MeshOf(after[i]) == mesh;
        shown = shown && !(Flags(after[i]) & 0x10);
        if (i < before + 3) where += VecText(at) + " ";
    }
    Check(placed > 0 && static_cast<int>(Actors().size()) == actors + static_cast<int>(placed) && Selected() == static_cast<int>(placed), label + "-placed",
          std::to_string(placed) + " props, selected " + std::to_string(Selected()) + " at " + where + "status '" + WindowText(GetDlgItem(w, kStatus)) + "'");
    Check(grouped && meshes, label + "-group-and-mesh", "grouped=" + std::to_string(grouped) + " mesh=" + std::to_string(meshes));
    Check(within, label + "-inside-area");
    Check(shown, label + "-not-hidden");
    Check(now.current == "Scatter props" && now.count - now.undone == undo.count - undo.undone + 1, label + "-one-undo-step",
          "'" + now.current + "' steps " + std::to_string(undo.count - undo.undone) + "->" + std::to_string(now.count - now.undone));
    Exec("TRANSACTION UNDO");
    Pump(300);
    Check(Props().size() == before && static_cast<int>(Actors().size()) == actors, label + "-undo-removes-all",
          std::to_string(Props().size()) + " props left, actors " + std::to_string(Actors().size()) + " (was " + std::to_string(actors) + ")");
    Exec("TRANSACTION REDO");
    Pump(300);
    Check(Props().size() == before + placed, label + "-redo-brings-back", std::to_string(Props().size()));
    Exec("TRANSACTION UNDO");
    Pump(300);
    Check(Props().size() == before, label + "-undo-again", std::to_string(Props().size()));
}
}

void ScatterSuite()
{
    using namespace scatter;
    // A small mesh from the map becomes the Static Mesh Browser's current one.
    unsigned char* mesh = nullptr;
    for (auto a : OfClass("StaticMeshActor"))
        if (auto m = MeshOf(a); m && Extent(m) >= 16 && Extent(m) <= 96) { mesh = m; break; }
    if (!mesh) { Fail("scatter-mesh", "no small static mesh in the map"); return; }
    auto editor = *reinterpret_cast<unsigned char**>(kEditor);
    *reinterpret_cast<unsigned char**>(editor + 0x13C) = mesh;
    // A player start's spot is open floor: the last click goes there.
    unsigned char* start = nullptr;
    for (auto a : Actors()) if (ClassName(a).find("PlayerStart") != std::string::npos) { start = a; break; }
    if (!start) { Fail("scatter-start", "no player start in the map"); return; }
    auto spot = ReadVector(start, "Location");
    auto unrealEd = *reinterpret_cast<unsigned char**>(0x117a59b0);
    *reinterpret_cast<Vec*>(unrealEd + 0x1bc) = spot;
    Info("scatter-setup", ObjectName(mesh) + std::string(" extent ") + Fixed(Extent(mesh)) + " at " + ClassName(start) + " " + VecText(spot));

    Exec("SELECT NONE");
    Command(41500);
    Pump(500);
    HWND w = OpenedWindow("Scatter Props");
    if (!w) { Fail("scatter-open", "the window did not open; boxes=" + std::to_string(boxes.size())); return; }
    Pass("scatter-open");
    // An empty list explains itself.
    Pump(600);
    const int rowsAtStart = static_cast<int>(SendMessageA(GetDlgItem(w, kList), LVM_GETITEMCOUNT, 0, 0));
    Info("scatter-empty-status", WindowText(GetDlgItem(w, kStatus)));
    Click(w, kAddBrowser);
    const int rows = static_cast<int>(SendMessageA(GetDlgItem(w, kList), LVM_GETITEMCOUNT, 0, 0));
    Check(rowsAtStart == 0 && rows == 1, "scatter-add-browser-mesh", std::to_string(rowsAtStart) + "->" + std::to_string(rows));
    // The same mesh again is refused.
    Click(w, kAddBrowser);
    Check(SendMessageA(GetDlgItem(w, kList), LVM_GETITEMCOUNT, 0, 0) == 1 && WindowText(GetDlgItem(w, kStatus)).find("already") != std::string::npos,
          "scatter-no-duplicate", WindowText(GetDlgItem(w, kStatus)));

    // Re-roll changes the seed.
    Type(w, kSeed, "12345");
    Click(w, kReroll);
    Check(WindowText(GetDlgItem(w, kSeed)) != "12345", "scatter-reroll", WindowText(GetDlgItem(w, kSeed)));

    // A circle of 384 round the start, 12 props, scaled 0.8 to 1.2, 8 apart.
    Choose(w, kArea, 1);
    Type(w, kRadius, "384");
    Type(w, kCount, "12");
    Type(w, kScaleMin, "0.8");
    Type(w, kScaleMax, "1.2");
    Type(w, kSpacing, "8");
    Type(w, kSeed, "777");
    PlaceAndUndo(w, "scatter-circle", [&](Vec v) { return std::hypot(v.x - spot.x, v.y - spot.y) <= 384 + 1; }, mesh);

    // The same with align to slope.
    SendMessageA(GetDlgItem(w, kAlign), BM_SETCHECK, BST_CHECKED, 0);
    Click(w, kAlign);
    SendMessageA(GetDlgItem(w, kAlign), BM_SETCHECK, BST_CHECKED, 0);
    PlaceAndUndo(w, "scatter-align", [&](Vec v) { return std::hypot(v.x - spot.x, v.y - spot.y) <= 384 + 1; }, mesh);
    SendMessageA(GetDlgItem(w, kAlign), BM_SETCHECK, BST_UNCHECKED, 0);
    Click(w, kAlign);

    // The builder brush's footprint: the default cube, moved over the start.
    auto builder = Actors().size() > 1 ? Actors()[1] : nullptr;
    if (builder && ClassName(builder) == "Brush")
    {
        Command(40911);
        WriteVector(builder, "Location", spot);
        Pump(200);
        Choose(w, kArea, 0);
        Type(w, kCount, "6");
        Type(w, kSpacing, "4");
        Info("scatter-builder", VecText(ReadVector(builder, "Location")));
        PlaceAndUndo(w, "scatter-builder", [&](Vec v) { return std::fabs(v.x - spot.x) <= 300 && std::fabs(v.y - spot.y) <= 300; }, mesh);
    }
    else Fail("scatter-builder", "no builder brush");

    // A count past the limit says so and places nothing.
    Type(w, kCount, "5000");
    Pump(800);
    const auto before = Props().size();
    Click(w, kPlace);
    Check(Props().size() == before && WindowText(GetDlgItem(w, kStatus)).find("between 1 and 500") != std::string::npos, "scatter-count-limit", WindowText(GetDlgItem(w, kStatus)));
    Type(w, kCount, "12");
    Choose(w, kArea, 1);
    Pump(1500);
    // hold=<ms> in -Extra: the viewport cameras look down on the start and the editor keeps running that long
    // after the report with the window open, for a picture (a new seed a second later redraws the preview).
    const auto hold = Ini("hold");
    if (!hold.empty())
    {
        // The player start with the most free floor round it.
        Type(w, kRadius, "448");
        Type(w, kCount, "24");
        int best = -1;
        Vec chosen = spot;
        for (auto a : Actors())
        {
            if (ClassName(a).find("PlayerStart") == std::string::npos) continue;
            const auto at = ReadVector(a, "Location");
            *reinterpret_cast<Vec*>(unrealEd + 0x1bc) = at;
            Type(w, kSeed, std::to_string(500 + best));
            Pump(900);
            const int fit = static_cast<int>(Number(WindowText(GetDlgItem(w, kStatus)).substr(0, 3)));
            if (fit > best) { best = fit; chosen = at; }
        }
        *reinterpret_cast<Vec*>(unrealEd + 0x1bc) = chosen;
        spot = chosen;
        for (auto a : Actors())
            if (ClassName(a) == "Camera")
            {
                WriteVector(a, "Location", {spot.x - 450, spot.y, spot.z + 380});
                if (auto p = FindProperty(a, "Rotation")) *reinterpret_cast<Rot*>(a + *reinterpret_cast<unsigned*>(p + 0x3C)) = {-7000, 0, 0};
            }
        Type(w, kRadius, "448");
        Type(w, kCount, "24");
        Pump(1500);
        Info("scatter-hold", hold + " " + WindowText(GetDlgItem(w, kStatus)));
        static HWND held;
        held = w;
        SetTimer(nullptr, 0, 1000, [](HWND, UINT, UINT_PTR id, DWORD) { KillTimer(nullptr, id); SetWindowTextA(GetDlgItem(held, kSeed), "778"); });
        // Then Place, for a picture of the meshes themselves.
        SetTimer(nullptr, 0, 9000, [](HWND, UINT, UINT_PTR id, DWORD) { KillTimer(nullptr, id); SendMessageA(held, WM_COMMAND, MAKEWPARAM(kPlace, BN_CLICKED), 0); });
        finishAfter = static_cast<DWORD>(std::stoul(hold));
        return;
    }
    CloseWindowAndWait(w);
    Check(!IsWindow(w), "scatter-close");
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
        else if (name == "scatter") ScatterSuite();
        else Fail("suite", "no suite called " + name);
    }
}
