// Headless job probe for the Emitter Library / vertex portal work.
// Derived from SCCT_Versus_Reloaded_Editor_mapjson/out/mapjson_v2_test/probe_continue.cpp (itself from
// SCCT_Versus_UE5_Bridge/tools/ue2build/ue2build_probe.cpp); native addresses are the ones those probes and
// tests/NativeMapRecoveryProbe.cpp + tests/WorkflowNativeTests.h already use against this editor build.
//
// Injected by Run-EmitterLibHeadless.ps1 (next to this file) into a disposable ChaosTheory_Editor.exe together with the
// Reloaded.Editor.dll under test. Runs ue2build_job.txt (next to this DLL) on the editor UI thread, one step per line,
// and writes ue2build_report.txt. Differences from probe_continue.cpp:
//   * a failed step is counted and the last line is "FAIL <n> step(s) failed" (probe_continue always ends in PASS);
//   * "wait <ms>" returns to the editor's own main loop (engine Tick + message pump) and resumes from a thread timer,
//     so a live preview driven by the engine or by timers actually advances between screenshots;
//   * window verbs for tool windows (title | control id | ...), PrintWindow screenshots, menu checks, vertex selection.
//
// Verbs (arguments containing a window title are separated by '|'):
//   new | load <file.sdc> | import <file.t3d> | exec <command> | rebuild | export <file.t3d> | save <MapsEd\X.sdc>
//   workflow[@name] <json>             ReloadedWorkflowRequest; {"$from":"name"[,"pointer":"/x"]} reuses a result
//   dump <name> <file>                 write a kept result as JSON
//   expect <name> | <json pointer> | <op> | <json>   op: == != >= <= > < ; array/object vs number compares its size
//   cmd <id>                           SendMessage(frame, WM_COMMAND, id) - menu command through Reloaded's dispatcher
//   menuhas <resource> | <id> | enabled|grayed|absent   native menu resource via the hooked LoadMenuA IAT slot
//   framemenu <id> | enabled|grayed|absent          the editor frame's menu bar (RE+ Tools items, UI.cpp)
//   window <title>                     a top-level window of the UI thread with exactly this title exists
//   count <title> | <ctrl> | <op> | <n>             ListBox/ComboBox/ListView/TreeView item count
//   pick <title> | <ctrl> | <index or text:Label>   select an item and notify the parent as a user click would
//   click <title> | <ctrl> [| <form name>]          BN_CLICKED; a Reloaded form or message box it opens is answered
//   text <title> | <ctrl>              record a control's text
//   shot <title> | <file.bmp> [| <ctrl>]            PrintWindow(PW_RENDERFULLCONTENT) of the window; with <ctrl>, the
//                                      control's rectangle inside the image is recorded ("shot_rect x y w h") for cropping
//   close <title>                      WM_CLOSE
//   wait <ms>                          yield to the editor main loop for <ms>, then continue with the next line
//   pump <ms>                          run a local message loop for <ms> (timers/paint only, no engine Tick)
//   vertexselect <json {"actor":identity,"polygon":n}>   Vertex Editing mode + native vertex selection of one polygon
//   vertexclear                        restore the vertex selection/mode saved by vertexselect (do this before saving)
//   brush@name <json {"actor":identity}>   polygon count, flags and local vertices of a brush, kept as a result
// Added for the effect pack tests (probe_emitterpacks.cpp, a copy of the el_core probe):
//   every step records "took <ms>"; the time of the last acting step is kept as the result "_ms"
//   (expect/dump/mem/text/tree/filejson do not replace it), so "expect _ms | / | < | 500" checks it
//   mem@name                           {"privateMB","workingSetMB","peakWorkingSetMB","addressSpaceMB"} of the editor
//   mkdir <dir> | touch <file> | filejson@name <file>   create a folder, bump a file's time, keep a JSON file
//   lock <file> | unlock <file>          hold a file open with no sharing, then close it
//   texthas|textlacks <title> | <ctrl> | <text>   a control's text contains (or not) the text
//   settext <title> | <ctrl> | <text>  set a control's text (an edit box notifies EN_CHANGE as typing does)
//   tree@name <title> | <ctrl>         the tree as {"items":[{"label","expanded","children"}],"selected","count"}
//   key <title> | <ctrl> | <vk>        TVN_KEYDOWN to the tree's parent; message boxes it opens are answered
//   expand <title> | <ctrl> | text:<label> | expand|collapse   TVM_EXPAND on a tree item (a first expand notifies)
//   pick/expand tree labels may be a path "text:Top > Group > Item" (groups on the way are opened)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <share.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#include "nlohmann/json.hpp"

namespace {
using Json = nlohmann::json;
HHOOK hook;
HWND frameWindow;
std::filesystem::path directory;
FILE* report;
constexpr UINT kRun = WM_APP + 0x31A;
constexpr uintptr_t kEditor = 0x1165DFA0;            // GEditor (UUnrealEdEngine*)
constexpr uintptr_t kWarn = 0x115BEFB0;              // GWarn output device
constexpr uintptr_t kVertexSelection = 0x11685a9c;   // native vertex-edit selection TArray {data,count,max}
constexpr uintptr_t kLoadMenuSlot = 0x11af23f0;      // LoadMenuA IAT slot (Reloaded's LoadMenuHook is installed here)
std::vector<std::string> jobLines;
size_t nextLine = 0;
int failures = 0;
std::map<std::string, Json> results;
std::string formName = "Probe entry";

void Record(const char* stage, const std::string& detail = std::string()) {
    if (!report) return;
    fprintf(report, "%s %s\n", stage, detail.c_str());
    fflush(report);
}
unsigned char* Editor() { return *reinterpret_cast<unsigned char**>(kEditor); }
const char* NameText(int index) {
    auto names = *reinterpret_cast<unsigned char***>(0x1169CFBC);
    int count = *reinterpret_cast<int*>(0x1169CFC0);
    return names && index >= 0 && index < count && names[index] ? reinterpret_cast<const char*>(names[index] + 12) : "<invalid>";
}
const char* ObjectName(void* object) { return object ? NameText(*reinterpret_cast<int*>(static_cast<unsigned char*>(object) + 0x20)) : "<null>"; }

int Exec(const std::string& command) {
    Record("exec", command.substr(0, command.find_first_of("\r\n")));
    auto editor = Editor();
    void* output = *reinterpret_cast<void**>(kWarn);
    if (!editor || !output) return 0;
    void* exec = editor + 0x28;
    using Fn = int(__thiscall*)(void*, const char*, void*);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(exec))[0])(exec, command.c_str(), output);
}

