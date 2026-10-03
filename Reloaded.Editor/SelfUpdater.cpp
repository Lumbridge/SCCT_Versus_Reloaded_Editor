#include "pch.h"
#include "SelfUpdater.h"
#include "SelfUpdaterModel.h"
#include "Version.h"
#include "logger.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <zlib.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

using Updater::Bytes;
using Updater::Release;
using Updater::Version;
namespace fs = std::filesystem;

namespace
{
    constexpr wchar_t kReleasesUrl[] = L"https://api.github.com/repos/Lumbridge/SCCT_Versus_Reloaded_Editor/releases?per_page=20";
    constexpr wchar_t kTitle[] = L"RE+ Update";
    constexpr char kSection[] = "Updates";
    // Long enough for the editor to finish opening before anything asks.
    constexpr DWORD kStartupDelayMs = 8000;
    constexpr size_t kMaxListBytes = 4u << 20, kMaxArchiveBytes = 64u << 20;
    // The files a release archive may replace, beside the DLL.
    const char* const kDllName = "Reloaded.Editor.dll";
    const char* const kLauncherName = "Reloaded_Editor.exe";

    fs::path directory;
    std::atomic<bool> busy{false};
    // Set once an update is in place, so a second check before the restart
    // does not offer the same release again.
    std::atomic<bool> installed{false};
    std::string installedVersion;
    HMENU helpMenu = nullptr;

    std::wstring Wide(const std::string& s)
    {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }

    std::string IniPath()
    {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        return (fs::path(exe).parent_path() / "Reloaded_Editor.ini").string();
    }
    bool CheckOnStartup() { return GetPrivateProfileIntA(kSection, "CheckOnStartup", 1, IniPath().c_str()) != 0; }
    void SetCheckOnStartup(bool on) { WritePrivateProfileStringA(kSection, "CheckOnStartup", on ? "1" : "0", IniPath().c_str()); }
    std::string SkippedVersion()
    {
        char value[64] = {};
        GetPrivateProfileStringA(kSection, "SkippedVersion", "", value, sizeof(value), IniPath().c_str());
        return value;
    }
    void SetSkippedVersion(const std::string& v) { WritePrivateProfileStringA(kSection, "SkippedVersion", v.c_str(), IniPath().c_str()); }

    int Ask(const std::wstring& text, UINT flags)
    {
        // The worker thread owns no window, so the box leaves the editor
        // usable; it is kept on top so it does not open behind the frame.
        return MessageBoxW(nullptr, text.c_str(), kTitle, flags | MB_SETFOREGROUND | MB_TOPMOST);
    }

    struct Internet
    {
        HINTERNET h = nullptr;
        explicit Internet(HINTERNET handle) : h(handle) {}
        ~Internet() { if (h) WinHttpCloseHandle(h); }
        Internet(const Internet&) = delete;
        Internet& operator=(const Internet&) = delete;
    };

    [[noreturn]] void FailWin32(const std::string& what)
    {
        throw std::runtime_error(what + " (error " + std::to_string(GetLastError()) + ").");
    }

