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
#include <mutex>
#include <stdexcept>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "version.lib")

using Updater::Bytes;
using Updater::Release;
using Updater::Version;
namespace fs = std::filesystem;

namespace
{
    constexpr wchar_t kReleasesUrl[] = L"https://api.github.com/repos/Lumbridge/SCCT_Versus_Reloaded_Editor/releases?per_page=20";
    // + the tag: one release, for the notes of the version running.
    constexpr wchar_t kReleaseByTagUrl[] = L"https://api.github.com/repos/Lumbridge/SCCT_Versus_Reloaded_Editor/releases/tags/";
    constexpr wchar_t kApiHeaders[] = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    constexpr char kReleasePage[] = "https://github.com/Lumbridge/SCCT_Versus_Reloaded_Editor/releases/tag/v";
    constexpr wchar_t kTitle[] = L"RE+ Update";
    // The release notes window, shown a moment after the frame is up.
    constexpr UINT_PTR kWhatsNewTimer = 0x524E;
    constexpr UINT kWhatsNewDelayMs = 1500;
    constexpr char kSection[] = "Updates";
    // Long enough for the editor to finish opening before anything asks.
    constexpr DWORD kStartupDelayMs = 8000;
    constexpr size_t kMaxListBytes = 4u << 20, kMaxArchiveBytes = 64u << 20;
    // The files a release archive may replace, beside the DLL.
    const char* const kDllName = "Reloaded.Editor.dll";
    const char* const kLauncherName = "Reloaded_Editor.exe";

    fs::path directory;
    std::atomic<bool> busy{false};
    // Set once an update or a roll back is in place, so a second check before
    // the restart does not offer the same release again.
    std::atomic<bool> installed{false};
    std::wstring installedName; // "RE+ 2.1.0" or "The previous version"
    HMENU helpMenu = nullptr;
    HWND frame = nullptr;
    // Set once the start-up clean-up has kept or removed the last update's files.
    HANDLE cleanedUp = nullptr;
    std::mutex fetchedLock;
    std::optional<Updater::WhatsNew> fetched;

