#include "pch.h"
#undef min
#undef max
#include "UndoHistory.h"
#include "UndoHistoryModel.h"
#include "MemoryWriter.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

// The editor's undo buffer as a list. GEditor->Trans is a UTransBuffer; its
// layout in this build, from UTransBuffer::Begin (0x1105ad20), Undo
// (0x11059990), Redo (0x11059b40), CanUndo (0x11059870), Reset (0x110592a0)
// and the size check Begin makes before trimming (0x1105ae30):
//   +0x00 vtable 0x1148dd50 (Reset 0x60, Begin 0x64, End 0x68, CanUndo 0x70,
//         CanRedo 0x74, Undo 0x78, Redo 0x7c)
//   +0x28 TArray<FTransaction> UndoBuffer (data, Num +0x2c), oldest first
//   +0x34 UndoCount, +0x38 FString ResetReason, +0x44 ActiveCount,
//   +0x48 MaxMemory (bytes; Begin drops the oldest step while the records
//         hold more), +0x4c Overflow
// FTransaction is 0x28 bytes: TArray<FObjectRecord> Records at +0x08 (Num
// +0x0c), FString Title at +0x14. An FObjectRecord is 0x30 bytes and starts
// with its TArray<BYTE> Data, whose Num (+0x04) is what the size check adds up.
// The stock Edit > Undo / Redo (40019 / 40020) run GEditor->Exec("TRANSACTION
// UNDO" / "REDO", GLog); a step here does exactly that.
namespace UndoHistory
{
namespace
{
    using Address = uintptr_t;
    constexpr Address kEditor = 0x1165DFA0, kLog = 0x115BEFB0, kMainFrame = 0x1165df84;
    constexpr Address kTransBufferVtable = 0x1148dd50;
    constexpr int kTransactionSize = 0x28, kRecordSize = 0x30;
    constexpr UINT kTimer = 1, kTimerMs = 300, kChanged = WM_APP + 0x55;
    enum Id { List = 100, UndoButton, RedoButton, GoButton, CopyButton, Status };

    HWND window = nullptr;
    HFONT font = nullptr, bold = nullptr;
    int rowHeight = 18;
    Snapshot shown;
    bool available = false, busy = false;
    std::string report;

