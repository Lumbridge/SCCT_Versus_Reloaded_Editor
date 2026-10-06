#pragma once
#include <windows.h>
// File > Optimise Map Assets...: what the open map uses from each asset package,
// and a release copy that carries only those assets (MapOptimiseModel.h).
namespace MapOptimiseDialog
{
constexpr UINT Command = 41390;
void Open(HWND owner);
} // namespace MapOptimiseDialog
