#include "pch.h"
#include "AssetNameGloss.h"
#include "AssetNameGlossModel.h"
#include "AssetNameDictionary.gen.h"
#include "Hooks.h"
#include "MemoryWriter.h"
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

INIT_HOOKS;

// Engine layout for the supported SCCT editor build.
#define GEDITOR_PTR                 0x1165DFA0u
#define GEDITOR_CURRENT_MATERIAL    0x138
#define GMALLOC_PTR                 0x115AC708u
#define GNAMES_DATA_PTR             0x1169CFBCu
#define GNAMES_NUM_PTR              0x1169CFC0u
#define FNAME_ENTRY_STR_OFFSET      0x0C
#define UOBJECT_FNAME_OFFSET        0x20

// Texture Browser, as read from this build's code:
//  - 0x10EC05E0 decides whether a material is filtered out of the browser.
//    It upper-cases the Filter box text and the material's name and calls
//    appStrstr (0x10FA35E0) at 0x10EC073D; the name's FString is at the
//    caller's [esp+8]. The call is redirected to also accept the English.
//  - DrawTextureBrowser (0x10EC9B50) builds each thumbnail's label FString at
//    [ebp-0x20] ("<Class> <Name>[*] [<Format>]" plus usage counts) and reads it at
//    0x10ECAAD3 for measuring and drawing; the material is at [ebp-0x4C].
//  - WBrowserTexture::SetCaption (0x10E80D40) hands its caption FString at
//    [ebp-0x28] ("<Class> <Path> (<U>x<V>)") to WBrowser::SetCaption at
//    0x10E80E97.
#define TB_FILTER_STRSTR_CALL       0x10EC073Du
#define TB_LABEL_READ               0x10ECAAD3u
#define TB_LABEL_RESUME             0x10ECAAD9u
#define TB_CAPTION_SET              0x10E80E97u
#define TB_CAPTION_RESUME           0x10E80E9Du
#define CREATEWINDOWEXA_IAT_SLOT    0x11AF23E0u

static constexpr const char* kIniSection = "AssetNames";
static constexpr const char* kIniKey = "ShowEnglish";
static constexpr UINT_PTR kListSubclassId = 0x414E; // 'AN'
static constexpr UINT_PTR kRootSubclassId = 0x414F;

static AssetNames::Dictionary g_Dictionary;
static bool g_Enabled = true;
static std::mutex g_CacheLock;
static std::unordered_map<std::string, std::string> g_GlossCache;
static std::unordered_set<HWND> g_Attached;
static std::unordered_set<HWND> g_AttachedRoots;

// ---- Files and option -----------------------------------------------------

static std::string AN_SystemDirectory()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash)
        slash[1] = '\0';
    return path;
}

static std::string AN_IniPath() { return AN_SystemDirectory() + "Reloaded_Editor.ini"; }

static std::string AN_UserDictionaryPath()
{
    return AN_SystemDirectory() + "ReloadedEditor\\Translations\\fr-en.txt";
}

static const char kUserTemplate[] =
    "# Your own English for French asset names (RE+). Entries here win over the\r\n"
    "# built-in dictionary; restart the editor after editing.\r\n"
    "#\r\n"
    "#   french = english     a word, without accents, in any case\r\n"
    "#   word =               (nothing after =) stops a built-in translation\r\n"
    "#\r\n"
    "# Section headers set the kind of the words below them:\r\n"
    "#   [nouns]              a trailing s or x makes a plural (the default)\r\n"
    "#   [adjectives]         plurals keep the English as it is\r\n"
    "#   [context]            words that are also English; translated only when\r\n"
    "#                        another French word is in the same name\r\n"
    "#   [english]            English words (one per line) that may be glued to\r\n"
    "#                        French ones, as in BetonWall\r\n"
    "\r\n"
    "[nouns]\r\n";

static void AN_LoadDictionaries()
{
    g_Dictionary.Clear();
    g_Dictionary.Load(AssetNames::BuiltInDictionaryText());
    const size_t builtIn = g_Dictionary.Translations();

    const std::string user = AN_UserDictionaryPath();
    std::ifstream in(user, std::ios::binary);
    if (in)
    {
        std::stringstream text;
        text << in.rdbuf();
        g_Dictionary.Load(text.str());
    }
    else
    {
        // Leave a commented template where users will look for it.
        const std::string folder = AN_SystemDirectory() + "ReloadedEditor";
        CreateDirectoryA(folder.c_str(), nullptr);
        CreateDirectoryA((folder + "\\Translations").c_str(), nullptr);
        std::ofstream out(user, std::ios::binary);
        if (out)
            out << kUserTemplate;
    }
    char message[160];
    snprintf(message, sizeof(message),
             "AssetNameGloss: %zu built-in translations, %zu with the user file",
             builtIn, g_Dictionary.Translations());
    Logger::log(message);
}

