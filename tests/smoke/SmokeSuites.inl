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
        else if (name == "objexport") ObjExportSuite();
        else Fail("suite", "no suite called " + name);
    }
}
