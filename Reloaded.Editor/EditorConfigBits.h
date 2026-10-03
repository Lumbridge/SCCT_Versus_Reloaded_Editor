#pragma once
#include <cstdint>

// The exe's native code reads the editor's config bools (AutoSave,
// UseSizingBox, UseAxisIndicator) at fixed bits of GEditor, laid out by the
// stock Editor.u. Rebuilt packages can declare them in another order (SVM 4.0
// puts AutoSave at +0x224 bit 1, so the native autosave never runs), so once
// the editor class is linked, every native read is repointed at the bit the
// property system actually loads.
namespace EditorConfigBits
{
    // On the frame's thread. True once the native reads match the loaded
    // properties; false while the editor is not ready, so callers retry.
    bool Apply();
    // Brings the View > Advanced Options values (saved to [Editor.EditorEngine])
    // into GEditor, which loads from [UnrealEd.UnrealEdEngine]. Apply calls it.
    void SyncAdvancedOptions();
    // A GEditor bool property's byte offset and bit within that byte.
    bool Locate(const char* name, uint32_t& byteOffset, uint8_t& bit);
}
