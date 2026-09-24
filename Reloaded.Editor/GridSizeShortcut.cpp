#include "pch.h"
#include "GridSizeShortcut.h"
#include "StoreyFilter.h"
#include "GridSizeShortcutState.h"
#include "MemoryWriter.h"
#include <windowsx.h>
#include <cstdlib>
#include <cstring>

namespace
{
    GridSizeShortcut::Detail::WheelGesture g_gesture;

    // Verified against ChaosTheory_Editor.exe:
    // UWindowsViewport::ViewportWndProc (0x10f7e350) receives msg/wParam/lParam.
    // Its WM_MOUSEWHEEL branch (0x10f7fbd1) sends wheel-axis and wheel-key input
    // BEFORE calling UUnrealEdEngine::MouseWheel. Intercept at entry to suppress
    // all three paths, including perspective camera movement.
    constexpr uintptr_t kViewportWndProc = 0x10f7e350;
    constexpr uintptr_t kGridSelectionChanged = 0x10e3b820;
    constexpr uintptr_t kGEditor = 0x1165dfa0;
    constexpr uintptr_t kEditorFrame = 0x1165df84;
    constexpr uintptr_t kLevelViewportConfigs = 0x1165e8d4;
    constexpr uintptr_t kLevelViewportCount = 0x1165e8d8;

    HWND LevelViewportWindow(void* viewport)
    {
        if (!GridSizeShortcut::Detail::IsLevelViewport(
                viewport, *reinterpret_cast<void**>(kLevelViewportConfigs),
                *reinterpret_cast<int*>(kLevelViewportCount)))
            return nullptr;
        auto* view = static_cast<char*>(viewport);
        if (!view || !*reinterpret_cast<void**>(view + 0x30)) // camera actor
            return nullptr;
        auto* window = *reinterpret_cast<char**>(view + 0x1b4);
        if (!window)
            return nullptr;
        return *reinterpret_cast<HWND*>(window + 4);
    }

    void ChangeGridSize(int steps)
    {
        if (!steps)
            return;
        auto* editor = *reinterpret_cast<char**>(kGEditor);
        auto* frame = *reinterpret_cast<char**>(kEditorFrame);
        if (!editor || !frame || !*reinterpret_cast<void**>(editor + 0x130))
            return;
        auto* bottomBar = *reinterpret_cast<char**>(frame + 0x40);
        if (!bottomBar)
            return;
        auto* combo = *reinterpret_cast<char**>(bottomBar + 0x48);
        if (!combo)
            return;
        const HWND hwnd = *reinterpret_cast<HWND*>(combo + 4);
        if (!IsWindow(hwnd) || !IsWindowEnabled(hwnd))
            return;

        // Read the actual presets instead of maintaining a second grid-size list.
        // The stock selector contains the powers of two from 1 through 4096.
        int values[64] = {};
        const LRESULT count = SendMessageA(hwnd, CB_GETCOUNT, 0, 0);
        if (count <= 0 || count > static_cast<LRESULT>(_countof(values)))
            return;
        for (int i = 0; i < count; ++i)
        {
            char text[32] = {};
            const LRESULT length = SendMessageA(hwnd, CB_GETLBTEXTLEN, i, 0);
            if (length <= 0 || length >= sizeof(text))
                continue;
            if (SendMessageA(hwnd, CB_GETLBTEXT, i,
                             reinterpret_cast<LPARAM>(text)) == CB_ERR)
                continue;
            char* end = nullptr;
            const long value = std::strtol(text, &end, 10);
            if (end != text && *end == '\0' && value > 0)
                values[i] = value;
        }

        const int direction = steps > 0 ? 1 : -1;
        while (steps)
        {
            // Read the engine value each time so menu/console changes also work.
            const float current = *reinterpret_cast<float*>(editor + 0x200);
            const int next = GridSizeShortcut::Detail::NextPreset(
                current, values, static_cast<int>(count), direction);
            if (next < 0) // At the limit; discard overscroll instead of wrapping.
                break;
            if (SendMessageA(hwnd, CB_SETCURSEL, next, 0) == CB_ERR)
                break;

            // WBottomBarStandard::OnDragGridSizeSelChange reads the selected
            // preset, updates all three grid axes, refreshes the bottom bar,
            // and posts the native viewport redraw command (WM_COMMAND 0x801c).
            // It leaves the translation/rotation snap flags untouched.
            using SelectionChanged = void(__thiscall*)(void*);
            reinterpret_cast<SelectionChanged>(kGridSelectionChanged)(bottomBar);
            steps -= direction;
        }
    }

    bool __cdecl HandleViewportMessage(void* viewport, UINT message,
                                       WPARAM wParam, LPARAM lParam)
    {
        const auto target = reinterpret_cast<uintptr_t>(viewport);
        if ((message == WM_KEYUP || message == WM_SYSKEYUP)
            && (wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL))
            g_gesture.Reset();
        if ((message == WM_KILLFOCUS || message == WM_DESTROY
             || message == WM_NCDESTROY || message == WM_CAPTURECHANGED)
            && g_gesture.viewport == target)
            g_gesture.Reset();
        if (message == WM_ACTIVATEAPP && !wParam)
            g_gesture.Reset();
        if (message == WM_MOUSEMOVE)
        {
            if (!(wParam & MK_CONTROL))
                g_gesture.Reset();
            else
                g_gesture.ObserveViewport(target);
        }
        // The storey palette, when it is open, takes the paging keys over a
        // viewport, so one floor is stepped through where it is being looked
        // at rather than only from the palette itself.
        if (message == WM_KEYDOWN && StoreyFilter::ViewportKey(wParam))
            return true;
        if (message != WM_MOUSEWHEEL)
            return false;

        const HWND hwnd = LevelViewportWindow(viewport);
        const POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const HWND hovered = WindowFromPoint(point);
        if (!(GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) || !hwnd
            || (hovered != hwnd && !IsChild(hwnd, hovered)))
        {
            g_gesture.Reset();
            return false;
        }

        ChangeGridSize(g_gesture.Consume(target, GET_WHEEL_DELTA_WPARAM(wParam)));
        return true; // Consume partial deltas and limit hits too: never also zoom.
    }

    __declspec(naked) void ViewportMessageHook()
    {
        static uintptr_t resume = kViewportWndProc + 5;
        __asm
        {
            pushfd
            pushad
            lea eax, [esp + 36] // original stack: return, msg, wParam, lParam
            push dword ptr [eax + 12]
            push dword ptr [eax + 8]
            push dword ptr [eax + 4]
            push ecx
            call HandleViewportMessage
            add esp, 16
            test al, al
            jnz handled
            popad
            popfd
            // Replay the five displaced bytes of the native SEH prologue.
            push ebp
            mov ebp, esp
            push -1
            jmp dword ptr [resume]
        handled:
            popad
            popfd
            xor eax, eax
            ret 12
        }
    }
}

void GridSizeShortcut::Initialize()
{
    constexpr unsigned char prologue[] = { 0x55, 0x8b, 0xec, 0x6a, 0xff };
    if (std::memcmp(reinterpret_cast<const void*>(kViewportWndProc),
                    prologue, sizeof(prologue)) != 0
        || std::memcmp(reinterpret_cast<const void*>(kGridSelectionChanged),
                       prologue, sizeof(prologue)) != 0)
    {
        Logger::log("Grid size shortcut: unsupported native handler; hook skipped");
        return;
    }
    if (MemoryWriter::WriteJump(kViewportWndProc, ViewportMessageHook))
        Logger::log("Grid size shortcut: installed (native level-viewport matching)");
}
