#include "pch.h"
#include "SaveBackups.h"
#include "SaveBackupModel.h"
#include "CrashRecoveryModel.h"
#include "EditorExtras.h"
#include "WorkflowEditor.h"
#include "logger.h"
#include <commdlg.h>
#include <chrono>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    // From the executable, not Workflow::Editor: the hook runs inside the
    // engine's Exec, before anything is known to be ready.
    fs::path SystemFolder()
    {
        wchar_t exe[32768] = {};
        GetModuleFileNameW(nullptr, exe, 32768);
        return fs::path(exe).parent_path();
    }
    fs::path MapsEd() { return SystemFolder().parent_path() / "Packages" / "MapsEd"; }

    // Backup names go through the model as UTF-8, so no name on disk is
    // left out for want of a code page.
    std::string Utf8(const std::wstring& text)
    {
        if (text.empty()) return {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
        return result;
    }
    std::wstring Wide(const std::string& text)
    {
        if (text.empty()) return {};
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring result(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length);
        return result;
    }

    // An int from Reloaded_Editor.ini, written with its default the first
    // time so the section can be found and edited.
    int Setting(const wchar_t* key, int fallback)
    {
        const auto ini = (SystemFolder() / "Reloaded_Editor.ini").wstring();
        wchar_t value[32] = {};
        GetPrivateProfileStringW(L"SaveBackups", key, L"", value, 32, ini.c_str());
        if (!value[0]) WritePrivateProfileStringW(L"SaveBackups", key, std::to_wstring(fallback).c_str(), ini.c_str());
        return static_cast<int>(GetPrivateProfileIntW(L"SaveBackups", key, fallback, ini.c_str()));
    }

    std::uint64_t Ticks(const FILETIME& time) { return (std::uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime; }

    // The files in one map's backup folder, by path.
    std::vector<SaveBackups::File> Files(const fs::path& folder, const std::string& map)
    {
        std::vector<SaveBackups::File> files;
        WIN32_FIND_DATAW found{};
        HANDLE find = FindFirstFileW((folder / L"*").c_str(), &found);
        if (find == INVALID_HANDLE_VALUE) return files;
        do
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                files.push_back({ map, Utf8((folder / found.cFileName).wstring()),
                                  (std::uint64_t(found.nFileSizeHigh) << 32) | found.nFileSizeLow, Ticks(found.ftLastWriteTime) });
        while (FindNextFileW(find, &found));
        FindClose(find);
        return files;
    }

    // Every map's backups, for the total.
    std::vector<SaveBackups::File> AllFiles()
    {
        std::vector<SaveBackups::File> files;
        std::error_code error, kind;
        for (fs::directory_iterator entry(SaveBackups::Folder(), error), end; !error && entry != end; entry.increment(error))
            if (entry->is_directory(kind))
            {
                auto more = Files(entry->path(), Utf8(entry->path().filename().wstring()));
                files.insert(files.end(), more.begin(), more.end());
            }
        return files;
    }

    void Backup(const std::string& value)
    {
        const auto mapsEd = MapsEd();
        std::error_code error;
        // The engine opens a relative name from the current directory
        // (System); the editor's own map name is relative to MapsEd.
        fs::path target(value);
        if (target.is_relative())
        {
            const auto here = fs::absolute(target, error);
            target = !error && fs::is_regular_file(here, error) ? here : mapsEd / target;
        }
        if (!fs::is_regular_file(target, error)) return; // A new map: nothing to keep.
        // Only the game's own MapsEd: not Play Level's Packages\Maps copy, nor
        // a release folder's.
        if (!fs::equivalent(target.parent_path(), mapsEd, error)) return;
        const auto name = Utf8(target.filename().wstring());
        if (Sessions::IsAutosaveName(name) || SaveBackups::IsBackupPath(Utf8(target.wstring()))) return;
        const int keep = Setting(L"Keep", 5), maxMB = Setting(L"MaxTotalMB", 2048);
        if (keep <= 0) return;

        WIN32_FILE_ATTRIBUTE_DATA source{};
        if (!GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &source)) return;
        const std::uint64_t size = (std::uint64_t(source.nFileSizeHigh) << 32) | source.nFileSizeLow;
        const auto stem = Utf8(target.stem().wstring());
        const auto folder = SaveBackups::Folder() / target.stem();
        if (!SaveBackups::Needed(Files(folder, stem), size, Ticks(source.ftLastWriteTime))) return;

        fs::create_directories(folder, error);
        if (error) throw std::runtime_error("cannot create " + Utf8(folder.wstring()) + ": " + error.message());
        SYSTEMTIME now{};
        GetLocalTime(&now);
        const SaveBackups::Stamp stamp{ now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond };
        const auto backup = folder / Wide(SaveBackups::BackupName(stem, Utf8(target.extension().wstring()), stamp));
        const auto started = std::chrono::steady_clock::now();
        // CopyFileW keeps the map's write time, which Needed compares.
        if (!CopyFileW(target.c_str(), backup.c_str(), TRUE))
        {
            const DWORD code = GetLastError();
            if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS)
            {
                Logger::log("Save backups: " + name + " already has a backup from this second");
                return;
            }
            throw std::runtime_error("could not copy " + name + " to " + Utf8(backup.wstring()) + " (error " + std::to_string(code) + ")");
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

        size_t removed = 0;
        for (const auto& old : SaveBackups::Prune(AllFiles(), static_cast<size_t>(keep), std::uint64_t((std::max)(maxMB, 0)) * 1024 * 1024))
            if (DeleteFileW(Wide(old).c_str())) ++removed;
            else Logger::log("Save backups: could not remove " + old + " (error " + std::to_string(GetLastError()) + ")");
        Logger::log(std::format("Save backups: copied {} ({:.1f} MB) in {:.2f} s", name, static_cast<double>(size) / (1024.0 * 1024.0), seconds)
                    + (removed ? "; removed " + std::to_string(removed) + (removed == 1 ? " old backup." : " old backups.") : std::string(".")));
    }

    void Report(const char* what) noexcept
    {
        try { Logger::log(std::string("Save backups: ") + what + "; the save goes ahead without a backup"); }
        catch (...) {}
    }
}

