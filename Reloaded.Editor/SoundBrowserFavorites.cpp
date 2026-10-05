#include "pch.h"
#include "SoundBrowserFavorites.h"
#include "SoundFavoritesModel.h"
#include "AssetNameGloss.h"
#include "FavoritesWindow.h"
#include "MemoryWriter.h"
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Engine layout for the supported SCCT editor build.
#define GEDITOR_PTR                 0x1165DFA0u
#define EXEC_LOG_DEV                0x115BEFB0u
#define GFILEMANAGER_PTR            0x115BEFC0u
#define GOBJECTS_DATA_PTR           0x11697B70u
#define GOBJECTS_NUM_PTR            0x11697B74u
#define GNAMES_DATA_PTR             0x1169CFBCu
#define GNAMES_NUM_PTR              0x1169CFC0u
#define GOBJ_BEGIN_LOAD_COUNT_PTR   0x11697B10u // UObject::GObjBeginLoadCount
#define USOUND_CLASS                0x11823128u
#define FSTRING_FROM_TCHAR          0x10E04BE8u // FString::FString(const TCHAR*), thiscall
#define APP_LOAD_FILE_TO_ARRAY      0x10F9E730u // appLoadFileToArray(TArray<BYTE>&, const TCHAR*, FFileManager*)

#define UOBJECT_OUTER_OFFSET        0x18
#define UOBJECT_FNAME_OFFSET        0x20
#define UOBJECT_CLASS_OFFSET        0x24
#define UCLASS_SUPER_OFFSET         0x28
#define FNAME_ENTRY_STR_OFFSET      0x0C

// USound: the fields WBrowserSound::AddListSoundsString and OnPlay read.
#define USOUND_DATA_ARRAY           0x34   // TArray<BYTE> inside the lazy RawData
#define USOUND_DATA_NUM             0x38
#define USOUND_FILENAME             0x48   // FString: Data, then Num at +4
#define USOUND_FLAGS                0x60
#define USOUND_FLAG_UAS_STREAM      0x04

// WBrowserSound and the functions hooked here.
#define SB_PACKAGE_COMBO            0x8C
#define SB_GROUP_COMBO              0x90
#define SB_SOUND_LIST               0x94
#define SB_GROUP_ALL                0x98
#define SB_UNUSED                   0x9C
#define SB_REFRESH_SOUND_LIST       0x10E7D580u
#define SB_GET_CURRENT_PATH_NAME    0x10E7BD30u
#define SB_ON_PLAY                  0x10E7E9F0u
#define SB_ADD_LIST_SOUNDS_STRING   0x10E05372u // thunk; thiscall (const TCHAR* name, USound*)
#define SB_GET_SELECTED_INDEX       0x10E04BA2u // thunk; thiscall, returns the selected row or -1
#define SB_COPY_SHORTCUT_CASE       0x10E7E6F2u // OnCommand: Copy Shortcut (40250)
#define SB_COPY_SHORTCUT_RESUME     0x10E7E6F9u
#define SB_ON_COMMAND_EPILOGUE      0x10E7E698u
#define APP_CLIPBOARD_COPY          0x10FD92C0u // appClipboardCopy(const TCHAR*), cdecl
#define SB_REFRESH_PACKAGES         0x10E051D3u // thunks
#define SB_REFRESH_GROUPS           0x10E053EFu

// Controls on the browser window (reserved block 41030-41044).
#define IDC_SB_SEARCH               41031
#define IDC_SB_ALL_PACKAGES         41032
#define IDC_SB_FAVORITES            41033
#define IDC_SB_PACKAGE_FILTER       41034
#define IDC_SB_SORT                 41035
#define IDC_SB_SEARCH_LABEL         41036
#define WM_SB_ATTACH                (WM_APP + 0x5C)
#define TIMER_SB_REFRESH            0x5AF2
#define TIMER_SB_SEARCH             0x5AF3

static constexpr const char* kFavoritesIniSection = "SoundBrowserFavorites";
// Kept apart: saving the favourites rewrites their whole section.
static constexpr const char* kViewIniSection = "SoundBrowserFavoritesView";
static constexpr size_t kMaxFavorites = 4096;

namespace SF = SoundFavorites;

// Favourites in the order they were added (the ini order), and each one's
// sound while it is loaded.
static std::vector<std::string> g_Favorites;
static std::vector<void*> g_FavoriteObjects;
static size_t g_LastResolvedCount = 0;
// Packages already loaded (or found missing) for unloaded favourites; cleared
// when favourites drop out of memory, as they do when another map is opened.
static std::unordered_set<std::string> g_AttemptedPackages;
static bool g_LoadedPackages = false; // since the native list last showed

// Every loaded sound, rebuilt when the object count changes.
static std::vector<std::string> g_SoundPaths;
static std::vector<void*> g_SoundObjects;
static int g_ScannedObjectCount = -1;

// The list's rows, by item index, while Reloaded rows are shown.
struct ListRow
{
    std::string path;
    void* sound = nullptr; // nullptr: not loaded
};
static std::vector<ListRow> g_Rows;

static std::string g_Query;
static bool g_AllPackages = true;
static bool g_FavoritesActive = false;
static bool g_CustomView = false; // the list currently holds Reloaded rows
static SF::Sort g_Sort = SF::Sort::Package;
static std::string g_FavoritePackage; // empty: every package
static std::string g_SearchPackage;
static bool g_FeatureSupported = false;

static void* g_Browser = nullptr;
static HWND g_BrowserWindow = nullptr;
static HWND g_PackageCombo = nullptr, g_GroupCombo = nullptr, g_GroupAll = nullptr, g_Unused = nullptr, g_List = nullptr;
static HWND g_SearchLabel = nullptr, g_SearchEdit = nullptr, g_AllPackagesCheck = nullptr;
static HWND g_FavoriteButton = nullptr, g_FavoritesToggle = nullptr, g_PackageFilterCombo = nullptr, g_SortCombo = nullptr;
static HWND g_WindowButton = nullptr; // opens the Favorites window
static bool g_ControlsAttached = false;
static int g_ListMargin = -1; // the native gap below the list
static int g_NormalColumnWidth = -1;
static LONG_PTR g_NativeListSortStyle = 0;
static bool g_NativeVisible[4] = {true, true, true, true};

typedef void(__thiscall* BrowserFn)(void*);

static LRESULT CALLBACK SBF_BrowserSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

static std::string SBF_IniPath()
{
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash)
        *(lastSlash + 1) = '\0';
    return std::string(exePath) + "Reloaded_Editor.ini";
}

// ---------------------------------------------------------------------------
// Engine reads. Fault-prone reads stay in leaf functions without C++ objects so
// SEH can protect the browser from stale UObject pointers.

static bool SBF_ReadObjectIdentity(void* object, void** outer, char* name, size_t nameSize)
{
    if (!object || !outer || !name || nameSize == 0)
        return false;
    __try
    {
        void** names = *reinterpret_cast<void***>(GNAMES_DATA_PTR);
        const INT nameCount = *reinterpret_cast<INT*>(GNAMES_NUM_PTR);
        const INT nameIndex = *reinterpret_cast<INT*>(static_cast<char*>(object) + UOBJECT_FNAME_OFFSET);
        if (!names || nameIndex < 0 || nameIndex >= nameCount)
            return false;
        void* entry = names[nameIndex];
        if (!entry)
            return false;
        const char* source = reinterpret_cast<const char*>(entry) + FNAME_ENTRY_STR_OFFSET;
        if (!source[0])
            return false;
        strncpy_s(name, nameSize, source, _TRUNCATE);
        *outer = *reinterpret_cast<void**>(static_cast<char*>(object) + UOBJECT_OUTER_OFFSET);
        return name[0] != '\0';
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        name[0] = '\0';
        *outer = nullptr;
        return false;
    }
}

