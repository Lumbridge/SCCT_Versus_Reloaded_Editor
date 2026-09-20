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
    constexpr UINT kRecentFirst = 40970, kRecentLast = 40979;

    // From the menu-injection thread, once the editor frame exists: brings up
    // the timer, the frame subclass and the menu entries on the frame's thread.
    void Attach(HWND frame);
    // From the frame's command dispatcher. True when the command was ours.
    bool HandleCommand(UINT command);
    // The Reloaded selection and visibility entries of the actor right-click menu.
    void AppendActorMenu(HMENU menu);
}
