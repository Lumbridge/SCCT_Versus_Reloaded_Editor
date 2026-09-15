#pragma once
#include <windows.h>
namespace MapAuthoringDialog
{
    constexpr UINT Export=40934,Import=40935;
    void Open(HWND owner,bool exporting);
}
