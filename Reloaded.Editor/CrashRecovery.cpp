#include "pch.h"
#include "CrashRecovery.h"
#include "CrashRecoveryModel.h"
#include "EditorExtras.h"
#include "WorkflowEditor.h"
#include "Version.h"
#include "logger.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    constexpr uintptr_t kCriticalError = 0x11692eb0; // GIsCriticalError, set by appError and every unguard an exception passes
    constexpr const char* kTitle = RE_PLUS_NAME " - Recover after a crash";
    constexpr DWORD kSettleMs = 3000; // after the frame is first seen idle, so start-up dialogs come first

    std::mutex markerLock;
    Sessions::Marker self;
    bool started = false;
    // The marker's path for EndSession, which may run under the loader lock
    // and so uses nothing but DeleteFileW.
    wchar_t markerPath[MAX_PATH * 2] = {};
    std::atomic<bool> crashed{false}, ended{false};
    std::optional<Sessions::Marker> abandoned; // the session to offer, until offered
    DWORD idleSince = 0;

    fs::path SessionsDirectory() { return Workflow::Editor::Directory() / "Sessions"; }
    fs::path MapsEd() { return Workflow::Editor::Directory().parent_path().parent_path() / "Packages" / "MapsEd"; }
    fs::path DiagnosticsDirectory() { return Workflow::Editor::Directory().parent_path() / "Diagnostics"; }

    std::uint64_t Ticks(const FILETIME& time) { return (std::uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime; }

    std::optional<std::uint64_t> ProcessCreated(DWORD pid)
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return std::nullopt;
        FILETIME created{}, exited{}, kernel{}, user{};
        std::optional<std::uint64_t> result;
        DWORD code = 0;
        // A process that has exited but is still referenced keeps its pid.
        if (GetProcessTimes(process, &created, &exited, &kernel, &user) && GetExitCodeProcess(process, &code) && code == STILL_ACTIVE)
            result = Ticks(created);
        CloseHandle(process);
        return result;
    }

    std::vector<Sessions::File> Files(const fs::path& directory, const char* pattern)
    {
        std::vector<Sessions::File> files;
        WIN32_FIND_DATAA found{};
        const auto search = (directory / pattern).string();
        HANDLE find = FindFirstFileA(search.c_str(), &found);
        if (find == INVALID_HANDLE_VALUE) return files;
        do
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                files.push_back({ (directory / found.cFileName).string(), Ticks(found.ftLastWriteTime) });
        while (FindNextFileA(find, &found));
        FindClose(find);
        return files;
    }

    // "14:32 today", "14:32 yesterday" or "14:32 on 2026-10-01", local time.
    std::string When(std::uint64_t ticks)
    {
        FILETIME utc{ static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32) }, local{};
        SYSTEMTIME time{}, now{};
        if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &time)) return "time unknown";
        GetLocalTime(&now);
        FILETIME nowFile{};
        SystemTimeToFileTime(&now, &nowFile);
        char text[64];
        const auto day = [](const SYSTEMTIME& t) { return t.wYear * 10000 + t.wMonth * 100 + t.wDay; };
        SYSTEMTIME yesterday{};
        const std::uint64_t yesterdayTicks = Ticks(nowFile) - 864000000000ull;
        FILETIME yesterdayFile{ static_cast<DWORD>(yesterdayTicks), static_cast<DWORD>(yesterdayTicks >> 32) };
        FileTimeToSystemTime(&yesterdayFile, &yesterday);
        if (day(time) == day(now)) sprintf_s(text, "%02u:%02u today", time.wHour, time.wMinute);
        else if (day(time) == day(yesterday)) sprintf_s(text, "%02u:%02u yesterday", time.wHour, time.wMinute);
        else sprintf_s(text, "%02u:%02u on %04u-%02u-%02u", time.wHour, time.wMinute, time.wYear, time.wMonth, time.wDay);
        return text;
    }

    bool OfferAtStartup()
    {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        const auto ini = (fs::path(exe).parent_path() / "Reloaded_Editor.ini").string();
        return GetPrivateProfileIntA("CrashRecovery", "OfferAtStartup", 1, ini.c_str()) != 0;
    }

    // Written beside and renamed over, so a session killed mid-write leaves
    // the previous marker rather than half of one.
    void WriteMarker()
    {
        const auto path = SessionsDirectory() / Sessions::MarkerFileName(self.pid);
        const auto temporary = fs::path(path).concat(".tmp");
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out << Sessions::Format(self);
            if (!out) throw std::runtime_error("cannot write " + temporary.string());
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
            throw std::runtime_error("cannot replace " + path.string());
    }

    // Takes over a marker by renaming it first, so of several editors
    // starting at once only one offers the same session.
    std::optional<Sessions::Marker> Claim(const fs::path& path)
    {
        auto claimed = path;
        claimed += ".claimed-" + std::to_string(GetCurrentProcessId());
        if (!MoveFileExW(path.c_str(), claimed.c_str(), 0)) return std::nullopt;
        std::ifstream in(claimed, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        in.close();
        DeleteFileW(claimed.c_str());
        return Sessions::Parse(text.str());
    }
}

