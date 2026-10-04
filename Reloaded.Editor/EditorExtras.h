#pragma once
#include <windows.h>
#include <string>

// Editor-wide conveniences that live outside any one tool: the File menu's
// recent maps, playtesting from the viewport camera, quick selection and
// visibility commands on the actor menu, and the shortcut legend. Commands
// arrive through the frame's menu dispatcher.
namespace EditorExtras
{
    constexpr UINT kSelectSameClass = 40954;
    constexpr UINT kSelectSameTag = 40955;
    constexpr UINT kInvertSelection = 40956;
    constexpr UINT kHideSelected = 40957;
    constexpr UINT kIsolateSelected = 40958;
    constexpr UINT kUnhideAll = 40959;
    constexpr UINT kPlayFromCameraSpy = 40960;
    constexpr UINT kPlayFromCameraMerc = 40961;
    constexpr UINT kShortcuts = 40962;
    constexpr UINT kSelectSameMesh = 40963;
    constexpr UINT kLevelSnapshotViewport = 40964;
    constexpr UINT kLevelSnapshotFile = 40965;
    constexpr UINT kRecentFirst = 40970, kRecentLast = 40979;

    // From the menu-injection thread, once the editor frame exists: brings up
    // the timer, the frame subclass and the menu entries on the frame's thread.
    // Returns once they are in place: the calling thread owns the hook that
    // does the work, so it must not exit before then.
    void Attach(HWND frame);
    // From the frame's command dispatcher. True when the command was ours.
    bool HandleCommand(UINT command);
    // The Reloaded selection and visibility entries of the actor right-click menu.
    void AppendActorMenu(HMENU menu);
    // Opens a saved map as Open Recent does: asks first when the open map has
    // unsaved changes (false when declined), then loads it and lists it in
    // Open Recent. Throws when the editor cannot load it.
    bool OpenMap(const std::string& path, HWND owner, const char* title);
}