fs::path SaveBackups::Folder() { return SystemFolder() / "ReloadedEditor" / "Backups"; }

void SaveBackups::BeforeMapSave(const char* command) noexcept
{
    // Every editor command passes here: anything but a MAP command is let
    // through before a string is made of it.
    if (!command) return;
    const char* word = command;
    while (*word == ' ' || *word == '\t') ++word;
    if (_strnicmp(word, "MAP", 3) != 0) return;
    try
    {
        if (const auto target = SaveTarget(command)) Backup(*target);
    }
    catch (const std::exception& e) { Report(e.what()); }
    catch (...) { Report("unknown error"); }
}

void SaveBackups::OpenDialog(HWND frame)
{
    const auto folder = Folder();
    std::error_code error;
    if (!fs::is_directory(folder, error))
        throw std::runtime_error("No saved versions have been kept yet. Before a save replaces a map in Packages\\MapsEd, "
                                 "RE+ copies the file being replaced into\n" + Utf8(folder.wstring()) + "\n\n"
                                 "[SaveBackups] Keep in Reloaded_Editor.ini says how many of each map stay; Keep=0 turns it off.");
    // In the open map's own folder, when it has one (for an open backup, the
    // folder of the map it was taken from).
    auto start = folder;
    try
    {
        const auto map = Workflow::Editor::MapFile();
        fs::path stem = fs::path(map).stem();
        if (IsBackupPath(map))
            if (const auto backup = ParseBackup(map)) stem = fs::path(backup->first);
        if (!stem.empty() && fs::is_directory(folder / stem, error)) start = folder / stem;
    }
    catch (const std::exception&) { /* No map open yet: start at the top. */ }

    wchar_t path[32768] = {};
    const auto initial = start.wstring();
    OPENFILENAMEW choose{ sizeof(choose) };
    choose.hwndOwner = frame;
    choose.lpstrFile = path;
    choose.nMaxFile = 32768;
    choose.lpstrFilter = L"Map backups (*.sdc)\0*.sdc\0";
    choose.lpstrInitialDir = initial.c_str();
    choose.lpstrTitle = L"Open Map Backup";
    // The engine's relative paths depend on the current directory staying System.
    choose.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&choose)) return;
    EditorExtras::OpenMap(fs::path(path).string(), frame, "Open Map Backup");
}
