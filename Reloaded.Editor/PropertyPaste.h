#pragma once
#include <windows.h>

// Copy Properties / Paste Properties on the actor right-click menu (RE+:
// Properties): copy one actor's properties, then paste a ticked choice of
// them onto any number of selected actors in one Undo step, or the same
// choice again without the dialog. The copy lasts until the editor closes.
// PropertyPasteModel.h holds the rules, MagicEventNative.inl the editor side.
namespace PropertyPaste
{
    // In the reserved block 41075-41089.
    constexpr UINT kCopy = 41075, kPaste = 41076, kPasteAgain = 41077;

    // Whether there is a copy to paste: the menu greys the paste entries without one.
    bool HasCopy();
    // From the frame's command dispatcher. True when the command was ours.
    bool HandleCommand(UINT command);
}
