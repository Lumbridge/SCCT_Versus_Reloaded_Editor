#include "pch.h"
#include "PlayLevelPlayers.h"
#include "ObjectivePlayersModel.h"
#include "WorkflowEditor.h"
#include "logger.h"
#include <commctrl.h>
#include <cstring>
#include <stdexcept>
#include <string>

namespace Editor = Workflow::Editor;

// The top bar (WTopBar) is the stock editor's row of WButtons under the menus;
// each button's control id is its command, Play Level 40015 and Story 40029 the
// last two. The list is a child of the bar placed after the last of its buttons,
// and the bar is subclassed for the list's notifications.
namespace
{
    constexpr int kListId = 0x5340;
    constexpr UINT_PTR kSubclass = 0x5341, kTimer = 0x5342;
    constexpr UINT kPlayLevel = 40015, kStory = 40029;
    constexpr int kWidth = 170;
    HWND bar = nullptr, list = nullptr;
    bool filling = false;

    std::string Label(int players)
    {
        if (!players) return "Play Level: every objective";
        return "Play Level: " + std::to_string(players) + (players == 1 ? " player" : " players");
    }

    // The map's count, shown unless the list is open.
    void Fill()
    {
        if (!list || SendMessageA(list, CB_GETDROPPEDSTATE, 0, 0)) return;
        int players = 0;
        try { players = Editor::ObjectivePlayerRules().value("playLevel", 0); }
        catch (const std::exception&) { players = 0; }
        filling = true;
        SendMessageA(list, CB_SETCURSEL, players, 0);
        filling = false;
    }

    void Choose()
    {
        const LRESULT players = SendMessageA(list, CB_GETCURSEL, 0, 0);
        if (players == CB_ERR) return;
        try { Editor::SetObjectivePlayLevelPlayers(static_cast<int>(players)); }
        catch (const std::exception& e)
        {
            MessageBoxA(GetAncestor(bar, GA_ROOT), e.what(), "Play Level", MB_OK | MB_ICONERROR);
        }
        Fill();
    }

    // After the bar's last button: Story, else Play Level, else the rightmost one.
    void Place()
    {
        if (!bar || !list) return;
        HWND after = GetDlgItem(bar, kStory);
        if (!after) after = GetDlgItem(bar, kPlayLevel);
        RECT best{};
        bool found = false;
        for (HWND child = GetWindow(bar, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        {
            if (child == list || (after && child != after)) continue;
            RECT r{};
            GetWindowRect(child, &r);
            if (!found || r.right > best.right) { best = r; found = true; }
        }
        MapWindowPoints(nullptr, bar, reinterpret_cast<POINT*>(&best), 2);
        RECT client{};
        GetClientRect(bar, &client);
        const int x = found ? best.right + 12 : 4;
        const int y = found ? best.top + (best.bottom - best.top - 21) / 2 : (client.bottom - 21) / 2;
        SetWindowPos(list, HWND_TOP, x, (std::max)(0, y), kWidth, 320, SWP_NOACTIVATE);
    }

    LRESULT CALLBACK BarProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR)
    {
        if (message == WM_COMMAND && LOWORD(w) == kListId)
        {
            if (HIWORD(w) == CBN_DROPDOWN) Fill();
            else if (HIWORD(w) == CBN_SELCHANGE && !filling) Choose();
            return 0;
        }
        if (message == WM_TIMER && w == kTimer) { Fill(); return 0; }
        if (message == WM_SIZE || message == WM_WINDOWPOSCHANGED)
        {
            const LRESULT result = DefSubclassProc(window, message, w, l);
            Place();
            return result;
        }
        if (message == WM_NCDESTROY)
        {
            KillTimer(window, kTimer);
            RemoveWindowSubclass(window, BarProc, kSubclass);
            bar = nullptr;
            list = nullptr;
        }
        return DefSubclassProc(window, message, w, l);
    }

    HWND FindBar(HWND parent)
    {
        for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        {
            char name[96] = {};
            GetClassNameA(child, name, static_cast<int>(sizeof(name)));
            const size_t length = strlen(name), suffix = strlen("WTopBar");
            if (length >= suffix && strcmp(name + length - suffix, "WTopBar") == 0) return child;
            if (HWND inner = FindBar(child)) return inner;
        }
        return nullptr;
    }
}

void PlayLevelPlayers::Attach(HWND frame)
{
    if (list && IsWindow(list)) return;
    HWND found = frame ? FindBar(frame) : nullptr;
    if (!found || !GetDlgItem(found, kPlayLevel)) return;
    const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrA(found, GWLP_HINSTANCE));
    HWND created = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, kWidth, 320, found,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)), instance, nullptr);
    if (!created) return;
    SendMessageA(created, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
    for (int players = 0; players <= ObjectivePlayers::MostPlayers; ++players)
        SendMessageA(created, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Label(players).c_str()));
    if (!SetWindowSubclass(found, BarProc, kSubclass, 0)) { DestroyWindow(created); return; }
    bar = found;
    list = created;
    // The stock buttons' own tooltips come from the bar; the list says what it is for.
    if (HWND tip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, nullptr, WS_POPUP | TTS_ALWAYSTIP, 0, 0, 0, 0, bar, nullptr, instance, nullptr))
    {
        TOOLINFOA info{};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = bar;
        info.uId = reinterpret_cast<UINT_PTR>(list);
        char text[] = "The lobby size Play Level tests the map's objective player counts with (Players in match). Real matches use the lobby's own count.";
        info.lpszText = text;
        SendMessageA(tip, TTM_ADDTOOLA, 0, reinterpret_cast<LPARAM>(&info));
        SendMessageA(tip, TTM_SETMAXTIPWIDTH, 0, 320);
    }
    Place();
    Fill();
    // Another map, an Undo or another tool may change the count.
    SetTimer(bar, kTimer, 2000, nullptr);
    Logger::log("Play Level players: list attached to the top bar");
}
