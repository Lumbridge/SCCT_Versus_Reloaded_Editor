#include "pch.h"
#include "BspTextureClipboard.h"

#include "MemoryWriter.h"
#include "logger.h"
#include "WorkflowEditor.h"

#include <cstring>

namespace
{
    constexpr uintptr_t kGEditor = 0x1165DFA0;
    constexpr uintptr_t kExecLogDevice = 0x115BEFB0;
    constexpr uintptr_t kGObjectsData = 0x11697B70;
    constexpr uintptr_t kGObjectsCount = 0x11697B74;
    constexpr uintptr_t kLoadMenuAIatSlot = 0x11AF23F0;

    constexpr size_t kEditorLevelOffset = 0x130;
    constexpr size_t kEditorCurrentMaterialOffset = 0x138;
    constexpr size_t kLevelModelOffset = 0x13C;
    constexpr size_t kModelSurfsOffset = 0x94;
    constexpr size_t kSurfStride = 0x2C;
    constexpr size_t kSurfMaterialOffset = 0x10;
    constexpr size_t kSurfFlagsOffset = 0x14;
    constexpr DWORD kPolySelected = 0x02000000;
    constexpr int kMaxSurfaceCount = 2000000;

    constexpr UINT kSurfaceContextMenuId = 108;
    constexpr UINT kApplyTextureCommandId = 40142;

    struct SelectedSurfaceState
    {
        int Count;
        void* SingleMaterial;
        bool Readable;
    };

    using LoadMenuAFn = HMENU(WINAPI*)(HINSTANCE, LPCSTR);

    LoadMenuAFn g_PreviousLoadMenuA = nullptr;
    void* g_CopiedMaterial = nullptr;

