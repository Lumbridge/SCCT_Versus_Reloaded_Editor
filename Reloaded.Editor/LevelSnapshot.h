#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>

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

    // The largest visible perspective level viewport: its window, its camera
    // actor and the engine viewport. Throws when none is open.
    struct Perspective { HWND window = nullptr; uintptr_t camera = 0, viewport = 0; };
    Perspective FindPerspective();
    // The viewport's picture, drawn afresh, fitted to Thumbnail::Fit and saved
    // as a PNG (written beside the file first, then moved into place). The
    // viewport draws itself into the picture, so the tool window that asked,
    // or any other window over the viewport, is not in the shot.
    void SaveViewportThumbnail(const Perspective& viewport, const std::filesystem::path& file);
    // A thumbnail letterboxed into a width x height cell for an image list,
    // or nullptr when the file is missing, too large or not a readable image.
    // The file is read into memory first, so it is never left locked.
    HBITMAP LoadThumbnail(const std::filesystem::path& file, int width, int height);
    // The cell shown for entries saved before thumbnails existed.
    HBITMAP ThumbnailPlaceholder(int width, int height);
}
