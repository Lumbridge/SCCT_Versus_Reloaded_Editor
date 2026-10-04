#include "pch.h"
#include "PropertySearch.h"
#include "PropertyFilterModel.h"
#include "Hooks.h"
#include "MemoryWriter.h"
#include "logger.h"
#include <commctrl.h>
#include <cstring>
#include <set>
#include <string>
#include <vector>
#pragma comment(lib, "comctl32.lib")

INIT_HOOKS;

// The stock Properties window is UnrealEd's WObjectProperties: a WProperties
// whose list is an owner-drawn list box (class WItemBox) of FTreeItem rows,
// FCategoryItem under the root FObjectsItem and FPropertyItem under those, as
// in Window's Properties.cpp. Verified against the supported executable:
//   FTreeItem   +0x08 OwnerProperties, +0x0c Parent, +0x10 Expandable,
//               +0x14 Expanded; vtable +0x54 Collapse, +0x58 Expand,
//               +0x6c GetReadAddress(Child, RequireSingleSelection) (null when
//               the selected objects disagree), +0x80 GetId (64-bit)
//   FCategoryItem +0x3c Category (FName);  FPropertyItem +0x3c Property
//   FObjectsItem  +0x58/+0x5c Objects (TArray<UObject*>)
//   WProperties vtable +0xe8 GetRoot, +0xf0 ResizeList, +0xf4 SetItemFocus,
//               +0xf8 ForceRefresh
namespace
{
    using Address = uintptr_t;
    constexpr Address kCategoryItemVtable = 0x11487968;
    constexpr Address kPropertyItemVtable = 0x114724d0;
    constexpr Address kObjectsItemVtable = 0x114607f0;
    constexpr Address kObjectPropertiesVtable = 0x114608c0;
    constexpr Address kNames = 0x1169CFBC, kNameCount = 0x1169CFC0;
    constexpr Address kCreateWindowExAImport = 0x11AF23E0;
    constexpr Address kEditPropertiesLabel = 0x11460cdc; // "Edit Properties"

    // UUnrealEdEngine::NotifyPreChange (FNotifyHook, Trans at this-0x100) and
    // the null-value exit of FPropertyItem::Draw.
    constexpr Address kNotifyPreChange = 0x10e1b6f0;
    constexpr Address kDrawValue = 0x10f95fc4;
    constexpr Address kEmptyChildren = 0x10f91f60; // FTreeItem::EmptyChildren

    constexpr int kFilterBar = 24;
    constexpr int kEditId = 0x52f0, kLabelId = 0x52f1;
    constexpr UINT_PTR kTimerId = 0x52f2;
    constexpr UINT_PTR kWindowSubclass = 0x52f3, kListSubclass = 0x52f4, kEditSubclass = 0x52f5;
    UINT attachMessage = 0, reapplyMessage = 0;

    template<class T> T At(Address a) { return *reinterpret_cast<T*>(a); }
    Address Slot(Address object, size_t offset) { return At<Address>(At<Address>(object) + offset); }
    void CallVoid(Address object, size_t offset) { reinterpret_cast<void(__thiscall*)(Address)>(Slot(object, offset))(object); }
    void CallVoid(Address object, size_t offset, int arg) { reinterpret_cast<void(__thiscall*)(Address, int)>(Slot(object, offset))(object, arg); }

    std::string NameText(int index)
    {
        if (index < 0 || index >= At<int>(kNameCount)) return {};
        const Address entry = At<Address>(At<Address>(kNames) + index * 4);
        return entry ? std::string(reinterpret_cast<const char*>(entry + 12)) : std::string{};
    }

    Address RootOf(Address item)
    {
        for (int guard = 0; item && guard < 64; ++guard)
        {
            const Address parent = At<Address>(item + 0x0c);
            if (!parent) return item;
            item = parent;
        }
        return 0;
    }

