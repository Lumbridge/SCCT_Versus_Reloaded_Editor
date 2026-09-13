#pragma once

#include <windows.h>
#include <string>

namespace PlayLevelConfig
{
    struct Resolution
    {
        UINT width;
        UINT height;
    };

    inline bool IsValid(Resolution resolution)
    {
        return resolution.width >= 320 && resolution.height >= 200
            && resolution.width <= 16384 && resolution.height <= 16384;
    }

    inline Resolution SelectResolution(UINT requestedWidth, UINT requestedHeight,
                                       Resolution display)
    {
        const Resolution requested = { requestedWidth, requestedHeight };
        if (IsValid(requested))
            return requested;
        if (IsValid(display))
            return display;
        return { 1280, 720 };
    }

    inline Resolution CurrentDisplayResolution(HWND editorWindow)
    {
        MONITORINFOEXA monitor = {};
        monitor.cbSize = sizeof(monitor);
        DEVMODEA mode = {};
        mode.dmSize = sizeof(mode);
        if (GetMonitorInfoA(MonitorFromWindow(editorWindow, MONITOR_DEFAULTTOPRIMARY),
                &monitor)
            && EnumDisplaySettingsA(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode))
            return { mode.dmPelsWidth, mode.dmPelsHeight };
        return { 0, 0 };
    }

    // Clone the game's complete configuration, preserving its non-display
    // settings and ANSI bytes. Only the generated playtest INI is modified.
    inline bool WriteConfiguration(const char* source, const char* destination,
                                   Resolution resolution)
    {
        if (!IsValid(resolution))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }
        if (!CopyFileA(source, destination, FALSE))
            return false;
        const std::string width = std::to_string(resolution.width);
        const std::string height = std::to_string(resolution.height);
        const char* widthKeys[] = { "FullscreenViewportX", "WindowedViewportX", "MenuViewportX" };
        const char* heightKeys[] = { "FullscreenViewportY", "WindowedViewportY", "MenuViewportY" };
        for (const char* key : widthKeys)
        {
            if (!WritePrivateProfileStringA("WinDrv.WindowsClient", key, width.c_str(), destination))
                return false;
        }
        for (const char* key : heightKeys)
        {
            if (!WritePrivateProfileStringA("WinDrv.WindowsClient", key, height.c_str(), destination))
                return false;
        }
        // Request windowed startup too. Reloaded owns the final presentation
        // mode through its normal borderless/fullscreen configuration.
        if (!WritePrivateProfileStringA("WinDrv.WindowsClient", "StartupFullscreen", "False", destination))
            return false;
        WritePrivateProfileStringA(nullptr, nullptr, nullptr, destination);
        return true;
    }
}