void CrashRecovery::Start()
{
    std::lock_guard<std::mutex> lock(markerLock);
    if (started) return;
    try
    {
        FILETIME created{}, exited{}, kernel{}, user{};
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
        self.pid = GetCurrentProcessId();
        self.created = Ticks(created);
        std::error_code error;
        fs::create_directories(SessionsDirectory(), error);

        std::vector<Sessions::Marker> left;
        for (const auto& entry : fs::directory_iterator(SessionsDirectory(), error))
        {
            const auto name = entry.path().filename().string();
            if (name.rfind("session-", 0) != 0 || entry.path().extension() != ".txt") continue;
            std::ifstream in(entry.path(), std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            in.close();
            const auto marker = Sessions::Parse(text.str());
            if (marker && marker->pid == self.pid && marker->created == self.created) continue;
            if (marker && !Sessions::Abandoned(*marker, ProcessCreated(marker->pid))) continue; // another editor, still running
            if (auto claimed = Claim(entry.path())) left.push_back(*claimed);
        }
        if (auto latest = Sessions::Latest(left))
        {
            abandoned = latest;
            Logger::log("Crash recovery: the session of process " + std::to_string(latest->pid) + " did not exit cleanly (map: "
                        + (latest->map.empty() ? std::string("none") : latest->map) + ")");
        }

        WriteMarker();
        const auto path = (SessionsDirectory() / Sessions::MarkerFileName(self.pid)).wstring();
        wcsncpy_s(markerPath, path.c_str(), _TRUNCATE);
        started = true;
    }
    catch (const std::exception& e)
    {
        Logger::log(std::string("Crash recovery: could not start the session marker: ") + e.what());
    }
}

void CrashRecovery::NoteMap(const std::string& map)
{
    std::lock_guard<std::mutex> lock(markerLock);
    if (!started || ended || map == self.map) return;
    self.map = map;
    try { WriteMarker(); }
    catch (const std::exception& e) { Logger::log(std::string("Crash recovery: ") + e.what()); }
}

void CrashRecovery::NoteCrash() { crashed = true; }

void CrashRecovery::EndSession()
{
    if (!markerPath[0] || crashed || ended.exchange(true)) return;
    // An appError or a fault the engine's guard caught: the engine is going
    // down with it, so this exit is not clean.
    if (*reinterpret_cast<const volatile int*>(kCriticalError) != 0) { ended = false; return; }
    DeleteFileW(markerPath);
}

bool CrashRecovery::OfferWhenIdle(HWND frame)
{
    {
        std::lock_guard<std::mutex> lock(markerLock);
        if (!abandoned) return true;
    }
    GUITHREADINFO gui{ sizeof(gui) };
    GetGUIThreadInfo(GetWindowThreadProcessId(frame, nullptr), &gui);
    const bool idle = IsWindowVisible(frame) && IsWindowEnabled(frame) && !IsIconic(frame) && !GetCapture()
        && !(gui.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_INMOVESIZE));
    if (!idle) { idleSince = 0; return false; }
    if (!idleSince) { idleSince = GetTickCount() | 1; return false; }
    if (GetTickCount() - idleSince < kSettleMs) return false;

    Sessions::Marker session;
    {
        std::lock_guard<std::mutex> lock(markerLock);
        session = *abandoned;
        abandoned.reset();
    }
    if (!OfferAtStartup()) { Logger::log("Crash recovery: offer turned off ([CrashRecovery] OfferAtStartup=0)"); return true; }
    try
    {
        const auto autosave = Sessions::NewestAutosave(Files(MapsEd(), "Auto*.sdc"), session.created);
        const auto report = Sessions::CrashReportFor(Files(DiagnosticsDirectory(), "EditorCrash_*.log"), session);
        std::error_code error;
        const bool mapExists = !session.map.empty() && fs::is_regular_file(session.map, error);
        const auto offer = Sessions::BuildOffer(session, autosave, autosave ? When(autosave->written) : "", mapExists, report);
        if (offer.choices == Sessions::Choices::None) return true;

        UINT flags = MB_ICONWARNING | MB_SETFOREGROUND;
        if (offer.choices == Sessions::Choices::AutosaveOrMap) flags |= MB_YESNOCANCEL;
        else if (offer.choices == Sessions::Choices::Inform) flags = MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND;
        else flags |= MB_YESNO;
        const int answer = MessageBoxA(frame, offer.text.c_str(), kTitle, flags);
        std::string open;
        if (answer == IDYES) open = offer.choices == Sessions::Choices::Map ? offer.map : offer.autosave;
        else if (answer == IDNO && offer.choices == Sessions::Choices::AutosaveOrMap) open = offer.map;
        Logger::log("Crash recovery: offered, answer " + std::to_string(answer) + (open.empty() ? std::string() : ", opening " + open));
        if (!open.empty()) EditorExtras::OpenMap(open, false);
    }
    catch (const std::exception& e)
    {
        MessageBoxA(frame, e.what(), kTitle, MB_OK | MB_ICONERROR);
    }
    return true;
}

void CrashRecovery::OpenLatestAutosave(HWND frame)
{
    const auto folder = MapsEd();
    const auto autosave = Sessions::NewestAutosave(Files(folder, "Auto*.sdc"));
    if (!autosave)
        throw std::runtime_error("There is no autosave in\n" + folder.string() + "\n\nThe editor writes Auto0 to Auto9.sdc there when AutoSave is on "
                                 "(View > Advanced Options, Editor.EditorEngine: AutoSave, AutoSaveTimeMinutes).");
    const auto text = "Open " + Sessions::FileName(autosave->path) + ", autosaved " + When(autosave->written) + "?\n\n" + autosave->path
        + "\n\nUnsaved changes in the current map will be lost. The autosave opens under its own name, and the editor "
          "reuses Auto0 to Auto9 in turn: use File > Save As to keep it.";
    if (MessageBoxA(frame, text.c_str(), "Open Latest Autosave", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    EditorExtras::OpenMap(autosave->path, false);
}