    bool CopySafe(void* to, const void* from, size_t size)
    {
        __try { memcpy(to, from, size); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    template<class T> T Read(Address address)
    {
        T value{};
        if (!address || !CopySafe(&value, reinterpret_cast<const void*>(address), sizeof(value)))
            throw std::runtime_error("The editor's undo buffer could not be read.");
        return value;
    }
    // An FString: data pointer, Num counting the terminator.
    std::string String(Address at, size_t cap = 1024)
    {
        const Address data = Read<Address>(at);
        const int count = Read<int>(at + 4);
        if (!data || count <= 1) return {};
        std::string text(std::min<size_t>(static_cast<size_t>(count - 1), cap), '\0');
        if (!CopySafe(text.data(), reinterpret_cast<const void*>(data), text.size()))
            throw std::runtime_error("The editor's undo buffer could not be read.");
        text.resize(strnlen(text.c_str(), text.size()));
        return text;
    }

    // The buffer tells the panel when it changes: End, Undo, Redo and Reset
    // post one coalesced message. Posted messages are not starved the way
    // WM_TIMER is while the frame works through a queue of its own posted
    // commands (thousands of edits in a row leave such a queue), so the timer
    // is only the fallback. The vtable slots are chained, as WorkflowEditor's
    // End observer chains whatever it finds.
    volatile LONG changePosted = 0;
    void Changed()
    {
        if (window && !InterlockedExchange(&changePosted, 1) && !PostMessageA(window, kChanged, 0, 0))
            InterlockedExchange(&changePosted, 0);
    }
    void* previousEnd = nullptr;
    void* previousUndo = nullptr;
    void* previousRedo = nullptr;
    void* previousReset = nullptr;
    void __fastcall HookEnd(void* self, void*) { reinterpret_cast<void(__thiscall*)(void*)>(previousEnd)(self); Changed(); }
    int __fastcall HookUndo(void* self, void*) { const int r = reinterpret_cast<int(__thiscall*)(void*)>(previousUndo)(self); Changed(); return r; }
    int __fastcall HookRedo(void* self, void*) { const int r = reinterpret_cast<int(__thiscall*)(void*)>(previousRedo)(self); Changed(); return r; }
    void __fastcall HookReset(void* self, void*, const char* reason) { reinterpret_cast<void(__thiscall*)(void*, const char*)>(previousReset)(self, reason); Changed(); }
    void Patch(size_t slot, void*& previous, void* hook)
    {
        if (previous) return;
        const Address at = kTransBufferVtable + slot;
        if (!CopySafe(&previous, reinterpret_cast<const void*>(at), sizeof(previous)) || !previous) { previous = nullptr; return; }
        if (!MemoryWriter::WriteBytes(at, &hook, sizeof(hook))) previous = nullptr;
    }
    void Observe()
    {
        Patch(0x60, previousReset, reinterpret_cast<void*>(&HookReset));
        Patch(0x68, previousEnd, reinterpret_cast<void*>(&HookEnd));
        Patch(0x78, previousUndo, reinterpret_cast<void*>(&HookUndo));
        Patch(0x7c, previousRedo, reinterpret_cast<void*>(&HookRedo));
    }

    Address Buffer()
    {
        const Address editor = Read<Address>(kEditor);
        if (!editor || !Read<Address>(editor + 0x130)) return 0;
        const Address trans = Read<Address>(editor + 0x148);
        if (!trans || Read<Address>(trans) != kTransBufferVtable) return 0;
        return trans;
    }

    // What changes whenever the list or the current point does, read without
    // walking the buffer, so an idle timer tick costs a handful of reads.
    struct Signature
    {
        Address trans = 0, data = 0, firstTitle = 0, lastTitle = 0, lastRecords = 0, reason = 0;
        int count = -1, undoCount = 0, active = 0, lastRecordCount = 0;
        bool operator==(const Signature&) const = default;
    };
    Signature seen;

    Signature Sign(Address trans)
    {
        Signature s;
        s.trans = trans;
        if (!trans) return s;
        s.data = Read<Address>(trans + 0x28);
        s.count = Read<int>(trans + 0x2c);
        s.undoCount = Read<int>(trans + 0x34);
        s.reason = Read<Address>(trans + 0x38);
        s.active = Read<int>(trans + 0x44);
        if (s.count > 0 && s.data)
        {
            s.firstTitle = Read<Address>(s.data + 0x14);
            const Address last = s.data + static_cast<Address>(s.count - 1) * kTransactionSize;
            s.lastTitle = Read<Address>(last + 0x14);
            s.lastRecords = Read<Address>(last + 0x08);
            s.lastRecordCount = Read<int>(last + 0x0c);
        }
        return s;
    }

    Snapshot ReadSnapshot(Address trans)
    {
        Snapshot s;
        const Address data = Read<Address>(trans + 0x28);
        const int count = Read<int>(trans + 0x2c);
        if (count < 0 || count > 1000000 || (count && !data)) throw std::runtime_error("The editor's undo buffer looks damaged.");
        s.undoCount = Read<int>(trans + 0x34);
        s.resetReason = String(trans + 0x38, 256);
        s.recording = Read<int>(trans + 0x44) != 0;
        s.limit = Read<unsigned>(trans + 0x48);
        s.titles.reserve(count);
        for (int i = 0; i < count; ++i)
        {
            const Address transaction = data + static_cast<Address>(i) * kTransactionSize;
            s.titles.push_back(String(transaction + 0x14, 512));
            const Address records = Read<Address>(transaction + 0x08);
            const int recordCount = Read<int>(transaction + 0x0c);
            if (recordCount < 0 || recordCount > 4000000 || (recordCount && !records)) continue;
            for (int r = 0; r < recordCount; ++r)
                s.used += static_cast<unsigned>(Read<int>(records + static_cast<Address>(r) * kRecordSize + 4));
        }
        return s;
    }

    HWND Child(int id) { return GetDlgItem(window, id); }

    void ShowStatus()
    {
        std::string text = available ? StatusText(shown) : "Open a map to see its undo history.";
        if (!report.empty()) text = report + "\r\n" + text;
        SetWindowTextA(Child(Status), text.c_str());
        const int current = Current(shown);
        EnableWindow(Child(UndoButton), available && current > 0);
        EnableWindow(Child(RedoButton), available && current < Count(shown));
        RedrawWindow(window, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
    }

    // Reads the buffer again when it moved; force rereads it regardless.
    void Refresh(bool force)
    {
        if (!window || busy) return;
        Address trans = 0;
        Signature now;
        try { trans = Buffer(); now = Sign(trans); }
        catch (const std::exception&) { trans = 0; now = Signature{}; }
        if (!force && now == seen) return;
        seen = now;
        Snapshot next;
        bool ok = trans != 0;
        std::string failure;
        if (ok)
        {
            try { next = ReadSnapshot(trans); }
            catch (const std::exception& error) { ok = false; failure = error.what(); }
        }
        HWND list = Child(List);
        const bool moved = ok != available || next.titles != shown.titles || next.undoCount != shown.undoCount;
        // The last jump's report stays until something else moves the buffer.
        if (moved || !failure.empty()) report = failure;
        available = ok;
        shown = ok ? std::move(next) : Snapshot{};
        if (moved)
        {
            SendMessageA(list, WM_SETREDRAW, FALSE, 0);
            SendMessageA(list, LB_SETCOUNT, available ? Rows(shown) : 0, 0);
            // The selection follows the current point; selecting scrolls it into view.
            if (available) SendMessageA(list, LB_SETCURSEL, Current(shown), 0);
            SendMessageA(list, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(list, nullptr, TRUE);
        }
        ShowStatus();
    }

    // One stock Undo or Redo. True when the buffer moved.
    bool Step(bool redo)
    {
        const Address trans = Buffer();
        if (!trans) return false;
        const int before = Read<int>(trans + 0x34);
        const Address editor = Read<Address>(kEditor);
        const Address exec = editor + 0x28;
        const auto method = Read<Address>(Read<Address>(exec));
        reinterpret_cast<int(__thiscall*)(void*, const char*, void*)>(method)(
            reinterpret_cast<void*>(exec), redo ? "TRANSACTION REDO" : "TRANSACTION UNDO", reinterpret_cast<void*>(Read<Address>(kLog)));
        const Address after = Buffer();
        return after == trans && Read<int>(trans + 0x34) != before;
    }

    void GoTo(int row)
    {
        Refresh(true);
        if (!available) { report = "Open a map first."; ShowStatus(); return; }
        if (shown.recording) { report = "Finish the current editor operation first."; ShowStatus(); return; }
        const int steps = StepsTo(shown, row);
        JumpResult result;
        std::string failure;
        busy = true;
        HCURSOR previous = SetCursor(LoadCursor(nullptr, IDC_WAIT));
        try { result = Jump(steps, [](bool redo) { return Step(redo); }); }
        catch (const std::exception& error) { result.wanted = steps; result.stopped = true; failure = error.what(); }
        SetCursor(previous);
        busy = false;
        Refresh(true);
        report = JumpReport(result) + (failure.empty() ? "" : " " + failure);
        ShowStatus();
    }

    // One step from wherever the buffer is now, not from what the list last
    // showed: a stock Undo a moment ago may not have reached the list yet.
    void Move(int delta)
    {
        Refresh(true);
        GoTo(Current(shown) + delta);
    }

    void CopyList()
    {
        std::string text;
        for (int row = 0; row < Rows(shown) && available; ++row)
            text += std::string(row == Current(shown) ? "> " : Undone(shown, row) ? "  (redo) " : "  ") + RowText(shown, row) + "\r\n";
        text += StatusText(shown) + "\r\n";
        if (!OpenClipboard(window)) return;
        EmptyClipboard();
        if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1))
        {
            memcpy(GlobalLock(memory), text.c_str(), text.size() + 1);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_TEXT, memory)) GlobalFree(memory);
        }
        CloseClipboard();
        report = "Copied " + std::to_string(Rows(shown)) + " rows.";
        ShowStatus();
    }