    // An HTTPS GET, following redirects, refused beyond limit bytes.
    Bytes Get(const std::wstring& url, const wchar_t* headers, size_t limit)
    {
        URL_COMPONENTS parts{sizeof(parts)};
        wchar_t host[256] = {}, path[2048] = {};
        parts.lpszHostName = host;
        parts.dwHostNameLength = ARRAYSIZE(host);
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = ARRAYSIZE(path);
        wchar_t extra[2048] = {};
        parts.lpszExtraInfo = extra;
        parts.dwExtraInfoLength = ARRAYSIZE(extra);
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
            throw std::runtime_error("Unusable update address.");
        const std::wstring agent = L"RE-Plus/" + Wide(RE_PLUS_VERSION);
        HINTERNET opened = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!opened) opened = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        Internet session(opened);
        if (!session.h) FailWin32("Could not start a connection");
        WinHttpSetTimeouts(session.h, 10000, 10000, 15000, 30000);
        Internet connection(WinHttpConnect(session.h, host, parts.nPort, 0));
        if (!connection.h) FailWin32("Could not connect to GitHub");
        const std::wstring object = std::wstring(path) + extra;
        Internet request(WinHttpOpenRequest(connection.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (!request.h) FailWin32("Could not create the request");
        if (!WinHttpSendRequest(request.h, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, headers ? static_cast<DWORD>(-1L) : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            || !WinHttpReceiveResponse(request.h, nullptr))
            FailWin32("Could not reach GitHub");
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status == 403 || status == 429)
            throw std::runtime_error("GitHub refused the request, probably because too many were made from this network. Try again in an hour.");
        if (status != 200) throw std::runtime_error("GitHub answered with HTTP status " + std::to_string(status) + ".");
        Bytes body;
        for (;;)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.h, &available)) FailWin32("The download was interrupted");
            if (!available) break;
            if (body.size() + available > limit) throw std::runtime_error("The download is larger than expected.");
            const size_t at = body.size();
            body.resize(at + available);
            DWORD read = 0;
            if (!WinHttpReadData(request.h, body.data() + at, available, &read)) FailWin32("The download was interrupted");
            body.resize(at + read);
        }
        return body;
    }

    std::string Sha256(const Bytes& data)
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        unsigned char digest[32] = {};
        bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0
                  && BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0
                  && BCryptHashData(hash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0) >= 0
                  && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        if (!ok) throw std::runtime_error("Could not check the download's SHA-256.");
        static const char hex[] = "0123456789abcdef";
        std::string out;
        for (unsigned char b : digest) out += hex[b >> 4], out += hex[b & 15];
        return out;
    }

    Bytes Extract(const Bytes& zip, const Updater::ZipEntry& entry)
    {
        Bytes out(entry.size);
        if (entry.method == 0)
        {
            if (entry.compressedSize != entry.size) throw std::runtime_error("The update archive is damaged.");
            std::copy_n(zip.begin() + entry.dataOffset, entry.size, out.begin());
        }
        else if (entry.method == 8)
        {
            z_stream s{};
            if (inflateInit2(&s, -MAX_WBITS) != Z_OK) throw std::runtime_error("Could not unpack the update archive.");
            s.next_in = const_cast<Bytef*>(zip.data() + entry.dataOffset);
            s.avail_in = entry.compressedSize;
            s.next_out = out.data();
            s.avail_out = static_cast<uInt>(out.size());
            const int result = inflate(&s, Z_FINISH);
            const bool complete = result == Z_STREAM_END && s.total_out == entry.size;
            inflateEnd(&s);
            if (!complete) throw std::runtime_error("The update archive is damaged: " + entry.name + " does not unpack.");
        }
        else throw std::runtime_error("The update archive uses an unsupported compression method.");
        if (Updater::Crc32(out.data(), out.size()) != entry.crc)
            throw std::runtime_error("The update archive is damaged: " + entry.name + " fails its checksum.");
        return out;
    }

    Bytes Load(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return in ? Bytes(std::istreambuf_iterator<char>(in), {}) : Bytes{};
    }

    void Store(const fs::path& path, const Bytes& data)
    {
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!out) throw std::runtime_error("Could not write " + path.string() + ". Is the folder read-only?");
        }
        if (Load(path) != data) throw std::runtime_error("Could not write " + path.string() + " intact.");
    }

    fs::path Old(const fs::path& target) { return fs::path(target.wstring() + L".old"); }
    fs::path Staged(const fs::path& target) { return fs::path(target.wstring() + L".update"); }

    // Puts the staged file in place of target, keeping the old one as
    // target.old. Renaming works on a DLL or EXE that is running.
    void Swap(const fs::path& target)
    {
        const auto staged = Staged(target), old = Old(target);
        const bool existed = fs::exists(target);
        if (existed && !MoveFileExW(target.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            FailWin32("Could not move " + target.filename().string() + " aside");
        if (!MoveFileExW(staged.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            const DWORD error = GetLastError();
            if (existed) MoveFileExW(old.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            SetLastError(error);
            FailWin32("Could not put the new " + target.filename().string() + " in place");
        }
    }

    // What the previous update left behind. The old DLL is no longer loaded
    // once the editor has restarted; one still in use by another open editor
    // stays until a later start.
    void CleanUp()
    {
        for (const char* name : {kDllName, kLauncherName})
        {
            const auto target = directory / name;
            for (const auto& leftover : {Old(target), Staged(target)})
            {
                std::error_code e;
                if (fs::exists(leftover, e) && !fs::remove(leftover, e))
                    Logger::log("Updater: could not remove " + leftover.string() + " yet");
            }
        }
    }

    // A pre-release (2.1.0-rc.1) is only offered to an editor already running
    // one; everyone else waits for the release.
    std::optional<Release> Fetch(bool includePrereleases)
    {
        const auto body = Get(kReleasesUrl, L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n", kMaxListBytes);
        const auto list = Updater::Json::parse(body.begin(), body.end(), nullptr, false);
        if (list.is_discarded()) throw std::runtime_error("GitHub returned a releases list that could not be read.");
        return Updater::Newest(list, includePrereleases);
    }

    void Install(const Release& release)
    {
        Logger::log("Updater: downloading " + release.assetUrl);
        const auto zip = Get(Wide(release.assetUrl), nullptr, kMaxArchiveBytes);
        if (release.assetSize && zip.size() != release.assetSize)
            throw std::runtime_error("The download is incomplete. Try again.");
        if (!release.sha256.empty())
        {
            if (Sha256(zip) != release.sha256) throw std::runtime_error("The download does not match the SHA-256 GitHub published for it. Try again.");
        }
        else Logger::log("Updater: GitHub published no SHA-256 for " + release.assetName + "; relying on the zip checksums");

        const auto entries = Updater::ReadZip(zip);
        const auto* dllEntry = Updater::FindEntry(entries, kDllName);
        if (!dllEntry) throw std::runtime_error(std::string("The update archive does not contain ") + kDllName + ".");
        const auto dll = Extract(zip, *dllEntry);
        if (Updater::ImageKind(dll) != Updater::Image::Dll) throw std::runtime_error(std::string("The update's ") + kDllName + " is not a 32-bit Windows DLL.");
        Bytes launcher;
        if (const auto* e = Updater::FindEntry(entries, kLauncherName))
        {
            launcher = Extract(zip, *e);
            if (Updater::ImageKind(launcher) != Updater::Image::Exe) throw std::runtime_error(std::string("The update's ") + kLauncherName + " is not a 32-bit Windows program.");
        }

        // Stage everything before replacing anything.
        const auto dllPath = directory / kDllName, launcherPath = directory / kLauncherName;
        const bool newLauncher = !launcher.empty() && Load(launcherPath) != launcher;
        Store(Staged(dllPath), dll);
        if (newLauncher) Store(Staged(launcherPath), launcher);
        try { Swap(dllPath); }
        catch (...)
        {
            std::error_code e;
            fs::remove(Staged(dllPath), e);
            fs::remove(Staged(launcherPath), e);
            throw;
        }
        if (newLauncher)
            try { Swap(launcherPath); }
            catch (const std::exception& e)
            {
                // The launcher only injects the DLL; the old one starts the new DLL fine.
                Logger::log(std::string("Updater: launcher left as it was: ") + e.what());
                std::error_code ignored;
                fs::remove(Staged(launcherPath), ignored);
            }
        Logger::log("Updater: installed " + release.version.ToString());
    }

    void Check(bool interactive)
    {
        if (busy.exchange(true))
        {
            if (interactive) Ask(L"An update check is already running.", MB_OK | MB_ICONINFORMATION);
            return;
        }
        struct Done { ~Done() { busy = false; } } done;
        const auto current = Version::Parse(RE_PLUS_VERSION);
        try
        {
            if (installed)
            {
                if (interactive)
                    Ask(L"RE+ " + Wide(installedVersion) + L" is already installed. Save your work and restart the editor to start using it.", MB_OK | MB_ICONINFORMATION);
                return;
            }
            const auto newest = Fetch(current && !current->pre.empty());
            if (!newest || !current || !(*current < newest->version))
            {
                Logger::log("Updater: up to date (" RE_PLUS_VERSION ")");
                if (interactive) Ask(L"You have the latest version, " RE_PLUS_DISPLAY_VERSION L".", MB_OK | MB_ICONINFORMATION);
                return;
            }
            const auto& release = *newest;
            if (!interactive && SkippedVersion() == release.tag) return;
            Logger::log("Updater: " + release.tag + " is available");
            std::wstring text = L"RE+ " + Wide(release.version.ToString()) + (release.prerelease ? L" (release candidate)" : L"")
                + L" is available. You have " RE_PLUS_DISPLAY_VERSION L".";
            if (const auto notes = Updater::PlainNotes(release.notes); !notes.empty()) text += L"\r\n\r\n" + Wide(notes);
            text += L"\r\n\r\nInstall it now? It takes effect when you restart the editor.\r\n\r\n"
                    L"Yes: download and install it.\r\nNo: skip this version.\r\nCancel: ask me again next time.";
            const int answer = Ask(text, MB_YESNOCANCEL | MB_ICONINFORMATION);
            if (answer == IDNO) { SetSkippedVersion(release.tag); return; }
            if (answer != IDYES) return;
            try { Install(release); }
            catch (const std::exception& e)
            {
                Logger::log(std::string("Updater: install failed: ") + e.what());
                Ask(L"The update could not be installed. The editor is unchanged.\r\n\r\n" + Wide(e.what()), MB_OK | MB_ICONERROR);
                return;
            }
            installedVersion = release.version.ToString();
            installed = true;
            SetSkippedVersion("");
            Ask(L"RE+ " + Wide(installedVersion) + L" is installed. Save your work and restart the editor to start using it.", MB_OK | MB_ICONINFORMATION);
        }
        catch (const std::exception& e)
        {
            // An automatic check stays quiet: offline is normal.
            Logger::log(std::string("Updater: check failed: ") + e.what());
            if (interactive) Ask(L"Could not check for updates.\r\n\r\n" + Wide(e.what()), MB_OK | MB_ICONWARNING);
        }
    }

    DWORD WINAPI StartupThread(LPVOID)
    {
        CleanUp();
        if (!CheckOnStartup()) return 0;
        Sleep(kStartupDelayMs);
        Check(false);
        return 0;
    }
    DWORD WINAPI CheckThread(LPVOID) { Check(true); return 0; }
}

void SelfUpdater::Initialize(const std::wstring& dllPath)
{
    directory = fs::path(dllPath).parent_path();
    // DllMain holds the loader lock; everything happens on the worker.
    if (HANDLE h = CreateThread(nullptr, 0, StartupThread, nullptr, 0, nullptr)) CloseHandle(h);
}

void SelfUpdater::AppendHelpMenu(HMENU help)
{
    if (GetMenuState(help, kCheckNow, MF_BYCOMMAND) != UINT(-1)) return;
    helpMenu = help;
    AppendMenuA(help, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(help, MF_STRING, kCheckNow, "Check for " RE_PLUS_NAME " &Updates...");
    AppendMenuA(help, MF_STRING | (CheckOnStartup() ? MF_CHECKED : MF_UNCHECKED), kToggleStartupCheck, "Check for Updates at S&tartup");
    AppendMenuA(help, MF_STRING, kAbout, "&About " RE_PLUS_NAME "...");
}

bool SelfUpdater::HandleCommand(UINT command)
{
    if (command == kAbout)
    {
        MessageBoxA(GetActiveWindow(),
            RE_PLUS_DISPLAY_VERSION "\r\n\r\n"
            RE_PLUS_FULL_NAME ": a patch for the Splinter Cell: Chaos Theory Versus map editor, "
            "built on AllyPal's Reloaded Editor 1.21.\r\n\r\n"
            "https://github.com/Lumbridge/SCCT_Versus_Reloaded_Editor",
            "About " RE_PLUS_NAME, MB_OK | MB_ICONINFORMATION);
        return true;
    }
    if (command == kCheckNow)
    {
        if (HANDLE h = CreateThread(nullptr, 0, CheckThread, nullptr, 0, nullptr)) CloseHandle(h);
        return true;
    }
    if (command == kToggleStartupCheck)
    {
        const bool on = !CheckOnStartup();
        SetCheckOnStartup(on);
        if (helpMenu) CheckMenuItem(helpMenu, kToggleStartupCheck, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
        return true;
    }
    return false;
}
