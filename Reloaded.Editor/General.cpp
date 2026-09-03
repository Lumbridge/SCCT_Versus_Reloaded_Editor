#include "pch.h"
#include "General.h"
#include "Hooks.h"
#include "ReloadedOptions.h"
#include "RealtimeFix.h"
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#include "MemoryWriter.h"
#include "AnimationBrowser.h"
#include "MapRecovery.h"
#include "WindowDriftFix.h"
#include "RebuildAllMaps.h"
#include <mimalloc.h>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

INIT_HOOKS;

JMP_HOOK(0x110518AD, RemoveAudioSizeLimit) {
    static int Return = 0x110518B3;
    __asm {
        jmp dword ptr[Return]
    }
}

void InstallMemoryHooks() {
    uintptr_t fn_ptr;

    fn_ptr = reinterpret_cast<uintptr_t>(mi_malloc);
    MemoryWriter::WriteBytes(0x11AF2114, &fn_ptr, sizeof(fn_ptr));
    fn_ptr = reinterpret_cast<uintptr_t>(mi_free);
    MemoryWriter::WriteBytes(0x11AF209C, &fn_ptr, sizeof(fn_ptr));
    fn_ptr = reinterpret_cast<uintptr_t>(mi_realloc);
    MemoryWriter::WriteBytes(0x11AF21F0, &fn_ptr, sizeof(fn_ptr));
    fn_ptr = reinterpret_cast<uintptr_t>(mi_calloc);
    MemoryWriter::WriteBytes(0x11AF2098, &fn_ptr, sizeof(fn_ptr));
    fn_ptr = reinterpret_cast<uintptr_t>(mi_strdup);
    MemoryWriter::WriteBytes(0x11AF21C0, &fn_ptr, sizeof(fn_ptr));
}

// Play Map minimizes the editor by calling CloseWindow. Route that call
// through this wrapper so minimizing only happens when enabled.
static BOOL WINAPI ReloadedCloseWindow(HWND hWnd)
{
    if (g_ReloadedMinimizeOnPlay)
        return CloseWindow(hWnd);
    return TRUE;
}

static void InstallMinimizeOnPlayHook()
{
    uintptr_t fn_ptr = reinterpret_cast<uintptr_t>(ReloadedCloseWindow);
    MemoryWriter::WriteBytes(0x11AF23CC, &fn_ptr, sizeof(fn_ptr));
}