bool AssetNameGloss::Enabled() { return g_Enabled; }

static BOOL CALLBACK AN_RepaintThreadWindow(HWND window, LPARAM)
{
    RedrawWindow(window, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    return TRUE;
}

void AssetNameGloss::SetEnabled(bool enabled)
{
    g_Enabled = enabled;
    WritePrivateProfileStringA(kIniSection, kIniKey, enabled ? "1" : "0",
                               AN_IniPath().c_str());
    // Lists, combo faces and the browsers' own windows (the Texture Browser
    // redraws its thumbnails and caption on its next paint).
    EnumThreadWindows(GetCurrentThreadId(), AN_RepaintThreadWindow, 0);
}

// ---- Glosses --------------------------------------------------------------

static std::string AN_GlossCached(const char* name)
{
    if (!name || !*name)
        return {};
    std::lock_guard<std::mutex> lock(g_CacheLock);
    const auto it = g_GlossCache.find(name);
    if (it != g_GlossCache.end())
        return it->second;
    if (g_GlossCache.size() > 200000)
        g_GlossCache.clear();
    std::string gloss = AssetNames::Gloss(g_Dictionary, name);
    g_GlossCache.emplace(name, gloss);
    return gloss;
}

std::string AssetNameGloss::GlossFor(const char* name)
{
    return g_Enabled ? AN_GlossCached(name) : std::string();
}

std::string AssetNameGloss::LabelFor(const char* name)
{
    return AssetNames::Label(name ? name : "", GlossFor(name));
}

bool AssetNameGloss::MatchesQuery(const char* name, const char* query)
{
    if (!name)
        return false;
    if (!query)
        return true;
    return AssetNames::MatchesGloss(name, GlossFor(name), query);
}

// ---- Lists ----------------------------------------------------------------

namespace
{
enum class ListKind { None, ListBox, ListView, TreeView, ComboBox };

ListKind AN_KindOf(HWND window)
{
    // The editor's own controls are superclasses named after their base:
    // SplinterCell2UnrealWListView, ...WTreeView, ...WComboBox, ...WListBox.
    char name[96] = {};
    if (!GetClassNameA(window, name, static_cast<int>(sizeof(name))))
        return ListKind::None;
    auto endsWith = [&](const char* suffix) {
        const size_t length = strlen(name), suffixLength = strlen(suffix);
        return length >= suffixLength && _stricmp(name + length - suffixLength, suffix) == 0;
    };
    if (endsWith("CheckListBox")) // owner-drawn check boxes (the Group browser)
        return ListKind::None;
    if (endsWith("ListBox") || _stricmp(name, "ComboLBox") == 0)
        return ListKind::ListBox;
    if (endsWith("ListView") || _stricmp(name, WC_LISTVIEWA) == 0)
        return ListKind::ListView;
    if (endsWith("TreeView") || _stricmp(name, WC_TREEVIEWA) == 0)
        return ListKind::TreeView;
    if (endsWith("ComboBox"))
        return ListKind::ComboBox;
    return ListKind::None;
}

COLORREF AN_Blend(COLORREF a, COLORREF b)
{
    return RGB((GetRValue(a) + GetRValue(b)) / 2, (GetGValue(a) + GetGValue(b)) / 2,
               (GetBValue(a) + GetBValue(b)) / 2);
}

// Draws " (gloss)" starting at x on the row [top, bottom), clipped to right.
void AN_DrawGloss(HDC dc, const std::string& gloss, int x, int top, int bottom, int right,
                  bool selected, COLORREF background)
{
    if (gloss.empty() || x >= right)
        return;
    const std::string text = "(" + gloss + ")";
    SIZE size = {};
    GetTextExtentPoint32A(dc, text.c_str(), static_cast<int>(text.size()), &size);
    RECT box = {x, top, x + size.cx + 4, bottom};
    if (box.right > right)
        box.right = right;
    const COLORREF back = selected ? GetSysColor(COLOR_HIGHLIGHT) : background;
    const COLORREF fore = selected ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                                   : AN_Blend(GetSysColor(COLOR_GRAYTEXT), background);
    SetBkColor(dc, back);
    SetTextColor(dc, fore);
    const int y = top + ((bottom - top) - size.cy) / 2;
    ExtTextOutA(dc, x + 2, y, ETO_OPAQUE | ETO_CLIPPED, &box, text.c_str(),
                static_cast<UINT>(text.size()), nullptr);
}

int AN_TextWidth(HDC dc, const char* text)
{
    SIZE size = {};
    GetTextExtentPoint32A(dc, text, static_cast<int>(strlen(text)), &size);
    return size.cx;
}

void AN_PaintListBox(HWND list, HDC dc, const RECT& client)
{
    const LONG style = GetWindowLongA(list, GWL_STYLE);
    if ((style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) && !(style & LBS_HASSTRINGS))
        return;
    const int count = static_cast<int>(SendMessageA(list, LB_GETCOUNT, 0, 0));
    const COLORREF background = GetSysColor(COLOR_WINDOW);
    for (int i = static_cast<int>(SendMessageA(list, LB_GETTOPINDEX, 0, 0)); i >= 0 && i < count; ++i)
    {
        RECT row = {};
        if (SendMessageA(list, LB_GETITEMRECT, i, reinterpret_cast<LPARAM>(&row)) == LB_ERR)
            break;
        if (row.top >= client.bottom)
            break;
        const int length = static_cast<int>(SendMessageA(list, LB_GETTEXTLEN, i, 0));
        if (length <= 0 || length > 1024)
            continue;
        std::string text(static_cast<size_t>(length) + 1, '\0');
        SendMessageA(list, LB_GETTEXT, i, reinterpret_cast<LPARAM>(text.data()));
        text.resize(strlen(text.c_str()));
        const std::string gloss = AN_GlossCached(text.c_str());
        if (gloss.empty())
            continue;
        const bool selected = SendMessageA(list, LB_GETSEL, i, 0) > 0;
        AN_DrawGloss(dc, gloss, row.left + 2 + AN_TextWidth(dc, text.c_str()) + 4, row.top,
                     row.bottom, client.right, selected, background);
    }
}

void AN_PaintListView(HWND list, HDC dc, const RECT& client)
{
    const LONG view = GetWindowLongA(list, GWL_STYLE) & LVS_TYPEMASK;
    if (view != LVS_REPORT && view != LVS_LIST)
        return;
    const int count = ListView_GetItemCount(list);
    auto usable = [](COLORREF color) { return color != CLR_NONE && color != CLR_DEFAULT; };
    COLORREF background = ListView_GetTextBkColor(list);
    if (!usable(background))
        background = ListView_GetBkColor(list);
    if (!usable(background))
        background = GetSysColor(COLOR_WINDOW);
    const HWND header = ListView_GetHeader(list);
    const int columns = header ? Header_GetItemCount(header) : 0;
    int top = ListView_GetTopIndex(list);
    if (top < 0)
        top = 0;
    char text[512];
    for (int i = top; i < count; ++i)
    {
        RECT label = {};
        label.left = LVIR_LABEL;
        if (!SendMessageA(list, LVM_GETITEMRECT, i, reinterpret_cast<LPARAM>(&label)))
            break;
        if (label.top >= client.bottom || label.left >= client.right)
            break;
        if (label.bottom <= client.top)
            continue;
        LVITEMA item = {};
        item.iSubItem = 0;
        item.pszText = text;
        item.cchTextMax = static_cast<int>(sizeof(text));
        text[0] = '\0';
        SendMessageA(list, LVM_GETITEMTEXTA, i, reinterpret_cast<LPARAM>(&item));
        const std::string gloss = AN_GlossCached(text);
        if (gloss.empty())
            continue;
        const int width = static_cast<int>(SendMessageA(list, LVM_GETSTRINGWIDTHA, 0,
                                                        reinterpret_cast<LPARAM>(text)));
        // Report view: stay inside the first column when others follow it;
        // list view: inside the item's own column.
        int right = client.right;
        if (view == LVS_LIST || columns > 1)
            right = label.right;
        AN_DrawGloss(dc, gloss, label.left + width + 8, label.top, label.bottom, right, false,
                     background);
    }
}

void AN_PaintTreeView(HWND tree, HDC dc, const RECT& client)
{
    COLORREF background = TreeView_GetBkColor(tree);
    if (background == static_cast<COLORREF>(-1) || background == CLR_DEFAULT)
        background = GetSysColor(COLOR_WINDOW);
    char text[512];
    for (HTREEITEM item = TreeView_GetFirstVisible(tree); item;
         item = TreeView_GetNextVisible(tree, item))
    {
        RECT row = {};
        *reinterpret_cast<HTREEITEM*>(&row) = item;
        if (!SendMessageA(tree, TVM_GETITEMRECT, TRUE, reinterpret_cast<LPARAM>(&row)))
            break;
        if (row.top >= client.bottom)
            break;
        TVITEMA data = {};
        data.mask = TVIF_TEXT | TVIF_HANDLE;
        data.hItem = item;
        data.pszText = text;
        data.cchTextMax = static_cast<int>(sizeof(text));
        text[0] = '\0';
        if (!SendMessageA(tree, TVM_GETITEMA, 0, reinterpret_cast<LPARAM>(&data)))
            continue;
        const std::string gloss = AN_GlossCached(text);
        if (!gloss.empty())
            AN_DrawGloss(dc, gloss, row.right + 2, row.top, row.bottom, client.right, false,
                         background);
    }
}

void AN_PaintComboFace(HWND combo, HDC dc, const RECT& client)
{
    const LONG style = GetWindowLongA(combo, GWL_STYLE);
    if ((style & 0x3) != CBS_DROPDOWNLIST || (style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)))
        return;
    const int selection = static_cast<int>(SendMessageA(combo, CB_GETCURSEL, 0, 0));
    if (selection < 0)
        return;
    const int length = static_cast<int>(SendMessageA(combo, CB_GETLBTEXTLEN, selection, 0));
    if (length <= 0 || length > 1024)
        return;
    std::string text(static_cast<size_t>(length) + 1, '\0');
    SendMessageA(combo, CB_GETLBTEXT, selection, reinterpret_cast<LPARAM>(text.data()));
    text.resize(strlen(text.c_str()));
    const std::string gloss = AN_GlossCached(text.c_str());
    if (gloss.empty())
        return;
    COMBOBOXINFO info = {sizeof(info)};
    if (!GetComboBoxInfo(combo, &info))
        return;
    // The face is highlighted while the combo has the focus.
    const bool focused = GetFocus() == combo;
    AN_DrawGloss(dc, gloss, info.rcItem.left + 2 + AN_TextWidth(dc, text.c_str()) + 4,
                 info.rcItem.top, info.rcItem.bottom, info.rcItem.right, focused,
                 GetSysColor(IsWindowEnabled(combo) ? COLOR_WINDOW : COLOR_BTNFACE));
    (void)client;
}

void AN_PaintGlosses(HWND window, ListKind kind)
{
    if (!g_Enabled || !IsWindowVisible(window))
        return;
    HDC dc = GetDC(window);
    if (!dc)
        return;
    RECT client = {};
    GetClientRect(window, &client);
    HFONT font = reinterpret_cast<HFONT>(SendMessageA(window, WM_GETFONT, 0, 0));
    HGDIOBJ previousFont = font ? SelectObject(dc, font) : nullptr;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, client.left, client.top, client.right, client.bottom);
    switch (kind)
    {
    case ListKind::ListBox: AN_PaintListBox(window, dc, client); break;
    case ListKind::ListView: AN_PaintListView(window, dc, client); break;
    case ListKind::TreeView: AN_PaintTreeView(window, dc, client); break;
    case ListKind::ComboBox: AN_PaintComboFace(window, dc, client); break;
    default: break;
    }
    RestoreDC(dc, saved);
    if (previousFont)
        SelectObject(dc, previousFont);
    ReleaseDC(window, dc);
}