bool Rebuild() {   // as probe_continue.cpp / NativeMapRecoveryProbe.cpp Rebuild()
    struct Array { void* data{}; int count{}; int capacity{}; } first, second;
    void* editor = Editor();
    using Bracket = void(__thiscall*)(void*, void*, void*);
    reinterpret_cast<Bracket>(0x10E06A1A)(editor, &first, &second);
    bool ok = Exec("MAP REBUILD") && Exec("BSP REBUILD") && Exec("LIGHT APPLY");
    using Paths = void(__thiscall*)(void*);
    if (ok) { Record("rebuild_paths"); reinterpret_cast<Paths>(0x10E06399)(editor); }
    reinterpret_cast<Bracket>(0x10E02EEC)(editor, &first, &second);
    auto context = *reinterpret_cast<unsigned char**>(0x11691D7C);
    using CacheMeshLighting = void(__thiscall*)(void*, int);
    if (ok) reinterpret_cast<CacheMeshLighting>(0x10E06605)(editor, *reinterpret_cast<int*>(context + 0x78));
    return ok;
}

bool Import(const std::string& t3d) {   // as probe_continue.cpp Import()
    if (!Exec("MAP NEW") || !Exec("MAP IMPORT FILE=\"" + t3d + "\"")) return false;
    auto editor = Editor();
    auto level = *reinterpret_cast<unsigned char**>(editor + 0x130);
    using Finalize = void(__thiscall*)(void*, void*);
    reinterpret_cast<Finalize>((*reinterpret_cast<void***>(editor))[0xE0 / 4])(editor, level);
    auto activeActors = *reinterpret_cast<unsigned char***>(level + 0x2C);
    int activeCount = *reinterpret_cast<int*>(level + 0x30);
    *reinterpret_cast<int*>(level + 0x40) = 0;
    *reinterpret_cast<int*>(level + 0x50) = 0;
    using AddUnique = int(__thiscall*)(void*, void*);
    for (int i = 0; i < activeCount; ++i) if (activeActors[i]) {
        auto actor = activeActors[i];
        if (actor[0x2D0] != 2) reinterpret_cast<AddUnique>(0x10E025CD)(level + 0x3C, &actor);
        if (actor[0x2D0] != 1) reinterpret_cast<AddUnique>(0x10E025CD)(level + 0x4C, &actor);
    }
    Record("imported", "actors=" + std::to_string(activeCount));
    return true;
}

Json Resolve(const Json& value) {
    if (value.is_object()) {
        if (value.contains("$from")) {
            const auto& found = results.at(value.at("$from").get<std::string>());
            return value.contains("pointer") ? found.at(Json::json_pointer(value.at("pointer").get<std::string>())) : found;
        }
        Json out = Json::object();
        for (auto it = value.begin(); it != value.end(); ++it) out[it.key()] = Resolve(it.value());
        return out;
    }
    if (value.is_array()) {
        Json out = Json::array();
        for (const auto& item : value) out.push_back(Resolve(item));
        return out;
    }
    return value;
}

bool Workflow(const std::string& name, const std::string& text, std::string& detail) {
    char dll[MAX_PATH] = {};
    GetPrivateProfileStringA("job", "editor_dll", "Reloaded.Editor.dll", dll, MAX_PATH, (directory / "ue2build.ini").string().c_str());
    auto module = GetModuleHandleA(std::filesystem::path(dll).filename().string().c_str());
    using Request = int(__cdecl*)(const char*, char*, unsigned);
    auto request = module ? reinterpret_cast<Request>(GetProcAddress(module, "ReloadedWorkflowRequest")) : nullptr;
    if (!request && module) request = reinterpret_cast<Request>(GetProcAddress(module, "_ReloadedWorkflowRequest"));
    if (!request) { detail = "ReloadedWorkflowRequest not found"; return false; }
    const auto query = Resolve(Json::parse(text)).dump();
    std::vector<char> buffer(16 * 1024 * 1024);
    int ok = request(query.c_str(), buffer.data(), static_cast<unsigned>(buffer.size()));
    if (ok < 0) { buffer.resize(static_cast<size_t>(-ok)); ok = request(query.c_str(), buffer.data(), static_cast<unsigned>(buffer.size())); }
    detail = buffer.data();
    if (ok == 1 && !name.empty()) results[name] = Json::parse(detail).at("result");
    return ok == 1;
}

