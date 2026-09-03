#include "pch.h"
#include "WindowDriftFix.h"
#include "Hooks.h"

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
//
// OnDestroy (0x10F863A0) saves owned top-level windows in owner-client
// coordinates, but PerformCreateWindowEx (0x10F864F0) restores them as
// screen coordinates, causing saved positions to drift toward the top-left
// every session. Surface Properties (0x10EACAA0) exposed the bug once the
// snap-to-zero above was fixed.
//
// PerformCreateWindowEx's VerifyPosition call at 0x10F8662E runs before
// hWnd exists, so it has never worked. Reused here as the intended
// restore-time clamp.

static const int  kOffsetHWnd  = 4; // WWindow::hWnd
static const LONG kMinVisibleX = 120;
static const LONG kCaptionBand = 32;

// Clamps the origin only, so an oversized window may still run off the
// right/bottom edges (same as stock)
static void ClampOriginToWorkArea(LONG& x, LONG& y)
{
    const POINT pt = { x, y };
    const HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

    MONITORINFO mi;
    mi.cbSize = sizeof(mi);

    RECT wa;
    if (monitor && GetMonitorInfo(monitor, &mi))
    {
        wa = mi.rcWork;
    }
    else
    {
        wa.left = 0;
        wa.top = 0;
        wa.right = GetSystemMetrics(SM_CXSCREEN);
        wa.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    const LONG maxX = wa.right - kMinVisibleX;
    const LONG maxY = wa.bottom - kCaptionBand;

    if (x < wa.left) x = wa.left;
    if (y < wa.top)  y = wa.top;
    if (x > maxX) x = (maxX > wa.left) ? maxX : wa.left;
    if (y > maxY) y = (maxY > wa.top) ? maxY : wa.top;
}

static void __fastcall VerifyPositionFixed(void* wwindow)
{
    const HWND hwnd = *reinterpret_cast<HWND*>(static_cast<char*>(wwindow) + kOffsetHWnd);
    if (!hwnd || !IsWindow(hwnd)) return;
    if (IsZoomed(hwnd) || IsIconic(hwnd)) return;
    if (GetWindowLongA(hwnd, GWL_STYLE) & WS_CHILD) return;

    RECT wr;
    GetWindowRect(hwnd, &wr);

    LONG x = wr.left;
    LONG y = wr.top;
    ClampOriginToWorkArea(x, y);

    if (x != wr.left || y != wr.top)
        SetWindowPos(hwnd, NULL, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
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

// Owner-client space is only correct for a genuine child window
static BOOL __fastcall ShouldConvertRectToOwnerClient(void* wwindow)
{
    const HWND hwnd = *reinterpret_cast<HWND*>(static_cast<char*>(wwindow) + kOffsetHWnd);
    if (!hwnd || !IsWindow(hwnd)) return TRUE;

    return (GetWindowLongA(hwnd, GWL_STYLE) & WS_CHILD) ? TRUE : FALSE;
}

JMP_HOOK(0x10f8640f, WWindowOnDestroySaveRectHook)
{
    static int s_resume = 0x10f86414;

    __asm
    {
        push    ecx
        call    ShouldConvertRectToOwnerClient
        pop     ecx

        push    eax                                 // bConvert
        lea     edx, [ebp - 0x30]                   // &FRect out
        jmp     dword ptr [s_resume]
    }
}

// PerformCreateWindowEx locals at the patch site:
//   [ebp+0x10] dwStyle   [ebp+0x14] x   [ebp+0x18] y
static void __fastcall ClampRestoredPosition(DWORD dwStyle, int* x, int* y)
{
    if (!x || !y) return;
    if (dwStyle & WS_CHILD) return; // Child/MDI coordinates are parent-relative.

    LONG cx = *x;
    LONG cy = *y;
    ClampOriginToWorkArea(cx, cy);

    *x = cx;
    *y = cy;
}

JMP_HOOK(0x10f8662e, WWindowRestorePositionHook)
{
    static int s_resume = 0x10f86633;

    __asm
    {
        push    eax
        push    ecx
        push    edx

        lea     eax, [ebp + 0x18]
        push    eax
        lea     edx, [ebp + 0x14]
        mov     ecx, dword ptr [ebp + 0x10]
        call    ClampRestoredPosition               // __fastcall pops &y

        pop     edx
        pop     ecx
        pop     eax
        jmp     dword ptr [s_resume]
    }
}

void WindowDriftFix::Initialize()
{
    INSTALL_HOOKS;
}
