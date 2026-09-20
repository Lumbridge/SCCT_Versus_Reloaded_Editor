#pragma once
#include <windows.h>

void ShowReloadedOptionsDialog(HWND hParent);
// Autosave: minutes between copies (0 = off) and how many copies to keep.
int ReloadedAutosaveMinutes();
int ReloadedAutosaveKeep();

class ReloadedOptions
{
public:
    static void Initialize();
};
