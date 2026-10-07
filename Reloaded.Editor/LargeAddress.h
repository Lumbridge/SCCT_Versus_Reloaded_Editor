#pragma once

// Makes ChaosTheory_Editor.exe large address aware (LargeAddressModel.h) so the
// editor can use 4 GB instead of 2. A running program cannot be rewritten but can
// be renamed: when the running editor lacks the flag, a flagged copy is written,
// the editor is renamed to ChaosTheory_Editor.original.exe and the copy takes its
// name, so the next start has the room. [Memory] LargeAddressAware=0 in
// Reloaded_Editor.ini leaves the editor alone.
namespace LargeAddress
{
    void Initialize();
}