    std::wstring Wide(const std::string& text)
    {
        if (text.empty()) return {};
        const int count = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(count, L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), wide.data(), count);
        return wide;
    }

    void DrawRow(const DRAWITEMSTRUCT& item)
    {
        if (item.itemID == static_cast<UINT>(-1)) return;
        const int row = static_cast<int>(item.itemID);
        const bool selected = (item.itemState & ODS_SELECTED) != 0, current = available && row == Current(shown);
        const bool undone = available && Undone(shown, row);
        HDC dc = item.hDC;
        RECT rect = item.rcItem;
        FillRect(dc, &rect, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : undone ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
        HGDIOBJ old = SelectObject(dc, current ? bold : font);
        RECT marker = rect;
        marker.left += 4;
        marker.right = marker.left + 14;
        if (current) DrawTextW(dc, L"\x25B6", 1, &marker, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        RECT text = rect;
        text.left = marker.right + 2;
        text.right -= 4;
        const auto line = Wide(RowText(shown, row) + (undone ? "   (redo)" : ""));
        DrawTextW(dc, line.c_str(), static_cast<int>(line.size()), &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, old);
        // A rule under the current point: what is below it is waiting for Redo.
        if (current && row < Count(shown))
        {
            HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_GRAYTEXT));
            HGDIOBJ previous = SelectObject(dc, pen);
            MoveToEx(dc, rect.left, rect.bottom - 1, nullptr);
            LineTo(dc, rect.right, rect.bottom - 1);
            SelectObject(dc, previous);
            DeleteObject(pen);
        }
        if (item.itemState & ODS_FOCUS) DrawFocusRect(dc, &rect);
    }

