#include "pch.h"
#include "PasteFix.h"
#include "Hooks.h"
#include <cmath>

INIT_HOOKS;

// GEditor->Constraints: Exec_Edit's GRID= writes bit 0 of +0x1f8, and GridSize follows at +0x200
// (+0x1d4 is ClickPlane, which AddActor uses as the push-out normal)
static void __cdecl SnapPasteAdjust(float* adjust, const unsigned char* editor)
{
    if (!(*reinterpret_cast<const unsigned*>(editor + 0x1f8) & 1))
        return;
    const float* grid = reinterpret_cast<const float*>(editor + 0x200);
    for (int axis = 0; axis < 3; ++axis)
        if (grid[axis] > 0.0f)
            adjust[axis] = static_cast<float>(std::round(adjust[axis] / grid[axis]) * grid[axis]);
}

// Exec_Edit's "Paste > Here" / "At World Origin" operation; edi is GEditor
// Snapping the delta rather than the destination keeps the group's spacing and leaves on-grid geometry on grid
JMP_HOOK(0x10EF449F, PasteAdjustGridSnap)
{
    static int s_resume = 0x10EF44C0;

    __asm
    {
        // Adjust = Origin [ebp-0x24] minus bbox center [ebp-0x3c], written over Origin
        fld  dword ptr [ebp - 0x24]
        fsub dword ptr [ebp - 0x3c]
        fstp dword ptr [ebp - 0x24]
        fld  dword ptr [ebp - 0x20]
        fsub dword ptr [ebp - 0x38]
        fstp dword ptr [ebp - 0x20]
        fld  dword ptr [ebp - 0x1c]
        fsub dword ptr [ebp - 0x34]
        fstp dword ptr [ebp - 0x1c]

        // The apply loop needs edi (GEditor) and bl (the selected flag) intact
        pushad
        push edi
        lea  eax, [ebp - 0x24]
        push eax
        call SnapPasteAdjust
        add  esp, 8
        popad

        // The apply loop wants its counter zeroed and Adjust.Z on the x87 stack.
        xor  ecx, ecx
        fld  dword ptr [ebp - 0x1c]
        jmp  dword ptr [s_resume]
    }
}

void PasteFix::Initialize()
{
    INSTALL_HOOKS;
}
