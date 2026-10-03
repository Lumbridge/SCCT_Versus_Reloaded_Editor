#pragma once
#include <windows.h>
#include <string>

// Updates the editor from the GitHub releases. A short while after start-up
// (unless turned off) and from Help > Check for Reloaded Updates, a worker
// thread asks GitHub for the newest release; when it is newer than this DLL
// it offers to install it. Installing downloads the release archive, checks
// it, and swaps Reloaded.Editor.dll (and the launcher) in place: a loaded DLL
// cannot be overwritten but can be renamed, so the running one becomes
// Reloaded.Editor.dll.old, which the next start deletes. The new version runs
// after the editor is restarted.
namespace SelfUpdater
{
    constexpr UINT kCheckNow = 40995;
    constexpr UINT kToggleStartupCheck = 40996;

    // From DLL start-up, with the path of this DLL.
    void Initialize(const std::wstring& dllPath);
    // The Help menu entries.
    void AppendHelpMenu(HMENU help);
    // From the frame's command dispatcher. True when the command was ours.
    bool HandleCommand(UINT command);
}