    std::wstring Wide(const std::string& s)
    {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }
    std::string Narrow(const std::wstring& w)
    {
        if (w.empty()) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
        return s;
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

    // Reloaded.Editor.previous.dll, Reloaded_Editor.previous.exe: the version
    // an update replaced, which Roll Back puts back.
    fs::path Previous(const fs::path& target)
    {
        return target.parent_path() / (target.stem().wstring() + L".previous" + target.extension().wstring());
    }

    // Puts image in place of target and keeps the running target as its
    // Previous, replacing the one image was read from: a swap that leaves the
    // newer version ready to switch back to.
    void SwapWithPrevious(const fs::path& target, const Bytes& image)
    {
        const auto staged = Staged(target), previous = Previous(target);
        Store(staged, image);
        if (!MoveFileExW(target.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            const DWORD error = GetLastError();
            std::error_code ignored;
            fs::remove(staged, ignored);
            SetLastError(error);
            FailWin32("Could not move " + target.filename().string() + " aside");
        }
        if (!MoveFileExW(staged.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            const DWORD error = GetLastError();
            // The running file back, then the previous one from its copy.
            MoveFileExW(previous.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            MoveFileExW(staged.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            SetLastError(error);
            FailWin32("Could not put the previous " + target.filename().string() + " in place");
        }
    }

    // What the last update left behind. The old DLL is no longer loaded once
    // the editor has restarted, and renaming works even while another open
    // editor still uses it, so it is kept as the previous version; staged
    // files of an interrupted install are removed.
    void CleanUp()
    {
        using Updater::CleanUpAction;
        const auto dll = directory / kDllName, launcher = directory / kLauncherName;
        std::error_code e;
        Updater::Leftovers found;
        found.oldDll = fs::exists(Old(dll), e);
        found.oldLauncher = fs::exists(Old(launcher), e);
        found.previousLauncher = fs::exists(Previous(launcher), e);
        found.stagedDll = fs::exists(Staged(dll), e);
        found.stagedLauncher = fs::exists(Staged(launcher), e);
        auto keep = [](const fs::path& from, const fs::path& to) {
            if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                Logger::log("Updater: kept " + from.filename().string() + " as " + to.filename().string());
                return true;
            }
            Logger::log("Updater: could not keep " + from.filename().string() + " as " + to.filename().string()
                        + " yet (error " + std::to_string(GetLastError()) + ")");
            return false;
        };
        auto remove = [](const fs::path& path) {
            std::error_code error;
            if (fs::remove(path, error) || !fs::exists(path, error)) return true;
            Logger::log("Updater: could not remove " + path.string() + " yet");
            return false;
        };
        bool keptDll = true;
        for (const auto& step : Updater::CleanUpPlan(found))
        {
            if (step.needsDll && !keptDll) continue;
            switch (step.action)
            {
            case CleanUpAction::KeepOldDll: keptDll = keep(Old(dll), Previous(dll)); break;
            case CleanUpAction::KeepOldLauncher: keep(Old(launcher), Previous(launcher)); break;
            case CleanUpAction::RemovePreviousLauncher: remove(Previous(launcher)); break;
            case CleanUpAction::RemoveOldLauncher: remove(Old(launcher)); break;
            case CleanUpAction::RemoveStagedDll: remove(Staged(dll)); break;
            case CleanUpAction::RemoveStagedLauncher: remove(Staged(launcher)); break;
            }
        }
    }

    // The version in a DLL's file properties; nullopt for 1.x builds, which
    // did not record it.
    std::optional<Version> FileVersion(const fs::path& file)
    {
        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &ignored);
        if (!size) return std::nullopt;
        std::vector<BYTE> data(size);
        if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data())) return std::nullopt;
        struct Translation { WORD language, codePage; };
        Translation* translation = nullptr;
        UINT length = 0;
        if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translation), &length) || length < sizeof(Translation))
            return std::nullopt;
        wchar_t key[64] = {};
        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\ProductVersion", translation->language, translation->codePage);
        wchar_t* value = nullptr;
        if (!VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&value), &length) || !length || !value) return std::nullopt;
        return Updater::ProductVersion(Narrow(value));
    }

    // What's new: the notes of the release last installed.
    fs::path WhatsNewPath() { return directory / "ReloadedEditor" / "WhatsNew.txt"; }
    std::optional<Updater::WhatsNew> LoadWhatsNew()
    {
        const auto bytes = Load(WhatsNewPath());
        return Updater::WhatsNew::Parse(std::string(bytes.begin(), bytes.end()));
    }
    void SaveWhatsNew(const Updater::WhatsNew& record)
    {
        try
        {
            std::error_code e;
            fs::create_directories(WhatsNewPath().parent_path(), e);
            const auto text = record.Format();
            Store(WhatsNewPath(), Bytes(text.begin(), text.end()));
        }
        catch (const std::exception& e) { Logger::log(std::string("Updater: could not save the release notes: ") + e.what()); }
    }

    HWND whatsNewWindow = nullptr;
    LRESULT CALLBACK WhatsNewProc(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        if (message == WM_CREATE)
        {
            HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                        8, 8, 600, 400, window, reinterpret_cast<HMENU>(1), GetModuleHandle(nullptr), nullptr);
            SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            SendMessageW(edit, EM_SETLIMITTEXT, 0, 0);
            return 0;
        }
        if (message == WM_SIZE) { MoveWindow(GetDlgItem(window, 1), 8, 8, (std::max)(1, LOWORD(l) - 16), (std::max)(1, HIWORD(l) - 16), TRUE); return 0; }
        if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
        if (message == WM_NCDESTROY) whatsNewWindow = nullptr;
        return DefWindowProcW(window, message, w, l);
    }
    // A resizable, read-only window: release notes can be long.
    void ShowWhatsNew(const Updater::WhatsNew& record)
    {
        const auto caption = L"What's New in " RE_PLUS_NAME L" " + Wide(record.version);
        if (!whatsNewWindow)
        {
            static bool registered = false;
            WNDCLASSW wc{};
            wc.hInstance = GetModuleHandle(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
            wc.lpfnWndProc = WhatsNewProc;
            wc.lpszClassName = L"ReloadedWhatsNew";
            if (!registered) registered = RegisterClassW(&wc) != 0;
            whatsNewWindow = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, caption.c_str(), WS_OVERLAPPEDWINDOW,
                                             CW_USEDEFAULT, CW_USEDEFAULT, 640, 460, frame, nullptr, wc.hInstance, nullptr);
            if (!whatsNewWindow) { Logger::log("Updater: could not open the What's New window"); return; }
        }
        else SetWindowTextW(whatsNewWindow, caption.c_str());
        std::string text = record.title.empty() ? RE_PLUS_NAME " " + record.version : record.title;
        text += "\r\n\r\n" + (record.notes.empty() ? std::string("This release has no notes.") : record.notes);
        text += std::string("\r\n\r\n") + kReleasePage + record.version;
        SetWindowTextW(GetDlgItem(whatsNewWindow, 1), Wide(text).c_str());
        ShowWindow(whatsNewWindow, SW_SHOWNORMAL);
        SetForegroundWindow(whatsNewWindow);
    }

    // Once, at the first start of a version the updater installed.
    void CALLBACK WhatsNewAtStart(HWND window, UINT, UINT_PTR id, DWORD)
    {
        KillTimer(window, id);
        auto record = LoadWhatsNew();
        if (!record || !record->ShowAtStart(RE_PLUS_VERSION)) return;
        record->shown = true;
        SaveWhatsNew(*record);
        Logger::log("Updater: showing what's new in " + record->version);
        ShowWhatsNew(*record);
    }

    // Help > What's New when the notes are not kept here: the editor was
    // installed by hand, or the notes belong to another version.
    DWORD WINAPI FetchNotesThread(LPVOID)
    {
        try
        {
            const auto body = Get(kReleaseByTagUrl + Wide("v" RE_PLUS_VERSION), kApiHeaders, kMaxListBytes);
            const auto release = Updater::Json::parse(body.begin(), body.end(), nullptr, false);
            if (release.is_discarded()) throw std::runtime_error("GitHub returned a release that could not be read.");
            const auto [title, notes] = Updater::TitleAndNotes(release);
            {
                std::lock_guard lock(fetchedLock);
                fetched = Updater::WhatsNew{RE_PLUS_VERSION, title, Updater::PlainNotes(notes, Updater::WhatsNew::kNotesLimit), true};
            }
            if (!frame || !PostMessageW(frame, WM_COMMAND, SelfUpdater::kShowFetchedNotes, 0)) throw std::runtime_error("The editor window went away.");
        }
        catch (const std::exception& e)
        {
            Logger::log(std::string("Updater: could not fetch the release notes: ") + e.what());
            Ask(L"Could not fetch the notes for " RE_PLUS_DISPLAY_VERSION L".\r\n\r\n" + Wide(e.what())
                    + L"\r\n\r\nEvery release's notes are at https://github.com/Lumbridge/SCCT_Versus_Reloaded_Editor/releases",
                MB_OK | MB_ICONWARNING);
        }
        return 0;
    }

    std::wstring PreviousName(const std::optional<Version>& version)
    {
        return version ? L"RE+ " + Wide(version->ToString()) : L"the previous version";
    }
    // "Roll Back to RE+ 2.0.0..." (or Forward, after rolling back), greyed
    // when no previous version is kept or a change waits for a restart.
    void UpdateRollBackItem()
    {
        if (!helpMenu) return;
        const auto previous = Previous(directory / kDllName);
        std::error_code e;
        std::wstring label;
        UINT flags = MF_BYCOMMAND | MF_STRING;
        if (installed) label = L"Roll &Back (restart the editor to finish the change)", flags |= MF_GRAYED;
        else if (!fs::exists(previous, e)) label = L"Roll &Back to a Previous Version", flags |= MF_GRAYED;
        else
        {
            const auto version = FileVersion(previous);
            const auto current = Version::Parse(RE_PLUS_VERSION);
            const bool forward = version && current && *current < *version;
            label = std::wstring(forward ? L"Roll &Forward to " : L"Roll &Back to ") + (version ? PreviousName(version) : L"the Previous Version") + L"...";
        }
        ModifyMenuW(helpMenu, SelfUpdater::kRollBack, flags, SelfUpdater::kRollBack, label.c_str());
    }

    void RollBack()
    {
        const auto dll = directory / kDllName, launcher = directory / kLauncherName, previous = Previous(dll);
        auto tell = [](const std::wstring& text, UINT icon) { MessageBoxW(frame, text.c_str(), L"RE+ Roll Back", MB_OK | icon); };
        if (installed)
        {
            tell(installedName + L" is already in place. Save your work and restart the editor to start using it.", MB_ICONINFORMATION);
            return;
        }
        std::error_code e;
        if (!fs::exists(previous, e))
        {
            tell(L"No previous version is kept. One is kept from the next update on.", MB_ICONINFORMATION);
            UpdateRollBackItem();
            return;
        }
        const auto version = FileVersion(previous);
        const auto current = Version::Parse(RE_PLUS_VERSION);
        const bool forward = version && current && *current < *version;
        const auto skip = Updater::SkipAfterRollBack(RE_PLUS_VERSION, version);
        auto name = PreviousName(version);
        std::wstring text = std::wstring(forward ? L"Switch to " : L"Roll back to ") + name + L"?\r\n\r\n"
                            RE_PLUS_DISPLAY_VERSION L" is kept in its place, so you can switch back from the Help menu. "
                            L"The change takes effect when you restart the editor.";
        if (!skip.empty())
            text += L"\r\n\r\nThe update check at startup will not offer " RE_PLUS_DISPLAY_VERSION
                    L" again; Help > Check for RE+ Updates still can.";
        if (MessageBoxW(frame, text.c_str(), L"RE+ Roll Back", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        if (busy.exchange(true))
        {
            tell(L"An update check is running. Try again when it has finished.", MB_ICONINFORMATION);
            return;
        }
        struct Done { ~Done() { busy = false; } } done;
        try
        {
            const auto image = Load(previous);
            if (Updater::ImageKind(image) != Updater::Image::Dll)
                throw std::runtime_error("Reloaded.Editor.previous.dll is not a 32-bit Windows DLL.");
            SwapWithPrevious(dll, image);
            if (fs::exists(Previous(launcher), e))
                try
                {
                    const auto exe = Load(Previous(launcher));
                    if (Updater::ImageKind(exe) != Updater::Image::Exe) throw std::runtime_error("it is not a 32-bit Windows program");
                    SwapWithPrevious(launcher, exe);
                }
                catch (const std::exception& error)
                {
                    // The launcher only injects the DLL; either one starts either DLL.
                    Logger::log(std::string("Updater: launcher left as it was: ") + error.what());
                }
        }
        catch (const std::exception& error)
        {
            Logger::log(std::string("Updater: roll back failed: ") + error.what());
            tell(L"Could not roll back. The editor is unchanged.\r\n\r\n" + Wide(error.what()), MB_ICONERROR);
            return;
        }
        if (!skip.empty()) SetSkippedVersion(skip);
        name[0] = towupper(name[0]);
        installedName = name;
        installed = true;
        Logger::log("Updater: rolled " + std::string(forward ? "forward" : "back") + " from " RE_PLUS_VERSION " to " + Narrow(name));
        UpdateRollBackItem();
        tell(name + L" is in place. Save your work and restart the editor to start using it.", MB_ICONINFORMATION);
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
                    Ask(installedName + L" is already in place. Save your work and restart the editor to start using it.", MB_OK | MB_ICONINFORMATION);
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
            installedName = L"RE+ " + Wide(release.version.ToString());
            installed = true;
            SetSkippedVersion("");
            // Shown once by the first start of the new version.
            SaveWhatsNew({release.version.ToString(), release.name.empty() ? release.tag : release.name,
                          Updater::PlainNotes(release.notes, Updater::WhatsNew::kNotesLimit), false});
            UpdateRollBackItem();
            Ask(installedName + L" is installed. Save your work and restart the editor to start using it.", MB_OK | MB_ICONINFORMATION);
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
        SetEvent(cleanedUp);
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
    cleanedUp = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    // DllMain holds the loader lock; everything happens on the worker.
    if (HANDLE h = CreateThread(nullptr, 0, StartupThread, nullptr, 0, nullptr)) CloseHandle(h);
    else if (cleanedUp) SetEvent(cleanedUp);
}

void SelfUpdater::AppendHelpMenu(HMENU help, HWND frameWindow)
{
    if (GetMenuState(help, kCheckNow, MF_BYCOMMAND) != UINT(-1)) return;
    helpMenu = help;
    frame = frameWindow;
    // The Roll Back item names the kept version, which the clean-up may only
    // just have put in place.
    if (cleanedUp) WaitForSingleObject(cleanedUp, 3000);
    AppendMenuA(help, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(help, MF_STRING, kCheckNow, "Check for " RE_PLUS_NAME " &Updates...");
    AppendMenuA(help, MF_STRING | (CheckOnStartup() ? MF_CHECKED : MF_UNCHECKED), kToggleStartupCheck, "Check for Updates at S&tartup");
    AppendMenuA(help, MF_STRING, kWhatsNew, "What's &New in " RE_PLUS_NAME "...");
    AppendMenuA(help, MF_STRING, kRollBack, "Roll &Back");
    UpdateRollBackItem();
    AppendMenuA(help, MF_STRING, kAbout, "&About " RE_PLUS_NAME "...");
    static bool scheduled = false;
    if (!scheduled && frame) scheduled = SetTimer(frame, kWhatsNewTimer, kWhatsNewDelayMs, WhatsNewAtStart) != 0;
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
    if (command == kWhatsNew)
    {
        if (const auto record = LoadWhatsNew(); record && record->For(RE_PLUS_VERSION)) ShowWhatsNew(*record);
        else if (HANDLE h = CreateThread(nullptr, 0, FetchNotesThread, nullptr, 0, nullptr)) CloseHandle(h);
        return true;
    }
    if (command == kShowFetchedNotes)
    {
        std::optional<Updater::WhatsNew> notes;
        {
            std::lock_guard lock(fetchedLock);
            notes.swap(fetched);
        }
        if (notes) ShowWhatsNew(*notes);
        return true;
    }
    if (command == kRollBack)
    {
        RollBack();
        return true;
    }
    return false;
}