// ---- keep every top-level window of the UI thread off screen (copied from probe_continue.cpp) ----
HHOOK cbtHook = nullptr;
LRESULT CALLBACK OffScreen(int code, WPARAM window, LPARAM parameter) {
    if (code == HCBT_CREATEWND) {
        auto create = reinterpret_cast<CBT_CREATEWND*>(parameter)->lpcs;
        if (!(create->style & WS_CHILD)) { create->x = -30000; create->y = -30000; }
    }
    return CallNextHookEx(cbtHook, code, window, parameter);
}
void CALLBACK OnShowOrMove(HWINEVENTHOOK, DWORD, HWND w, LONG object, LONG child, DWORD, DWORD) {
    if (object != OBJID_WINDOW || child != CHILDID_SELF || !w || GetAncestor(w, GA_ROOT) != w) return;
    RECT rect{};
    if (GetWindowRect(w, &rect) && rect.right > -20000)
        SetWindowPos(w, nullptr, -30000, -30000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
void HideAll() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&OnShowOrMove), &self);
    SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_LOCATIONCHANGE, self, OnShowOrMove, GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_INCONTEXT);
    cbtHook = SetWindowsHookExW(WH_CBT, OffScreen, nullptr, GetCurrentThreadId());
    EnumThreadWindows(GetCurrentThreadId(), [](HWND w, LPARAM) -> BOOL {
        if (!(GetWindowLongW(w, GWL_STYLE) & WS_CHILD)) SetWindowPos(w, nullptr, -30000, -30000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return TRUE;
    }, 0);
}

