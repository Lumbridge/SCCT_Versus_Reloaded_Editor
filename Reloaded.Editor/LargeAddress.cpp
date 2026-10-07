#include "pch.h"
#include "LargeAddress.h"
#include "LargeAddressModel.h"
#include "logger.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    bool RunningIsLargeAddressAware()
    {
        const auto base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        return (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
    }

    void Apply()
    {
        wchar_t path[MAX_PATH] = {};
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;
        const fs::path exe(path);
        // Only the editor: the DLL is never meant to touch the game or anything else.
        if (_wcsicmp(exe.filename().c_str(), L"ChaosTheory_Editor.exe") != 0) return;
        const auto ini = (exe.parent_path() / "Reloaded_Editor.ini").string();
        if (!GetPrivateProfileIntA("Memory", "LargeAddressAware", 1, ini.c_str())) return;
        if (RunningIsLargeAddressAware())
        {
            Logger::log("Large address aware: the editor can use up to 4 GB");
            return;
        }

        std::vector<unsigned char> image;
        {
            std::ifstream in(exe, std::ios::binary);
            if (!in) { Logger::log("Large address aware: cannot read " + exe.string()); return; }
            image.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        if (!LargeAddress::MakeLargeAddressAware(image))
        {
            // The file is flagged already (patched since this run started): the next start has it.
            Logger::log("Large address aware: ChaosTheory_Editor.exe is flagged; restart the editor to use up to 4 GB");
            return;
        }
        const fs::path folder = exe.parent_path();
        const fs::path fresh = folder / "ChaosTheory_Editor.large-address.tmp";
        {
            std::ofstream out(fresh, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
            if (!out) { Logger::log("Large address aware: cannot write " + fresh.string()); std::error_code e; fs::remove(fresh, e); return; }
        }
        const fs::path backup = folder / LargeAddress::BackupName([&](const std::string& name) { std::error_code e; return fs::exists(folder / name, e); });
        // The running editor can be renamed though not rewritten; the flagged copy takes its name.
        if (!MoveFileExW(exe.c_str(), backup.c_str(), 0))
        {
            Logger::log("Large address aware: cannot rename the editor (error " + std::to_string(GetLastError()) + "); left as it is");
            std::error_code e; fs::remove(fresh, e);
            return;
        }
        if (!MoveFileExW(fresh.c_str(), exe.c_str(), 0))
        {
            const DWORD error = GetLastError();
            MoveFileExW(backup.c_str(), exe.c_str(), 0);
            std::error_code e; fs::remove(fresh, e);
            Logger::log("Large address aware: cannot put the flagged editor in place (error " + std::to_string(error) + "); left as it is");
            return;
        }
        Logger::log("Large address aware: ChaosTheory_Editor.exe can use up to 4 GB from the next start (the original is kept as " +
                    backup.filename().string() + ")");
    }
}

void LargeAddress::Initialize()
{
    try { Apply(); }
    catch (const std::exception& e) { Logger::log(std::string("Large address aware: ") + e.what()); }
}
