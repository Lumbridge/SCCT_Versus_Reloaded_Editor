// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include <Windows.h>
#include "logger.h"
#include "Rendering.h"
#include "Debug.h"
#include "UI.h"
#include "General.h"
#include "Shadows.h"
#include "SoundBrowser.h"
#include "TextureBrowser.h"
#include "GEWireframeFix.h"
#include "GEKeybindSwap.h"
#include "LightmapFix.h"
#include "RealtimeFix.h"
#include "ReloadedOptions.h"
#include "DialogFix.h"
#include "BrowserOpenDir.h"
#include "AnimationBrowser.h"
#include "AmbientSoundZone.h"
#include "WindowDriftFix.h"
#include "DeintersectFix.h"
#include "MapUnlock.h"
#include "StaticMeshCollisionFix.h"
#include "StaticMeshBrowserFavorites.h"
#include "BspTextureClipboard.h"
#include "DdsImportFix.h"
#include "LightCullFix.h"
#include "ProjectorDetachFix.h"
#include "SizingBoxFix.h"
#include "MapRecovery.h"
#include "CrashDiagnostics.h"
#include "BspDiagnostics.h"
#include "LightmapPacker.h"
#include "RebuildAllMaps.h"
#include "ViewportConfigFix.h"
#include "EngineLog.h"
#include "MapCheckLog.h"

INIT_ONCE g_InitOnce = INIT_ONCE_STATIC_INIT;
HINSTANCE g_hReloadedDll = nullptr;

std::wstring GetDllPath(HINSTANCE hModule) {
    std::vector<wchar_t> pathBuffer(MAX_PATH);
    const DWORD result = GetModuleFileName(hModule, pathBuffer.data(), pathBuffer.size());
    if (result == 0) {
        return L"";
    }

    pathBuffer.resize(result);
    return std::wstring(pathBuffer.begin(), pathBuffer.end());
}

static std::wstring GetExecutableDirectory(std::wstring executablePath) {
    std::wstring::size_type pos = std::wstring(executablePath).find_last_of(L"\\/");
    return std::wstring(executablePath).substr(0, pos);
}

void RedirectToConsole()
{
#ifdef _DEBUG
    AllocConsole();
#else
    return;
#endif
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONIN$", "r", stdin);
    freopen_s(&fp, "CONOUT$", "w", stderr);

    std::cout << "SCCT Versus Reloaded injected successfully" << "\n";
}

// Custom unhandled exception filter
LONG WINAPI CustomUnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
    // The BSP handler normally captures these faults at first chance, before
    // Unreal's guard/unguard chain consumes them. Calling it again here is a
    // safe fallback for failures raised outside that guarded path.
    BspDiagnostics::LogException(exceptionInfo);
    CrashDiagnostics::LogUnhandledException(exceptionInfo);
    return EXCEPTION_EXECUTE_HANDLER;
}

LONG CALLBACK RecoveryActorTickExceptionHandler(
    EXCEPTION_POINTERS* exceptionInfo)
{
    BspDiagnostics::LogException(exceptionInfo);
    MapRecovery::LogActorTickException(exceptionInfo);
    MapRecovery::LogViewportException(exceptionInfo);
    return EXCEPTION_CONTINUE_SEARCH;
}

const int BaseAddress = 0x10900000;
BOOL CALLBACK InitFunction(PINIT_ONCE InitOnce, PVOID Parameter, PVOID* Context) {
    HMODULE hModule = static_cast<HMODULE>(Parameter);
    auto dllPath = GetDllPath(hModule);
    auto directoryPath = GetExecutableDirectory(dllPath);

    RedirectToConsole();

    Logger::Initialize(dllPath);
    Logger::log("");
    CrashDiagnostics::Initialize(dllPath);

    Rendering::Initialize();
    UI::Initialize();
    General::Initialize();
    Shadows::Initialize();
    SoundBrowser::Initialize();
    TextureBrowser::Initialize();
    StaticMeshBrowserFavorites::Initialize();
    BspTextureClipboard::Initialize();
    GEWireframeFix::Initialize();
    LightmapFix::Initialize();
    ReloadedOptions::Initialize();
    GEKeybindSwap::Initialize();
    RealtimeFix::Initialize();
    DialogFix::Initialize();
    BrowserOpenDir::Initialize();
    AnimationBrowser::Initialize();
    AmbientSoundZone::Initialize();
    WindowDriftFix::Initialize();
    DeintersectFix::Initialize();
    MapUnlock::Initialize();
    StaticMeshCollisionFix::Initialize();
    DdsImportFix::Initialize();
    LightCullFix::Initialize();
    ProjectorDetachFix::Initialize();
    SizingBoxFix::Initialize();
    LightmapPacker::Initialize();
    RebuildAllMaps::Initialize();
    ViewportConfigFix::Initialize();
    EngineLog::Initialize();
    MapCheckLog::Initialize();
    BspDiagnostics::Initialize(dllPath);

#ifdef _DEBUG
    Debug::Initialize();
#endif

    AddVectoredExceptionHandler(1, RecoveryActorTickExceptionHandler);
    MapRecovery::ArmActorTickDiagnostic();
    SetUnhandledExceptionFilter(CustomUnhandledExceptionFilter);

    return TRUE;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            g_hReloadedDll = hModule;
            InitOnceExecuteOnce(&g_InitOnce, InitFunction, hModule, NULL);
            break;
    }
    return TRUE;
}
