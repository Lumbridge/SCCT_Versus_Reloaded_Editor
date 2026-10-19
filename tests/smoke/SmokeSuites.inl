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

// ---- Map Outliner: every actor listed in a virtual list, a search over all columns, grouping, a
// click selecting in the editor and the list following the editor's selection, Hide / Show in one
// Undo step each, Delete asking first and deleting in one Undo step that Undo brings back, and the
// window reopening where it was.
namespace OutlinerIds { enum : int { kSearch = 101, kGroupBy = 103, kFollow = 104, kList = 105, kStatus = 106, kHide = 110, kShow, kShowAll, kDelete, kFrame, kRefresh }; }
int ListCount(HWND list) { return static_cast<int>(SendMessageA(list, LVM_GETITEMCOUNT, 0, 0)); }
std::vector<int> ListSelection(HWND list)
{
    std::vector<int> rows;
    for (int i = static_cast<int>(SendMessageA(list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED)); i >= 0;
         i = static_cast<int>(SendMessageA(list, LVM_GETNEXTITEM, static_cast<WPARAM>(i), LVNI_SELECTED)))
        rows.push_back(i);
    return rows;
}
void SelectListRows(HWND list, const std::vector<int>& rows)
{
    LVITEMA clear{};
    clear.stateMask = LVIS_SELECTED;
    SendMessageA(list, LVM_SETITEMSTATE, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(&clear));
    for (int row : rows)
    {
        LVITEMA item{};
        item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        item.state = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageA(list, LVM_SETITEMSTATE, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&item));
    }
    Pump(400);
}
// The list row whose Name column is exactly this, or -1.
int RowNamed(const std::vector<std::string>& rows, const std::string& name)
{
    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto cell = Cells(rows[i])[0];
        cell.erase(0, cell.find_first_not_of(' '));
        if (cell == name) return static_cast<int>(i);
    }
    return -1;
}
void Search(HWND outliner, const std::string& text)
{
    HWND edit = GetDlgItem(outliner, OutlinerIds::kSearch);
    SetWindowTextA(edit, text.c_str());
    SendMessageA(outliner, WM_COMMAND, MAKEWPARAM(OutlinerIds::kSearch, EN_CHANGE), reinterpret_cast<LPARAM>(edit));
    Pump(200);
}
void GroupOutliner(HWND outliner, int by)
{
    HWND combo = GetDlgItem(outliner, OutlinerIds::kGroupBy);
    SendMessageA(combo, CB_SETCURSEL, by, 0);
    SendMessageA(outliner, WM_COMMAND, MAKEWPARAM(OutlinerIds::kGroupBy, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
    Pump(200);
}
std::string Lowered(std::string text)
{
    for (auto& c : text) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return text;
}

void OutlinerSuite()
{
    // On the View menu and RE+ Tools.
    std::string menus;
    HMENU bar = GetMenu(frameWindow);
    for (int i = 0; bar && i < GetMenuItemCount(bar); ++i)
        if (HMENU sub = GetSubMenu(bar, i); sub && GetMenuState(sub, 41560, MF_BYCOMMAND) != UINT(-1))
        {
            char text[64] = {};
            GetMenuStringA(bar, i, text, sizeof(text), MF_BYPOSITION);
            menus += std::string(menus.empty() ? "" : ", ") + text;
        }
    Check(menus.find("View") != std::string::npos && menus.find("RE+") != std::string::npos, "outliner-menus", menus);
    Exec("SELECT NONE");
    Pump(200);
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    Command(41560);
    Pump(500);
    QueryPerformanceCounter(&b);
    HWND outliner = OpenedWindow("Map Outliner");
    if (!outliner) { Fail("outliner-open", "did not open; boxes=" + std::to_string(boxes.size())); return; }
    HWND list = GetDlgItem(outliner, OutlinerIds::kList);
    const bool virtualList = (GetWindowLongA(list, GWL_STYLE) & LVS_OWNERDATA) != 0;
    int cameras = 0;
    for (auto actor : Actors()) cameras += ClassName(actor) == "Camera";
    const int actors = static_cast<int>(Actors().size()) - cameras;
    Info("outliner-open", "ms=" + Fixed(1000.0 * double(b.QuadPart - a.QuadPart) / double(f.QuadPart)) + " actors=" + std::to_string(actors)
         + " status '" + WindowText(GetDlgItem(outliner, OutlinerIds::kStatus)) + "'");

    // Grouped by nothing: one line per actor, the viewport cameras left out.
    GroupOutliner(outliner, 0);
    auto rows = ListRows(list);
    Check(virtualList && static_cast<int>(rows.size()) == actors, "outliner-lists-every-actor",
          std::to_string(rows.size()) + " rows for " + std::to_string(actors) + " actors (" + std::to_string(cameras) + " cameras) virtual=" + std::to_string(virtualList));
    for (size_t i = 0; i < rows.size() && i < 4; ++i) Info("outliner-row", rows[i]);

    // Grouped by class: a header per class with its count, the rows under it.
    GroupOutliner(outliner, 1);
    auto grouped = ListRows(list);
    size_t headers = 0;
    long long counted = 0;
    for (const auto& row : grouped)
        if (row.rfind("[-] ", 0) == 0) { ++headers; counted += Number(row.substr(row.rfind('('))); }
    Check(headers > 1 && static_cast<int>(grouped.size()) == actors + static_cast<int>(headers) && counted == actors, "outliner-group-by-class",
          std::to_string(headers) + " classes, " + std::to_string(counted) + " counted, " + std::to_string(grouped.size()) + " lines");
    // Collapsing a class (double-click its header) keeps the header and hides its rows.
    ClickRow(outliner, OutlinerIds::kList, 0, true);
    const int collapsed = ListCount(list);
    const auto firstHeader = ListRows(list).empty() ? std::string() : ListRows(list)[0];
    Check(firstHeader.rfind("[+] ", 0) == 0 && collapsed < static_cast<int>(grouped.size()), "outliner-collapse", firstHeader + " lines " + std::to_string(collapsed));
    ClickRow(outliner, OutlinerIds::kList, 0, true);
    Check(ListCount(list) == static_cast<int>(grouped.size()), "outliner-expand", std::to_string(ListCount(list)));
    GroupOutliner(outliner, 0);

    // Search: every word must appear in some column.
    auto meshes = FreeMeshes(6, 30);
    if (meshes.size() < 6) { Fail("outliner-meshes", "the map needs six free static meshes"); CloseWindowAndWait(outliner); return; }
    Search(outliner, "staticmeshactor");
    rows = ListRows(list);
    bool all = !rows.empty();
    for (const auto& row : rows) all = all && Lowered(row).find("staticmeshactor") != std::string::npos;
    Check(all && static_cast<int>(rows.size()) < actors, "outliner-search", std::to_string(rows.size()) + " rows");
    const std::string named = Name(meshes[0]);
    Search(outliner, "  StaticMeshActor   " + named + " ");
    rows = ListRows(list);
    all = RowNamed(rows, named) >= 0;
    for (const auto& row : rows) all = all && row.find(named) != std::string::npos;
    Check(all, "outliner-search-words-narrow", named + ": " + std::to_string(rows.size()) + " rows");
    Search(outliner, "");
    rows = ListRows(list);
    Check(static_cast<int>(rows.size()) == actors, "outliner-search-cleared", std::to_string(rows.size()));

    // A click on three rows selects those three actors in the editor.
    std::vector<int> three;
    for (int i = 0; i < 3; ++i) three.push_back(RowNamed(rows, Name(meshes[i])));
    SelectListRows(list, three);
    int on = 0;
    for (int i = 0; i < 3; ++i) on += (Flags(meshes[i]) & 0x40) != 0;
    Check(on == 3 && Selected() == 3, "outliner-click-selects", "selected " + std::to_string(Selected()) + " of them " + std::to_string(on)
          + " status '" + WindowText(GetDlgItem(outliner, OutlinerIds::kStatus)) + "'");

    // Follow selection: a mesh selected in the editor becomes the list's selection.
    Exec("SELECT NONE");
    SetSelected(meshes[3], true);
    Pump(800);
    auto chosen = ListSelection(list);
    Check(chosen.size() == 1 && chosen[0] == RowNamed(rows, Name(meshes[3])), "outliner-follows-selection", std::to_string(chosen.size()) + " row(s)");

    // Double-click frames.
    ClickRow(outliner, OutlinerIds::kList, RowNamed(rows, Name(meshes[4])), true);
    const auto framed = WindowText(GetDlgItem(outliner, OutlinerIds::kStatus));
    Check(framed.find("framed") != std::string::npos && (Flags(meshes[4]) & 0x40), "outliner-double-click-frames", framed);

    // Hide two, one Undo step; the rows say Hidden; Show brings them back.
    SelectListRows(list, {RowNamed(rows, Name(meshes[0])), RowNamed(rows, Name(meshes[1]))});
    auto before = Undo();
    Command(OutlinerIds::kHide, outliner);
    Pump(500);
    auto after = Undo();
    rows = ListRows(list);
    const int hiddenRow = RowNamed(rows, Name(meshes[0]));
    const bool marked = hiddenRow >= 0 && rows[hiddenRow].find("Hidden") != std::string::npos;
    Check((Flags(meshes[0]) & 0x10) && (Flags(meshes[1]) & 0x10) && after.count == before.count + 1 && marked, "outliner-hide",
          "undo '" + after.current + "' row '" + (hiddenRow >= 0 ? rows[hiddenRow] : std::string()) + "'");
    // A hidden row is never selected in the editor.
    SelectListRows(list, {RowNamed(rows, Name(meshes[0]))});
    Check(!(Flags(meshes[0]) & 0x40), "outliner-hidden-not-selected", WindowText(GetDlgItem(outliner, OutlinerIds::kStatus)));
    SelectListRows(list, {RowNamed(rows, Name(meshes[0])), RowNamed(rows, Name(meshes[1]))});
    before = Undo();
    Command(OutlinerIds::kShow, outliner);
    Pump(500);
    after = Undo();
    Check(!(Flags(meshes[0]) & 0x10) && !(Flags(meshes[1]) & 0x10) && after.count == before.count + 1, "outliner-show", "undo '" + after.current + "'");

    // Delete: answered No, nothing goes; through the list's Delete key.
    const std::string gone0 = Name(meshes[0]), gone1 = Name(meshes[1]);
    rows = ListRows(list);
    SelectListRows(list, {RowNamed(rows, gone0), RowNamed(rows, gone1)});
    const size_t actorsBefore = Actors().size();
    before = Undo();
    boxAnswer = IDNO;
    boxes.clear();
    {
        const UINT_PTR answering = SetTimer(nullptr, 0, 100, AnswerBoxes);
        NMLVKEYDOWN key{};
        key.hdr.hwndFrom = list;
        key.hdr.idFrom = OutlinerIds::kList;
        key.hdr.code = LVN_KEYDOWN;
        key.wVKey = VK_DELETE;
        SendMessageA(outliner, WM_NOTIFY, OutlinerIds::kList, reinterpret_cast<LPARAM>(&key));
        KillTimer(nullptr, answering);
    }
    boxAnswer = IDOK;
    Pump(300);
    Check(BoxContains("Delete these 2 actors?") && Actors().size() == actorsBefore && Undo().count == before.count, "outliner-delete-asks",
          "boxes=" + std::to_string(boxes.size()) + (boxes.empty() ? "" : " " + boxes[0]));
    // Answered Yes: both go in one Undo step, and Undo brings them back.
    boxAnswer = IDYES;
    Command(OutlinerIds::kDelete, outliner);
    boxAnswer = IDOK;
    Pump(500);
    after = Undo();
    rows = ListRows(list);
    Check(!ActorNamed(gone0) && !ActorNamed(gone1) && Actors().size() == actorsBefore - 2 && after.count == before.count + 1
              && RowNamed(rows, gone0) < 0 && static_cast<int>(rows.size()) == actors - 2,
          "outliner-delete-one-step", "undo '" + after.current + "' actors " + std::to_string(actorsBefore) + "->" + std::to_string(Actors().size())
          + " rows " + std::to_string(rows.size()) + " boxes=" + std::to_string(boxes.size()));
    Command(40019); // Edit > Undo
    Pump(800);
    rows = ListRows(list);
    Check(ActorNamed(gone0) && ActorNamed(gone1) && Actors().size() == actorsBefore && RowNamed(rows, gone0) >= 0, "outliner-delete-undo",
          "actors " + std::to_string(Actors().size()) + " rows " + std::to_string(rows.size()));

    // The level info cannot be deleted.
    GroupOutliner(outliner, 0);
    Search(outliner, "levelinfo");
    rows = ListRows(list);
    if (!rows.empty())
    {
        SelectListRows(list, {0});
        Command(OutlinerIds::kDelete, outliner);
        Pump(200);
        Check(boxes.empty() && WindowText(GetDlgItem(outliner, OutlinerIds::kStatus)).find("cannot be deleted") != std::string::npos, "outliner-keeps-level-info",
              WindowText(GetDlgItem(outliner, OutlinerIds::kStatus)));
    }
    Search(outliner, "");

    // Reading the whole map again is quick.
    const double ms = Command(OutlinerIds::kRefresh, outliner);
    Check(ms < 2000, "outliner-refresh-fast", "ms=" + Fixed(ms) + " for " + std::to_string(actors) + " actors");

    // It reopens where it was left.
    MONITORINFO main{sizeof(main)};
    GetMonitorInfoA(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &main);
    SetWindowPos(outliner, nullptr, main.rcWork.left + 97, main.rcWork.top + 61, 811, 533, SWP_NOZORDER | SWP_NOACTIVATE);
    Pump(150);
    const RECT moved = NormalRect(outliner);
    CloseWindowAndWait(outliner);
    Command(41560);
    Pump(300);
    HWND again = OpenedWindow("Map Outliner");
    Check(again && SameRect(NormalRect(again), moved), "outliner-placement", "moved " + RectText(moved) + " back " + (again ? RectText(NormalRect(again)) : std::string("none")));
    // shot=1 in -Extra: grouped by class and left open for a while, for a picture taken from outside.
    if (again && Ini("shot") == "1")
    {
        GroupOutliner(again, 1);
        Exec("SELECT NONE");
        for (int i = 2; i < 6; ++i) SetSelected(meshes[i], true);
        Info("outliner-shot", "window " + std::to_string(reinterpret_cast<uintptr_t>(again)));
        Pump(15000);
    }
    if (again) CloseWindowAndWait(again);
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
        else if (name == "outliner") OutlinerSuite();
        else Fail("suite", "no suite called " + name);
    }
}