    void Layout(int width, int height)
    {
        const int buttons = height - 92, status = height - 56;
        MoveWindow(Child(List), 8, 8, std::max(40, width - 16), std::max(40, buttons - 16), TRUE);
        const int w = std::max(40, (width - 16 - 3 * 6) / 4);
        MoveWindow(Child(UndoButton), 8, buttons, w, 28, TRUE);
        MoveWindow(Child(RedoButton), 8 + (w + 6), buttons, w, 28, TRUE);
        MoveWindow(Child(GoButton), 8 + 2 * (w + 6), buttons, w, 28, TRUE);
        MoveWindow(Child(CopyButton), 8 + 3 * (w + 6), buttons, w, 28, TRUE);
        MoveWindow(Child(Status), 8, status, std::max(40, width - 16), 50, TRUE);
    }

    int Selected() { return static_cast<int>(SendMessageA(Child(List), LB_GETCURSEL, 0, 0)); }

    LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM w, LPARAM l)
    {
        try
        {
            switch (message)
            {
            case WM_MEASUREITEM:
                reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight = rowHeight;
                return TRUE;
            case WM_DRAWITEM:
                if (w == List) { DrawRow(*reinterpret_cast<DRAWITEMSTRUCT*>(l)); return TRUE; }
                break;
            case WM_SIZE:
                Layout(LOWORD(l), HIWORD(l));
                return 0;
            case WM_TIMER:
                if (w == kTimer) Refresh(false);
                return 0;
            case kChanged:
                InterlockedExchange(&changePosted, 0);
                Refresh(false);
                return 0;
            case WM_VKEYTOITEM:
            {
                const UINT key = LOWORD(w);
                const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (key == VK_RETURN) { GoTo(Selected()); return -2; }
                if (control && key == 'Z') { Move(-1); return -2; }
                if (control && key == 'Y') { Move(1); return -2; }
                return -1;
            }
            case WM_COMMAND:
            {
                const int id = LOWORD(w), code = HIWORD(w);
                if (id == List && code == LBN_DBLCLK) { const int row = Selected(); if (row >= 0) GoTo(row); return 0; }
                if (id == UndoButton) { Move(-1); return 0; }
                if (id == RedoButton) { Move(1); return 0; }
                if (id == GoButton) { const int row = Selected(); if (row >= 0) GoTo(row); return 0; }
                if (id == CopyButton) { CopyList(); return 0; }
                break;
            }
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_NCDESTROY:
                window = nullptr;
                if (font) DeleteObject(font);
                if (bold) DeleteObject(bold);
                font = bold = nullptr;
                break;
            }
        }
        catch (const std::exception& error)
        {
            report = error.what();
            if (window) ShowStatus();
        }
        return DefWindowProcA(hwnd, message, w, l);
    }

    HWND Owner()
    {
        Address frame = 0;
        if (CopySafe(&frame, reinterpret_cast<const void*>(kMainFrame), sizeof(frame)) && frame)
        {
            HWND hwnd = nullptr;
            if (CopySafe(&hwnd, reinterpret_cast<const void*>(frame + 4), sizeof(hwnd)) && IsWindow(hwnd)) return hwnd;
        }
        return GetActiveWindow();
    }
}

