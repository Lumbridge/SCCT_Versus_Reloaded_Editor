#include "pch.h"
#include "WindowDriftFix.h"
#include "Hooks.h"
#include "MemoryWriter.h"

#include <cstring>

INIT_HOOKS;

// Windows 10/11 fix for the maximized "window drift" bug.
//
// WWindow::OnActivate() is just a thunk to VerifyPosition(), which runs on
// every WM_ACTIVATE (when the window is activated or deactivated).
//
// VerifyPosition() is a Windows XP off-screen rescue that snaps a window
// to (0,0) whenever GetWindowRect() reports left/top < -4. That threshold
// matched Windows XP's maximized border overhang, but on Windows 10/11 the
// overhang is larger (-8px at 96 DPI, more with DPI scaling), so a
// legitimately maximized window at (-8,-8) is mistaken for an off-screen
// window and shifted down/right, often leaving the bottom edge beneath the
// taskbar.

static void __fastcall VerifyPositionFixed(void* wwindow)
{
    const HWND hwnd = *reinterpret_cast<HWND*>(static_cast<char*>(wwindow) + 4);
    if (!hwnd || !IsWindow(hwnd)) return;
    if (IsZoomed(hwnd) || IsIconic(hwnd)) return;

    RECT wr;
    GetWindowRect(hwnd, &wr);
    const LONG vl = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const LONG vt = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const LONG vr = vl + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const LONG vb = vt + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    LONG newX = wr.left, newY = wr.top;
    if (wr.left >= vr || wr.right <= vl) newX = 0;
    if (wr.top >= vb || wr.bottom <= vt) newY = 0;
    if (newX != wr.left || newY != wr.top)
        SetWindowPos(hwnd, NULL, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOSENDCHANGING);
}

// WWindow::OnActivate(int Active) is __thiscall with ecx = this.
// Its 8-byte body is fully replaced by the 5-byte jump hook.
JMP_HOOK(0x10f81f50, WWindowOnActivateHook)
{
    __asm
    {
        // ecx (this) passes straight through as the __fastcall argument.
        call    VerifyPositionFixed
        ret     4
    }
}

static bool ContainsTextIgnoreCase(const char* value, const char* token)
{
    const size_t valueLength = std::strlen(value);
    const size_t tokenLength = std::strlen(token);
    if (tokenLength == 0 || tokenLength > valueLength)
        return false;

    for (size_t i = 0; i <= valueLength - tokenLength; ++i)
        if (_strnicmp(value + i, token, tokenLength) == 0)
            return true;

    return false;
}

static bool IsPropertyWindow(HWND hwnd)
{
    char className[128] = {};
    char title[256] = {};
    GetClassNameA(hwnd, className, static_cast<int>(sizeof(className)));
    GetWindowTextA(hwnd, title, static_cast<int>(sizeof(title)));

    // The stock editors use several top-level WWindow classes for these:
    // WObjectProperties covers Actor/Level Properties, while Surface and
    // texture properties use property-sheet/dialog variants.
    return ContainsTextIgnoreCase(className, "propert")
        || ContainsTextIgnoreCase(className, "propsheet")
        || _stricmp(className, "WDlgTexProp") == 0
        || _stricmp(className, "WDlgUseProperties") == 0
        || ContainsTextIgnoreCase(title, " properties");
}

struct PropertyWindowResetContext
{
    RECT workArea;
    int resetCount;
};

static BOOL CALLBACK ResetPropertyWindowProc(HWND hwnd, LPARAM lParam)
{
    if (!IsWindow(hwnd) || !IsPropertyWindow(hwnd))
        return TRUE;

    PropertyWindowResetContext* context =
        reinterpret_cast<PropertyWindowResetContext*>(lParam);

    RECT windowRect = {};
    if (!GetWindowRect(hwnd, &windowRect))
        return TRUE;

    const LONG windowWidth = windowRect.right - windowRect.left;
    const LONG windowHeight = windowRect.bottom - windowRect.top;
    const LONG workWidth = context->workArea.right - context->workArea.left;
    const LONG workHeight = context->workArea.bottom - context->workArea.top;

    // Centre each window in the current editor monitor's usable area. Keep
    // its size and visibility unchanged so hidden singleton property windows
    // are also repaired without unexpectedly opening them.
    const LONG x = context->workArea.left
        + (windowWidth < workWidth ? (workWidth - windowWidth) / 2 : 0);
    const LONG y = context->workArea.top
        + (windowHeight < workHeight ? (workHeight - windowHeight) / 2 : 0);

    if (SetWindowPos(hwnd, nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE))
        ++context->resetCount;

    return TRUE;
}

int WindowDriftFix::ResetPropertyWindowPositions(HWND referenceWindow)
{
    HMONITOR monitor = MonitorFromWindow(referenceWindow,
                                         MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoA(monitor, &monitorInfo))
        return 0;

    PropertyWindowResetContext context = { monitorInfo.rcWork, 0 };
    EnumThreadWindows(GetCurrentThreadId(), ResetPropertyWindowProc,
                      reinterpret_cast<LPARAM>(&context));
    return context.resetCount;
}

void WindowDriftFix::Initialize()
{
    INSTALL_HOOKS;
}
