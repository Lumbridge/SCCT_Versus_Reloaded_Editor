#pragma once
#include <windows.h>
#include <string>

// Offers the editor's own autosave after a session that did not exit cleanly.
// Each running editor keeps a marker under System\ReloadedEditor\Sessions
// naming its process and the map it has open; a clean exit deletes it. The
// next editor to start that finds a marker whose process is gone (crashed,
// hung and ended, or killed) offers, once the frame is idle, the newest
// Auto*.sdc written during that session or the map as last saved, and names
// the session's crash report if CrashDiagnostics wrote one. Markers of other
// editors still running are left alone. File > Open Latest Autosave... opens
// the newest autosave at any time. The model is CrashRecoveryModel.h.
namespace CrashRecovery
{
    // From the menu-injection thread before the frame is set up: writes this
    // session's marker and claims any marker left by a session that ended
    // abnormally.
    void Start();
    // On the frame's thread whenever the open map changes.
    void NoteMap(const std::string& map);
    // From the unhandled-exception filter: the exit that follows is a crash.
    void NoteCrash();
    // On a clean exit (the frame's destruction, or the process detach as a
    // fallback). Leaves the marker when the engine is in a critical error or
    // NoteCrash ran. Safe under the loader lock.
    void EndSession();
    // On the frame's thread, from a timer: shows the offer once the frame is
    // up and no modal dialog, menu or drag is in progress. True when there is
    // nothing (more) to do, so the timer can stop.
    bool OfferWhenIdle(HWND frame);
    // File > Open Latest Autosave...
    void OpenLatestAutosave(HWND frame);
}
