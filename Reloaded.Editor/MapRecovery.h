#pragma once

namespace MapRecovery
{
    constexpr UINT kCommandId = 40902;
    constexpr UINT kOpenRecoveredCommandId = 40903;
    constexpr UINT kExportBrushesCommandId = 40904;
    constexpr UINT kRecoverEditableCommandId = 40905;

    void Run(HWND owner);
    void RunEditable(HWND owner);
    void OpenRecovered(HWND owner);
    void ExportRecoveredBrushes(HWND owner);
    bool HandleSaveCommand(UINT commandId);
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

    // Recovered levels still use the cooked serializer layout. SavePackage
    // must use that same layout or its editor-only FBspSurf/FPoly fields are
    // read as invalid memory. These calls are paired by the SavePackage hook.
    void BeginSavePackage(uintptr_t returnAddress);
    uintptr_t EndSavePackage();
}
