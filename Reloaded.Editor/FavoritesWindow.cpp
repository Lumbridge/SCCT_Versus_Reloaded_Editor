#include "pch.h"
#undef min
#undef max
#include "FavoritesWindow.h"
#include "FavoritesWindowModel.h"
#include "CharacterSkinsImage.h"
#include "TextureBrowser.h"
#include "StaticMeshBrowserFavorites.h"
#include "SoundBrowserFavorites.h"
#include "logger.h"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#pragma comment(lib, "comctl32.lib")

// The favourites are read from the browsers' own sections of
// Reloaded_Editor.ini every second while the window is open, so a favourite
// added in a browser, or in another open editor, shows up here; whatever this
// window changes is written the browsers' way (a list re-read just before it
// is rewritten) and the browsers are told to reload. Which favourites are in
// memory comes from one pass over GObjects whenever the object count moves.
namespace FavoritesWindow
{
namespace
{
    namespace M = Model;
    using Address = uintptr_t;
    using M::Kind;

    // Engine layout for the supported SCCT editor build (as the browsers use it).
    constexpr Address kGEditor = 0x1165DFA0, kLog = 0x115BEFB0, kMainFrame = 0x1165df84;
    constexpr Address kGObjectsData = 0x11697B70, kGObjectsNum = 0x11697B74;
    constexpr Address kGNamesData = 0x1169CFBC, kGNamesNum = 0x1169CFC0;
    constexpr Address kBeginLoadCount = 0x11697B10;
    constexpr Address kStaticMeshClass = 0x11820B68, kSoundClass = 0x11823128;
    constexpr Address kLazyArrayLoad = 0x10EAB600;
    constexpr size_t kOuter = 0x18, kName = 0x20, kClass = 0x24, kSuper = 0x28, kNameText = 0x0C;
    constexpr size_t kCurrentMaterial = 0x138, kCurrentMesh = 0x13C;
    // UTexture: format, UBits, VBits, then TArray<FMipmap> (0x28 bytes each:
    // the lazy loader at +0x10 and its byte array at +0x1C, count +0x20).
    constexpr size_t kTexFormat = 0x5C, kTexUBits = 0x5F, kTexVBits = 0x60, kTexMips = 0x70, kTexMipCount = 0x74;
    constexpr size_t kMipStride = 0x28, kMipLoader = 0x10, kMipData = 0x1C, kMipCount = 0x20;

    constexpr UINT kTimer = 1, kSearchTimer = 2, kRetryTimer = 3;
    constexpr UINT kRefreshMessage = WM_APP + 0x61;
    constexpr int kThumb = 32;
    enum Id
    {
        TypeCombo = 100, PackageCombo, SortCombo, SearchLabel, SearchEdit,
        TagList = 110, TagEdit, NewTag, RenameTag, DeleteTag,
        List = 120,
        ShowButton = 130, UseButton, PlayButton, AddToButton, RemoveButton, LoadButton,
        Status = 140,
        // Right-click menus.
        CmdShow = 200, CmdUse, CmdPlay, CmdRemove, CmdRemoveFromTag, CmdNewTagWith, CmdCopyPath,
        CmdRenameTag, CmdDeleteTag,
        CmdAddToFirst = 300, CmdAddToLast = 899,
    };

    HWND window = nullptr;
    HFONT font = nullptr;
    HIMAGELIST images = nullptr;

    M::ListsByKind lists;
    M::Tags tags;
    std::vector<M::Entry> entries;
    std::vector<M::Row> rows;
    std::vector<M::TagRow> tagRows;
    std::vector<Favorites::Package> packages;
    M::Filter filter;
    std::string iniSeen; // every section the window reads, as last read
    std::string ini;

    // Each favourite's object while it is loaded, by lower-case path.
    std::array<std::unordered_map<std::string, void*>, M::kKindCount> objects;
    int resolvedCount = -1; // GObjects count at the last pass
    void* materialClass = nullptr;
    void* textureClass = nullptr;
    std::set<std::string> attempted; // packages already loaded or found missing
    std::unordered_map<std::string, int> thumbs; // lower-case material path -> image, -1: none

    std::string message; // the last action's report
    std::vector<M::Item> pendingTagItems; // go into the next tag created
    bool dragging = false;
    // A favourite to select in its browser once the browser has attached its controls.
    M::Item retryItem;
    int retries = 0;

    std::string Lower(const std::string& s) { return M::Lower(s); }
    std::string Identity(Kind kind, const std::string& path) { return std::string(M::KindKey(kind)) + ":" + Lower(path); }

    // ------------------------------------------------------------------
    // Engine reads, each fault-prone one in a leaf without C++ objects.

