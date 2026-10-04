#pragma once
#include <windows.h>

class RebuildAllMaps
{
public:
    static void Initialize();

    // Gated on -UnlockPackages.
    static bool Available();

    // True while the batch runs; nothing may prompt during it.
    static bool Running();

    static void Show(HWND hParent);
};