    // ---------------------------------------------------------------------
    // Undo. NotifyPreChange opens the "Edit Properties" transaction, but no
    // stock path marks the edited objects, so Undo restored nothing. Modify
    // every object the window edits inside that transaction, so one Undo
    // takes a value back from all of the selected actors.
    void ModifyEdited(Address source)
    {
        if (!source || At<Address>(source) != kObjectPropertiesVtable) return;
        const Address root = reinterpret_cast<Address(__thiscall*)(Address)>(Slot(source, 0xe8))(source);
        if (!root || At<Address>(root) != kObjectsItemVtable) return;
        const Address objects = At<Address>(root + 0x58);
        const int count = At<int>(root + 0x5c);
        if (count < 0 || count > 1000000 || (count && !objects)) return;
        for (int i = 0; i < count; ++i)
            if (const Address object = At<Address>(objects + i * 4)) CallVoid(object, 0x20); // UObject::Modify
    }
    void ModifyEditedGuarded(Address source)
    {
        __try { ModifyEdited(source); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // ---------------------------------------------------------------------
    // "(multiple values)": FPropertyItem::Draw leaves the value cell blank
    // when the item has no single value to read. Write the label into Draw's
    // own text buffer instead, for rows of a selection of several objects.
    int MultipleValuesText(char* buffer, Address item)
    {
        const Address root = RootOf(item);
        if (!root || At<Address>(root) != kObjectsItemVtable) return 0;
        if (!PropertyFilter::ShowsMultipleValues(At<int>(root + 0x5c), false)) return 0;
        strcpy_s(buffer, 0x1000, PropertyFilter::kMultipleValues);
        return 1;
    }
}

static void __cdecl PropertyPreChange(Address hook, Address source)
{
    const Address trans = At<Address>(hook - 0x100);
    reinterpret_cast<void(__thiscall*)(Address, const char*)>(Slot(trans, 0x64))(trans, reinterpret_cast<const char*>(kEditPropertiesLabel));
    ModifyEditedGuarded(source);
}

static int __cdecl PropertyMultipleValuesText(char* buffer, Address item)
{
    __try { return MultipleValuesText(buffer, item); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

JMP_HOOK(kNotifyPreChange, PropertyNotifyPreChange)
{
    __asm
    {
        mov  eax, dword ptr [esp + 4]
        push eax
        push ecx
        call PropertyPreChange
        add  esp, 8
        ret  4
    }
}

// esi is the FPropertyItem, [ebp-0x24] its read address, [ebp-0x1060] the
// text buffer the plain value path (0x10f9613d) draws into the value column.
JMP_HOOK(kDrawValue, PropertyDrawMultipleValues)
{
    static int s_hasValue = 0x10f95fcf;
    static int s_drawText = 0x10f9613d;
    static int s_noValue = 0x10f9618d;

    __asm
    {
        mov  eax, dword ptr [ebp - 0x24]
        test eax, eax
        jz   no_value
        jmp  dword ptr [s_hasValue]
    no_value:
        push esi
        lea  eax, [ebp - 0x1060]
        push eax
        call PropertyMultipleValuesText
        add  esp, 8
        test eax, eax
        jz   blank
        jmp  dword ptr [s_drawText]
    blank:
        jmp  dword ptr [s_noValue]
    }
}

namespace
{
    // ---------------------------------------------------------------------
    // The filter box.
    struct SearchState
    {
        HWND window = nullptr, list = nullptr, edit = nullptr, label = nullptr;
        Address owner = 0; // the WObjectProperties, known once the list has had a row
        bool applying = false, filtered = false;
        std::set<unsigned long long> expanded; // row ids expanded before filtering
    };

    unsigned long long RowId(Address item)
    {
        return reinterpret_cast<unsigned long long(__thiscall*)(Address)>(Slot(item, 0x80))(item);
    }
    Address RowItem(HWND list, int index)
    {
        const LRESULT data = SendMessageA(list, LB_GETITEMDATA, index, 0);
        return data == LB_ERR ? 0 : static_cast<Address>(data);
    }
    int RowCount(HWND list)
    {
        const LRESULT count = SendMessageA(list, LB_GETCOUNT, 0, 0);
        return count == LB_ERR ? 0 : static_cast<int>(count);
    }
    Address OwnerOf(SearchState& s)
    {
        const Address item = RowItem(s.list, 0);
        const Address owner = item ? At<Address>(item + 0x08) : 0;
        if (owner && At<Address>(owner) == kObjectPropertiesVtable) s.owner = owner;
        return s.owner;
    }

    // Every row back, categories closed: the core of WProperties::ForceRefresh
    // (List.Empty, Root->EmptyChildren, Root->Expand) without its expansion
    // memory. ForceRefresh itself left rows the filter had removed missing.
    bool Rebuild(SearchState& s, Address owner)
    {
        const Address root = reinterpret_cast<Address(__thiscall*)(Address)>(Slot(owner, 0xe8))(owner);
        if (!root || At<Address>(root) != kObjectsItemVtable) return false;
        SendMessageA(s.list, LB_RESETCONTENT, 0, 0);
        reinterpret_cast<void(__thiscall*)(Address)>(kEmptyChildren)(root);
        *reinterpret_cast<int*>(root + 0x14) = 0;
        CallVoid(root, 0x58);
        return true;
    }

    std::vector<PropertyFilter::Row> ReadRows(HWND list)
    {
        std::vector<PropertyFilter::Row> rows;
        const int count = RowCount(list);
        rows.reserve(count);
        for (int i = 0; i < count; ++i)
        {
            const Address item = RowItem(list, i);
            PropertyFilter::Row row;
            row.depth = 0;
            for (Address p = item ? At<Address>(item + 0x0c) : 0; p && row.depth < 64; p = At<Address>(p + 0x0c)) ++row.depth;
            const Address vtable = item ? At<Address>(item) : 0;
            if (vtable == kCategoryItemVtable) row.name = NameText(At<int>(item + 0x3c));
            else if (vtable == kPropertyItemVtable)
            {
                const Address property = At<Address>(item + 0x3c);
                if (property) row.name = NameText(At<int>(property + 0x20));
            }
            rows.push_back(std::move(row));
        }
        return rows;
    }

    void ExpandCategories(HWND list)
    {
        for (int i = 0; i < RowCount(list); ++i)
        {
            const Address item = RowItem(list, i);
            if (item && At<Address>(item) == kCategoryItemVtable && At<int>(item + 0x10) && !At<int>(item + 0x14))
                CallVoid(item, 0x58);
        }
    }

    void SaveExpansion(SearchState& s)
    {
        s.expanded.clear();
        for (int i = 0; i < RowCount(s.list); ++i)
        {
            const Address item = RowItem(s.list, i);
            if (item && At<int>(item + 0x14)) s.expanded.insert(RowId(item));
        }
    }

    // Back to the rows the window showed before filtering: categories the
    // filter opened close again and those that were open reopen.
    void RestoreExpansion(SearchState& s)
    {
        for (int i = 0; i < RowCount(s.list); ++i)
        {
            const Address item = RowItem(s.list, i);
            if (!item || !At<int>(item + 0x10)) continue;
            const bool wanted = s.expanded.count(RowId(item)) != 0, open = At<int>(item + 0x14) != 0;
            if (open && !wanted && At<Address>(item) == kCategoryItemVtable) CallVoid(item, 0x54);
            else if (!open && wanted) CallVoid(item, 0x58);
        }
    }

    void ApplyNative(SearchState& s, const std::string& filter, bool rebuild)
    {
        const Address owner = OwnerOf(s);
        if (!owner) return;
        CallVoid(owner, 0xf4, 0); // SetItemFocus(0) closes an open value editor
        if (PropertyFilter::Words(filter).empty())
        {
            if (Rebuild(s, owner)) RestoreExpansion(s);
            s.filtered = false;
        }
        else
        {
            if (!s.filtered) SaveExpansion(s);
            s.filtered = true;
            if (rebuild && !Rebuild(s, owner)) return;
            ExpandCategories(s.list);
            const auto keep = PropertyFilter::Visible(ReadRows(s.list), filter);
            for (int i = static_cast<int>(keep.size()) - 1; i >= 0; --i)
                if (!keep[i]) SendMessageA(s.list, LB_DELETESTRING, i, 0);
        }
        CallVoid(owner, 0xf0); // ResizeList to the rows that are left
    }
    bool ApplyGuarded(SearchState& s, const std::string& filter, bool rebuild)
    {
        __try { ApplyNative(s, filter, rebuild); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    std::string FilterText(const SearchState& s)
    {
        char text[256] = {};
        if (s.edit) GetWindowTextA(s.edit, text, static_cast<int>(sizeof(text)));
        return text;
    }

    void Apply(SearchState& s, bool rebuild)
    {
        if (s.applying || !IsWindow(s.list)) return;
        const std::string filter = FilterText(s);
        if (PropertyFilter::Words(filter).empty() && !s.filtered) return;
        s.applying = true;
        SendMessageA(s.list, WM_SETREDRAW, FALSE, 0);
        if (!ApplyGuarded(s, filter, rebuild))
            Logger::log("Property filter: the property list changed while filtering");
        SendMessageA(s.list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(s.list, nullptr, TRUE);
        s.applying = false;
    }

    // The native code places the list at the top of the window and sizes it
    // to its rows, leaving 27 pixels for a button under it when there is one.
    int ListLimit(const SearchState& s)
    {
        RECT client{};
        GetClientRect(s.window, &client);
        int limit = client.bottom;
        for (HWND child = GetWindow(s.window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        {
            char name[64] = {};
            GetClassNameA(child, name, static_cast<int>(sizeof(name)));
            if (child != s.edit && child != s.list && IsWindowVisible(child) && strstr(name, "WButton"))
            {
                limit = client.bottom - 0x1b;
                break;
            }
        }
        return limit;
    }

    void LayoutBar(const SearchState& s)
    {
        RECT client{};
        GetClientRect(s.window, &client);
        // The label is the whole bar, so it also paints over what the list
        // left there before it moved down; the window paints only below it.
        if (s.label) MoveWindow(s.label, 0, 0, client.right, kFilterBar, TRUE);
        if (s.edit) MoveWindow(s.edit, 40, 2, (std::max)(40, static_cast<int>(client.right) - 42), 20, TRUE);
    }

    LRESULT CALLBACK ListProc(HWND list, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
    {
        auto& s = *reinterpret_cast<SearchState*>(data);
        if (message == WM_WINDOWPOSCHANGING)
        {
            auto* pos = reinterpret_cast<WINDOWPOS*>(lParam);
            if (!(pos->flags & SWP_NOMOVE))
            {
                const int height = (pos->flags & SWP_NOSIZE) ? [&] { RECT r{}; GetWindowRect(list, &r); return static_cast<int>(r.bottom - r.top); }() : pos->cy;
                const auto placed = PropertyFilter::BelowBar(pos->y, height, kFilterBar, ListLimit(s));
                pos->y = placed.y;
                if (placed.height != height)
                {
                    if (pos->flags & SWP_NOSIZE)
                    {
                        RECT r{};
                        GetWindowRect(list, &r);
                        pos->cx = r.right - r.left;
                        pos->flags &= ~SWP_NOSIZE;
                    }
                    pos->cy = placed.height;
                }
            }
        }
        else if (message == LB_RESETCONTENT)
        {
            // The editor rebuilt the rows (a new selection, a refresh after an
            // edit): filter them again once it has finished.
            const LRESULT result = DefSubclassProc(list, message, wParam, lParam);
            if (!s.applying && !PropertyFilter::Words(FilterText(s)).empty()) PostMessageA(s.window, reapplyMessage, 0, 0);
            return result;
        }
        else if (message == WM_NCDESTROY) RemoveWindowSubclass(list, ListProc, kListSubclass);
        return DefSubclassProc(list, message, wParam, lParam);
    }

    LRESULT CALLBACK EditProc(HWND edit, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
    {
        auto& s = *reinterpret_cast<SearchState*>(data);
        if (message == WM_KEYDOWN && (wParam == VK_ESCAPE || wParam == VK_RETURN))
        {
            if (wParam == VK_ESCAPE) SetWindowTextA(edit, "");
            KillTimer(s.window, kTimerId);
            Apply(s, true);
            return 0;
        }
        if (message == WM_CHAR && (wParam == VK_ESCAPE || wParam == VK_RETURN)) return 0; // no beep
        if (message == WM_NCDESTROY) RemoveWindowSubclass(edit, EditProc, kEditSubclass);
        return DefSubclassProc(edit, message, wParam, lParam);
    }

    void Attach(SearchState& s)
    {
        if (s.edit) return;
        for (HWND child = GetWindow(s.window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        {
            char name[64] = {};
            GetClassNameA(child, name, static_cast<int>(sizeof(name)));
            if (strstr(name, "WItemBox")) { s.list = child; break; }
        }
        if (!s.list) return;
        const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrA(s.window, GWLP_HINSTANCE));
        const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        s.label = CreateWindowExA(0, "STATIC", " Filter:", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_CENTERIMAGE, 0, 0, 40, kFilterBar,
                                  s.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLabelId)), instance, nullptr);
        s.edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | ES_AUTOHSCROLL, 40, 2, 100, 20,
                                 s.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)), instance, nullptr);
        if (!s.edit) return;
        SetWindowPos(s.label, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); // under the edit box
        SendMessageA(s.label, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        SendMessageA(s.edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        SendMessageA(s.edit, EM_LIMITTEXT, 200, 0);
        SetWindowSubclass(s.edit, EditProc, kEditSubclass, reinterpret_cast<DWORD_PTR>(&s));
        SetWindowSubclass(s.list, ListProc, kListSubclass, reinterpret_cast<DWORD_PTR>(&s));
        LayoutBar(s);
        // Move the list under the bar now; the subclass keeps it there.
        RECT r{};
        GetWindowRect(s.list, &r);
        MapWindowPoints(nullptr, s.window, reinterpret_cast<POINT*>(&r), 2);
        SetWindowPos(s.list, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
    {
        auto* s = reinterpret_cast<SearchState*>(data);
        if (message == attachMessage) { Attach(*s); return 0; }
        if (message == reapplyMessage) { Apply(*s, false); return 0; }
        switch (message)
        {
        case WM_COMMAND:
            if (LOWORD(wParam) == kEditId && reinterpret_cast<HWND>(lParam) == s->edit)
            {
                if (HIWORD(wParam) == EN_CHANGE) SetTimer(window, kTimerId, 200, nullptr);
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lParam) == s->label)
            {
                SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
            break;
        case WM_TIMER:
            if (wParam == kTimerId)
            {
                KillTimer(window, kTimerId);
                Apply(*s, true);
                return 0;
            }
            break;
        case WM_SIZE:
        {
            const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
            LayoutBar(*s);
            return result;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(window, WindowProc, kWindowSubclass);
            delete s;
            break;
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }

    using CreateWindowExAFn = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
    CreateWindowExAFn previousCreateWindowExA = nullptr;

    // Every WObjectProperties window that stands on its own (not the one
    // embedded in the Static Mesh Browser) gets the filter bar once the
    // editor has finished building it.
    HWND WINAPI CreateWindowExAHook(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style, int x, int y, int width, int height,
                                    HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter)
    {
        const HWND window = previousCreateWindowExA(exStyle, className, windowName, style, x, y, width, height, parent, menu, instance, parameter);
        if (!window || (style & WS_CHILD)) return window;
        char name[96] = {};
        GetClassNameA(window, name, static_cast<int>(sizeof(name)));
        const size_t length = strlen(name), suffix = strlen("WObjectProperties");
        if (length < suffix || strcmp(name + length - suffix, "WObjectProperties") != 0) return window;
        auto* state = new SearchState;
        state->window = window;
        if (!SetWindowSubclass(window, WindowProc, kWindowSubclass, reinterpret_cast<DWORD_PTR>(state))) { delete state; return window; }
        PostMessageA(window, attachMessage, 0, 0);
        return window;
    }
}

void PropertySearch::Initialize()
{
    static const BYTE preChange[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x90, 0x02, 0x41, 0x11 };
    static const BYTE drawValue[] = { 0x8B, 0x45, 0xDC, 0x85, 0xC0, 0x0F, 0x84, 0xBE, 0x01, 0x00, 0x00 };
    if (memcmp(reinterpret_cast<const void*>(kNotifyPreChange), preChange, sizeof(preChange)) != 0
        || memcmp(reinterpret_cast<const void*>(kDrawValue), drawValue, sizeof(drawValue)) != 0)
    {
        Logger::log("Property filter: unsupported editor build, not installed");
        return;
    }
    INSTALL_HOOKS;

    attachMessage = RegisterWindowMessageA("ReloadedPropertySearchAttach");
    reapplyMessage = RegisterWindowMessageA("ReloadedPropertySearchReapply");
    // Chained with the Texture Browser's wrapper of the same import.
    previousCreateWindowExA = *reinterpret_cast<CreateWindowExAFn*>(kCreateWindowExAImport);
    const uintptr_t hook = reinterpret_cast<uintptr_t>(&CreateWindowExAHook);
    MemoryWriter::WriteBytes(kCreateWindowExAImport, &hook, sizeof(hook));
}
