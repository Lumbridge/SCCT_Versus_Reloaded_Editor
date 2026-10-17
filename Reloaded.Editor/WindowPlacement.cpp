#include "pch.h"
#undef min
#undef max
#include "WindowPlacement.h"
#include "WindowPlacementModel.h"
#include <filesystem>
#include <string>
#include <vector>

namespace WindowPlacement
{
namespace
{
    constexpr char kSection[] = "WindowPlacement";

    std::string IniPath()
    {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        return (std::filesystem::path(exe).parent_path() / "Reloaded_Editor.ini").string();
    }

    // A window placement's normal rectangle is in workspace coordinates,
    // which start at the main monitor's work area, unless the window is a
    // tool window, whose are screen coordinates. The ini holds screen ones.
    POINT WorkspaceOrigin(HWND window)
    {
        if (GetWindowLongA(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return {0, 0};
        MONITORINFO info{sizeof(info)};
        if (!GetMonitorInfoA(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info)) return {0, 0};
        return {info.rcWork.left - info.rcMonitor.left, info.rcWork.top - info.rcMonitor.top};
    }

    BOOL CALLBACK AddMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM list)
    {
        MONITORINFO info{sizeof(info)};
        if (GetMonitorInfoA(monitor, &info))
            reinterpret_cast<std::vector<Rect>*>(list)->push_back({info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom});
        return TRUE;
    }
}

bool Restore(HWND window, const char* key)
{
    if (!window || !key) return false;
    char text[128] = {};
    GetPrivateProfileStringA(kSection, key, "", text, sizeof(text), IniPath().c_str());
    auto stored = Parse(text);
    if (!stored) return false;

    WINDOWPLACEMENT placement{sizeof(placement)};
    if (!GetWindowPlacement(window, &placement)) return false;
    const LONG style = GetWindowLongA(window, GWL_STYLE);
    Placement wanted = *stored;
    if (!(style & WS_THICKFRAME))
    {
        wanted.width = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
        wanted.height = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
    }
    else
    {
        // The window's own minimum, as it answers when it is resized.
        MINMAXINFO limits{};
        limits.ptMinTrackSize = {GetSystemMetrics(SM_CXMINTRACK), GetSystemMetrics(SM_CYMINTRACK)};
        SendMessageA(window, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
        wanted = Clamp(wanted, std::max<int>(kMinWidth, limits.ptMinTrackSize.x), std::max<int>(kMinHeight, limits.ptMinTrackSize.y));
    }
    if (!(style & WS_MAXIMIZEBOX)) wanted.maximised = false;

    std::vector<Rect> monitors;
    EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&monitors));
    if (!Usable(wanted, monitors)) return false;

    const POINT origin = WorkspaceOrigin(window);
    placement.rcNormalPosition = {wanted.x - origin.x, wanted.y - origin.y, wanted.x - origin.x + wanted.width, wanted.y - origin.y + wanted.height};
    placement.flags = 0;
    placement.showCmd = wanted.maximised ? SW_SHOWMAXIMIZED : IsWindowVisible(window) ? SW_SHOWNORMAL : SW_HIDE;
    return SetWindowPlacement(window, &placement) != FALSE;
}

void Remember(HWND window, const char* key)
{
    if (!window || !key) return;
    WINDOWPLACEMENT placement{sizeof(placement)};
    if (!GetWindowPlacement(window, &placement)) return;
    const POINT origin = WorkspaceOrigin(window);
    const RECT& normal = placement.rcNormalPosition;
    Placement where{normal.left + origin.x, normal.top + origin.y, normal.right - normal.left, normal.bottom - normal.top,
                    placement.showCmd == SW_SHOWMAXIMIZED
                        || (placement.showCmd == SW_SHOWMINIMIZED && (placement.flags & WPF_RESTORETOMAXIMIZED))};
    if (where.width <= 0 || where.height <= 0) return;
    WritePrivateProfileStringA(kSection, key, Format(where).c_str(), IniPath().c_str());
}
}
