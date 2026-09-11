#include "pch.h"
#include "StaticMeshBrowserFavorites.h"
#include "MemoryWriter.h"
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// WBrowserStaticMesh and engine layout for the supported SCCT editor build.
#define SM_BROWSER_GLOBAL_PTR          0x1165E834u
#define GEDITOR_PTR                    0x1165DFA0u
#define EXEC_LOG_DEV                   0x115BEFB0u
#define GOBJECTS_DATA_PTR              0x11697B70u
#define GOBJECTS_NUM_PTR               0x11697B74u
#define GNAMES_DATA_PTR                0x1169CFBCu
#define GNAMES_NUM_PTR                 0x1169CFC0u
#define USTATICMESH_CLASS              0x11820B68u

#define SM_NATIVE_REFRESH_LIST         0x10E86A40u
#define SM_NATIVE_REFRESH_LIST_RESUME  0x10E86A45u
#define CREATEWINDOWEXA_IAT_SLOT       0x11AF23E0u
#define LOADMENUA_IAT_SLOT             0x11AF23F0u

#define UOBJECT_OUTER_OFFSET           0x18
#define UOBJECT_FNAME_OFFSET           0x20
#define UOBJECT_CLASS_OFFSET           0x24
#define UCLASS_SUPER_OFFSET            0x28
#define FNAME_ENTRY_STR_OFFSET         0x0C

#define GEDITOR_CURRENT_STATIC_MESH    0x13C
#define SM_BROWSER_PACKAGE_COMBO       0x90
#define SM_BROWSER_GROUP_COMBO         0x94
#define SM_BROWSER_GROUP_ALL           0x98
#define SM_BROWSER_MESH_LIST           0x9C
#define SM_BROWSER_PROPERTIES          0xA0
#define SM_BROWSER_VIEWPORT            0xC8

#define STATIC_MESH_CONTEXT_MENU_ID    15159
#define IDMN_SM_TOGGLE_FAVORITE        40909
#define IDC_SM_FAVORITES_FILTER        40910
#define WM_SM_ATTACH_FAVORITES         (WM_APP + 0x5A)
#define TIMER_SM_FAVORITES_REFRESH     0x5AF1

static constexpr const char* kFavoritesIniSection =
    "StaticMeshBrowserFavorites";
static constexpr int kMaxFavorites = 4096;

struct SM_ResolvedFavorite
{
    std::string Path;
    void* Object;
};

static std::vector<std::string> g_FavoritePaths;
static std::vector<SM_ResolvedFavorite> g_ResolvedFavorites;
static std::string g_IniPath;
static volatile LONG g_FavoritesActive = FALSE;
static bool g_FeatureSupported = false;
static int g_LastGObjectsCount = -1;

static void* g_BrowserObject = nullptr;
static HWND g_BrowserWindow = nullptr;
static HWND g_PackageCombo = nullptr;
static HWND g_GroupCombo = nullptr;
static HWND g_GroupAll = nullptr;
static HWND g_MeshList = nullptr;
static HWND g_MeshListParent = nullptr;
static HWND g_FilterButton = nullptr;
static bool g_PackageWasEnabled = true;
static bool g_GroupWasEnabled = true;
static bool g_GroupAllWasEnabled = true;
static int g_NormalColumnWidth = -1;
static bool g_ControlsAttached = false;

typedef HWND(WINAPI* CreateWindowExAFn)(DWORD, LPCSTR, LPCSTR, DWORD,
                                        int, int, int, int, HWND, HMENU,
                                        HINSTANCE, LPVOID);
typedef HMENU(WINAPI* LoadMenuAFn)(HINSTANCE, LPCSTR);
typedef void(__thiscall* NativeRefreshListFn)(void*);
typedef void(__thiscall* PropertySetObjectFn)(void*, void*, INT);
typedef void(__thiscall* ViewportRefreshFn)(void*, INT);

static CreateWindowExAFn g_PreviousCreateWindowExA = nullptr;
static LoadMenuAFn g_PreviousLoadMenuA = nullptr;

static LRESULT CALLBACK SM_BrowserSubclassProc(HWND, UINT, WPARAM, LPARAM,
                                                UINT_PTR, DWORD_PTR);
static LRESULT CALLBACK SM_ListParentSubclassProc(HWND, UINT, WPARAM, LPARAM,
                                                   UINT_PTR, DWORD_PTR);
static LRESULT CALLBACK SM_CommandSubclassProc(HWND, UINT, WPARAM, LPARAM,
                                                 UINT_PTR, DWORD_PTR);
static void SM_SaveFavorites();

