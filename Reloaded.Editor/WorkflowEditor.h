#pragma once
#include "WorkflowModel.h"
#include <memory>

namespace MapCheck { struct Scene; struct Settings; }

namespace Workflow::Editor
{
    void Initialize();
    std::filesystem::path Directory();
    std::string MapKey();
    // The open map's file as the frame shows it (unfolded), and the stock Save As setter.
    std::string MapFile();
    void SetMapFile(const std::string& path);
    std::string LevelPath();
    uintptr_t LevelIdentity();
    unsigned Revision();
    unsigned MapGeneration();
    Json Actors(bool selectedOnly = false);
    Json SelectedIdentities();
    Json BrushVisibility();
    void SetBrushVisibility(int category, const std::string& action);
    Json HiddenActors();
    size_t RestoreHiddenActors(const Json& hidden);
    void Select(const Json& identities, bool focus = false);
    Json SelectedSurfaceBrushes();
    Json BspSurfaceOwners();
    Json SelectedMeshBounds();
    void FitBuilderBrushToMeshes();
    void FitBuilderBrushToBrushes();
    // Moves the builder brush to the viewport's last right-click point (grid
    // snapped along the surface when the grid is on), optionally rebuilding it
    // as the default cube. On a surface click it rests on the surface.
    void PlaceBuilderBrushAtClick(bool rebuild, bool onSurface);
    bool CanAddVertexPortal();
    Json AddVertexPortal();
    Json BrushSnapBounds(bool surfaces = false);
    Json SelectedBrushVertices();
    void SnapSelectedBrushVertices(unsigned axes);
    void SnapBrushesToGrid(unsigned axes, bool surfaces = false);
    Json CaptureView();
    std::string RestoreView(const Json& view);
    // Thumbnail framing: a viewport camera moved so the identities' locations
    // fill the shot (Thumbnail::FrameBox), and put back byte for byte.
    struct CameraState { uintptr_t camera = 0; std::array<unsigned char, 12> location{}, rotation{}; };
    CameraState FrameCamera(uintptr_t camera, const Json& identities, double aspect);
    void RestoreCamera(const CameraState& state);
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
    size_t DesignTurnActors(const Json& members,double degrees,const Vector& pivot);
    Json DesignActorSpans();
    size_t DesignSetHidden(const Json& hide,const Json& show);
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
    // Lighting Budget: every light with the raw flags the stock light checks
    // read, the in-game render sphere of those that count, each BSP leaf's
    // in-game light list (regenerated as Tools > Check InGame / Dynamic Lights
    // in Leaves does, then restored to the build's lists) and the zone names.
    // See LightingBudgetModel.h.
    Json LightingBudgetScene();
    // Map Check (MapCheckModel.h): the BSP as last built, zones, zone portals,
    // actors with the editor's encroachment test, and the stock CheckForErrors
    // entries (when settings.stockChecks).
    MapCheck::Scene MapCheckScene(const MapCheck::Settings& settings);
    // Selects the actors (framing them when asked) and the BSP surfaces of a row.
    size_t SelectMapCheckRow(const std::vector<std::string>& actors, const std::vector<int>& surfaces, bool frame);
    // Character Skins (CharacterSkinsModel.h): the map's Character Skins actor.
    // Settings: {placed, legacy, extra, compiled, slots:{SpyBody,...: material path or ""},
    // models:{SpyModel,...: skeletal mesh path or ""}, goggles:{SpyGoggleOffset,...: [x,y,z]}}.
    // Apply compiles the class into the map package when needed, replaces an earlier
    // version's actor, places the actor once and sets its values in one Undo step;
    // Remove deletes the actor (of any version).
    Json CharacterSkinSettings();
    Json LoadedSkeletalMeshes();
    Json ApplyCharacterSkins(const Json& slots, const Json& models, const Json& goggles);
    void RemoveCharacterSkins();
    // Writes the stock textures of the four slots as 32-bit TGA files (SpyBody.tga, ...).
    Json ExportDefaultCharacterSkins(const std::filesystem::path& folder);
    // Imports an edited image into MyLevel.CharacterSkins for a slot; returns its path.
    std::string ImportCharacterSkin(const std::string& property, const std::filesystem::path& file);
    // Selects exactly the live actors with these paths, one pass over the
    // level; focus frames them in the viewports. Returns how many were found.
    size_t SelectActorPaths(const std::vector<std::string>& paths, bool focus);
    // Security devices: creation, the motion sensor with its volume, and wiring.
    Json SecurityActors();
    Json Lights();
    Json ObjectiveActors();
    Json CreatePlayerStart(const std::string& type,const std::string& team,const Pose& pose);
    Json AddAlarmLocks(const Json& alarm,const Json& targets);
    Json SetActorProperties(const Json& identity,const Json& properties);
    Json InspectSettingsActor(const Json& identity);
    Json CreateSecurityActor(const std::string& type,const Pose& pose,const Json& properties);
    Json CreateMotionSensor(const Vector& low,const Vector& high,const Json& properties);
    Json MoveSecurityActor(const Json& identity,const Pose& pose,const Json& properties,const Json& followers = Json{});
    void LinkDetectorToAlarm(const Json& detector,const Json& alarm);
    void UnwireDetector(const Json& detector);
    void DeleteSecurityActor(const Json& identity);
    void AddAlarmOutputs(const Json& alarm,const Json& targets);
    void DesignRemoveTemporaryStart(const Json& identity);
    // Stages: the actors a plan can name, the plan the map carries, and
    // applying a plan in one Undo step.
    Json StageActors();
    Json StagePlan();
    Json PreviewStages(const Json& plan);
    Json ApplyStages(const Json& plan);
    Json InspectActor(const Json& identity);
    Json ExportEventJson(const Json& identity);
    void ImportEventJson(const Json& snapshot,const Json& document);
    void EditActor(const Json& snapshot, const Json& changes);
    Json CreateEventActor(const std::string& type, const Json& event = Json{}, bool trigger = false, const std::string& geometry = "point", int group = 0);
    void LinkEventActor(const Json& event, const Json& target, bool trigger, int group = 0);
    // Emitter Library (entry schema in EmitterLibraryModel.h). SelectedEmitters
    // is cheap enough for building a context menu. CaptureEmitters returns an
    // unsaved draft (no id) around the members' centre; map names its source
    // when the frame's file name is not the open map. PlaceEmitterEntry is one
    // Undo step and selects the new actors. Storage calls do not need a map.
    // A damaged emitter_library.json leaves the built-ins and packs listed,
    // reports EmitterLibrary::DamagedFile() as its only problem and refuses writes.
    // Effect packs are read from Directory()/EmitterPacks/*.json; a pack entry
    // whose required packages are not installed refuses to place with
    // EmitterLibrary::MissingPackagesMessage.
    Json SelectedEmitters();
    Json CaptureEmitters(const Json& members, const std::string& map = "");
    Json PlaceEmitterEntry(const Json& entry, const Pose& pose);
    // The library as one snapshot, rebuilt only when emitter_library.json or a file
    // in EmitterPacks changes (size or time) or this editor writes the user file;
    // each rebuild has a new revision. Unchanged pack files are not read again.
    // Hold the shared lists instead of copying them.
    struct EmitterLibraryView
    {
        std::shared_ptr<const Json> entries;    // built-ins, pack entries, your entries (EmitterLibrary())
        std::shared_ptr<const Json> categories; // EmitterCategories()
        std::shared_ptr<const Json> problems;   // EmitterLibraryProblems(): the user file's first, then the packs'
        std::shared_ptr<const Json> packs;      // listed packs: {id,name,description,requires,entries,problems,file,hidden}
        size_t userProblems = 0;                // how many problems are the user file's
        unsigned revision = 0;
    };
    EmitterLibraryView EmitterLibraryState();
    Json EmitterLibrary();
    Json EmitterLibraryProblems();
    Json EmitterCategories();
    unsigned EmitterLibraryRevision();
    // The listed packs with "missing": their required package files not in Packages.
    Json EmitterPacks();
    // A pack entry's required package files (that its dependencies use) that are not installed.
    Json MissingEmitterPackages(const Json& entry);
    // For tests: {"revision","packReads" (pack files parsed so far),"entries","packs","problems"}.
    Json EmitterLibraryCacheState();
    Json SaveEmitterEntry(const Json& entry);
    Json UpdateEmitterEntry(const std::string& id, const Json& changes);
    void DeleteEmitterEntry(const std::string& id);
    void RestoreBuiltinEmitters();
}
