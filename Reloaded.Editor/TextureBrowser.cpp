#include "pch.h"
#include "TextureBrowser.h"
#include "FavoritesModel.h"
#include "WorkflowTools.h"
#include "MapUsagesWindow.h"
#include "FavoritesWindow.h"
#include "Hooks.h"
#include "MemoryWriter.h"
#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

INIT_HOOKS;

// SCCT's UnrealEd lacks DDS export support present in stock UE2.
// Restore DDS export for DXT1/DXT3/DXT5 textures in the Texture Browser.

#define HOOK_EXPORT_DISPATCH   0x10EA2035u
#define RESUME_NATIVE_OFN      0x10EA203Bu
#define SKIP_TO_CASE_EPILOGUE  0x10EA2101u

// UTexture field offsets
// SCCT Versus's UTexture layout differs from stock UE2; USize/VSize are not
// stored at the usual offsets. Compute dimensions from UBits/VBits instead.
#define UTEX_FORMAT_OFFSET     0x5C
#define UTEX_UBITS_OFFSET      0x5F
#define UTEX_VBITS_OFFSET      0x60
#define UTEX_MIPS_DATA         0x70   // TArray<FMipmap>.Data (FMipmap*)
#define UTEX_MIPS_NUM          0x74   // TArray<FMipmap>.Num
#define UTEX_NAME_OFFSET       0x20   // UObject::Name (FName index, lower 32 bits)

#define MIP_STRIDE             0x28
#define MIP_LAZYLOADER_OFFSET  0x10
#define MIP_DATAARRAY_DATA     0x1C
#define MIP_DATAARRAY_NUM      0x20

// TLazyArray<BYTE>::Load(this)
// Loads mip data into DataArray if not already resident.
typedef void (__fastcall *FArrayLoadFn)(void* lazyLoaderSubobj);
#define TLAZYARRAY_LOAD_ADDR   0x10EAB600u

#define TEXF_DXT1              3
#define TEXF_DXT3              7
#define TEXF_DXT5              8

#define GNAMES_DATA_PTR        0x1169cfbcu
#define GNAMES_NUM_PTR         0x1169cfc0u
#define FNAME_ENTRY_STR_OFFSET 0x0C

// Texture Browser favorites.  The editor already has a native thumbnail
// renderer for the Recent page (REN_TexBrowserMRU).  The fourth tab reuses that
// page and substitutes this persisted list only while Favorites is selected;
// the editor's real MRU list is never modified.  Favourites come from every
// package, whichever map is open: their packages are loaded as needed.  While
// Favorites is shown, a package filter and a sort order sit above the
// thumbnails, and favourites that cannot be loaded (so have no thumbnail) are
// listed below them, where their right-click menu can still remove them.
#define HOOK_MRU_LIST_BUILD         0x10ECA1E8u
#define RESUME_MRU_LIST_BUILD       0x10ECA1EEu
#define CONTINUE_AFTER_LIST_BUILD   0x10EC9D19u
#define GTB_OPTIONS_PTR             0x1165DFECu
#define GEDITOR_PTR                 0x1165DFA0u
#define EXEC_LOG_DEV                0x115BEFB0u
#define GOBJECTS_DATA_PTR           0x11697B70u
#define GOBJECTS_NUM_PTR            0x11697B74u
#define CREATEWINDOWEXA_IAT_SLOT    0x11AF23E0u
#define LOADMENUA_IAT_SLOT          0x11AF23F0u

#define UOBJECT_OUTER_OFFSET        0x18
#define UOBJECT_FNAME_OFFSET        0x20
#define UOBJECT_CLASS_OFFSET        0x24
#define UCLASS_SUPER_OFFSET         0x28
#define GEDITOR_CURRENT_MATERIAL    0x138

#define TEXTURE_CONTEXT_MENU_ID     15109
#define IDMN_TB_TOGGLE_FAVORITE     40908
// Child controls of the browser's tab control; their notifications go there.
#define IDC_TB_FAVORITES_PACKAGE    40916
#define IDC_TB_FAVORITES_SORT       40917
#define IDC_TB_FAVORITES_NOT_LOADED 40918
#define WM_TB_REFRESH_FAVORITES     (WM_APP + 0x5C)
#define WM_TB_LAYOUT_FAVORITES      (WM_APP + 0x5D)
#define TIMER_TB_FAVORITES_REFRESH  0x5AF2

static constexpr const char* kFavoritesIniSection = "TextureBrowserFavorites";
// Kept apart: saving the favourites rewrites their whole section.
static constexpr const char* kViewIniSection = "TextureBrowserFavoritesView";
static constexpr int kMaxFavorites = 4096;
static constexpr int kMaxRenderedMaterials = 16384;
static constexpr int kMaxNotLoadedRows = 4;

// g_FavoritePaths is in the order the favourites were added (the ini order);
// g_FavoriteObjects holds each one's material while it is loaded, else nullptr.
static std::vector<std::string> g_FavoritePaths;
static std::vector<void*> g_FavoriteObjects;
static size_t g_LastResolvedCount = 0;
// Packages already loaded (or found missing) for unloaded favourites; cleared
// when favourites drop out of memory, as they do when another map is opened.
static std::unordered_set<std::string> g_AttemptedPackages;
// What the Favorites page shows: the thumbnails' favourites in drawing order,
// and the rows of the ones that are not loaded.
static std::vector<size_t> g_TileFavorites;
static std::vector<Favorites::Row> g_NotLoadedRows;
static std::string g_PackageFilter; // empty: every package
static Favorites::Sort g_Sort = Favorites::Sort::Package;
// The favourite a right-click on the not-loaded list was over, while its menu is open.
static std::string g_ContextPath;
static std::string g_FavoritesIniPath;
static volatile LONG g_FavoritesActive = FALSE;
static HWND g_TextureTab = nullptr;
static int g_LastNativeTab = 0;
static int g_LastGObjectsCount = -1;
static void* g_MaterialClass = nullptr;

// The Recent page the Favorites tab borrows, and the controls added around it.
// While Favorites is shown the page is moved down by g_TopStrip and shortened
// by g_BottomStrip as well, so the native layout fits the thumbnails between.
static HWND g_MruPage = nullptr;
static RECT g_MruNativeRect = {};
static RECT g_LaidOutRect = {}; // the native place the controls were last put around
static int g_TopStrip = 0;
static int g_BottomStrip = 0;
static HWND g_PackageFilterCombo = nullptr;
static HWND g_SortCombo = nullptr;
static HWND g_NotLoadedList = nullptr;
static HWND g_WindowButton = nullptr; // opens the Favorites window

typedef HWND(WINAPI* CreateWindowExAFn)(DWORD, LPCSTR, LPCSTR, DWORD,
                                        int, int, int, int, HWND, HMENU,
                                        HINSTANCE, LPVOID);
typedef HMENU(WINAPI* LoadMenuAFn)(HINSTANCE, LPCSTR);
static CreateWindowExAFn g_PreviousCreateWindowExA = nullptr;
static LoadMenuAFn g_PreviousLoadMenuA = nullptr;
static void TB_SaveFavorites();

static std::string TB_GetIniPath()
{
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash)
        *(lastSlash + 1) = '\0';
    return std::string(exePath) + "Reloaded_Editor.ini";
}

