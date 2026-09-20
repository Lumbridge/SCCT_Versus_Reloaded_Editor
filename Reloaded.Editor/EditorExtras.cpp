#include "pch.h"
#include "EditorExtras.h"
#include "WorkflowEditor.h"
#include "ReloadedOptions.h"
#include "GEKeybindSwap.h"
#include "logger.h"
#include <commctrl.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

using Workflow::Json;
using Workflow::Vector;
using Workflow::Rotation;
using Workflow::Pose;
namespace Editor = Workflow::Editor;

namespace
{
    HWND frameWindow = nullptr;
    HHOOK startupHook = nullptr;
    HMENU recentMenu = nullptr;
    HWND shortcutsWindow = nullptr;
    std::vector<std::string> recent;
    std::string lastMap;
    unsigned lastAutosaveRevision = 0, lastCleanRevision = 0;
    ULONGLONG lastAutosaveTick = 0;
    int autosaveSlot = 0;
    constexpr UINT_PTR kTimer = 0x5245;
    constexpr int kRecentMax = 10;
    constexpr UINT kFileSave = 40007, kFileSaveAs = 40008;

    std::filesystem::path RecentFile() { return Editor::Directory() / "recent_maps.json"; }
    void LoadRecent()
    {
        recent.clear();
        try
        {
            std::ifstream in(RecentFile());
            if (!in) return;
            Json list; in >> list;
            for (const auto& entry : list) if (entry.is_string() && recent.size() < kRecentMax) recent.push_back(entry.get<std::string>());
        }
        catch (const std::exception&) { recent.clear(); }
    }
    void SaveRecent()
    {
        try
        {
            std::error_code error;
            std::filesystem::create_directories(RecentFile().parent_path(), error);
            std::ofstream out(RecentFile());
            out << Json(recent).dump(2);
        }
        catch (const std::exception&) { /* The list is a convenience; a failed save is not worth a dialog. */ }
    }
    void RebuildRecentMenu()
    {
        if (!recentMenu) return;
        while (GetMenuItemCount(recentMenu) > 0) DeleteMenu(recentMenu, 0, MF_BYPOSITION);
        if (recent.empty()) AppendMenuA(recentMenu, MF_STRING | MF_GRAYED, 0, "(no maps opened yet)");
        for (size_t i = 0; i < recent.size() && i < kRecentMax; ++i)
        {
            const std::filesystem::path path(recent[i]);
            const std::string label = "&" + std::to_string((i + 1) % 10) + "  " + path.filename().string() + "\t" + path.parent_path().filename().string();
            AppendMenuA(recentMenu, MF_STRING, EditorExtras::kRecentFirst + static_cast<UINT>(i), label.c_str());
        }
    }
    void NoteMap(const std::string& map)
    {
        if (map.empty()) return;
        const auto folded = Workflow::Fold(map);
        recent.erase(std::remove_if(recent.begin(), recent.end(), [&](const std::string& entry) { return Workflow::Fold(entry) == folded; }), recent.end());
        recent.insert(recent.begin(), map);
        if (recent.size() > kRecentMax) recent.resize(kRecentMax);
        SaveRecent();
        RebuildRecentMenu();
    }

