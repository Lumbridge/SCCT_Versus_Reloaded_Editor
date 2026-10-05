#include "pch.h"
#include "BrowserWindowClose.h"
#include "MemoryWriter.h"
#include "logger.h"

#include <commctrl.h>
#include <cstring>

// The browser window (WBrowserMaster, the one with the Textures / Actor
// Classes / Static Meshes / ... tabs) has no OnClose of its own, so its X
// button reaches WWindow::OnClose and destroys it, and the docked browsers
// with it. WBrowserMaster keeps its Browsers[] pointers (+0x78) all the same,
// and the next View > Show ... Browser crashes in WBrowserMaster::ShowBrowser
// (0x10E45F20). The stock editor does this too. Closing the window hides it
// instead; ShowBrowser brings it back with the browser asked for.

namespace
{
constexpr uintptr_t kCreateWindowExASlot = 0x11AF23E0;
constexpr UINT_PTR kSubclassId = 0x42574343; // "BWCC"

typedef HWND(WINAPI* CreateWindowExAFn)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
CreateWindowExAFn previousCreateWindowExA = nullptr;

LRESULT CALLBACK MasterProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR)
{
    switch (msg)
    {
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, MasterProc, kSubclassId);
        break;
    }
    return DefSubclassProc(hwnd, msg, w, l);
}

HWND WINAPI CreateWindowExAHook(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style, int x, int y, int width,
                                int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter)
{
    HWND created = previousCreateWindowExA
        ? previousCreateWindowExA(exStyle, className, windowName, style, x, y, width, height, parent, menu, instance, parameter)
        : CreateWindowExA(exStyle, className, windowName, style, x, y, width, height, parent, menu, instance, parameter);
    char name[96] = {};
    if (created && GetClassNameA(created, name, static_cast<int>(sizeof(name))))
    {
        static const char kMaster[] = "WBrowserMaster";
        const size_t length = strlen(name);
        if (length >= sizeof(kMaster) - 1 && _stricmp(name + length - (sizeof(kMaster) - 1), kMaster) == 0)
            SetWindowSubclass(created, MasterProc, kSubclassId, 0);
    }
    return created;
}
}

void BrowserWindowClose::Initialize()
{
    // Chained after the other hooks of the same import.
    previousCreateWindowExA = *reinterpret_cast<CreateWindowExAFn*>(kCreateWindowExASlot);
    const uintptr_t hook = reinterpret_cast<uintptr_t>(CreateWindowExAHook);
    if (!MemoryWriter::WriteBytes(kCreateWindowExASlot, &hook, sizeof(hook)))
        Logger::log("BrowserWindowClose: could not hook window creation; closing the browser window still destroys it");
}
