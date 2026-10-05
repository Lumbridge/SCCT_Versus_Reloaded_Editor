#pragma once
#include <windows.h>
#include <commctrl.h>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

// Window plumbing shared by the Lighting Budget and Render Budget windows:
// child controls in the GUI font, report list views and the ini beside the
// editor.
namespace Budget
{
    inline std::string IniPath()
    {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        return (std::filesystem::path(exe).parent_path() / "Reloaded_Editor.ini").string();
    }

    // An int from Reloaded_Editor.ini, written with its default the first
    // time so the section can be found and edited.
    inline int IniInt(const char* section, const char* key, int fallback)
    {
        const auto ini = IniPath();
        char value[32] = {};
        GetPrivateProfileStringA(section, key, "", value, sizeof(value), ini.c_str());
        if (!value[0]) WritePrivateProfileStringA(section, key, std::to_string(fallback).c_str(), ini.c_str());
        return static_cast<int>(GetPrivateProfileIntA(section, key, fallback, ini.c_str()));
    }

    inline HWND Control(HWND window, const char* type, const char* text, DWORD style, int id, int x, int y, int width, int height)
    {
        auto control = CreateWindowExA(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, window,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
        SendMessage(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return control;
    }

    // A full-row-select report list.
    inline HWND ReportList(HWND window, int id)
    {
        auto list = Control(window, WC_LISTVIEWA, "", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, id, 0, 0, 0, 0);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        return list;
    }

    // Counts read better right-aligned; the first column and the named text
    // columns stay left.
    inline void Columns(HWND list, const std::vector<std::pair<const char*, int>>& columns, std::initializer_list<const char*> textColumns)
    {
        for (int i = 0; i < static_cast<int>(columns.size()); ++i)
        {
            LVCOLUMNA column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            bool text = !i;
            for (const char* name : textColumns) text = text || std::string(columns[i].first) == name;
            column.fmt = text ? LVCFMT_LEFT : LVCFMT_RIGHT;
            column.cx = columns[i].second;
            column.pszText = const_cast<char*>(columns[i].first);
            SendMessageA(list, LVM_INSERTCOLUMNA, i, reinterpret_cast<LPARAM>(&column));
        }
    }

    inline void Row(HWND list, int row, LPARAM data, const std::vector<std::string>& cells)
    {
        LVITEMA item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        item.lParam = data;
        item.pszText = const_cast<char*>(cells[0].c_str());
        SendMessageA(list, LVM_INSERTITEMA, 0, reinterpret_cast<LPARAM>(&item));
        for (int i = 1; i < static_cast<int>(cells.size()); ++i)
        {
            LVITEMA cell{};
            cell.iSubItem = i;
            cell.pszText = const_cast<char*>(cells[i].c_str());
            SendMessageA(list, LVM_SETITEMTEXTA, row, reinterpret_cast<LPARAM>(&cell));
        }
    }

    // The row's data, or false when the row is gone.
    inline bool RowData(HWND list, int row, LPARAM& data)
    {
        if (row < 0) return false;
        LVITEMA item{};
        item.mask = LVIF_PARAM;
        item.iItem = row;
        if (!SendMessageA(list, LVM_GETITEMA, 0, reinterpret_cast<LPARAM>(&item))) return false;
        data = item.lParam;
        return true;
    }

    // Selects and shows a row by its data; false when no row has it.
    inline bool SelectRowByData(HWND list, LPARAM data)
    {
        const int count = ListView_GetItemCount(list);
        for (int row = 0; row < count; ++row)
        {
            LPARAM value = 0;
            if (RowData(list, row, value) && value == data)
            {
                ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(list, row, FALSE);
                return true;
            }
        }
        return false;
    }

    inline void RegisterClassOnce(const char* name, WNDPROC proc)
    {
        WNDCLASSA wc{};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = name;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassA(&wc);
    }
}
