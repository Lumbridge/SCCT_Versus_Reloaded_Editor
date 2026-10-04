#pragma once
#include <windows.h>
#include <string>

// Find Usages in All Maps: which saved maps use a texture, material or static
// mesh, read from the map files' package tables without opening them.
namespace MapUsagesWindow
{
    // Texture / static mesh browser right-click entries, also accepted by the
    // frame's command dispatcher (the browser's current material or mesh).
    constexpr UINT kFindMaterial = 41060, kFindMesh = 41061;
    // Opens the window (or brings it forward) for this object and starts the
    // search when the name is not empty.
    void Open(HWND owner, const std::string& object);
}
