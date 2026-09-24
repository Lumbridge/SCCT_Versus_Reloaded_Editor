#pragma once
#include <windows.h>
#include <string>
#include "WorkflowModel.h"
namespace WorkflowTools
{
    constexpr UINT kConnections = 40920;
    constexpr UINT kAddObjective = 40949, kAddComputerObjective = 40950, kAddBombObjective = 40951, kAddFlagObjective = 40952;
    constexpr UINT kViews = 40921;
    constexpr UINT kBrushVisibility = 40953;
    constexpr UINT kAssemblies = 40922;
    constexpr UINT kSaveAssembly = 40923;
    constexpr UINT kFindMaterial = 40924;
    constexpr UINT kFindMesh = 40925;
    constexpr UINT kFitBuilderBrush = 40933;
    constexpr UINT kFitBuilderBrushToBrush = 40980;
    constexpr UINT kAddVertexPortal = 40981;
    constexpr UINT kBrushSnapX = 40936, kBrushSnapY = 40937, kBrushSnapZ = 40938, kBrushSnapAll = 40939;
    constexpr UINT kSurfaceSnapX = 40940, kSurfaceSnapY = 40941, kSurfaceSnapZ = 40942, kSurfaceSnapAll = 40943;
    constexpr UINT kVertexSnapX = 40944, kVertexSnapY = 40945, kVertexSnapZ = 40946, kVertexSnapAll = 40947;
    bool HandleCommand(UINT command);
    bool AppendObjectiveMenu(HMENU menu,const Workflow::Json& actor);
    void RunObjectiveCommand(UINT command,const Workflow::Json& snapshot);
    void Initialize();
    void FindUsages(HWND owner, void* asset, bool mesh);
    void RenameActorTag(HWND owner,std::string path,std::string type);
}
