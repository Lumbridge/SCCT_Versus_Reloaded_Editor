#pragma once

#include <filesystem>
#include <string>

namespace MapRecovery
{
    constexpr UINT kCommandId = 40903;
    // Retained command identifiers; 40904 now converts legacy recovery files.
    constexpr UINT kOpenRecoveredCommandId = 40904;
    constexpr UINT kExportBrushesCommandId = 40905;
    constexpr UINT kRecoverEditableCommandId = 40906;

    void Run(HWND owner);
    void RunEditable(HWND owner);
    void OpenRecovered(HWND owner);
    void ExportRecoveredBrushes(HWND owner);
    // One-time conversion; the output uses the normal editor package layout.
    // No dialogs. The caller must supply a new destination filename.
    bool RecoverToSource(const std::filesystem::path& source,
                         const std::filesystem::path& destination,
                         std::string& error);
    bool HandleSaveCommand(UINT commandId);
    constexpr UINT kRecalculateLightingCommandId = 40929;
    constexpr UINT kRecalculateSelectedLightingCommandId = 40930;
    void RecalculateSelectedLighting(HWND owner, bool matchSurroundings = false);
    constexpr UINT kMatchSelectedLightingCommandId = 40931;
    void InitializeLightingProtection();
    void RecalculateLighting(HWND owner);
    void* ResolveBuilderBrushActor(void* level);
    bool IsRecoveredMapActive();
    bool UsesRecoveredBspLayout();
    bool ShouldSkipRecoveredEditorPoly();
    bool IsRecoveredBspModel(void* model);
    void RepairZoneInfoAssignment(void* actor);

    // The actor snapshot can contain native runtime actors whose cooked-only
    // state is not valid after T3D reconstruction.  These calls bracket the
    // engine's two actor Tick call sites so a first-chance exception can name
    // the exact reconstructed actor instead of reporting only TickAllActors.
    void ArmActorTickDiagnostic();
    void BeginActorTickLoop(void* level);
    void EndActorTickLoop();
    void RecordActorTick(void* actor, int actorIndex);
    void ClearActorTick();
    bool LogActorTickException(EXCEPTION_POINTERS* exceptionInfo);

    // Armed after the recovered-map confirmation closes. The vectored
    // exception handler records the first UI-thread fault because Unreal's
    // guarded call chain consumes it before the process-level crash logger.
    void ArmViewportExceptionDiagnostic();
    bool LogViewportException(EXCEPTION_POINTERS* exceptionInfo);

    // Temporary cooked extraction levels require the cooked serializer layout.
    // Finished source maps use the ordinary save path with these guards off.
    // These calls are paired by the SavePackage hook.
    void BeginSavePackage(uintptr_t returnAddress);
    uintptr_t EndSavePackage();
}