// ---- window helpers ----
std::string Trim(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}
std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        const size_t bar = s.find('|', start);
        parts.push_back(Trim(s.substr(start, bar == std::string::npos ? std::string::npos : bar - start)));
        if (bar == std::string::npos) return parts;
        start = bar + 1;
    }
}
std::string TextOf(HWND window) {
    const int length = GetWindowTextLengthA(window);
    std::string text(static_cast<size_t>(length) + 1, '\0');
    GetWindowTextA(window, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}
std::string ClassOf(HWND window) { char name[128] = {}; GetClassNameA(window, name, sizeof(name)); return name; }
HWND FindTop(const std::string& title) {
    struct Search { const std::string* title; HWND found; } search{&title, nullptr};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND w, LPARAM p) -> BOOL {
        auto s = reinterpret_cast<Search*>(p);
        if (TextOf(w) == *s->title) { s->found = w; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
HWND Need(const std::string& title) { HWND w = FindTop(title); if (!w) throw std::runtime_error("no window titled \"" + title + "\""); return w; }
HWND NeedControl(HWND top, const std::string& id) {
    // GetDlgItem only searches direct children; controls may sit on a child panel, so search all descendants.
    struct Search { int id; HWND found; } search{std::stoi(id), nullptr};
    EnumChildWindows(top, [](HWND c, LPARAM p) -> BOOL {
        auto s = reinterpret_cast<Search*>(p);
        if (GetDlgCtrlID(c) == s->id) { s->found = c; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    if (!search.found) throw std::runtime_error("no control " + id);
    return search.found;
}
bool Compare(double a, const std::string& op, double b) {
    if (op == "==") return a == b;
    if (op == "!=") return a != b;
    if (op == ">=") return a >= b;
    if (op == "<=") return a <= b;
    if (op == ">") return a > b;
    if (op == "<") return a < b;
    throw std::runtime_error("unknown comparison " + op);
}
int CountItems(HWND c) {
    const auto cls = ClassOf(c);
    if (!_stricmp(cls.c_str(), "ListBox")) return static_cast<int>(SendMessageA(c, LB_GETCOUNT, 0, 0));
    if (!_stricmp(cls.c_str(), "ComboBox")) return static_cast<int>(SendMessageA(c, CB_GETCOUNT, 0, 0));
    if (!_stricmp(cls.c_str(), WC_LISTVIEWA)) return static_cast<int>(SendMessageA(c, LVM_GETITEMCOUNT, 0, 0));
    if (!_stricmp(cls.c_str(), WC_TREEVIEWA)) return static_cast<int>(SendMessageA(c, TVM_GETCOUNT, 0, 0));
    throw std::runtime_error("cannot count items of a " + cls);
}
std::string TreeText(HWND tree, HTREEITEM item) {
    char label[512] = {};
    TVITEMA value{}; value.mask = TVIF_TEXT; value.hItem = item; value.pszText = label; value.cchTextMax = sizeof(label);
    SendMessageA(tree, TVM_GETITEMA, 0, reinterpret_cast<LPARAM>(&value));
    return label;
}
HTREEITEM TreeFind(HWND tree, HTREEITEM item, const std::string& text, int& index) {   // depth-first, all items
    for (; item; item = TreeView_GetNextSibling(tree, item)) {
        if ((text.empty() && index-- == 0) || (!text.empty() && TreeText(tree, item) == text)) return item;
        if (auto child = TreeFind(tree, TreeView_GetChild(tree, item), text, index)) return child;
    }
    return nullptr;
}
// "A > B > C": the item C under B under the top-level item A, opening A and B on the way (a first
// TVM_EXPAND notifies TVN_ITEMEXPANDING, so a tree that fills a group when it opens does so).
HTREEITEM TreePath(HWND tree, const std::string& path) {
    HTREEITEM item = TreeView_GetRoot(tree), found = nullptr;
    size_t start = 0;
    for (;;) {
        const size_t split = path.find(" > ", start);
        const std::string label = path.substr(start, split == std::string::npos ? std::string::npos : split - start);
        for (found = nullptr; item; item = TreeView_GetNextSibling(tree, item)) if (TreeText(tree, item) == label) { found = item; break; }
        if (!found || split == std::string::npos) return found;
        TreeView_Expand(tree, found, TVE_EXPAND);
        item = TreeView_GetChild(tree, found);
        start = split + 3;
    }
}
bool Pick(HWND c, const std::string& which, std::string& detail) {
    const auto cls = ClassOf(c);
    const bool byText = which.rfind("text:", 0) == 0;
    const std::string text = byText ? which.substr(5) : std::string();
    int index = byText ? -1 : std::stoi(which);
    HWND parent = GetParent(c);
    const int id = GetDlgCtrlID(c);
    if (!_stricmp(cls.c_str(), "ListBox")) {
        if (byText) index = static_cast<int>(SendMessageA(c, LB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(text.c_str())));
        if (index < 0 || SendMessageA(c, LB_SETCURSEL, index, 0) == LB_ERR) { detail = "no such list item"; return false; }
        SendMessageA(parent, WM_COMMAND, MAKEWPARAM(id, LBN_SELCHANGE), reinterpret_cast<LPARAM>(c));   // LB_SETCURSEL does not notify
        return true;
    }
    if (!_stricmp(cls.c_str(), "ComboBox")) {
        if (byText) index = static_cast<int>(SendMessageA(c, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(text.c_str())));
        if (index < 0 || SendMessageA(c, CB_SETCURSEL, index, 0) == CB_ERR) { detail = "no such combo item"; return false; }
        SendMessageA(parent, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(c));
        return true;
    }
    if (!_stricmp(cls.c_str(), WC_LISTVIEWA)) {
        if (byText) { LVFINDINFOA find{}; find.flags = LVFI_STRING; find.psz = text.c_str(); index = static_cast<int>(SendMessageA(c, LVM_FINDITEMA, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(&find))); }
        if (index < 0 || index >= ListView_GetItemCount(c)) { detail = "no such list-view row"; return false; }
        ListView_SetItemState(c, -1, 0, LVIS_SELECTED);
        ListView_SetItemState(c, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);   // sends LVN_ITEMCHANGED
        ListView_EnsureVisible(c, index, FALSE);
        return true;
    }
    if (!_stricmp(cls.c_str(), WC_TREEVIEWA)) {
        auto item = text.find(" > ") != std::string::npos ? TreePath(c, text) : TreeFind(c, TreeView_GetRoot(c), text, index);
        if (!item) { detail = "no such tree item"; return false; }
        TreeView_EnsureVisible(c, item);
        return TreeView_SelectItem(c, item) != FALSE;   // sends TVN_SELCHANGED
    }
    detail = "cannot pick in a " + cls;
    return false;
}

Json TreeItems(HWND tree, HTREEITEM item) {
    Json items = Json::array();
    for (; item; item = TreeView_GetNextSibling(tree, item)) {
        const bool expanded = (TreeView_GetItemState(tree, item, TVIS_EXPANDED) & TVIS_EXPANDED) != 0;
        Json node{{"label", TreeText(tree, item)}, {"expanded", expanded}};
        if (auto child = TreeView_GetChild(tree, item)) node["children"] = TreeItems(tree, child);
        items.push_back(std::move(node));
    }
    return items;
}
Json Memory() {
    PROCESS_MEMORY_COUNTERS_EX counters{}; counters.cb = sizeof(counters);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    MEMORYSTATUSEX status{}; status.dwLength = sizeof(status); GlobalMemoryStatusEx(&status);
    const double mb = 1024.0 * 1024.0;
    return {{"privateMB", counters.PrivateUsage / mb}, {"workingSetMB", counters.WorkingSetSize / mb}, {"peakWorkingSetMB", counters.PeakWorkingSetSize / mb},
            {"addressSpaceMB", (status.ullTotalVirtual - status.ullAvailVirtual) / mb}};
}

// Answers what a click opens: a Reloaded name form (edit 1000 + OK, as WorkflowProbe::Answer does) or a message box.
void CALLBACK AnswerForms(HWND, UINT, UINT_PTR, DWORD) {
    EnumThreadWindows(GetCurrentThreadId(), [](HWND w, LPARAM) -> BOOL {
        const auto cls = ClassOf(w);
        if (cls == "ReloadedWorkflowForm") {
            SetWindowTextA(GetDlgItem(w, 1000), formName.c_str());
            Record("answered_form", TextOf(w));
            SendMessageA(w, WM_COMMAND, IDOK, 0);
        } else if (cls == "#32770" && IsWindowVisible(w) && (GetDlgItem(w, IDOK) || GetDlgItem(w, IDCANCEL))) {
            // A message box with only OK may give that button IDCANCEL; either closes it.
            HWND text = GetDlgItem(w, 0xFFFF);
            Record("answered_dialog", TextOf(w) + ": " + (text ? TextOf(text) : std::string()));
            PostMessageA(w, WM_COMMAND, GetDlgItem(w, IDOK) ? IDOK : IDCANCEL, 0);
        }
        return TRUE;
    }, 0);
}

void Screenshot(HWND window, const std::filesystem::path& path) {   // WorkflowProbe::Screenshot in tests/WorkflowNativeTests.h
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    RECT r{}; GetWindowRect(window, &r);
    const int width = r.right - r.left, height = r.bottom - r.top;
    HDC screen = GetDC(window), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    auto old = SelectObject(dc, bitmap);
    PrintWindow(window, dc, 2);   // PW_RENDERFULLCONTENT
    SelectObject(dc, old);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    std::vector<char> pixels(static_cast<size_t>(width) * height * 4);
    GetDIBits(dc, bitmap, 0, height, pixels.data(), &info, DIB_RGB_COLORS);
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<char*>(&header), sizeof(header));
    out.write(reinterpret_cast<char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    out.write(pixels.data(), static_cast<std::streamsize>(pixels.size()));
    DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window, screen);
}

void Pump(DWORD milliseconds) {
    const ULONGLONG end = GetTickCount64() + milliseconds;
    MSG message{};
    while (GetTickCount64() < end) {
        while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageA(&message); }
        Sleep(5);
    }
}

// ---- vertex selection (layout as used by tests/WorkflowNativeTests.h portal checks) ----
std::vector<std::array<uintptr_t, 3>> vertexEntries;   // stays alive while the native selection points at it
std::array<uintptr_t, 3> savedVertexHeader{};
int savedMode = -1;
unsigned char* ActorByIdentity(const Json& identity) {
    const auto path = identity.at("path").get<std::string>();
    const auto name = path.substr(path.find_last_of('.') + 1);
    auto level = *reinterpret_cast<unsigned char**>(Editor() + 0x130);
    auto data = *reinterpret_cast<unsigned char***>(level + 0x2C);
    const int count = *reinterpret_cast<int*>(level + 0x30);
    for (int i = 0; i < count; ++i) if (data[i] && name == ObjectName(data[i])) return data[i];
    throw std::runtime_error("actor " + path + " not in the level");
}
bool VertexSelect(const Json& spec, std::string& detail) {
    auto actor = ActorByIdentity(spec.at("actor"));
    auto model = *reinterpret_cast<unsigned char**>(actor + 0x238);
    auto polys = model ? *reinterpret_cast<unsigned char**>(model + 0x50) : nullptr;
    if (!polys) { detail = "actor has no brush polygons"; return false; }
    const int pi = spec.at("polygon").get<int>();
    if (pi < 0 || pi >= *reinterpret_cast<int*>(polys + 0x2c)) { detail = "polygon index out of range"; return false; }
    auto poly = *reinterpret_cast<unsigned char**>(polys + 0x28) + pi * 0x14c;
    const int n = *reinterpret_cast<unsigned short*>(poly + 0x148);
    if (savedMode < 0) {
        auto header = reinterpret_cast<uintptr_t*>(kVertexSelection);
        savedVertexHeader = {header[0], header[1], header[2]};
        savedMode = *reinterpret_cast<int*>(Editor() + 0x1ac);
    }
    vertexEntries.clear();
    for (int vi = 0; vi < n; ++vi) vertexEntries.push_back({reinterpret_cast<uintptr_t>(actor), static_cast<uintptr_t>(pi), static_cast<uintptr_t>(vi)});
    *reinterpret_cast<int*>(Editor() + 0x1ac) = 0x19;   // vertex editing mode
    auto header = reinterpret_cast<uintptr_t*>(kVertexSelection);
    header[0] = reinterpret_cast<uintptr_t>(vertexEntries.data());
    header[1] = header[2] = vertexEntries.size();
    detail = std::to_string(n) + " vertices of polygon " + std::to_string(pi);
    return true;
}
void VertexClear() {
    if (savedMode < 0) return;
    auto header = reinterpret_cast<uintptr_t*>(kVertexSelection);
    header[0] = savedVertexHeader[0]; header[1] = savedVertexHeader[1]; header[2] = savedVertexHeader[2];
    *reinterpret_cast<int*>(Editor() + 0x1ac) = savedMode;
    savedMode = -1;
}
Json BrushInfo(const Json& spec) {
    auto actor = ActorByIdentity(spec.at("actor"));
    auto model = *reinterpret_cast<unsigned char**>(actor + 0x238);
    auto polys = model ? *reinterpret_cast<unsigned char**>(model + 0x50) : nullptr;
    Json out{{"actorPolyFlags", *reinterpret_cast<unsigned*>(actor + 0x344)}, {"polygons", Json::array()}};
    if (!polys) return out;
    auto data = *reinterpret_cast<unsigned char**>(polys + 0x28);
    for (int pi = 0; pi < *reinterpret_cast<int*>(polys + 0x2c); ++pi) {
        auto poly = data + pi * 0x14c;
        Json vertices = Json::array();
        for (int vi = 0; vi < *reinterpret_cast<unsigned short*>(poly + 0x148); ++vi) {
            auto v = reinterpret_cast<float*>(poly + 0x18 + vi * 12);
            vertices.push_back({v[0], v[1], v[2]});
        }
        out["polygons"].push_back({{"flags", *reinterpret_cast<unsigned*>(poly + 0x140)}, {"vertices", vertices}});
    }
    return out;
}

bool Step(const std::string& verb, const std::string& name, const std::string& arg, std::string& detail, DWORD& waitMs) {
    try {
        if (verb == "import") return Import(arg);
        if (verb == "new") return Exec("MAP NEW") != 0;
        if (verb == "load") return Exec("MAP LOAD FILE=\"" + arg + "\"") != 0;
        if (verb == "exec") return Exec(arg) != 0;
        if (verb == "rebuild") return Rebuild();
        if (verb == "export") return Exec("MAP EXPORT FILE=\"" + arg + "\"") != 0;
        if (verb == "save") { using Save = int(__thiscall*)(void*, const char*); return reinterpret_cast<Save>(0x10E0416B)(Editor(), arg.c_str()) != 0; }
        if (verb == "workflow") {
            const bool ok = Workflow(name, arg, detail);
            Record(ok ? "workflow_ok" : "workflow_error", detail.substr(0, 4000));
            return ok;
        }
        if (verb == "reject") {   // reject <json> : passes when the request fails (its error is recorded)
            const bool ok = Workflow(std::string(), arg, detail);
            Record(ok ? "workflow_ok" : "workflow_rejected", detail.substr(0, 4000));
            return !ok;
        }
        if (verb == "set") {   // set <name> | <json pointer> | <json> : change a kept result before passing it on
            auto p = Split(arg);
            results.at(p.at(0))[Json::json_pointer(p.at(1))] = Resolve(Json::parse(p.at(2)));
            return true;
        }
        if (verb == "dump") { const auto split = arg.find(' '); std::ofstream(arg.substr(split + 1)) << results.at(arg.substr(0, split)).dump(2); return true; }
        if (verb == "expect") {
            auto p = Split(arg);   // name | pointer | op | json
            const Json& root = results.at(p.at(0));
            const Json& actual = p.at(1).empty() || p.at(1) == "/" ? root : root.at(Json::json_pointer(p.at(1)));
            const Json wanted = Resolve(Json::parse(p.at(3)));   // {"$from":..} compares with another kept result
            detail = "actual " + actual.dump().substr(0, 400);
            if ((actual.is_array() || actual.is_object()) && wanted.is_number()) return Compare(static_cast<double>(actual.size()), p.at(2), wanted.get<double>());
            if (actual.is_number() && wanted.is_number()) return Compare(actual.get<double>(), p.at(2), wanted.get<double>());
            if (p.at(2) == "has" || p.at(2) == "lacks") {   // substring of a string value (or of the JSON text of anything else)
                const auto haystack = actual.is_string() ? actual.get<std::string>() : actual.dump();
                const auto needle = wanted.is_string() ? wanted.get<std::string>() : wanted.dump();
                return (haystack.find(needle) != std::string::npos) == (p.at(2) == "has");
            }
            if (p.at(2) == "==") return actual == wanted;
            if (p.at(2) == "!=") return actual != wanted;
            detail += " (ordering needs numbers)";
            return false;
        }
        if (verb == "recoveractors") {   // recoveractors <cooked map> | <MapsEd destination> : MapRecovery exports the cooked level's actors (Recovery\<stem>\Actors.t3d)
            auto p = Split(arg);
            const std::filesystem::path destination(p.at(1));
            const auto scratch = destination.parent_path() / "Recovery" / destination.stem();
            // A directory where Geometry.t3d goes stops recovery after the actor export and before any package is
            // written, as the native harness's -ExpectRecoveryFailure does.
            std::filesystem::create_directories(scratch / "Geometry.t3d");
            using Recover = int(__cdecl*)(const char*, const char*, char*, unsigned);
            auto recover = reinterpret_cast<Recover>(GetProcAddress(GetModuleHandleA("Reloaded.Editor.dll"), "ReloadedRecoverMapToSource"));
            if (!recover) { detail = "ReloadedRecoverMapToSource is not exported"; return false; }
            char error[2048]{};
            const int ok = recover(p.at(0).c_str(), destination.string().c_str(), error, sizeof(error));
            detail = std::string(ok ? "recovered " : "stopped: ") + error;
            return std::filesystem::is_regular_file(scratch / "Actors.t3d");
        }
        if (verb == "copyfile") {   // copyfile <from> | <to>
            auto p = Split(arg);
            std::error_code failed;
            std::filesystem::copy_file(p.at(0), p.at(1), std::filesystem::copy_options::overwrite_existing, failed);
            detail = failed ? failed.message() : "copied";
            return !failed;
        }
        if (verb == "cmd") { SendMessageA(frameWindow, WM_COMMAND, static_cast<WPARAM>(std::stoul(arg)), 0); return true; }
        if (verb == "cmdform") {   // cmdform <id> | <form name> : a menu command whose Reloaded forms and message boxes are answered
            auto p = Split(arg);
            if (p.size() > 1) formName = p.at(1);
            const UINT_PTR timer = SetTimer(nullptr, 0, 50, AnswerForms);
            SendMessageA(frameWindow, WM_COMMAND, static_cast<WPARAM>(std::stoul(p.at(0))), 0);
            KillTimer(nullptr, timer);
            return true;
        }
        if (verb == "filehas" || verb == "filelacks") {   // filehas <file> | <text>
            auto p = Split(arg);
            std::ifstream in(p.at(0), std::ios::binary);
            if (!in) { detail = "cannot read " + p.at(0); return false; }
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            // MAP EXPORT writes a diagnostic section-sign (n) after names; compare without them.
            for (size_t at = text.find('\xA7'); at != std::string::npos; at = text.find('\xA7', at)) {
                size_t end = at + 1;
                if (end < text.size() && text[end] == '(') { while (++end < text.size() && isdigit(static_cast<unsigned char>(text[end]))); if (end < text.size() && text[end] == ')') { text.erase(at, end - at + 1); continue; } }
                ++at;
            }
            const bool found = text.find(p.at(1)) != std::string::npos;
            detail = found ? "found" : "not found";
            return found == (verb == "filehas");
        }
        if (verb == "menuhas") {
            auto p = Split(arg);   // resource | id | enabled|grayed|absent
            using Load = HMENU(WINAPI*)(HINSTANCE, LPCSTR);
            HMENU menu = (*reinterpret_cast<Load*>(kLoadMenuSlot))(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(std::stoi(p.at(0))));
            if (!menu) { detail = "menu resource not loaded"; return false; }
            const UINT state = GetMenuState(GetSubMenu(menu, 0), static_cast<UINT>(std::stoul(p.at(1))), MF_BYCOMMAND);
            DestroyMenu(menu);
            detail = state == UINT(-1) ? "absent" : (state & (MF_DISABLED | MF_GRAYED)) ? "grayed" : "enabled";
            return detail == p.at(2);
        }
        if (verb == "framemenu") {   // framemenu <id> | enabled|grayed|absent : the frame's menu bar (RE+ Tools lives there, UI.cpp)
            auto p = Split(arg);
            const UINT state = GetMenuState(GetMenu(frameWindow), static_cast<UINT>(std::stoul(p.at(0))), MF_BYCOMMAND);
            detail = state == UINT(-1) ? "absent" : (state & (MF_DISABLED | MF_GRAYED)) ? "grayed" : "enabled";
            return detail == p.at(1);
        }
        if (verb == "window") { detail = FindTop(arg) ? "found" : "missing"; return detail == "found"; }
        if (verb == "count") {
            auto p = Split(arg);   // title | ctrl | op | n
            const int count = CountItems(NeedControl(Need(p.at(0)), p.at(1)));
            detail = "count " + std::to_string(count);
            return Compare(count, p.at(2), std::stod(p.at(3)));
        }
        if (verb == "pick") { auto p = Split(arg); return Pick(NeedControl(Need(p.at(0)), p.at(1)), p.at(2), detail); }
        if (verb == "click") {
            auto p = Split(arg);   // title | ctrl [| form name]
            HWND top = Need(p.at(0)); HWND control = NeedControl(top, p.at(1));
            if (p.size() > 2) formName = p.at(2);
            const UINT_PTR timer = SetTimer(nullptr, 0, 50, AnswerForms);
            SendMessageA(GetParent(control), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(control), BN_CLICKED), reinterpret_cast<LPARAM>(control));
            KillTimer(nullptr, timer);
            return true;
        }
        if (verb == "text") { auto p = Split(arg); detail = TextOf(NeedControl(Need(p.at(0)), p.at(1))); return true; }
        if (verb == "shot") {
            auto p = Split(arg);   // title | file [| ctrl]
            HWND top = Need(p.at(0));
            Screenshot(top, p.at(1));
            if (p.size() > 2) {
                RECT window{}, control{}; GetWindowRect(top, &window); GetWindowRect(NeedControl(top, p.at(2)), &control);
                detail = std::to_string(control.left - window.left) + " " + std::to_string(control.top - window.top) + " "
                    + std::to_string(control.right - control.left) + " " + std::to_string(control.bottom - control.top);
                Record("shot_rect", detail);
            }
            return std::filesystem::exists(p.at(1));
        }
        if (verb == "close") { SendMessageA(Need(arg), WM_CLOSE, 0, 0); return true; }
        if (verb == "mem") { auto m = Memory(); detail = m.dump(); if (!name.empty()) results[name] = m; return true; }
        if (verb == "mkdir") { std::error_code error; std::filesystem::create_directories(Trim(arg), error); detail = error ? error.message() : "ok"; return !error; }
        if (verb == "lock" || verb == "unlock") {   // lock <file>: hold it open with no sharing (as another program writing it would); unlock <file>
            static std::map<std::string, HANDLE> locks; const auto file = Trim(arg);
            if (verb == "unlock") { auto it = locks.find(file); if (it == locks.end()) { detail = "not locked"; return false; } CloseHandle(it->second); locks.erase(it); detail = "unlocked"; return true; }
            HANDLE h = CreateFileW(std::filesystem::path(file).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) { detail = "cannot open, error " + std::to_string(GetLastError()); return false; }
            locks[file] = h; detail = "locked"; return true;
        }
        if (verb == "touch") {
            std::error_code error; std::filesystem::last_write_time(Trim(arg), std::filesystem::file_time_type::clock::now(), error);
            detail = error ? error.message() : "touched"; return !error;
        }
        if (verb == "filejson") {
            std::ifstream in(Trim(arg), std::ios::binary); if (!in) { detail = "cannot read " + arg; return false; }
            Json value = Json::parse(in); detail = value.dump().substr(0, 1000); if (!name.empty()) results[name] = value; return true;
        }
        if (verb == "texthas" || verb == "textlacks") {   // texthas <title> | <ctrl> | <text>
            auto p = Split(arg); detail = TextOf(NeedControl(Need(p.at(0)), p.at(1)));
            return (detail.find(p.at(2)) != std::string::npos) == (verb == "texthas");
        }
        if (verb == "settext") { auto p = Split(arg); SetWindowTextA(NeedControl(Need(p.at(0)), p.at(1)), p.size() > 2 ? p.at(2).c_str() : ""); return true; }
        if (verb == "tree") {
            auto p = Split(arg); HWND tree = NeedControl(Need(p.at(0)), p.at(1));
            Json value{{"items", TreeItems(tree, TreeView_GetRoot(tree))}, {"count", static_cast<int>(TreeView_GetCount(tree))}, {"selected", nullptr}};
            if (auto selected = TreeView_GetSelection(tree)) value["selected"] = TreeText(tree, selected);
            if (auto top = TreeView_GetFirstVisible(tree)) value["firstVisible"] = TreeText(tree, top);
            detail = value.dump().substr(0, 1500); if (!name.empty()) results[name] = value; return true;
        }
        if (verb == "key") {
            auto p = Split(arg); HWND tree = NeedControl(Need(p.at(0)), p.at(1));
            NMTVKEYDOWN key{}; key.hdr.hwndFrom = tree; key.hdr.idFrom = GetDlgCtrlID(tree); key.hdr.code = TVN_KEYDOWN; key.wVKey = static_cast<WORD>(std::stoi(p.at(2)));
            const UINT_PTR timer = SetTimer(nullptr, 0, 50, AnswerForms);
            SendMessageA(GetParent(tree), WM_NOTIFY, key.hdr.idFrom, reinterpret_cast<LPARAM>(&key));
            KillTimer(nullptr, timer);
            return true;
        }
        if (verb == "expand") {
            auto p = Split(arg); HWND tree = NeedControl(Need(p.at(0)), p.at(1));
            int index = -1; const auto label = p.at(2).substr(5);
            auto item = label.find(" > ") != std::string::npos ? TreePath(tree, label) : TreeFind(tree, TreeView_GetRoot(tree), label, index);
            if (!item) { detail = "no such tree item"; return false; }
            return TreeView_Expand(tree, item, p.at(3) == "collapse" ? TVE_COLLAPSE : TVE_EXPAND) != FALSE;
        }
        if (verb == "pump") { Pump(std::stoul(arg)); return true; }
        if (verb == "wait") { waitMs = static_cast<DWORD>(std::stoul(arg)); return true; }
        if (verb == "vertexselect") return VertexSelect(Resolve(Json::parse(arg)), detail);
        if (verb == "vertexclear") { VertexClear(); return true; }
        if (verb == "brush") {
            auto info = BrushInfo(Resolve(Json::parse(arg)));
            detail = info.dump().substr(0, 2000);
            if (!name.empty()) results[name] = info;
            return true;
        }
        detail = "unknown step";
    } catch (const std::exception& e) { detail = e.what(); }
    return false;
}

struct StepCall { const std::string* verb; const std::string* name; const std::string* arg; std::string* detail; DWORD* waitMs; bool ok; };
void StepThunk(StepCall* call) { call->ok = Step(*call->verb, *call->name, *call->arg, *call->detail, *call->waitMs); }
DWORD GuardedStep(StepCall* call) {   // no C++ objects here, so __try can catch access violations from native calls
    __try { StepThunk(call); return 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
}

void Finish(const std::string& fatal) {
    VertexClear();
    if (!fatal.empty()) Record("FAIL", fatal);
    else if (failures) Record("FAIL", std::to_string(failures) + " step(s) failed");
    else Record("PASS", "job complete");
    if (report) fclose(report);
    report = nullptr;
    TerminateProcess(GetCurrentProcess(), 0);
}

void Continue();
void CALLBACK Resume(HWND, UINT, UINT_PTR timer, DWORD) { KillTimer(nullptr, timer); Continue(); }

// Runs job lines until the end or a "wait"; a wait returns to the editor's main loop (PeekMessageA(NULL hwnd) +
// DispatchMessageA at 0x10e4142a..0x10e4146b, beside its UpdateWorld guard), which dispatches the thread timer back here.
void Continue() {
    while (nextLine < jobLines.size()) {
        const size_t number = ++nextLine;
        std::string line = jobLines[number - 1];
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto space = line.find(' ');
        std::string verb = line.substr(0, space), name;
        if (const auto at = verb.find('@'); at != std::string::npos) { name = verb.substr(at + 1); verb = verb.substr(0, at); }
        const std::string arg = space == std::string::npos ? std::string() : line.substr(space + 1);
        Record("step", line.substr(0, 300));
        std::string detail;
        DWORD waitMs = 0;
        StepCall call{&verb, &name, &arg, &detail, &waitMs, false};
        LARGE_INTEGER frequency{}, started{}, ended{}; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&started);
        const DWORD code = GuardedStep(&call);
        QueryPerformanceCounter(&ended);
        const double took = (ended.QuadPart - started.QuadPart) * 1000.0 / frequency.QuadPart;
        {
            char text[64]; sprintf_s(text, "%.1f ms", took); Record("took", text);
            static const std::set<std::string> observing{"expect", "dump", "mem", "text", "tree", "filejson"};
            if (!observing.count(verb)) results["_ms"] = took;
        }
        if (code) {
            char text[80];
            sprintf_s(text, "exception 0x%08lX at line %zu", code, number);
            Finish(text);
            return;
        }
        if (!call.ok) {
            ++failures;
            Record("STEPFAIL", "line " + std::to_string(number) + ": " + line.substr(0, 200) + " " + detail.substr(0, 1000));
            continue;
        }
        if (!detail.empty() && verb != "workflow") Record("ok", detail.substr(0, 1000));
        if (waitMs) { SetTimer(nullptr, 0, waitMs, Resume); return; }
    }
    Finish(std::string());
}

LRESULT CALLBACK OnMessage(int code, WPARAM removed, LPARAM parameter) {
    if (code >= 0) {
        auto message = reinterpret_cast<CWPSTRUCT*>(parameter);
        if (message->message == kRun && hook) {
            UnhookWindowsHookEx(hook);
            hook = nullptr;
            HideAll();
            report = _fsopen((directory / "ue2build_report.txt").string().c_str(), "w", _SH_DENYNO);
            std::ifstream job(directory / "ue2build_job.txt");
            for (std::string line; std::getline(job, line);) jobLines.push_back(line);
            Continue();
            return 0;
        }
    }
    return CallNextHookEx(hook, code, removed, parameter);
}

BOOL CALLBACK FindEditorWindow(HWND window, LPARAM threadAddress) {
    DWORD process = 0;
    DWORD thread = GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) return TRUE;
    ShowWindow(window, SW_HIDE);
    char klass[128] = {}; GetClassNameA(window, klass, sizeof(klass));
    char title[256] = {}; GetWindowTextA(window, title, sizeof(title));
    if (!strcmp(klass, "#32770") && !strcmp(title, "Critical Error")) {
        FILE* failure = _fsopen((directory / "ue2build_report.txt").string().c_str(), "w", _SH_DENYNO);
        if (failure) { fputs("FAIL editor startup reached a Critical Error\n", failure); fclose(failure); }
        TerminateProcess(GetCurrentProcess(), 3);
    }
    if (strstr(klass, "WEditorFrame")) { *reinterpret_cast<DWORD*>(threadAddress) = thread; frameWindow = window; }
    return TRUE;
}

DWORD WINAPI WaitUntilReady(void*) {
    for (int attempt = 0; attempt < 480; ++attempt) {
        DWORD uiThread = 0;
        EnumWindows(FindEditorWindow, reinterpret_cast<LPARAM>(&uiThread));
        auto editor = Editor();
        if (uiThread && editor && *reinterpret_cast<void**>(editor + 0x130)) {
            Sleep(5000);   // initial windows exist before startup completes
            hook = SetWindowsHookExA(WH_CALLWNDPROC, OnMessage, nullptr, uiThread);
            DWORD_PTR result = 0;
            if (hook) SendMessageTimeoutA(frameWindow, kRun, 0, 0, SMTO_ABORTIFHUNG, 900000, &result);
            return 0;
        }
        Sleep(250);
    }
    return 1;
}
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        char path[MAX_PATH] = {};
        GetModuleFileNameA(module, path, MAX_PATH);
        directory = std::filesystem::path(path).parent_path();
        if (HANDLE worker = CreateThread(nullptr, 0, WaitUntilReady, nullptr, 0, nullptr)) CloseHandle(worker);
    }
    return TRUE;
}
