#pragma once
#include <windows.h>

// Edit > Undo History: every step in the editor's undo buffer, oldest first,
// with the steps waiting for Redo greyed below the current point.
// Double-clicking a step undoes or redoes, one step at a time through the
// stock TRANSACTION UNDO / REDO command, until the map is as it was after it.
namespace UndoHistory
{
    constexpr UINT kOpen = 41105;

    void Open();
    // From the frame's command dispatcher. True when the command was ours.
    bool HandleCommand(UINT command);
    // Puts Undo History after the stock Edit > Redo (40020). Idempotent.
    void InstallMenu(HMENU bar);
}
