#pragma once
#include "WorkflowModel.h"

namespace Workflow::Editor
{
    void Initialize();
    std::filesystem::path Directory();
    std::string MapKey();
    std::string LevelPath();
    uintptr_t LevelIdentity();
    unsigned Revision();
    unsigned MapGeneration();
    Json Actors(bool selectedOnly = false);
    Json SelectedIdentities();
    void Select(const Json& identities, bool focus = false);
    Json SelectedSurfaceBrushes();
    Json SelectedMeshBounds();
    void FitBuilderBrushToMeshes();
    Json BrushSnapBounds(bool surfaces = false);
    Json SelectedBrushVertices();
    void SnapSelectedBrushVertices(unsigned axes);
    void SnapBrushesToGrid(unsigned axes, bool surfaces = false);
    Json CaptureView();
    std::string RestoreView(const Json& view);
    std::string CurrentAsset(bool mesh);
    Json FindUsages(const std::string& asset);
    void ReplaceUsages(const Json& usages, const std::string& source, const std::string& replacement);
    Json Connections(const std::string& actor);
    Json PreviewTagRename(const Json& actor,const std::string& newTag);
    void RenameTag(const Json& preview);
    Pose BuilderPose();
    Json CaptureAssembly(const Json& members, const Pose& frame);
    Json PlaceAssembly(const Json& definition, const Pose& frame, const std::map<std::string,std::string>& bindings);
    bool Compatible(const std::string& actorPath, const std::string& type);
    bool Exec(const std::string& command);
    void Redraw();
    // Reflected authoring API. Snapshots carry map identity and property values;
    // mutations reject stale snapshots and use one native transaction.
    Json ExportMapAuthoring();
    Json PreviewMapAuthoring(const Json& document);
    Json ApplyMapAuthoring(const Json& document);
    Json EventClasses();
    Json CameraNetworks();
    void OrderCameras(const Json& snapshot,const Json& paths,bool loop,const std::string& detach = "");
    Json AddNetworkCamera(const Json& snapshot,const Json& paths,bool loop);
    Json EventAssets(const std::string& type, bool classes = false);
    Json CreateEventComponent(const Json& owner, const std::string& type);
    void CaptureMoverKey(const Json& mover, int key);
    void PlayLevel();
    Json InspectActor(const Json& identity);
    Json ExportEventJson(const Json& identity);
    void ImportEventJson(const Json& snapshot,const Json& document);
    void EditActor(const Json& snapshot, const Json& changes);
    Json CreateEventActor(const std::string& type, const Json& event = Json{}, bool trigger = false, const std::string& geometry = "point", int group = 0);
    void LinkEventActor(const Json& event, const Json& target, bool trigger, int group = 0);
}
