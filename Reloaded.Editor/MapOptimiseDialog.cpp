#include "pch.h"
#undef min
#undef max
#include "MapOptimiseDialog.h"
#include "MapOptimiseModel.h"
#include "MapPackageDialog.h"
#include "WorkflowEditor.h"
#include <commctrl.h>
#include <fstream>
#include <shellapi.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace MapOptimiseDialog
{
namespace
{
using Json = Workflow::Json;
enum : int
{
    kIntro = 101,
    kList = 100,
    kStatus = 102,
    kAssets = 103,
    kIntoMap = 104,
    kIntoPackage = 105,
    kPackageName = 106,
    kFolderLabel = 107,
    kFolder = 108,
    kBaseLabel = 109,
    kBase = 110,
    kBaseBrowse = 111,
    kScan = WM_APP + 1
};
struct State
{
    Json report;
    bool ready = false;
    std::string base; // The base install last scanned against.
    bool baseUsed = false; // It was valid, so the list reflects it.
};
// A copy of the game as players get it ([MapOptimise] BaseInstall in
// Reloaded_Editor.ini): packages identical there are not shipped.
std::string IniPath()
{
    char exe[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    return (std::filesystem::path(exe).parent_path() / "Reloaded_Editor.ini").string();
}
std::string SavedBase()
{
    char value[MAX_PATH]{};
    GetPrivateProfileStringA("MapOptimise", "BaseInstall", "", value, sizeof(value), IniPath().c_str());
    return value;
}
void SaveBase(const std::string &base)
{
    WritePrivateProfileStringA("MapOptimise", "BaseInstall", base.c_str(), IniPath().c_str());
}
HWND Control(HWND window, const char *type, const char *text, DWORD style, int id, int x, int y, int width, int height)
{
    auto control =
        CreateWindowExA(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, window,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
    SendMessage(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return control;
}
std::string Text(HWND window, int id)
{
    char text[1024]{};
    GetDlgItemTextA(window, id, text, sizeof(text));
    return text;
}
void Status(HWND window, const std::string &text)
{
    SetDlgItemTextA(window, kStatus, text.c_str());
}
std::vector<std::string> Ticked(HWND window, const State &state)
{
    std::vector<std::string> packs;
    auto list = GetDlgItem(window, kList);
    const auto &rows = state.report.at("packs");
    for (size_t i = 0; i < rows.size(); ++i)
        if (ListView_GetCheckState(list, static_cast<int>(i)))
            packs.push_back(rows[i].at("name").get<std::string>());
    return packs;
}
void Summary(HWND window, const State &state)
{
    if (!state.ready)
        return;
    std::uint64_t whole = 0, used = 0;
    size_t count = 0;
    auto list = GetDlgItem(window, kList);
    const auto &rows = state.report.at("packs");
    for (size_t i = 0; i < rows.size(); ++i)
        if (ListView_GetCheckState(list, static_cast<int>(i)))
        {
            whole += rows[i].at("fileSize").get<std::uint64_t>();
            used += rows[i].at("usedSize").get<std::uint64_t>();
            ++count;
        }
    size_t installed = 0;
    for (const auto &row : rows)
        installed += row.value("installed", false) ? 1 : 0;
    std::string text;
    if (installed)
        text = std::to_string(installed) + " package(s) are in the base install, so players have them: left unticked "
                                           "and listed last. ";
    if (!count)
        text += "Tick the packages to take assets from. Leave a package unticked when players already have it.";
    else
        text += "Ticked: " + std::to_string(count) + " package(s). Instead of shipping " + MapOptimise::Size(whole) +
               " of packages, the release copy carries about " + MapOptimise::Size(used) +
               " of assets. Your working map keeps using the packages, which are not changed.";
    Status(window, text);
    EnableWindow(GetDlgItem(window, IDOK), count > 0);
}
void Fill(HWND window, State &state)
{
    state.ready = false;
    state.base = Text(window, kBase);
    std::string baseError;
    state.baseUsed = false;
    try
    {
        state.report = Workflow::Editor::OptimiseReport(state.base);
        state.baseUsed = !state.base.empty();
    }
    catch (const std::exception &e)
    {
        if (state.base.empty())
            throw;
        baseError = e.what();
        state.report = Workflow::Editor::OptimiseReport();
    }
    auto list = GetDlgItem(window, kList);
    ListView_DeleteAllItems(list);
    int row = 0;
    for (const auto &pack : state.report.at("packs"))
    {
        const auto whole = pack.at("fileSize").get<std::uint64_t>(), used = pack.at("usedSize").get<std::uint64_t>();
        const bool installed = pack.value("installed", false);
        std::string values[] = {pack.at("name").get<std::string>(), std::to_string(pack.at("assets").size()),
                                MapOptimise::Size(used),
                                pack.at("file").get<std::string>().empty() ? std::string("no file found")
                                                                            : MapOptimise::Size(whole),
                                installed      ? std::string("players have it")
                                : whole > used ? MapOptimise::Size(whole - used)
                                               : std::string("-")};
        LVITEMA item{};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.pszText = values[0].data();
        SendMessageA(list, LVM_INSERTITEMA, 0, reinterpret_cast<LPARAM>(&item));
        for (int i = 1; i < 5; ++i)
        {
            item.iSubItem = i;
            item.pszText = values[i].data();
            SendMessageA(list, LVM_SETITEMTEXTA, row, reinterpret_cast<LPARAM>(&item));
        }
        ListView_SetCheckState(list, row, pack.at("suggested").get<bool>());
        ++row;
    }
    state.ready = true;
    EnableWindow(GetDlgItem(window, kAssets), TRUE);
    if (state.report.at("packs").empty())
    {
        Status(window, "The map uses no assets from outside the game's own packages: there is nothing to optimise.");
        EnableWindow(GetDlgItem(window, IDOK), FALSE);
        return;
    }
    Summary(window, state);
    if (!baseError.empty())
        MessageBoxA(window, ("The base install was not used: " + baseError).c_str(), "Optimise Map Assets",
                    MB_OK | MB_ICONWARNING);
}
void BrowseBase(HWND window, State &state)
{
    auto initialized = OleInitialize(nullptr);
    BROWSEINFOW browse{};
    browse.hwndOwner = window;
    browse.lpszTitle = L"Choose a copy of the game as players have it (the folder holding System and Packages). "
                       L"Packages identical there are not shipped.";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    auto selected = SHBrowseForFolderW(&browse);
    wchar_t path[MAX_PATH]{};
    const bool picked = selected && SHGetPathFromIDListW(selected, path);
    CoTaskMemFree(selected);
    if (SUCCEEDED(initialized))
        OleUninitialize();
    if (!picked)
        return;
    SetDlgItemTextW(window, kBase, path);
    SaveBase(Text(window, kBase));
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    Fill(window, state);
}
void Create(HWND window, State &state)
{
    const auto packs = Ticked(window, state);
    const bool intoMap = IsDlgButtonChecked(window, kIntoMap) == BST_CHECKED;
    Json options = {{"packs", packs},
                    {"destination", intoMap ? "map" : "package"},
                    {"folder", Text(window, kFolder)},
                    {"assetPackage", Text(window, kPackageName)}};
    const auto map = state.report.value("map", std::string());
    auto question = "Save " + map + " and write its release copy, also named " + map + ", into " +
                    options["folder"].get<std::string>() + "?\r\n\r\nThe assets the map uses from the ticked packages go " +
                    (intoMap ? std::string("inside the release copy")
                             : "into " + options["assetPackage"].get<std::string>() + ".usx") +
                    ". Your working map stays as it is, using the packages.";
    if (MessageBoxA(window, question.c_str(), "Optimise Map Assets", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return;
    Json result;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        try
        {
            SetCursor(LoadCursor(nullptr, IDC_WAIT));
            Status(window, "Writing the release copy...");
            result = Workflow::Editor::OptimiseRelease(options);
            break;
        }
        catch (const std::exception &e)
        {
            const std::string message = e.what();
            if (attempt == 0 && message.rfind("These files exist already:", 0) == 0)
            {
                auto ask = message + "\r\n\r\nReplace them?";
                if (MessageBoxA(window, ask.c_str(), "Optimise Map Assets", MB_YESNO | MB_ICONWARNING) != IDYES)
                {
                    Summary(window, state);
                    return;
                }
                options["overwrite"] = true;
                continue;
            }
            throw;
        }
    }
    std::string text = "Release copy written:\r\n";
    for (const auto &f : result.at("written"))
        text += "  " + f.at("file").get<std::string>() + " (" + MapOptimise::Size(f.at("size").get<std::uint64_t>()) +
                ")\r\n";
    text += "\r\n" + std::to_string(result.at("moved").size()) + " asset(s) carried by the copy.\r\n";
    if (result.at("needs").empty())
        text += "It needs nothing beyond the game's own packages.";
    else
    {
        text += "Players still need these packages:\r\n";
        for (const auto &p : result.at("needs"))
            text += "  " + p.at("name").get<std::string>() +
                    (p.at("file").get<std::string>().empty() ? std::string()
                                                             : " (" + MapOptimise::Size(p.at("size").get<std::uint64_t>()) + ")") +
                    "\r\n";
    }
    if (!result.at("left").empty())
        text += "\r\n" + std::to_string(result.at("left").size()) +
                " object(s) from the ticked packages could not be moved, so those packages are still needed. See the "
                "editor log for the list.";
    for (const auto &path : result.at("left"))
        Workflow::Editor::Exec("LOG Optimise Map Assets: still taken from its package: " + path.get<std::string>());
    text += "\r\n\r\nPackage the release copy for sharing now?";
    Summary(window, state);
    if (MessageBoxA(window, text.c_str(), "Optimise Map Assets", MB_YESNO | MB_ICONINFORMATION) == IDYES)
        // Packaged against the same base install: what players have stays out.
        MapPackageDialog::Preview(window, result.at("playable").get<std::string>(),
                                    state.baseUsed ? std::filesystem::path(state.base) : std::filesystem::path());
}
INT_PTR CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto state = reinterpret_cast<State *>(GetWindowLongPtr(window, DWLP_USER));
    try
    {
        if (message == WM_INITDIALOG)
        {
            state = reinterpret_cast<State *>(l);
            SetWindowLongPtr(window, DWLP_USER, l);
            SetWindowTextA(window, "Optimise Map Assets");
            RECT r{};
            GetClientRect(window, &r);
            const int width = r.right - 24;
            Control(window, "STATIC",
                    "A map that uses a few assets from a big package needs the whole package shipped with it. This "
                    "makes a release copy of the map that carries just the assets it uses from the ticked packages. "
                    "Your working map keeps using the packages, and they are not changed.",
                    0, kIntro, 12, 10, width, 42);
            auto list = Control(window, WC_LISTVIEWA, "", WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | WS_BORDER,
                                kList, 12, 56, width, r.bottom - 296);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
            const char *columns[] = {"Package", "Assets used", "Used size", "Whole package", "Saving"};
            const int widths[] = {width - 400, 85, 100, 105, 100};
            for (int i = 0; i < 5; ++i)
            {
                LVCOLUMNA col{};
                col.mask = LVCF_TEXT | LVCF_WIDTH;
                col.cx = widths[i];
                col.pszText = const_cast<char *>(columns[i]);
                SendMessageA(list, LVM_INSERTCOLUMNA, i, reinterpret_cast<LPARAM>(&col));
            }
            Control(window, "STATIC", "Players already have:", 0, kBaseLabel, 12, r.bottom - 233, 120, 20);
            Control(window, "EDIT", SavedBase().c_str(), WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, kBase, 135,
                    r.bottom - 236, width - 223, 22);
            Control(window, "BUTTON", "Base Install...", WS_TABSTOP, kBaseBrowse, r.right - 100, r.bottom - 237, 88, 25);
            int y = r.bottom - 202;
            Control(window, "BUTTON", "Show Assets...", WS_TABSTOP, kAssets, 12, y, 110, 25);
            y += 34;
            const auto map = Workflow::Editor::MapFile().empty()
                                 ? std::string("Map")
                                 : std::filesystem::path(Workflow::Editor::MapFile()).stem().string();
            Control(window, "BUTTON", "Put the assets inside the release copy (one file to share)",
                    WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP, kIntoMap, 12, y, width, 20);
            Control(window, "BUTTON", "Put them in a package of their own (share it with each map that uses them):",
                    WS_TABSTOP | BS_AUTORADIOBUTTON, kIntoPackage, 12, y + 22, 420, 20);
            Control(window, "EDIT", (map + "_Assets").c_str(), WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, kPackageName,
                    440, y + 21, width - 428, 22);
            CheckDlgButton(window, kIntoMap, BST_CHECKED);
            // The release copy keeps the map's name, which the game needs, so it
            // goes into a folder of its own laid out like the game's.
            Control(window, "STATIC", "Release folder:", 0, kFolderLabel, 12, y + 54, 110, 20);
            Control(window, "EDIT", Workflow::Editor::ReleaseFolder(map).string().c_str(),
                    WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, kFolder, 125, y + 51, width - 113, 22);
            Control(window, "STATIC", "Scanning the map...", 0, kStatus, 12, r.bottom - 74, width, 32);
            Control(window, "BUTTON", "Create Release Copy", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK, r.right - 260,
                    r.bottom - 36, 150, 27);
            Control(window, "BUTTON", "Close", WS_TABSTOP, IDCANCEL, r.right - 102, r.bottom - 36, 90, 27);
            EnableWindow(GetDlgItem(window, IDOK), FALSE);
            EnableWindow(GetDlgItem(window, kAssets), FALSE);
            PostMessage(window, kScan, 0, 0);
            return TRUE;
        }
        if (!state)
            return FALSE;
        if (message == kScan)
        {
            SetCursor(LoadCursor(nullptr, IDC_WAIT));
            Fill(window, *state);
            return TRUE;
        }
        if (message == WM_NOTIFY && state->ready)
        {
            auto header = reinterpret_cast<NMHDR *>(l);
            if (header->idFrom == kList && header->code == LVN_ITEMCHANGED)
                Summary(window, *state);
        }
        if (message == WM_COMMAND)
        {
            if (LOWORD(w) == IDCANCEL)
            {
                EndDialog(window, IDCANCEL);
                return TRUE;
            }
            if (LOWORD(w) == kBaseBrowse && state->ready)
            {
                BrowseBase(window, *state);
                return TRUE;
            }
            // A base install typed or pasted in is used once the field is left.
            if (LOWORD(w) == kBase && HIWORD(w) == EN_KILLFOCUS && state->ready && Text(window, kBase) != state->base)
            {
                SaveBase(Text(window, kBase));
                SetCursor(LoadCursor(nullptr, IDC_WAIT));
                Fill(window, *state);
                return TRUE;
            }
            if (LOWORD(w) == kAssets && state->ready)
            {
                auto file = Workflow::Editor::Directory() / "OptimiseMapAssets.txt";
                std::filesystem::create_directories(file.parent_path());
                std::ofstream output(file, std::ios::binary);
                output << state->report.at("text").get<std::string>();
                for (const auto &line : state->report.at("unreadable"))
                    output << "Could not read " << line.get<std::string>() << "\r\n";
                output.close();
                if (!output)
                    throw std::runtime_error("Cannot write " + file.string());
                ShellExecuteW(window, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            if (LOWORD(w) == IDOK && state->ready)
            {
                Create(window, *state);
                return TRUE;
            }
        }
        if (message == WM_CLOSE)
        {
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
    }
    catch (const std::exception &e)
    {
        if (state && state->ready)
            Summary(window, *state);
        else
            Status(window, e.what());
        MessageBoxA(window, e.what(), "Optimise Map Assets", MB_OK | MB_ICONERROR);
    }
    return FALSE;
}
} // namespace
void Open(HWND owner)
{
    Workflow::Editor::MapFile(); // A map must be open.
    State state;
    std::vector<WORD> bytes((sizeof(DLGTEMPLATE) + 1) / 2 + 3, 0);
    auto dialog = reinterpret_cast<DLGTEMPLATE *>(bytes.data());
    dialog->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    dialog->cx = 470;
    dialog->cy = 330;
    if (DialogBoxIndirectParamW(GetModuleHandle(nullptr), dialog, owner, Proc, reinterpret_cast<LPARAM>(&state)) == -1)
        throw std::runtime_error("Cannot open the Optimise Map Assets window.");
}
} // namespace MapOptimiseDialog
