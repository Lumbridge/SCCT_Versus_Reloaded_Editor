#include "pch.h"
#include "SaveBackups.h"
#include "SaveBackupModel.h"
#include "CrashRecoveryModel.h"
#include "EditorExtras.h"
#include "WorkflowEditor.h"
#include "logger.h"
#include "MemoryWriter.h"
#include <commdlg.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
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

    // In each map's backup folder: the backups a save in the editor wrote
    // into, one file name a line.
    constexpr wchar_t kSavedOverList[] = L"SavedOver.txt";

    // A small file's bytes; empty when there is none.
    std::string ReadText(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    }

    // The engine opens a relative name from the current directory (System);
    // the editor's own map name is relative to MapsEd.
    fs::path Resolve(const std::string& value)
    {
        fs::path target(value);
        if (!target.is_relative()) return target;
        std::error_code error;
        const auto here = fs::absolute(target, error);
        return !error && fs::is_regular_file(here, error) ? here : MapsEd() / target;
    }

    // The file the last MAP LOAD opened: when the open map is a backup, a
    // save over its map must not remove it before the save has read it.
    std::string loaded;
    // MAP SAVE commands passed to the engine so far, but for the stock
    // autosave's own, which may run while a Save As box is open.
    std::atomic<unsigned> saves = 0;

    // The files in one map's backup folder, by path.
    std::vector<SaveBackups::File> Files(const fs::path& folder, const std::string& map)
    {
        std::vector<SaveBackups::File> files;
        const auto savedOver = ReadText(folder / kSavedOverList);
        WIN32_FIND_DATAW found{};
        HANDLE find = FindFirstFileW((folder / L"*").c_str(), &found);
        if (find == INVALID_HANDLE_VALUE) return files;
        do
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                files.push_back({ map, Utf8((folder / found.cFileName).wstring()),
                                  (std::uint64_t(found.nFileSizeHigh) << 32) | found.nFileSizeLow, Ticks(found.ftLastWriteTime),
                                  Ticks(found.ftCreationTime), SaveBackups::Listed(savedOver, Utf8(found.cFileName)) });
        while (FindNextFileW(find, &found));
        FindClose(find);
        return files;
    }

    // A save into a kept backup (Save As onto one, or Save while one is open
    // on an editor whose File > Save is not redirected): from now on it holds
    // the user's work. Its name goes into SavedOver.txt beside it, and Prune
    // then neither removes nor counts it, whatever its times say.
    void NoteSavedOver(const std::string& value)
    {
        const auto target = Resolve(value);
        if (!SaveBackups::IsBackupPath(Utf8(target.wstring()))) return;
        std::error_code error;
        const auto folder = target.parent_path();
        if (!fs::is_directory(folder, error)) return; // The save cannot be written either.
        const auto name = Utf8(target.filename().wstring());
        const auto list = folder / kSavedOverList;
        if (SaveBackups::Listed(ReadText(list), name)) return;
        std::ofstream out(list, std::ios::binary | std::ios::app);
        out << name << "\r\n";
        out.close();
        if (!out) throw std::runtime_error("could not list " + name + " in " + Utf8(list.wstring()));
        Logger::log("Save backups: " + name + " is saved over, so it is kept and not counted from now on");
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
        // Play Level's command names MapsEd\Autoplay.sdc, but the engine
        // writes only Packages\Maps\Autoplay.sdc: nothing in MapsEd is
        // replaced, even when a file of that name is there.
        if (SaveBackups::IsPlayLevelName(value)) return;
        const auto mapsEd = MapsEd();
        std::error_code error;
        const auto target = Resolve(value);
        if (!fs::is_regular_file(target, error)) return; // A new map: nothing to keep.
        // Only the game's own MapsEd, not a release folder's.
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
        WIN32_FILE_ATTRIBUTE_DATA copied{};
        if (GetFileAttributesExW(backup.c_str(), GetFileExInfoStandard, &copied))
        {
            // A read-only map gives a read-only copy, which Prune could never remove.
            if (copied.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(backup.c_str(), copied.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            // The copy is created now with the map's write time. A map written
            // later than now (by a clock that ran ahead) would make it look
            // saved over since, so its creation time is moved up to match.
            if (Ticks(copied.ftLastWriteTime) > Ticks(copied.ftCreationTime))
                if (const HANDLE file = CreateFileW(backup.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                                    nullptr, OPEN_EXISTING, 0, nullptr);
                    file != INVALID_HANDLE_VALUE)
                {
                    SetFileTime(file, &copied.ftLastWriteTime, nullptr, nullptr);
                    CloseHandle(file);
                }
        }

        size_t removed = 0;
        for (const auto& old : SaveBackups::Prune(AllFiles(), static_cast<size_t>(keep), std::uint64_t((std::max)(maxMB, 0)) * 1024 * 1024, loaded))
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

    // The stock FileSave and FileSaveAs, int __cdecl (HWND), nonzero once
    // the map is saved. Every Save reaches FileSave through its thunk: File >
    // Save and Ctrl+L, the engine's own request to save the map, and the
    // build-and-save question when the map is closed.
    using SaveFunction = int(__cdecl*)(HWND);
    constexpr uintptr_t kFileSaveThunk = 0x10E03EFA, kFileSave = 0x10E3D300, kFileSaveAs = 0x10E047AB;

    bool BackupOpen() noexcept
    {
        try { return SaveBackups::IsBackupPath(Workflow::Editor::MapFile()); }
        catch (...) { return false; }
    }

    // In place of FileSave: an opened backup is saved where the user says,
    // as FileSave does for an untitled map. A plain Save then never puts the
    // user's work into the Backups folder, where pruning would remove it.
    int __cdecl SaveOrAsk(HWND window)
    {
        if (!BackupOpen()) return reinterpret_cast<SaveFunction>(kFileSave)(window);
        try { Logger::log("Save backups: the open map is a kept backup, so Save asks where to save it"); }
        catch (...) {}
        return reinterpret_cast<SaveFunction>(kFileSaveAs)(window);
    }
}

void SaveBackups::Initialize()
{
    // Only when the thunk is still the stock jump to FileSave.
    if (*reinterpret_cast<const unsigned char*>(kFileSaveThunk) == 0xE9
        && kFileSaveThunk + 5 + *reinterpret_cast<const int*>(kFileSaveThunk + 1) == kFileSave
        && MemoryWriter::WriteJump(kFileSaveThunk, reinterpret_cast<void (*)()>(&SaveOrAsk)))
        return;
    Logger::log("Save backups: File > Save is not the supported editor's; Save on an opened backup writes into that backup.");
}

unsigned SaveBackups::MapSaves() { return saves; }

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
        if (const auto target = SaveTarget(command))
        {
            if (!IsStockAutosave(command)) ++saves;
            NoteSavedOver(*target);
            Backup(*target);
        }
        else if (const auto file = LoadTarget(command)) loaded = Utf8(fs::path(*file).wstring());
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
                                 "RE+ copies the file being replaced into\n" + folder.string() + "\n\n"
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
    // The engine's relative paths depend on the current directory staying
    // System. A backup always opens normally: no read-only choice to ignore.
    choose.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&choose)) return;
    EditorExtras::OpenMap(fs::path(path).string(), frame, "Open Map Backup");
}
