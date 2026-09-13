#pragma once
#include <windows.h>
#include <filesystem>
namespace MapPackageDialog
{
constexpr UINT Command = 40928;
void Open(HWND owner);
void Preview(HWND owner, const std::filesystem::path &map);
} // namespace MapPackageDialog
