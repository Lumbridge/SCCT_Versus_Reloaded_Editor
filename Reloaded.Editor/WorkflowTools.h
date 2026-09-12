#pragma once
#include <windows.h>
#include <string>
namespace WorkflowTools
{
    constexpr UINT kConnections = 40920;
    constexpr UINT kViews = 40921;
    constexpr UINT kAssemblies = 40922;
    constexpr UINT kSaveAssembly = 40923;
    constexpr UINT kFindMaterial = 40924;
    constexpr UINT kFindMesh = 40925;
    bool HandleCommand(UINT command);
    void Initialize();
    void FindUsages(HWND owner, void* asset, bool mesh);
    void RenameActorTag(HWND owner,std::string path,std::string type);
}