// Messages after which the control may have drawn rows itself (selection,
// scrolling, focus) instead of going through WM_PAINT.
bool AN_RepaintsRows(UINT message)
{
    switch (message)
    {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_KEYDOWN: case WM_KEYUP:
    case WM_MOUSEWHEEL: case WM_VSCROLL: case WM_HSCROLL:
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE:
    case LB_SETCURSEL: case LB_SETSEL: case LB_SETTOPINDEX: case LB_SELECTSTRING:
    case CB_SETCURSEL: case CB_SELECTSTRING:
    case LVM_SETITEMSTATE: case LVM_ENSUREVISIBLE: case LVM_SCROLL:
    case TVM_SELECTITEM: case TVM_ENSUREVISIBLE: case TVM_EXPAND:
        return true;
    case WM_MOUSEMOVE:
        return true;
    default:
        return false;
    }
}

// ---- Hover tooltips: the whole "Name (gloss)" when a list is too narrow to
// show it (the Static Mesh Browser's list pane starts narrow).

constexpr UINT_PTR kHoverTimer = 0x414E01, kLeaveTimer = 0x414E02;
HWND g_Tip = nullptr;
HWND g_TipList = nullptr;     // the list the tip is shown for, or armed on
LONG_PTR g_TipItem = -1;      // row index, or HTREEITEM
char g_TipText[640] = {};

