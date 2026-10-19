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

// ---- Compare with Backup: File > Save keeps a backup; then a mesh is moved, another deleted and a light's
// brightness changed. The comparison lists all three, with the backup's value beside the map's; one ticked row
// (the deleted mesh) comes back as one Undo step and Undo takes it away again; then Tick All brings back the
// rest too.
constexpr UINT CompareBackupCommand = 41600; // File > Compare with Backup...
std::filesystem::path NewestBackup(const std::filesystem::path& folder)
{
    std::filesystem::path newest;
    std::filesystem::file_time_type when{};
    std::error_code e;
    for (auto& f : std::filesystem::directory_iterator(folder, e))
        if (f.is_regular_file() && f.path().extension() == ".sdc" && (newest.empty() || f.last_write_time() >= when)) { newest = f.path(); when = f.last_write_time(); }
    return newest;
}
struct CompareRun { bool listed = false; std::vector<std::string> rows; std::string status; int ticked = -1; };
// Opens File > Compare with Backup on this file; tick(rows, list) ticks rows in the list and returns whether to bring back.
CompareRun RunCompare(const std::filesystem::path& backup, const std::function<bool(const std::vector<std::string>&, HWND)>& tick)
{
    CompareRun run;
    int fileTicks = 0, windowTicks = 0;
    bool bring = false;
    dialogHandler = [&](HWND w, const std::string& title) {
        if (title == "Choose a Backup to Compare") return AnswerFile(w, backup.wstring(), fileTicks);
        char cls[64] = {};
        GetClassNameA(w, cls, sizeof(cls));
        if (title != "Compare with Backup" || strcmp(cls, "ReloadedMapAuthoringPreview")) return false;
        ++windowTicks;
        HWND list = GetDlgItem(w, 11);
        if (windowTicks == 3)
        {
            run.listed = true;
            run.rows = ListRows(list);
            bring = tick(run.rows, list);
            Pump(200);
            run.status = WindowText(GetDlgItem(w, 12));
        }
        if (windowTicks == 6) PostMessage(w, WM_COMMAND, bring ? IDOK : IDCANCEL, 0);
        return true;
    };
    Command(CompareBackupCommand);
    dialogHandler = nullptr;
    return run;
}
void TickRow(HWND list, int row)
{
    LVITEMA item{};
    item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    item.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageA(list, LVM_SETITEMSTATE, row, reinterpret_cast<LPARAM>(&item));
    Pump(100);
    SendMessageA(list, WM_KEYDOWN, VK_SPACE, 0);
    SendMessageA(list, WM_KEYUP, VK_SPACE, 0);
}
int RowIndex(const std::vector<std::string>& rows, const std::string& change, const std::string& target, const std::string& property = "")
{
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto cells = Cells(rows[i]);
        if (cells.size() >= 5 && cells[0] == change && cells[1] == target && (property.empty() || cells[2].rfind(property, 0) == 0)) return static_cast<int>(i);
    }
    return -1;
}
void CompareSuite()
{
    const auto mapName = std::filesystem::path(Ini("map")).stem().string();
    const auto folder = directory / "ReloadedEditor" / "Backups" / mapName;
    Command(40007);
    Pump(500);
    const auto backup = NewestBackup(folder);
    Check(!backup.empty(), "compare-backup-kept", backup.string() + " boxes=" + std::to_string(boxes.size()));
    if (backup.empty()) return;

    auto meshes = OfClass("StaticMeshActor", 50);
    auto lights = OfClass("Light", 5);
    if (meshes.size() < 2 || lights.empty()) { Fail("compare-fixture", "needs two static meshes and a light"); return; }
    unsigned char* mover = meshes[0];
    const std::string moverName = Name(mover), goneName = Name(meshes[1]), lightName = Name(lights[0]);
    const Vec moverAt = ReadVector(mover, "Location"), goneAt = ReadVector(meshes[1], "Location");
    const float brightness = ReadFloat(lights[0], "LightBrightness");
    WriteVector(mover, "Location", {moverAt.x + 256, moverAt.y, moverAt.z});
    if (auto p = FindProperty(lights[0], "LightBrightness")) *reinterpret_cast<float*>(lights[0] + *reinterpret_cast<unsigned*>(p + 0x3C)) = brightness + 7;
    for (auto a : Actors()) SetSelected(a, false);
    SetSelected(meshes[1], true);
    Exec("ACTOR DELETE");
    Pump(300);
    const size_t actorsEdited = Actors().size();
    Check(!ActorNamed(goneName), "compare-edits", "moved " + moverName + ", deleted " + goneName + ", light " + lightName + " " + Fixed(brightness) + "->" + Fixed(brightness + 7));

    // One row: the deleted mesh.
    auto before = Undo();
    auto run = RunCompare(backup, [&](const std::vector<std::string>& rows, HWND list) {
        const int gone = RowIndex(rows, "Deleted", goneName);
        if (gone >= 0) TickRow(list, gone);
        return gone >= 0;
    });
    for (size_t i = 0; i < run.rows.size() && i < 12; ++i) Info("compare-row", run.rows[i]);
    const int movedRow = RowIndex(run.rows, "Moved", moverName, "Location"), lightRow = RowIndex(run.rows, "Changed", lightName, "LightBrightness");
    Check(run.listed && RowIndex(run.rows, "Deleted", goneName) >= 0 && movedRow >= 0 && lightRow >= 0 && run.rows.size() == 3, "compare-lists-differences",
          std::to_string(run.rows.size()) + " rows; status '" + run.status + "' boxes=" + std::to_string(boxes.size()));
    if (lightRow >= 0) Info("compare-light-row", run.rows[lightRow]);
    Check(run.status.find("1 row(s) ticked") != std::string::npos, "compare-tick-one", run.status);
    // The copy the comparison loaded is unloaded and its file removed.
    Check(CountFiles(directory / "ReloadedEditor" / "compare") == 0, "compare-copy-removed");
    auto after = Undo();
    auto back = ActorNamed(goneName);
    if (!back) back = ActorNamed(goneName + "_2");
    const Vec backAt = back ? ReadVector(back, "Location") : Vec{};
    std::string text;
    for (auto& b : boxes) text += b + " ";
    Check(back && std::fabs(backAt.x - goneAt.x) < 0.01f && std::fabs(backAt.y - goneAt.y) < 0.01f && Actors().size() == actorsEdited + 1
              && std::fabs(ReadVector(mover, "Location").x - (moverAt.x + 256)) < 0.01f && after.count == before.count + 1 && after.current == "Bring back from backup",
          "compare-bring-back-one", "back=" + (back ? Name(back) : std::string("none")) + " actors " + std::to_string(actorsEdited) + "->" + std::to_string(Actors().size())
              + " undo '" + after.current + "' " + text);
    Exec("TRANSACTION UNDO");
    Pump(500);
    Check(!ActorNamed(goneName) && !ActorNamed(goneName + "_2") && Actors().size() == actorsEdited, "compare-undo", "actors=" + std::to_string(Actors().size()) + " undo '" + Undo().current + "'");

    // More to bring back: an emitter with its particle components deleted, and a copy of a mesh added.
    unsigned char* emitter = nullptr;
    int emitterParts = 0;
    for (auto a : Actors())
        if (auto p = FindProperty(a, "Emitters"); p && !(Flags(a) & 0x10))
            if (const int n = *reinterpret_cast<int*>(a + *reinterpret_cast<unsigned*>(p + 0x3C) + 4); n > 0) { emitter = a; emitterParts = n; break; }
    const std::string emitterName = emitter ? Name(emitter) : std::string();
    if (emitter)
        if (auto p = FindProperty(emitter, "Emitters"))
            Info("compare-emitter-component", ObjectPath(**reinterpret_cast<unsigned char***>(emitter + *reinterpret_cast<unsigned*>(p + 0x3C))));
    for (auto a : Actors()) SetSelected(a, false);
    if (emitter) { SetSelected(emitter, true); Exec("ACTOR DELETE"); Pump(300); }
    std::string copyName;
    if (meshes.size() > 2)
    {
        for (auto a : Actors()) SetSelected(a, false);
        SetSelected(meshes[2], true);
        std::set<std::string> names;
        for (auto a : Actors()) names.insert(Name(a));
        Exec("ACTOR DUPLICATE");
        Pump(300);
        for (auto a : Actors()) if (!names.count(Name(a))) copyName = Name(a);
    }
    Info("compare-more-edits", "deleted emitter " + emitterName + " (" + std::to_string(emitterParts) + " components), added copy " + copyName);

    // Everything: Tick All.
    run = RunCompare(backup, [&](const std::vector<std::string>&, HWND list) {
        SendMessageA(GetParent(list), WM_COMMAND, 13, 0);
        return true;
    });
    for (size_t i = 0; i < run.rows.size() && i < 12; ++i) Info("compare-row", run.rows[i]);
    unsigned char* emitterBack = nullptr;
    for (auto a : Actors()) if (!emitterName.empty() && (Name(a) == emitterName || Name(a).rfind(emitterName + "_", 0) == 0)) emitterBack = a;
    int partsBack = 0;
    if (emitterBack)
        if (auto p = FindProperty(emitterBack, "Emitters")) partsBack = *reinterpret_cast<int*>(emitterBack + *reinterpret_cast<unsigned*>(p + 0x3C) + 4);
    Check((emitterName.empty() || (emitterBack && partsBack == emitterParts)) && (copyName.empty() || !ActorNamed(copyName)), "compare-bring-back-emitter-and-delete-copy",
          "emitter back=" + (emitterBack ? Name(emitterBack) : std::string("none")) + " components " + std::to_string(partsBack) + "/" + std::to_string(emitterParts)
              + " copy " + copyName + (ActorNamed(copyName) ? " still there" : " gone"));
    auto light = ActorNamed(lightName);
    mover = reinterpret_cast<unsigned char*>(ActorNamed(moverName));
    // Brought back again, it is named _3: _2 is kept for Undo too.
    back = nullptr;
    for (auto a : Actors()) if (Name(a) == goneName || Name(a).rfind(goneName + "_", 0) == 0) back = a;
    text.clear();
    for (auto& b : boxes) text += b + " ";
    Check(back && light && std::fabs(ReadFloat(light, "LightBrightness") - brightness) < 0.01f
              && mover && std::fabs(ReadVector(mover, "Location").x - moverAt.x) < 0.01f,
          "compare-bring-back-all", "status '" + run.status + "' back=" + (back ? Name(back) : std::string("none")) + " brightness=" + (light ? Fixed(ReadFloat(light, "LightBrightness")) : "-")
              + " undo '" + Undo().current + "' " + text);
    // Compared again, nothing differs.
    run = RunCompare(backup, [&](const std::vector<std::string>&, HWND) { return false; });
    Check(!run.listed && BoxContains("match"), "compare-nothing-left", "rows=" + std::to_string(run.rows.size()) + " boxes=" + (boxes.empty() ? std::string("none") : boxes[0]));
    Exec("TRANSACTION UNDO");
    Pump(500);
    Check(ActorNamed(lightName) && std::fabs(ReadFloat(ActorNamed(lightName), "LightBrightness") - (brightness + 7)) < 0.01f, "compare-undo-all", "undo '" + Undo().current + "'");
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
        else if (name == "compare") CompareSuite();
        else Fail("suite", "no suite called " + name);
    }
}
