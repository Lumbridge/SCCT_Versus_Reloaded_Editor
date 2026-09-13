#include "pch.h"
#undef min
#undef max
#include "MapPackage.h"
#include "MapPackageDialog.h"
#include "WorkflowEditor.h"
#include <commctrl.h>
#include <commdlg.h>
#include <fstream>
#include <future>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace MapPackageDialog
{
namespace
{
struct State
{
    std::filesystem::path root, map, output;
    MapPackage::Plan plan;
    std::future<MapPackage::Plan> scan;
    std::future<void> write;
    std::future<std::vector<bool>> baseline;
    bool ready = false, busy = false;
};
HWND Control(HWND window, const char *type, const char *text, DWORD style, int id, int x, int y, int width, int height)
{
    auto control =
        CreateWindowExA(0, type, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, x, y, width, height, window,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
    SendMessage(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return control;
}
void Status(HWND window, const std::string &text)
{
    SetWindowTextA(GetDlgItem(window, 102), text.c_str());
}
void Checks(HWND window, State &state)
{
    if (!state.ready)
        return;
    auto list = GetDlgItem(window, 100);
    std::uintmax_t bytes = 0;
    size_t files = 0, omitted = 0;
    for (size_t i = 0; i < state.plan.files.size(); ++i)
    {
        auto &file = state.plan.files[i];
        file.include = ListView_GetCheckState(list, static_cast<int>(i)) != 0;
        if (file.include)
        {
            bytes += file.size;
            ++files;
        }
        else
            ++omitted;
    }
    auto message = std::to_string(files) + " files checked (" + std::to_string(bytes / 1024 / 1024) +
                   " MB before compression). " + std::to_string(omitted) +
                   " omitted dependencies must already be installed.\r\n";
    if (!state.plan.errors.empty())
        message += std::to_string(state.plan.errors.size()) +
                   " dependency errors. Open Report for details; resolve them and reopen packaging.";
    else
        message += "Dependencies found. Files loaded only by script string paths may need to be added separately.";
    Status(window, message);
    EnableWindow(GetDlgItem(window, IDOK),
                 !state.busy && state.plan.errors.empty() && !state.plan.files.empty() && state.plan.files[0].include);
}
bool Destination(HWND owner, State &state)
{
    wchar_t path[32768]{};
    auto proposed = state.map.stem().wstring() + L".zip";
    wcscpy_s(path, proposed.c_str());
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFile = path;
    dialog.nMaxFile = 32768;
    dialog.lpstrFilter = L"Map ZIP (*.zip)\0*.zip\0";
    dialog.lpstrDefExt = L"zip";
    dialog.lpstrTitle = L"Save map package (choose a new filename)";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog))
        return false;
    state.output = path;
    return true;
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
            SetWindowTextA(window, "Package Map for Sharing");
            RECT r{};
            GetClientRect(window, &r);
            Control(window, "STATIC",
                    "Packages the selected saved playable map. Save/build in the editor first to include your latest "
                    "edits.\r\nUncheck dependencies only when recipients already have those exact files. Omissions are "
                    "listed in the ZIP report.",
                    0, 101, 12, 12, r.right - 24, 52);
            auto list = Control(window, WC_LISTVIEWA, "", LVS_REPORT | LVS_SHOWSELALWAYS | WS_BORDER, 100, 12, 70,
                                r.right - 24, r.bottom - 182);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
            const char *columns[] = {"File in ZIP", "Size (KB)", "Required by"};
            int widths[] = {r.right / 2, 90, r.right / 2 - 125};
            for (int i = 0; i < 3; ++i)
            {
                LVCOLUMNA col{};
                col.mask = LVCF_TEXT | LVCF_WIDTH;
                col.cx = widths[i];
                col.pszText = const_cast<char *>(columns[i]);
                SendMessageA(list, LVM_INSERTCOLUMNA, i, reinterpret_cast<LPARAM>(&col));
            }
            Control(window, "STATIC", "Scanning saved map dependencies...", 0, 102, 12, r.bottom - 105, r.right - 24,
                    57);
            Control(window, "BUTTON", "Report...", 0, 103, 12, r.bottom - 38, 95, 27);
            Control(window, "BUTTON", "Exclude base files...", 0, 104, 115, r.bottom - 38, 155, 27);
            EnableWindow(GetDlgItem(window, 104), FALSE);
            Control(window, "BUTTON", "Create ZIP...", BS_DEFPUSHBUTTON, IDOK, r.right - 245, r.bottom - 38, 125, 27);
            Control(window, "BUTTON", "Close", 0, IDCANCEL, r.right - 110, r.bottom - 38, 95, 27);
            EnableWindow(GetDlgItem(window, IDOK), FALSE);
            EnableWindow(GetDlgItem(window, 103), FALSE);
            state->busy = true;
            state->scan = std::async(std::launch::async,
                                     [root = state->root, map = state->map] { return MapPackage::Inspect(root, map); });
            SetTimer(window, 1, 100, nullptr);
            return TRUE;
        }
        if (!state)
            return FALSE;
        if (message == WM_TIMER)
        {
            if (state->scan.valid() && state->scan.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                state->busy = false;
                state->plan = state->scan.get();
                auto list = GetDlgItem(window, 100);
                int row = 0;
                for (const auto &file : state->plan.files)
                {
                    std::string by;
                    for (const auto &name : file.requiredBy)
                    {
                        if (!by.empty())
                            by += ", ";
                        by += name;
                    }
                    std::string values[] = {file.destination, std::to_string((file.size + 1023) / 1024), by};
                    LVITEMA item{};
                    item.mask = LVIF_TEXT;
                    item.iItem = row;
                    item.pszText = values[0].data();
                    SendMessageA(list, LVM_INSERTITEMA, 0, reinterpret_cast<LPARAM>(&item));
                    for (int i = 1; i < 3; ++i)
                    {
                        item.iSubItem = i;
                        item.pszText = values[i].data();
                        SendMessageA(list, LVM_SETITEMTEXTA, row, reinterpret_cast<LPARAM>(&item));
                    }
                    ListView_SetCheckState(list, row, TRUE);
                    ++row;
                }
                state->ready = true;
                EnableWindow(GetDlgItem(window, 103), TRUE);
                EnableWindow(GetDlgItem(window, 104), TRUE);
                Checks(window, *state);
            }
            if (state->baseline.valid() &&
                state->baseline.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                auto matches = state->baseline.get();
                auto list = GetDlgItem(window, 100);
                for (size_t i = 1; i < matches.size(); ++i)
                    if (matches[i])
                        ListView_SetCheckState(list, static_cast<int>(i), FALSE);
                state->busy = false;
                EnableWindow(list, TRUE);
                Checks(window, *state);
            }
            if (state->write.valid() && state->write.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                state->busy = false;
                EnableWindow(GetDlgItem(window, 100), TRUE);
                state->write.get();
                Checks(window, *state);
                auto message = L"Created map package:\r\n" + state->output.wstring();
                MessageBoxW(window, message.c_str(), L"Map package created", MB_OK | MB_ICONINFORMATION);
            }
            return TRUE;
        }
        if (message == WM_NOTIFY && state->ready && !state->busy)
        {
            auto header = reinterpret_cast<NMHDR *>(l);
            if (header->idFrom == 100 && header->code == LVN_ITEMCHANGING)
            {
                auto change = reinterpret_cast<NMLISTVIEW *>(l);
                if (change->iItem == 0 && (change->uChanged & LVIF_STATE) &&
                    (change->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(1))
                {
                    SetWindowLongPtr(window, DWLP_MSGRESULT, TRUE);
                    return TRUE;
                }
            }
            if (header->idFrom == 100 && header->code == LVN_ITEMCHANGED)
                Checks(window, *state);
        }
        if (message == WM_COMMAND)
        {
            if (LOWORD(w) == 104 && state->ready && !state->busy)
            {
                auto initialized = OleInitialize(nullptr);
                BROWSEINFOW browse{};
                browse.hwndOwner = window;
                browse.lpszTitle =
                    L"Select a separate base game installation. Only identical dependency files will be unchecked.";
                browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                auto selected = SHBrowseForFolderW(&browse);
                wchar_t path[MAX_PATH]{};
                bool picked = selected && SHGetPathFromIDListW(selected, path);
                CoTaskMemFree(selected);
                if (SUCCEEDED(initialized))
                    OleUninitialize();
                if (!picked)
                    return TRUE;
                state->busy = true;
                EnableWindow(GetDlgItem(window, 100), FALSE);
                EnableWindow(GetDlgItem(window, IDOK), FALSE);
                Status(window, "Comparing dependencies with the base installation...");
                state->baseline =
                    std::async(std::launch::async, [plan = state->plan, base = std::filesystem::path(path)] {
                        return MapPackage::MatchBaseFiles(plan, base);
                    });
                return TRUE;
            }
            if (LOWORD(w) == IDCANCEL)
            {
                if (!state->busy)
                    EndDialog(window, IDCANCEL);
                return TRUE;
            }
            if (LOWORD(w) == 103 && state->ready)
            {
                // The preview report uses the same content embedded in the ZIP.
                auto report = MapPackage::Report(state->plan);
                auto file = Workflow::Editor::Directory() / "MapPackage-preview.txt";
                std::filesystem::create_directories(file.parent_path());
                std::ofstream output(file, std::ios::binary);
                output << report;
                output.close();
                if (!output)
                    throw std::runtime_error("Cannot write dependency preview report.");
                ShellExecuteW(window, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            if (LOWORD(w) == IDOK && state->ready && !state->busy)
            {
                Checks(window, *state);
                if (!state->plan.errors.empty() || !Destination(window, *state))
                    return TRUE;
                state->busy = true;
                EnableWindow(GetDlgItem(window, IDOK), FALSE);
                EnableWindow(GetDlgItem(window, 100), FALSE);
                Status(window, "Creating ZIP... Please wait for packaging to finish.");
                state->write = std::async(
                    std::launch::async, [plan = state->plan, path = state->output] { MapPackage::Write(plan, path); });
                return TRUE;
            }
        }
        if (message == WM_CLOSE)
        {
            if (!state->busy)
                EndDialog(window, IDCANCEL);
            return TRUE;
        }
        if (message == WM_DESTROY)
            KillTimer(window, 1);
    }
    catch (const std::exception &e)
    {
        if (state)
        {
            state->busy = false;
            EnableWindow(GetDlgItem(window, 100), TRUE);
            if (state->ready)
                Checks(window, *state);
        }
        Status(window, e.what());
        MessageBoxA(window, e.what(), "Map Packaging", MB_OK | MB_ICONERROR);
    }
    return FALSE;
}
} // namespace
void Open(HWND owner)
{
    State state;
    state.root = Workflow::Editor::Directory().parent_path().parent_path();
    auto directory = state.root / "Packages" / "Maps";
    wchar_t path[32768]{};
    auto key = Workflow::Editor::MapKey();
    if (!key.empty())
    {
        auto map = directory / std::filesystem::path(key).filename();
        wcscpy_s(path, map.c_str());
    }
    OPENFILENAMEW choose{sizeof(choose)};
    choose.hwndOwner = owner;
    choose.lpstrFile = path;
    choose.nMaxFile = 32768;
    choose.lpstrInitialDir = directory.c_str();
    choose.lpstrFilter = L"Playable SCCT map (*.sdc)\0*.sdc\0";
    choose.lpstrTitle = L"Select saved playable map (save/build your edits first)";
    choose.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&choose))
        return;
    Preview(owner, path);
}
void Preview(HWND owner, const std::filesystem::path &map)
{
    State state;
    state.root = Workflow::Editor::Directory().parent_path().parent_path();
    state.map = map;
    std::vector<WORD> bytes((sizeof(DLGTEMPLATE) + 1) / 2 + 3, 0);
    auto dialog = reinterpret_cast<DLGTEMPLATE *>(bytes.data());
    dialog->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    dialog->cx = 550;
    dialog->cy = 330;
    if (DialogBoxIndirectParamW(GetModuleHandle(nullptr), dialog, owner, Proc, reinterpret_cast<LPARAM>(&state)) == -1)
        throw std::runtime_error("Cannot open map packaging dialog.");
}
} // namespace MapPackageDialog