static std::string TB_ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static std::string TB_NormalizeLegacyPath(const std::string& path)
{
    // Older favorites treated UObject+0x24 (the class pointer in this build)
    // as an FName number and persisted it as an address-like suffix.
    std::string normalized;
    size_t start = 0;
    while (start <= path.size())
    {
        const size_t end = path.find('.', start);
        std::string part = path.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        const size_t underscore = part.rfind('_');
        if (underscore != std::string::npos && underscore + 1 < part.size())
        {
            unsigned long long value = 0;
            bool digits = true;
            for (size_t i = underscore + 1; i < part.size(); ++i)
            {
                const unsigned char c = static_cast<unsigned char>(part[i]);
                if (!std::isdigit(c))
                {
                    digits = false;
                    break;
                }
                value = value * 10 + (c - '0');
            }
            if (digits && value + 1 >= 0x10E00000ull &&
                value + 1 < 0x12000000ull)
                part.erase(underscore);
        }

        if (!normalized.empty())
            normalized.push_back('.');
        normalized += part;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return normalized;
}

// Keep all fault-prone engine memory reads in leaf functions without C++
// objects, allowing SEH to protect favorites from stale UObject pointers.
static bool TB_ReadObjectIdentity(void* object, void** outer,
                                  char* name, size_t nameSize)
{
    if (!object || !outer || !name || nameSize == 0)
        return false;

    __try
    {
        void** names = *reinterpret_cast<void***>(GNAMES_DATA_PTR);
        const INT nameCount = *reinterpret_cast<INT*>(GNAMES_NUM_PTR);
        const INT nameIndex = *reinterpret_cast<INT*>(
            static_cast<char*>(object) + UOBJECT_FNAME_OFFSET);
        if (!names || nameIndex < 0 || nameIndex >= nameCount)
            return false;

        void* entry = names[nameIndex];
        if (!entry)
            return false;

        const char* source = reinterpret_cast<const char*>(entry) +
            FNAME_ENTRY_STR_OFFSET;
        if (!source || !source[0])
            return false;

        strncpy_s(name, nameSize, source, _TRUNCATE);
        *outer = *reinterpret_cast<void**>(
            static_cast<char*>(object) + UOBJECT_OUTER_OFFSET);
        return name[0] != '\0';
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        name[0] = '\0';
        *outer = nullptr;
        return false;
    }
}

static bool TB_ReadPointerAt(void* object, size_t offset, void** value)
{
    __try
    {
        *value = *reinterpret_cast<void**>(static_cast<char*>(object) + offset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *value = nullptr;
        return false;
    }
}

// Engine.Material, found by name the first time a material is met.
static bool TB_IsMaterialClass(void* objectClass)
{
    if (g_MaterialClass)
        return objectClass == g_MaterialClass;

    char name[64] = {};
    char package[64] = {};
    void* outer = nullptr;
    void* packageOuter = nullptr;
    if (!TB_ReadObjectIdentity(objectClass, &outer, name, std::size(name)) ||
        _stricmp(name, "Material") != 0 || !outer ||
        !TB_ReadObjectIdentity(outer, &packageOuter, package, std::size(package)) ||
        _stricmp(package, "Engine") != 0 || packageOuter)
        return false;
    g_MaterialClass = objectClass;
    return true;
}

static bool TB_IsMaterial(void* object)
{
    void* objectClass = nullptr;
    if (!object || !TB_ReadPointerAt(object, UOBJECT_CLASS_OFFSET, &objectClass))
        return false;
    for (int depth = 0; objectClass && depth < 64; ++depth)
    {
        if (TB_IsMaterialClass(objectClass))
            return true;
        if (!TB_ReadPointerAt(objectClass, UCLASS_SUPER_OFFSET, &objectClass))
            return false;
    }
    return false;
}

static bool TB_BuildObjectPath(void* object, std::string& path)
{
    struct ObjectNamePart
    {
        char Name[256];
    };

    ObjectNamePart parts[32] = {};
    int partCount = 0;
    void* cursor = object;

    while (cursor && partCount < static_cast<int>(std::size(parts)))
    {
        void* outer = nullptr;
        if (!TB_ReadObjectIdentity(cursor, &outer, parts[partCount].Name,
                                   std::size(parts[partCount].Name)))
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

static bool TB_ReadGObjects(void*** data, INT* count)
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

static bool TB_ReadGObjectAt(void** data, INT index, void** object)
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

static void* TB_GetCurrentMaterial()
{
    __try
    {
        void* editor = *reinterpret_cast<void**>(GEDITOR_PTR);
        return editor ? *reinterpret_cast<void**>(
            static_cast<char*>(editor) + GEDITOR_CURRENT_MATERIAL) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

static bool TB_FindFavorite(const std::string& path, size_t* index = nullptr)
{
    const std::string wanted = TB_ToLower(path);
    for (size_t i = 0; i < g_FavoritePaths.size(); ++i)
    {
        if (TB_ToLower(g_FavoritePaths[i]) == wanted)
        {
            if (index)
                *index = i;
            return true;
        }
    }
    return false;
}

static void TB_LoadFavorites()
{
    g_FavoritesIniPath = TB_GetIniPath();
    g_FavoritePaths.clear();
    bool migrated = false;

    int count = GetPrivateProfileIntA(kFavoritesIniSection, "Count", 0,
                                      g_FavoritesIniPath.c_str());
    count = (std::max)(0, (std::min)(count, kMaxFavorites));
    for (int i = 0; i < count; ++i)
    {
        char key[32] = {};
        char value[1024] = {};
        snprintf(key, sizeof(key), "Favorite%d", i);
        GetPrivateProfileStringA(kFavoritesIniSection, key, "", value,
                                 static_cast<DWORD>(std::size(value)),
                                 g_FavoritesIniPath.c_str());
        if (value[0])
        {
            const std::string normalized = TB_NormalizeLegacyPath(value);
            migrated = migrated || normalized != value;
            if (!TB_FindFavorite(normalized))
                g_FavoritePaths.push_back(normalized);
        }
    }
    if (migrated)
        TB_SaveFavorites();
    // Paths re-read from the ini start unresolved; the next refresh finds them.
    g_FavoriteObjects.assign(g_FavoritePaths.size(), nullptr);
}

static void TB_SaveFavorites()
{
    if (g_FavoritesIniPath.empty())
        g_FavoritesIniPath = TB_GetIniPath();

    // Recreate only this section so removed entries cannot linger.
    WritePrivateProfileStringA(kFavoritesIniSection, nullptr, nullptr,
                               g_FavoritesIniPath.c_str());

    char count[32] = {};
    snprintf(count, sizeof(count), "%zu", g_FavoritePaths.size());
    WritePrivateProfileStringA(kFavoritesIniSection, "Count", count,
                               g_FavoritesIniPath.c_str());

    for (size_t i = 0; i < g_FavoritePaths.size(); ++i)
    {
        char key[32] = {};
        snprintf(key, sizeof(key), "Favorite%zu", i);
        WritePrivateProfileStringA(kFavoritesIniSection, key,
                                   g_FavoritePaths[i].c_str(),
                                   g_FavoritesIniPath.c_str());
    }
}

static void TB_LoadView()
{
    const std::string ini = TB_GetIniPath();
    char value[256] = {};
    GetPrivateProfileStringA(kViewIniSection, "Sort", "", value,
                             static_cast<DWORD>(std::size(value)), ini.c_str());
    g_Sort = Favorites::SortFromKey(value);
    GetPrivateProfileStringA(kViewIniSection, "Package", "", value,
                             static_cast<DWORD>(std::size(value)), ini.c_str());
    g_PackageFilter = value;
}

static void TB_SaveView()
{
    const std::string ini = TB_GetIniPath();
    WritePrivateProfileStringA(kViewIniSection, "Sort", Favorites::SortKey(g_Sort), ini.c_str());
    WritePrivateProfileStringA(kViewIniSection, "Package", g_PackageFilter.c_str(), ini.c_str());
}

static void TB_RefreshResolvedFavorites()
{
    g_FavoriteObjects.assign(g_FavoritePaths.size(), nullptr);
    void** objects = nullptr;
    INT objectCount = 0;
    if (!TB_ReadGObjects(&objects, &objectCount))
        return;

    g_LastGObjectsCount = objectCount;
    if (g_FavoritePaths.empty())
    {
        g_LastResolvedCount = 0;
        return;
    }

    std::unordered_map<std::string, size_t> wanted;
    wanted.reserve(g_FavoritePaths.size());
    for (size_t i = 0; i < g_FavoritePaths.size(); ++i)
        wanted.emplace(TB_ToLower(g_FavoritePaths[i]), i);

    std::vector<void*> resolved(g_FavoritePaths.size(), nullptr);
    size_t remaining = wanted.size();
    for (INT i = 0; i < objectCount && remaining > 0; ++i)
    {
        void* object = nullptr;
        if (!TB_ReadGObjectAt(objects, i, &object) || !TB_IsMaterial(object))
            continue;

        std::string path;
        if (!TB_BuildObjectPath(object, path))
            continue;

        const auto found = wanted.find(TB_ToLower(path));
        if (found != wanted.end() && !resolved[found->second])
        {
            resolved[found->second] = object;
            --remaining;
        }
    }

    g_FavoriteObjects = resolved;
    const size_t resolvedCount = static_cast<size_t>(
        std::count_if(resolved.begin(), resolved.end(), [](void* o) { return o != nullptr; }));
    // Opening another map unloads the texture packages it does not use; let
    // their favourites' packages be loaded again.
    if (resolvedCount < g_LastResolvedCount)
        g_AttemptedPackages.clear();
    g_LastResolvedCount = resolvedCount;
}

// The favourite's material, if it is still the object it was resolved to: a
// map change can free it and reuse its memory before the next refresh.
static void* TB_LiveFavorite(size_t favorite)
{
    if (favorite >= g_FavoriteObjects.size() || !g_FavoriteObjects[favorite])
        return nullptr;
    void* object = g_FavoriteObjects[favorite];
    std::string path;
    if (!TB_IsMaterial(object) || !TB_BuildObjectPath(object, path) ||
        TB_ToLower(path) != TB_ToLower(g_FavoritePaths[favorite]))
        return nullptr;
    return object;
}

static bool __cdecl TB_ExecEditorCommand(const char* command)
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

static std::string TB_GetPackageDirectory(const char* folder)
{
    char executable[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, executable,
                            static_cast<DWORD>(std::size(executable))))
        return {};
    char* slash = strrchr(executable, '\\');
    if (!slash)
        return {};
    *(slash + 1) = '\0';

    const std::string relative = std::string(executable) +
        "..\\Packages\\" + folder + "\\";
    char fullPath[MAX_PATH] = {};
    const DWORD length = GetFullPathNameA(relative.c_str(),
        static_cast<DWORD>(std::size(fullPath)), fullPath, nullptr);
    if (!length || length >= std::size(fullPath))
        return {};
    return fullPath;
}

// Package files by lower-case name. An earlier folder wins a name clash.
static void TB_FindPackageFiles(const char* folder, const char* pattern,
                                std::unordered_map<std::string, std::string>& files)
{
    const std::string directory = TB_GetPackageDirectory(folder);
    if (directory.empty())
        return;

    WIN32_FIND_DATAA data = {};
    HANDLE search = FindFirstFileA((directory + pattern).c_str(), &data);
    if (search == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        std::string fileName = data.cFileName;
        const size_t dot = fileName.rfind('.');
        const std::string baseName = dot == std::string::npos
            ? fileName : fileName.substr(0, dot);
        files.emplace(TB_ToLower(baseName), directory + fileName);
    } while (FindNextFileA(search, &data));
    FindClose(search);
}

static void TB_LoadMissingFavoritePackages()
{
    std::unordered_map<std::string, std::string> wantedPackages;
    for (size_t i = 0; i < g_FavoritePaths.size(); ++i)
    {
        if (i < g_FavoriteObjects.size() && g_FavoriteObjects[i])
            continue;
        const std::string package = Favorites::Split(g_FavoritePaths[i]).package;
        // Each package is tried once until favourites are unloaded again, so
        // one that is missing or is a map's own package is not retried every
        // second.
        if (!package.empty() && g_AttemptedPackages.insert(TB_ToLower(package)).second)
            wantedPackages.emplace(TB_ToLower(package), package);
    }
    if (wantedPackages.empty())
        return;

    // Materials live in texture packages, and in static mesh packages that
    // carry their own.
    std::unordered_map<std::string, std::string> packageFiles;
    TB_FindPackageFiles("Textures", "*.utx", packageFiles);
    TB_FindPackageFiles("StaticMeshes", "*.usx", packageFiles);

    int loaded = 0;
    int unavailable = 0;
    for (const auto& wanted : wantedPackages)
    {
        const auto file = packageFiles.find(wanted.first);
        if (file == packageFiles.end())
        {
            ++unavailable;
            continue;
        }

        char command[MAX_PATH + 32] = {};
        _snprintf_s(command, sizeof(command), _TRUNCATE,
                    "OBJ LOAD FILE=\"%s\"", file->second.c_str());
        if (TB_ExecEditorCommand(command))
            ++loaded;
        else
            ++unavailable;
    }

    if (loaded > 0)
        Logger::log("TextureBrowser: loaded " + std::to_string(loaded) +
                    " favorite package(s)");
    if (unavailable > 0)
        Logger::log("TextureBrowser: could not locate or load " +
            std::to_string(unavailable) + " favorite package(s)");

    TB_RefreshResolvedFavorites();
}

static void TB_RedrawFavoritesPage()
{
    if (!g_TextureTab || !IsWindow(g_TextureTab))
        return;

    RedrawWindow(GetParent(g_TextureTab), nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

static std::vector<Favorites::Favorite> TB_FavoriteStates()
{
    std::vector<Favorites::Favorite> favorites;
    favorites.reserve(g_FavoritePaths.size());
    for (size_t i = 0; i < g_FavoritePaths.size(); ++i)
        favorites.push_back({ g_FavoritePaths[i], i < g_FavoriteObjects.size() && g_FavoriteObjects[i] });
    return favorites;
}

// The package filter's entries: every package, then each one with its count.
// A filter naming a package with no favourites left falls back to every package.
static void TB_FillPackageFilter(const std::vector<Favorites::Favorite>& favorites)
{
    const auto packages = Favorites::Packages(favorites);
    if (!g_PackageFilter.empty() &&
        std::none_of(packages.begin(), packages.end(), [](const Favorites::Package& p)
            { return TB_ToLower(p.name) == TB_ToLower(g_PackageFilter); }))
        g_PackageFilter.clear();
    if (!g_PackageFilterCombo)
        return;

    SendMessageA(g_PackageFilterCombo, WM_SETREDRAW, FALSE, 0);
    SendMessageA(g_PackageFilterCombo, CB_RESETCONTENT, 0, 0);
    SendMessageA(g_PackageFilterCombo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(Favorites::AllLabel(favorites.size()).c_str()));
    int selected = 0;
    for (size_t i = 0; i < packages.size(); ++i)
    {
        SendMessageA(g_PackageFilterCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(Favorites::PackageLabel(packages[i]).c_str()));
        if (TB_ToLower(packages[i].name) == TB_ToLower(g_PackageFilter))
            selected = static_cast<int>(i) + 1;
    }
    SendMessageA(g_PackageFilterCombo, CB_SETCURSEL, selected, 0);
    SendMessageA(g_PackageFilterCombo, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_PackageFilterCombo, nullptr, TRUE);
}

// The package the filter's entry at index names (0 is every package).
static std::string TB_PackageAt(int index)
{
    const auto packages = Favorites::Packages(TB_FavoriteStates());
    return index > 0 && static_cast<size_t>(index) <= packages.size() ? packages[index - 1].name : std::string{};
}

static int TB_ControlHeight(HWND control, int fallback)
{
    RECT rect = {};
    return control && GetWindowRect(control, &rect) && rect.bottom > rect.top
        ? static_cast<int>(rect.bottom - rect.top) : fallback;
}

// Places the Favorites controls around the borrowed Recent page and moves the
// page between them; with Favorites off, the page gets its native place back.
static void TB_ApplyFavoritesLayout()
{
    if (!g_MruPage || !IsWindow(g_MruPage))
        return;

    const bool active = InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE) != FALSE;
    constexpr int gap = 3;
    const int comboHeight = TB_ControlHeight(g_SortCombo, 21);
    const RECT native = g_MruNativeRect;
    g_LaidOutRect = native;
    const int width = static_cast<int>(native.right - native.left);
    const int height = static_cast<int>(native.bottom - native.top);

    int listHeight = 0;
    if (active && g_NotLoadedList && !g_NotLoadedRows.empty())
    {
        const int itemHeight = (std::max)(1, static_cast<int>(
            SendMessage(g_NotLoadedList, LB_GETITEMHEIGHT, 0, 0)));
        const int rows = (std::min)(static_cast<int>(g_NotLoadedRows.size()), kMaxNotLoadedRows);
        listHeight = rows * itemHeight + 2 * GetSystemMetrics(SM_CYEDGE);
        // Never leave the thumbnails less than half the page.
        listHeight = (std::min)(listHeight, (std::max)(0, height / 2 - gap));
    }

    g_TopStrip = active ? 2 * (comboHeight + gap) : 0;
    g_BottomStrip = listHeight > 0 ? listHeight + gap : 0;

    // The drop-down height includes its list.
    constexpr int dropHeight = 320;
    if (g_PackageFilterCombo)
        SetWindowPos(g_PackageFilterCombo, HWND_TOP, native.left, native.top,
                     width, dropHeight, SWP_NOACTIVATE | (active ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    // The sort order shares its row with the Favorites window's button.
    constexpr int windowButtonWidth = 96;
    const int sortWidth = g_WindowButton ? (std::max)(40, width - windowButtonWidth - gap) : width;
    if (g_SortCombo)
        SetWindowPos(g_SortCombo, HWND_TOP, native.left, native.top + comboHeight + gap,
                     sortWidth, dropHeight, SWP_NOACTIVATE | (active ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    if (g_WindowButton)
        SetWindowPos(g_WindowButton, HWND_TOP, native.left + sortWidth + gap, native.top + comboHeight + gap,
                     (std::max)(1, width - sortWidth - gap), comboHeight,
                     SWP_NOACTIVATE | (active ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    if (g_NotLoadedList)
        SetWindowPos(g_NotLoadedList, HWND_TOP, native.left, native.bottom - listHeight,
                     width, (std::max)(listHeight, 1),
                     SWP_NOACTIVATE | (listHeight > 0 ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));

    // TB_MruPageSubclassProc applies the strips to this native placement.
    SetWindowPos(g_MruPage, nullptr, native.left, native.top, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

// Rebuilds what the Favorites page shows from the favourites' current state.
static void TB_UpdateFavoritesView()
{
    void** objects = nullptr;
    INT objectCount = 0;
    if (TB_ReadGObjects(&objects, &objectCount) &&
        objectCount != g_LastGObjectsCount)
        TB_RefreshResolvedFavorites();

    const auto favorites = TB_FavoriteStates();
    TB_FillPackageFilter(favorites);
    auto view = Favorites::Tiles(favorites, g_PackageFilter, g_Sort);
    g_TileFavorites = std::move(view.tiles);
    g_NotLoadedRows = std::move(view.notLoaded);

    if (g_NotLoadedList)
    {
        SendMessageA(g_NotLoadedList, WM_SETREDRAW, FALSE, 0);
        SendMessageA(g_NotLoadedList, LB_RESETCONTENT, 0, 0);
        for (const auto& row : g_NotLoadedRows)
            SendMessageA(g_NotLoadedList, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(row.label.c_str()));
        SendMessageA(g_NotLoadedList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g_NotLoadedList, nullptr, TRUE);
    }

    if (InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE))
    {
        TB_ApplyFavoritesLayout();
        TB_RedrawFavoritesPage();
    }
}

static int __cdecl TB_CopyFavoriteMaterials(void** destination, int capacity)
{
    if (!destination || capacity <= 0)
        return 0;

    // Each thumbnail's material is checked before the native draw uses it.
    // New objects are left to the refresh timer: drawing may create some.
    bool stale = false;
    int count = 0;
    for (size_t favorite : g_TileFavorites)
    {
        if (count >= capacity)
            break;
        void* material = TB_LiveFavorite(favorite);
        if (material)
            destination[count++] = material;
        else
            stale = true;
    }
    // Rebuild the page once this draw is over.
    if (stale && g_TextureTab)
        PostMessage(g_TextureTab, WM_TB_REFRESH_FAVORITES, 0, 0);
    return count;
}

// At this point DrawTextureBrowser has selected REN_TexBrowserMRU and EDI is
// its temporary UMaterial* list.  Preserve the native path for Recent; for the
// Favorites tab, fill the same list and continue at the shared draw code.
__declspec(naked) static void TB_FavoritesRenderHook()
{
    static int resumeNative = static_cast<int>(RESUME_MRU_LIST_BUILD);
    static int continueDraw = static_cast<int>(CONTINUE_AFTER_LIST_BUILD);

    __asm
    {
        mov  ecx, dword ptr [GTB_OPTIONS_PTR]
        cmp  dword ptr [g_FavoritesActive], 0
        je   native_mru

        lea  edx, [ecx + 20h]           // FTBOptions::LastScrollMRU
        mov  dword ptr [ebp - 74h], edx // DrawTextureBrowser::LastScroll
        push kMaxRenderedMaterials
        push edi
        call TB_CopyFavoriteMaterials
        add  esp, 8
        mov  dword ptr [ebp - 44h], eax // DrawTextureBrowser::n
        jmp  dword ptr [continueDraw]

    native_mru:
        jmp  dword ptr [resumeNative]
    }
}

static bool TB_CurrentMaterialPath(std::string& path)
{
    void* material = TB_GetCurrentMaterial();
    return material && TB_IsMaterial(material) && TB_BuildObjectPath(material, path);
}

static void TB_ToggleCurrentFavorite()
{
    // A right-click in the not-loaded list names its row; anywhere else it is
    // the browser's current material.
    std::string path = g_ContextPath;
    if (path.empty() && !TB_CurrentMaterialPath(path))
        return;

    // Start from the saved list, so favourites added in another open editor
    // are not dropped by this one's save.
    TB_LoadFavorites();
    size_t index = 0;
    if (TB_FindFavorite(path, &index))
        g_FavoritePaths.erase(g_FavoritePaths.begin() + index);
    else if (g_FavoritePaths.size() < kMaxFavorites)
        g_FavoritePaths.push_back(path);
    else
    {
        MessageBoxA(g_TextureTab,
                    "The Favorites list has reached its 4096-item limit.",
                    "Texture Browser", MB_OK | MB_ICONWARNING);
        return;
    }

    TB_SaveFavorites();
    TB_RefreshResolvedFavorites();
    TB_UpdateFavoritesView();
    FavoritesWindow::Changed();
}

static void TB_OpenFavoritesWindow()
{
    try
    {
        FavoritesWindow::Open();
    }
    catch (const std::exception& error)
    {
        Logger::log(std::string("TextureBrowser: ") + error.what());
    }
}

static int TB_MenuPosition(HMENU menu, UINT command)
{
    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i)
    {
        if (GetMenuItemID(menu, i) == command)
            return i;
    }
    return -1;
}

static HMENU WINAPI TB_LoadMenuA_Hook(HINSTANCE instance, LPCSTR menuName)
{
    HMENU menu = g_PreviousLoadMenuA
        ? g_PreviousLoadMenuA(instance, menuName)
        : LoadMenuA(instance, menuName);
    if (!menu || (reinterpret_cast<ULONG_PTR>(menuName) >> 16) != 0 ||
        LOWORD(reinterpret_cast<ULONG_PTR>(menuName)) != TEXTURE_CONTEXT_MENU_ID)
        return menu;

    HMENU context = GetSubMenu(menu, 0);
    if (!context || TB_MenuPosition(context, IDMN_TB_TOGGLE_FAVORITE) >= 0)
        return menu;

    std::string path;
    const bool hasMaterial = TB_CurrentMaterialPath(path);
    // A not-loaded favourite can only be removed: every other command acts
    // on the browser's current material, which is not this row.
    const bool notLoaded = !g_ContextPath.empty();
    const bool isFavorite = notLoaded || (hasMaterial && TB_FindFavorite(path));
    const char* label = isFavorite
        ? "Remove from &Favorites"
        : "Add to &Favorites";
    const UINT toggleFlags = MF_BYPOSITION | MF_STRING |
        (hasMaterial || notLoaded ? MF_ENABLED : MF_GRAYED);
    const UINT materialFlags = MF_BYPOSITION | MF_STRING |
        (hasMaterial && !notLoaded ? MF_ENABLED : MF_GRAYED);

    // Stock positions 0-2 are Properties, Duplicate and Rename.
    InsertMenuA(context, 3, toggleFlags, IDMN_TB_TOGGLE_FAVORITE, label);
    InsertMenuA(context, 4, materialFlags, WorkflowTools::kFindMaterial, "Find &Usages...");
    InsertMenuA(context, 5, materialFlags, MapUsagesWindow::kFindMaterial, "Find Usages in All &Maps...");
    InsertMenuA(context, 6, MF_BYPOSITION | MF_STRING, FavoritesWindow::kOpen, "All Fa&vorites...");
    if (notLoaded)
        for (int i = 0; i < GetMenuItemCount(context); ++i)
            if (GetMenuItemID(context, i) != IDMN_TB_TOGGLE_FAVORITE && GetMenuItemID(context, i) != FavoritesWindow::kOpen)
                EnableMenuItem(context, i, MF_BYPOSITION | MF_GRAYED);
    return menu;
}

// The right-click menu of a not-loaded favourite, at a screen point (or under
// the selected row for the keyboard's menu key).
static void TB_ShowNotLoadedMenu(HWND list, LPARAM screenPosition)
{
    POINT screenPoint = { GET_X_LPARAM(screenPosition), GET_Y_LPARAM(screenPosition) };
    int item = -1;
    if (screenPosition == -1)
    {
        item = static_cast<int>(SendMessage(list, LB_GETCURSEL, 0, 0));
        RECT itemRect = {};
        if (item < 0 || SendMessage(list, LB_GETITEMRECT, item,
                                    reinterpret_cast<LPARAM>(&itemRect)) == LB_ERR)
            return;
        screenPoint = { itemRect.left, itemRect.bottom };
        ClientToScreen(list, &screenPoint);
    }
    else
    {
        POINT clientPoint = screenPoint;
        ScreenToClient(list, &clientPoint);
        const LRESULT hit = SendMessage(list, LB_ITEMFROMPOINT, 0,
                                        MAKELPARAM(clientPoint.x, clientPoint.y));
        if (HIWORD(hit))
            return; // outside every row
        item = LOWORD(hit);
    }
    if (item < 0 || static_cast<size_t>(item) >= g_NotLoadedRows.size())
        return;
    SendMessage(list, LB_SETCURSEL, item, 0);

    g_ContextPath = g_FavoritePaths[g_NotLoadedRows[item].favorite];
    struct ClearContext { ~ClearContext() { g_ContextPath.clear(); } } clearContext;

    HMENU menu = TB_LoadMenuA_Hook(GetModuleHandleA(nullptr),
                                   MAKEINTRESOURCEA(TEXTURE_CONTEXT_MENU_ID));
    if (!menu)
        return;
    HMENU context = GetSubMenu(menu, 0);
    UINT command = 0;
    if (context)
    {
        command = TrackPopupMenu(context, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                 screenPoint.x, screenPoint.y, 0, list, nullptr);
        PostMessage(list, WM_NULL, 0, 0);
    }
    DestroyMenu(menu);
    if (command == IDMN_TB_TOGGLE_FAVORITE)
        TB_ToggleCurrentFavorite();
    else if (command == FavoritesWindow::kOpen)
        TB_OpenFavoritesWindow();
}

static LRESULT CALLBACK TB_MruPageSubclassProc(HWND page, UINT message,
                                               WPARAM wParam, LPARAM lParam,
                                               UINT_PTR, DWORD_PTR)
{
    if (message == WM_WINDOWPOSCHANGING)
    {
        // Track where the property sheet puts the page, and while Favorites
        // is shown move it between the Favorites controls.
        WINDOWPOS* position = reinterpret_cast<WINDOWPOS*>(lParam);
        if (position && (!(position->flags & SWP_NOMOVE) || !(position->flags & SWP_NOSIZE)))
        {
            RECT& native = g_MruNativeRect;
            if (!(position->flags & SWP_NOMOVE))
                OffsetRect(&native, position->x - native.left, position->y - native.top);
            if (!(position->flags & SWP_NOSIZE))
            {
                native.right = native.left + position->cx;
                native.bottom = native.top + position->cy;
            }
            position->x = native.left;
            position->y = native.top + g_TopStrip;
            position->cx = native.right - native.left;
            position->cy = (std::max)(0, static_cast<int>(native.bottom - native.top) - g_TopStrip - g_BottomStrip);
            position->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
        }
    }
    else if (message == WM_WINDOWPOSCHANGED &&
             InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE))
    {
        // The property sheet moved the page (the browser was resized): bring
        // the Favorites controls along once this move is over.
        if (!EqualRect(&g_LaidOutRect, &g_MruNativeRect) && g_TextureTab)
            PostMessage(g_TextureTab, WM_TB_LAYOUT_FAVORITES, 0, 0);
    }
    else if (message == WM_NCDESTROY)
    {
        if (page == g_MruPage)
            g_MruPage = nullptr;
        RemoveWindowSubclass(page, TB_MruPageSubclassProc, 4);
    }
    return DefSubclassProc(page, message, wParam, lParam);
}

// Finds the Recent page and adds the Favorites controls beside it, once per
// browser window.
static bool TB_AttachFavoritesPage(HWND tab)
{
    if (g_MruPage && IsWindow(g_MruPage) && g_SortCombo && IsWindow(g_SortCombo))
        return true;

    HWND page = FindWindowExA(tab, nullptr, "SplinterCell2UnrealWPageMRU", nullptr);
    if (!page)
        return false;
    if (page != g_MruPage)
    {
        RECT rect = {};
        GetWindowRect(page, &rect);
        MapWindowPoints(nullptr, tab, reinterpret_cast<POINT*>(&rect), 2);
        g_MruNativeRect = rect;
        g_TopStrip = 0;
        g_BottomStrip = 0;
        if (!SetWindowSubclass(page, TB_MruPageSubclassProc, 4, 0))
            return false;
        g_MruPage = page;
    }

    HFONT font = reinterpret_cast<HFONT>(SendMessage(tab, WM_GETFONT, 0, 0));
    if (!font)
        font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    auto create = [tab, font](const char* className, DWORD style, int id) {
        HWND control = CreateWindowExA(
            0, className, "", WS_CHILD | WS_TABSTOP | style,
            0, 0, 120, 120, tab,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleA(nullptr), nullptr);
        if (control)
            SendMessage(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return control;
    };
    if (!g_PackageFilterCombo || !IsWindow(g_PackageFilterCombo))
        g_PackageFilterCombo = create("COMBOBOX", WS_VSCROLL | CBS_DROPDOWNLIST,
                                      IDC_TB_FAVORITES_PACKAGE);
    if (!g_SortCombo || !IsWindow(g_SortCombo))
    {
        g_SortCombo = create("COMBOBOX", WS_VSCROLL | CBS_DROPDOWNLIST,
                             IDC_TB_FAVORITES_SORT);
        for (auto sort : Favorites::kSorts)
            SendMessageA(g_SortCombo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(Favorites::SortLabel(sort)));
    }
    if (g_SortCombo)
        SendMessage(g_SortCombo, CB_SETCURSEL, static_cast<WPARAM>(g_Sort), 0);
    if (!g_NotLoadedList || !IsWindow(g_NotLoadedList))
        g_NotLoadedList = create("LISTBOX", WS_VSCROLL | WS_BORDER | LBS_NOTIFY |
                                 LBS_NOINTEGRALHEIGHT, IDC_TB_FAVORITES_NOT_LOADED);
    if (!g_WindowButton || !IsWindow(g_WindowButton))
    {
        g_WindowButton = create("BUTTON", BS_PUSHBUTTON, static_cast<int>(FavoritesWindow::kOpen));
        if (g_WindowButton)
            SetWindowTextA(g_WindowButton, "All Favorites...");
    }
    return g_SortCombo != nullptr;
}

static void TB_SendNativeTabChanged(HWND tab)
{
    NMHDR notification = {};
    notification.hwndFrom = tab;
    notification.idFrom = static_cast<UINT_PTR>(GetDlgCtrlID(tab));
    notification.code = TCN_SELCHANGE;
    SendMessage(GetParent(tab), WM_NOTIFY, notification.idFrom,
                reinterpret_cast<LPARAM>(&notification));
}

static void TB_ActivateFavorites(HWND tab)
{
    if (!tab || !IsWindow(tab))
        return;

    // Subclass the Recent page first, so it is moved into place as the
    // property sheet shows it.
    TB_AttachFavoritesPage(tab);
    if (!InterlockedCompareExchange(&g_FavoritesActive, TRUE, FALSE))
    {
        const int current = static_cast<int>(
            SendMessage(tab, TCM_GETCURSEL, 0, 0));
        if (current >= 0 && current < 3)
            g_LastNativeTab = current;
        // Pick up favourites another open editor saved, and retry every
        // package: switching to Favorites is the way to ask again.
        TB_LoadFavorites();
        g_AttemptedPackages.clear();
        TB_RefreshResolvedFavorites();
        TB_LoadMissingFavoritePackages();
    }

    // Ask the native property sheet to show its Recent page, then select the
    // fourth tab visually without notifying it about the unsupported index.
    SendMessage(tab, TCM_SETCURSEL, 2, 0);
    SendMessage(tab, TCM_SETCURFOCUS, 2, 0);
    TB_SendNativeTabChanged(tab);
    SendMessage(tab, TCM_SETCURSEL, 3, 0);
    SetFocus(tab);
    TB_UpdateFavoritesView();
}

// Favorites gives the Recent page back; the caller shows the page it wants.
static void TB_DeactivateFavorites()
{
    InterlockedExchange(&g_FavoritesActive, FALSE);
    TB_ApplyFavoritesLayout();
}

static void TB_LeaveFavoritesForRecent(HWND tab)
{
    TB_DeactivateFavorites();
    g_LastNativeTab = 2;
    SendMessage(tab, TCM_SETCURSEL, 2, 0);
    SendMessage(tab, TCM_SETCURFOCUS, 2, 0);
    TB_RedrawFavoritesPage();
}

static LRESULT CALLBACK TB_TabSubclassProc(HWND tab, UINT message,
                                           WPARAM wParam, LPARAM lParam,
                                           UINT_PTR, DWORD_PTR)
{
    switch (message)
    {
    case TCM_GETCURSEL:
    case TCM_GETCURFOCUS:
        // The native WPropertySheet owns only three pages.  Keep its queries
        // on the Recent page while the common control still paints tab 3.
        if (InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE))
            return 2;
        break;

    case WM_LBUTTONDOWN:
    {
        TCHITTESTINFO hit = {};
        hit.pt.x = GET_X_LPARAM(lParam);
        hit.pt.y = GET_Y_LPARAM(lParam);
        const int index = static_cast<int>(
            SendMessage(tab, TCM_HITTEST, 0, reinterpret_cast<LPARAM>(&hit)));
        if (index == 3)
        {
            TB_ActivateFavorites(tab);
            return 0;
        }
        if (index >= 0 && index < 3)
        {
            if (InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE))
            {
                TB_DeactivateFavorites();
                SendMessage(tab, TCM_SETCURSEL, 2, 0);
                SendMessage(tab, TCM_SETCURFOCUS, 2, 0);
                TB_RedrawFavoritesPage();
            }
            g_LastNativeTab = index;
        }
        break;
    }

    case WM_KEYDOWN:
        if (!InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE) &&
            g_LastNativeTab == 2 &&
            (wParam == VK_RIGHT || wParam == VK_DOWN || wParam == VK_END))
        {
            TB_ActivateFavorites(tab);
            return 0;
        }
        if (InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE))
        {
            if (wParam == VK_LEFT || wParam == VK_UP)
            {
                TB_LeaveFavoritesForRecent(tab);
                return 0;
            }
            if (wParam == VK_RIGHT || wParam == VK_DOWN || wParam == VK_END)
                return 0;
        }
        break;

    case WM_KEYUP:
        if (!InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE))
        {
            const int current = static_cast<int>(
                SendMessage(tab, TCM_GETCURSEL, 0, 0));
            if (current >= 0 && current < 3)
                g_LastNativeTab = current;
        }
        break;

    case WM_COMMAND:
        if (HIWORD(wParam) == CBN_SELCHANGE &&
            (LOWORD(wParam) == IDC_TB_FAVORITES_PACKAGE || LOWORD(wParam) == IDC_TB_FAVORITES_SORT))
        {
            const int choice = static_cast<int>(SendMessage(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0));
            if (choice >= 0)
            {
                if (LOWORD(wParam) == IDC_TB_FAVORITES_PACKAGE)
                    g_PackageFilter = TB_PackageAt(choice);
                else if (static_cast<size_t>(choice) < std::size(Favorites::kSorts))
                    g_Sort = Favorites::kSorts[choice];
                TB_SaveView();
                TB_UpdateFavoritesView();
            }
            return 0;
        }
        if (LOWORD(wParam) == IDC_TB_FAVORITES_NOT_LOADED)
            return 0;
        if (LOWORD(wParam) == FavoritesWindow::kOpen)
        {
            TB_OpenFavoritesWindow();
            return 0;
        }
        break;

    case WM_CONTEXTMENU:
        if (g_NotLoadedList && reinterpret_cast<HWND>(wParam) == g_NotLoadedList)
        {
            TB_ShowNotLoadedMenu(g_NotLoadedList, lParam);
            return 0;
        }
        break;

    case WM_TB_REFRESH_FAVORITES:
        TB_RefreshResolvedFavorites();
        TB_UpdateFavoritesView();
        return 0;

    case WM_TB_LAYOUT_FAVORITES:
        if (InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE))
            TB_ApplyFavoritesLayout();
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_TB_FAVORITES_REFRESH)
        {
            // Ticks since the object count last changed.
            static int steadyTicks = 0;
            if (InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE))
            {
                void** objects = nullptr;
                INT objectCount = 0;
                if (TB_ReadGObjects(&objects, &objectCount) &&
                    objectCount != g_LastGObjectsCount)
                {
                    steadyTicks = 0;
                    TB_RefreshResolvedFavorites();
                    TB_UpdateFavoritesView();
                }
                else if (++steadyTicks == 2)
                {
                    // Opening another map unloads the packages it does not
                    // use. Load the favourites' packages back once nothing
                    // has loaded for two ticks and no modal dialog has the
                    // editor, so this never lands in the middle of a map load.
                    HWND top = GetAncestor(tab, GA_ROOTOWNER);
                    if (!top || IsWindowEnabled(top))
                    {
                        const size_t before = g_LastResolvedCount;
                        TB_LoadMissingFavoritePackages();
                        if (g_LastResolvedCount != before)
                            TB_UpdateFavoritesView();
                    }
                }
            }
            return 0;
        }
        break;

    case WM_NCDESTROY:
        if (tab == g_TextureTab)
        {
            KillTimer(tab, TIMER_TB_FAVORITES_REFRESH);
            g_TextureTab = nullptr;
            g_MruPage = nullptr;
            g_PackageFilterCombo = nullptr;
            g_SortCombo = nullptr;
            g_NotLoadedList = nullptr;
            g_WindowButton = nullptr;
            g_TileFavorites.clear();
            g_NotLoadedRows.clear();
            g_TopStrip = 0;
            g_BottomStrip = 0;
            InterlockedExchange(&g_FavoritesActive, FALSE);
        }
        RemoveWindowSubclass(tab, TB_TabSubclassProc, 1);
        break;
    }

    return DefSubclassProc(tab, message, wParam, lParam);
}

static LRESULT CALLBACK TB_BrowserSubclassProc(HWND window, UINT message,
                                               WPARAM wParam, LPARAM lParam,
                                               UINT_PTR, DWORD_PTR)
{
    if (message == WM_COMMAND && LOWORD(wParam) == WorkflowTools::kFindMaterial)
    {
        WorkflowTools::FindUsages(window, TB_GetCurrentMaterial(), false);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == MapUsagesWindow::kFindMaterial)
    {
        WorkflowTools::FindUsagesInAllMaps(window, false);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDMN_TB_TOGGLE_FAVORITE)
    {
        TB_ToggleCurrentFavorite();
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == FavoritesWindow::kOpen)
    {
        TB_OpenFavoritesWindow();
        return 0;
    }

    if (message == WM_NOTIFY && g_TextureTab)
    {
        const NMHDR* notification = reinterpret_cast<const NMHDR*>(lParam);
        if (notification && notification->hwndFrom == g_TextureTab &&
            notification->code == TCN_SELCHANGE &&
            !InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE))
        {
            const int selected = static_cast<int>(
                SendMessage(g_TextureTab, TCM_GETCURSEL, 0, 0));
            if (selected == 3)
            {
                TB_ActivateFavorites(g_TextureTab);
                return 0;
            }
            if (selected >= 0 && selected < 3)
                g_LastNativeTab = selected;
        }
    }

    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, TB_BrowserSubclassProc, 2);
    return DefSubclassProc(window, message, wParam, lParam);
}

static bool TB_GetTabText(HWND tab, int index, char* text, size_t textSize)
{
    if (!text || textSize == 0)
        return false;
    text[0] = '\0';
    TCITEMA item = {};
    item.mask = TCIF_TEXT;
    item.pszText = text;
    item.cchTextMax = static_cast<int>(textSize);
    return SendMessageA(tab, TCM_GETITEMA, index,
                        reinterpret_cast<LPARAM>(&item)) != FALSE;
}

static bool TB_IsTabControlClass(const char* className)
{
    return className &&
        (_stricmp(className, WC_TABCONTROLA) == 0 ||
         _stricmp(className, "SplinterCell2UnrealWTabControl") == 0);
}

static bool TB_IsTextureBrowserTab(HWND window)
{
    char className[64] = {};
    if (!GetClassNameA(window, className, static_cast<int>(std::size(className))) ||
        !TB_IsTabControlClass(className) ||
        SendMessage(window, TCM_GETITEMCOUNT, 0, 0) != 3)
        return false;

    char full[32] = {};
    char used[32] = {};
    char recent[32] = {};
    return TB_GetTabText(window, 0, full, std::size(full)) &&
           TB_GetTabText(window, 1, used, std::size(used)) &&
           TB_GetTabText(window, 2, recent, std::size(recent)) &&
           _stricmp(full, "Full") == 0 &&
           _stricmp(used, "In Use") == 0 &&
           _stricmp(recent, "Recent") == 0;
}

static void TB_AttachFavoritesTab(HWND tab)
{
    TCITEMA item = {};
    char caption[] = "Favorites";
    item.mask = TCIF_TEXT;
    item.pszText = caption;
    if (SendMessageA(tab, TCM_INSERTITEMA, 3,
                     reinterpret_cast<LPARAM>(&item)) == -1)
        return;

    g_TextureTab = tab;
    g_LastNativeTab = static_cast<int>(SendMessage(tab, TCM_GETCURSEL, 0, 0));
    if (g_LastNativeTab < 0 || g_LastNativeTab > 2)
        g_LastNativeTab = 0;

    SetWindowSubclass(tab, TB_TabSubclassProc, 1, 0);
    for (HWND parent = GetParent(tab); parent; parent = GetParent(parent))
        SetWindowSubclass(parent, TB_BrowserSubclassProc, 2, 0);
    SetTimer(tab, TIMER_TB_FAVORITES_REFRESH, 1000, nullptr);

    Logger::log("TextureBrowser: Favorites tab attached");
}

static LRESULT CALLBACK TB_TabDiscoverySubclassProc(HWND tab, UINT message,
                                                    WPARAM wParam, LPARAM lParam,
                                                    UINT_PTR, DWORD_PTR)
{
    const LRESULT result = DefSubclassProc(tab, message, wParam, lParam);

    if ((message == TCM_INSERTITEMA || message == TCM_INSERTITEMW ||
         message == TCM_SETITEMA || message == TCM_SETITEMW) &&
        !g_TextureTab && TB_IsTextureBrowserTab(tab))
    {
        RemoveWindowSubclass(tab, TB_TabDiscoverySubclassProc, 3);
        TB_AttachFavoritesTab(tab);
    }
    else if (message == WM_NCDESTROY)
        RemoveWindowSubclass(tab, TB_TabDiscoverySubclassProc, 3);

    return result;
}

static HWND WINAPI TB_CreateWindowExA_Hook(DWORD exStyle, LPCSTR className,
                                           LPCSTR windowName, DWORD style,
                                           int x, int y, int width, int height,
                                           HWND parent, HMENU menu,
                                           HINSTANCE instance, LPVOID parameter)
{
    HWND window = g_PreviousCreateWindowExA
        ? g_PreviousCreateWindowExA(exStyle, className, windowName, style,
                                    x, y, width, height, parent, menu,
                                    instance, parameter)
        : CreateWindowExA(exStyle, className, windowName, style,
                          x, y, width, height, parent, menu,
                          instance, parameter);
    if (!window)
        return window;

    char actualClassName[64] = {};
    if (GetClassNameA(window, actualClassName,
                      static_cast<int>(std::size(actualClassName))) &&
        TB_IsTabControlClass(actualClassName))
        SetWindowSubclass(window, TB_TabDiscoverySubclassProc, 3, 0);
    return window;
}

void TextureBrowser::FavoritesChanged()
{
    TB_LoadFavorites();
    TB_RefreshResolvedFavorites();
    TB_UpdateFavoritesView();
}

bool TextureBrowser::ShowFavorite(const std::string& path)
{
    if (!g_TextureTab || !IsWindow(g_TextureTab))
        return false;
    // A package filter that would hide it gives way.
    if (!g_PackageFilter.empty() &&
        TB_ToLower(Favorites::Split(path).package) != TB_ToLower(g_PackageFilter))
    {
        g_PackageFilter.clear();
        TB_SaveView();
    }
    TB_ActivateFavorites(g_TextureTab);
    TB_LoadFavorites();
    TB_RefreshResolvedFavorites();
    TB_UpdateFavoritesView();
    TB_RedrawFavoritesPage();
    return true;
}

void TextureBrowser::Redraw()
{
    TB_RedrawFavoritesPage();
}

// DDS file format constants
#define DDS_MAGIC              0x20534444u   // "DDS "
#define DDSD_CAPS              0x00000001u
#define DDSD_HEIGHT            0x00000002u
#define DDSD_WIDTH             0x00000004u
#define DDSD_PIXELFORMAT       0x00001000u
#define DDSD_MIPMAPCOUNT       0x00020000u
#define DDSD_LINEARSIZE        0x00080000u
#define DDPF_FOURCC            0x00000004u
#define DDSCAPS_COMPLEX        0x00000008u
#define DDSCAPS_TEXTURE        0x00001000u
#define DDSCAPS_MIPMAP         0x00400000u

#pragma pack(push, 4)
struct DDPIXELFORMAT_t {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    DWORD dwRGBBitCount;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwRGBAlphaBitMask;
};
struct DDSCAPS2_t {
    DWORD dwCaps;
    DWORD dwCaps2;
    DWORD dwCaps3;
    DWORD dwCaps4;
};
struct DDSURFACEDESC2_t {
    DWORD            dwSize;
    DWORD            dwFlags;
    DWORD            dwHeight;
    DWORD            dwWidth;
    DWORD            dwPitchOrLinearSize;
    DWORD            dwDepth;
    DWORD            dwMipMapCount;
    DWORD            dwReserved1[11];
    DDPIXELFORMAT_t  ddpfPixelFormat;
    DDSCAPS2_t       ddsCaps;
    DWORD            dwReserved2;
};
struct FDDSFileHeader_t {
    DWORD            Magic;
    DDSURFACEDESC2_t desc;
};
#pragma pack(pop)

static_assert(sizeof(DDPIXELFORMAT_t)  == 32,  "DDPIXELFORMAT must be 32 bytes");
static_assert(sizeof(DDSURFACEDESC2_t) == 124, "DDSURFACEDESC2 must be 124 bytes");
static_assert(sizeof(FDDSFileHeader_t) == 128, "DDS file header must be 128 bytes");

// DDSD_LINEARSIZE for DXT textures. Surfaces below 4x4 still occupy one block.
static DWORD DXT_LinearSize(BYTE format, DWORD width, DWORD height)
{
    DWORD w = (width  < 4) ? 4 : width;
    DWORD h = (height < 4) ? 4 : height;
    DWORD blockBytes = (format == TEXF_DXT1) ? 8 : 16;
    return (w / 4) * (h / 4) * blockBytes;
}

static void GetTextureObjectName(void* pTexture, char* outBuf, size_t bufSize)
{
    if (!pTexture || !outBuf || bufSize == 0) return;
    outBuf[0] = '\0';

    void* namesArray = *reinterpret_cast<void**>(GNAMES_DATA_PTR);
    INT   namesCount = *reinterpret_cast<INT*>  (GNAMES_NUM_PTR);
    if (!namesArray || namesCount <= 0) return;

    INT nameIndex = *reinterpret_cast<INT*>(static_cast<char*>(pTexture) + UTEX_NAME_OFFSET);
    if (nameIndex < 0 || nameIndex >= namesCount) return;

    void* fnameEntry = reinterpret_cast<void**>(namesArray)[nameIndex];
    if (!fnameEntry) return;

    const char* nameStr = reinterpret_cast<const char*>(
        static_cast<char*>(fnameEntry) + FNAME_ENTRY_STR_OFFSET);
    strncpy_s(outBuf, bufSize, nameStr, _TRUNCATE);
}

static BOOL WriteDDSFromUTexture(void* pTexture, const char* path)
{
    if (!pTexture || !path) return FALSE;

    BYTE  format   = *reinterpret_cast<BYTE*> (static_cast<char*>(pTexture) + UTEX_FORMAT_OFFSET);
    BYTE  ubits    = *reinterpret_cast<BYTE*> (static_cast<char*>(pTexture) + UTEX_UBITS_OFFSET);
    BYTE  vbits    = *reinterpret_cast<BYTE*> (static_cast<char*>(pTexture) + UTEX_VBITS_OFFSET);
    void* mipsData = *reinterpret_cast<void**>(static_cast<char*>(pTexture) + UTEX_MIPS_DATA);
    INT   mipsNum  = *reinterpret_cast<INT*>  (static_cast<char*>(pTexture) + UTEX_MIPS_NUM);

    if (format != TEXF_DXT1 && format != TEXF_DXT3 && format != TEXF_DXT5) return FALSE;
    if (ubits > 13 || vbits > 13)                                          return FALSE;
    if (!mipsData || mipsNum <= 0)                                         return FALSE;

    INT usize = 1 << ubits;
    INT vsize = 1 << vbits;

    FDDSFileHeader_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.Magic                            = DDS_MAGIC;
    hdr.desc.dwSize                      = sizeof(DDSURFACEDESC2_t);
    hdr.desc.dwFlags                     = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH
                                         | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT | DDSD_LINEARSIZE;
    hdr.desc.dwWidth                     = static_cast<DWORD>(usize);
    hdr.desc.dwHeight                    = static_cast<DWORD>(vsize);
    hdr.desc.dwMipMapCount               = static_cast<DWORD>(mipsNum);
    hdr.desc.dwPitchOrLinearSize         = DXT_LinearSize(format,
                                              static_cast<DWORD>(usize),
                                              static_cast<DWORD>(vsize));
    hdr.desc.ddpfPixelFormat.dwSize      = sizeof(DDPIXELFORMAT_t);
    hdr.desc.ddpfPixelFormat.dwFlags     = DDPF_FOURCC;
    hdr.desc.ddpfPixelFormat.dwFourCC    =
        (format == TEXF_DXT1) ? ('D' | ('X' << 8) | ('T' << 16) | ('1' << 24)) :
        (format == TEXF_DXT3) ? ('D' | ('X' << 8) | ('T' << 16) | ('3' << 24)) :
                                ('D' | ('X' << 8) | ('T' << 16) | ('5' << 24));
    hdr.desc.ddsCaps.dwCaps              = DDSCAPS_TEXTURE | DDSCAPS_MIPMAP | DDSCAPS_COMPLEX;

    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    DWORD written = 0;
    if (!WriteFile(hFile, &hdr, sizeof(hdr), &written, nullptr) || written != sizeof(hdr))
    {
        CloseHandle(hFile);
        DeleteFileA(path);
        return FALSE;
    }

    // Ensure mip data is resident before writing.
    FArrayLoadFn TLazyArray_Load = reinterpret_cast<FArrayLoadFn>(
        static_cast<uintptr_t>(TLAZYARRAY_LOAD_ADDR));

    char* mipCursor = static_cast<char*>(mipsData);
    for (INT m = 0; m < mipsNum; ++m)
    {
        TLazyArray_Load(mipCursor + MIP_LAZYLOADER_OFFSET);

        void* dataPtr = *reinterpret_cast<void**>(mipCursor + MIP_DATAARRAY_DATA);
        INT   dataNum = *reinterpret_cast<INT*>  (mipCursor + MIP_DATAARRAY_NUM);
        if (!dataPtr || dataNum <= 0)
        {
            CloseHandle(hFile);
            DeleteFileA(path);
            return FALSE;
        }
        if (!WriteFile(hFile, dataPtr, static_cast<DWORD>(dataNum), &written, nullptr) ||
            written != static_cast<DWORD>(dataNum))
        {
            CloseHandle(hFile);
            DeleteFileA(path);
            return FALSE;
        }
        mipCursor += MIP_STRIDE;
    }

    CloseHandle(hFile);
    return TRUE;
}

static int __cdecl TB_RunDDSExport(void* pTexture, HWND hParent)
{
    if (!pTexture) return 0;

    BYTE fmt = *reinterpret_cast<BYTE*>(static_cast<char*>(pTexture) + UTEX_FORMAT_OFFSET);
    if (fmt != TEXF_DXT1 && fmt != TEXF_DXT3 && fmt != TEXF_DXT5)
        return 0;

    char defaultName[256] = {};
    GetTextureObjectName(pTexture, defaultName, sizeof(defaultName));
    if (defaultName[0] == '\0')
        strncpy_s(defaultName, "texture", _TRUNCATE);

    char filePath[MAX_PATH * 2] = {};
    snprintf(filePath, sizeof(filePath), "%s.dds", defaultName);

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hParent;
    ofn.lpstrFilter = "DirectDraw Surface (*.dds)\0*.dds\0All Files (*.*)\0*.*\0";
    ofn.lpstrDefExt = "dds";
    ofn.lpstrFile   = filePath;
    ofn.nMaxFile    = sizeof(filePath);
    ofn.lpstrTitle  = "Export Texture";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;

    if (!GetSaveFileNameA(&ofn))
        return 1;

    if (!WriteDDSFromUTexture(pTexture, filePath))
    {
        char msg[MAX_PATH + 128];
        snprintf(msg, sizeof(msg),
                 "Failed to export DDS to:\n%s\n\n"
                 "Texture mip data may not be loaded.", filePath);
        MessageBoxA(hParent, msg, "Export Texture", MB_OK | MB_ICONERROR);
    }
    return 1;
}

JMP_HOOK(HOOK_EXPORT_DISPATCH, TB_ExportDispatchHook)
{
    static int resumeNative = static_cast<int>(RESUME_NATIVE_OFN);
    static int skipToEpilog = static_cast<int>(SKIP_TO_CASE_EPILOGUE);

    __asm {
        mov  al, byte ptr [esi + UTEX_FORMAT_OFFSET]
        cmp  al, TEXF_DXT1
        je   handle_dds
        cmp  al, TEXF_DXT3
        je   handle_dds
        cmp  al, TEXF_DXT5
        je   handle_dds

        // Resume native export path.
        lea  eax, [ebp + 0xffffff18]
        jmp  dword ptr [resumeNative]

    handle_dds:
        pushad
        pushfd

        push dword ptr [ebp + 0xffffff1c]  // HWND
        push esi                           // UTexture*
        call TB_RunDDSExport
        add  esp, 8
        popfd
        popad
        jmp  dword ptr [skipToEpilog]
    }
}

// The browser's refresh (0x10E81400) reloads each listed package with
// UObject::LoadPackage, and a map with its own textures lists MyLevel, which
// has no file. That load throws past its BeginLoad, so check(GObjBeginLoadCount
// ==0) fires on the next tick: every refresh of such a map, including the one
// sent while the map loads. MyLevel is always in memory with its map; skip it.
#define TB_REFRESH_LOAD_PACKAGE_CALL 0x10E81ABEu
#define UOBJECT_LOAD_PACKAGE         0x10FB1EB0u
typedef void* (__cdecl* LoadPackageFn)(void* outer, const char* name, DWORD flags);

static void* __cdecl TB_RefreshLoadPackage(void* outer, const char* name, DWORD flags)
{
    if (name && _stricmp(name, "MyLevel") == 0)
        return nullptr;
    return reinterpret_cast<LoadPackageFn>(UOBJECT_LOAD_PACKAGE)(outer, name, flags);
}

static void TB_InstallRefreshLoadGuard()
{
    const intptr_t displacement = static_cast<intptr_t>(UOBJECT_LOAD_PACKAGE)
        - static_cast<intptr_t>(TB_REFRESH_LOAD_PACKAGE_CALL + 5);
    BYTE expected[5] = { 0xE8 };
    memcpy(expected + 1, &displacement, sizeof(INT));
    if (memcmp(reinterpret_cast<const void*>(TB_REFRESH_LOAD_PACKAGE_CALL), expected, sizeof(expected)) != 0 ||
        !MemoryWriter::WriteCall(TB_REFRESH_LOAD_PACKAGE_CALL, reinterpret_cast<void (*)()>(TB_RefreshLoadPackage)))
    {
        Logger::log("TextureBrowser: MyLevel refresh guard not installed (fingerprint mismatch)");
    }
}

void TextureBrowser::Initialize()
{
    TB_LoadFavorites();
    TB_LoadView();
    INSTALL_HOOKS;
    TB_InstallRefreshLoadGuard();

    static const BYTE expectedMruListLoad[] = { 0x8B, 0x0D, 0xEC, 0xDF, 0x65, 0x11 };
    if (memcmp(reinterpret_cast<const void*>(HOOK_MRU_LIST_BUILD),
               expectedMruListLoad, sizeof(expectedMruListLoad)) == 0)
    {
        MemoryWriter::WriteJump(HOOK_MRU_LIST_BUILD, TB_FavoritesRenderHook);
    }
    else
    {
        Logger::log("TextureBrowser: Favorites renderer hook fingerprint mismatch");
    }

    // SoundBrowser installs its LoadMenuA wrapper first.  Chain it so both
    // features can extend their own editor menus.
    g_PreviousLoadMenuA = *reinterpret_cast<LoadMenuAFn*>(LOADMENUA_IAT_SLOT);
    uintptr_t loadMenuHook = reinterpret_cast<uintptr_t>(TB_LoadMenuA_Hook);
    MemoryWriter::WriteBytes(LOADMENUA_IAT_SLOT, &loadMenuHook,
                             sizeof(loadMenuHook));

    // Texture Browser windows can be created, closed and recreated at runtime.
    // Observe native tab creation through the editor's CreateWindowExA import.
    // The wrapper runs on the creating UI thread, as SetWindowSubclass requires.
    g_PreviousCreateWindowExA =
        *reinterpret_cast<CreateWindowExAFn*>(CREATEWINDOWEXA_IAT_SLOT);
    uintptr_t createWindowHook =
        reinterpret_cast<uintptr_t>(TB_CreateWindowExA_Hook);
    MemoryWriter::WriteBytes(CREATEWINDOWEXA_IAT_SLOT, &createWindowHook,
                             sizeof(createWindowHook));
}