    // A short notice near the bottom-right of the frame that fades after a
    // few seconds: what an autosave did, without a dialog to dismiss.
    LRESULT CALLBACK NoticeProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        if (message == WM_PAINT)
        {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(window, &ps);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
            char text[512]{};
            GetWindowTextA(window, text, sizeof(text));
            RECT rect{};
            GetClientRect(window, &rect);
            DrawTextA(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            EndPaint(window, &ps);
            return 0;
        }
        if (message == WM_TIMER || message == WM_LBUTTONDOWN) { DestroyWindow(window); return 0; }
        return DefWindowProcA(window, message, w, l);
    }
    void Notice(const std::string& text)
    {
        static bool registered = false;
        if (!registered)
        {
            WNDCLASSA wc{};
            wc.lpfnWndProc = NoticeProc;
            wc.hInstance = GetModuleHandle(nullptr);
            wc.lpszClassName = "ReloadedNotice";
            wc.hbrBackground = CreateSolidBrush(RGB(40, 60, 80));
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            registered = RegisterClassA(&wc) != 0;
            if (!registered) return;
        }
        RECT frame{};
        GetWindowRect(frameWindow, &frame);
        const int width = static_cast<int>(text.size()) * 7 + 40, height = 34;
        HWND notice = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, "ReloadedNotice", text.c_str(), WS_POPUP,
                                      frame.right - width - 24, frame.bottom - height - 48, width, height, frameWindow, nullptr, GetModuleHandle(nullptr), nullptr);
        if (!notice) return;
        ShowWindow(notice, SW_SHOWNOACTIVATE);
        SetTimer(notice, 1, 4000, nullptr);
    }

    // Every few seconds on the frame's thread: notice a newly opened or saved
    // map for the recent list, and take an autosave copy when the map has
    // changed since the last one and the interval has passed.
    void Tick()
    {
        try
        {
            const auto map = Editor::MapFile();
            const auto revision = Editor::Revision();
            if (map != lastMap)
            {
                lastMap = map;
                lastCleanRevision = revision;
                lastAutosaveRevision = revision;
                lastAutosaveTick = GetTickCount64();
                NoteMap(map);
            }
            const int minutes = ReloadedAutosaveMinutes();
            if (minutes <= 0 || map.empty() || revision == lastAutosaveRevision) return;
            if (GetTickCount64() - lastAutosaveTick < static_cast<ULONGLONG>(minutes) * 60000ull) return;
            // Not while a dialog or menu is up or a drag holds the mouse: the
            // map may be mid-transaction, and a save would close the menu.
            if (!IsWindowEnabled(frameWindow) || GetCapture()) return;
            GUITHREADINFO gui{ sizeof(gui) };
            if (GetGUIThreadInfo(GetCurrentThreadId(), &gui) && (gui.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_INMOVESIZE | GUI_SYSTEMMENUMODE))) return;
            const auto path = EditorExtras::AutosaveNow();
            Notice("Autosaved a copy to " + std::filesystem::path(path).filename().string());
        }
        catch (const std::exception& e)
        {
            lastAutosaveTick = GetTickCount64(); // Try again after the interval, not every tick.
            Logger::log(std::string("Autosave: ") + e.what());
        }
    }

    LRESULT CALLBACK FrameProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR)
    {
        if (message == WM_TIMER && w == kTimer) { Tick(); return 0; }
        if (message == WM_INITMENUPOPUP && reinterpret_cast<HMENU>(w) == recentMenu) { RebuildRecentMenu(); return 0; }
        if (message == WM_COMMAND && (LOWORD(w) == kFileSave || LOWORD(w) == kFileSaveAs))
        {
            // After the stock save the map is as clean as it will get: opening
            // a recent map from here needs no warning.
            const LRESULT result = DefSubclassProc(window, message, w, l);
            try { lastCleanRevision = Editor::Revision(); } catch (const std::exception&) {}
            return result;
        }
        if (message == WM_NCDESTROY) { RemoveWindowSubclass(window, FrameProc, 1); frameWindow = nullptr; }
        return DefSubclassProc(window, message, w, l);
    }

    // Top-level submenu of the frame's bar that directly contains a command.
    HMENU MenuWithCommand(HMENU bar, UINT command)
    {
        for (int i = 0; i < GetMenuItemCount(bar); ++i)
            if (HMENU sub = GetSubMenu(bar, i))
                for (int j = 0; j < GetMenuItemCount(sub); ++j)
                    if (GetMenuItemID(sub, j) == command) return sub;
        return nullptr;
    }
    void InstallMenus()
    {
        HMENU bar = GetMenu(frameWindow);
        if (!bar) return;
        if (HMENU file = GetSubMenu(bar, 0); file && !recentMenu)
        {
            // After New and Open.
            recentMenu = CreatePopupMenu();
            InsertMenuA(file, 2, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(recentMenu), "Open &Recent");
            InsertMenuA(file, 3, MF_BYPOSITION | MF_STRING, EditorExtras::kAutosaveNow, "Save an Autosave Cop&y Now");
            RebuildRecentMenu();
        }
        if (HMENU build = MenuWithCommand(bar, 40038); build && GetMenuState(build, EditorExtras::kPlayFromCameraSpy, MF_BYCOMMAND) == UINT(-1))
        {
            AppendMenuA(build, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(build, MF_STRING, EditorExtras::kPlayFromCameraSpy, "Play From Camera as &Spy");
            AppendMenuA(build, MF_STRING, EditorExtras::kPlayFromCameraMerc, "Play From Camera as &Merc");
        }
        if (HMENU help = MenuWithCommand(bar, 40480); help && GetMenuState(help, EditorExtras::kShortcuts, MF_BYCOMMAND) == UINT(-1))
            AppendMenuA(help, MF_STRING, EditorExtras::kShortcuts, "Reloaded &Shortcuts...");
        DrawMenuBar(frameWindow);
    }
    void Install()
    {
        LoadRecent();
        InstallMenus();
        SetWindowSubclass(frameWindow, FrameProc, 1, 0);
        SetTimer(frameWindow, kTimer, 10000, nullptr);
    }
    LRESULT CALLBACK StartupHook(int code, WPARAM w, LPARAM l)
    {
        // Runs on the frame's thread. The first message brings everything up;
        // then the hook removes itself.
        HHOOK hook = startupHook;
        const LRESULT result = CallNextHookEx(hook, code, w, l);
        if (hook)
        {
            startupHook = nullptr;
            try { Install(); } catch (const std::exception& e) { Logger::log(std::string("Editor extras: ") + e.what()); }
            UnhookWindowsHookEx(hook);
        }
        return result;
    }

    // The shortcut legend: everything Reloaded adds to the editor's keys and
    // mouse, with the Geometric Event keys as currently configured.
    std::string ShortcutText()
    {
        auto key = [](uint8_t k) { return std::string("Shift+") + static_cast<char>(k); };
        return "EVERYWHERE IN THE EDITOR\r\n"
               "Ctrl + mouse wheel over a viewport: change the grid size (the plan's snapping follows).\r\n"
               "Ctrl+D in a viewport: duplicate the selection (Reloaded Options can drop the offset).\r\n"
               "J in a viewport: Game View, hiding editor icons and sprites the game does not draw.\r\n"
               "F12: Reloaded Options.   F7: disabled (the stock script compiler would crash).\r\n"
               "\r\n"
               "GEOMETRIC EVENTS (set in Reloaded Options)\r\n"
               + key(g_KeyLedgeGrab) + ": ledge grab   " + key(g_KeyHandOverHand) + ": hand-over-hand   " + key(g_KeyPipe) + ": pipe\r\n"
               + key(g_KeyLadder) + ": ladder   " + key(g_KeyZipline) + ": zip line   " + key(g_KeyFence) + ": fence\r\n"
               "\r\n"
               "RIGHT-CLICK MENUS\r\n"
               "Actor: Reloaded: Select (all of this class, all with this Tag, same static mesh, invert) and\r\n"
               "Reloaded: Visibility (hide selected, isolate selected, unhide all); Save Selection as Assembly;\r\n"
               "Edit SMagicEvent; Add SObjective / triggers on a mission or objective; position the builder brush around meshes.\r\n"
               "Brush face or vertices: snap to the grid per axis.   Texture / mesh browser: Favorites and Find Usages.\r\n"
               "\r\n"
               "MENUS\r\n"
               "File: Open Recent, Save an Autosave Copy Now (autosave copies land in Autosave beside the map).\r\n"
               "Build: Play From Camera as Spy / Merc starts a playtest at the perspective viewport's camera.\r\n"
               "View > Reloaded Tools: Map Design (its own Keys... window lists the plan's shortcuts), Brush Visibility,\r\n"
               "Gameplay Connections, SMagicEvent Workbench, SCamNetwork Manager, Working Views, Assemblies, JSON.\r\n";
    }
    LRESULT CALLBACK ShortcutsProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        if (message == WM_CREATE)
        {
            const auto text = ShortcutText();
            HWND edit = CreateWindowExA(0, "EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                        8, 8, 700, 420, window, reinterpret_cast<HMENU>(1), GetModuleHandle(nullptr), nullptr);
            SendMessage(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return 0;
        }
        if (message == WM_SIZE) { MoveWindow(GetDlgItem(window, 1), 8, 8, (std::max)(1, LOWORD(l) - 16), (std::max)(1, HIWORD(l) - 16), TRUE); return 0; }
        if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
        if (message == WM_NCDESTROY) shortcutsWindow = nullptr;
        return DefWindowProcA(window, message, w, l);
    }
    void ShowShortcuts()
    {
        if (!shortcutsWindow)
        {
            WNDCLASSA wc{};
            wc.hInstance = GetModuleHandle(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
            wc.lpfnWndProc = ShortcutsProc;
            wc.lpszClassName = "ReloadedShortcuts";
            RegisterClassA(&wc);
            shortcutsWindow = CreateWindowExA(WS_EX_TOOLWINDOW, wc.lpszClassName, "Reloaded Shortcuts", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                              CW_USEDEFAULT, CW_USEDEFAULT, 740, 470, frameWindow, nullptr, wc.hInstance, nullptr);
            if (!shortcutsWindow) throw std::runtime_error("Cannot open the shortcut legend.");
        }
        else SetWindowTextA(GetDlgItem(shortcutsWindow, 1), ShortcutText().c_str());
        ShowWindow(shortcutsWindow, SW_SHOWNORMAL);
        SetForegroundWindow(shortcutsWindow);
    }

    // Selection helpers built on the editor's own actor list, so they work
    // for several classes or tags at once.
    bool Camera(const Json& actor) { return actor.value("class", std::string()).find("Camera") != std::string::npos; }
    void SelectMatching(const char* field, const char* what)
    {
        const auto actors = Editor::Actors();
        std::set<std::string> wanted;
        for (const auto& actor : actors)
            if (actor.value("selected", false) && !Camera(actor) && actor.value("authorable", true))
            {
                const auto value = actor.value(field, std::string());
                if (!value.empty() && Workflow::Fold(value) != "none") wanted.insert(Workflow::Fold(value));
            }
        if (wanted.empty()) throw std::runtime_error(std::string("Select an actor with a ") + what + " first.");
        Json matching = Json::array();
        for (const auto& actor : actors)
            if (!Camera(actor) && actor.value("authorable", true) && wanted.count(Workflow::Fold(actor.value(field, std::string())))) matching.push_back(actor);
        Editor::Select(matching, false);
    }
    void Exec(const char* command, const char* failure)
    {
        if (!Editor::Exec(command)) throw std::runtime_error(failure);
        Editor::Redraw();
    }
    // The rest of the selection, and hiding through the same flags the Scene
    // panel sets, so the plan, the panel and the viewports agree.
    void InvertSelection()
    {
        Json others = Json::array();
        for (const auto& actor : Editor::Actors())
            if (!Camera(actor) && actor.value("authorable", true) && !actor.value("selected", false)) others.push_back(actor);
        Editor::Select(others, false);
    }
    void Hide(bool selectedOnes)
    {
        Json members = Json::array();
        for (const auto& actor : Editor::Actors())
            if (!Camera(actor) && actor.value("authorable", true) && actor.value("selected", false) == selectedOnes) members.push_back(actor);
        if (members.empty()) throw std::runtime_error(selectedOnes ? "Select something to hide first." : "Select what should stay visible first.");
        Editor::DesignSetFlags(members, 1, -1);
        Editor::Redraw();
    }
    void UnhideAll()
    {
        Json members = Json::array();
        for (const auto& actor : Editor::DesignScene())
            if (actor.value("hidden", false)) members.push_back(actor);
        if (members.empty()) throw std::runtime_error("Nothing is hidden.");
        Editor::DesignSetFlags(members, 0, -1);
        Editor::Redraw();
    }

    // A playtest from the perspective viewport's camera, as one team: from
    // that team's first start when it has one, else from a temporary start
    // that is removed again when Play Level returns.
    void PlayFromCamera(const std::string& team)
    {
        Pose pose;
        bool found = false;
        for (const auto& camera : Editor::CaptureView().at("cameras"))
        {
            const int mode = camera.at("mode");
            if (mode == 13 || mode == 14 || mode == 15) continue; // The orthographic views.
            pose = { camera.at("position").get<Vector>(), camera.at("rotation").get<Rotation>() };
            found = true;
            break;
        }
        if (!found) throw std::runtime_error("No perspective viewport to take the camera from.");
        pose.rotation[0] = 0; // A pawn stands level; only the camera's yaw carries over.
        pose.rotation[2] = 0;
        const auto starts = Editor::DesignSpawns();
        for (const auto& start : starts)
            if (start.at("team") == team) { Editor::DesignPlay(start, pose); return; }
        const std::string type = starts.empty() ? "Engine.PlayerStart" : starts.front().at("class").get<std::string>();
        const auto temporary = Editor::DesignTemporaryStart(type, team, pose);
        struct Remove
        {
            Json start;
            ~Remove() { try { Editor::DesignRemoveTemporaryStart(start); } catch (const std::exception&) {} }
        } remove{ temporary };
        Editor::DesignPlay(temporary, pose);
    }

    void OpenRecent(size_t index)
    {
        if (index >= recent.size()) return;
        const std::string path = recent[index];
        if (!std::filesystem::exists(path))
        {
            recent.erase(recent.begin() + index);
            SaveRecent();
            RebuildRecentMenu();
            throw std::runtime_error("That map is no longer there:\n" + path);
        }
        if (Editor::Revision() != lastCleanRevision
            && MessageBoxA(frameWindow, ("Open " + std::filesystem::path(path).filename().string() + "?\n\nUnsaved changes in the current map will be lost.").c_str(),
                           "Open Recent", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
        if (!Editor::Exec("MAP LOAD FILE=\"" + path + "\"")) throw std::runtime_error("The editor could not open\n" + path);
        Editor::SetMapFile(path);
        lastMap = path;
        lastCleanRevision = lastAutosaveRevision = Editor::Revision();
        lastAutosaveTick = GetTickCount64();
        NoteMap(path);
        Editor::Redraw();
    }
}

void EditorExtras::Attach(HWND frame)
{
    if (!frame || frameWindow) return;
    frameWindow = frame;
    startupHook = SetWindowsHookExA(WH_GETMESSAGE, StartupHook, nullptr, GetWindowThreadProcessId(frame, nullptr));
    if (startupHook) PostMessage(frame, WM_NULL, 0, 0); // Something for the hook to see.
    else Logger::log("Editor extras: could not reach the frame's thread");
}

std::string EditorExtras::AutosaveNow()
{
    const auto map = Editor::MapFile();
    if (map.empty()) throw std::runtime_error("The map has no file name yet: save it once first.");
    const std::filesystem::path source(map);
    const auto directory = source.parent_path() / "Autosave";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const int keep = (std::max)(1, ReloadedAutosaveKeep());
    autosaveSlot = autosaveSlot % keep + 1;
    const auto target = directory / (source.stem().string() + "_Autosave" + std::to_string(autosaveSlot) + source.extension().string());
    // The save must not make the editor think the map itself is saved: what
    // it clears on the level's package is put back.
    const auto words = Editor::PackageWords();
    if (!Editor::Exec("MAP SAVE FILE=\"" + target.string() + "\"")) throw std::runtime_error("The editor refused to write the autosave copy.");
    Editor::RestorePackageWords(words);
    lastAutosaveRevision = Editor::Revision();
    lastAutosaveTick = GetTickCount64();
    Logger::log("Autosaved " + target.string());
    return target.string();
}

void EditorExtras::AppendActorMenu(HMENU menu)
{
    HMENU select = CreatePopupMenu(), visibility = CreatePopupMenu();
    if (!select || !visibility) return;
    AppendMenuA(select, MF_STRING, kSelectSameClass, "All of this &class");
    AppendMenuA(select, MF_STRING, kSelectSameTag, "All with this &Tag");
    AppendMenuA(select, MF_STRING, kSelectSameMesh, "Same static &mesh");
    AppendMenuA(select, MF_STRING, kInvertSelection, "&Invert selection");
    AppendMenuA(visibility, MF_STRING, kHideSelected, "&Hide selected");
    AppendMenuA(visibility, MF_STRING, kIsolateSelected, "&Isolate selected (hide the rest)");
    AppendMenuA(visibility, MF_STRING, kUnhideAll, "&Unhide all");
    AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(select), "Reloaded: &Select");
    AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(visibility), "Reloaded: &Visibility");
}

bool EditorExtras::HandleCommand(UINT command)
{
    const bool ours = (command >= kSelectSameClass && command <= kAutosaveNow) || (command >= kRecentFirst && command <= kRecentLast);
    if (!ours) return false;
    try
    {
        if (command >= kRecentFirst && command <= kRecentLast) OpenRecent(command - kRecentFirst);
        else if (command == kSelectSameClass) SelectMatching("class", "class");
        else if (command == kSelectSameTag) SelectMatching("tag", "Tag");
        else if (command == kSelectSameMesh) Exec("ACTOR SELECT MATCHINGSTATICMESH", "Select a static mesh actor first.");
        else if (command == kInvertSelection) InvertSelection();
        else if (command == kHideSelected) Hide(true);
        else if (command == kIsolateSelected) Hide(false);
        else if (command == kUnhideAll) UnhideAll();
        else if (command == kPlayFromCameraSpy) PlayFromCamera("0");
        else if (command == kPlayFromCameraMerc) PlayFromCamera("1");
        else if (command == kShortcuts) ShowShortcuts();
        else if (command == kAutosaveNow) Notice("Autosaved a copy to " + std::filesystem::path(AutosaveNow()).filename().string());
    }
    catch (const std::exception& e)
    {
        MessageBoxA(GetActiveWindow(), e.what(), "Reloaded Editor", MB_OK | MB_ICONINFORMATION);
    }
    return true;
}
