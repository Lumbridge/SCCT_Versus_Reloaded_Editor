#pragma once

#include "PlayLevelPresentation.h"
#include <cstdint>
#include <limits>

namespace PlayLevelWindow
{
    // Preserve the rendered aspect ratio while giving the playtest most of
    // the monitor's work area. Frame sizes include the existing title/borders.
    inline bool CalculatePlacement(const RECT& workArea, LONG frameWidth,
                                   LONG frameHeight, UINT renderWidth,
                                   UINT renderHeight, RECT& result)
    {
        const int64_t workWidth = int64_t(workArea.right) - workArea.left;
        const int64_t workHeight = int64_t(workArea.bottom) - workArea.top;
        if (workWidth <= 0 || workHeight <= 0
            || workWidth > (std::numeric_limits<LONG>::max)()
            || workHeight > (std::numeric_limits<LONG>::max)()
            || frameWidth < 0 || frameHeight < 0
            || renderWidth == 0 || renderHeight == 0)
            return false;

        const int64_t availableWidth = workWidth * 9 / 10 - frameWidth;
        const int64_t availableHeight = workHeight * 9 / 10 - frameHeight;
        if (availableWidth <= 0 || availableHeight <= 0)
            return false;

        int64_t width = availableWidth;
        int64_t height = availableHeight;
        if (availableWidth * renderHeight <= availableHeight * renderWidth)
            height = availableWidth * renderHeight / renderWidth;
        else
            width = availableHeight * renderWidth / renderHeight;
        if (width <= 0 || height <= 0)
            return false;

        width += frameWidth;
        height += frameHeight;
        result.left = static_cast<LONG>(workArea.left + (workWidth - width) / 2);
        result.top = static_cast<LONG>(workArea.top + (workHeight - height) / 2);
        result.right = result.left + static_cast<LONG>(width);
        result.bottom = result.top + static_cast<LONG>(height);
        return true;
    }

    class PlacementState
    {
    public:
        bool IsPending() const { return pending && !applying; }
        bool Begin()
        {
            if (!IsPending())
                return false;
            pending = false;
            applying = true;
            return true;
        }
        void Complete() { applying = false; }
        void Request()
        {
            // SetWindowPos can synchronously send WM_SIZE and reenter Reset.
            // That reset must not start a placement/reset feedback loop.
            if (!applying)
                pending = true;
        }

    private:
        bool pending = true;
        bool applying = false;
    };

    class Window
    {
    public:
        void OnReset() { placement.Request(); }

        const RECT* DestinationRect(const RECT* destination, HWND windowOverride) const
        {
            // COPY presentations may explicitly name the old full backbuffer
            // rectangle. Null asks D3D to fill the new client area instead.
            if (presentationWindow && destination
                && (!windowOverride || windowOverride == presentationWindow)
                && destination->left == 0 && destination->top == 0
                && destination->right > 0 && destination->bottom > 0
                && UINT(destination->right) == renderWidth
                && UINT(destination->bottom) == renderHeight)
                return nullptr;
            return destination;
        }

        void AfterPresent(IDirect3DDevice9* device, HWND destinationOverride)
        {
            if (!ShouldApply())
                return;
            IDirect3DSwapChain9* chain = nullptr;
            if (SUCCEEDED(device->GetSwapChain(0, &chain)))
            {
                AfterPresent(chain, destinationOverride);
                chain->Release();
            }
        }

        void AfterPresent(IDirect3DSwapChain9* chain, HWND destinationOverride)
        {
            if (!ShouldApply())
                return;

            D3DPRESENT_PARAMETERS parameters = {};
            if (FAILED(chain->GetPresentParameters(&parameters)))
                return;
            HWND window = parameters.hDeviceWindow;
            if (!window)
            {
                IDirect3DDevice9* device = nullptr;
                if (FAILED(chain->GetDevice(&device)))
                    return;
                D3DDEVICE_CREATION_PARAMETERS creation = {};
                const HRESULT hr = device->GetCreationParameters(&creation);
                device->Release();
                if (FAILED(hr))
                    return;
                window = creation.hFocusWindow;
            }
            if (destinationOverride && destinationOverride != window)
                return;

            DWORD processId = 0;
            const DWORD threadId = GetWindowThreadProcessId(window, &processId);
            if (processId != GetCurrentProcessId() || threadId != GetCurrentThreadId()
                || (GetWindowLongPtr(window, GWL_STYLE) & WS_CHILD)
                || !IsWindowVisible(window) || IsIconic(window) || IsZoomed(window))
                return;

            MONITORINFO monitor = {};
            monitor.cbSize = sizeof(monitor);
            RECT outer = {}, client = {};
            if (!GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)
                || !GetWindowRect(window, &outer) || !GetClientRect(window, &client))
                return;
            const LONG clientWidth = client.right - client.left;
            const LONG clientHeight = client.bottom - client.top;
            if (clientWidth <= 0 || clientHeight <= 0)
                return;
            presentationWindow = window;
            renderWidth = parameters.BackBufferWidth ? parameters.BackBufferWidth : UINT(clientWidth);
            renderHeight = parameters.BackBufferHeight ? parameters.BackBufferHeight : UINT(clientHeight);
            const LONG frameWidth = (outer.right - outer.left) - clientWidth;
            const LONG frameHeight = (outer.bottom - outer.top) - clientHeight;
            RECT desired = {};
            if (!CalculatePlacement(monitor.rcWork, frameWidth, frameHeight,
                    renderWidth, renderHeight,
                    desired) || !placement.Begin())
                return;

            // Present has already returned from D3D, and the engine has finished
            // positioning its startup window. Do this once, without activation
            // or any display-mode change, then let the user move the window.
            RECT oldClientScreen = {}, oldClip = {};
            const bool ownsOldClip = GetCapture() == window && GetForegroundWindow() == window
                && GetScreenClientRect(window, oldClientScreen) && GetClipCursor(&oldClip)
                && EqualRect(&oldClientScreen, &oldClip);
            if (!EqualRect(&outer, &desired)
                && SetWindowPos(window, nullptr, desired.left, desired.top,
                    desired.right - desired.left, desired.bottom - desired.top,
                    SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER))
            {
                // The game may already have captured the mouse in its original
                // small window. Only move that exact clip while it still owns
                // capture/focus, and leave any clip updated by WM_SIZE alone.
                RECT currentClip = {}, newClientScreen = {};
                if (ownsOldClip && GetCapture() == window && GetForegroundWindow() == window
                    && GetClipCursor(&currentClip) && EqualRect(&currentClip, &oldClip)
                    && GetScreenClientRect(window, newClientScreen))
                    ClipCursor(&newClientScreen);
            }
            placement.Complete();
        }

    private:
        bool ShouldApply() const
        {
            return PlayLevelPresentation::IsEnabledForProcess() && placement.IsPending();
        }
        static bool GetScreenClientRect(HWND window, RECT& rectangle)
        {
            POINT origin = {};
            if (!GetClientRect(window, &rectangle) || !ClientToScreen(window, &origin))
                return false;
            OffsetRect(&rectangle, origin.x, origin.y);
            return true;
        }
        PlacementState placement;
        HWND presentationWindow = nullptr;
        UINT renderWidth = 0;
        UINT renderHeight = 0;
    };
}