static std::string SM_GetIniPath()
{
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash)
        *(lastSlash + 1) = '\0';
    return std::string(exePath) + "Reloaded_Editor.ini";
}

static std::string SM_ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static std::string SM_NormalizeLegacyPath(const std::string& path)
{
    // The first implementation read UObject+0x24 as an FName number. In this
    // editor that field is actually the class pointer, so old paths contain
    // address-like suffixes such as "_292127879". Strip only values that map
    // back into this executable's fixed image range.
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

// Fault-prone engine reads stay in leaf functions without C++ objects so SEH
// can protect the browser from stale UObject pointers.
static bool SM_ReadObjectIdentity(void* object, void** outer,
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

static bool SM_IsStaticMesh(void* object)
{
    if (!object)
        return false;

    __try
    {
        void* objectClass = *reinterpret_cast<void**>(
            static_cast<char*>(object) + UOBJECT_CLASS_OFFSET);
        for (int depth = 0; objectClass && depth < 64; ++depth)
        {
            if (objectClass == reinterpret_cast<void*>(USTATICMESH_CLASS))
                return true;
            objectClass = *reinterpret_cast<void**>(
                static_cast<char*>(objectClass) + UCLASS_SUPER_OFFSET);
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool SM_BuildObjectPath(void* object, std::string& path)
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
        if (!SM_ReadObjectIdentity(cursor, &outer, parts[partCount].Name,
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

static bool SM_ReadGObjects(void*** data, INT* count)
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

static bool SM_ReadGObjectAt(void** data, INT index, void** object)
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

static void* SM_GetCurrentMesh()
{
    __try
    {
        void* editor = *reinterpret_cast<void**>(GEDITOR_PTR);
        return editor ? *reinterpret_cast<void**>(
            static_cast<char*>(editor) + GEDITOR_CURRENT_STATIC_MESH) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

static bool SM_FindFavorite(const std::string& path, size_t* index = nullptr)
{
    const std::string wanted = SM_ToLower(path);
    for (size_t i = 0; i < g_FavoritePaths.size(); ++i)
    {
        if (SM_ToLower(g_FavoritePaths[i]) == wanted)
        {
            if (index)
                *index = i;
            return true;
        }
    }
    return false;
}

static void SM_LoadFavorites()
{
    g_IniPath = SM_GetIniPath();
    g_FavoritePaths.clear();
    bool migrated = false;
    int count = GetPrivateProfileIntA(kFavoritesIniSection, "Count", 0,
                                      g_IniPath.c_str());
    count = (std::max)(0, (std::min)(count, kMaxFavorites));
    for (int i = 0; i < count; ++i)
    {
        char key[32] = {};
        char value[1024] = {};
        snprintf(key, sizeof(key), "Favorite%d", i);
        GetPrivateProfileStringA(kFavoritesIniSection, key, "", value,
                                 static_cast<DWORD>(std::size(value)),
                                 g_IniPath.c_str());
        if (value[0])
        {
            const std::string normalized = SM_NormalizeLegacyPath(value);
            migrated = migrated || normalized != value;
            if (!SM_FindFavorite(normalized))
                g_FavoritePaths.push_back(normalized);
        }
    }
    if (migrated)
        SM_SaveFavorites();
}

static void SM_SaveFavorites()
{
    if (g_IniPath.empty())
        g_IniPath = SM_GetIniPath();
    WritePrivateProfileStringA(kFavoritesIniSection, nullptr, nullptr,
                               g_IniPath.c_str());

    char count[32] = {};
    snprintf(count, sizeof(count), "%zu", g_FavoritePaths.size());
    WritePrivateProfileStringA(kFavoritesIniSection, "Count", count,
                               g_IniPath.c_str());
    for (size_t i = 0; i < g_FavoritePaths.size(); ++i)
    {
        char key[32] = {};
        snprintf(key, sizeof(key), "Favorite%zu", i);
        WritePrivateProfileStringA(kFavoritesIniSection, key,
                                   g_FavoritePaths[i].c_str(),
                                   g_IniPath.c_str());
    }
}

static void SM_RefreshResolvedFavorites()
{
    g_ResolvedFavorites.clear();
    void** objects = nullptr;
    INT objectCount = 0;
    if (!SM_ReadGObjects(&objects, &objectCount))
        return;

    g_LastGObjectsCount = objectCount;
    if (g_FavoritePaths.empty())
        return;

    std::unordered_map<std::string, size_t> wanted;
    wanted.reserve(g_FavoritePaths.size());
    for (size_t i = 0; i < g_FavoritePaths.size(); ++i)
        wanted.emplace(SM_ToLower(g_FavoritePaths[i]), i);

    std::vector<void*> resolved(g_FavoritePaths.size(), nullptr);
    size_t remaining = wanted.size();
    for (INT i = 0; i < objectCount && remaining > 0; ++i)
    {
        void* object = nullptr;
        if (!SM_ReadGObjectAt(objects, i, &object) ||
            !SM_IsStaticMesh(object))
            continue;

        std::string path;
        if (!SM_BuildObjectPath(object, path))
            continue;
        const auto found = wanted.find(SM_ToLower(path));
        if (found != wanted.end() && !resolved[found->second])
        {
            resolved[found->second] = object;
            --remaining;
        }
    }

    g_ResolvedFavorites.reserve(resolved.size());
    for (size_t i = 0; i < resolved.size(); ++i)
    {
        if (resolved[i])
            g_ResolvedFavorites.push_back({ g_FavoritePaths[i], resolved[i] });
    }
}

static bool __cdecl SM_ExecEditorCommand(const char* command)
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

static std::string SM_GetStaticMeshDirectory()
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
        "..\\Packages\\StaticMeshes\\";
    char fullPath[MAX_PATH] = {};
    const DWORD length = GetFullPathNameA(relative.c_str(),
        static_cast<DWORD>(std::size(fullPath)), fullPath, nullptr);
    if (!length || length >= std::size(fullPath))
        return {};
    return fullPath;
}

static std::string SM_FavoritePackage(const std::string& path)
{
    const size_t dot = path.find('.');
    return dot == std::string::npos ? path : path.substr(0, dot);
}

static void SM_LoadMissingFavoritePackages()
{
    std::unordered_set<std::string> resolved;
    resolved.reserve(g_ResolvedFavorites.size());
    for (const auto& favorite : g_ResolvedFavorites)
        resolved.insert(SM_ToLower(favorite.Path));

    std::unordered_map<std::string, std::string> wantedPackages;
    for (const auto& path : g_FavoritePaths)
    {
        if (resolved.find(SM_ToLower(path)) != resolved.end())
            continue;
        const std::string package = SM_FavoritePackage(path);
        if (!package.empty())
            wantedPackages.emplace(SM_ToLower(package), package);
    }
    if (wantedPackages.empty())
        return;

    const std::string directory = SM_GetStaticMeshDirectory();
    if (directory.empty())
        return;

    std::unordered_map<std::string, std::string> packageFiles;
    WIN32_FIND_DATAA data = {};
    HANDLE search = FindFirstFileA((directory + "*.usx").c_str(), &data);
    if (search != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            std::string fileName = data.cFileName;
            const size_t dot = fileName.rfind('.');
            const std::string baseName = dot == std::string::npos
                ? fileName : fileName.substr(0, dot);
            packageFiles.emplace(SM_ToLower(baseName), directory + fileName);
        } while (FindNextFileA(search, &data));
        FindClose(search);
    }

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
        if (SM_ExecEditorCommand(command))
            ++loaded;
        else
            ++unavailable;
    }

    if (loaded > 0)
        Logger::log("StaticMeshBrowserFavorites: loaded " +
            std::to_string(loaded) + " favorite package(s)");
    if (unavailable > 0)
        Logger::log("StaticMeshBrowserFavorites: could not locate or load " +
            std::to_string(unavailable) + " favorite package(s)");

    SM_RefreshResolvedFavorites();
}

static bool SM_ReadBrowserControls(void* browser, HWND* root,
                                   HWND* packageCombo, HWND* groupCombo,
                                   HWND* groupAll, HWND* meshList)
{
    if (!browser)
        return false;
    __try
    {
        *root = *reinterpret_cast<HWND*>(
            static_cast<char*>(browser) + sizeof(void*));
        auto readControl = [browser](size_t offset) -> HWND
        {
            void* wrapper = *reinterpret_cast<void**>(
                static_cast<char*>(browser) + offset);
            return wrapper ? *reinterpret_cast<HWND*>(
                static_cast<char*>(wrapper) + sizeof(void*)) : nullptr;
        };
        *packageCombo = readControl(SM_BROWSER_PACKAGE_COMBO);
        *groupCombo = readControl(SM_BROWSER_GROUP_COMBO);
        *groupAll = readControl(SM_BROWSER_GROUP_ALL);
        *meshList = readControl(SM_BROWSER_MESH_LIST);
        return *root && *packageCombo && *groupCombo && *groupAll && *meshList;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *root = nullptr;
        *packageCombo = nullptr;
        *groupCombo = nullptr;
        *groupAll = nullptr;
        *meshList = nullptr;
        return false;
    }
}

static void* SM_ReadBrowserObject()
{
    __try
    {
        return *reinterpret_cast<void**>(SM_BROWSER_GLOBAL_PTR);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

static bool SM_TryUpdateNativePreview(void* mesh)
{
    // This mirrors the tail of WBrowserStaticMesh's native list-selection
    // handler: update GUnrealEd::CurrentStaticMesh, refresh its property view,
    // then redraw the existing static-mesh viewport.
    __try
    {
        void* editor = *reinterpret_cast<void**>(GEDITOR_PTR);
        void* browser = g_BrowserObject;
        if (!editor || !browser)
            return false;

        void* currentSlot = static_cast<char*>(editor) +
            GEDITOR_CURRENT_STATIC_MESH;
        *reinterpret_cast<void**>(currentSlot) = mesh;

        void* properties = *reinterpret_cast<void**>(
            static_cast<char*>(browser) + SM_BROWSER_PROPERTIES);
        if (properties)
        {
            void* propertyTarget = static_cast<char*>(properties) + 0x104;
            void** vtable = *reinterpret_cast<void***>(propertyTarget);
            if (vtable && vtable[0x98 / sizeof(void*)])
            {
                auto refreshProperties = reinterpret_cast<PropertySetObjectFn>(
                    vtable[0x98 / sizeof(void*)]);
                refreshProperties(propertyTarget, currentSlot, TRUE);
            }
        }

        void* viewport = *reinterpret_cast<void**>(
            static_cast<char*>(browser) + SM_BROWSER_VIEWPORT);
        if (viewport)
        {
            void** vtable = *reinterpret_cast<void***>(viewport);
            if (vtable && vtable[0xC8 / sizeof(void*)])
            {
                auto refreshViewport = reinterpret_cast<ViewportRefreshFn>(
                    vtable[0xC8 / sizeof(void*)]);
                refreshViewport(viewport, TRUE);
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static void SM_UpdateNativePreview(void* mesh)
{
    if ((mesh && !SM_IsStaticMesh(mesh)) ||
        !SM_TryUpdateNativePreview(mesh))
        Logger::log("StaticMeshBrowserFavorites: native preview refresh failed");
}

static void SM_SelectFavoriteItem(int index)
{
    if (!g_MeshList || index < 0)
        return;
    LVITEMA item = {};
    item.mask = LVIF_PARAM;
    item.iItem = index;
    if (SendMessageA(g_MeshList, LVM_GETITEMA, 0,
                     reinterpret_cast<LPARAM>(&item)) && item.lParam)
        SM_UpdateNativePreview(reinterpret_cast<void*>(item.lParam));
}

static void __cdecl SM_PopulateFavorites(void* browser)
{
    if (!InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE))
        return;

    void** objects = nullptr;
    INT objectCount = 0;
    if (SM_ReadGObjects(&objects, &objectCount) &&
        objectCount != g_LastGObjectsCount)
        SM_RefreshResolvedFavorites();

    if (!g_MeshList || !IsWindow(g_MeshList))
    {
        HWND root = nullptr;
        HWND packageCombo = nullptr;
        HWND groupCombo = nullptr;
        HWND groupAll = nullptr;
        HWND meshList = nullptr;
        if (!SM_ReadBrowserControls(browser, &root, &packageCombo,
                                    &groupCombo, &groupAll, &meshList))
            return;
        g_MeshList = meshList;
    }

    void* current = SM_GetCurrentMesh();
    SendMessage(g_MeshList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_MeshList);

    int selected = -1;
    for (size_t i = 0; i < g_ResolvedFavorites.size(); ++i)
    {
        LVITEMA item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<char*>(g_ResolvedFavorites[i].Path.c_str());
        item.lParam = reinterpret_cast<LPARAM>(g_ResolvedFavorites[i].Object);
        const int inserted = static_cast<int>(SendMessageA(
            g_MeshList, LVM_INSERTITEMA, 0,
            reinterpret_cast<LPARAM>(&item)));
        if (inserted >= 0 && g_ResolvedFavorites[i].Object == current)
            selected = inserted;
    }

    if (!g_ResolvedFavorites.empty())
        ListView_SetColumnWidth(g_MeshList, 0, LVSCW_AUTOSIZE);
    if (selected >= 0)
    {
        ListView_SetItemState(g_MeshList, selected,
                              LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g_MeshList, selected, FALSE);
    }
    SendMessage(g_MeshList, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(g_MeshList, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

// Detour WBrowserStaticMesh::RefreshStaticMeshList. Native refresh requests
// repopulate our list while the filter is active; otherwise the exact original
// prologue is replayed before resuming the editor function.
__declspec(naked) static void SM_RefreshListHook()
{
    static int resumeNative = static_cast<int>(SM_NATIVE_REFRESH_LIST_RESUME);
    __asm
    {
        cmp  dword ptr [g_FavoritesActive], 0
        je   native_refresh

        pushfd
        pushad
        push ecx
        call SM_PopulateFavorites
        add  esp, 4
        popad
        popfd
        ret

    native_refresh:
        push ebp
        mov  ebp, esp
        push 0FFFFFFFFh
        jmp  dword ptr [resumeNative]
    }
}

static void SM_PositionFilterButton()
{
    if (!g_BrowserWindow || !g_FilterButton || !g_PackageCombo ||
        !g_GroupCombo)
        return;

    RECT packageRect = {};
    RECT groupRect = {};
    if (!GetWindowRect(g_PackageCombo, &packageRect) ||
        !GetWindowRect(g_GroupCombo, &groupRect))
        return;
    MapWindowPoints(nullptr, g_BrowserWindow,
                    reinterpret_cast<POINT*>(&packageRect), 2);
    MapWindowPoints(nullptr, g_BrowserWindow,
                    reinterpret_cast<POINT*>(&groupRect), 2);

    constexpr int buttonWidth = 86;
    constexpr int gap = 4;
    const int rightEdge = static_cast<int>(groupRect.right);
    const int packageLeft = static_cast<int>(packageRect.left);
    const int buttonX = (std::max)(packageLeft + 40,
                                   rightEdge - buttonWidth);
    const int packageWidth = (std::max)(40,
        buttonX - gap - packageLeft);

    SetWindowPos(g_PackageCombo, nullptr, packageRect.left, packageRect.top,
                 packageWidth, packageRect.bottom - packageRect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_FilterButton, nullptr, buttonX, packageRect.top,
                 buttonWidth, packageRect.bottom - packageRect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

static void SM_EnableNativeFilters(bool enable)
{
    if (enable)
    {
        EnableWindow(g_PackageCombo, g_PackageWasEnabled);
        EnableWindow(g_GroupCombo, g_GroupWasEnabled);
        EnableWindow(g_GroupAll, g_GroupAllWasEnabled);
    }
    else
    {
        g_PackageWasEnabled = IsWindowEnabled(g_PackageCombo) != FALSE;
        g_GroupWasEnabled = IsWindowEnabled(g_GroupCombo) != FALSE;
        g_GroupAllWasEnabled = IsWindowEnabled(g_GroupAll) != FALSE;
        EnableWindow(g_PackageCombo, FALSE);
        EnableWindow(g_GroupCombo, FALSE);
        EnableWindow(g_GroupAll, FALSE);
    }
}

static void SM_SetFavoritesActive(bool active)
{
    if (!g_FeatureSupported || !g_BrowserObject || !g_MeshList)
        return;

    const bool wasActive =
        InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE) != FALSE;
    if (active == wasActive)
        return;

    if (active)
    {
        g_NormalColumnWidth = ListView_GetColumnWidth(g_MeshList, 0);
        SM_RefreshResolvedFavorites();
        SM_LoadMissingFavoritePackages();
        InterlockedExchange(&g_FavoritesActive, TRUE);
        SM_EnableNativeFilters(false);
        if (g_FilterButton)
            SendMessage(g_FilterButton, BM_SETCHECK, BST_CHECKED, 0);
        SM_PopulateFavorites(g_BrowserObject);
    }
    else
    {
        InterlockedExchange(&g_FavoritesActive, FALSE);
        if (g_FilterButton)
            SendMessage(g_FilterButton, BM_SETCHECK, BST_UNCHECKED, 0);
        SM_EnableNativeFilters(true);
        reinterpret_cast<NativeRefreshListFn>(SM_NATIVE_REFRESH_LIST)(
            g_BrowserObject);
        if (g_NormalColumnWidth > 0)
            ListView_SetColumnWidth(g_MeshList, 0, g_NormalColumnWidth);
    }
}

static void SM_ToggleCurrentFavorite()
{
    void* mesh = SM_GetCurrentMesh();
    std::string path;
    if (!SM_IsStaticMesh(mesh) || !SM_BuildObjectPath(mesh, path))
        return;

    size_t index = 0;
    if (SM_FindFavorite(path, &index))
        g_FavoritePaths.erase(g_FavoritePaths.begin() + index);
    else if (g_FavoritePaths.size() < kMaxFavorites)
        g_FavoritePaths.push_back(path);
    else
    {
        MessageBoxA(g_BrowserWindow,
                    "The Favorites list has reached its 4096-item limit.",
                    "Static Mesh Browser", MB_OK | MB_ICONWARNING);
        return;
    }

    SM_SaveFavorites();
    SM_RefreshResolvedFavorites();
    if (InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE))
        SM_PopulateFavorites(g_BrowserObject);
}

static int SM_MenuPosition(HMENU menu, UINT command)
{
    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i)
    {
        if (GetMenuItemID(menu, i) == command)
            return i;
    }
    return -1;
}

static HMENU WINAPI SM_LoadMenuA_Hook(HINSTANCE instance, LPCSTR menuName)
{
    HMENU menu = g_PreviousLoadMenuA
        ? g_PreviousLoadMenuA(instance, menuName)
        : LoadMenuA(instance, menuName);
    if (!menu || (reinterpret_cast<ULONG_PTR>(menuName) >> 16) != 0 ||
        LOWORD(reinterpret_cast<ULONG_PTR>(menuName)) !=
            STATIC_MESH_CONTEXT_MENU_ID)
        return menu;

    HMENU context = GetSubMenu(menu, 0);
    if (!context || SM_MenuPosition(context, IDMN_SM_TOGGLE_FAVORITE) >= 0)
        return menu;

    void* mesh = SM_GetCurrentMesh();
    std::string path;
    const bool hasMesh = SM_IsStaticMesh(mesh) &&
        SM_BuildObjectPath(mesh, path);
    const bool isFavorite = hasMesh && SM_FindFavorite(path);
    const char* label = isFavorite
        ? "Remove from &Favorites"
        : "Add to &Favorites";
    const UINT flags = MF_BYPOSITION | MF_STRING |
        (hasMesh ? MF_ENABLED : MF_GRAYED);

    // Stock positions 0-2 are Delete, Copy and Rename; keep Sections last.
    InsertMenuA(context, 3, flags, IDMN_SM_TOGGLE_FAVORITE, label);
    return menu;
}

static bool SM_HandleCommand(WPARAM wParam)
{
    if (LOWORD(wParam) == IDMN_SM_TOGGLE_FAVORITE)
    {
        SM_ToggleCurrentFavorite();
        return true;
    }
    return false;
}

static void SM_ShowContextMenu(HWND list, LPARAM mousePosition)
{
    POINT clientPoint = {
        static_cast<short>(LOWORD(mousePosition)),
        static_cast<short>(HIWORD(mousePosition))
    };

    LVHITTESTINFO hit = {};
    hit.pt = clientPoint;
    const int item = ListView_HitTest(list, &hit);
    // In the browser's thumbnail view, HitTest can identify the item while
    // reporting whitespace rather than LVHT_ONITEM for much of its tile.
    if (item < 0)
        return;

    ListView_SetItemState(list, item,
        LVIS_SELECTED | LVIS_FOCUSED,
        LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list, item, FALSE);
    SetFocus(list);

    if (InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE))
        SM_SelectFavoriteItem(item);

    HMENU menu = SM_LoadMenuA_Hook(
        GetModuleHandleA(nullptr),
        MAKEINTRESOURCEA(STATIC_MESH_CONTEXT_MENU_ID));
    if (!menu)
        return;

    HMENU context = GetSubMenu(menu, 0);
    if (context)
    {
        POINT screenPoint = clientPoint;
        ClientToScreen(list, &screenPoint);
        SetForegroundWindow(g_BrowserWindow);
        const UINT command = TrackPopupMenu(
            context, TPM_RIGHTBUTTON | TPM_RETURNCMD,
            screenPoint.x, screenPoint.y, 0, g_BrowserWindow, nullptr);
        if (command)
            SendMessage(g_BrowserWindow, WM_COMMAND,
                        MAKEWPARAM(command, 0), 0);
        PostMessage(g_BrowserWindow, WM_NULL, 0, 0);
    }
    DestroyMenu(menu);
}

static bool SM_AttachBrowserControls()
{
    if (g_ControlsAttached && g_FilterButton && IsWindow(g_FilterButton))
    {
        SM_PositionFilterButton();
        return true;
    }

    void* browser = SM_ReadBrowserObject();
    HWND root = nullptr;
    HWND packageCombo = nullptr;
    HWND groupCombo = nullptr;
    HWND groupAll = nullptr;
    HWND meshList = nullptr;
    if (!SM_ReadBrowserControls(browser, &root, &packageCombo, &groupCombo,
                                &groupAll, &meshList) ||
        root != g_BrowserWindow || !IsWindow(meshList))
        return false;

    g_BrowserObject = browser;
    g_PackageCombo = packageCombo;
    g_GroupCombo = groupCombo;
    g_GroupAll = groupAll;
    g_MeshList = meshList;
    g_MeshListParent = GetParent(meshList);

    if (!g_FilterButton || !IsWindow(g_FilterButton))
    {
        g_FilterButton = CreateWindowExA(
            0, "BUTTON", "Favorites",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                BS_AUTOCHECKBOX | BS_PUSHLIKE,
            0, 0, 86, 21, g_BrowserWindow,
            reinterpret_cast<HMENU>(IDC_SM_FAVORITES_FILTER),
            GetModuleHandleA(nullptr), nullptr);
        if (!g_FilterButton)
            return false;
        HFONT font = reinterpret_cast<HFONT>(
            SendMessage(g_PackageCombo, WM_GETFONT, 0, 0));
        if (!font)
            font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessage(g_FilterButton, WM_SETFONT,
                    reinterpret_cast<WPARAM>(font), TRUE);
    }

    if (g_MeshListParent)
        SetWindowSubclass(g_MeshListParent, SM_ListParentSubclassProc, 12, 0);
    for (HWND commandWindow = g_MeshList; commandWindow;
         commandWindow = GetParent(commandWindow))
    {
        if (commandWindow != g_BrowserWindow &&
            commandWindow != g_MeshListParent)
            SetWindowSubclass(commandWindow, SM_CommandSubclassProc, 13, 0);
    }

    SM_PositionFilterButton();
    SetTimer(g_BrowserWindow, TIMER_SM_FAVORITES_REFRESH, 1000, nullptr);
    g_ControlsAttached = true;
    Logger::log("StaticMeshBrowserFavorites: Favorites filter attached");
    return true;
}

static void SM_TryAttachToCreatedWindow(HWND window)
{
    if (!g_FeatureSupported || !window)
        return;

    void* browser = SM_ReadBrowserObject();
    HWND root = nullptr;
    HWND packageCombo = nullptr;
    HWND groupCombo = nullptr;
    HWND groupAll = nullptr;
    HWND meshList = nullptr;
    if (!SM_ReadBrowserControls(browser, &root, &packageCombo, &groupCombo,
                                &groupAll, &meshList) || !IsWindow(root) ||
        (window != root && !IsChild(root, window)))
        return;

    if (g_BrowserWindow != root)
    {
        g_BrowserWindow = root;
        if (!SetWindowSubclass(root, SM_BrowserSubclassProc, 11, 0))
        {
            g_BrowserWindow = nullptr;
            return;
        }
    }
    if (!g_ControlsAttached)
        PostMessage(root, WM_SM_ATTACH_FAVORITES, 0, 0);
}

static HWND WINAPI SM_CreateWindowExA_Hook(DWORD exStyle, LPCSTR className,
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
    SM_TryAttachToCreatedWindow(window);
    return window;
}

static LRESULT CALLBACK SM_BrowserSubclassProc(HWND window, UINT message,
                                                WPARAM wParam, LPARAM lParam,
                                                UINT_PTR, DWORD_PTR)
{
    if (message == WM_COMMAND)
    {
        if (SM_HandleCommand(wParam))
            return 0;
        if (LOWORD(wParam) == IDC_SM_FAVORITES_FILTER &&
            HIWORD(wParam) == BN_CLICKED)
        {
            const bool checked = SendMessage(g_FilterButton, BM_GETCHECK,
                                             0, 0) == BST_CHECKED;
            SM_SetFavoritesActive(checked);
            return 0;
        }
    }

    if (message == WM_SM_ATTACH_FAVORITES)
    {
        SM_AttachBrowserControls();
        return 0;
    }

    if (message == WM_TIMER && wParam == TIMER_SM_FAVORITES_REFRESH)
    {
        if (InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE))
        {
            void** objects = nullptr;
            INT objectCount = 0;
            if (SM_ReadGObjects(&objects, &objectCount) &&
                objectCount != g_LastGObjectsCount)
            {
                SM_RefreshResolvedFavorites();
                SM_PopulateFavorites(g_BrowserObject);
            }
        }
        return 0;
    }

    if (message == WM_SIZE)
    {
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        SM_PositionFilterButton();
        return result;
    }

    if (message == WM_NCDESTROY)
    {
        KillTimer(window, TIMER_SM_FAVORITES_REFRESH);
        InterlockedExchange(&g_FavoritesActive, FALSE);
        g_BrowserObject = nullptr;
        g_BrowserWindow = nullptr;
        g_PackageCombo = nullptr;
        g_GroupCombo = nullptr;
        g_GroupAll = nullptr;
        g_MeshList = nullptr;
        g_MeshListParent = nullptr;
        g_FilterButton = nullptr;
        g_ControlsAttached = false;
        RemoveWindowSubclass(window, SM_BrowserSubclassProc, 11);
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

static LRESULT CALLBACK SM_ListParentSubclassProc(HWND window, UINT message,
                                                   WPARAM wParam, LPARAM lParam,
                                                   UINT_PTR, DWORD_PTR)
{
    if (message == WM_COMMAND && SM_HandleCommand(wParam))
        return 0;

    if (message == WM_NOTIFY &&
        InterlockedCompareExchange(&g_FavoritesActive, TRUE, TRUE))
    {
        const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header && header->hwndFrom == g_MeshList)
        {
            if (header->code == LVN_ITEMCHANGED)
            {
                const NMLISTVIEW* change =
                    reinterpret_cast<const NMLISTVIEW*>(lParam);
                if ((change->uChanged & LVIF_STATE) &&
                    (change->uNewState & LVIS_SELECTED) &&
                    !(change->uOldState & LVIS_SELECTED))
                    SM_SelectFavoriteItem(change->iItem);
                return 0;
            }

            if (header->code == NM_RCLICK)
            {
                const NMITEMACTIVATE* activation =
                    reinterpret_cast<const NMITEMACTIVATE*>(lParam);
                if (activation->iItem >= 0)
                {
                    ListView_SetItemState(g_MeshList, activation->iItem,
                        LVIS_SELECTED | LVIS_FOCUSED,
                        LVIS_SELECTED | LVIS_FOCUSED);
                    SM_SelectFavoriteItem(activation->iItem);
                }
                // Preserve the editor's native context-menu handling.
                return DefSubclassProc(window, message, wParam, lParam);
            }

            // Do not let the native browser resolve our full-path display text
            // as if it were a mesh name in the currently selected package.
            return 0;
        }
    }

    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, SM_ListParentSubclassProc, 12);
    return DefSubclassProc(window, message, wParam, lParam);
}

static LRESULT CALLBACK SM_CommandSubclassProc(HWND window, UINT message,
                                                 WPARAM wParam, LPARAM lParam,
                                                 UINT_PTR, DWORD_PTR)
{
    if (window == g_MeshList && message == WM_RBUTTONUP)
    {
        SM_ShowContextMenu(window, lParam);
        return 0;
    }
    if (message == WM_COMMAND && SM_HandleCommand(wParam))
        return 0;
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, SM_CommandSubclassProc, 13);
    return DefSubclassProc(window, message, wParam, lParam);
}

void StaticMeshBrowserFavorites::Initialize()
{
    SM_LoadFavorites();

    static const BYTE expectedRefreshPrologue[] =
        { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };
    if (memcmp(reinterpret_cast<const void*>(SM_NATIVE_REFRESH_LIST),
               expectedRefreshPrologue,
               sizeof(expectedRefreshPrologue)) != 0)
    {
        Logger::log("StaticMeshBrowserFavorites: editor fingerprint mismatch; feature disabled");
        return;
    }

    if (!MemoryWriter::WriteJump(SM_NATIVE_REFRESH_LIST, SM_RefreshListHook))
    {
        Logger::log("StaticMeshBrowserFavorites: list refresh hook failed; feature disabled");
        return;
    }
    g_FeatureSupported = true;

    // TextureBrowser already chains SoundBrowser's menu hook. Install last so
    // all three wrappers continue to receive their own menu resources.
    g_PreviousLoadMenuA = *reinterpret_cast<LoadMenuAFn*>(LOADMENUA_IAT_SLOT);
    uintptr_t loadMenuHook = reinterpret_cast<uintptr_t>(SM_LoadMenuA_Hook);
    if (!g_PreviousLoadMenuA ||
        !MemoryWriter::WriteBytes(LOADMENUA_IAT_SLOT, &loadMenuHook,
                                  sizeof(loadMenuHook)))
        Logger::log("StaticMeshBrowserFavorites: LoadMenuA hook failed");

    g_PreviousCreateWindowExA =
        *reinterpret_cast<CreateWindowExAFn*>(CREATEWINDOWEXA_IAT_SLOT);
    uintptr_t createWindowHook =
        reinterpret_cast<uintptr_t>(SM_CreateWindowExA_Hook);
    if (!g_PreviousCreateWindowExA ||
        !MemoryWriter::WriteBytes(CREATEWINDOWEXA_IAT_SLOT, &createWindowHook,
                                  sizeof(createWindowHook)))
        Logger::log("StaticMeshBrowserFavorites: CreateWindowExA hook failed");
}
