#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <system_error>

// A copy of the game as players get it ([MapOptimise] BaseInstall in
// Reloaded_Editor.ini): Optimise Map Assets does not ship the packages
// identical there, and Package Map for Sharing leaves those files out.
namespace BaseInstall
{
inline std::string IniPath()
{
    char exe[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    return (std::filesystem::path(exe).parent_path() / "Reloaded_Editor.ini").string();
}
inline std::string Saved()
{
    char value[MAX_PATH]{};
    GetPrivateProfileStringA("MapOptimise", "BaseInstall", "", value, sizeof(value), IniPath().c_str());
    return value;
}
// Saved() reads it back in the ANSI code page, so a folder that code page
// cannot spell is not kept and the one saved before stays.
inline void Save(const std::filesystem::path &base)
{
    std::string text;
    try
    {
        text = base.string();
    }
    catch (const std::system_error &)
    {
        return;
    }
    WritePrivateProfileStringA("MapOptimise", "BaseInstall", text.c_str(), IniPath().c_str());
}
} // namespace BaseInstall
