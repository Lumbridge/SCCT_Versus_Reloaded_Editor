#pragma once
#include "WorkflowModel.h"

namespace Workflow::Editor
{
    void Initialize();
    std::filesystem::path Directory();
    std::string MapKey();
    // The open map's file as the frame shows it (unfolded), and the stock Save As setter.
    std::string MapFile();
    void SetMapFile(const std::string& path);
    // The level package's first words, and their restoration after an autosave copy.
    Json PackageWords();
    void RestorePackageWords(const Json& before);
    std::string LevelPath();
    uintptr_t LevelIdentity();
    unsigned Revision();
    unsigned MapGeneration();
    Json Actors(bool selectedOnly = false);
    Json SelectedIdentities();
    Json BrushVisibility();
    void SetBrushVisibility(int category, const std::string& action);
    void Select(const Json& identities, bool focus = false);
    Json SelectedSurfaceBrushes();
    Json BspSurfaceOwners();
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
    Json AddObjectiveActor(const Json& owner, const std::string& type);
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
    void BuildGeometry();
    unsigned long GeometryBuilds();
    Json DesignScene();
    Vector DesignGrid();
    // Followers move by delta inside the same transaction: a group keeping up
    // with one of its members.
    Json DesignBlockout(const Json& spec,const Pose& frame,const Json& previous = Json{},const Json& followers = Json{},const Vector& delta = {});
    Json DesignBlockoutBatch(const Json& items,const Json& followers = Json{},const Vector& delta = {});
    void DesignAlign(const Json& scene,int axis,const std::string& mode,double spacing);
    void DesignLayer(const Json& members,bool hidden,bool locked,const std::string& group = "",const std::string& groupAction = "none");
    void DesignSetFlags(const Json& members,int hidden,int locked);
    void DesignGroupMembers(const Json& members,const std::string& group,const std::string& action);
    void DesignTranslate(const Json& members,const Vector& delta);
    Json CreateLift(const Vector& position,double width,double length,double thickness,double rise,double moveTime);
    Json DesignPolyFlags(const Json& members);
    size_t DesignSetPolyFlags(const Json& members,const Json& flags);
    size_t DesignSendToLast(const Json& members);
    Json DesignSpawns();
    Json DesignClearances();
    void DesignPlay(const Json& start,const Pose& pose,bool launch = true);
    Json DesignTemporaryStart(const std::string& type,const std::string& team,const Pose& pose);
    // Security devices: creation, the motion sensor with its volume, and wiring.
    Json SecurityActors();
    Json Lights();
    Json ObjectiveActors();
    Json CreatePlayerStart(const std::string& type,const std::string& team,const Pose& pose);
    Json AddAlarmLocks(const Json& alarm,const Json& targets);
    Json SetActorProperties(const Json& identity,const Json& properties);
    Json CreateSecurityActor(const std::string& type,const Pose& pose,const Json& properties);
    Json CreateMotionSensor(const Vector& low,const Vector& high,const Json& properties);
    Json MoveSecurityActor(const Json& identity,const Pose& pose,const Json& properties,const Json& followers = Json{});
    void LinkDetectorToAlarm(const Json& detector,const Json& alarm);
    void UnwireDetector(const Json& detector);
    void DeleteSecurityActor(const Json& identity);
    void AddAlarmOutputs(const Json& alarm,const Json& targets);
    void DesignRemoveTemporaryStart(const Json& identity);
    Json InspectActor(const Json& identity);
    Json ExportEventJson(const Json& identity);
    void ImportEventJson(const Json& snapshot,const Json& document);
    void EditActor(const Json& snapshot, const Json& changes);
    Json CreateEventActor(const std::string& type, const Json& event = Json{}, bool trigger = false, const std::string& geometry = "point", int group = 0);
    void LinkEventActor(const Json& event, const Json& target, bool trigger, int group = 0);
}