// The row under a client point and its text; -1 when none.
LONG_PTR AN_RowAt(HWND window, ListKind kind, POINT point, std::string& text)
{
    char buffer[512] = {};
    switch (kind)
    {
    case ListKind::ListBox:
    {
        const LRESULT hit = SendMessageA(window, LB_ITEMFROMPOINT, 0, MAKELPARAM(point.x, point.y));
        if (HIWORD(hit))
            return -1;
        const int index = LOWORD(hit);
        const int length = static_cast<int>(SendMessageA(window, LB_GETTEXTLEN, index, 0));
        if (length <= 0 || length >= static_cast<int>(sizeof(buffer)))
            return -1;
        SendMessageA(window, LB_GETTEXT, index, reinterpret_cast<LPARAM>(buffer));
        text = buffer;
        return index;
    }
    case ListKind::ListView:
    {
        LVHITTESTINFO hit = {};
        hit.pt = point;
        const int index = static_cast<int>(SendMessageA(window, LVM_HITTEST, 0, reinterpret_cast<LPARAM>(&hit)));
        if (index < 0)
            return -1;
        LVITEMA item = {};
        item.pszText = buffer;
        item.cchTextMax = static_cast<int>(sizeof(buffer));
        SendMessageA(window, LVM_GETITEMTEXTA, index, reinterpret_cast<LPARAM>(&item));
        text = buffer;
        return index;
    }
    case ListKind::TreeView:
    {
        TVHITTESTINFO hit = {};
        hit.pt = point;
        HTREEITEM item = reinterpret_cast<HTREEITEM>(
            SendMessageA(window, TVM_HITTEST, 0, reinterpret_cast<LPARAM>(&hit)));
        if (!item || !(hit.flags & TVHT_ONITEM))
            return -1;
        TVITEMA data = {};
        data.mask = TVIF_TEXT | TVIF_HANDLE;
        data.hItem = item;
        data.pszText = buffer;
        data.cchTextMax = static_cast<int>(sizeof(buffer));
        SendMessageA(window, TVM_GETITEMA, 0, reinterpret_cast<LPARAM>(&data));
        text = buffer;
        return reinterpret_cast<LONG_PTR>(item);
    }
    default:
        return -1;
    }
}