    bool ReadPointer(const void* at, void** value)
    {
        __try { *value = *reinterpret_cast<void* const*>(at); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { *value = nullptr; return false; }
    }
    bool ReadInt(const void* at, int* value)
    {
        __try { *value = *reinterpret_cast<const int*>(at); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { *value = 0; return false; }
    }
    bool ReadIdentity(void* object, void** outer, char* name, size_t size)
    {
        __try
        {
            void** names = *reinterpret_cast<void***>(kGNamesData);
            const int count = *reinterpret_cast<int*>(kGNamesNum);
            const int index = *reinterpret_cast<int*>(static_cast<char*>(object) + kName);
            if (!names || index < 0 || index >= count || !names[index]) return false;
            const char* text = static_cast<const char*>(names[index]) + kNameText;
            if (!text[0]) return false;
            strncpy_s(name, size, text, _TRUNCATE);
            *outer = *reinterpret_cast<void**>(static_cast<char*>(object) + kOuter);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { name[0] = '\0'; *outer = nullptr; return false; }
    }
    bool BuildPath(void* object, std::string& path)
    {
        char parts[32][256];
        int count = 0;
        void* cursor = object;
        while (cursor && count < 32)
        {
            void* outer = nullptr;
            if (!ReadIdentity(cursor, &outer, parts[count], sizeof(parts[count])) || outer == cursor) return false;
            ++count;
            cursor = outer;
        }
        if (!count || cursor) return false;
        path.clear();
        for (int i = count - 1; i >= 0; --i)
        {
            if (!path.empty()) path.push_back('.');
            path += parts[i];
        }
        return true;
    }
    // A class called name inside the package called package.
    bool ClassNamed(void* cls, const char* name, const char* package)
    {
        char text[64] = {}, outerText[64] = {};
        void* outer = nullptr;
        void* outerOuter = nullptr;
        return ReadIdentity(cls, &outer, text, sizeof(text)) && _stricmp(text, name) == 0 && outer &&
               ReadIdentity(outer, &outerOuter, outerText, sizeof(outerText)) && _stricmp(outerText, package) == 0 && !outerOuter;
    }
    bool IsMaterialClass(void* cls)
    {
        if (materialClass) return cls == materialClass;
        if (!ClassNamed(cls, "Material", "Engine")) return false;
        materialClass = cls;
        return true;
    }
    // Which of the three kinds the object is, if any; only kinds wanted are looked for.
    int KindOf(void* object, const std::array<bool, M::kKindCount>& wanted)
    {
        void* cls = nullptr;
        if (!object || !ReadPointer(static_cast<char*>(object) + kClass, &cls)) return -1;
        for (int depth = 0; cls && depth < 64; ++depth)
        {
            if (wanted[M::Index(Kind::Mesh)] && cls == reinterpret_cast<void*>(kStaticMeshClass)) return static_cast<int>(Kind::Mesh);
            if (wanted[M::Index(Kind::Sound)] && cls == reinterpret_cast<void*>(kSoundClass)) return static_cast<int>(Kind::Sound);
            if (wanted[M::Index(Kind::Material)] && IsMaterialClass(cls)) return static_cast<int>(Kind::Material);
            if (!ReadPointer(static_cast<char*>(cls) + kSuper, &cls)) return -1;
        }
        return -1;
    }
    bool IsTexture(void* object)
    {
        void* cls = nullptr;
        if (!ReadPointer(static_cast<char*>(object) + kClass, &cls)) return false;
        for (int depth = 0; cls && depth < 64; ++depth)
        {
            if (textureClass ? cls == textureClass : ClassNamed(cls, "Texture", "Engine"))
            {
                textureClass = cls;
                return true;
            }
            if (!ReadPointer(static_cast<char*>(cls) + kSuper, &cls)) return false;
        }
        return false;
    }
    bool ReadGObjects(void*** data, int* count)
    {
        __try
        {
            *data = *reinterpret_cast<void***>(kGObjectsData);
            *count = *reinterpret_cast<int*>(kGObjectsNum);
            return *data && *count >= 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { *data = nullptr; *count = 0; return false; }
    }
    int ObjectCount()
    {
        void** data = nullptr;
        int count = 0;
        return ReadGObjects(&data, &count) ? count : -1;
    }
    // A package or map is loading: its progress pumps messages, so the timer can
    // run in the middle of it. Nothing scans or loads then.
    bool Loading()
    {
        int count = 0;
        return !ReadInt(reinterpret_cast<const void*>(kBeginLoadCount), &count) || count > 0;
    }
    bool ExecCommand(const char* command)
    {
        __try
        {
            void* editor = *reinterpret_cast<void**>(kGEditor);
            if (!editor) return false;
            void* exec = static_cast<char*>(editor) + 0x28;
            void* method = **reinterpret_cast<void***>(exec);
            void* log = *reinterpret_cast<void**>(kLog);
            if (!method || !log) return false;
            reinterpret_cast<int(__thiscall*)(void*, const char*, void*)>(method)(exec, command, log);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool WriteEditorSlot(size_t offset, void* value)
    {
        __try
        {
            void* editor = *reinterpret_cast<void**>(kGEditor);
            if (!editor) return false;
            *reinterpret_cast<void**>(static_cast<char*>(editor) + offset) = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // The mip a thumbnail is made from: the smallest still 32 pixels across,
    // its data loaded if it was not.
    bool ReadThumbMip(void* texture, int* format, int* width, int* height, void** data, int* bytes)
    {
        __try
        {
            const char* t = static_cast<const char*>(texture);
            *format = *reinterpret_cast<const unsigned char*>(t + kTexFormat);
            const int ubits = *reinterpret_cast<const unsigned char*>(t + kTexUBits);
            const int vbits = *reinterpret_cast<const unsigned char*>(t + kTexVBits);
            char* mips = *reinterpret_cast<char* const*>(t + kTexMips);
            const int mipCount = *reinterpret_cast<const int*>(t + kTexMipCount);
            if (ubits > 13 || vbits > 13 || !mips || mipCount <= 0 || mipCount > 16) return false;
            int m = 0;
            while (m + 1 < mipCount && ((1 << ubits) >> (m + 1)) >= kThumb && ((1 << vbits) >> (m + 1)) >= kThumb) ++m;
            *width = std::max(1, (1 << ubits) >> m);
            *height = std::max(1, (1 << vbits) >> m);
            char* mip = mips + static_cast<size_t>(m) * kMipStride;
            if (*reinterpret_cast<int*>(mip + kMipCount) <= 0)
                reinterpret_cast<void(__fastcall*)(void*)>(kLazyArrayLoad)(mip + kMipLoader);
            *data = *reinterpret_cast<void**>(mip + kMipData);
            *bytes = *reinterpret_cast<int*>(mip + kMipCount);
            return *data && *bytes > 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool CopyBytes(void* to, const void* from, size_t size)
    {
        __try { memcpy(to, from, size); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // ------------------------------------------------------------------
    // Reloaded_Editor.ini.

    std::string IniPath()
    {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        if (char* slash = strrchr(exe, '\\')) slash[1] = '\0';
        return std::string(exe) + "Reloaded_Editor.ini";
    }
    std::string RawSection(const char* section)
    {
        std::vector<char> buffer(16384);
        for (;;)
        {
            const DWORD n = GetPrivateProfileSectionA(section, buffer.data(), static_cast<DWORD>(buffer.size()), ini.c_str());
            if (n + 2 < buffer.size() || buffer.size() >= (16u << 20)) return std::string(buffer.data(), n);
            buffer.resize(buffer.size() * 4);
        }
    }
    M::Lines ParseSection(const std::string& raw)
    {
        M::Lines lines;
        size_t at = 0;
        while (at < raw.size())
        {
            size_t end = raw.find('\0', at);
            if (end == std::string::npos) end = raw.size();
            const std::string line = raw.substr(at, end - at);
            const size_t eq = line.find('=');
            if (!line.empty() && eq != std::string::npos) lines.push_back({line.substr(0, eq), line.substr(eq + 1)});
            at = end + 1;
        }
        return lines;
    }
    M::Lines Section(const char* section) { return ParseSection(RawSection(section)); }

    void WriteKeys(const char* section, const M::Writes& writes)
    {
        for (const auto& w : writes)
            WritePrivateProfileStringA(section, w.key.c_str(), w.value ? w.value->c_str() : nullptr, ini.c_str());
    }
    // A browser's list is written the way the browser writes it: the whole section.
    void WriteList(Kind kind, const std::vector<std::string>& paths)
    {
        const char* section = M::ListSection(kind);
        WritePrivateProfileStringA(section, nullptr, nullptr, ini.c_str());
        for (const auto& [key, value] : M::ListLines(paths)) WritePrivateProfileStringA(section, key.c_str(), value.c_str(), ini.c_str());
    }
    void TellBrowser(Kind kind)
    {
        switch (kind)
        {
        case Kind::Material: TextureBrowser::FavoritesChanged(); break;
        case Kind::Mesh: StaticMeshBrowserFavorites::FavoritesChanged(); break;
        case Kind::Sound: SoundBrowserFavorites::FavoritesChanged(); break;
        }
    }

    // Re-reads the lists and tags; true when anything changed since last time.
    bool ReadIni(bool force)
    {
        std::string all;
        std::array<std::string, M::kKindCount> raw;
        for (Kind k : M::kKinds)
        {
            raw[M::Index(k)] = RawSection(M::ListSection(k));
            all += raw[M::Index(k)] + '\x01';
        }
        const std::string rawTags = RawSection(M::kTagSection);
        all += rawTags;
        if (!force && all == iniSeen) return false;
        iniSeen = all;
        for (Kind k : M::kKinds) lists[M::Index(k)] = M::ParseList(ParseSection(raw[M::Index(k)]));
        tags = M::ParseTags(ParseSection(rawTags));
        return true;
    }

    void LoadView()
    {
        char value[256] = {};
        GetPrivateProfileStringA(M::kViewSection, "Type", "", value, sizeof(value), ini.c_str());
        filter.kind = M::KindFromKey(value);
        GetPrivateProfileStringA(M::kViewSection, "Sort", "", value, sizeof(value), ini.c_str());
        filter.sort = Favorites::SortFromKey(value);
        GetPrivateProfileStringA(M::kViewSection, "Package", "", value, sizeof(value), ini.c_str());
        filter.package = value;
        GetPrivateProfileStringA(M::kViewSection, "Tag", "", value, sizeof(value), ini.c_str());
        const std::string tag = value;
        if (tag == "*untagged") filter.tag = {M::TagFilter::Mode::Untagged, {}};
        else if (!tag.empty()) filter.tag = {M::TagFilter::Mode::Tag, tag};
        else filter.tag = {};
    }
    void SaveView()
    {
        WritePrivateProfileStringA(M::kViewSection, "Type", filter.kind ? M::KindKey(*filter.kind) : "", ini.c_str());
        WritePrivateProfileStringA(M::kViewSection, "Sort", Favorites::SortKey(filter.sort), ini.c_str());
        WritePrivateProfileStringA(M::kViewSection, "Package", filter.package.c_str(), ini.c_str());
        const std::string tag = filter.tag.mode == M::TagFilter::Mode::Untagged ? "*untagged" : filter.tag.mode == M::TagFilter::Mode::Tag ? filter.tag.name : "";
        WritePrivateProfileStringA(M::kViewSection, "Tag", tag.c_str(), ini.c_str());
    }

    // ------------------------------------------------------------------
    // Which favourites are loaded.

    void Resolve()
    {
        for (auto& map : objects) map.clear();
        void** data = nullptr;
        int count = 0;
        if (!ReadGObjects(&data, &count)) return;
        resolvedCount = count;
        std::array<std::unordered_map<std::string, bool>, M::kKindCount> wanted;
        std::array<bool, M::kKindCount> any{};
        size_t remaining = 0;
        for (Kind k : M::kKinds)
        {
            for (const auto& path : lists[M::Index(k)]) wanted[M::Index(k)].emplace(Lower(path), false);
            any[M::Index(k)] = !wanted[M::Index(k)].empty();
            remaining += wanted[M::Index(k)].size();
        }
        for (int i = 0; i < count && remaining > 0; ++i)
        {
            void* object = nullptr;
            if (!ReadPointer(data + i, &object) || !object) continue;
            const int kind = KindOf(object, any);
            if (kind < 0) continue;
            std::string path;
            if (!BuildPath(object, path)) continue;
            auto& want = wanted[static_cast<size_t>(kind)];
            const auto found = want.find(Lower(path));
            if (found == want.end() || found->second) continue;
            found->second = true;
            objects[static_cast<size_t>(kind)][found->first] = object;
            --remaining;
        }
    }

    // The entry's object, if it is still the one found: a map change can free
    // it and reuse its memory before the next pass.
    void* Live(const M::Entry& entry)
    {
        auto& map = objects[M::Index(entry.kind)];
        const auto it = map.find(Lower(entry.path));
        if (it == map.end()) return nullptr;
        std::array<bool, M::kKindCount> want{};
        want[M::Index(entry.kind)] = true;
        std::string path;
        if (KindOf(it->second, want) != static_cast<int>(entry.kind) || !BuildPath(it->second, path) || Lower(path) != it->first)
        {
            map.erase(it);
            resolvedCount = -1;
            return nullptr;
        }
        return it->second;
    }

    std::string PackageDirectory(const char* folder)
    {
        char exe[MAX_PATH] = {};
        if (!GetModuleFileNameA(nullptr, exe, MAX_PATH)) return {};
        if (char* slash = strrchr(exe, '\\')) slash[1] = '\0';
        char full[MAX_PATH] = {};
        const std::string relative = std::string(exe) + "..\\Packages\\" + folder + "\\";
        const DWORD n = GetFullPathNameA(relative.c_str(), MAX_PATH, full, nullptr);
        return n && n < MAX_PATH ? std::string(full) : std::string{};
    }
    void FindPackages(const char* folder, const char* pattern, std::unordered_map<std::string, std::string>& files)
    {
        const std::string directory = PackageDirectory(folder);
        if (directory.empty()) return;
        WIN32_FIND_DATAA data{};
        HANDLE search = FindFirstFileA((directory + pattern).c_str(), &data);
        if (search == INVALID_HANDLE_VALUE) return;
        do
        {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string name = data.cFileName;
            const size_t dot = name.rfind('.');
            files.emplace(Lower(dot == std::string::npos ? name : name.substr(0, dot)), directory + name);
        } while (FindNextFileA(search, &data));
        FindClose(search);
    }

    std::vector<M::Entry> BuildEntries()
    {
        return M::Entries(lists, tags, [](Kind kind, const std::string& path) { return objects[M::Index(kind)].count(Lower(path)) != 0; });
    }

    // Loads packages with OBJ LOAD, as the browsers' Favorites views do, and
    // finds the favourites again. Returns how many loaded.
    int LoadPackages(const std::vector<M::PackageLoad>& loads)
    {
        if (loads.empty() || Loading()) return 0;
        std::array<std::unordered_map<std::string, std::string>, M::kKindCount> files;
        // Materials live in texture packages, and in static mesh packages that carry their own.
        FindPackages("Textures", "*.utx", files[M::Index(Kind::Material)]);
        FindPackages("StaticMeshes", "*.usx", files[M::Index(Kind::Material)]);
        FindPackages("StaticMeshes", "*.usx", files[M::Index(Kind::Mesh)]);
        FindPackages("Sounds", "*.uax", files[M::Index(Kind::Sound)]);
        int loaded = 0, missing = 0;
        for (const auto& load : loads)
        {
            attempted.insert(M::AttemptKey(load));
            const auto& map = files[M::Index(load.kind)];
            const auto file = map.find(Lower(load.package));
            if (file == map.end()) { ++missing; continue; }
            const std::string command = "OBJ LOAD FILE=\"" + file->second + "\"";
            if (ExecCommand(command.c_str())) ++loaded;
            else ++missing;
        }
        if (loaded) Logger::log("FavoritesWindow: loaded " + std::to_string(loaded) + " favourite package(s)");
        if (missing) Logger::log("FavoritesWindow: could not locate or load " + std::to_string(missing) + " favourite package(s)");
        Resolve();
        return loaded;
    }
    // The packages of favourites that are not in memory; with retry, packages
    // tried before are tried again.
    int LoadMissing(bool retry)
    {
        if (Loading()) return 0;
        if (retry) attempted.clear();
        return LoadPackages(M::MissingPackages(BuildEntries(), attempted));
    }

    // ------------------------------------------------------------------
    // Thumbnails.

    HBITMAP NewBitmap(std::uint32_t** bits)
    {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = kThumb;
        info.bmiHeader.biHeight = -kThumb;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        *bits = static_cast<std::uint32_t*>(pixels);
        return bitmap;
    }
    void Opaque(std::uint32_t* bits)
    {
        GdiFlush();
        for (int i = 0; i < kThumb * kThumb; ++i) bits[i] |= 0xFF000000u;
    }
    // The three type glyphs, images 0 to 2, for rows with no thumbnail.
    void AddGlyphs()
    {
        const char* labels[] = {"MAT", "SM", "SND"};
        const COLORREF colours[] = {RGB(196, 120, 40), RGB(70, 120, 190), RGB(60, 150, 90)};
        HDC dc = CreateCompatibleDC(nullptr);
        for (int i = 0; i < 3; ++i)
        {
            std::uint32_t* bits = nullptr;
            HBITMAP bitmap = NewBitmap(&bits);
            if (!bitmap) continue;
            HGDIOBJ old = SelectObject(dc, bitmap);
            RECT r{0, 0, kThumb, kThumb};
            FillRect(dc, &r, GetSysColorBrush(COLOR_WINDOW));
            RECT box{3, 7, kThumb - 3, kThumb - 7};
            HBRUSH brush = CreateSolidBrush(colours[i]);
            FillRect(dc, &box, brush);
            DeleteObject(brush);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            HGDIOBJ oldFont = SelectObject(dc, font);
            DrawTextA(dc, labels[i], -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, oldFont);
            SelectObject(dc, old);
            Opaque(bits);
            ImageList_Add(images, bitmap, nullptr);
            DeleteObject(bitmap);
        }
        DeleteDC(dc);
    }
    void ResetImages()
    {
        if (images) ImageList_RemoveAll(images);
        else images = ImageList_Create(kThumb, kThumb, ILC_COLOR32, 64, 64);
        thumbs.clear();
        AddGlyphs();
    }
    // The material's thumbnail image, made the first time: a texture's own
    // pixels (DXT1/3/5 and RGBA8); any other material keeps its type glyph.
    int Thumbnail(const M::Entry& entry)
    {
        const int glyph = static_cast<int>(M::Index(entry.kind));
        if (entry.kind != Kind::Material || !entry.loaded) return glyph;
        const std::string key = Lower(entry.path);
        const auto cached = thumbs.find(key);
        if (cached != thumbs.end()) return cached->second < 0 ? glyph : cached->second;
        if (thumbs.size() > 2048) ResetImages();
        int image = -1;
        void* texture = Live(entry);
        int format = -1, width = 0, height = 0, bytes = 0;
        void* data = nullptr;
        if (texture && IsTexture(texture) && ReadThumbMip(texture, &format, &width, &height, &data, &bytes))
        {
            const size_t blocks = static_cast<size_t>((width + 3) / 4) * ((height + 3) / 4);
            size_t need = 0;
            if (format == Snapshot::kFormatDxt1) need = blocks * 8;
            else if (format == Snapshot::kFormatDxt3 || format == Snapshot::kFormatDxt5) need = blocks * 16;
            else if (format == Snapshot::kFormatRgba8) need = static_cast<size_t>(width) * height * 4;
            CharacterSkins::Mip mip;
            mip.format = format;
            mip.width = width;
            mip.height = height;
            if (need && static_cast<size_t>(bytes) >= need)
            {
                mip.data.resize(need);
                if (CopyBytes(mip.data.data(), data, need))
                {
                    try
                    {
                        const auto picture = CharacterSkins::Decode(mip);
                        std::uint32_t* bits = nullptr;
                        if (HBITMAP bitmap = NewBitmap(&bits))
                        {
                            const COLORREF back = GetSysColor(COLOR_WINDOW);
                            const std::uint32_t fill = 0xFF000000u | (GetRValue(back) << 16) | (GetGValue(back) << 8) | GetBValue(back);
                            std::fill(bits, bits + kThumb * kThumb, fill);
                            const int across = width >= height ? kThumb : std::max(1, kThumb * width / height);
                            const int down = height >= width ? kThumb : std::max(1, kThumb * height / width);
                            const int left = (kThumb - across) / 2, top = (kThumb - down) / 2;
                            for (int y = 0; y < down; ++y)
                                for (int x = 0; x < across; ++x)
                                {
                                    const size_t sx = static_cast<size_t>(x) * width / across, sy = static_cast<size_t>(y) * height / down;
                                    const std::uint8_t* p = &picture.rgba[(sy * width + sx) * 4];
                                    bits[(top + y) * kThumb + left + x] = 0xFF000000u | (p[0] << 16) | (p[1] << 8) | p[2];
                                }
                            image = ImageList_Add(images, bitmap, nullptr);
                            DeleteObject(bitmap);
                        }
                    }
                    catch (const std::exception&)
                    {
                        image = -1;
                    }
                }
            }
        }
        thumbs[key] = image;
        return image < 0 ? glyph : image;
    }

    // ------------------------------------------------------------------
    // The window's contents.

    HWND Child(int id) { return GetDlgItem(window, id); }

    void ShowStatus()
    {
        std::string text = M::Summary(entries, rows.size());
        if (!message.empty()) text = message + "\r\n" + text;
        SetWindowTextA(Child(Status), text.c_str());
    }
    void Report(const std::string& text)
    {
        message = text;
        ShowStatus();
    }

    std::vector<int> SelectedRows()
    {
        std::vector<int> out;
        HWND list = Child(List);
        for (int i = ListView_GetNextItem(list, -1, LVNI_SELECTED); i >= 0; i = ListView_GetNextItem(list, i, LVNI_SELECTED))
            if (static_cast<size_t>(i) < rows.size()) out.push_back(i);
        return out;
    }
    std::vector<M::Item> SelectedItems()
    {
        std::vector<M::Item> out;
        for (int i : SelectedRows())
        {
            const auto& e = entries[rows[i].entry];
            out.push_back({e.kind, e.path});
        }
        return out;
    }
    // The row the actions that take one favourite act on: the focused one if
    // selected, else the first selected.
    const M::Entry* Current()
    {
        HWND list = Child(List);
        const int focused = ListView_GetNextItem(list, -1, LVNI_FOCUSED);
        if (focused >= 0 && static_cast<size_t>(focused) < rows.size() && (ListView_GetItemState(list, focused, LVIS_SELECTED) & LVIS_SELECTED))
            return &entries[rows[focused].entry];
        const auto selected = SelectedRows();
        return selected.empty() ? nullptr : &entries[rows[selected.front()].entry];
    }

    void UpdateButtons()
    {
        const M::Entry* e = Current();
        const bool any = !SelectedRows().empty();
        EnableWindow(Child(ShowButton), e != nullptr);
        EnableWindow(Child(UseButton), e != nullptr);
        EnableWindow(Child(PlayButton), e && e->kind == Kind::Sound);
        EnableWindow(Child(AddToButton), any);
        EnableWindow(Child(RemoveButton), any);
        const bool tagRow = filter.tag.mode == M::TagFilter::Mode::Tag;
        EnableWindow(Child(RenameTag), tagRow);
        EnableWindow(Child(DeleteTag), tagRow);
    }

    void FillTags()
    {
        tagRows = M::TagRows(entries, tags, filter.kind);
        if (filter.tag.mode == M::TagFilter::Mode::Tag && !tags.Has(filter.tag.name)) filter.tag = {};
        HWND box = Child(TagList);
        SendMessageA(box, WM_SETREDRAW, FALSE, 0);
        const int top = static_cast<int>(SendMessageA(box, LB_GETTOPINDEX, 0, 0));
        SendMessageA(box, LB_RESETCONTENT, 0, 0);
        int selected = 0;
        for (size_t i = 0; i < tagRows.size(); ++i)
        {
            SendMessageA(box, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tagRows[i].label.c_str()));
            const auto& f = tagRows[i].filter;
            if (f.mode == filter.tag.mode && (f.mode != M::TagFilter::Mode::Tag || Lower(f.name) == Lower(filter.tag.name)))
                selected = static_cast<int>(i);
        }
        SendMessageA(box, LB_SETCURSEL, selected, 0);
        SendMessageA(box, LB_SETTOPINDEX, top, 0);
        SendMessageA(box, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(box, nullptr, TRUE);
    }

    void FillPackages()
    {
        size_t total = 0;
        M::Filter others = filter;
        packages = M::Packages(entries, others, &total);
        if (!filter.package.empty() &&
            std::none_of(packages.begin(), packages.end(), [](const Favorites::Package& p) { return Lower(p.name) == Lower(filter.package); }))
            filter.package.clear();
        HWND combo = Child(PackageCombo);
        SendMessageA(combo, WM_SETREDRAW, FALSE, 0);
        SendMessageA(combo, CB_RESETCONTENT, 0, 0);
        SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Favorites::AllLabel(total).c_str()));
        int selected = 0;
        for (size_t i = 0; i < packages.size(); ++i)
        {
            SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Favorites::PackageLabel(packages[i]).c_str()));
            if (Lower(packages[i].name) == Lower(filter.package)) selected = static_cast<int>(i) + 1;
        }
        SendMessageA(combo, CB_SETCURSEL, selected, 0);
        SendMessageA(combo, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(combo, nullptr, TRUE);
    }

    // The selection, focus and scroll position, kept across a rebuild.
    struct Keep
    {
        std::set<std::string> selected;
        std::string focused;
        int top = 0;
    };
    Keep Remember()
    {
        Keep keep;
        HWND list = Child(List);
        for (int i : SelectedRows())
        {
            const auto& e = entries[rows[i].entry];
            keep.selected.insert(Identity(e.kind, e.path));
        }
        if (const M::Entry* e = Current()) keep.focused = Identity(e->kind, e->path);
        keep.top = ListView_GetTopIndex(list);
        return keep;
    }

    void Rebuild()
    {
        if (!window) return;
        const Keep keep = Remember();
        entries = BuildEntries();
        FillTags();
        FillPackages();
        rows = M::Rows(entries, filter);
        HWND list = Child(List);
        SendMessageA(list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list);
        int focus = -1;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const auto& row = rows[i];
            const auto& e = entries[row.entry];
            LVITEMA item{};
            item.mask = LVIF_TEXT | LVIF_IMAGE;
            item.iItem = static_cast<int>(i);
            item.pszText = const_cast<char*>(row.name.c_str());
            item.iImage = Thumbnail(e);
            const int at = static_cast<int>(SendMessageA(list, LVM_INSERTITEMA, 0, reinterpret_cast<LPARAM>(&item)));
            if (at < 0) continue;
            const std::string* columns[] = {&row.type, &row.location, &row.tags};
            for (int c = 0; c < 3; ++c)
            {
                LVITEMA sub{};
                sub.iSubItem = c + 1;
                sub.pszText = const_cast<char*>(columns[c]->c_str());
                SendMessageA(list, LVM_SETITEMTEXTA, at, reinterpret_cast<LPARAM>(&sub));
            }
            const std::string id = Identity(e.kind, e.path);
            if (keep.selected.count(id)) ListView_SetItemState(list, at, LVIS_SELECTED, LVIS_SELECTED);
            if (id == keep.focused) focus = at;
        }
        if (focus >= 0) ListView_SetItemState(list, focus, LVIS_FOCUSED, LVIS_FOCUSED);
        if (keep.top > 0 && !rows.empty())
        {
            ListView_EnsureVisible(list, static_cast<int>(rows.size()) - 1, FALSE);
            ListView_EnsureVisible(list, std::min(keep.top, static_cast<int>(rows.size()) - 1), FALSE);
        }
        SendMessageA(list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list, nullptr, TRUE);
        UpdateButtons();
        ShowStatus();
    }

    // Re-reads the ini and the objects when either moved (or always, with force).
    void Refresh(bool force)
    {
        if (!window || dragging) return;
        const bool listsChanged = ReadIni(force);
        const bool loading = Loading();
        const int count = ObjectCount();
        if (!force && !listsChanged && (loading || count == resolvedCount)) return;
        if (!loading) Resolve();
        Rebuild();
    }

    // ------------------------------------------------------------------
    // Actions.

    HWND Frame()
    {
        void* frame = nullptr;
        void* hwnd = nullptr;
        if (ReadPointer(reinterpret_cast<const void*>(kMainFrame), &frame) && frame &&
            ReadPointer(static_cast<char*>(frame) + 4, &hwnd) && IsWindow(static_cast<HWND>(hwnd)))
            return static_cast<HWND>(hwnd);
        return GetActiveWindow();
    }

    // The frame menu's command whose text contains the words, ignoring & and case.
    UINT MenuCommand(HMENU menu, const std::string& words, int depth = 0)
    {
        if (!menu || depth > 2) return 0;
        for (int i = 0; i < GetMenuItemCount(menu); ++i)
        {
            if (HMENU sub = GetSubMenu(menu, i))
            {
                if (UINT found = MenuCommand(sub, words, depth + 1)) return found;
                continue;
            }
            char text[128] = {};
            GetMenuStringA(menu, i, text, sizeof(text), MF_BYPOSITION);
            std::string plain;
            for (const char* c = text; *c && *c != '\t'; ++c)
                if (*c != '&') plain.push_back(*c);
            if (Lower(plain).find(words) != std::string::npos) return GetMenuItemID(menu, i);
        }
        return 0;
    }
    // The window with the editor's menu bar: the one whose menu holds View >
    // Advanced Options (40065).
    bool HasViewMenu(HWND hwnd)
    {
        HMENU bar = hwnd ? GetMenu(hwnd) : nullptr;
        for (int i = 0; bar && i < GetMenuItemCount(bar); ++i)
            if (HMENU sub = GetSubMenu(bar, i))
                if (GetMenuState(sub, 40065, MF_BYCOMMAND) != UINT(-1)) return true;
        return false;
    }
    BOOL CALLBACK FindMenuFrame(HWND hwnd, LPARAM found)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId() || !HasViewMenu(hwnd)) return TRUE;
        *reinterpret_cast<HWND*>(found) = hwnd;
        return FALSE;
    }
    HWND MenuFrame()
    {
        HWND frame = Frame();
        if (HasViewMenu(frame)) return frame;
        frame = nullptr;
        EnumWindows(FindMenuFrame, reinterpret_cast<LPARAM>(&frame));
        return frame;
    }
    bool OpenBrowser(Kind kind)
    {
        HWND frame = MenuFrame();
        const char* words = kind == Kind::Material ? "texture browser" : kind == Kind::Mesh ? "static mesh browser" : "sound browser";
        const UINT command = MenuCommand(GetMenu(frame), words);
        if (!command) return false;
        SendMessageA(frame, WM_COMMAND, MAKEWPARAM(command, 0), 0);
        return true;
    }
    bool ShowInItsBrowser(const M::Item& item)
    {
        switch (item.kind)
        {
        case Kind::Material: return TextureBrowser::ShowFavorite(item.path);
        case Kind::Mesh: return StaticMeshBrowserFavorites::ShowFavorite(item.path);
        case Kind::Sound: return SoundBrowserFavorites::ShowFavorite(item.path);
        }
        return false;
    }

    // Loads the entry's package if it is not in memory; its object, if any.
    void* Ensure(const M::Entry& entry)
    {
        if (void* object = Live(entry)) return object;
        if (Loading()) return nullptr;
        LoadPackages({{entry.kind, Favorites::Split(entry.path).package}});
        auto& map = objects[M::Index(entry.kind)];
        const auto it = map.find(Lower(entry.path));
        return it == map.end() ? nullptr : it->second;
    }

    void Show(const M::Entry& entry)
    {
        const std::string name = Favorites::Split(entry.path).name;
        // The browser highlights its current material or mesh.
        if (entry.kind != Kind::Sound)
            if (void* object = Live(entry))
                WriteEditorSlot(entry.kind == Kind::Material ? kCurrentMaterial : kCurrentMesh, object);
        if (!OpenBrowser(entry.kind))
        {
            Report("Could not find the " + std::string(entry.kind == Kind::Material ? "Texture" : entry.kind == Kind::Mesh ? "Static Mesh" : "Sound") +
                   " Browser in the View menu.");
            return;
        }
        if (ShowInItsBrowser({entry.kind, entry.path}))
        {
            Report("Showing " + name + " in the browser's Favorites.");
            return;
        }
        // A browser opened just now attaches its Favorites controls a moment later.
        retryItem = {entry.kind, entry.path};
        retries = 5;
        SetTimer(window, kRetryTimer, 200, nullptr);
        Report("Opening the browser for " + name + "...");
    }

    void Use(const M::Entry& entry)
    {
        const std::string name = Favorites::Split(entry.path).name;
        if (entry.kind == Kind::Sound)
        {
            // A property's Use button takes the Sound Browser's selection.
            Show(entry);
            if (message.rfind("Showing", 0) == 0) Report(name + " is selected in the Sound Browser: a property's Use button takes it.");
            return;
        }
        void* object = Ensure(entry);
        if (!object)
        {
            Report(name + " is not loaded and its package could not be loaded.");
            Rebuild();
            return;
        }
        bool done = false;
        if (entry.kind == Kind::Mesh) done = StaticMeshBrowserFavorites::UseMesh(object);
        if (!done) done = WriteEditorSlot(entry.kind == Kind::Material ? kCurrentMaterial : kCurrentMesh, object);
        if (entry.kind == Kind::Material) TextureBrowser::Redraw();
        Report(!done ? "Could not make " + name + " the current one."
               : entry.kind == Kind::Material ? name + " is the current material: apply it to surfaces as from the Texture Browser."
                                              : name + " is the current static mesh: place it from the viewport's right-click menu.");
        Rebuild();
    }

    void Play(const M::Entry& entry)
    {
        if (entry.kind != Kind::Sound) return;
        const std::string name = Favorites::Split(entry.path).name;
        void* sound = Ensure(entry);
        if (!sound) { Report(name + " is not loaded and its package could not be loaded."); Rebuild(); return; }
        Report(SoundBrowserFavorites::Play(sound, entry.path) ? "Playing " + name + "." : "Could not play " + name + ".");
    }

    void Remove(const std::vector<M::Item>& items)
    {
        if (items.empty()) return;
        std::array<bool, M::kKindCount> touched{};
        size_t removed = 0;
        for (Kind k : M::kKinds)
        {
            // Start from the list as saved now, as the browsers do.
            auto saved = M::ParseList(Section(M::ListSection(k)));
            const size_t before = saved.size();
            for (const auto& item : items)
                if (item.kind == k) saved = SoundFavorites::Remove(saved, item.path);
            if (saved.size() == before) continue;
            removed += before - saved.size();
            WriteList(k, saved);
            touched[M::Index(k)] = true;
        }
        WriteKeys(M::kTagSection, M::ForgetItems(M::ParseTags(Section(M::kTagSection)), items));
        for (Kind k : M::kKinds)
            if (touched[M::Index(k)]) TellBrowser(k);
        Report(removed == 1 ? "Removed 1 favourite." : "Removed " + std::to_string(removed) + " favourites.");
        Refresh(true);
    }

    // Each tag change re-reads the tags, then writes only the keys it changes.
    template <class Change>
    bool ChangeTags(Change&& change, const std::string& done)
    {
        try
        {
            const auto writes = change(M::ParseTags(Section(M::kTagSection)));
            WriteKeys(M::kTagSection, writes);
            Report(done);
            Refresh(true);
            return true;
        }
        catch (const std::exception& error)
        {
            Report(error.what());
            return false;
        }
    }
    std::string Count(size_t n) { return n == 1 ? "1 favourite" : std::to_string(n) + " favourites"; }

    void AddTo(const std::vector<M::Item>& items, const std::string& tag)
    {
        if (items.empty()) { Report("Select the favourites to tag first."); return; }
        ChangeTags([&](const M::Tags& t) { return M::AddToTag(t, items, tag); }, "Added " + Count(items.size()) + " to " + tag + ".");
    }
    void RemoveFromTag(const std::vector<M::Item>& items, const std::string& tag)
    {
        ChangeTags([&](const M::Tags& t) { return M::RemoveFromTag(t, items, tag); }, "Removed " + Count(items.size()) + " from " + tag + ".");
    }
    std::string TagText()
    {
        char text[128] = {};
        GetWindowTextA(Child(TagEdit), text, sizeof(text));
        return text;
    }
    void CreateTagFromBox()
    {
        const std::string name = M::CleanTagName(TagText());
        auto items = std::move(pendingTagItems);
        pendingTagItems.clear();
        if (!ChangeTags([&](const M::Tags& t) { return M::CreateTag(t, name); }, "Created the tag " + name + "."))
            return;
        if (!items.empty()) AddTo(items, name);
        SetWindowTextA(Child(TagEdit), "");
    }
    void RenameSelectedTag()
    {
        if (filter.tag.mode != M::TagFilter::Mode::Tag) { Report("Select a tag on the left first."); return; }
        const std::string from = filter.tag.name, to = M::CleanTagName(TagText());
        // The filter follows the tag to its new name, so the refresh keeps it selected.
        filter.tag.name = to;
        if (ChangeTags([&](const M::Tags& t) { return M::RenameTag(t, from, to); }, "Renamed " + from + " to " + to + "."))
            SaveView();
        else
            filter.tag.name = from;
    }
    void DeleteSelectedTag()
    {
        if (filter.tag.mode != M::TagFilter::Mode::Tag) { Report("Select a tag on the left first."); return; }
        const std::string name = filter.tag.name;
        ChangeTags([&](const M::Tags& t) { return M::DeleteTag(t, name); }, "Deleted the tag " + name + "; its favourites are still favourites.");
        filter.tag = {};
        SaveView();
        Refresh(true);
        SetWindowTextA(Child(TagEdit), "");
    }

    void CopyPaths()
    {
        std::string text;
        for (const auto& item : SelectedItems()) text += item.path + "\r\n";
        if (text.empty() || !OpenClipboard(window)) return;
        EmptyClipboard();
        if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1))
        {
            memcpy(GlobalLock(memory), text.c_str(), text.size() + 1);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_TEXT, memory)) GlobalFree(memory);
        }
        CloseClipboard();
        Report("Copied " + std::to_string(SelectedItems().size()) + " path(s).");
    }

    // "Add to Tag": each tag (ticked when every selected favourite has it), then New Tag.
    HMENU AddToMenu()
    {
        HMENU menu = CreatePopupMenu();
        const auto selected = SelectedRows();
        std::vector<std::string> names = tags.names;
        std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });
        for (size_t i = 0; i < names.size() && CmdAddToFirst + i <= CmdAddToLast; ++i)
        {
            const bool all = !selected.empty() && std::all_of(selected.begin(), selected.end(), [&](int r) { return M::HasTag(entries[rows[r].entry], names[i]); });
            AppendMenuA(menu, MF_STRING | (all ? MF_CHECKED : 0), CmdAddToFirst + i, names[i].c_str());
        }
        if (!names.empty()) AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(menu, MF_STRING, CmdNewTagWith, "&New Tag...");
        return menu;
    }
    std::string AddToName(UINT command)
    {
        std::vector<std::string> names = tags.names;
        std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });
        const size_t i = command - CmdAddToFirst;
        return i < names.size() ? names[i] : std::string{};
    }

    void Command(UINT id);

    void ShowListMenu(POINT screen)
    {
        const M::Entry* e = Current();
        if (!e) return;
        HMENU menu = CreatePopupMenu();
        AppendMenuA(menu, MF_STRING, CmdShow, "&Show in Browser");
        AppendMenuA(menu, MF_STRING, CmdUse, e->kind == Kind::Sound ? "&Use (select in the Sound Browser)" : "&Use as Current");
        if (e->kind == Kind::Sound) AppendMenuA(menu, MF_STRING, CmdPlay, "&Play");
        AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(AddToMenu()), "&Add to Tag");
        if (filter.tag.mode == M::TagFilter::Mode::Tag)
            AppendMenuA(menu, MF_STRING, CmdRemoveFromTag, ("Remove from &Tag \"" + filter.tag.name + "\"").c_str());
        AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(menu, MF_STRING, CmdCopyPath, "&Copy Path");
        AppendMenuA(menu, MF_STRING, CmdRemove, "&Remove from Favourites");
        const UINT command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, screen.x, screen.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (command) Command(command);
    }
    void ShowTagMenu(POINT screen)
    {
        HWND box = Child(TagList);
        POINT client = screen;
        ScreenToClient(box, &client);
        const LRESULT hit = SendMessageA(box, LB_ITEMFROMPOINT, 0, MAKELPARAM(client.x, client.y));
        if (HIWORD(hit)) return;
        const int index = LOWORD(hit);
        if (index < 0 || static_cast<size_t>(index) >= tagRows.size() || tagRows[index].filter.mode != M::TagFilter::Mode::Tag) return;
        SendMessageA(box, LB_SETCURSEL, index, 0);
        SendMessageA(window, WM_COMMAND, MAKEWPARAM(TagList, LBN_SELCHANGE), reinterpret_cast<LPARAM>(box));
        HMENU menu = CreatePopupMenu();
        AppendMenuA(menu, MF_STRING, CmdRenameTag, "&Rename (type the new name below)");
        AppendMenuA(menu, MF_STRING, CmdDeleteTag, "&Delete Tag");
        const UINT command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, screen.x, screen.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (command) Command(command);
    }

    void Command(UINT id)
    {
        const M::Entry* e = Current();
        // Copies: the actions below rebuild the entries.
        const M::Entry entry = e ? *e : M::Entry{};
        if (id != CmdNewTagWith && id != NewTag) pendingTagItems.clear();
        switch (id)
        {
        case ShowButton: case CmdShow: if (e) Show(entry); return;
        case UseButton: case CmdUse: if (e) Use(entry); return;
        case PlayButton: case CmdPlay: if (e) Play(entry); return;
        case RemoveButton: case CmdRemove: Remove(SelectedItems()); return;
        case LoadButton:
        {
            const int loaded = LoadMissing(true);
            Report(loaded ? "Loaded " + std::to_string(loaded) + " package(s)." : "No more favourites could be loaded.");
            Refresh(true);
            return;
        }
        case CmdRemoveFromTag:
            if (filter.tag.mode == M::TagFilter::Mode::Tag) RemoveFromTag(SelectedItems(), filter.tag.name);
            return;
        case CmdNewTagWith:
            pendingTagItems = SelectedItems();
            SetWindowTextA(Child(TagEdit), "");
            SetFocus(Child(TagEdit));
            Report("Type the new tag's name below and press Enter; the " + Count(pendingTagItems.size()) + " selected go in it.");
            return;
        case NewTag: CreateTagFromBox(); return;
        case RenameTag: RenameSelectedTag(); return;
        case CmdRenameTag:
            SetWindowTextA(Child(TagEdit), filter.tag.name.c_str());
            SetFocus(Child(TagEdit));
            SendMessageA(Child(TagEdit), EM_SETSEL, 0, -1);
            Report("Type the tag's new name below and press Rename.");
            return;
        case DeleteTag: case CmdDeleteTag: DeleteSelectedTag(); return;
        case CmdCopyPath: CopyPaths(); return;
        case AddToButton:
        {
            RECT r{};
            GetWindowRect(Child(AddToButton), &r);
            HMENU menu = AddToMenu();
            const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD, r.left, r.bottom, 0, window, nullptr);
            DestroyMenu(menu);
            if (command) Command(command);
            return;
        }
        default:
            if (id >= CmdAddToFirst && id <= CmdAddToLast)
            {
                const std::string name = AddToName(id);
                if (!name.empty()) AddTo(SelectedItems(), name);
            }
        }
    }

    // ------------------------------------------------------------------
    // Layout and the window procedure.

    void Layout(int width, int height)
    {
        constexpr int pad = 8, row = 24, left = 190;
        const int listLeft = pad + left + 6;
        int x = pad;
        MoveWindow(Child(TypeCombo), x, pad, 130, 300, TRUE);
        x += 136;
        MoveWindow(Child(PackageCombo), x, pad, 190, 300, TRUE);
        x += 196;
        MoveWindow(Child(SortCombo), x, pad, 150, 300, TRUE);
        x += 158;
        MoveWindow(Child(SearchLabel), x, pad + 4, 46, row - 4, TRUE);
        x += 48;
        MoveWindow(Child(SearchEdit), x, pad, std::max(60, width - pad - x), row - 2, TRUE);

        const int top = pad + row + 6, statusTop = height - pad - 34, buttonsTop = statusTop - row - 8;
        MoveWindow(Child(TagList), pad, top, left, std::max(40, buttonsTop - top - 2 * (row + 4)), TRUE);
        MoveWindow(Child(TagEdit), pad, buttonsTop - 2 * (row + 4) + 4, left, row - 2, TRUE);
        const int tagButton = (left - 8) / 3;
        MoveWindow(Child(NewTag), pad, buttonsTop - row - 4 + 4, tagButton, row, TRUE);
        MoveWindow(Child(RenameTag), pad + tagButton + 4, buttonsTop - row - 4 + 4, tagButton, row, TRUE);
        MoveWindow(Child(DeleteTag), pad + 2 * (tagButton + 4), buttonsTop - row - 4 + 4, tagButton, row, TRUE);

        MoveWindow(Child(List), listLeft, top, std::max(60, width - pad - listLeft), std::max(40, buttonsTop - 6 - top), TRUE);
        const int ids[] = {ShowButton, UseButton, PlayButton, AddToButton, RemoveButton, LoadButton};
        const int count = static_cast<int>(std::size(ids));
        const int button = std::max(60, (width - pad - listLeft - (count - 1) * 6) / count);
        for (int i = 0; i < count; ++i) MoveWindow(Child(ids[i]), listLeft + i * (button + 6), buttonsTop + 4, button, row, TRUE);
        MoveWindow(Child(Status), pad, statusTop + 4, std::max(60, width - 2 * pad), 34, TRUE);
    }

    void SavePlacement()
    {
        RECT r{};
        if (!window || IsIconic(window) || !GetWindowRect(window, &r)) return;
        const std::string value = std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right - r.left) + "," + std::to_string(r.bottom - r.top);
        WritePrivateProfileStringA(M::kViewSection, "Window", value.c_str(), ini.c_str());
    }
    RECT LoadPlacement()
    {
        RECT r{CW_USEDEFAULT, CW_USEDEFAULT, 860, 540};
        char value[64] = {};
        GetPrivateProfileStringA(M::kViewSection, "Window", "", value, sizeof(value), ini.c_str());
        int x = 0, y = 0, w = 0, h = 0;
        if (sscanf_s(value, "%d,%d,%d,%d", &x, &y, &w, &h) == 4 && w >= 400 && h >= 300 && w < 10000 && h < 10000)
        {
            RECT placed{x, y, x + w, y + h};
            if (MonitorFromRect(&placed, MONITOR_DEFAULTTONULL)) r = {x, y, w, h};
        }
        return r;
    }

    // Enter in the tag box makes the tag; Escape forgets a pending "New Tag".
    LRESULT CALLBACK TagEditProc(HWND edit, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR)
    {
        if (msg == WM_KEYDOWN && w == VK_RETURN) { PostMessageA(window, WM_COMMAND, MAKEWPARAM(NewTag, BN_CLICKED), 0); return 0; }
        if (msg == WM_KEYDOWN && w == VK_ESCAPE) { pendingTagItems.clear(); SetWindowTextA(edit, ""); Report(""); return 0; }
        if (msg == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;
        if (msg == WM_NCDESTROY) RemoveWindowSubclass(edit, TagEditProc, 1);
        return DefSubclassProc(edit, msg, w, l);
    }
    LRESULT CALLBACK SearchEditProc(HWND edit, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR)
    {
        if (msg == WM_KEYDOWN && w == VK_ESCAPE) { SetWindowTextA(edit, ""); return 0; }
        if (msg == WM_KEYDOWN && w == VK_DOWN) { SetFocus(Child(List)); return 0; }
        if (msg == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;
        if (msg == WM_NCDESTROY) RemoveWindowSubclass(edit, SearchEditProc, 2);
        return DefSubclassProc(edit, msg, w, l);
    }

    // The tag row under a screen point, if it is a real tag.
    std::string TagAt(POINT screen)
    {
        HWND box = Child(TagList);
        POINT client = screen;
        ScreenToClient(box, &client);
        RECT r{};
        GetClientRect(box, &r);
        if (!PtInRect(&r, client)) return {};
        const LRESULT hit = SendMessageA(box, LB_ITEMFROMPOINT, 0, MAKELPARAM(client.x, client.y));
        const int index = LOWORD(hit);
        if (HIWORD(hit) || index < 0 || static_cast<size_t>(index) >= tagRows.size() || tagRows[index].filter.mode != M::TagFilter::Mode::Tag) return {};
        return tagRows[index].filter.name;
    }

    LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
    {
        try
        {
            switch (msg)
            {
            case WM_SIZE:
                Layout(LOWORD(l), HIWORD(l));
                return 0;
            case WM_GETMINMAXINFO:
                reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize = {640, 360};
                return 0;
            case WM_TIMER:
                if (w == kTimer)
                {
                    // Not while a modal dialog has the editor (a map is opening).
                    HWND owner = GetWindow(hwnd, GW_OWNER);
                    if (!owner || IsWindowEnabled(owner)) Refresh(false);
                }
                else if (w == kSearchTimer)
                {
                    KillTimer(hwnd, kSearchTimer);
                    char text[256] = {};
                    GetWindowTextA(Child(SearchEdit), text, sizeof(text));
                    filter.query = text;
                    Rebuild();
                }
                else if (w == kRetryTimer)
                {
                    if (ShowInItsBrowser(retryItem))
                    {
                        KillTimer(hwnd, kRetryTimer);
                        Report("Showing " + Favorites::Split(retryItem.path).name + " in the browser's Favorites.");
                    }
                    else if (--retries <= 0)
                    {
                        KillTimer(hwnd, kRetryTimer);
                        Report("The browser did not show its Favorites; open it once and try again.");
                    }
                }
                return 0;
            case kRefreshMessage:
                Refresh(false);
                return 0;
            case WM_NOTIFY:
            {
                const NMHDR* header = reinterpret_cast<const NMHDR*>(l);
                if (header->idFrom != List) break;
                if (header->code == LVN_ITEMCHANGED) UpdateButtons();
                else if (header->code == NM_DBLCLK)
                {
                    if (const M::Entry* e = Current())
                    {
                        const M::Entry entry = *e;
                        if (entry.kind == Kind::Sound) Play(entry);
                        else Use(entry);
                    }
                }
                else if (header->code == LVN_KEYDOWN)
                {
                    const auto* key = reinterpret_cast<const NMLVKEYDOWN*>(l);
                    if (key->wVKey == VK_DELETE) Command(RemoveButton);
                    else if (key->wVKey == VK_RETURN) Command(UseButton);
                    else if (key->wVKey == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) ListView_SetItemState(Child(List), -1, LVIS_SELECTED, LVIS_SELECTED);
                }
                else if (header->code == LVN_BEGINDRAG && !tags.names.empty())
                {
                    dragging = true;
                    SetCapture(hwnd);
                    Report("Drop on a tag on the left to add the selected favourites to it.");
                }
                break;
            }
            case WM_MOUSEMOVE:
                if (dragging)
                {
                    POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                    ClientToScreen(hwnd, &p);
                    SetCursor(LoadCursor(nullptr, TagAt(p).empty() ? IDC_NO : IDC_ARROW));
                    return 0;
                }
                break;
            case WM_LBUTTONUP:
                if (dragging)
                {
                    POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                    ClientToScreen(hwnd, &p);
                    dragging = false;
                    ReleaseCapture();
                    const std::string tag = TagAt(p);
                    if (!tag.empty()) AddTo(SelectedItems(), tag);
                    else Report("");
                    return 0;
                }
                break;
            case WM_CAPTURECHANGED:
                dragging = false;
                break;
            case WM_CONTEXTMENU:
            {
                POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                if (reinterpret_cast<HWND>(w) == Child(List))
                {
                    if (l == -1)
                    {
                        RECT r{};
                        const M::Entry* e = Current();
                        if (!e) return 0;
                        const int focused = ListView_GetNextItem(Child(List), -1, LVNI_FOCUSED);
                        ListView_GetItemRect(Child(List), std::max(0, focused), &r, LVIR_LABEL);
                        p = {r.left, r.bottom};
                        ClientToScreen(Child(List), &p);
                    }
                    ShowListMenu(p);
                    return 0;
                }
                if (reinterpret_cast<HWND>(w) == Child(TagList) && l != -1) { ShowTagMenu(p); return 0; }
                break;
            }
            case WM_COMMAND:
            {
                const UINT id = LOWORD(w), code = HIWORD(w);
                if (id == SearchEdit)
                {
                    if (code == EN_CHANGE) SetTimer(hwnd, kSearchTimer, 150, nullptr);
                    return 0;
                }
                if (id == TagEdit) return 0;
                if (id == TypeCombo || id == PackageCombo || id == SortCombo)
                {
                    if (code != CBN_SELCHANGE) return 0;
                    const int choice = static_cast<int>(SendMessageA(Child(id), CB_GETCURSEL, 0, 0));
                    if (choice < 0) return 0;
                    if (id == TypeCombo) filter.kind = choice == 0 ? std::nullopt : std::optional<Kind>(M::kKinds[std::min(choice - 1, 2)]);
                    else if (id == PackageCombo)
                        filter.package = choice > 0 && static_cast<size_t>(choice) <= packages.size() ? packages[choice - 1].name : std::string{};
                    else if (static_cast<size_t>(choice) < std::size(Favorites::kSorts)) filter.sort = Favorites::kSorts[choice];
                    SaveView();
                    Rebuild();
                    return 0;
                }
                if (id == TagList)
                {
                    if (code != LBN_SELCHANGE) return 0;
                    const int choice = static_cast<int>(SendMessageA(Child(TagList), LB_GETCURSEL, 0, 0));
                    if (choice < 0 || static_cast<size_t>(choice) >= tagRows.size()) return 0;
                    filter.tag = tagRows[choice].filter;
                    if (filter.tag.mode == M::TagFilter::Mode::Tag) SetWindowTextA(Child(TagEdit), filter.tag.name.c_str());
                    SaveView();
                    Rebuild();
                    return 0;
                }
                Command(id);
                return 0;
            }
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                SavePlacement();
                break;
            case WM_NCDESTROY:
                window = nullptr;
                dragging = false;
                if (images) ImageList_Destroy(images);
                images = nullptr;
                thumbs.clear();
                if (font) DeleteObject(font);
                font = nullptr;
                break;
            }
        }
        catch (const std::exception& error)
        {
            message = error.what();
            if (window) ShowStatus();
        }
        return DefWindowProcA(hwnd, msg, w, l);
    }
}

void Open()
{
    if (window)
    {
        ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
        Refresh(true);
        return;
    }
    ini = IniPath();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSA wc{};
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpfnWndProc = Proc;
    wc.lpszClassName = "ReloadedFavoritesWindow";
    RegisterClassA(&wc);

    NONCLIENTMETRICSA metrics{};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    font = CreateFontIndirectA(&metrics.lfMessageFont);
    LoadView();
    const RECT place = LoadPlacement();
    // A tool window owned by the frame: it stays above the editor and goes with it.
    window = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, wc.lpszClassName, "Favorites", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             place.left, place.top, place.right, place.bottom, Frame(), nullptr, wc.hInstance, nullptr);
    if (!window) throw std::runtime_error("Cannot open the Favorites window.");
    auto add = [](const char* cls, const char* text, int id, DWORD style, DWORD ex = 0) {
        HWND child = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, window,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandle(nullptr), nullptr);
        SendMessageA(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        return child;
    };
    HWND type = add("COMBOBOX", "", TypeCombo, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST);
    SendMessageA(type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("All types"));
    for (Kind k : M::kKinds) SendMessageA(type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(M::KindPlural(k)));
    SendMessageA(type, CB_SETCURSEL, filter.kind ? M::Index(*filter.kind) + 1 : 0, 0);
    add("COMBOBOX", "", PackageCombo, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST);
    HWND sort = add("COMBOBOX", "", SortCombo, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST);
    for (auto s : Favorites::kSorts) SendMessageA(sort, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Favorites::SortLabel(s)));
    SendMessageA(sort, CB_SETCURSEL, static_cast<WPARAM>(filter.sort), 0);
    add("STATIC", "Search:", SearchLabel, SS_LEFT);
    HWND search = add("EDIT", "", SearchEdit, WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
    SendMessageA(search, EM_LIMITTEXT, 200, 0);
    SetWindowSubclass(search, SearchEditProc, 2, 0);
    add("LISTBOX", "", TagList, WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, WS_EX_CLIENTEDGE);
    HWND tagEdit = add("EDIT", "", TagEdit, WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
    SendMessageA(tagEdit, EM_LIMITTEXT, static_cast<WPARAM>(M::kMaxTagName), 0);
    SendMessageW(tagEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Tag name"));
    SendMessageW(search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Words in a name, package or tag"));
    SetWindowSubclass(tagEdit, TagEditProc, 1, 0);
    add("BUTTON", "&New", NewTag, WS_TABSTOP | BS_PUSHBUTTON);
    add("BUTTON", "Rena&me", RenameTag, WS_TABSTOP | BS_PUSHBUTTON);
    add("BUTTON", "&Delete", DeleteTag, WS_TABSTOP | BS_PUSHBUTTON);
    HWND list = add(WC_LISTVIEWA, "", List, WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    const char* columns[] = {"Name", "Type", "Package / group", "Tags"};
    const int widths[] = {200, 80, 170, 140};
    for (int i = 0; i < 4; ++i)
    {
        LVCOLUMNA column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.cx = widths[i];
        column.pszText = const_cast<char*>(columns[i]);
        SendMessageA(list, LVM_INSERTCOLUMNA, i, reinterpret_cast<LPARAM>(&column));
    }
    ResetImages();
    ListView_SetImageList(list, images, LVSIL_SMALL);
    add("BUTTON", "&Show in Browser", ShowButton, WS_TABSTOP | BS_PUSHBUTTON);
    add("BUTTON", "&Use", UseButton, WS_TABSTOP | BS_PUSHBUTTON);
    add("BUTTON", "&Play", PlayButton, WS_TABSTOP | BS_PUSHBUTTON);
    add("BUTTON", "&Add to Tag...", AddToButton, WS_TABSTOP | BS_PUSHBUTTON);
    add("BUTTON", "&Remove", RemoveButton, WS_TABSTOP | BS_PUSHBUTTON);
    add("BUTTON", "&Load Packages", LoadButton, WS_TABSTOP | BS_PUSHBUTTON);
    add("STATIC", "", Status, SS_LEFT | SS_NOPREFIX);
    RECT client{};
    GetClientRect(window, &client);
    Layout(client.right, client.bottom);

    for (auto& map : objects) map.clear();
    resolvedCount = -1;
    attempted.clear();
    entries.clear();
    rows.clear();
    pendingTagItems.clear();
    iniSeen.clear();
    ReadIni(true);
    // As a browser's Favorites view does when it is switched on: load the
    // favourites' packages that are not in memory.
    if (!Loading()) Resolve();
    LoadMissing(true);
    message = "Double-click to use a favourite (or play a sound); right-click or drag to tag.";
    Rebuild();
    SetTimer(window, kTimer, 1000, nullptr);
    ShowWindow(window, SW_SHOW);
    SetFocus(list);
}

bool HandleCommand(UINT command)
{
    if (command != kOpen) return false;
    Open();
    return true;
}

void InstallMenu(HMENU bar)
{
    if (!bar) return;
    for (int i = 0; i < GetMenuItemCount(bar); ++i)
    {
        HMENU sub = GetSubMenu(bar, i);
        // The View menu holds Advanced Options (40065).
        if (!sub || GetMenuState(sub, 40065, MF_BYCOMMAND) == UINT(-1)) continue;
        if (GetMenuState(sub, kOpen, MF_BYCOMMAND) != UINT(-1)) return;
        // After the last "Show ... Browser" entry.
        int after = -1;
        for (int pos = 0; pos < GetMenuItemCount(sub); ++pos)
        {
            char text[128] = {};
            GetMenuStringA(sub, pos, text, sizeof(text), MF_BYPOSITION);
            if (strstr(text, "Browser")) after = pos;
        }
        InsertMenuA(sub, after < 0 ? static_cast<UINT>(-1) : static_cast<UINT>(after + 1), MF_BYPOSITION | MF_STRING, kOpen, "&Favorites...");
        return;
    }
}

void Changed()
{
    if (window) PostMessageA(window, kRefreshMessage, 0, 0);
}
}
