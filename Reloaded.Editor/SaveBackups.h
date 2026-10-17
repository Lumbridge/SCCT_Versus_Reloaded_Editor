#pragma once
#include <windows.h>
#include <filesystem>

// Keeps the last saved versions of each map. Before a MAP SAVE writes over a
// map in the game's Packages\MapsEd (File > Save, Save As, Optimise Map
// Assets, Rebuild All Maps), the file on disk is copied to
// System\ReloadedEditor\Backups\<Map>\<Map>_YYYYMMDD_HHMMSS.sdc, stamped with
// the local time it was replaced. [SaveBackups] Keep (per map) and MaxTotalMB
// (all of them) in Reloaded_Editor.ini say how many stay; Keep=0 turns it
// off. File > Open Map Backup... opens one; while it is open, File > Save asks
// where to save, as for an untitled map, and saving over the map puts it
// back. The model is SaveBackupModel.h.
namespace SaveBackups
{
    // Once, at startup: points the stock File > Save at Save As while the
    // open map is a backup.
    void Initialize();
    // With every editor command, before it runs. A save over a map in MapsEd
    // is backed up first, and a save into a backup lists it in SavedOver.txt
    // beside it, so it is kept; a failure is logged and never stops the save.
    // The file a MAP LOAD opens is noted, so a backup the open map came from
    // stays.
    void BeforeMapSave(const char* command) noexcept;
    // How many MAP SAVE commands (the stock autosave's own aside) have
    // reached the engine: unchanged across a Save that was cancelled.
    unsigned MapSaves();
    // File > Open Map Backup..., on the frame's thread.
    void OpenDialog(HWND frame);
    // System\ReloadedEditor\Backups.
    std::filesystem::path Folder();
}