void Open()
{
    if (window)
    {
        ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
        Refresh(true);
        return;
    }
    WNDCLASSA wc{};
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpfnWndProc = Proc;
    wc.lpszClassName = "ReloadedUndoHistory";
    RegisterClassA(&wc);

    NONCLIENTMETRICSA metrics{};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    font = CreateFontIndirectA(&metrics.lfMessageFont);
    LOGFONTA heavy = metrics.lfMessageFont;
    heavy.lfWeight = FW_BOLD;
    bold = CreateFontIndirectA(&heavy);
    if (HDC dc = GetDC(nullptr))
    {
        HGDIOBJ old = SelectObject(dc, font);
        TEXTMETRICA tm{};
        GetTextMetricsA(dc, &tm);
        rowHeight = tm.tmHeight + 5;
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
    }

    // A tool window owned by the frame: it stays above the editor and goes
    // with it, without covering other programs.
    Observe();
    window = CreateWindowExA(WS_EX_TOOLWINDOW, wc.lpszClassName, "Undo History", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 380, 560, Owner(), nullptr, wc.hInstance, nullptr);
    if (!window) throw std::runtime_error("Cannot open the Undo History window.");
    auto add = [](const char* cls, const char* text, int id, DWORD style)
    {
        HWND child = CreateWindowExA(cls[0] == 'L' ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, window,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
        SendMessageA(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    };
    // No data in the list box itself: rows are drawn from the snapshot, so a
    // buffer of thousands of steps costs one LB_SETCOUNT.
    add("LISTBOX", "", List, WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_NODATA | LBS_NOINTEGRALHEIGHT | LBS_WANTKEYBOARDINPUT);
    add("BUTTON", "&Undo", UndoButton, WS_TABSTOP | BS_PUSHBUTTON);
    add("BUTTON", "&Redo", RedoButton, WS_TABSTOP | BS_PUSHBUTTON);
    add("BUTTON", "&Go to step", GoButton, WS_TABSTOP | BS_PUSHBUTTON);
    add("BUTTON", "&Copy list", CopyButton, WS_TABSTOP | BS_PUSHBUTTON);
    add("STATIC", "", Status, SS_LEFT | SS_NOPREFIX);
    RECT client{};
    GetClientRect(window, &client);
    Layout(client.right, client.bottom);
    shown = Snapshot{};
    seen = Signature{};
    available = false;
    Refresh(true);
    report = "Double-click a step to undo or redo to it.";
    ShowStatus();
    SetTimer(window, kTimer, kTimerMs, nullptr);
    ShowWindow(window, SW_SHOW);
    SetFocus(Child(List));
}

bool HandleCommand(UINT command)
{
    if (command != kOpen) return false;
    Open();
    return true;
}

void InstallMenu(HMENU bar)
{
    if (!bar) return;
    for (int i = 0; i < GetMenuItemCount(bar); ++i)
    {
        HMENU sub = GetSubMenu(bar, i);
        if (!sub || GetMenuState(sub, 40020, MF_BYCOMMAND) == UINT(-1)) continue;
        if (GetMenuState(sub, kOpen, MF_BYCOMMAND) != UINT(-1)) return;
        for (int pos = 0; pos < GetMenuItemCount(sub); ++pos)
            if (GetMenuItemID(sub, pos) == 40020)
            {
                InsertMenuA(sub, pos + 1, MF_BYPOSITION | MF_STRING, kOpen, "Undo &History...");
                return;
            }
    }
}
}