TTTOOLINFOA AN_TipInfo()
{
    TTTOOLINFOA info = {};
    info.cbSize = TTTOOLINFOA_V2_SIZE;
    info.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    info.hwnd = g_Tip;
    info.uId = 1;
    info.lpszText = g_TipText;
    return info;
}

void AN_HideTip()
{
    if (g_Tip && g_TipList)
    {
        TTTOOLINFOA info = AN_TipInfo();
        SendMessageA(g_Tip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&info));
    }
    if (g_TipList && IsWindow(g_TipList))
    {
        KillTimer(g_TipList, kHoverTimer);
        KillTimer(g_TipList, kLeaveTimer);
    }
    g_TipList = nullptr;
    g_TipItem = -1;
}

void AN_ShowTip(HWND window, const std::string& text)
{
    if (!g_Tip || !IsWindow(g_Tip))
    {
        g_Tip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, nullptr,
                                WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
                                CW_USEDEFAULT, CW_USEDEFAULT, GetAncestor(window, GA_ROOT), nullptr,
                                GetModuleHandleA(nullptr), nullptr);
        if (!g_Tip)
            return;
        g_TipText[0] = '\0';
        TTTOOLINFOA info = AN_TipInfo();
        SendMessageA(g_Tip, TTM_ADDTOOLA, 0, reinterpret_cast<LPARAM>(&info));
        SendMessageA(g_Tip, TTM_SETMAXTIPWIDTH, 0, 600);
    }
    strncpy_s(g_TipText, text.c_str(), _TRUNCATE);
    TTTOOLINFOA info = AN_TipInfo();
    SendMessageA(g_Tip, TTM_UPDATETIPTEXTA, 0, reinterpret_cast<LPARAM>(&info));
    POINT cursor = {};
    GetCursorPos(&cursor);
    SendMessageA(g_Tip, TTM_TRACKPOSITION, 0, MAKELPARAM(cursor.x + 12, cursor.y + 20));
    SendMessageA(g_Tip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&info));
}