static bool SBF_IsSound(void* object)
{
    if (!object)
        return false;
    __try
    {
        void* objectClass = *reinterpret_cast<void**>(static_cast<char*>(object) + UOBJECT_CLASS_OFFSET);
        for (int depth = 0; objectClass && depth < 64; ++depth)
        {
            if (objectClass == reinterpret_cast<void*>(USOUND_CLASS))
                return true;
            objectClass = *reinterpret_cast<void**>(static_cast<char*>(objectClass) + UCLASS_SUPER_OFFSET);
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool SBF_BuildObjectPath(void* object, std::string& path)
{
    struct NamePart
    {
        char Name[256];
    };
    NamePart parts[32] = {};
    int partCount = 0;
    void* cursor = object;
    while (cursor && partCount < static_cast<int>(std::size(parts)))
    {
        void* outer = nullptr;
        if (!SBF_ReadObjectIdentity(cursor, &outer, parts[partCount].Name, std::size(parts[partCount].Name)))
            return false;
        ++partCount;
        if (outer == cursor)
            return false;
        cursor = outer;
    }
    if (partCount == 0 || cursor)
        return false;
    path.clear();
    for (int i = partCount - 1; i >= 0; --i)
    {
        if (!path.empty())
            path.push_back('.');
        path += parts[i].Name;
    }
    return !path.empty();
}

static bool SBF_ReadGObjects(void*** data, INT* count)
{
    __try
    {
        *data = *reinterpret_cast<void***>(GOBJECTS_DATA_PTR);
        *count = *reinterpret_cast<INT*>(GOBJECTS_NUM_PTR);
        return *data != nullptr && *count >= 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *data = nullptr;
        *count = 0;
        return false;
    }
}

static bool SBF_ReadGObjectAt(void** data, INT index, void** object)
{
    __try
    {
        *object = data[index];
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *object = nullptr;
        return false;
    }
}

static int SBF_ObjectCount()
{
    void** objects = nullptr;
    INT count = 0;
    return SBF_ReadGObjects(&objects, &count) ? count : -1;
}

// True while a package or map is being loaded. Its progress updates pump
// messages, so the browser's timer can run in the middle of a load; nothing
// here scans the objects or loads a package then.
static bool SBF_Loading()
{
    __try
    {
        return *reinterpret_cast<const INT*>(GOBJ_BEGIN_LOAD_COUNT_PTR) > 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return true;
    }
}

typedef int(__thiscall* AddListSoundFn)(void*, const char*, void*);

// The native row for a sound (icon, flags, volume and the other columns, with
// the sound as its item data), inserted at the top of the list.
static bool SBF_AddNativeRow(void* browser, const char* name, void* sound)
{
    __try
    {
        reinterpret_cast<AddListSoundFn>(SB_ADD_LIST_SOUNDS_STRING)(browser, name, sound);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The sound behind a row, if it is still the object the row was built with: a
// map change can free it and reuse its memory before the next refresh.
static void* SBF_LiveSound(const ListRow& row)
{
    std::string path;
    if (!row.sound || !SBF_IsSound(row.sound) || !SBF_BuildObjectPath(row.sound, path) || !SF::SameName(path, row.path))
        return nullptr;
    return row.sound;
}

static bool __cdecl SBF_ExecEditorCommand(const char* command)
{
    if (!command || !command[0])
        return false;
    __try
    {
        void* editor = *reinterpret_cast<void**>(GEDITOR_PTR);
        if (!editor)
            return false;
        void* exec = static_cast<char*>(editor) + 0x28;
        void* vtable = *reinterpret_cast<void**>(exec);
        void* function = vtable ? *reinterpret_cast<void**>(vtable) : nullptr;
        void* log = *reinterpret_cast<void**>(EXEC_LOG_DEV);
        if (!function || !log)
            return false;
        __asm
        {
            push log
            push command
            mov  ecx, exec
            mov  eax, function
            call eax
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// ---------------------------------------------------------------------------
// Loaded sounds and favourites.

static void SBF_ScanSounds(bool force)
{
    void** objects = nullptr;
    INT objectCount = 0;
    if (!SBF_ReadGObjects(&objects, &objectCount))
        return;
    if (!force && objectCount == g_ScannedObjectCount)
        return;
    g_ScannedObjectCount = objectCount;
    g_SoundPaths.clear();
    g_SoundObjects.clear();
    for (INT i = 0; i < objectCount; ++i)
    {
        void* object = nullptr;
        if (!SBF_ReadGObjectAt(objects, i, &object) || !SBF_IsSound(object))
            continue;
        std::string path;
        if (!SBF_BuildObjectPath(object, path))
            continue;
        g_SoundPaths.push_back(path);
        g_SoundObjects.push_back(object);
    }
}

static void SBF_ResolveFavorites()
{
    SBF_ScanSounds(false);
    std::unordered_map<std::string, void*> loaded;
    loaded.reserve(g_SoundPaths.size());
    for (size_t i = 0; i < g_SoundPaths.size(); ++i)
        loaded.emplace(SF::Lower(g_SoundPaths[i]), g_SoundObjects[i]);
    g_FavoriteObjects.assign(g_Favorites.size(), nullptr);
    size_t resolved = 0;
    for (size_t i = 0; i < g_Favorites.size(); ++i)
    {
        const auto found = loaded.find(SF::Lower(g_Favorites[i]));
        if (found != loaded.end())
        {
            g_FavoriteObjects[i] = found->second;
            ++resolved;
        }
    }
    // Opening another map unloads the sound packages it does not use; let
    // their favourites' packages be loaded again.
    if (resolved < g_LastResolvedCount)
        g_AttemptedPackages.clear();
    g_LastResolvedCount = resolved;
}

static void SBF_LoadFavorites()
{
    const std::string ini = SBF_IniPath();
    int count = GetPrivateProfileIntA(kFavoritesIniSection, "Count", 0, ini.c_str());
    count = (std::max)(0, (std::min)(count, static_cast<int>(kMaxFavorites)));
    std::vector<std::string> paths;
    for (int i = 0; i < count; ++i)
    {
        char key[32] = {};
        char value[1024] = {};
        snprintf(key, sizeof(key), "Favorite%d", i);
        GetPrivateProfileStringA(kFavoritesIniSection, key, "", value, static_cast<DWORD>(std::size(value)), ini.c_str());
        paths.push_back(value);
    }
    g_Favorites = SF::Dedupe(paths);
    g_FavoriteObjects.assign(g_Favorites.size(), nullptr);
}

// Writes the favourites key by key; nothing else in Reloaded_Editor.ini is touched.
static void SBF_SaveFavorites()
{
    const std::string ini = SBF_IniPath();
    WritePrivateProfileStringA(kFavoritesIniSection, nullptr, nullptr, ini.c_str());
    WritePrivateProfileStringA(kFavoritesIniSection, "Count", std::to_string(g_Favorites.size()).c_str(), ini.c_str());
    for (size_t i = 0; i < g_Favorites.size(); ++i)
        WritePrivateProfileStringA(kFavoritesIniSection, ("Favorite" + std::to_string(i)).c_str(), g_Favorites[i].c_str(), ini.c_str());
}

static void SBF_LoadView()
{
    const std::string ini = SBF_IniPath();
    char value[256] = {};
    GetPrivateProfileStringA(kViewIniSection, "Sort", "", value, static_cast<DWORD>(std::size(value)), ini.c_str());
    g_Sort = SF::SortFromKey(value);
    GetPrivateProfileStringA(kViewIniSection, "Package", "", value, static_cast<DWORD>(std::size(value)), ini.c_str());
    g_FavoritePackage = value;
    g_AllPackages = GetPrivateProfileIntA(kViewIniSection, "SearchAllPackages", 1, ini.c_str()) != 0;
}

static void SBF_SaveView()
{
    const std::string ini = SBF_IniPath();
    WritePrivateProfileStringA(kViewIniSection, "Sort", SF::SortKey(g_Sort), ini.c_str());
    WritePrivateProfileStringA(kViewIniSection, "Package", g_FavoritePackage.c_str(), ini.c_str());
    WritePrivateProfileStringA(kViewIniSection, "SearchAllPackages", g_AllPackages ? "1" : "0", ini.c_str());
}

static std::string SBF_SoundDirectory()
{
    char executable[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, executable, static_cast<DWORD>(std::size(executable))))
        return {};
    char* slash = strrchr(executable, '\\');
    if (!slash)
        return {};
    *(slash + 1) = '\0';
    char fullPath[MAX_PATH] = {};
    const std::string relative = std::string(executable) + "..\\Packages\\Sounds\\";
    const DWORD length = GetFullPathNameA(relative.c_str(), static_cast<DWORD>(std::size(fullPath)), fullPath, nullptr);
    return length && length < std::size(fullPath) ? std::string(fullPath) : std::string{};
}

// Loads the packages of favourites that are not in memory. Each package is
// tried once until favourites are unloaded again, so one that is missing, or
// a map's own package, is not retried every second.
static void SBF_LoadMissingFavoritePackages()
{
    if (SBF_Loading())
        return;
    std::unordered_map<std::string, std::string> wanted;
    for (size_t i = 0; i < g_Favorites.size(); ++i)
    {
        if (i < g_FavoriteObjects.size() && g_FavoriteObjects[i])
            continue;
        const std::string package = SF::Split(g_Favorites[i]).package;
        if (!package.empty() && g_AttemptedPackages.insert(SF::Lower(package)).second)
            wanted.emplace(SF::Lower(package), package);
    }
    if (wanted.empty())
        return;

    const std::string directory = SBF_SoundDirectory();
    if (directory.empty())
        return;
    int loaded = 0, unavailable = 0;
    for (const auto& package : wanted)
    {
        const std::string file = directory + package.second + ".uax";
        if (GetFileAttributesA(file.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            ++unavailable;
            continue;
        }
        const std::string command = "OBJ LOAD FILE=\"" + file + "\"";
        if (SBF_ExecEditorCommand(command.c_str()))
            ++loaded;
        else
            ++unavailable;
    }
    if (loaded > 0)
    {
        g_LoadedPackages = true;
        Logger::log("SoundBrowserFavorites: loaded " + std::to_string(loaded) + " favorite package(s)");
    }
    if (unavailable > 0)
        Logger::log("SoundBrowserFavorites: could not locate or load " + std::to_string(unavailable) + " favorite package(s)");
    SBF_ResolveFavorites();
}

// ---------------------------------------------------------------------------
// The list.

static bool SBF_WantCustom()
{
    return g_FavoritesActive || (g_AllPackages && !SF::IsBlank(g_Query));
}

static int SBF_SelectedItem()
{
    return g_List ? static_cast<int>(SendMessageA(g_List, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED)) : -1;
}

static void* SBF_ItemParam(int item)
{
    LVITEMA lvi = {};
    lvi.mask = LVIF_PARAM;
    lvi.iItem = item;
    if (!g_List || item < 0 || !SendMessageA(g_List, LVM_GETITEMA, 0, reinterpret_cast<LPARAM>(&lvi)))
        return nullptr;
    return reinterpret_cast<void*>(lvi.lParam);
}

// The selected sound's path: a Reloaded row names it; a native row's sound
// is its item data.
static bool SBF_SelectedPath(std::string& path, bool* loaded = nullptr)
{
    const int item = SBF_SelectedItem();
    if (item < 0)
        return false;
    if (g_CustomView)
    {
        if (static_cast<size_t>(item) >= g_Rows.size())
            return false;
        path = g_Rows[item].path;
        if (loaded)
            *loaded = SBF_LiveSound(g_Rows[item]) != nullptr;
        return true;
    }
    void* sound = SBF_ItemParam(item);
    if (!SBF_IsSound(sound) || !SBF_BuildObjectPath(sound, path))
        return false;
    if (loaded)
        *loaded = true;
    return true;
}

static void SBF_UpdateFavoriteButton()
{
    if (!g_FavoriteButton)
        return;
    std::string path;
    const bool has = SBF_SelectedPath(path);
    const bool favorite = has && SF::Contains(g_Favorites, path);
    SetWindowTextA(g_FavoriteButton, favorite ? "Remove Favorite" : "Add Favorite");
    EnableWindow(g_FavoriteButton, has);
}

static std::vector<SF::Favorite> SBF_FavoriteStates()
{
    std::vector<SF::Favorite> favorites;
    for (size_t i = 0; i < g_Favorites.size(); ++i)
        favorites.push_back({g_Favorites[i], i < g_FavoriteObjects.size() && g_FavoriteObjects[i]});
    return favorites;
}

// The package filter's entries: every package, then each one with its count.
// A filter naming a package that has dropped out falls back to every package.
static void SBF_FillPackageFilter(const std::vector<SF::Favorite>& entries, std::string& filter)
{
    const auto packages = SF::Packages(entries);
    if (!filter.empty() && std::none_of(packages.begin(), packages.end(), [&](const SF::Package& p) { return SF::SameName(p.name, filter); }))
        filter.clear();
    if (!g_PackageFilterCombo)
        return;
    SendMessageA(g_PackageFilterCombo, WM_SETREDRAW, FALSE, 0);
    SendMessageA(g_PackageFilterCombo, CB_RESETCONTENT, 0, 0);
    SendMessageA(g_PackageFilterCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(SF::AllLabel(entries.size()).c_str()));
    int selected = 0;
    for (size_t i = 0; i < packages.size(); ++i)
    {
        SendMessageA(g_PackageFilterCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(SF::PackageLabel(packages[i]).c_str()));
        if (SF::SameName(packages[i].name, filter))
            selected = static_cast<int>(i) + 1;
    }
    SendMessageA(g_PackageFilterCombo, CB_SETCURSEL, selected, 0);
    SendMessageA(g_PackageFilterCombo, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_PackageFilterCombo, nullptr, TRUE);
}

static void SBF_PopulateCustom()
{
    std::string selectedPath;
    SBF_SelectedPath(selectedPath);

    // Mid-load the objects are half made and the rows' sounds may be gone:
    // show nothing until the timer sees the load finish.
    if (SBF_Loading())
    {
        g_Rows.clear();
        g_ScannedObjectCount = -1;
        SendMessageA(g_List, LVM_DELETEALLITEMS, 0, 0);
        return;
    }
    SBF_ScanSounds(false);
    std::vector<SF::Row> rows;
    g_Rows.clear();
    if (g_FavoritesActive)
    {
        SBF_ResolveFavorites();
        const auto favorites = SBF_FavoriteStates();
        SBF_FillPackageFilter(favorites, g_FavoritePackage);
        rows = SF::FavoriteRows(favorites, g_FavoritePackage, g_Sort, g_Query);
        for (const auto& row : rows)
            g_Rows.push_back({g_Favorites[row.item], g_FavoriteObjects[row.item]});
    }
    else
    {
        SBF_FillPackageFilter(SF::Matching(g_SoundPaths, g_Query), g_SearchPackage);
        rows = SF::SearchRows(g_SoundPaths, g_SearchPackage, g_Query, g_Sort);
        for (const auto& row : rows)
            g_Rows.push_back({g_SoundPaths[row.item], g_SoundObjects[row.item]});
    }

    SendMessageA(g_List, WM_SETREDRAW, FALSE, 0);
    SendMessageA(g_List, LVM_DELETEALLITEMS, 0, 0);
    // The native code inserts each row at the top, so the rows go in last
    // first. A loaded sound gets the native row, which keeps the sound as its
    // item data; one that is not loaded gets its label alone. Either way the
    // row's text becomes the label, and g_Rows follows the item order exactly.
    int selected = -1;
    for (size_t i = rows.size(); i-- > 0;)
    {
        const int before = static_cast<int>(SendMessageA(g_List, LVM_GETITEMCOUNT, 0, 0));
        if (g_Rows[i].sound)
            SBF_AddNativeRow(g_Browser, SF::Split(g_Rows[i].path).name.c_str(), g_Rows[i].sound);
        if (static_cast<int>(SendMessageA(g_List, LVM_GETITEMCOUNT, 0, 0)) == before)
        {
            g_Rows[i].sound = nullptr;
            LVITEMA item = {};
            item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
            item.iItem = 0;
            item.pszText = const_cast<char*>(rows[i].label.c_str());
            item.iImage = I_IMAGENONE;
            item.lParam = 0;
            SendMessageA(g_List, LVM_INSERTITEMA, 0, reinterpret_cast<LPARAM>(&item));
        }
        else
        {
            LVITEMA item = {};
            item.iSubItem = 0;
            item.pszText = const_cast<char*>(rows[i].label.c_str());
            SendMessageA(g_List, LVM_SETITEMTEXTA, 0, reinterpret_cast<LPARAM>(&item));
        }
        if (!selectedPath.empty() && SF::SameName(g_Rows[i].path, selectedPath))
            selected = static_cast<int>(i);
    }
    if (!rows.empty())
        ListView_SetColumnWidth(g_List, 0, LVSCW_AUTOSIZE);
    if (selected >= 0)
    {
        ListView_SetItemState(g_List, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g_List, selected, FALSE);
    }
    SendMessageA(g_List, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(g_List, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

// With the search kept to the browser's own package, the native rows that do
// not match are dropped; the rest stay native, so every stock command works.
static void SBF_FilterNativeRows()
{
    const int count = static_cast<int>(SendMessageA(g_List, LVM_GETITEMCOUNT, 0, 0));
    SendMessageA(g_List, WM_SETREDRAW, FALSE, 0);
    for (int i = count - 1; i >= 0; --i)
    {
        std::string path;
        void* sound = SBF_ItemParam(i);
        if (!SBF_IsSound(sound) || !SBF_BuildObjectPath(sound, path))
        {
            char text[256] = {};
            LVITEMA item = {};
            item.pszText = text;
            item.cchTextMax = static_cast<int>(std::size(text));
            SendMessageA(g_List, LVM_GETITEMTEXTA, i, reinterpret_cast<LPARAM>(&item));
            path = text;
        }
        if (!SF::Matches(path, g_Query))
            SendMessageA(g_List, LVM_DELETEITEM, i, 0);
    }
    SendMessageA(g_List, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(g_List, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

// ---------------------------------------------------------------------------
// Layout.

static RECT SBF_ChildRect(HWND child)
{
    RECT rect = {};
    if (child && GetWindowRect(child, &rect))
        MapWindowPoints(nullptr, g_BrowserWindow, reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}

static int SBF_TextWidth(const char* text)
{
    HDC dc = GetDC(g_BrowserWindow);
    HFONT font = reinterpret_cast<HFONT>(SendMessageA(g_PackageCombo, WM_GETFONT, 0, 0));
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    SIZE size = {};
    GetTextExtentPoint32A(dc, text, static_cast<int>(strlen(text)), &size);
    if (old)
        SelectObject(dc, old);
    ReleaseDC(g_BrowserWindow, dc);
    return size.cx;
}

// The search row sits under the list, which gives up one row of its height.
// nativeLayout: the native code has just placed the list, so its gap to the
// window's bottom edge is the one to keep.
static void SBF_Layout(bool nativeLayout)
{
    if (!g_ControlsAttached || !g_BrowserWindow || !g_List)
        return;
    RECT client = {};
    GetClientRect(g_BrowserWindow, &client);
    const RECT list = SBF_ChildRect(g_List);
    const RECT package = SBF_ChildRect(g_PackageCombo);
    const RECT group = SBF_ChildRect(g_GroupCombo);
    const RECT all = SBF_ChildRect(g_GroupAll);
    const int rowHeight = (std::max)(18L, package.bottom - package.top);
    // A list already shortened by this layout is not a native gap.
    const int margin = static_cast<int>(client.bottom - list.bottom);
    if (g_ListMargin < 0 || (nativeLayout && margin >= 0 && margin < rowHeight))
        g_ListMargin = (std::max)(0, (std::min)(margin, rowHeight));

    constexpr int gap = 3;
    const int rowTop = client.bottom - g_ListMargin - rowHeight;
    const int left = list.left;
    const int right = (std::max)(list.right, group.right);
    SetWindowPos(g_List, nullptr, list.left, list.top, list.right - list.left,
                 (std::max)(20L, static_cast<LONG>(rowTop - gap) - list.top), SWP_NOZORDER | SWP_NOACTIVATE);

    const int labelWidth = SBF_TextWidth("Search:") + 6;
    const int allWidth = SBF_TextWidth("All packages") + 24;
    const int buttonWidth = (std::max)(SBF_TextWidth("Remove Favorite"), SBF_TextWidth("Add Favorite")) + 16;
    const int toggleWidth = SBF_TextWidth("Favorites") + 20;
    int x = right - toggleWidth;
    SetWindowPos(g_FavoritesToggle, nullptr, x, rowTop, toggleWidth, rowHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    x -= gap + buttonWidth;
    SetWindowPos(g_FavoriteButton, nullptr, x, rowTop, buttonWidth, rowHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    x -= gap + allWidth;
    SetWindowPos(g_AllPackagesCheck, nullptr, x, rowTop, allWidth, rowHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_SearchLabel, nullptr, left, rowTop + 3, labelWidth, rowHeight - 3, SWP_NOZORDER | SWP_NOACTIVATE);
    const int editLeft = left + labelWidth;
    SetWindowPos(g_SearchEdit, nullptr, editLeft, rowTop, (std::max)(40, x - gap - editLeft), rowHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Reloaded rows replace the package, group, All and Unused controls with a
    // package filter and a sort order; the heights include the drop-down list.
    constexpr int dropHeight = 320;
    SetWindowPos(g_PackageFilterCombo, HWND_TOP, package.left, package.top, package.right - package.left, dropHeight, SWP_NOACTIVATE);
    const LONG sortLeft = (std::min)(all.left, group.left);
    // With Favorites shown, the Favorites window's button takes the row's right end.
    const bool windowButton = g_WindowButton && g_CustomView && g_FavoritesActive;
    const int windowButtonWidth = SBF_TextWidth("All Favorites...") + 16;
    const LONG sortRight = windowButton ? (std::max)(sortLeft + 40, group.right - windowButtonWidth - gap) : group.right;
    SetWindowPos(g_SortCombo, HWND_TOP, sortLeft, group.top, sortRight - sortLeft, dropHeight, SWP_NOACTIVATE);
    if (g_WindowButton)
        SetWindowPos(g_WindowButton, HWND_TOP, sortRight + gap, group.top, (std::max)(1L, group.right - sortRight - gap),
                     package.bottom - package.top, SWP_NOACTIVATE | (windowButton ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

static void SBF_ShowNativeFilters(bool show)
{
    HWND natives[4] = {g_PackageCombo, g_GroupCombo, g_GroupAll, g_Unused};
    if (show)
    {
        ShowWindow(g_PackageFilterCombo, SW_HIDE);
        ShowWindow(g_SortCombo, SW_HIDE);
        if (g_WindowButton)
            ShowWindow(g_WindowButton, SW_HIDE);
        for (int i = 0; i < 4; ++i)
            if (natives[i])
                ShowWindow(natives[i], g_NativeVisible[i] ? SW_SHOWNA : SW_HIDE);
    }
    else
    {
        for (int i = 0; i < 4; ++i)
        {
            if (!natives[i])
                continue;
            g_NativeVisible[i] = IsWindowVisible(natives[i]) != FALSE;
            ShowWindow(natives[i], SW_HIDE);
        }
        SBF_Layout(false);
        ShowWindow(g_PackageFilterCombo, SW_SHOWNA);
        ShowWindow(g_SortCombo, SW_SHOWNA);
    }
}

// A self-sorting list would reorder the rows away from g_Rows.
static void SBF_SuspendListSorting(bool suspend)
{
    const LONG_PTR style = GetWindowLongPtrA(g_List, GWL_STYLE);
    if (suspend)
    {
        g_NativeListSortStyle = style & (LVS_SORTASCENDING | LVS_SORTDESCENDING);
        if (g_NativeListSortStyle)
            SetWindowLongPtrA(g_List, GWL_STYLE, style & ~(LVS_SORTASCENDING | LVS_SORTDESCENDING));
    }
    else if (g_NativeListSortStyle)
    {
        SetWindowLongPtrA(g_List, GWL_STYLE, style | g_NativeListSortStyle);
        g_NativeListSortStyle = 0;
    }
}

// Re-reads the native package list, keeping the chosen package and group, so
// packages loaded for Favorites show up there too.
static void SBF_ResyncNativePackages()
{
    char package[256] = {}, group[256] = {};
    GetWindowTextA(g_PackageCombo, package, static_cast<int>(std::size(package)));
    GetWindowTextA(g_GroupCombo, group, static_cast<int>(std::size(group)));
    reinterpret_cast<BrowserFn>(SB_REFRESH_PACKAGES)(g_Browser);
    LRESULT index = SendMessageA(g_PackageCombo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(package));
    if (index != CB_ERR)
        SendMessageA(g_PackageCombo, CB_SETCURSEL, index, 0);
    reinterpret_cast<BrowserFn>(SB_REFRESH_GROUPS)(g_Browser);
    index = SendMessageA(g_GroupCombo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(group));
    if (index != CB_ERR && group[0])
        SendMessageA(g_GroupCombo, CB_SETCURSEL, index, 0);
}

static void SBF_SwitchView(bool custom)
{
    if (custom == g_CustomView)
        return;
    g_CustomView = custom;
    if (custom)
    {
        g_NormalColumnWidth = ListView_GetColumnWidth(g_List, 0);
        SBF_ShowNativeFilters(false);
        SBF_SuspendListSorting(true);
    }
    else
    {
        g_Rows.clear();
        SendMessageA(g_List, LVM_DELETEALLITEMS, 0, 0);
        SBF_SuspendListSorting(false);
        SBF_ShowNativeFilters(true);
        if (g_NormalColumnWidth > 0)
            ListView_SetColumnWidth(g_List, 0, g_NormalColumnWidth);
        if (g_LoadedPackages)
        {
            g_LoadedPackages = false;
            SBF_ResyncNativePackages();
        }
    }
}

static void SBF_NativeRefreshTrampoline();

// Every list refresh, the browser's own included, comes through here.
static void __cdecl SBF_RefreshDispatch(void* browser)
{
    HWND root = nullptr;
    __try
    {
        root = *reinterpret_cast<HWND*>(static_cast<char*>(browser) + sizeof(void*));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        root = nullptr;
    }
    if (root && IsWindow(root) && root != g_BrowserWindow && SetWindowSubclass(root, SBF_BrowserSubclassProc, 21, 0))
    {
        g_Browser = browser;
        g_BrowserWindow = root;
        g_ControlsAttached = false;
        PostMessageA(root, WM_SB_ATTACH, 0, 0);
    }

    const bool ours = browser == g_Browser && g_ControlsAttached;
    if (ours)
        SBF_SwitchView(SBF_WantCustom());
    if (ours && g_CustomView)
        SBF_PopulateCustom();
    else
    {
        reinterpret_cast<BrowserFn>(&SBF_NativeRefreshTrampoline)(browser);
        if (ours && !SF::IsBlank(g_Query))
            SBF_FilterNativeRows();
    }
    if (ours)
        SBF_UpdateFavoriteButton();
}

static void SBF_Refresh()
{
    if (g_Browser)
        reinterpret_cast<BrowserFn>(SB_REFRESH_SOUND_LIST)(g_Browser);
}

// ---------------------------------------------------------------------------
// Hooks on WBrowserSound.

__declspec(naked) static void SBF_NativeRefreshTrampoline()
{
    static int resume = static_cast<int>(SB_REFRESH_SOUND_LIST + 5);
    __asm
    {
        push ebp
        mov  ebp, esp
        push 0FFFFFFFFh
        jmp  dword ptr [resume]
    }
}

// RefreshSoundList (thiscall, no arguments).
__declspec(naked) static void SBF_RefreshListHook()
{
    __asm
    {
        push ecx
        call SBF_RefreshDispatch
        add  esp, 4
        ret
    }
}

// GetCurrentPathName, which the property windows' Use button and the stock
// commands read: a Reloaded row names its own sound.
static int __cdecl SBF_CustomCurrentPath(void* browser, void* result)
{
    if (browser != g_Browser || !g_CustomView)
        return 0;
    const int item = SBF_SelectedItem();
    if (item < 0 || static_cast<size_t>(item) >= g_Rows.size())
        return 0;
    const char* path = g_Rows[item].path.c_str();
    void* construct = reinterpret_cast<void*>(FSTRING_FROM_TCHAR);
    __asm
    {
        push path
        mov  ecx, result
        mov  eax, construct
        call eax
    }
    return 1;
}

__declspec(naked) static void SBF_GetCurrentPathNameHook()
{
    static int resume = static_cast<int>(SB_GET_CURRENT_PATH_NAME + 5);
    __asm
    {
        push ecx
        push dword ptr [esp + 8]   // FString* result
        push ecx                   // this
        call SBF_CustomCurrentPath
        add  esp, 8
        pop  ecx
        test eax, eax
        jz   native_path
        mov  eax, dword ptr [esp + 4]
        ret  4
    native_path:
        push ebp
        mov  ebp, esp
        push 0FFFFFFFFh
        jmp  dword ptr [resume]
    }
}

static bool SBF_LoadStreamData(void* sound, const char* file)
{
    __try
    {
        void* array = static_cast<char*>(sound) + USOUND_DATA_ARRAY;
        void* fileManager = *reinterpret_cast<void**>(GFILEMANAGER_PTR);
        void* load = reinterpret_cast<void*>(APP_LOAD_FILE_TO_ARRAY);
        __asm
        {
            push fileManager
            push file
            push array
            mov  eax, load
            call eax
            add  esp, 12
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool SBF_StreamFileName(void* sound, char* name, size_t size)
{
    __try
    {
        const char* base = static_cast<const char*>(sound);
        if (*reinterpret_cast<const int*>(base + USOUND_DATA_NUM) != 0 ||
            (*reinterpret_cast<const unsigned*>(base + USOUND_FLAGS) & USOUND_FLAG_UAS_STREAM))
            return false;
        const char* data = *reinterpret_cast<const char* const*>(base + USOUND_FILENAME);
        const int count = *reinterpret_cast<const int*>(base + USOUND_FILENAME + 4);
        strncpy_s(name, size, count && data ? data : "", _TRUNCATE);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// OnPlay for a Reloaded row: the native code looks the row's text up in the
// chosen package. As it does, a sound with no data loaded reads its wave from
// ..\packages\sounds\ first.
static int __cdecl SBF_CustomPlay(void* browser)
{
    if (browser != g_Browser || !g_CustomView)
        return 0;
    const int item = SBF_SelectedItem();
    if (item < 0 || static_cast<size_t>(item) >= g_Rows.size())
        return 1;
    void* sound = SBF_LiveSound(g_Rows[item]);
    if (!sound)
        return 1;
    char fileName[MAX_PATH] = {};
    if (SBF_StreamFileName(sound, fileName, std::size(fileName)))
    {
        const std::string file = std::string("..\\packages\\sounds\\") + fileName + ".wav";
        SBF_LoadStreamData(sound, file.c_str());
    }
    const std::string command = "AUDIO PLAY NAME=\"" + g_Rows[item].path + "\"";
    SBF_ExecEditorCommand(command.c_str());
    return 1;
}

__declspec(naked) static void SBF_OnPlayHook()
{
    static int resume = static_cast<int>(SB_ON_PLAY + 5);
    __asm
    {
        push ecx
        push ecx
        call SBF_CustomPlay
        add  esp, 4
        pop  ecx
        test eax, eax
        jz   native_play
        ret
    native_play:
        push ebp
        mov  ebp, esp
        push 0FFFFFFFFh
        jmp  dword ptr [resume]
    }
}

static void SBF_ClipboardCopy(const char* text)
{
    __try
    {
        reinterpret_cast<void(__cdecl*)(const char*)>(APP_CLIPBOARD_COPY)(text);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

// Copy Shortcut builds Sound'Package.Group.Name' from the package and group
// boxes and the row's text; a Reloaded row names its sound in full.
static int __cdecl SBF_CustomCopyShortcut(void* browser)
{
    if (browser != g_Browser || !g_CustomView)
        return 0;
    const int item = SBF_SelectedItem();
    if (item < 0 || static_cast<size_t>(item) >= g_Rows.size())
        return 1;
    SBF_ClipboardCopy(("Sound'" + g_Rows[item].path + "'").c_str());
    return 1;
}

__declspec(naked) static void SBF_CopyShortcutHook()
{
    static int getSelected = static_cast<int>(SB_GET_SELECTED_INDEX);
    static int resume = static_cast<int>(SB_COPY_SHORTCUT_RESUME);
    static int epilogue = static_cast<int>(SB_ON_COMMAND_EPILOGUE);
    __asm
    {
        push esi                   // this
        call SBF_CustomCopyShortcut
        add  esp, 4
        test eax, eax
        jz   native_copy
        jmp  dword ptr [epilogue]
    native_copy:
        mov  ecx, esi              // displaced: MOV ECX,ESI / CALL GetSelectedIndex
        call dword ptr [getSelected]
        jmp  dword ptr [resume]
    }
}

// ---------------------------------------------------------------------------
// Commands.

static void SBF_SetFavoritesActive(bool active)
{
    if (active == g_FavoritesActive)
        return;
    if (active)
    {
        // Pick up favourites another open editor saved, and retry every
        // package: switching Favorites on is the way to ask again.
        SBF_LoadFavorites();
        g_AttemptedPackages.clear();
        SBF_ResolveFavorites();
        SBF_LoadMissingFavoritePackages();
    }
    g_FavoritesActive = active;
    if (g_FavoritesToggle)
        SendMessageA(g_FavoritesToggle, BM_SETCHECK, active ? BST_CHECKED : BST_UNCHECKED, 0);
    if (g_AllPackagesCheck)
        EnableWindow(g_AllPackagesCheck, !active);
    SBF_Refresh();
    SBF_Layout(false);
}

static void SBF_ReadQuery()
{
    char text[256] = {};
    if (g_SearchEdit)
        GetWindowTextA(g_SearchEdit, text, static_cast<int>(std::size(text)));
    g_Query = text;
}

void SoundBrowserFavorites::ToggleSelected()
{
    std::string path;
    if (!SBF_SelectedPath(path))
        return;
    // Start from the saved list, so favourites added or removed in another
    // open editor are kept; the change is the one this editor showed.
    const bool wasFavorite = SF::Contains(g_Favorites, path);
    SBF_LoadFavorites();
    if (wasFavorite)
        g_Favorites = SF::Remove(g_Favorites, path);
    else if (g_Favorites.size() < kMaxFavorites)
        g_Favorites = SF::Add(g_Favorites, path, kMaxFavorites);
    else
    {
        MessageBoxA(g_BrowserWindow, "The Favorites list has reached its 4096-item limit.", "Sound Browser", MB_OK | MB_ICONWARNING);
        return;
    }
    SBF_SaveFavorites();
    SBF_ResolveFavorites();
    if (g_FavoritesActive)
        SBF_Refresh();
    else
        SBF_UpdateFavoriteButton();
    FavoritesWindow::Changed();
}

void SoundBrowserFavorites::FavoritesChanged()
{
    SBF_LoadFavorites();
    SBF_ResolveFavorites();
    if (g_FavoritesActive && g_ControlsAttached)
        SBF_Refresh();
    else
        SBF_UpdateFavoriteButton();
}

bool SoundBrowserFavorites::ShowFavorite(const std::string& path)
{
    if (!g_FeatureSupported || !g_ControlsAttached || !g_Browser || !g_List)
        return false;
    SBF_LoadFavorites();
    // A search or package filter that would hide it gives way.
    if (!SF::IsBlank(g_Query))
    {
        g_Query.clear();
        if (g_SearchEdit)
            SetWindowTextA(g_SearchEdit, "");
        KillTimer(g_BrowserWindow, TIMER_SB_SEARCH);
    }
    if (!g_FavoritePackage.empty() && !SF::SameName(SF::Split(path).package, g_FavoritePackage))
    {
        g_FavoritePackage.clear();
        SBF_SaveView();
    }
    if (g_FavoritesActive)
        SBF_Refresh();
    else
        SBF_SetFavoritesActive(true);
    for (size_t i = 0; i < g_Rows.size(); ++i)
    {
        if (!SF::SameName(g_Rows[i].path, path))
            continue;
        const int item = static_cast<int>(i);
        ListView_SetItemState(g_List, -1, 0, LVIS_SELECTED);
        ListView_SetItemState(g_List, item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g_List, item, FALSE);
        break;
    }
    SBF_UpdateFavoriteButton();
    return true;
}

bool SoundBrowserFavorites::Play(void* sound, const std::string& path)
{
    if (!SBF_IsSound(sound))
        return false;
    // As the browser's Play does: a sound with no data loaded reads its wave
    // from ..\packages\sounds\ first.
    char fileName[MAX_PATH] = {};
    if (SBF_StreamFileName(sound, fileName, std::size(fileName)))
    {
        const std::string file = std::string("..\\packages\\sounds\\") + fileName + ".wav";
        SBF_LoadStreamData(sound, file.c_str());
    }
    const std::string command = "AUDIO PLAY NAME=\"" + path + "\"";
    return SBF_ExecEditorCommand(command.c_str());
}

bool SoundBrowserFavorites::CustomRows()
{
    return g_CustomView;
}

bool SoundBrowserFavorites::RowPath(int item, std::string& path)
{
    if (!g_CustomView || item < 0 || static_cast<size_t>(item) >= g_Rows.size())
        return false;
    path = g_Rows[item].path;
    return true;
}

bool SoundBrowserFavorites::HasLoadedRow(const std::string& path)
{
    return std::any_of(g_Rows.begin(), g_Rows.end(), [&](const ListRow& row) { return row.sound && SF::SameName(row.path, path); });
}

void SoundBrowserFavorites::AddContextItems(HMENU context)
{
    if (!context || !g_FeatureSupported)
        return;
    std::string path;
    bool loaded = false;
    const bool has = SBF_SelectedPath(path, &loaded);
    const bool favorite = has && SF::Contains(g_Favorites, path);
    AppendMenuA(context, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(context, MF_STRING | (has ? MF_ENABLED : MF_GRAYED), kToggleFavorite,
                favorite ? "Remove from &Favorites" : "Add to &Favorites");
    AppendMenuA(context, MF_STRING, FavoritesWindow::kOpen, "All Fa&vorites...");
    // The stock commands act on the selected sound, which this row does not have.
    if (has && !loaded)
        for (int i = 0; i < GetMenuItemCount(context); ++i)
            if (GetMenuItemID(context, i) != kToggleFavorite && GetMenuItemID(context, i) != FavoritesWindow::kOpen)
                EnableMenuItem(context, i, MF_BYPOSITION | MF_GRAYED);
}

// ---------------------------------------------------------------------------
// The browser window.

static LRESULT CALLBACK SBF_SearchEditProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE)
    {
        SetWindowTextA(window, "");
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_DOWN && g_List)
    {
        SetFocus(g_List);
        return 0;
    }
    if (message == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE))
        return 0;
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, SBF_SearchEditProc, 22);
    return DefSubclassProc(window, message, wParam, lParam);
}

static bool SBF_ReadBrowserControls(void* browser)
{
    __try
    {
        auto control = [browser](size_t offset) -> HWND {
            void* wrapper = *reinterpret_cast<void**>(static_cast<char*>(browser) + offset);
            return wrapper ? *reinterpret_cast<HWND*>(static_cast<char*>(wrapper) + sizeof(void*)) : nullptr;
        };
        g_PackageCombo = control(SB_PACKAGE_COMBO);
        g_GroupCombo = control(SB_GROUP_COMBO);
        g_List = control(SB_SOUND_LIST);
        g_GroupAll = control(SB_GROUP_ALL);
        g_Unused = control(SB_UNUSED);
        return g_PackageCombo && g_GroupCombo && g_List && IsWindow(g_List);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static void SBF_AttachControls()
{
    if (g_ControlsAttached || !g_Browser || !SBF_ReadBrowserControls(g_Browser))
        return;
    HINSTANCE instance = GetModuleHandleA(nullptr);
    HFONT font = reinterpret_cast<HFONT>(SendMessageA(g_PackageCombo, WM_GETFONT, 0, 0));
    if (!font)
        font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    auto create = [&](DWORD exStyle, const char* cls, const char* text, DWORD style, int id) {
        HWND control = CreateWindowExA(exStyle, cls, text, WS_CHILD | style, 0, 0, 10, 10, g_BrowserWindow,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        if (control)
            SendMessageA(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return control;
    };
    g_SearchLabel = create(0, "STATIC", "Search:", WS_VISIBLE | SS_LEFT, IDC_SB_SEARCH_LABEL);
    g_SearchEdit = create(WS_EX_CLIENTEDGE, "EDIT", "", WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, IDC_SB_SEARCH);
    g_AllPackagesCheck = create(0, "BUTTON", "All packages", WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, IDC_SB_ALL_PACKAGES);
    g_FavoriteButton = create(0, "BUTTON", "Add Favorite", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, SoundBrowserFavorites::kToggleFavorite);
    g_FavoritesToggle = create(0, "BUTTON", "Favorites", WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_PUSHLIKE, IDC_SB_FAVORITES);
    g_PackageFilterCombo = create(0, "COMBOBOX", "", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, IDC_SB_PACKAGE_FILTER);
    g_SortCombo = create(0, "COMBOBOX", "", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, IDC_SB_SORT);
    g_WindowButton = create(0, "BUTTON", "All Favorites...", WS_TABSTOP | BS_PUSHBUTTON, static_cast<int>(FavoritesWindow::kOpen));
    if (!g_SearchEdit || !g_AllPackagesCheck || !g_FavoriteButton || !g_FavoritesToggle || !g_PackageFilterCombo || !g_SortCombo)
    {
        Logger::log("SoundBrowserFavorites: could not create the search controls");
        return;
    }
    SendMessageA(g_SearchEdit, EM_LIMITTEXT, 200, 0);
    SetWindowSubclass(g_SearchEdit, SBF_SearchEditProc, 22, 0);
    SendMessageA(g_AllPackagesCheck, BM_SETCHECK, g_AllPackages ? BST_CHECKED : BST_UNCHECKED, 0);
    for (auto sort : SF::kSorts)
        SendMessageA(g_SortCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(SF::SortLabel(sort)));
    SendMessageA(g_SortCombo, CB_SETCURSEL, static_cast<WPARAM>(g_Sort), 0);

    g_ControlsAttached = true;
    SBF_Layout(true);
    SetTimer(g_BrowserWindow, TIMER_SB_REFRESH, 1000, nullptr);
    SBF_UpdateFavoriteButton();
    Logger::log("SoundBrowserFavorites: search and Favorites attached");
}

static void SBF_OnTimer(HWND window)
{
    // Ticks since the object count last changed.
    static int steadyTicks = 0;
    if (!g_ControlsAttached)
        return;
    // Keep the search row clear if the native layout moved the list back.
    if (SBF_ChildRect(g_List).bottom > SBF_ChildRect(g_SearchEdit).top)
        SBF_Layout(false);
    if (!g_CustomView)
        return;
    if (SBF_Loading())
    {
        steadyTicks = 0;
        return;
    }
    const int count = SBF_ObjectCount();
    if (count != g_ScannedObjectCount)
    {
        steadyTicks = 0;
        SBF_Refresh();
    }
    else if (g_FavoritesActive && (steadyTicks = (std::min)(steadyTicks + 1, 2)) == 2)
    {
        // Opening another map unloads the packages it does not use. Load the
        // favourites' packages back once nothing has loaded for two ticks and
        // no modal dialog has the editor, so this never lands in a map load.
        // A package already tried is not tried again, so this is cheap.
        HWND top = GetAncestor(window, GA_ROOTOWNER);
        if (!top || IsWindowEnabled(top))
        {
            const size_t before = g_LastResolvedCount;
            SBF_LoadMissingFavoritePackages();
            if (g_LastResolvedCount != before)
                SBF_Refresh();
        }
    }
}

static bool SBF_HandleCommand(WPARAM wParam, LPARAM lParam)
{
    const UINT id = LOWORD(wParam), code = HIWORD(wParam);
    if (id == SoundBrowserFavorites::kToggleFavorite && (code == BN_CLICKED || code == 0))
    {
        SoundBrowserFavorites::ToggleSelected();
        return true;
    }
    if (id == FavoritesWindow::kOpen && (code == BN_CLICKED || code == 0))
    {
        try
        {
            FavoritesWindow::Open();
        }
        catch (const std::exception& error)
        {
            Logger::log(std::string("SoundBrowserFavorites: ") + error.what());
        }
        return true;
    }
    if (id == IDC_SB_FAVORITES && code == BN_CLICKED)
    {
        SBF_SetFavoritesActive(SendMessageA(g_FavoritesToggle, BM_GETCHECK, 0, 0) == BST_CHECKED);
        return true;
    }
    if (id == IDC_SB_ALL_PACKAGES && code == BN_CLICKED)
    {
        g_AllPackages = SendMessageA(g_AllPackagesCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        SBF_SaveView();
        SBF_Refresh();
        return true;
    }
    if (id == IDC_SB_SEARCH && code == EN_CHANGE)
    {
        // Wait for a pause in typing before searching every loaded sound.
        SetTimer(g_BrowserWindow, TIMER_SB_SEARCH, 150, nullptr);
        return true;
    }
    if ((id == IDC_SB_PACKAGE_FILTER || id == IDC_SB_SORT) && code == CBN_SELCHANGE)
    {
        const int choice = static_cast<int>(SendMessageA(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0));
        if (choice >= 0)
        {
            if (id == IDC_SB_SORT && static_cast<size_t>(choice) < std::size(SF::kSorts))
                g_Sort = SF::kSorts[choice];
            else if (id == IDC_SB_PACKAGE_FILTER)
            {
                const auto packages = SF::Packages(g_FavoritesActive ? SBF_FavoriteStates() : SF::Matching(g_SoundPaths, g_Query));
                std::string& filter = g_FavoritesActive ? g_FavoritePackage : g_SearchPackage;
                filter = choice > 0 && static_cast<size_t>(choice) <= packages.size() ? packages[choice - 1].name : std::string{};
            }
            SBF_SaveView();
            SBF_Refresh();
        }
        return true;
    }
    return false;
}

static LRESULT CALLBACK SBF_BrowserSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    switch (message)
    {
    case WM_SB_ATTACH:
        SBF_AttachControls();
        return 0;
    case WM_COMMAND:
        if (g_ControlsAttached && SBF_HandleCommand(wParam, lParam))
            return 0;
        break;
    case WM_NOTIFY:
    {
        const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header && header->hwndFrom == g_List && header->code == LVN_ITEMCHANGED)
        {
            const NMLISTVIEW* change = reinterpret_cast<const NMLISTVIEW*>(lParam);
            if (change->uChanged & LVIF_STATE)
                SBF_UpdateFavoriteButton();
        }
        break;
    }
    case WM_TIMER:
        if (wParam == TIMER_SB_SEARCH)
        {
            KillTimer(window, TIMER_SB_SEARCH);
            SBF_ReadQuery();
            SBF_Refresh();
            return 0;
        }
        if (wParam == TIMER_SB_REFRESH)
        {
            SBF_OnTimer(window);
            return 0;
        }
        break;
    case WM_SIZE:
    {
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        if (wParam != SIZE_MINIMIZED)
            SBF_Layout(true);
        return result;
    }
    case WM_NCDESTROY:
        KillTimer(window, TIMER_SB_REFRESH);
        KillTimer(window, TIMER_SB_SEARCH);
        RemoveWindowSubclass(window, SBF_BrowserSubclassProc, 21);
        g_Browser = nullptr;
        g_BrowserWindow = nullptr;
        g_PackageCombo = g_GroupCombo = g_GroupAll = g_Unused = g_List = nullptr;
        g_SearchLabel = g_SearchEdit = g_AllPackagesCheck = g_FavoriteButton = g_FavoritesToggle = nullptr;
        g_PackageFilterCombo = g_SortCombo = g_WindowButton = nullptr;
        g_ControlsAttached = false;
        g_CustomView = false;
        g_FavoritesActive = false;
        g_NativeListSortStyle = 0;
        g_Query.clear();
        g_Rows.clear();
        g_ListMargin = -1;
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

void SoundBrowserFavorites::Initialize()
{
    // Searches match the English for French names too: "drop" finds goutte_01.
    SoundFavorites::SearchText() = [](const std::string& path) {
        std::string text = path;
        const auto parts = SoundFavorites::Split(path);
        for (const auto* part : {&parts.package, &parts.group, &parts.name})
            if (!part->empty()) text += " " + AssetNameGloss::GlossFor(part->c_str());
        return text;
    };
    SBF_LoadFavorites();
    SBF_LoadView();

    static const BYTE prologue[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};
    const uintptr_t targets[] = {SB_REFRESH_SOUND_LIST, SB_GET_CURRENT_PATH_NAME, SB_ON_PLAY};
    for (uintptr_t target : targets)
    {
        if (memcmp(reinterpret_cast<const void*>(target), prologue, sizeof(prologue)) != 0)
        {
            Logger::log("SoundBrowserFavorites: editor fingerprint mismatch; feature disabled");
            return;
        }
    }
    // MOV ECX,ESI then CALL GetSelectedIndex.
    const BYTE copyCase[] = {0x8B, 0xCE, 0xE8};
    const int copyCall = static_cast<int>(SB_GET_SELECTED_INDEX - (SB_COPY_SHORTCUT_CASE + 7));
    if (memcmp(reinterpret_cast<const void*>(SB_COPY_SHORTCUT_CASE), copyCase, sizeof(copyCase)) != 0 ||
        memcmp(reinterpret_cast<const void*>(SB_COPY_SHORTCUT_CASE + 3), &copyCall, sizeof(copyCall)) != 0)
    {
        Logger::log("SoundBrowserFavorites: editor fingerprint mismatch; feature disabled");
        return;
    }
    if (!MemoryWriter::WriteJump(SB_REFRESH_SOUND_LIST, SBF_RefreshListHook) ||
        !MemoryWriter::WriteJump(SB_GET_CURRENT_PATH_NAME, SBF_GetCurrentPathNameHook) ||
        !MemoryWriter::WriteJump(SB_ON_PLAY, SBF_OnPlayHook) ||
        !MemoryWriter::WriteJump(SB_COPY_SHORTCUT_CASE, SBF_CopyShortcutHook))
    {
        Logger::log("SoundBrowserFavorites: hook install failed");
        return;
    }
    g_FeatureSupported = true;
}
