#pragma once
#include <windows.h>

// Edit > Undo History: every step in the editor's undo buffer, oldest first,
// with the steps waiting for Redo greyed below the current point.
// Double-clicking a step undoes or redoes, one step at a time through the
// stock TRANSACTION UNDO / REDO command, until the map is as it was after it.
// Checkpoints name a step ("before vent rework"); they show in the list and
// in a jump list above it, and last while their step is in the buffer.
namespace UndoHistory
{
    constexpr UINT kOpen = 41105;
    // Edit > Add Undo Checkpoint...: names the current step, opening the panel.
    constexpr UINT kAddCheckpoint = 41360;

    void Open();
    // From the frame's command dispatcher. True when the command was ours.
    bool HandleCommand(UINT command);
    // Puts Undo History and Add Undo Checkpoint after the stock Edit > Redo
    // (40020). Idempotent.
    void InstallMenu(HMENU bar);
}