// Returns true when the message was one of the tooltip's own timers.
bool AN_HandleHover(HWND window, ListKind kind, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (kind == ListKind::ComboBox)
        return false;
    if (message == WM_MOUSEMOVE)
    {
        if (!g_Enabled)
            return false;
        POINT point = {static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
        std::string text;
        const LONG_PTR row = AN_RowAt(window, kind, point, text);
        if (window != g_TipList || row != g_TipItem)
        {
            AN_HideTip();
            if (row != -1 && !AN_GlossCached(text.c_str()).empty())
            {
                g_TipList = window;
                g_TipItem = row;
                SetTimer(window, kHoverTimer, 500, nullptr);
            }
        }
        return false;
    }
    if (message == WM_MOUSELEAVE || message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
        message == WM_MOUSEWHEEL || message == WM_KEYDOWN || message == WM_VSCROLL)
    {
        if (window == g_TipList)
            AN_HideTip();
        return false;
    }
    if (message != WM_TIMER || (wParam != kHoverTimer && wParam != kLeaveTimer))
        return false;
    POINT cursor = {};
    GetCursorPos(&cursor);
    POINT point = cursor;
    ScreenToClient(window, &point);
    std::string text;
    const bool still = window == g_TipList && WindowFromPoint(cursor) == window &&
                       AN_RowAt(window, kind, point, text) == g_TipItem;
    if (!still)
    {
        AN_HideTip();
        return true;
    }
    if (wParam == kHoverTimer)
    {
        KillTimer(window, kHoverTimer);
        const std::string gloss = AN_GlossCached(text.c_str());
        if (!gloss.empty())
        {
            AN_ShowTip(window, AssetNames::Label(text, gloss));
            SetTimer(window, kLeaveTimer, 150, nullptr);
        }
    }
    return true;
}

LRESULT CALLBACK AN_ListSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR, DWORD_PTR data)
{
    if (message == WM_NCDESTROY)
    {
        if (window == g_TipList)
            AN_HideTip();
        g_Attached.erase(window);
        RemoveWindowSubclass(window, AN_ListSubclassProc, kListSubclassId);
        return DefSubclassProc(window, message, wParam, lParam);
    }
    if (AN_HandleHover(window, static_cast<ListKind>(data), message, wParam, lParam))
        return 0;
    const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    const ListKind kind = static_cast<ListKind>(data);
    if (message == WM_PAINT || message == WM_PRINTCLIENT ||
        (AN_RepaintsRows(message) && (message != WM_MOUSEMOVE || (wParam & MK_LBUTTON))))
        AN_PaintGlosses(window, kind);
    return result;
}

void AN_AttachOne(HWND window)
{
    const ListKind kind = AN_KindOf(window);
    if (kind == ListKind::None)
        return;
    if (kind == ListKind::ComboBox)
    {
        COMBOBOXINFO info = {sizeof(info)};
        if (GetComboBoxInfo(window, &info) && info.hwndList)
            AN_AttachOne(info.hwndList);
    }
    if (!g_Attached.insert(window).second)
        return;
    SetWindowSubclass(window, AN_ListSubclassProc, kListSubclassId,
                      static_cast<DWORD_PTR>(kind));
    InvalidateRect(window, nullptr, FALSE);
}

BOOL CALLBACK AN_AttachChild(HWND child, LPARAM)
{
    AN_AttachOne(child);
    return TRUE;
}

// Browser windows: lists created after the first attach are picked up when
// the browser is created, resized or shown.
LRESULT CALLBACK AN_RootSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR, DWORD_PTR)
{
    if (message == WM_NCDESTROY)
    {
        g_AttachedRoots.erase(window);
        RemoveWindowSubclass(window, AN_RootSubclassProc, kRootSubclassId);
        return DefSubclassProc(window, message, wParam, lParam);
    }
    const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    if ((message == WM_PARENTNOTIFY && LOWORD(wParam) == WM_CREATE) || message == WM_SIZE ||
        message == WM_SHOWWINDOW)
        EnumChildWindows(window, AN_AttachChild, 0);
    return result;
}
} // namespace

void AssetNameGloss::AttachList(HWND control)
{
    if (control && IsWindow(control))
        AN_AttachOne(control);
}

void AssetNameGloss::AttachBrowser(HWND window)
{
    if (!window || !IsWindow(window))
        return;
    if (g_AttachedRoots.insert(window).second)
        SetWindowSubclass(window, AN_RootSubclassProc, kRootSubclassId, 0);
    EnumChildWindows(window, AN_AttachChild, 0);
}

// ---- Texture Browser ------------------------------------------------------

