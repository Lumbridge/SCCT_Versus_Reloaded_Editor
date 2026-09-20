#pragma once
#include <windows.h>

// The level snapshot the game shows in map selection: the Menu texture of the
// map's <Map>-i interface package. One command captures the perspective
// viewport into it, another takes an image file; both go through the
// editor's own texture importer and the stock SAVEMAPPROP save, so the
// loading screens and map settings in that package ride along and the next
// map save keeps the new picture. Errors propagate to the caller's dialog.
namespace LevelSnapshot
{
    void Attach(HWND frame);
    void FromViewport();
    void FromImageFile();
}
