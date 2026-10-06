#pragma once
#include <windows.h>
#include <filesystem>
namespace MapPackageDialog
{
constexpr UINT Command = 40928;
void Open(HWND owner);
// With a base install, dependencies identical there (players have them) start
// unchecked, as Exclude base files... would leave them.
void Preview(HWND owner, const std::filesystem::path &map, const std::filesystem::path &base = {});
} // namespace MapPackageDialog