    SelectedSurfaceState ReadSelectedSurfaces()
    {
        SelectedSurfaceState result = {};

        __try
        {
            void* editor = *reinterpret_cast<void**>(kGEditor);
            void* level = editor ? *reinterpret_cast<void**>(
                static_cast<char*>(editor) + kEditorLevelOffset) : nullptr;
            void* model = level ? *reinterpret_cast<void**>(
                static_cast<char*>(level) + kLevelModelOffset) : nullptr;
            if (!model)
                return result;

            char* surfaces = *reinterpret_cast<char**>(
                static_cast<char*>(model) + kModelSurfsOffset);
            const int surfaceCount = *reinterpret_cast<int*>(
                static_cast<char*>(model) + kModelSurfsOffset + sizeof(void*));
            const int surfaceCapacity = *reinterpret_cast<int*>(
                static_cast<char*>(model) + kModelSurfsOffset +
                sizeof(void*) + sizeof(int));
            if (surfaceCount < 0 || surfaceCount > surfaceCapacity ||
                surfaceCapacity < 0 || surfaceCapacity > kMaxSurfaceCount ||
                (surfaceCount > 0 && !surfaces))
                return result;

            result.Readable = true;
            for (int i = 0; i < surfaceCount; ++i)
            {
                char* surface = surfaces +
                    static_cast<size_t>(i) * kSurfStride;
                const DWORD flags = *reinterpret_cast<DWORD*>(
                    surface + kSurfFlagsOffset);
                if (!(flags & kPolySelected))
                    continue;

                ++result.Count;
                if (result.Count == 1)
                {
                    result.SingleMaterial = *reinterpret_cast<void**>(
                        surface + kSurfMaterialOffset);
                }
                else
                {
                    // The menu only needs to distinguish zero, one and many.
                    result.SingleMaterial = nullptr;
                    break;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            result = {};
        }

        return result;
    }

    bool IsLiveObject(void* wanted)
    {
        if (!wanted)
            return false;

        __try
        {
            void** objects = *reinterpret_cast<void***>(kGObjectsData);
            const int objectCount = *reinterpret_cast<int*>(kGObjectsCount);
            if (!objects || objectCount < 0 ||
                objectCount > kMaxSurfaceCount)
                return false;

            for (int i = 0; i < objectCount; ++i)
            {
                if (objects[i] == wanted)
                    return true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return false;
    }

    bool ExecEditorCommand(const char* command)
    {
        if (!command || !command[0])
            return false;

        __try
        {
            void* editor = *reinterpret_cast<void**>(kGEditor);
            void* output = *reinterpret_cast<void**>(kExecLogDevice);
            if (!editor || !output)
                return false;

            void* execInterface = static_cast<char*>(editor) + 0x28;
            void** vtable = *reinterpret_cast<void***>(execInterface);
            if (!vtable || !vtable[0])
                return false;

            using ExecFn = int(__thiscall*)(void*, const char*, void*);
            return reinterpret_cast<ExecFn>(vtable[0])(
                execInterface, command, output) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ApplyCopiedMaterial(void* material)
    {
        void** currentMaterial = nullptr;
        void* previousMaterial = nullptr;

        __try
        {
            void* editor = *reinterpret_cast<void**>(kGEditor);
            if (!editor || !material)
                return false;

            currentMaterial = reinterpret_cast<void**>(
                static_cast<char*>(editor) + kEditorCurrentMaterialOffset);
            previousMaterial = *currentMaterial;
            *currentMaterial = material;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        // This is the same command used by the stock Apply Texture menu item.
        // It owns the undo transaction, updates master polys and redraws.
        const bool applied = ExecEditorCommand("POLY SETTEXTURE");

        __try
        {
            *currentMaterial = previousMaterial;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return applied;
    }

    int MenuPosition(HMENU menu, UINT command)
    {
        const int count = GetMenuItemCount(menu);
        for (int i = 0; i < count; ++i)
        {
            if (GetMenuItemID(menu, i) == command)
                return i;
        }
        return -1;
    }

    HMENU WINAPI LoadMenuA_Hook(HINSTANCE instance, LPCSTR menuName)
    {
        HMENU menu = g_PreviousLoadMenuA
            ? g_PreviousLoadMenuA(instance, menuName)
            : LoadMenuA(instance, menuName);
        if (!menu || (reinterpret_cast<ULONG_PTR>(menuName) >> 16) != 0 ||
            LOWORD(reinterpret_cast<ULONG_PTR>(menuName)) !=
                kSurfaceContextMenuId)
            return menu;

        HMENU context = GetSubMenu(menu, 0);
        if (!context ||
            MenuPosition(context,
                         BspTextureClipboard::kCopyTextureCommandId) >= 0)
            return menu;

        const SelectedSurfaceState selected = ReadSelectedSurfaces();
        const bool canCopy = selected.Readable && selected.Count == 1 &&
            selected.SingleMaterial != nullptr;
        const bool copiedMaterialIsLive = IsLiveObject(g_CopiedMaterial);
        if (g_CopiedMaterial && !copiedMaterialIsLive)
            g_CopiedMaterial = nullptr;
        const bool canPaste = selected.Readable && selected.Count > 0 &&
            copiedMaterialIsLive;
        const int applyPosition = MenuPosition(context, kApplyTextureCommandId);
        if (applyPosition < 0)
            return menu;

        bool canSelectBrush = false;
        try { canSelectBrush = !Workflow::Editor::SelectedSurfaceBrushes().empty(); }
        catch (const std::exception&) { /* Missing master polygons are normal on cooked BSP. */ }
        AppendMenuA(context, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(context, MF_STRING | (canSelectBrush ? MF_ENABLED : MF_GRAYED),
                    BspTextureClipboard::kSelectBrushCommandId, "Select &Brush");

        InsertMenuA(context, applyPosition,
                    MF_BYPOSITION | MF_STRING |
                        (canCopy ? MF_ENABLED : MF_GRAYED),
                    BspTextureClipboard::kCopyTextureCommandId,
                    "Copy &Texture");
        InsertMenuA(context, applyPosition + 1,
                    MF_BYPOSITION | MF_STRING |
                        (canPaste ? MF_ENABLED : MF_GRAYED),
                    BspTextureClipboard::kPasteTextureCommandId,
                    "Paste T&exture");
        return menu;
    }
}

void BspTextureClipboard::CopySelectedTexture()
{
    const SelectedSurfaceState selected = ReadSelectedSurfaces();
    if (selected.Readable && selected.Count == 1 && selected.SingleMaterial)
        g_CopiedMaterial = selected.SingleMaterial;
}

void BspTextureClipboard::PasteTexture()
{
    const SelectedSurfaceState selected = ReadSelectedSurfaces();
    if (!selected.Readable || selected.Count == 0 ||
        !IsLiveObject(g_CopiedMaterial))
    {
        g_CopiedMaterial = nullptr;
        return;
    }

    if (!ApplyCopiedMaterial(g_CopiedMaterial))
        Logger::log("BspTextureClipboard: POLY SETTEXTURE failed");
}

void BspTextureClipboard::SelectBrush()
{
    try
    {
        // Re-resolve after the popup closes; no pointers or selection are cached.
        const auto brushes = Workflow::Editor::SelectedSurfaceBrushes();
        Workflow::Editor::Select(brushes, false);
    }
    catch (const std::exception& e)
    {
        MessageBoxA(GetActiveWindow(), e.what(), "Select Brush", MB_OK | MB_ICONINFORMATION);
    }
}

void BspTextureClipboard::Initialize()
{
    // SoundBrowser, TextureBrowser and StaticMeshBrowserFavorites install
    // wrappers on the same import. Chaining preserves every menu extension.
    g_PreviousLoadMenuA =
        *reinterpret_cast<LoadMenuAFn*>(kLoadMenuAIatSlot);
    const uintptr_t hook = reinterpret_cast<uintptr_t>(LoadMenuA_Hook);
    if (!g_PreviousLoadMenuA ||
        !MemoryWriter::WriteBytes(kLoadMenuAIatSlot, &hook, sizeof(hook)))
        Logger::log("BspTextureClipboard: LoadMenuA hook failed");
}