namespace
{
struct FStringRaw
{
    char* data;
    int num;
    int max;
};

typedef void* (__thiscall* GMallocReallocFn)(void* self, void* pointer, DWORD size, const char* tag);

bool AN_ReadObjectName(void* object, char* out, size_t size)
{
    out[0] = '\0';
    __try
    {
        void** names = *reinterpret_cast<void***>(GNAMES_DATA_PTR);
        const int count = *reinterpret_cast<int*>(GNAMES_NUM_PTR);
        const int index = *reinterpret_cast<int*>(static_cast<char*>(object) + UOBJECT_FNAME_OFFSET);
        if (!names || index < 0 || index >= count || !names[index])
            return false;
        strncpy_s(out, size,
                  static_cast<const char*>(names[index]) + FNAME_ENTRY_STR_OFFSET, _TRUNCATE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out[0] = '\0';
    }
    return out[0] != '\0';
}

// Replaces an engine FString's text, growing it through the engine's GMalloc.
bool AN_SetFString(FStringRaw* target, const std::string& text)
{
    __try
    {
        void* allocator = *reinterpret_cast<void**>(GMALLOC_PTR);
        if (!allocator)
            return false;
        const int needed = static_cast<int>(text.size()) + 1;
        if (needed > target->max)
        {
            auto reallocate = reinterpret_cast<GMallocReallocFn>((*reinterpret_cast<void***>(allocator))[1]);
            void* grown = reallocate(allocator, target->data, static_cast<DWORD>(needed), "FString");
            if (!grown)
                return false;
            target->data = static_cast<char*>(grown);
            target->max = needed;
        }
        memcpy(target->data, text.c_str(), static_cast<size_t>(needed));
        target->num = needed;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Inserts " (gloss)" after the last whole occurrence of the object's name
// that is followed by the end or a space ("Texture Porte_01* [DXT1]",
// "Texture Pkg.Grp.Porte_01 (256x256)").
void AN_InsertGloss(FStringRaw* text, void* object)
{
    if (!g_Enabled || !text || !object || !text->data || text->num <= 1)
        return;
    char name[256];
    if (!AN_ReadObjectName(object, name, sizeof(name)))
        return;
    const std::string gloss = AN_GlossCached(name);
    if (gloss.empty())
        return;
    const std::string current(text->data);
    const std::string insert = " (" + gloss + ")";
    if (current.find(insert) != std::string::npos)
        return;
    size_t at = std::string::npos;
    for (size_t pos = current.find(name); pos != std::string::npos; pos = current.find(name, pos + 1))
    {
        size_t end = pos + strlen(name);
        if (end < current.size() && current[end] == '*') // "*": not saved yet
            ++end;
        if (end == current.size() || current[end] == ' ')
            at = end;
    }
    if (at == std::string::npos)
        at = current.size();
    AN_SetFString(text, current.substr(0, at) + insert + current.substr(at));
}
} // namespace

static const char* __cdecl AN_TextureFilterMatch(const char* nameCaps, const char* filterCaps,
                                                 const char* name)
{
    const char* found = nameCaps && filterCaps ? strstr(nameCaps, filterCaps) : nullptr;
    if (found || !g_Enabled || !name || !filterCaps || !*filterCaps)
        return found;
    return AssetNameGloss::MatchesQuery(name, filterCaps) ? nameCaps : nullptr;
}

static void __cdecl AN_TextureLabel(FStringRaw* label, void* material)
{
    AN_InsertGloss(label, material);
}

static void __cdecl AN_TextureCaption(FStringRaw* caption)
{
    void* material = nullptr;
    __try
    {
        void* editor = *reinterpret_cast<void**>(GEDITOR_PTR);
        if (editor)
            material = *reinterpret_cast<void**>(static_cast<char*>(editor) + GEDITOR_CURRENT_MATERIAL);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        material = nullptr;
    }
    AN_InsertGloss(caption, material);
}

// appStrstr(NameCaps, FilterCaps) inside the Texture Browser's filter test.
CALL_HOOK(TB_FILTER_STRSTR_CALL, AN_TextureFilterStrstr)
{
    __asm
    {
        mov  eax, dword ptr [esp + 0x14]   // the name's FString data (caller's [esp+8])
        push eax
        push dword ptr [esp + 0x0C]        // FilterCaps
        push dword ptr [esp + 0x0C]        // NameCaps
        call AN_TextureFilterMatch
        add  esp, 12
        ret
    }
}

// Displaced: mov eax,[ebp-1Ch] / mov ecx,[ebp-20h] (the label's Num and Data).
JMP_HOOK(TB_LABEL_READ, AN_TextureLabelHook)
{
    static int resume = static_cast<int>(TB_LABEL_RESUME);
    __asm
    {
        pushad
        push dword ptr [ebp - 0x4C]        // the material
        lea  eax, [ebp - 0x20]             // its label
        push eax
        call AN_TextureLabel
        add  esp, 8
        popad
        mov  eax, dword ptr [ebp - 0x1C]
        mov  ecx, dword ptr [ebp - 0x20]
        jmp  dword ptr [resume]
    }
}

// Displaced: mov ecx,[ebp-14h] / lea edx,[ebp-28h] before WBrowser::SetCaption.
JMP_HOOK(TB_CAPTION_SET, AN_TextureCaptionHook)
{
    static int resume = static_cast<int>(TB_CAPTION_RESUME);
    __asm
    {
        pushad
        lea  eax, [ebp - 0x28]
        push eax
        call AN_TextureCaption
        add  esp, 4
        popad
        mov  ecx, dword ptr [ebp - 0x14]
        lea  edx, [ebp - 0x28]
        jmp  dword ptr [resume]
    }
}

// The browser window that holds the Texture, Static Mesh, Sound, Actor Class
// and other stock browsers: every list in it gets glosses.
typedef HWND(WINAPI* CreateWindowExAFn)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND,
                                        HMENU, HINSTANCE, LPVOID);
static CreateWindowExAFn g_PreviousCreateWindowExA = nullptr;

static HWND WINAPI AN_CreateWindowExA_Hook(DWORD exStyle, LPCSTR className, LPCSTR windowName,
                                           DWORD style, int x, int y, int width, int height,
                                           HWND parent, HMENU menu, HINSTANCE instance,
                                           LPVOID parameter)
{
    HWND window = g_PreviousCreateWindowExA
        ? g_PreviousCreateWindowExA(exStyle, className, windowName, style, x, y, width, height,
                                    parent, menu, instance, parameter)
        : CreateWindowExA(exStyle, className, windowName, style, x, y, width, height, parent,
                          menu, instance, parameter);
    char name[96] = {};
    if (window && GetClassNameA(window, name, static_cast<int>(sizeof(name))))
    {
        const size_t length = strlen(name);
        static const char kMaster[] = "WBrowserMaster";
        if (length >= sizeof(kMaster) - 1 && _stricmp(name + length - (sizeof(kMaster) - 1), kMaster) == 0)
            AssetNameGloss::AttachBrowser(window);
    }
    return window;
}

void AssetNameGloss::Initialize()
{
    g_Enabled = GetPrivateProfileIntA(kIniSection, kIniKey, 1, AN_IniPath().c_str()) != 0;
    AN_LoadDictionaries();

    // Chained after the other browsers' hooks of the same import.
    g_PreviousCreateWindowExA = *reinterpret_cast<CreateWindowExAFn*>(CREATEWINDOWEXA_IAT_SLOT);
    const uintptr_t createWindowHook = reinterpret_cast<uintptr_t>(AN_CreateWindowExA_Hook);
    MemoryWriter::WriteBytes(CREATEWINDOWEXA_IAT_SLOT, &createWindowHook, sizeof(createWindowHook));

    // Only patch the build these addresses were read from.
    static const unsigned char kFilterCall[] = {0xE8};
    static const unsigned char kLabelRead[] = {0x8B, 0x45, 0xE4, 0x8B, 0x4D, 0xE0};
    static const unsigned char kCaptionSet[] = {0x8B, 0x4D, 0xEC, 0x8D, 0x55, 0xD8};
    const bool expected =
        memcmp(reinterpret_cast<void*>(TB_FILTER_STRSTR_CALL), kFilterCall, sizeof(kFilterCall)) == 0 &&
        *reinterpret_cast<int*>(TB_FILTER_STRSTR_CALL + 1) ==
            static_cast<int>(0x10FA35E0u - (TB_FILTER_STRSTR_CALL + 5)) &&
        memcmp(reinterpret_cast<void*>(TB_LABEL_READ), kLabelRead, sizeof(kLabelRead)) == 0 &&
        memcmp(reinterpret_cast<void*>(TB_CAPTION_SET), kCaptionSet, sizeof(kCaptionSet)) == 0;
    if (!expected)
    {
        Logger::log("AssetNameGloss: Texture Browser code differs; its labels and filter are left alone");
        return;
    }
    INSTALL_HOOKS;
}