// The Enhanced SCCT launcher injects Reloaded.Core.dll before the game starts.
// That DLL expects the normal front-end startup path and crashes when UnrealEd
// launches the special Editeur=true URL.  For Play Level only, start the
// underlying game executable directly; normal ShellExecute calls are untouched.
static HINSTANCE WINAPI ReloadedShellExecuteA(HWND hwnd, LPCSTR operation,
                                               LPCSTR file, LPCSTR parameters,
                                               LPCSTR directory, INT showCommand)
{
    const char* fileName = file;
    if (fileName)
    {
        const char* slash = std::strrchr(fileName, '\\');
        const char* forwardSlash = std::strrchr(fileName, '/');
        if (!slash || (forwardSlash && forwardSlash > slash))
            slash = forwardSlash;
        if (slash)
            fileName = slash + 1;
    }

    if (fileName && parameters
        && _stricmp(fileName, "SCCT_Versus.exe") == 0
        && std::strstr(parameters, "Editeur=true"))
    {
        std::string gameDirectory;
        if (file && fileName != file)
            gameDirectory.assign(file, static_cast<size_t>(fileName - file - 1));
        else if (directory && *directory)
            gameDirectory = directory;
        else
        {
            char editorPath[MAX_PATH] = {};
            GetModuleFileNameA(nullptr, editorPath, MAX_PATH);
            const char* lastSlash = std::strrchr(editorPath, '\\');
            if (lastSlash)
                gameDirectory.assign(editorPath,
                                     static_cast<size_t>(lastSlash - editorPath));
        }

        if (!gameDirectory.empty())
        {
            std::string gamePath = gameDirectory + "\\SCCT Versus";
            if (GetFileAttributesA(gamePath.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                std::string commandLine = "\"" + gamePath + "\" "
                                        + parameters;
                STARTUPINFOA startupInfo = {};
                startupInfo.cb = sizeof(startupInfo);
                PROCESS_INFORMATION processInfo = {};

                if (CreateProcessA(gamePath.c_str(), commandLine.data(), nullptr,
                                   nullptr, FALSE, 0, nullptr,
                                   gameDirectory.c_str(), &startupInfo,
                                   &processInfo))
                {
                    CloseHandle(processInfo.hThread);
                    CloseHandle(processInfo.hProcess);
                    return reinterpret_cast<HINSTANCE>(static_cast<INT_PTR>(33));
                }

                return reinterpret_cast<HINSTANCE>(
                    static_cast<INT_PTR>(SE_ERR_ACCESSDENIED));
            }
        }
    }

    return ShellExecuteA(hwnd, operation, file, parameters, directory, showCommand);
}

static void InstallPlayLevelLaunchHook()
{
    uintptr_t fn_ptr = reinterpret_cast<uintptr_t>(ReloadedShellExecuteA);
    MemoryWriter::WriteBytes(0x11AF228C, &fn_ptr, sizeof(fn_ptr));
}

static const char s_github_url[] = "https://github.com/AllyPal/SCCT_Versus_Reloaded_Editor";
static const char s_wiki_url[]    = "https://github.com/AllyPal/SCCT_Versus_Reloaded_Editor/wiki";

static void __cdecl OpenURL(const char* url)
{
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

static void __cdecl OpenReloadedOptions()
{
    ShowReloadedOptionsDialog(GetActiveWindow());
}

static void __cdecl OpenAnimationBrowser()
{
    AnimationBrowser::Show(GetActiveWindow());
}

static void __cdecl ResetPropertyWindows()
{
    WindowDriftFix::ResetPropertyWindowPositions(GetActiveWindow());
}

static void __cdecl RecoverCompiledMap()
{
    MapRecovery::Run(GetActiveWindow());
}

static void __cdecl OpenRecoveredMap()
{
    MapRecovery::OpenRecovered(GetActiveWindow());
}

static void __cdecl ExportRecoveredBrushes()
{
    MapRecovery::ExportRecoveredBrushes(GetActiveWindow());
}

static void __cdecl RecoverCompiledMapAsEditable()
{
    MapRecovery::RunEditable(GetActiveWindow());
}

static bool __cdecl HandleRecoveredMapSave(UINT commandId)
{
    return MapRecovery::HandleSaveCommand(commandId);
}

static bool __cdecl IsControlKeyDown()
{
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

static void __cdecl OpenRebuildAllMaps()
{
    RebuildAllMaps::Show(GetActiveWindow());
}

// Game View (J) - Simulates the in-game view in the viewport
// Copies bHidden to bHiddenEd and only clears the flags it set when disabled
static const uint32_t kGEditor            = 0x1165dfa0;
static const uint32_t kGWarn              = 0x115befb0;
static const uint32_t kEditor_Level       = 0x130;
static const uint32_t kEditor_RedrawVtbl  = 0xE8;   // RedrawLevel(ULevel*)
static const uint32_t kLevel_ActorsData   = 0x2C;
static const uint32_t kLevel_ActorsNum    = 0x30;
static const uint32_t kActor_Flags        = 0x2E8;  // dword holding bHidden
static const uint32_t kMask_Hidden        = 0x1000; // its bit
static const uint32_t kActor_EdFlags      = 0x2F4;  // dword holding bHiddenEd
static const uint32_t kMask_HiddenEd      = 0x08;   // its bit
static const uint32_t kActor_Texture      = 0x228;  // editor icon sprite
static const uint32_t kActor_DrawType     = 0x2D9;
static const uint8_t  kDrawType_Particle  = 10;
static const uint32_t kObj_Class          = 0x24;
static const uint32_t kObj_Name           = 0x20;
static const uint32_t kClass_Super        = 0x28;
static const uint32_t kGNames             = 0x1169cfbc;

// Manual exclusion for SComputerObjectiveTrigger because it has bHidden=true but is visible in game
static const char* const kGameViewKeep[] = { "SComputerObjectiveTrigger" };

// Visible in game (corona, light beam, rain) but the editor also billboards their icon
static const char* const kGameViewIconOnly[] = { "Light", "ERainVolume" };

static void __cdecl DuplicateSelection()
{
    static constexpr char kCommand[] = "ACTOR DUPLICATE";

    void* editor = *reinterpret_cast<void**>(kGEditor);
    void* output = *reinterpret_cast<void**>(kGWarn);
    if (!editor || !output)
        return;

    // UUnrealEdEngine exposes its FExec interface at +0x28. Calling the
    // interface's first virtual method mirrors the editor's Duplicate menu
    // command, including transactions, multi-selection and brush handling.
    void* execInterface = static_cast<char*>(editor) + 0x28;
    void** vtable = *reinterpret_cast<void***>(execInterface);
    if (!vtable || !vtable[0])
        return;

    using ExecFn = int(__thiscall*)(void*, const char*, void*);
    reinterpret_cast<ExecFn>(vtable[0])(execInterface, kCommand, output);
}

static bool g_gameView = false;
static std::unordered_set<void*> g_gameViewHidden;          // compared only, never dereferenced
static std::unordered_map<void*, void*> g_gameViewSprites;  // actor -> saved Texture

// Hides the rain volume wireframe but keeps the rain visible
JMP_HOOK(0x1114aa20, RainVolumeBoundsHook)
{
    static int s_resume = 0x1114aa25;

    __asm
    {
        cmp  byte ptr [g_gameView], 0
        jnz  skip_bounds

        push ebp
        mov  ebp, esp
        push -1
        jmp  dword ptr [s_resume]

    skip_bounds:
        ret  8
    }
}

// AActor::RenderEditorInfo assumes Actor->Texture is non-null
JMP_HOOK(0x11191110, ActorEditorInfoHook)
{
    static int s_resume = 0x11191115;

    __asm
    {
        cmp  byte ptr [g_gameView], 0
        jnz  skip_info

        push ebp
        mov  ebp, esp
        push -1
        jmp  dword ptr [s_resume]

    skip_info:
        ret  0xc
    }
}

// DrawSprite assumes Actor->Texture is non-null. Callers must guard it or it will crash on cleared icons
JMP_HOOK(0x110a3270, DrawSpriteNullTextureFix)
{
    static int s_resume = 0x110a3275;

    __asm
    {
        mov  eax, dword ptr [esp + 4]      // the actor
        test eax, eax
        jz   skip_sprite
        cmp  dword ptr [eax + 0x228], 0    // Actor->Texture
        jz   skip_sprite

        push ebp
        mov  ebp, esp
        push -1
        jmp  dword ptr [s_resume]

    skip_sprite:
        ret                                // caller cleans the arg
    }
}

// Skip ULevel::RenderGEs in the editor viewport to hide GE lines without affecting hit-testing
JMP_HOOK(0x10eced35, GEViewportRenderHook)
{
    static int s_renderGEs = 0x11119b20;
    static int s_resume    = 0x10eced3a;

    __asm
    {
        cmp  byte ptr [g_gameView], 0
        jnz  skip_ge

        call dword ptr [s_renderGEs]   // callee cleans its arg
        jmp  dword ptr [s_resume]

    skip_ge:
        add  esp, 4                    // drop the pushed render context
        jmp  dword ptr [s_resume]
    }
}

static bool GV_ClassChainHas(void* actor, const char* want)
{
    int* gnames = *reinterpret_cast<int**>(kGNames);
    if (!gnames) return false;

    void* cls = *reinterpret_cast<void**>(static_cast<char*>(actor) + kObj_Class);
    for (int depth = 0; cls && depth < 16; ++depth)
    {
        int entry = gnames[*reinterpret_cast<int*>(static_cast<char*>(cls) + kObj_Name)];
        if (entry && _stricmp(reinterpret_cast<const char*>(entry + 0x0C), want) == 0)
            return true;
        cls = *reinterpret_cast<void**>(static_cast<char*>(cls) + kClass_Super);
    }
    return false;
}

static bool GV_IsKeptClass(void* actor)
{
    for (const char* keep : kGameViewKeep)
        if (GV_ClassChainHas(actor, keep))
            return true;
    return false;
}

static bool GV_IsIconOnlyClass(void* actor)
{
    for (const char* cls : kGameViewIconOnly)
        if (GV_ClassChainHas(actor, cls))
            return true;
    return false;
}

static void** GV_Actors(void* gEditor, int* outCount)
{
    void* level = *reinterpret_cast<void**>(static_cast<char*>(gEditor) + kEditor_Level);
    if (!level) return nullptr;
    *outCount = *reinterpret_cast<int*>(static_cast<char*>(level) + kLevel_ActorsNum);
    return *reinterpret_cast<void***>(static_cast<char*>(level) + kLevel_ActorsData);
}

static void GV_Apply(void** actors, int count)
{
    for (int i = 0; i < count; ++i)
    {
        void* actor = actors[i];
        if (!actor) continue;
        DWORD* flags = reinterpret_cast<DWORD*>(static_cast<char*>(actor) + kActor_Flags);
        DWORD* ed    = reinterpret_cast<DWORD*>(static_cast<char*>(actor) + kActor_EdFlags);
        void** tex   = reinterpret_cast<void**>(static_cast<char*>(actor) + kActor_Texture);

        // Emitter particles, and a light's corona / light beam, are visible in game
        // and die with the actor, so only hide the editor icon
        const bool iconOnly =
            *reinterpret_cast<uint8_t*>(static_cast<char*>(actor) + kActor_DrawType)
                == kDrawType_Particle ||
            GV_IsIconOnlyClass(actor);

        if ((*flags & kMask_Hidden) && !(*ed & kMask_HiddenEd)
                && !iconOnly && !GV_IsKeptClass(actor))
        {
            *ed |= kMask_HiddenEd;
            g_gameViewHidden.insert(actor);
            continue;
        }

        if (iconOnly && *tex)
        {
            g_gameViewSprites[actor] = *tex;
            *tex = nullptr;
        }
    }
    g_gameView = true;
}

// Refresh from the live actor list because a map reload invalidates the saved pointers.
static void GV_Restore(void** actors, int count)
{
    for (int i = 0; i < count; ++i)
    {
        void* actor = actors[i];
        if (!actor) continue;

        if (g_gameViewHidden.count(actor))
            *reinterpret_cast<DWORD*>(static_cast<char*>(actor) + kActor_EdFlags)
                &= ~kMask_HiddenEd;

        auto its = g_gameViewSprites.find(actor);
        if (its != g_gameViewSprites.end())
        {
            void** tex = reinterpret_cast<void**>(static_cast<char*>(actor) + kActor_Texture);

            // Restore only if Texture is still null (the value set by GV_Apply).
            // If it's non-null, the texture was changed or this actor was reused,
            // so restoring the saved pointer could overwrite valid data.
            if (!*tex)
                *tex = its->second;
        }
    }
    g_gameViewHidden.clear();
    g_gameViewSprites.clear();
    g_gameView = false;
}

// bHiddenEd is saved, so Game View must be off before SavePackage to avoid baking hidden state into the map
static void __cdecl GameViewClearForSave()
{
    if (!g_gameView) return;

    void* gEditor = *reinterpret_cast<void**>(kGEditor);
    if (!gEditor) return;

    int count = 0;
    void** actors = GV_Actors(gEditor, &count);
    if (actors) GV_Restore(actors, count);
}

static void __cdecl BeginRecoverySavePackage(uintptr_t returnAddress)
{
    MapRecovery::BeginSavePackage(returnAddress);
}

static uintptr_t __cdecl EndRecoverySavePackage()
{
    return MapRecovery::EndSavePackage();
}

JMP_HOOK(0x10fb2610, SavePackageGameViewHook)
{
    static int s_resume = 0x10fb2615;
    static uintptr_t s_recoveryReturnAddress = 0;

    __asm
    {
        mov  eax, dword ptr [esp]
        push eax
        call BeginRecoverySavePackage
        add  esp, 4
        mov  dword ptr [esp], offset recovery_save_complete

        pushad
        call GameViewClearForSave
        popad

        push ebp
        mov  ebp, esp
        push -1
        jmp  dword ptr [s_resume]

    recovery_save_complete:
        pushad
        call EndRecoverySavePackage
        mov  dword ptr [s_recoveryReturnAddress], eax
        popad
        push dword ptr [s_recoveryReturnAddress]
        ret
    }
}

static void __cdecl ToggleGameView()
{
    void* gEditor = *reinterpret_cast<void**>(kGEditor);
    if (!gEditor) return;

    void* level = *reinterpret_cast<void**>(static_cast<char*>(gEditor) + kEditor_Level);
    if (!level) return;

    int count = 0;
    void** actors = GV_Actors(gEditor, &count);
    if (!actors) return;

    if (g_gameView) GV_Restore(actors, count);
    else            GV_Apply(actors, count);

    void* vtable = *reinterpret_cast<void**>(gEditor);
    void* redraw = *reinterpret_cast<void**>(static_cast<char*>(vtable) + kEditor_RedrawVtbl);
    __asm {
        mov  ecx, gEditor
        push level
        mov  eax, redraw
        call eax
    }
}

JMP_HOOK(0x10e57b30, MenuBarDispatch)
{
    static int s_continue = 0x10e57b35;
    static bool s_recoverySaveHandled = false;

    __asm {
        cmp  dword ptr [esp+4], 40066 // Reloaded Options
        je   do_reloaded_options
        cmp  dword ptr [esp+4], 40067 // Show Animation Browser
        je   do_anim_browser
        cmp  dword ptr [esp+4], 40907 // Reset Property Window Positions
        je   do_reset_property_windows
        cmp  dword ptr [esp+4], 40902 // Rebuild All Maps
        je   do_rebuild_all
        cmp  dword ptr [esp+4], 40900 // Reloaded Github
        je   do_github
        cmp  dword ptr [esp+4], 40901 // Reloaded Wiki
        je   do_wiki
        cmp  dword ptr [esp+4], 40903 // Recover Compiled Map
        je   do_recover_map
        cmp  dword ptr [esp+4], 40904 // Open Recovered Map
        je   do_open_recovered_map
        cmp  dword ptr [esp+4], 40905 // Export Recovered BSP as Brushes
        je   do_export_recovered_brushes
        cmp  dword ptr [esp+4], 40906 // Recover Compiled Map as Editable
        je   do_recover_editable_map
        cmp  dword ptr [esp+4], 40007 // File > Save
        je   maybe_save_recovered_map
        cmp  dword ptr [esp+4], 40008 // File > Save As
        je   maybe_save_recovered_map

        // Fallthrough: replay overwritten prologue then continue
    continue_stock_command:
        push ebp
        mov  ebp, esp
        push -1
        jmp  dword ptr [s_continue]

    do_reloaded_options:
        call OpenReloadedOptions
        retn 4

    do_anim_browser:
        call OpenAnimationBrowser
        retn 4

    do_reset_property_windows:
        call ResetPropertyWindows
        retn 4

    do_rebuild_all:
        call OpenRebuildAllMaps
        retn 4

    do_github:
        push offset s_github_url
        call OpenURL
        add  esp, 4
        retn 4

    do_wiki:
        push offset s_wiki_url
        call OpenURL
        add  esp, 4
        retn 4

    do_recover_map:
        call RecoverCompiledMap
        retn 4

    do_open_recovered_map:
        call OpenRecoveredMap
        retn 4

    do_export_recovered_brushes:
        call ExportRecoveredBrushes
        retn 4

    do_recover_editable_map:
        call RecoverCompiledMapAsEditable
        retn 4

    maybe_save_recovered_map:
        pushad
        mov  eax, dword ptr [esp+36]
        push eax
        call HandleRecoveredMapSave
        add  esp, 4
        mov  byte ptr [s_recoverySaveHandled], al
        popad
        cmp  byte ptr [s_recoverySaveHandled], 0
        je   continue_stock_command
        retn 4
    }
}

// Cooked maps retain the builder brush actor slot but strip its editor-only
// UModel. During compiled-map recovery, provide the blank editor map's model
// before UnrealEd's GetBrush() invariant is evaluated.
static void* __fastcall ResolveRecoveryBuilderBrush(void* level, void*)
{
    return MapRecovery::ResolveBuilderBrushActor(level);
}

JMP_HOOK(0x10E632B0, RecoveryBuilderBrushHook)
{
    __asm
    {
        jmp ResolveRecoveryBuilderBrush
    }
}

static bool __cdecl IsRecoveredBspModelForHooks(void* model)
{
    return MapRecovery::IsRecoveredBspModel(model);
}

static bool __cdecl UseRecoveredBspSurfaceLayoutForHooks()
{
    return MapRecovery::UsesRecoveredBspLayout();
}

static bool __cdecl SkipRecoveredEditorPolyForHooks()
{
    return MapRecovery::ShouldSkipRecoveredEditorPoly();
}

// MouseDelta snapshots every affected editor object on the first mouse hit,
// before it has decided whether the gesture is only a viewport pan. A compiled
// level can be reached through those object references, causing the undo
// archive to traverse cooked BSP with source-editor serialization rules. Keep
// that one MouseDelta snapshot disabled for recovered maps; its following null
// check already supports running without an undo archive.
JMP_HOOK(0x10EC6F92, RecoveredMouseDeltaTransactionHook)
{
    static int s_continue = 0x10EC6F98;

    __asm
    {
        pushad
        call UseRecoveredBspSurfaceLayoutForHooks
        test al, al
        popad
        jnz  without_undo

        // Replay the overwritten six-byte GUndo load for normal source maps.
        mov  ecx, dword ptr ds:[11691D6Ch]
        jmp  dword ptr [s_continue]

    without_undo:
        xor  ecx, ecx
        jmp  dword ptr [s_continue]
    }
}

// FBspSurf::Serialize shares its common fields between source and PC-runtime
// layouts, then selects whether to serialize the editor-only FPoly pointer.
// MouseDelta starts transactions before it knows whether a click will pan or
// select. That path can therefore serialize a cooked surface without going
// through UModel::ModifySurf, where the narrower transaction guard below
// lives. Transactions can serialize temporary copies rather than exact
// elements of the model's surface array, so use the PC-runtime branch for all
// BSP surface serialization while a recovered map is active. Otherwise the
// cooked +0x20 field is mistaken for an FPoly pointer and UnModel.cpp asserts
// while deserializing its vertex count.
JMP_HOOK(0x110CDADB, RecoveredBspSurfaceLayoutHook)
{
    static int s_loadConfiguredPlatform = 0x110CDAE1;
    static int s_comparePlatform = 0x110CDAE7;

    __asm
    {
        pushad
        call UseRecoveredBspSurfaceLayoutForHooks
        test al, al
        popad
        jnz  use_runtime_layout

        // Replay the complete overwritten six-byte instruction. The five-byte
        // jump replaces its first five bytes, so resume at the next boundary.
        mov  ecx, dword ptr ds:[11691D7Ch]
        jmp  dword ptr [s_loadConfiguredPlatform]

    use_runtime_layout:
        mov  eax, 1
        jmp  dword ptr [s_comparePlatform]
    }
}

// PC-runtime surfaces with either of the two editor-poly flags can contain an
// attached FPoly in the package, so MAP LOAD must follow the normal flag test.
// Later editor transactions can serialize temporary 0x2c-byte cooked-surface
// copies whose attached pointer is not safe to traverse. Skip the editor poly
// only after the recovered level has finished loading.
JMP_HOOK(0x110CDAF6, RecoveredBspSurfaceEditorPolyHook)
{
    static int s_testEditorPolyFlags = 0x110CDAFD;
    static int s_skipEditorPoly = 0x110CDAFF;

    __asm
    {
        pushad
        call SkipRecoveredEditorPolyForHooks
        test al, al
        popad
        jnz  skip_editor_poly

        // Replay the overwritten seven-byte flag test and resume at the
        // conditional branch which consumes its result.
        test dword ptr [ebx+14h], 0C000000h
        jmp  dword ptr [s_testEditorPolyFlags]

    skip_editor_poly:
        jmp  dword ptr [s_skipEditorPoly]
    }
}

// UModel::ModifySurf normally snapshots the selected FBspSurf through GUndo
// before changing it. Recovered maps contain the cooked 0x2c-byte surface
// layout, while the editor transaction serializer expects the larger source
// layout; attempting that snapshot reads stripped fields and crashes. Skip
// only the undo snapshot for the active recovered level, then resume the rest
// of ModifySurf so selection and master-surface propagation still work.
JMP_HOOK(0x110D0E37, RecoveredBspSurfaceTransactionHook)
{
    static int s_continue = 0x110D0E3D;
    static int s_skipTransaction = 0x110D0E66;

    __asm
    {
        pushad
        lea  eax, [edi-94h]
        push eax
        call IsRecoveredBspModelForHooks
        add  esp, 4
        test al, al
        popad
        jnz  skip_transaction

        // Replay the overwritten UModel::ModifySurf instruction.
        mov  ecx, dword ptr ds:[11691D6Ch]
        jmp  dword ptr [s_continue]

    skip_transaction:
        jmp  dword ptr [s_skipTransaction]
    }
}

// Cooked packages already contain render data that the runtime loader can use.
// Surface selection clears that cache before UModel::BuildRenderData rebuilds
// it through editor-only assumptions; recovered maps no longer have all of
// that source state. Preserve both the loaded cache and its geometry whenever
// the viewport requests a rebuild. Normal source maps remain unchanged.
JMP_HOOK(0x110D1230, RecoveredBspClearRenderDataHook)
{
    static int s_continue = 0x110D1235;

    __asm
    {
        pushad
        push ecx
        call IsRecoveredBspModelForHooks
        add  esp, 4
        test al, al
        popad
        jnz  keep_cooked_render_data

        // Replay the complete five-byte UModel::ClearRenderData prologue.
        push ebp
        mov  ebp, esp
        push -1
        jmp  dword ptr [s_continue]

    keep_cooked_render_data:
        ret  4
    }
}

JMP_HOOK(0x110D13D0, RecoveredBspRenderDataHook)
{
    static int s_continue = 0x110D13D5;

    __asm
    {
        pushad
        push ecx
        call IsRecoveredBspModelForHooks
        add  esp, 4
        test al, al
        popad
        jnz  keep_cooked_render_data

        // Replay the complete five-byte UModel::BuildRenderData prologue.
        push ebp
        mov  ebp, esp
        push -1
        jmp  dword ptr [s_continue]

    keep_cooked_render_data:
        ret
    }
}

static void __fastcall RepairRecoveredZoneInfo(void* actor, void*)
{
    MapRecovery::RepairZoneInfoAssignment(actor);
}

// Cooked UModel serialization can omit the editor-side ZoneActor reference.
// AZoneInfo::CheckForErrors assumes every nonzero zone already has that link
// and dereferences it while formatting an error message. Restore a missing
// association immediately before the stock validator runs.
JMP_HOOK(0x111906F0, RecoveredZoneInfoValidationHook)
{
    static int s_continue = 0x111906F5;

    __asm
    {
        pushad
        call RepairRecoveredZoneInfo
        popad

        // Replay the overwritten five-byte function prologue.
        push ebp
        mov  ebp, esp
        push -1
        jmp  dword ptr [s_continue]
    }
}

// The build dialog's geometry, BSP and lighting stages consume editor-only
// brush/FPoly state which a compiled recovery cannot contain. MAP REBUILD
// otherwise replaces the valid cooked BSP with an empty wireframe result.
// Preserve the recovered geometry and lightmaps; the following path stage is
// still allowed to validate actors and rebuild navigation data.
JMP_HOOK(0x10E10C00, RecoveredBuildGeometryHook)
{
    static int s_continue = 0x10E10C05;

    __asm
    {
        pushad
        call UseRecoveredBspSurfaceLayoutForHooks
        test al, al
        popad
        jnz  skip_build

        push ebp
        mov  ebp, esp
        push -1
        jmp  dword ptr [s_continue]

    skip_build:
        ret
    }
}

JMP_HOOK(0x10E10CC0, RecoveredBuildBspHook)
{
    static int s_continue = 0x10E10CC5;

    __asm
    {
        pushad
        call UseRecoveredBspSurfaceLayoutForHooks
        test al, al
        popad
        jnz  skip_build

        push ebp
        mov  ebp, esp
        push -1
        jmp  dword ptr [s_continue]

    skip_build:
        ret
    }
}

JMP_HOOK(0x10E10D80, RecoveredBuildLightingHook)
{
    static int s_continue = 0x10E10D85;

    __asm
    {
        pushad
        call UseRecoveredBspSurfaceLayoutForHooks
        test al, al
        popad
        jnz  skip_build

        push ebp
        mov  ebp, esp
        push -1
        jmp  dword ptr [s_continue]

    skip_build:
        ret
    }
}

static void __cdecl BeginRecoveredActorTick(void* actor, int actorIndex)
{
    MapRecovery::RecordActorTick(actor, actorIndex);
}

static void __cdecl EndRecoveredActorTick()
{
    MapRecovery::ClearActorTick();
}

static void __cdecl BeginRecoveredActorTickLoop(void* level)
{
    MapRecovery::BeginActorTickLoop(level);
}

static void __cdecl EndRecoveredActorTickLoop()
{
    MapRecovery::EndActorTickLoop();
}

// ULevel::Tick has a normal actor-array pass and a deferred linked-list pass.
// Bracket both virtual Tick calls so a first-chance AV can be attributed to
// the actor which owns the invalid reconstructed runtime state.
JMP_HOOK(0x11184ADF, RecoveredActorTickLoopBeginHook)
{
    static int s_resume = 0x11184AE4;

    __asm
    {
        pushad
        mov  eax, dword ptr [ebp-14h]
        push eax
        call BeginRecoveredActorTickLoop
        add  esp, 4
        popad
        // Replay: mov eax, [ebp-14h]; xor ebx, ebx
        mov  eax, dword ptr [ebp-14h]
        xor  ebx, ebx
        jmp  dword ptr [s_resume]
    }
}

// Capture the array slot before ULevel::Tick reads any native AActor fields.
// The previous diagnostic started later, after the unsafe flag read.
JMP_HOOK(0x11184B03, RecoveredActorArrayFetchHook)
{
    static int s_resume = 0x11184B08;

    __asm
    {
        mov  eax, dword ptr [ecx + esi*4]
        pushad
        push esi
        push eax
        call BeginRecoveredActorTick
        add  esp, 8
        popad
        test eax, eax
        jmp  dword ptr [s_resume]
    }
}

JMP_HOOK(0x11184B53, RecoveredActorArrayTickBeginHook)
{
    static int s_resume = 0x11184B58;

    __asm
    {
        // Replay: mov ecx, [ecx + esi*4]; mov edx, [ecx]
        mov  ecx, dword ptr [ecx + esi*4]
        pushad
        push esi
        push ecx
        call BeginRecoveredActorTick
        add  esp, 8
        popad
        mov  edx, dword ptr [ecx]
        jmp  dword ptr [s_resume]
    }
}

JMP_HOOK(0x11184B66, RecoveredActorArrayTickEndHook)
{
    static int s_resume = 0x11184B6B;

    __asm
    {
        pushad
        call EndRecoveredActorTick
        popad
        // Replay: mov edx, [ecx + esi*4]; add ebx, eax
        mov  edx, dword ptr [ecx + esi*4]
        add  ebx, eax
        jmp  dword ptr [s_resume]
    }
}

JMP_HOOK(0x11184BE0, RecoveredDeferredActorTickBeginHook)
{
    static int s_resume = 0x11184BE5;

    __asm
    {
        pushad
        push -1
        push ecx
        call BeginRecoveredActorTick
        add  esp, 8
        popad
        // Replay: mov eax, [ebp+8]; mov edx, [ecx]
        mov  eax, dword ptr [ebp+8]
        mov  edx, dword ptr [ecx]
        jmp  dword ptr [s_resume]
    }
}

JMP_HOOK(0x11184BED, RecoveredDeferredActorTickEndHook)
{
    static int s_resume = 0x11184BF2;

    __asm
    {
        pushad
        call EndRecoveredActorTick
        popad
        // Replay: mov edx, [ebp-14h]; add ebx, eax
        mov  edx, dword ptr [ebp-14h]
        add  ebx, eax
        jmp  dword ptr [s_resume]
    }
}

JMP_HOOK(0x11184C30, RecoveredActorTickLoopEndHook)
{
    static int s_resume = 0x11184C35;

    __asm
    {
        pushad
        call EndRecoveredActorTickLoop
        popad
        // Replay: mov eax, dword ptr ds:[11825244h]
        mov  eax, dword ptr ds:[11825244h]
        jmp  dword ptr [s_resume]
    }
}

JMP_HOOK(0x10f00d10, ViewportKeyUpHook)
{
    static int s_resume = 0x10f00d15;

    __asm
    {
        // Ctrl+D: duplicate the current actor/brush selection. This hook is
        // viewport-only, so text fields keep their normal Ctrl+D behaviour.
        cmp  dword ptr [esp + 4], 0x44
        jne  check_f12
        push ecx
        call IsControlKeyDown
        pop  ecx
        test al, al
        jnz  do_duplicate

    check_f12:
        // F12: Reloaded Options
        cmp  dword ptr [esp + 4], 0x7B
        je   do_f12

        // J: Game View
        cmp  dword ptr [esp + 4], 0x4A
        je   do_game_view

        // F7: Attempts to compile UnrealScript but fails (scripts are stripped). Disabled to prevent accidentally pressing F7 and crashing.
        cmp  dword ptr [esp + 4], 0x76
        je   swallow

        push ebp
        mov  ebp, esp
        push -1
        jmp  dword ptr [s_resume]

    do_game_view:
        call ToggleGameView
        ret  8

    do_duplicate:
        call DuplicateSelection
        ret  8

    do_f12:
        call OpenReloadedOptions
    swallow:
        ret  8
    }
}

// Duplication offset
JMP_HOOK(0x10eb8573, DupOffsetHook)
{
    static int s_skip   = 0x10eb861c;
    static int s_is_dup = 0x10eb8579;
    static int s_no_dup = 0x10eb85d2;

    __asm
    {
        cmp  byte ptr [g_ReloadedNoDuplicateOffset], 0
        jz   normal

        cmp  dword ptr [ebp + 0xc], 0
        jnz  skip_offset

    normal:
        cmp  dword ptr [ebp + 0xc], 0
        jz   is_no_dup
        jmp  dword ptr [s_is_dup]

    is_no_dup:
        jmp  dword ptr [s_no_dup]

    skip_offset:
        jmp  dword ptr [s_skip]
    }
}

JMP_HOOK(0x10eb8722, DupOffsetHook2)
{
    static int s_skip   = 0x10eb897b;
    static int s_is_dup = 0x10eb8728;
    static int s_no_dup = 0x10eb8755;

    __asm
    {
        cmp  byte ptr [g_ReloadedNoDuplicateOffset], 0
        jz   normal2

        cmp  dword ptr [ebp + 0xc], 0
        jnz  skip_offset2

    normal2:
        cmp  dword ptr [ebp + 0xc], 0
        jz   is_no_dup2
        jmp  dword ptr [s_is_dup]

    is_no_dup2:
        jmp  dword ptr [s_no_dup]

    skip_offset2:
        jmp  dword ptr [s_skip]
    }
}

void General::Initialize()
{
    INSTALL_HOOKS;
    InstallMemoryHooks();
    InstallMinimizeOnPlayHook();
    InstallPlayLevelLaunchHook();
}
