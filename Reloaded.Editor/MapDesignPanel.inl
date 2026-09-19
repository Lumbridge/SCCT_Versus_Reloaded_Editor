// Included inside WorkflowTools' anonymous namespace, sharing its persistent
// per-map library and standard native forms.
enum DesignControl
{
    DPlane=700,DFit,DRefresh,DReference,DCalibrate,DRemoveReference,DBlock,DEdit,DPlace,DDetach,
    DGuides,DMeasure,DAlign,DLayer,DRepeat,DPlay,DRoute,DFinish,DClear,DCanvas,DStatus,DDepth,
    DSnap,DFloor,DDoorway,DMovement,DCompare,DWorkspace,DUndo,DRedo,
    // Quick-add menu entries, raised from the hovered wall.
    DAddDoorway=730,DAddCorridor,DAddVent,DAddRoom,DAddRoomAbove,DAddRoomBelow,DAddStairsUp,DAddStairsDown,
    // Inspector: live fields for the preview or the selected piece.
    DName=740,DShape,DConstruction,DWidth,DLength,DHeight,DThickness,DSteps,DCeiling,DPortal,
    DPositionX,DPositionY,DPositionZ,DYaw,DInspectorTitle,DDiscard,
    // Building, checking and overlays.
    DBuild=760,DCheck,DOverlays,DSecurity,DUnlit,DScene,DKeys,DDepthLabel,DCheckList=780,DInspectorLabel=785,
    // Right-click menu on the design view: what is under the cursor, then what
    // can start at that point.
    DCtxEditPiece=850,DCtxSelectPiece,DCtxDetachPiece,DCtxEditAnnotation,DCtxRemoveAnnotation,DCtxEditGuide,DCtxRemoveGuide,
    DCtxSelectDevice,DCtxRoomHere,DCtxCorridorHere,DCtxVentHere,DCtxGuideHere,DCtxRouteHere,DCtxMeasureHere,DCtxPlayHere,
    DCtxDeleteDevice,DCtxDeletePiece,DCtxUnwire,DCtxUseAlarm,DCtxAlarmOutputs,
    DCtxDeviceFirst=870,DCtxDeviceLast=875,
    // Alarms a detector can be wired to, or detectors an alarm can take.
    DCtxWireFirst=880,DCtxWireLast=959,
    // Lights: edit, select, delete, and the presets placed at a point.
    DCtxEditLight=960,DCtxSelectLight,DCtxDeleteLight,DCtxEditDevice,DCtxSnapWall,DCtxLightFirst=970,DCtxLightLast=979,
    // Game-mode actors: starts, the mission, objectives and devices.
    DCtxStartSpy=980,DCtxStartMerc,DCtxMission,DCtxObjective,DCtxComputer,DCtxBomb,DCtxFlag,DCtxAlarmLock,DCtxSelectObjective,DCtxDeleteObjective,
    DCtxMissionFirst=990,DCtxMissionLast=999,
    // Gameplay elements placed in stages.
    DCtxElementFirst=1120,DCtxElementLast=1129,
    // Floors above and below, and what joins them.
    DCtxFloorUp=1130,DCtxFloorDown,DCtxFloorDupUp,DCtxFloorDupDown,DCtxStairsUp,DCtxStairsDown,DCtxLadderUp,DCtxPipeUp,DCtxOpening,
    // Stairs by shape: four up, four down, from the badge and from the Floors menu.
    DAddStairsUpFirst=1140,DAddStairsDownFirst=1144,DCtxStairsUpFirst=1150,DCtxStairsDownFirst=1154,
    DAddLiftUp=1148,DAddLiftDown,DCtxLiftUp=1158,DCtxLiftDown,
    // Locking what is under the cursor or selected, and the Scene panel.
    DCtxLock=1160,DCtxUnlock,DCtxLockSelection,DCtxUnlockSelection,DCtxScene,DCtxSliderAll=1170,DCtxTurnCW,DCtxTurnCCW,
    // The contextual properties sheet: its fields, their labels and its buttons.
    DSheetField=1000,DSheetLabel=1040,DSheetButton=1100
};
// What the properties sheet is showing: a light, a device, a reference, an
// annotation, or the route being drawn.
struct Sheet
{
    std::string kind;
    Json subject;
    std::vector<std::string> keys;
    std::map<std::string,std::string> shown;
};
// Dragging in the design views edits the preview directly; a drag that starts
// on empty space selects instead.
// A storey the floor slider can show: where its floor is and how high it goes.
struct DesignLevel{double base=0,top=0;};
// A row of the Scene panel: a library piece, a loose actor, or a group header.
struct SceneRow
{
    std::string key,name,type,group,layer;
    Json members=Json::array();
    bool header=false,piece=false,hidden=false,locked=false,selected=false;
    double z=0;
    Json data; // The library piece, for pieces.
};
struct DesignDrag
{
    enum class Kind { None,Move,Resize,Select,Guide,RoutePoint,Device,DeviceAim,LightRadius,Floor,Rotate };
    Kind kind=Kind::None;
    Vector start{};
    POINT from{},to{};
    Json spec;
    Pose frame{};
    int axis=0,side=0;
    size_t index=0,point=0;
    bool moved=false;
    // A move that began by pressing inside a selected piece: without motion it
    // is an ordinary click on that piece.
    bool fromSelection=false;
    // Where the rotate handle was grabbed, as an angle about the piece's base.
    double angle=0;
    // A security device being dragged or aimed: its live pose and beam length.
    Json device;
    Pose devicePose{};
    double deviceLength=0;
};
struct DesignState
{
    HWND window{},canvas{},status{};
    unsigned epoch=0,revision=0,dataRevision=0;
    Json scene=Json::array(),pending,previous,points=Json::array(),data;
    Pose frame{};
    int plane=0;
    double zoom=.2,panX=300,panY=300,depth=0;
    POINT last{};
    bool panning=false,snap=true,floorFilter=false;
    double floorLow=0,floorHigh=256;
    Vector grid{64,64,64};
    DesignDrag drag;
    std::string mode,annotationName,annotationKind,annotationTeam,calibrationFile;
    bool annotationCrouched=false;
    int playtestChoice=-1,playtestFacing=0;
    std::map<std::string,std::unique_ptr<Gdiplus::Bitmap>> images;
    // Projected brush outlines for filled plan drawing, cached per view and
    // per map revision.
    // The wall the cursor is near, for adding a neighbour where you point.
    Json hoverPiece;
    std::string hoverWall;
    double hoverOffset=0;
    POINT hoverAt{};
    // The tick and cross drawn beside a new preview: click to place or discard.
    POINT placeOrb{},discardOrb{};
    bool orbsShown=false;
    std::vector<std::vector<Design::Point>> hulls;
    int hullPlane=-1;
    unsigned hullRevision=0;
    bool writingInspector=false;
    // Whether built BSP still matches the brushes, from the build counter.
    unsigned long builds=0;
    unsigned builtRevision=0;
    // Workspace edits (annotations, references, guides, layers) have their own
    // history, used when they were the most recent thing that changed.
    std::vector<Json> workspaceUndo,workspaceRedo;
    unsigned workspaceRevision=~0u;
    bool overlays=true;
    HWND checkWindow{};
    Json issues=Json::array();
    // Security devices and the placement in progress.
    HWND securityWindow{};
    Json securityActors=Json::array(),securityRows=Json::array();
    std::string securityMode,securityAlarm;
    Vector securityFirst{};
    bool securityFirstSet=false;
    // The right-clicked point, while a menu action that starts there runs.
    Vector contextPoint{};
    bool contextPointSet=false;
    // Pieces that followed brushes moved in the editor, for diagnostics.
    Json followed=Json::array();
    // Lights, and whether the plan shades what they do not reach.
    Json lights=Json::array();
    // Player starts, the mission, objectives and their devices.
    Json objectives=Json::array();
    // A gameplay element being placed: its kind, points so far, stage, and
    // the top view's depth to return to.
    std::string elementKind;
    std::vector<Vector> elementPoints;
    int elementStage=0;
    double elementDepth=0;
    bool unlit=false;
    // The contextual properties sheet and where the inspector area starts.
    Sheet sheet;
    bool writingSheet=false;
    int inspectorTop=0;
    // The storeys the floor slider offers, cached per map and workspace revision.
    std::vector<DesignLevel> levels;
    unsigned levelRevision=~0u,levelDataRevision=~0u;
    // The Scene panel: its rows, what they were last time, folded groups, the
    // filter, and a guard while the list is being written.
    HWND sceneWindow{};
    std::vector<SceneRow> sceneRows;
    std::vector<std::string> sceneKeys;
    std::set<std::string> sceneCollapsed;
    std::string sceneFilter;
    int sceneGroupBy=0,sceneShow=0;
    bool sceneSyncing=false;
    // Group members and other selected actors that move with the next edit,
    // and by how much.
    Json followers=Json::array();
    Vector followDelta{};
    // Tooltips for the panel's controls, and the keyboard legend window.
    HWND tips{},keysWindow{};
};
HWND designWindow=nullptr;
Json NormalizeDesign(Json data)
{
    if(!data.is_object())data=Json::object();
    for(const char* key:{"pieces","layers","annotations","guides"})
        if(!data.contains(key) || !data.at(key).is_array())data[key]=Json::array();
    data["references"]=Design::References(data);
    data.erase("reference");
    return data;
}
// Cached until the shared library document changes, so painting does not
// re-read and copy the whole workflow library on every frame.
const Json& DesignData(DesignState& s)
{
    Sync();
    if(s.data.is_null() || s.dataRevision!=documentRevision)
    {
        s.data=NormalizeDesign(document["maps"][mapKey].value("design",Json::object()));
        s.dataRevision=documentRevision;
    }
    return s.data;
}
std::filesystem::path DesignWorkspacePath()
{
    if(mapKey.empty())return {};
    return Editor::Directory()/"Workspaces"/(std::filesystem::path(mapKey).stem().string()+".json");
}
// A copy of the workspace travels with the map: map packaging includes this
// file and its reference images.
void DesignWriteWorkspace(const Json& data)
{
    auto path=DesignWorkspacePath();
    if(path.empty())return;
    std::filesystem::create_directories(path.parent_path());
    WriteDocument(path,Design::ExportWorkspace(data,std::filesystem::path(mapKey).stem().string()));
}
void SheetRefreshLater(DesignState& s);
void DesignSave(DesignState& s,const Json& data,bool record=true)
{
    if(record && !s.data.is_null() && s.data!=data)
    {
        s.workspaceUndo.push_back(s.data);
        if(s.workspaceUndo.size()>50)s.workspaceUndo.erase(s.workspaceUndo.begin());
        s.workspaceRedo.clear();
        s.workspaceRevision=Editor::Revision();
    }
    Json next=document;
    next["maps"][mapKey]["design"]=data;
    Save(next);
    s.data=data;
    s.dataRevision=documentRevision;
    try{DesignWriteWorkspace(data);}catch(const std::exception&){ /* The library is saved; the portable copy is best effort. */ }
    SheetRefreshLater(s);
}
void DesignStatus(DesignState& s,const std::string& text){SetWindowTextA(s.status,text.c_str());}
std::filesystem::path DesignReferenceFile(const std::string& file)
{
    return Editor::Directory()/"References"/file;
}
Gdiplus::Bitmap* DesignImage(DesignState& s,const std::string& file)
{
    auto found=s.images.find(file);
    if(found==s.images.end())
        found=s.images.emplace(file,std::unique_ptr<Gdiplus::Bitmap>(Gdiplus::Bitmap::FromFile(DesignReferenceFile(file).c_str()))).first;
    auto* image=found->second.get();
    return image && image->GetLastStatus()==Gdiplus::Ok ? image : nullptr;
}
int DesignHorizontal(const DesignState& s){return s.plane==2?1:0;}
int DesignVertical(const DesignState& s){return s.plane==0?1:2;}
// The depth field beside the view combo: the height clicks land on in the top
// view, the depth in an elevation. Typed values apply on Enter or focus loss;
// the field is rewritten only when its text would change.
void DesignDepthShow(DesignState& s)
{
    if(!s.window)return;
    const int axis=3-DesignHorizontal(s)-DesignVertical(s);
    const char* label=axis==2?"Z":axis==1?"Y":"X";
    if(Text(GetDlgItem(s.window,DDepthLabel))!=label)SetWindowTextA(GetDlgItem(s.window,DDepthLabel),label);
    auto field=GetDlgItem(s.window,DDepth);
    const auto text=Design::Round(s.depth);
    if(GetFocus()!=field && Text(field)!=text)SetWindowTextA(field,text.c_str());
}
void DesignDepthRead(DesignState& s)
{
    const double value=Design::Number(Text(GetDlgItem(s.window,DDepth)));
    if(std::abs(value-s.depth)<1e-9)return;
    s.depth=value;
    InvalidateRect(s.canvas,nullptr,FALSE);
    DesignStatus(s,std::string(s.plane==0?"Clicks and new pieces now land at Z ":"Clicks now land at depth ")+Design::Round(value)+".");
}
Vector DesignWorld(DesignState& s,double x,double y)
{
    Vector v{};
    const int a=DesignHorizontal(s),b=DesignVertical(s);
    v[3-a-b]=s.depth;
    v[a]=(x-s.panX)/s.zoom;
    v[b]=(s.panY-y)/s.zoom;
    return v;
}
Gdiplus::PointF DesignScreen(DesignState& s,const Vector& v)
{
    const int a=DesignHorizontal(s),b=DesignVertical(s);
    return {static_cast<float>(s.panX+v[a]*s.zoom),static_cast<float>(s.panY-v[b]*s.zoom)};
}
// Snapping uses the editor's own grid spacing, so design views and viewports
// place brushes on the same lines.
void DesignUpdateGrid(DesignState& s)
{
    // The editor's grid changes without a map revision, so read it when a drag
    // or click is about to use it.
    try{s.grid=Editor::DesignGrid();}catch(const std::exception&){}
}
Vector DesignSnap(DesignState& s,const Vector& v)
{
    if(!s.snap)return v;
    Vector grid{};
    grid[DesignHorizontal(s)]=s.grid[DesignHorizontal(s)];
    grid[DesignVertical(s)]=s.grid[DesignVertical(s)];
    return Design::SnapVector(v,grid);
}
bool DesignOnFloor(DesignState& s,double first,double second)
{
    return !s.floorFilter || Design::WithinFloor(s.floorLow,s.floorHigh,first,second);
}
// The storeys of the map, lowest first: one per room, corridor or vent floor
// height, with pieces that sit inside a storey (a vent on its wall) folded into
// it. Without library pieces, carved brushes stand in. Stairwells are left out
// because they span two storeys.
const std::vector<DesignLevel>& DesignLevels(DesignState& s)
{
    if(s.levelRevision==s.revision && s.levelDataRevision==s.dataRevision)return s.levels;
    std::vector<std::pair<double,double>> spans;
    try
    {
        for(const auto& piece:DesignData(s).at("pieces"))
        {
            const auto kind=piece.at("spec").at("kind").get<std::string>();
            if(kind!="Room" && kind!="Corridor" && !Design::Crouching(kind))continue;
            if(piece.at("spec").value("name",std::string())=="Stairwell")continue;
            bool live=false;
            for(auto& member:piece.at("members"))for(auto& actor:s.scene)if(actor.at("path")==member.at("path"))live=true;
            if(!live)continue;
            const double base=piece.at("position").get<Vector>()[2];
            spans.push_back({base,base+piece.at("spec").value("height",256.0)});
        }
    }
    catch(const std::exception&) { spans.clear(); }
    if(spans.empty())
        for(const auto& actor:s.scene)
        {
            if(actor.value("csg",0)!=2 || actor.at("edges").empty())continue;
            double lo=1e18,hi=-1e18;
            for(auto& edge:actor.at("edges"))for(int end=0;end<2;++end){const double z=edge[end].get<Vector>()[2];lo=std::min(lo,z);hi=std::max(hi,z);}
            spans.push_back({lo,hi});
        }
    std::sort(spans.begin(),spans.end());
    std::vector<DesignLevel> levels;
    for(const auto& span:spans)
    {
        // A base well inside the storey below belongs to it.
        if(!levels.empty() && span.first<levels.back().top-32){levels.back().top=std::max(levels.back().top,span.second);continue;}
        levels.push_back({span.first,span.second});
    }
    if(levels.size()>40)levels.resize(40);
    s.levels=levels;
    s.levelRevision=s.revision;
    s.levelDataRevision=s.dataRevision;
    return s.levels;
}
// The slider runs down the right edge of the canvas: "All" at the top, then
// each storey from the highest down, one detent per step.
struct DesignSliderLayout{float x=0,top=0,step=0;int count=0;};
DesignSliderLayout DesignSliderLayoutFor(DesignState& s,const RECT& rect)
{
    DesignSliderLayout layout;
    layout.count=static_cast<int>(DesignLevels(s).size());
    layout.x=static_cast<float>(rect.right-18);
    layout.top=52;
    const float room=static_cast<float>(std::max(60L,rect.bottom-100));
    layout.step=std::clamp(room/static_cast<float>(layout.count+1),16.f,32.f);
    return layout;
}
int DesignSliderIndexAt(DesignState& s,double y)
{
    RECT rect{};GetClientRect(s.canvas,&rect);
    const auto layout=DesignSliderLayoutFor(s,rect);
    return std::clamp(static_cast<int>(std::lround((y-layout.top)/layout.step)),0,layout.count);
}
bool DesignSliderHit(DesignState& s,POINT at,int& index)
{
    RECT rect{};GetClientRect(s.canvas,&rect);
    const auto layout=DesignSliderLayoutFor(s,rect);
    if(layout.count==0)return false;
    const float bottom=layout.top+layout.step*layout.count;
    // Only the track and its handle take clicks: a fitted map's rightmost wall
    // sits 50 pixels from the edge, and the labels must not steal it.
    if(at.x<layout.x-10 || at.x>layout.x+10 || at.y<layout.top-10 || at.y>bottom+10)return false;
    index=DesignSliderIndexAt(s,at.y);
    return true;
}
// The detent the current filter sits on: 0 for all floors, else the storey
// whose floor is nearest the filter's lower height.
int DesignLevelIndex(DesignState& s)
{
    const auto& levels=DesignLevels(s);
    if(!s.floorFilter || levels.empty())return 0;
    size_t best=0;
    for(size_t i=1;i<levels.size();++i)if(std::abs(levels[i].base-s.floorLow)<std::abs(levels[best].base-s.floorLow))best=i;
    return static_cast<int>(levels.size()-best);
}
void DesignSetLevel(DesignState& s,int index)
{
    const auto levels=DesignLevels(s);
    const int count=static_cast<int>(levels.size());
    index=std::clamp(index,0,count);
    if(index==0)
    {
        if(!s.floorFilter)return;
        s.floorFilter=false;
        s.hoverPiece=Json{};
        InvalidateRect(s.canvas,nullptr,FALSE);
        DesignStatus(s,"Showing every floor. Drag the slider, or press Page Up / Page Down, to show one storey at a time.");
        return;
    }
    const auto& level=levels[count-index];
    // From just under this floor to just under the next one, so what sits on
    // the floor counts and the storey above does not.
    const double low=level.base-.5;
    const double high=(count-index+1<count?levels[count-index+1].base:level.top+16)-.5;
    if(s.floorFilter && std::abs(s.floorLow-low)<1e-9 && std::abs(s.floorHigh-high)<1e-9)return;
    s.floorFilter=true;s.floorLow=low;s.floorHigh=high;
    if(s.plane==0)s.depth=level.base;
    DesignDepthShow(s);
    s.hoverPiece=Json{};
    InvalidateRect(s.canvas,nullptr,FALSE);
    DesignStatus(s,"Showing the floor at Z "+Design::Round(level.base)+" ("+std::to_string(index)+" of "+std::to_string(count)+" from the top)"
        +(s.plane==0?"; new pieces go at that height.":".")+" Page Up / Page Down step between floors; the top of the slider shows all of them.");
}
bool DesignShows(DesignState& s,const Json& actor)
{
    if(actor.at("hidden").get<bool>())return false;
    if(!s.floorFilter)return true;
    const auto& edges=actor.at("edges");
    if(edges.empty())return DesignOnFloor(s,actor.at("position").get<Vector>()[2],actor.at("position").get<Vector>()[2]);
    for(auto& edge:edges)if(DesignOnFloor(s,edge[0].get<Vector>()[2],edge[1].get<Vector>()[2]))return true;
    return false;
}
std::vector<Json> DesignReferencesInView(DesignState& s,const Json& data)
{
    std::vector<Json> result;
    for(auto& reference:data.at("references"))
    {
        if(reference.at("plane").get<int>()!=s.plane)continue;
        if(s.floorFilter && reference.contains("floor") && !DesignOnFloor(s,reference.at("floor"),reference.at("floor")))continue;
        result.push_back(reference);
    }
    return result;
}
void DesignBuildHulls(DesignState& s);
void DesignVerifyActive(DesignState& s);
void DesignFollowEditorMoves(DesignState& s);
void DesignWarn(DesignState& s,const std::string& prefix);
void DesignPlaytest(DesignState& s,int choice,const Pose& pose);
void DesignQuickAddMenu(DesignState& s,POINT at);
bool DesignGeometryStale(DesignState& s);
void DesignCheck(DesignState& s);
void SecurityOpen(DesignState& s);
void SecurityClick(DesignState& s,const Vector& at);
void SecurityRefreshList(DesignState& s);
void SecurityPaint(DesignState& s,Gdiplus::Graphics& g,const std::function<void(const std::string&,Gdiplus::PointF)>& label);
bool SecurityHandleAt(DesignState& s,double x,double y,Json& device,bool& aim);
void SceneRefreshList(DesignState& s);
void SceneOpen(DesignState& s);
void DesignDeleteSelection(DesignState& s);
void DesignKeys(DesignState& s);
void DesignCommand(DesignState& s,int id);
std::string SceneGroupOf(DesignState& s,const std::string& path);
Json SceneGroupMembers(DesignState& s,const std::string& name);
// Whether an actor is locked, by its path in the scene.
bool DesignLockedPath(DesignState& s,const std::string& path)
{
    for(const auto& actor:s.scene)if(actor.at("path")==path)return actor.at("locked").get<bool>();
    return false;
}
std::string SecurityName(const Json& actor);
bool LightHandleAt(DesignState& s,double x,double y,Json& light,bool& radiusHandle);
void SheetApply(DesignState& s);
void SheetButton(DesignState& s,int index);
void SheetRefresh(DesignState& s);
void SheetRefreshLater(DesignState& s);
void SheetShowRoute(DesignState& s);
void SheetShowGuide(DesignState& s,size_t index);
void SheetShowAnnotation(DesignState& s,size_t index);
void SheetShowLight(DesignState& s,const Json& light);
void SheetShowDevice(DesignState& s,const Json& device);
void SheetHint(DesignState& s,const std::string& hint);
void SheetPieceControls(DesignState& s,bool show);
void SheetDestroyControls(DesignState& s);
LRESULT CALLBACK DesignFieldProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR reference);
void LightPaint(DesignState& s,Gdiplus::Graphics& g,const std::function<void(const std::string&,Gdiplus::PointF)>& label);
void LightUnlitPaint(DesignState& s,Gdiplus::Graphics& g);
void ObjectivePaint(DesignState& s,Gdiplus::Graphics& g,const std::function<void(const std::string&,Gdiplus::PointF)>& label);
Json ObjectiveAt(DesignState& s,double x,double y);
bool ObjectiveAimHandleAt(DesignState& s,double x,double y,Json& start);
std::string ObjectiveLabel(const Json& actor);
void ElementClick(DesignState& s,const Vector& at);
void ElementCancel(DesignState& s);
void ElementPaint(DesignState& s,Gdiplus::Graphics& g,const std::function<void(const std::string&,Gdiplus::PointF)>& label);
std::vector<std::string> LightIssues(DesignState& s);
void DesignRefresh(DesignState& s)
{
    Sync();
    if(s.epoch!=mapEpoch)
    {
        s.pending=Json{};s.previous=Json{};s.points=Json::array();s.mode.clear();
        s.images.clear();s.drag={};s.epoch=mapEpoch;
    }
    s.scene=Editor::DesignScene();
    s.revision=Editor::Revision();
    try{s.securityActors=Editor::SecurityActors();}catch(const std::exception&){s.securityActors=Json::array();}
    try{s.lights=Editor::Lights();}catch(const std::exception&){s.lights=Json::array();}
    try{s.objectives=Editor::ObjectiveActors();}catch(const std::exception&){s.objectives=Json::array();}
    SecurityRefreshList(s);
    SceneRefreshList(s);
    try{s.grid=Editor::DesignGrid();}catch(const std::exception&){s.grid={64,64,64};}
    DesignBuildHulls(s);
    DesignFollowEditorMoves(s);
    DesignVerifyActive(s);
    SheetRefreshLater(s);
    InvalidateRect(s.canvas,nullptr,FALSE);
}
void DesignFit(DesignState& s)
{
    Vector lo{},hi{};bool first=true;
    auto add=[&](Vector p)
    {
        if(first){lo=hi=p;first=false;}
        else for(int i=0;i<3;++i){lo[i]=std::min(lo[i],p[i]);hi[i]=std::max(hi[i],p[i]);}
    };
    for(auto& actor:s.scene)if(DesignShows(s,actor))
    {
        add(actor.at("position").get<Vector>());
        for(auto& e:actor.at("edges")){add(e[0].get<Vector>());add(e[1].get<Vector>());}
    }
    const auto& data=DesignData(s);
    for(auto& reference:DesignReferencesInView(s,data))
        if(auto* image=DesignImage(s,reference.at("file")))
        {
            Vector origin=reference.at("origin");
            add(origin);
            origin[DesignHorizontal(s)]+=image->GetWidth()*reference.at("scale").get<double>();
            origin[DesignVertical(s)]-=image->GetHeight()*reference.at("scale").get<double>();
            add(origin);
        }
    if(!s.pending.is_null())
        for(auto& solid:Design::Geometry(s.pending))
            for(auto& f:solid.faces)
                for(auto& v:f)add(TransformPoint(v,s.frame));
    RECT r{};GetClientRect(s.canvas,&r);
    const int a=DesignHorizontal(s),b=DesignVertical(s);
    // The floor slider keeps a strip down the right edge, so a fitted map never
    // puts a wall under it.
    const double reserve=DesignLevels(s).empty()?0:48;
    s.zoom=std::clamp(std::min((r.right-100-reserve)/std::max(hi[a]-lo[a],256.0),(r.bottom-100)/std::max(hi[b]-lo[b],256.0)),.002,8.0);
    s.panX=(r.right-reserve)/2.0-(hi[a]+lo[a])/2*s.zoom;
    s.panY=r.bottom/2.0+(hi[b]+lo[b])/2*s.zoom;
    InvalidateRect(s.canvas,nullptr,FALSE);
}
// Preview handles sit at the middle of each editable face, in the axes the
// current view can actually show.
// Projected outlines for the current view, rebuilt when the map or the view
// plane changes rather than on every repaint.
void DesignBuildHulls(DesignState& s)
{
    s.hulls.assign(s.scene.size(),{});
    s.hullPlane=s.plane;
    s.hullRevision=s.revision;
    const int a=DesignHorizontal(s),b=DesignVertical(s);
    size_t budget=400000;
    for(size_t i=0;i<s.scene.size();++i)
    {
        const auto& edges=s.scene[i].at("edges");
        if(edges.empty() || edges.size()*2>budget)continue;
        budget-=edges.size()*2;
        std::vector<Design::Point> points;
        points.reserve(edges.size()*2);
        for(auto& edge:edges)
            for(int end=0;end<2;++end)
            {
                auto v=edge[end].get<Vector>();
                points.push_back({v[a],v[b]});
            }
        try{s.hulls[i]=Design::ConvexHull(std::move(points));}catch(const std::exception&){}
    }
}
// World bounds of a piece, used for snapping it against its neighbours.
Design::Extent DesignBoundsOf(const Json& spec,const Pose& pose)
{
    std::vector<Vector> points;
    for(auto& solid:Design::Geometry(spec))
        for(auto& face:solid.faces)
            for(auto& v:face)points.push_back(TransformPoint(v,pose));
    return Design::Bounds(points);
}
// Bounds of everything else in the map, so a drag can line up with it.
std::vector<Design::Extent> DesignNeighbourBounds(DesignState& s)
{
    std::set<std::string> mine;
    if(!s.previous.is_null())
        for(auto& member:s.previous.at("members"))mine.insert(member.at("path").get<std::string>());
    std::vector<Design::Extent> boxes;
    for(size_t i=0;i<s.scene.size();++i)
    {
        const auto& actor=s.scene[i];
        if(!DesignShows(s,actor) || actor.at("edges").empty())continue;
        if(mine.count(actor.at("path").get<std::string>()))continue;
        std::vector<Vector> points;
        for(auto& edge:actor.at("edges"))
            for(int end=0;end<2;++end)points.push_back(edge[end].get<Vector>());
        try{boxes.push_back(Design::Bounds(points));}catch(const std::exception&){}
        if(boxes.size()>=4096)break;
    }
    return boxes;
}
// Geometry snapping wins over the grid when a face is within a few pixels.
double DesignSnapTolerance(DesignState& s) { return s.snap?10/std::max(s.zoom,.0001):0; }
struct DesignHandle { int axis,side; Vector world; };
std::vector<DesignHandle> DesignHandles(DesignState& s)
{
    std::vector<DesignHandle> handles;
    if(s.pending.is_null())return handles;
    const double w=s.pending.at("width"),l=s.pending.at("length"),h=s.pending.at("height");
    const int a=DesignHorizontal(s),b=DesignVertical(s);
    auto add=[&](int axis,int side,Vector local)
    {
        // Local Z is the floor anchor; only its top face is a handle.
        if(axis==2 && side!=1)return;
        if(axis!=2 && axis!=a && axis!=b)return;
        if(axis==2 && b!=2)return;
        handles.push_back({axis,side,TransformPoint(local,s.frame)});
    };
    add(0,1,{w/2,0,h/2});add(0,-1,{-w/2,0,h/2});
    add(1,1,{0,l/2,h/2});add(1,-1,{0,-l/2,h/2});
    add(2,1,{0,0,h});
    // The rotate handle sits a little beyond the +Y edge in the top view and
    // turns the piece about its base; axis 3 marks it.
    if(b==1 && s.zoom>0)handles.push_back({3,0,TransformPoint({0,l/2+36/s.zoom,0},s.frame)});
    return handles;
}
// Turns the edited piece by whole degrees about its base, applying to a placed
// piece straight away.
bool DesignApplyEdit(DesignState& s,const std::string& summary);
void DesignInspectorRefresh(DesignState& s);
void DesignTurn(DesignState& s,double degrees)
{
    if(s.pending.is_null())throw std::runtime_error("Click a piece first, then press R to turn it.");
    const double current=s.frame.rotation[1]*360.0/65536;
    double next=std::fmod(current+degrees,360.0);
    if(next<0)next+=360;
    s.frame.rotation[1]=static_cast<int>(std::lround(next*65536/360))%65536;
    DesignInspectorRefresh(s);
    InvalidateRect(s.canvas,nullptr,FALSE);
    const auto turned="Turned to "+Design::Round(next)+" degrees. R turns 90 degrees, Shift+R the other way; drag the round handle to turn freely.";
    if(!s.previous.is_null())DesignApplyEdit(s,turned);
    else DesignWarn(s,turned);
}
// True when a point in the view lies inside the preview's footprint.
bool DesignInsidePreview(DesignState& s,const Vector& world)
{
    if(s.pending.is_null())return false;
    auto local=TransformPoint(world,s.frame,true);
    const double w=s.pending.at("width"),l=s.pending.at("length"),h=s.pending.at("height");
    return std::abs(local[0])<=w/2 && std::abs(local[1])<=l/2
        && (DesignVertical(s)!=2 || (local[2]>=-h*.1 && local[2]<=h*1.1));
}
double DesignPointDistance(const Gdiplus::PointF& p,double x,double y)
{
    return std::hypot(p.X-x,p.Y-y);
}
double DesignSegmentDistance(const Gdiplus::PointF& a,const Gdiplus::PointF& b,double x,double y)
{
    const double dx=b.X-a.X,dy=b.Y-a.Y,length=dx*dx+dy*dy;
    if(length<=0)return DesignPointDistance(a,x,y);
    double t=((x-a.X)*dx+(y-a.Y)*dy)/length;
    t=std::clamp(t,0.0,1.0);
    return std::hypot(a.X+t*dx-x,a.Y+t*dy-y);
}
void DesignPaint(DesignState& s,HDC dc)
{
    using namespace Gdiplus;
    RECT rect{};GetClientRect(s.canvas,&rect);
    Bitmap buffer(std::max(1L,rect.right),std::max(1L,rect.bottom));
    Graphics g(&buffer);
    // The background stands for this engine's solid space; carved rooms are
    // the light shapes cut out of it.
    g.Clear(Color(228,232,237));
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    const auto& data=DesignData(s);
    for(auto& reference:DesignReferencesInView(s,data))
    {
        auto* image=DesignImage(s,reference.at("file"));
        if(!image)continue;
        auto origin=DesignScreen(s,reference.at("origin").get<Vector>());
        const float scale=static_cast<float>(reference.at("scale").get<double>()*s.zoom);
        ColorMatrix matrix={1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,static_cast<float>(reference.at("opacity").get<double>()),0, 0,0,0,0,1};
        ImageAttributes attrs;attrs.SetColorMatrix(&matrix);
        g.DrawImage(image,RectF(origin.X,origin.Y,image->GetWidth()*scale,image->GetHeight()*scale),0,0,
                    static_cast<float>(image->GetWidth()),static_cast<float>(image->GetHeight()),UnitPixel,&attrs);
    }
    Pen grid(Color(40,100,110,120)),axis(Color(90,70,90,100));
    auto spacingFor=[&](int index)
    {
        double spacing=s.grid[index]>0?s.grid[index]:64;
        while(spacing*s.zoom<24)spacing*=2;
        while(spacing*s.zoom>150)spacing/=2;
        return spacing;
    };
    const double horizontal=spacingFor(DesignHorizontal(s))*s.zoom,vertical=spacingFor(DesignVertical(s))*s.zoom;
    for(double x=std::fmod(s.panX,horizontal);x<rect.right;x+=horizontal)
        g.DrawLine(&grid,static_cast<float>(x),0.f,static_cast<float>(x),static_cast<float>(rect.bottom));
    for(double y=std::fmod(s.panY,vertical);y<rect.bottom;y+=vertical)
        g.DrawLine(&grid,0.f,static_cast<float>(y),static_cast<float>(rect.right),static_cast<float>(y));
    g.DrawLine(&axis,static_cast<float>(s.panX),0.f,static_cast<float>(s.panX),static_cast<float>(rect.bottom));
    g.DrawLine(&axis,0.f,static_cast<float>(s.panY),static_cast<float>(rect.right),static_cast<float>(s.panY));
    Font font(L"Segoe UI",10);
    SolidBrush ink(Color(30,45,60));
    auto label=[&](const std::string& text,PointF p)
    {
        auto wide=std::wstring(text.begin(),text.end());
        g.DrawString(wide.c_str(),-1,&font,p,&ink);
    };
    auto line=[&](Pen& pen,Vector a,Vector b){g.DrawLine(&pen,DesignScreen(s,a),DesignScreen(s,b));};
    std::vector<std::pair<PointF,std::vector<std::string>>> selectedLabels;
    if(s.hullPlane!=s.plane || s.hullRevision!=s.revision)DesignBuildHulls(s);
    // Filled outlines first, so the plan reads as carved space, solid space,
    // portals and movers rather than a wireframe tangle.
    for(size_t i=0;i<s.scene.size() && i<s.hulls.size();++i)
    {
        const auto& actor=s.scene[i];
        if(s.hulls[i].size()<3 || !DesignShows(s,actor))continue;
        const int csg=actor.value("csg",0);
        Color fill=actor.value("portal",false)?Color(70,40,170,190)
            :actor.value("mover",false)?Color(60,220,150,40)
            :actor.value("volume",false)?Color(45,60,190,120)
            :csg==2?Color(150,255,255,255) // Carved, walkable space.
            :csg==1?Color(190,70,90,115)   // Added solid geometry.
            :Color(45,120,140,160);
        if(actor.at("selected").get<bool>())fill=Color(120,240,150,80);
        std::vector<PointF> outline;
        outline.reserve(s.hulls[i].size());
        for(const auto& point:s.hulls[i])
        {
            Vector world{};
            world[DesignHorizontal(s)]=point[0];
            world[DesignVertical(s)]=point[1];
            outline.push_back(DesignScreen(s,world));
        }
        SolidBrush brush(fill);
        g.FillPolygon(&brush,outline.data(),static_cast<INT>(outline.size()));
    }
    LightUnlitPaint(s,g);
    // A small padlock marks what is locked: point actors, and whole pieces.
    auto padlock=[&](float x,float y)
    {
        SolidBrush body(Color(235,85,90,100));
        Pen shackle(Color(235,85,90,100),1.5f);
        g.FillRectangle(&body,x-4,y-1,8.f,6.f);
        g.DrawArc(&shackle,x-2.5f,y-6,5.f,6.f,180.f,180.f);
    };
    if(s.overlays)for(auto& actor:s.scene)
    {
        if(!DesignShows(s,actor))continue;
        const auto at=DesignScreen(s,actor.at("position").get<Vector>());
        if(actor.contains("light"))
        {
            // Reach as a disc; brightness as its opacity.
            const double radius=actor.at("light").at("radius").get<double>()*s.zoom;
            const int brightness=std::clamp(actor.at("light").at("brightness").get<int>(),0,255);
            SolidBrush glow(Color(static_cast<BYTE>(20+brightness/6),255,225,110));
            Pen rim(Color(90,220,180,60),1);
            g.FillEllipse(&glow,static_cast<float>(at.X-radius),static_cast<float>(at.Y-radius),static_cast<float>(2*radius),static_cast<float>(2*radius));
            g.DrawEllipse(&rim,static_cast<float>(at.X-radius),static_cast<float>(at.Y-radius),static_cast<float>(2*radius),static_cast<float>(2*radius));
        }
        if(actor.contains("camera"))
        {
            // View cone along the camera's direction projected onto this view,
            // screen angles running clockwise.
            const double fov=std::clamp(actor.at("camera").at("fov").get<double>(),5.0,170.0);
            const auto direction=Security::BeamEnd({0,0,0},actor.at("rotation").get<Rotation>(),1.0);
            const double dh=direction[DesignHorizontal(s)],dv=direction[DesignVertical(s)];
            if(std::hypot(dh,dv)>0.05)
            {
                const double angle=std::atan2(-dv,dh)*180/3.14159265358979323846;
                const float reach=static_cast<float>(768*s.zoom);
                SolidBrush cone(Color(55,80,140,255));
                g.FillPie(&cone,at.X-reach,at.Y-reach,2*reach,2*reach,static_cast<float>(angle-fov/2),static_cast<float>(fov));
            }
        }
    }
    for(auto& actor:s.scene)
    {
        if(!DesignShows(s,actor))continue;
        const bool selected=actor.at("selected").get<bool>(),locked=actor.at("locked").get<bool>();
        Pen pen(selected?Color(230,90,30):locked?Color(160,165,175):Color(60,95,120),selected?2.f:1.f);
        for(auto& edge:actor.at("edges"))
        {
            if(!DesignOnFloor(s,edge[0].get<Vector>()[2],edge[1].get<Vector>()[2]))continue;
            line(pen,edge[0].get<Vector>(),edge[1].get<Vector>());
        }
        auto p=DesignScreen(s,actor.at("position").get<Vector>());
        if(actor.at("edges").empty())g.DrawEllipse(&pen,p.X-3,p.Y-3,6.f,6.f);
        if(locked && actor.at("edges").empty())padlock(p.X+8,p.Y-8);
        if(selected)
        {
            auto path=actor.at("path").get<std::string>();
            auto name=path.substr(path.find_last_of('.')+1);
            auto found=std::find_if(selectedLabels.begin(),selectedLabels.end(),[&](const auto& entry)
                {return std::abs(entry.first.X-p.X)<16 && std::abs(entry.first.Y-p.Y)<16;});
            if(found==selectedLabels.end())selectedLabels.push_back({p,{name}});
            else found->second.push_back(name);
        }
    }
    if(s.overlays)for(const auto& piece:DesignData(s).at("pieces"))
    {
        size_t live=0,lockedCount=0;
        for(auto& member:piece.at("members"))
            for(auto& actor:s.scene)
                if(actor.at("path")==member.at("path")){++live;if(actor.at("locked").get<bool>())++lockedCount;}
        if(live==0 || lockedCount<live)continue;
        const Vector position=piece.at("position");
        if(!DesignOnFloor(s,position[2],position[2]+piece.at("spec").value("height",0.0)))continue;
        const auto p=DesignScreen(s,position);
        padlock(p.X,p.Y);
    }
    for(auto& entry:selectedLabels)
        label(entry.second.size()==1?entry.second.front():std::to_string(entry.second.size())+" selected actors",{entry.first.X+5,entry.first.Y+8});
    if(!s.pending.is_null() && !s.previous.is_null())
    {
        // The piece as it stands today, under the live preview.
        Pen original(Color(110,150,90,60),1);
        original.SetDashStyle(DashStyleDash);
        for(auto& solid:Design::Geometry(s.previous.at("spec")))
            for(auto& f:solid.faces)
                for(size_t i=0;i<f.size();++i)
                {
                    Pose was{s.previous.at("position").get<Vector>(),s.previous.at("rotation").get<Rotation>()};
                    line(original,TransformPoint(f[i],was),TransformPoint(f[(i+1)%f.size()],was));
                }
    }
    if(!s.pending.is_null())
    {
        Pen pen(Color(0,145,105),2);
        for(auto& solid:Design::Geometry(s.pending))
            for(auto& f:solid.faces)
                for(size_t i=0;i<f.size();++i)
                    line(pen,TransformPoint(f[i],s.frame),TransformPoint(f[(i+1)%f.size()],s.frame));
        SolidBrush handleInk(Color(0,145,105));
        for(auto& handle:DesignHandles(s))
        {
            auto p=DesignScreen(s,handle.world);
            if(handle.axis==3)
            {
                // The rotate handle: a ring on a short stalk from the +Y edge.
                const auto edge=DesignScreen(s,TransformPoint({0,s.pending.at("length").get<double>()/2,0},s.frame));
                g.DrawLine(&pen,edge,p);
                Pen ring(Color(255,255,255,255),2);
                g.DrawEllipse(&ring,p.X-7,p.Y-7,14.f,14.f);
                g.FillEllipse(&handleInk,p.X-6,p.Y-6,12.f,12.f);
                continue;
            }
            g.FillRectangle(&handleInk,p.X-4,p.Y-4,8.f,8.f);
        }
        // While a placed piece is dragged, the rest of the selection shows
        // where it will land.
        if(s.drag.kind==DesignDrag::Kind::Move && s.drag.moved && !s.previous.is_null())
        {
            Vector delta{};
            for(int axis=0;axis<3;++axis)delta[axis]=s.frame.position[axis]-s.drag.frame.position[axis];
            Pen ghost(Color(170,0,145,105),1);
            ghost.SetDashStyle(DashStyleDash);
            for(const auto& actor:s.scene)
            {
                if(!actor.value("selected",false) || actor.at("locked").get<bool>() || !DesignShows(s,actor))continue;
                bool own=false;
                for(auto& member:s.previous.at("members"))if(member.at("path")==actor.at("path"))own=true;
                if(own)continue;
                auto shifted=[&](const Vector& v){Vector w=v;for(int axis=0;axis<3;++axis)w[axis]+=delta[axis];return w;};
                for(auto& edge:actor.at("edges"))line(ghost,shifted(edge[0].get<Vector>()),shifted(edge[1].get<Vector>()));
                if(actor.at("edges").empty()){const auto p=DesignScreen(s,shifted(actor.at("position").get<Vector>()));g.DrawEllipse(&ghost,p.X-4,p.Y-4,8.f,8.f);}
            }
        }
        // A new preview carries a tick to place it and a cross to discard it,
        // just outside its top-right corner.
        s.orbsShown=false;
        if(s.previous.is_null() && s.drag.kind==DesignDrag::Kind::None)
        {
            float right=-1e9f,top=1e9f;
            for(auto& solid:Design::Geometry(s.pending))
                for(auto& f:solid.faces)
                    for(auto& v:f)
                    {
                        const auto p=DesignScreen(s,TransformPoint(v,s.frame));
                        right=std::max(right,p.X);top=std::min(top,p.Y);
                    }
            if(right>-1e8f)
            {
                s.placeOrb={static_cast<LONG>(right+18),static_cast<LONG>(top-4)};
                s.discardOrb={static_cast<LONG>(right+46),static_cast<LONG>(top-4)};
                s.orbsShown=true;
                Pen outline(Color(255,255,255,255),2);
                SolidBrush tickFill(Color(235,0,150,90)),crossFill(Color(235,190,60,60));
                const float px=static_cast<float>(s.placeOrb.x),py=static_cast<float>(s.placeOrb.y);
                g.DrawEllipse(&outline,px-12,py-12,24.f,24.f);
                g.FillEllipse(&tickFill,px-11,py-11,22.f,22.f);
                Pen mark(Color(255,255,255,255),2.5f);
                g.DrawLine(&mark,px-6,py,px-2,py+5);
                g.DrawLine(&mark,px-2,py+5,px+7,py-5);
                const float dx=static_cast<float>(s.discardOrb.x),dy=static_cast<float>(s.discardOrb.y);
                g.DrawEllipse(&outline,dx-10,dy-10,20.f,20.f);
                g.FillEllipse(&crossFill,dx-9,dy-9,18.f,18.f);
                g.DrawLine(&mark,dx-5,dy-5,dx+5,dy+5);
                g.DrawLine(&mark,dx-5,dy+5,dx+5,dy-5);
                label("place (Enter) / discard (Esc)",{px+62,py-8});
            }
        }
        // Live dimensions, so a drag reads without measuring afterwards.
        const double width=s.pending.at("width"),length=s.pending.at("length"),height=s.pending.at("height");
        auto edgeLabel=[&](Vector local,const std::string& text)
        {
            auto at=DesignScreen(s,TransformPoint(local,s.frame));
            label(text,{at.X+6,at.Y-8});
        };
        if(DesignVertical(s)==2)
        {
            edgeLabel({0,0,height},Design::Round(DesignHorizontal(s)==0?width:length)+" wide");
            edgeLabel({0,0,height/2},Design::Round(height)+" high");
        }
        else
        {
            edgeLabel({0,-length/2,0},Design::Round(width)+" x "+Design::Round(length));
        }
        const auto name=s.pending.value("name",std::string("Preview"));
        label(s.previous.is_null()?name+" (new preview: drag, resize, then Place / Apply)"
                                  :name+" (editing this piece: changes replace its brushes)",DesignScreen(s,s.frame.position));
    }
    if(s.overlays)SecurityPaint(s,g,label);
    if(s.overlays)LightPaint(s,g,label);
    ObjectivePaint(s,g,label);
    ElementPaint(s,g,label);
    for(size_t index=0;index<data.at("guides").size();++index)
    {
        const auto& guide=data.at("guides")[index];
        Vector p=guide.at("position");
        const double width=guide.at("width"),height=guide.at("height");
        if(!DesignOnFloor(s,p[2],p[2]+height))continue;
        const bool held=s.drag.kind==DesignDrag::Kind::Guide && s.drag.index==index;
        Pen pen(held?Color(230,120,40):Color(190,80,170),2);
        // The guide is a body of the given width and height, standing on its
        // position, with a head to make the posture read at a glance.
        auto a=p,b=p;
        const int sideways=DesignHorizontal(s);
        a[sideways]-=width/2;b[sideways]+=width/2;
        if(s.plane==0){a[1]-=width/2;b[1]+=width/2;}else b[2]+=height;
        auto p1=DesignScreen(s,a),p2=DesignScreen(s,b);
        g.DrawRectangle(&pen,p1.X,p2.Y,p2.X-p1.X,p1.Y-p2.Y);
        if(s.plane!=0)
        {
            const double headRadius=std::min(width/2,height*.12);
            auto head=p;head[2]+=height-headRadius;
            auto hp=DesignScreen(s,head);
            const float radius=static_cast<float>(headRadius*s.zoom);
            g.DrawEllipse(&pen,hp.X-radius,hp.Y-radius,2*radius,2*radius);
            auto feet=p,shoulders=p;shoulders[2]+=height-2*headRadius;
            line(pen,feet,shoulders);
        }
        auto caption=DesignScreen(s,b);caption.Y-=20;
        label(guide.at("name").get<std::string>()+": "+Design::Round(width)+" x "+Design::Round(height)
            +(guide.value("posture",std::string())=="Crouching"?" crouching":""),caption);
    }
    auto annotations=data.at("annotations");
    if(!s.points.empty())
        annotations.push_back({{"name",s.annotationName},{"kind",s.mode},{"team",s.annotationTeam},
                               {"crouched",s.annotationCrouched},{"points",s.points}});
    for(auto& annotation:annotations)
    {
        Pen pen(annotation.at("kind")=="Measure"?Color(160,70,160):Color(35,145,70),2);
        const auto& points=annotation.at("points");
        bool visible=false;
        for(auto& point:points)if(DesignOnFloor(s,point.get<Vector>()[2],point.get<Vector>()[2]))visible=true;
        if(!visible)continue;
        double distance=0;
        for(size_t i=0;i<points.size();++i)
        {
            auto p=DesignScreen(s,points[i].get<Vector>());
            g.DrawEllipse(&pen,p.X-4,p.Y-4,8.f,8.f);
            if(i)
            {
                line(pen,points[i-1].get<Vector>(),points[i].get<Vector>());
                distance+=Design::Distance(points[i-1].get<Vector>(),points[i].get<Vector>());
            }
        }
        if(points.empty())continue;
        std::string caption=annotation.at("name").get<std::string>();
        if(points.size()>1)
            caption=annotation.at("kind")=="Measure"?caption+" ("+Design::Round(distance)+" units)":Design::RouteSummary(annotation,data);
        label(caption,DesignScreen(s,points.back().get<Vector>()));
    }
    if(!s.hoverPiece.is_null() && s.drag.kind==DesignDrag::Kind::None && s.mode.empty())
    {
        // A "+" on the wall under the cursor: click it to add a neighbour.
        SolidBrush badge(Color(210,0,145,105));
        SolidBrush ink(Color(255,255,255,255));
        const float x=static_cast<float>(s.hoverAt.x),y=static_cast<float>(s.hoverAt.y);
        Pen outline(Color(255,255,255,255),2);
        g.DrawEllipse(&outline,x-10,y-10,20.f,20.f);
        g.FillEllipse(&badge,x-9,y-9,18.f,18.f);
        g.FillRectangle(&ink,x-5,y-1.5f,10.f,3.f);
        g.FillRectangle(&ink,x-1.5f,y-5,3.f,10.f);
        label(s.hoverWall=="+Z"?"add above":s.hoverWall=="-Z"?"add below":"add on this wall",{x+12,y-8});
    }
    if(s.drag.kind==DesignDrag::Kind::Select && s.drag.moved)
    {
        Pen pen(Color(120,60,95,120),1);
        pen.SetDashStyle(DashStyleDash);
        g.DrawRectangle(&pen,static_cast<float>(std::min(s.drag.from.x,s.drag.to.x)),static_cast<float>(std::min(s.drag.from.y,s.drag.to.y)),
                        static_cast<float>(std::abs(s.drag.to.x-s.drag.from.x)),static_cast<float>(std::abs(s.drag.to.y-s.drag.from.y)));
    }
    DesignDepthShow(s);
    // The floor slider: "All" at the top, then each storey from the highest
    // down, with the handle on the one being shown.
    if(const auto layout=DesignSliderLayoutFor(s,rect);layout.count>0)
    {
        const auto& levels=DesignLevels(s);
        const float bottom=layout.top+layout.step*layout.count;
        const int current=DesignLevelIndex(s);
        Pen track(Color(150,90,100,115),3),tick(Color(170,90,100,115),1);
        g.DrawLine(&track,layout.x,layout.top,layout.x,bottom);
        SolidBrush dim(Color(200,70,80,95)),strong(Color(255,20,30,40));
        StringFormat rightAligned;
        rightAligned.SetAlignment(StringAlignmentFar);
        rightAligned.SetLineAlignment(StringAlignmentCenter);
        for(int i=0;i<=layout.count;++i)
        {
            const float y=layout.top+layout.step*i;
            g.DrawLine(&tick,layout.x-5,y,layout.x+5,y);
            const std::string text=i==0?"All":"Z "+Design::Round(levels[layout.count-i].base);
            const std::wstring wide(text.begin(),text.end());
            g.DrawString(wide.c_str(),-1,&font,RectF(layout.x-72,y-9,62,18),&rightAligned,i==current?&strong:&dim);
        }
        const float y=layout.top+layout.step*current;
        Pen outline(Color(255,255,255,255),2);
        SolidBrush handle(s.floorFilter?Color(235,0,120,200):Color(235,90,100,115));
        g.DrawEllipse(&outline,layout.x-7,y-7,14.f,14.f);
        g.FillEllipse(&handle,layout.x-6,y-6,12.f,12.f);
    }
    std::string caption=s.plane==0?"TOP XY":s.plane==1?"FRONT XZ":"SIDE YZ";
    caption+=s.snap?"  grid "+Design::Round(s.grid[DesignHorizontal(s)]):"  no snapping";
    if(s.floorFilter)caption+="  floor "+Design::Round(s.floorLow)+" to "+Design::Round(s.floorHigh);
    label(caption,{10,8});
    if(DesignGeometryStale(s))
    {
        // The banner is sized to its text, whatever the font metrics.
        const std::wstring text=L"Brushes changed since the last geometry build. Press B to rebuild.";
        RectF measured;
        g.MeasureString(text.c_str(),-1,&font,PointF(0,0),&measured);
        const float width=measured.Width+16,height=std::max(22.f,measured.Height+6);
        SolidBrush warn(Color(230,200,60,40));
        g.FillRectangle(&warn,static_cast<float>(rect.right-width-10),8.f,width,height);
        SolidBrush white(Color(255,255,255,255));
        g.DrawString(text.c_str(),-1,&font,PointF(static_cast<float>(rect.right-width-2),8+(height-measured.Height)/2),&white);
    }
    Graphics target(dc);
    target.DrawImage(&buffer,0,0);
}
Json DesignPiece(DesignState& s)
{
    auto selection=Editor::SelectedIdentities();
    const auto& data=DesignData(s);
    for(auto it=data.at("pieces").rbegin();it!=data.at("pieces").rend();++it)
        for(auto& selected:selection)
            for(auto& member:it->at("members"))
                if(selected.at("path")==member.at("path"))return *it;
    throw std::runtime_error("Select a generated blockout brush first.");
}
void DesignInspectorRefresh(DesignState& s);
// Editing a placed piece loads it as the live preview, drawn over the brushes
// it will replace. Applying regenerates them in one Undo step.
void DesignActivate(DesignState& s,const Json& piece)
{
    s.pending=piece.at("spec");
    s.frame={piece.at("position").get<Vector>(),piece.at("rotation").get<Rotation>()};
    s.previous=piece;
    s.drag={};
    DesignInspectorRefresh(s);
    // The outline must match the brushes from the first frame.
    DesignVerifyActive(s);
    InvalidateRect(s.canvas,nullptr,FALSE);
}
void DesignDeactivate(DesignState& s)
{
    s.pending=Json{};
    s.previous=Json{};
    s.drag={};
    DesignInspectorRefresh(s);
    InvalidateRect(s.canvas,nullptr,FALSE);
}
// The placed piece a brush belongs to, or null.
Json DesignPieceOf(DesignState& s,const Json& actor)
{
    const auto& data=DesignData(s);
    for(auto it=data.at("pieces").rbegin();it!=data.at("pieces").rend();++it)
        for(auto& member:it->at("members"))
            if(member.at("path")==actor.at("path"))return *it;
    return Json{};
}
// The placed piece a selection belongs to, or null.
Json DesignSelectedPiece(DesignState& s)
{
    const auto& data=DesignData(s);
    for(auto it=data.at("pieces").rbegin();it!=data.at("pieces").rend();++it)
        for(auto& actor:s.scene)
            if(actor.at("selected").get<bool>())
                for(auto& member:it->at("members"))
                    if(actor.at("path")==member.at("path"))return *it;
    return Json{};
}
// The preview must never disagree with the map. If the brushes of the piece
// being edited have gone, or have been moved or reshaped outside the toolkit,
// the preview goes back to describing what is actually there.
// The bounds a piece's brushes actually occupy, when all of them are in the map.
bool DesignActualBounds(DesignState& s,const Json& piece,Design::Extent& bounds)
{
    std::vector<Vector> points;
    size_t found=0;
    for(auto& member:piece.at("members"))
        for(auto& actor:s.scene)
            if(actor.at("path")==member.at("path"))
            {
                ++found;
                for(auto& edge:actor.at("edges"))
                    for(int end=0;end<2;++end)points.push_back(edge[end].get<Vector>());
            }
    if(found!=piece.at("members").size() || points.empty())return false;
    bounds=Design::Bounds(points);
    return true;
}
// Brushes moved in the editor's own viewports keep their piece: when a piece's
// brushes sit somewhere else but still have their shape, the library follows
// them, so the outline, the wall badges and the inspector stay with the brushes.
void DesignFollowEditorMoves(DesignState& s)
{
    if(s.drag.kind!=DesignDrag::Kind::None)return;
    Json data;
    try{data=DesignData(s);}catch(const std::exception&){return;}
    bool changed=false;
    for(auto& piece:data["pieces"])
    {
        try
        {
            Design::Extent actual;
            if(!DesignActualBounds(s,piece,actual))continue;
            const Pose pose{piece.at("position").get<Vector>(),piece.at("rotation").get<Rotation>()};
            const auto expected=DesignBoundsOf(piece.at("spec"),pose);
            Vector delta{};
            bool moved=false,reshaped=false;
            for(int axis=0;axis<3;++axis)
            {
                delta[axis]=actual.lo[axis]-expected.lo[axis];
                if(std::abs(delta[axis])>.05)moved=true;
                if(std::abs((actual.hi[axis]-actual.lo[axis])-(expected.hi[axis]-expected.lo[axis]))>.05)reshaped=true;
            }
            if(!moved || reshaped)continue;
            auto position=pose.position;
            for(int axis=0;axis<3;++axis)position[axis]+=delta[axis];
            s.followed.push_back({{"name",piece.at("spec").value("name",std::string())},{"delta",delta},{"actualLo",actual.lo},{"actualHi",actual.hi},
                                  {"expectedLo",expected.lo},{"expectedHi",expected.hi},{"from",pose.position}});
            piece["position"]=position;
            if(!s.previous.is_null() && s.previous.at("members")==piece.at("members"))
            {
                s.previous["position"]=position;
                s.frame.position=position;
                DesignInspectorRefresh(s);
            }
            changed=true;
        }
        catch(const std::exception&) { /* Unreadable geometry is left alone. */ }
    }
    if(!changed)return;
    try{DesignSave(s,data,false);}catch(const std::exception&){}
    DesignStatus(s,"Brushes moved in the editor: their pieces followed them.");
}
void DesignVerifyActive(DesignState& s)
{
    if(s.previous.is_null() || s.drag.kind!=DesignDrag::Kind::None)return;
    std::vector<Vector> points;
    size_t found=0;
    for(auto& member:s.previous.at("members"))
        for(auto& actor:s.scene)
            if(actor.at("path")==member.at("path"))
            {
                ++found;
                for(auto& edge:actor.at("edges"))
                    for(int end=0;end<2;++end)points.push_back(edge[end].get<Vector>());
            }
    if(found!=s.previous.at("members").size() || points.empty())
    {
        DesignDeactivate(s);
        DesignStatus(s,"The piece being edited is no longer in the map, so the preview was cleared.");
        return;
    }
    try
    {
        const auto actual=Design::Bounds(points);
        const auto expected=DesignBoundsOf(s.previous.at("spec"),{s.previous.at("position").get<Vector>(),s.previous.at("rotation").get<Rotation>()});
        double drift=0,reshape=0;
        for(int axis=0;axis<3;++axis)
        {
            drift=std::max({drift,std::abs(actual.lo[axis]-expected.lo[axis]),std::abs(actual.hi[axis]-expected.hi[axis])});
            reshape=std::max(reshape,std::abs((actual.hi[axis]-actual.lo[axis])-(expected.hi[axis]-expected.lo[axis])));
        }
        if(drift<=.05)return;
        if(reshape<=.05)
        {
            // Same shape, elsewhere: the brushes are the truth, so the piece
            // and its outline move to them.
            auto position=s.previous.at("position").get<Vector>();
            for(int axis=0;axis<3;++axis)position[axis]+=actual.lo[axis]-expected.lo[axis];
            Json data=DesignData(s);
            for(auto& piece:data["pieces"])if(piece.at("members")==s.previous.at("members"))piece["position"]=position;
            s.previous["position"]=position;
            s.frame.position=position;
            try{DesignSave(s,data,false);}catch(const std::exception&){}
            DesignInspectorRefresh(s);
            InvalidateRect(s.canvas,nullptr,FALSE);
            return;
        }
        auto piece=s.previous;
        DesignDeactivate(s);
        auto span=[&](const Design::Extent& e){return Design::Round(e.lo[0],2)+".."+Design::Round(e.hi[0],2)+" x "+Design::Round(e.lo[1],2)+".."+Design::Round(e.hi[1],2)+" x "+Design::Round(e.lo[2],2)+".."+Design::Round(e.hi[2],2);};
        DesignStatus(s,"\""+piece.at("spec").value("name",std::string("This piece"))+"\" differs from its brushes by "+Design::Round(drift,2)
            +" units (brushes "+span(actual)+"; piece "+span(expected)+"), so its preview was dropped. Click it again to edit where the brushes are.");
    }
    catch(const std::exception&) { /* Unreadable geometry is left to the next refresh. */ }
}
// What moves along with some actors: the rest of their group, and every other
// selected actor, unlocked and not among the actors themselves. So a box
// selection of several pieces drags as one.
Json DesignGroupOthers(DesignState& s,const Json& moved)
{
    Json others=Json::array();
    if(!moved.is_array() || moved.empty())return others;
    auto own=[&](const std::string& path){for(auto& done:moved)if(done.at("path")==path)return true;return false;};
    auto add=[&](const Json& identity)
    {
        const auto path=identity.at("path").get<std::string>();
        if(own(path) || DesignLockedPath(s,path))return;
        for(auto& o:others)if(o.at("path")==path)return;
        others.push_back(Json{{"path",path},{"class",identity.at("class")}});
    };
    if(const auto group=SceneGroupOf(s,moved[0].at("path").get<std::string>());!group.empty())
        for(auto& m:SceneGroupMembers(s,group))add(m);
    for(const auto& actor:s.scene)if(actor.value("selected",false) && DesignShows(s,actor))add(actor);
    return others;
}
// A selected piece whose footprint contains a point of the view: dragging
// inside it moves it, and the rest of the selection with it.
Json DesignSelectedPieceContaining(DesignState& s,const Vector& world)
{
    const auto& data=DesignData(s);
    const int a=DesignHorizontal(s),b=DesignVertical(s);
    // The editor's selection as it is now: the panel's copy may predate a
    // change made elsewhere.
    Json selection;
    try{selection=Editor::SelectedIdentities();}catch(const std::exception&){return Json{};}
    if(!selection.is_array() || selection.empty())return Json{};
    auto selectedNow=[&](const std::string& path){for(auto& id:selection)if(id.at("path")==path)return true;return false;};
    for(auto it=data.at("pieces").rbegin();it!=data.at("pieces").rend();++it)
    {
        size_t live=0,selected=0;
        for(auto& member:it->at("members"))
            for(auto& actor:s.scene)
                if(actor.at("path")==member.at("path"))
                {
                    ++live;
                    if(selectedNow(member.at("path").get<std::string>()) && !actor.at("locked").get<bool>())++selected;
                }
        if(live==0 || selected<live)continue;
        try
        {
            const auto bounds=DesignBoundsOf(it->at("spec"),{it->at("position").get<Vector>(),it->at("rotation").get<Rotation>()});
            if(world[a]>=bounds.lo[a] && world[a]<=bounds.hi[a] && world[b]>=bounds.lo[b] && world[b]<=bounds.hi[b])return *it;
        }
        catch(const std::exception&) { /* Not a piece the view can place. */ }
    }
    return Json{};
}
// What moves when the edited piece moves: its brushes and, for a pure move,
// the doorways cut into it.
std::vector<size_t> DesignAttachedDoorways(DesignState& s,const Json& piece);
Json DesignMovedMembers(DesignState& s)
{
    Json moved=Json::array();
    if(s.previous.is_null())return moved;
    for(auto& m:s.previous.at("members"))moved.push_back(m);
    const bool moveOnly=s.pending==s.previous.at("spec") && s.frame.rotation==s.previous.at("rotation").get<Rotation>();
    if(moveOnly)
    {
        const auto& pieces=DesignData(s).at("pieces");
        for(size_t index:DesignAttachedDoorways(s,s.previous))
            if(index<pieces.size())for(auto& m:pieces[index].at("members"))moved.push_back(m);
    }
    return moved;
}
// Plans the group to follow the edited piece by delta; the next apply moves
// them in its own Undo step.
void DesignPlanFollow(DesignState& s,const Vector& delta)
{
    s.followers=DesignGroupOthers(s,DesignMovedMembers(s));
    s.followDelta=delta;
}
void DesignApply(DesignState& s)
{
    if(s.pending.is_null())throw std::runtime_error("Create or edit a preview first.");
    Json data=DesignData(s);
    const bool editing=!s.previous.is_null();
    const Json followers=s.followers;
    s.followers=Json::array();
    auto piece=Editor::DesignBlockout(s.pending,s.frame,s.previous,followers,s.followDelta);
    if(editing)for(auto& layer:data["layers"])
    {
        bool included=false;
        for(auto& member:layer["members"])
            for(auto& previous:s.previous["members"])
                if(member.at("path")==previous.at("path"))included=true;
        // Keep historical identities as well so native Undo can restore the
        // old brushes without losing their logical layer membership.
        if(included)for(auto& member:piece["members"])layer["members"].push_back(member);
    }
    data["pieces"].push_back(piece);
    try{DesignSave(s,data,false);}catch(...){Editor::Exec("TRANSACTION UNDO");throw;}
    DesignRefresh(s);
    // Stay on the piece so it can be adjusted again straight away, as the
    // library has it after the refresh.
    for(const auto& stored:DesignData(s).at("pieces"))if(stored.at("members")==piece.at("members"))piece=stored;
    DesignActivate(s,piece);
}
// Applies an edit to the piece under the cursor, and on failure puts the
// preview back where the map actually is rather than leaving the two adrift.
// Doorways cut into a piece's walls belong to it: when the piece is moved they
// come along, in the same Undo step.
std::vector<size_t> DesignAttachedDoorways(DesignState& s,const Json& piece)
{
    std::vector<size_t> attached;
    const auto kind=piece.at("spec").at("kind").get<std::string>();
    if(kind!="Room" && kind!="Corridor" && !Design::Crouching(kind))return attached;
    const auto& pieces=DesignData(s).at("pieces");
    Design::Extent host{};
    try{host=Design::PieceExtent(piece.at("spec"),{piece.at("position").get<Vector>(),piece.at("rotation").get<Rotation>()});}
    catch(const std::exception&){return attached;}
    const double margin=piece.at("spec").value("thickness",16.0)+1;
    for(size_t i=0;i<pieces.size();++i)
    {
        const auto& other=pieces[i];
        if(other.at("spec").at("kind")!="Doorway")continue;
        bool live=!other.at("members").empty();
        for(auto& member:other.at("members"))
        {
            bool found=false;
            for(auto& actor:s.scene)if(actor.at("path")==member.at("path"))found=true;
            live=live && found;
        }
        if(!live)continue;
        try
        {
            const auto extent=Design::PieceExtent(other.at("spec"),{other.at("position").get<Vector>(),other.at("rotation").get<Rotation>()});
            if(Design::Overlaps(host,extent,margin))attached.push_back(i);
        }
        catch(const std::exception&){}
    }
    return attached;
}
// A pure move of a placed piece carries its doorways with it.
void DesignApplyMove(DesignState& s,const std::vector<size_t>& attached)
{
    Json data=DesignData(s);
    Vector delta{};
    for(int axis=0;axis<3;++axis)delta[axis]=s.frame.position[axis]-s.previous.at("position").get<Vector>()[axis];
    Json items=Json::array({{{"spec",s.pending},{"position",s.frame.position},{"rotation",s.frame.rotation},{"previous",s.previous}}});
    for(auto index:attached)
    {
        const auto& doorway=data.at("pieces")[index];
        auto position=doorway.at("position").get<Vector>();
        for(int axis=0;axis<3;++axis)position[axis]+=delta[axis];
        items.push_back({{"spec",doorway.at("spec")},{"position",position},{"rotation",doorway.at("rotation")},{"previous",doorway}});
    }
    const Json followers=s.followers;
    s.followers=Json::array();
    auto pieces=Editor::DesignBlockoutBatch(items,followers,s.followDelta);
    for(auto& piece:pieces)data["pieces"].push_back(piece);
    try{DesignSave(s,data,false);}catch(...){Editor::Exec("TRANSACTION UNDO");throw;}
    DesignRefresh(s);
    Json moved=pieces[0];
    for(const auto& stored:DesignData(s).at("pieces"))if(stored.at("members")==moved.at("members"))moved=stored;
    DesignActivate(s,moved);
}
bool DesignApplyEdit(DesignState& s,const std::string& summary)
{
    auto piece=s.previous;
    try
    {
        const bool moveOnly=!piece.is_null() && s.pending==piece.at("spec") && s.frame.rotation==piece.at("rotation").get<Rotation>();
        const auto attached=moveOnly?DesignAttachedDoorways(s,piece):std::vector<size_t>{};
        if(!attached.empty())
        {
            DesignApplyMove(s,attached);
            if(s.pending.is_null())return true; // Verification reported a drift and dropped the preview.
            DesignWarn(s,summary+" Moved with "+std::to_string(attached.size())+" doorway(s) in one Undo step; rebuild geometry when finished.");
            return true;
        }
        DesignApply(s);
        if(s.pending.is_null())return true;
        DesignWarn(s,summary+" Brushes replaced in one Undo step; rebuild geometry when finished.");
        return true;
    }
    catch(const std::exception& e)
    {
        if(!piece.is_null())DesignActivate(s,piece);
        else DesignDeactivate(s);
        DesignStatus(s,std::string("The map was not changed: ")+e.what());
        return false;
    }
}
// Movement limits prefer the map's saved values, then pawn class defaults.
Json DesignMovementDefaults(DesignState& s)
{
    auto movement=Design::Movement(DesignData(s));
    if(DesignData(s).contains("movement"))return movement;
    try
    {
        double width=0,height=0;
        for(auto& profile:Editor::DesignClearances())
        {
            const auto name=profile.at("name").get<std::string>();
            if(name.find("standing")==std::string::npos)continue;
            const auto team=profile.value("team",std::string());
            // Openings must clear the largest pawn, so keep the widest and tallest;
            // collision cylinders understate the player, so the defaults are a floor.
            width=std::max({width,profile.at("width").get<double>(),movement.at("width").get<double>()});
            height=std::max({height,profile.at("height").get<double>(),movement.at("height").get<double>()});
            if(profile.contains("stepHeight"))movement["stepHeight"]=profile.at("stepHeight");
            if(profile.contains("speed"))movement[team=="Merc"?"mercSpeed":"spySpeed"]=profile.at("speed");
        }
        if(width>0){movement["width"]=width;movement["height"]=height;}
    }
    catch(const std::exception&) { /* Class defaults are optional. */ }
    return Design::Movement(Json{{"movement",movement}});
}
void DesignWarn(DesignState& s,const std::string& prefix)
{
    if(s.pending.is_null()){DesignStatus(s,prefix);return;}
    auto warnings=Design::TraversalWarnings(s.pending,DesignData(s));
    std::string text=prefix;
    for(auto& warning:warnings)text+="\r\n"+warning;
    DesignStatus(s,text);
}
// Inspector: the live fields for whatever is being previewed or edited.
const char* const kDesignShapes[]={"Room","Corridor","Vent","Crawlway","Doorway","Stairs","Stairs L","Stairs U","Spiral","Ramp","Platform"};
const char* const kDesignConstructions[]={"Carve","Shell"};
void DesignInspectorRefresh(DesignState& s)
{
    if(!s.window)return;
    s.writingInspector=true;
    const bool active=!s.pending.is_null();
    const bool editing=active && !s.previous.is_null();
    if(active && !s.sheet.kind.empty())
    {
        // A preview takes the panel; the sheet's subject is let go.
        s.sheet.kind.clear();s.sheet.subject=Json{};s.sheet.keys.clear();s.sheet.shown.clear();
        SheetDestroyControls(s);
        SheetPieceControls(s,true);
    }
    else if(!active && !s.sheet.kind.empty())
    {
        SheetPieceControls(s,false);
        s.writingInspector=false;
        return;
    }
    SetWindowTextA(GetDlgItem(s.window,DInspectorTitle),
        editing?"Selected piece (changes replace its brushes)":active?"New preview (Place / Apply to create)":"No preview: use New blockout..., or click a piece, light, device, reference or route point.");
    for(int id=DName;id<=DDiscard;++id)if(auto control=GetDlgItem(s.window,id))EnableWindow(control,active);
    if(!active)
    {
        for(int id:{DName,DWidth,DLength,DHeight,DThickness,DSteps,DPositionX,DPositionY,DPositionZ,DYaw})
            if(auto control=GetDlgItem(s.window,id))SetWindowTextA(control,"");
        s.writingInspector=false;
        return;
    }
    auto text=[&](int id,const std::string& value){SetWindowTextA(GetDlgItem(s.window,id),value.c_str());};
    auto number=[&](int id,double value){text(id,Design::Round(value,std::abs(value-std::round(value))<.005?0:2));};
    text(DName,s.pending.value("name",std::string("Blockout")));
    auto choose=[&](int id,const char* const* values,size_t count,const std::string& current)
    {
        for(size_t i=0;i<count;++i)if(current==values[i])SendDlgItemMessage(s.window,id,CB_SETCURSEL,i,0);
    };
    choose(DShape,kDesignShapes,std::size(kDesignShapes),s.pending.at("kind"));
    choose(DConstruction,kDesignConstructions,std::size(kDesignConstructions),s.pending.value("construction",std::string("Shell")));
    number(DWidth,s.pending.at("width"));
    number(DLength,s.pending.at("length"));
    number(DHeight,s.pending.at("height"));
    number(DThickness,s.pending.value("thickness",16.0));
    number(DSteps,s.pending.value("steps",8));
    SendDlgItemMessage(s.window,DCeiling,BM_SETCHECK,s.pending.value("ceiling",true)?BST_CHECKED:BST_UNCHECKED,0);
    SendDlgItemMessage(s.window,DPortal,BM_SETCHECK,s.pending.value("portal",false)?BST_CHECKED:BST_UNCHECKED,0);
    for(int axis=0;axis<3;++axis)number(DPositionX+axis,s.frame.position[axis]);
    number(DYaw,s.frame.rotation[1]*360.0/65536);
    s.writingInspector=false;
}
// Reads the fields back into the preview. Invalid entries are reported and the
// field is restored, so a half-typed number never reaches the map.
void DesignInspectorRead(DesignState& s)
{
    if(s.writingInspector || s.pending.is_null())return;
    auto value=[&](int id){return Text(GetDlgItem(s.window,id));};
    Json spec=s.pending;
    Pose frame=s.frame;
    spec["name"]=value(DName).substr(0,120);
    const auto shape=SendDlgItemMessage(s.window,DShape,CB_GETCURSEL,0,0);
    if(shape>=0 && shape<static_cast<LRESULT>(std::size(kDesignShapes)))spec["kind"]=kDesignShapes[shape];
    const auto construction=SendDlgItemMessage(s.window,DConstruction,CB_GETCURSEL,0,0);
    if(construction>=0 && construction<static_cast<LRESULT>(std::size(kDesignConstructions)))spec["construction"]=kDesignConstructions[construction];
    spec["width"]=Design::Number(value(DWidth),1,65536);
    spec["length"]=Design::Number(value(DLength),1,65536);
    spec["height"]=Design::Number(value(DHeight),1,65536);
    spec["thickness"]=Design::Number(value(DThickness),1,65536);
    const double steps=Design::Number(value(DSteps),1,128);
    if(steps!=std::floor(steps))throw std::runtime_error("Stair count must be a whole number.");
    spec["steps"]=static_cast<int>(steps);
    spec["ceiling"]=SendDlgItemMessage(s.window,DCeiling,BM_GETCHECK,0,0)==BST_CHECKED;
    spec["portal"]=SendDlgItemMessage(s.window,DPortal,BM_GETCHECK,0,0)==BST_CHECKED;
    for(int axis=0;axis<3;++axis)frame.position[axis]=Design::Number(value(DPositionX+axis));
    frame.rotation[1]=static_cast<int>(std::fmod(Design::Number(value(DYaw)),360.0)*65536/360);
    // Switching to or from a crouch shape brings its height with it, so a vent
    // is vent-sized without retyping.
    const auto was=s.pending.at("kind").get<std::string>(),now=spec.at("kind").get<std::string>();
    if(now!=was)
    {
        const auto movement=Design::Movement(DesignData(s));
        const double clearance=Design::ClearanceHeight(now,movement);
        const bool crouching=Design::Crouching(now);
        if(crouching && spec.at("height").get<double>()>clearance)spec["height"]=clearance;
        else if(!crouching && Design::Crouching(was) && spec.at("height").get<double>()<clearance)spec["height"]=clearance;
    }
    Design::Geometry(spec);
    Design::CheckVector(frame.position);
    const bool changed=spec!=s.pending || frame.position!=s.frame.position || frame.rotation!=s.frame.rotation;
    s.pending=spec;
    s.frame=frame;
    if(!changed)return;
    DesignInspectorRefresh(s);
    InvalidateRect(s.canvas,nullptr,FALSE);
    // A placed piece follows its fields straight away; a new preview waits for
    // Place / Apply.
    if(!s.previous.is_null())DesignApplyEdit(s,"Field applied.");
    else DesignWarn(s,"Preview updated. Place / Apply creates the brushes.");
}
// New blockouts start from a preview at the builder brush; the inspector
// edits it live, so no form stands between wanting a room and seeing one.
void DesignNewBlockout(DesignState& s)
{
    Json spec={{"kind","Room"},{"width",1024},{"length",1024},{"height",256},{"thickness",16},
               {"steps",8},{"ceiling",true},{"portal",false},{"construction","Carve"},{"name","Blockout"}};
    Pose frame=Editor::BuilderPose();
    if(s.snap)frame.position=Design::SnapVector(frame.position,s.grid);
    Design::Geometry(spec);
    s.pending=spec;
    s.previous=Json{};
    s.frame=frame;
    s.drag={};
    DesignInspectorRefresh(s);
    DesignFit(s);
    DesignWarn(s,"Green preview: drag it, drag a square to resize, or edit the fields. Place / Apply creates the brushes.");
}
// Editing a placed piece needs no dialog: it becomes the live preview.
void DesignEditSelected(DesignState& s)
{
    DesignActivate(s,DesignPiece(s));
    DesignWarn(s,"Editing the selected piece. Drag or edit its fields; each change replaces its brushes in one Undo step.");
}
void DesignDoorwayForm(DesignState& s)
{
    auto piece=DesignPiece(s);
    auto room=piece.at("spec");
    if(room.at("kind")!="Room" && room.at("kind")!="Corridor")throw std::runtime_error("Select a room or corridor blockout brush first.");
    auto movement=Design::Movement(DesignData(s));
    Json doorway={{"kind","Doorway"},{"construction",room.value("construction",std::string("Shell"))},
        {"width",movement.at("width").get<double>()+16},{"length",room.value("thickness",16.0)},
        {"height",movement.at("height").get<double>()+16},{"thickness",room.value("thickness",16.0)},{"steps",8},{"ceiling",true}};
    std::vector<InputField> fields={
        {"Name",room.value("name",std::string("Blockout"))+" doorway",{}},
        {"Wall (room's local axes)","+Y",{"+Y","-Y","+X","-X"}},
        {"Offset along the wall","0",{}},
        {"Width",doorway["width"].dump(),{}},
        {"Height",doorway["height"].dump(),{}},
        {"Sill height above the floor","0",{}},
        {"Depth through the wall",doorway["length"].dump(),{}},
        {"Zone portal","Yes",{"Yes","No"}}};
    if(!Ask(s.window,"Doorway in Selected Room",fields))return;
    doorway["name"]=fields[0].value;
    doorway["width"]=Design::Number(fields[3].value,1,65536);
    doorway["height"]=Design::Number(fields[4].value,1,65536);
    doorway["length"]=Design::Number(fields[6].value,1,65536);
    doorway["portal"]=fields[7].value=="Yes";
    Pose pose=Design::WallDoorway(room,{piece.at("position").get<Vector>(),piece.at("rotation").get<Rotation>()},
                                  doorway,fields[1].value,Design::Number(fields[2].value),Design::Number(fields[5].value,0,65536));
    s.pending=doorway;s.previous=Json{};s.frame=pose;
    DesignFit(s);
    DesignWarn(s,"Doorway preview placed in the wall. Place / Apply creates it, then rebuild geometry.");
}
void DesignMovementForm(DesignState& s)
{
    auto movement=DesignMovementDefaults(s);
    std::vector<InputField> fields={
        {"Step-up height",movement["stepHeight"].dump(),{}},
        {"Walkable slope (degrees)",movement["slope"].dump(),{}},
        {"Standing clearance width",movement["width"].dump(),{}},
        {"Standing clearance height",movement["height"].dump(),{}},
        {"Crouching clearance width",movement["crouchWidth"].dump(),{}},
        {"Crouching clearance height (vents)",movement["crouchHeight"].dump(),{}},
        {"Crawl clearance height (crawlways)",Design::Round(movement.value("crawlHeight",105.0)),{}},
        {"Crouched speed (fraction)",movement["crouchFactor"].dump(),{}},
        {"Climb speed (units per second)",movement["climbSpeed"].dump(),{}},
        {"Spy speed (units per second)",movement["spySpeed"].dump(),{}},
        {"Merc speed (units per second)",movement["mercSpeed"].dump(),{}}};
    if(!Ask(s.window,"Movement Limits (verify with movement tests)",fields))return;
    Json next={{"stepHeight",Design::Number(fields[0].value,1,10000)},{"slope",Design::Number(fields[1].value,1,89)},
        {"width",Design::Number(fields[2].value,1,10000)},{"height",Design::Number(fields[3].value,1,10000)},
        {"crouchWidth",Design::Number(fields[4].value,1,10000)},{"crouchHeight",Design::Number(fields[5].value,1,10000)},
        {"crawlHeight",Design::Number(fields[6].value,1,10000)},
        {"crouchFactor",Design::Number(fields[7].value,.05,1)},{"climbSpeed",Design::Number(fields[8].value,1,100000)},
        {"spySpeed",Design::Number(fields[9].value,1,100000)},{"mercSpeed",Design::Number(fields[10].value,1,100000)}};
    Json data=DesignData(s);
    data["movement"]=Design::Movement(Json{{"movement",next}});
    DesignSave(s,data);
    DesignStatus(s,"Movement limits saved. Blockout previews and route times use them. They are planning values: confirm them in game.");
}
// Workspace layers are joined by any native Groups already in the map, so a
// shared .sdc keeps its layers without the workspace file.
std::vector<std::pair<std::string,Json>> DesignLayers(DesignState& s,const Json& data)
{
    std::vector<std::pair<std::string,Json>> layers;
    for(auto& layer:data.at("layers"))layers.push_back({layer.at("name"),layer.at("members")});
    std::map<std::string,Json> native;
    for(auto& actor:s.scene)
        for(auto& name:Design::GroupNames(actor.value("group",std::string("None"))))
        {
            auto known=std::find_if(layers.begin(),layers.end(),[&](const auto& layer){return Fold(layer.first)==Fold(name);});
            if(known!=layers.end())continue;
            if(!native.count(name))native[name]=Json::array();
            native[name].push_back(Json{{"path",actor.at("path")},{"class",actor.at("class")}});
        }
    for(auto& [name,members]:native)layers.push_back({name,members});
    return layers;
}
void DesignCommand(DesignState& s,int id)
{
    Sync();
    if(s.epoch!=mapEpoch)
    {
        DesignRefresh(s);
        if(id!=DRefresh)throw std::runtime_error("The map changed. Try the action again in the current map.");
    }
    Json data=DesignData(s);
    if(id==DRefresh){DesignRefresh(s);return;}
    if(id==DPlane)
    {
        s.plane=static_cast<int>(SendMessage(GetDlgItem(s.window,DPlane),CB_GETCURSEL,0,0));
        s.points=Json::array();s.mode.clear();DesignRefresh(s);DesignFit(s);DesignDepthShow(s);return;
    }
    if(id==DFit){DesignFit(s);return;}
    if(id==DSnap)
    {
        s.snap=SendDlgItemMessage(s.window,DSnap,BM_GETCHECK,0,0)==BST_CHECKED;
        InvalidateRect(s.canvas,nullptr,FALSE);
        DesignStatus(s,s.snap?"Clicks and drags snap to the editor's grid. Change it with Ctrl + mouse wheel over a viewport.":"Snapping off. Clicks and drags use exact positions.");
        return;
    }
    if(id==DFloor)
    {
        std::vector<InputField> f={{"Show floor from Z",Design::Round(s.floorLow),{}},{"to Z",Design::Round(s.floorHigh),{}},{"Filter","On",{"On","Off"}}};
        if(!Ask(s.window,"Floor Height Filter",f))return;
        const double low=Design::Number(f[0].value),high=Design::Number(f[1].value);
        if(high<=low)throw std::runtime_error("Enter a height range with the upper value above the lower one.");
        s.floorLow=low;s.floorHigh=high;s.floorFilter=f[2].value=="On";
        DesignFit(s);
        DesignStatus(s,s.floorFilter?"Showing actors, brush edges, guides and annotations between those heights. The slider on the right steps between storeys; right-click it for this dialog.":"Floor filter off.");
        return;
    }
    if(id==DDepth){DesignDepthRead(s);return;}
    if(id==DCtxSliderAll){DesignSetLevel(s,0);return;}
    if(id==DUndo || id==DRedo)
    {
        // Workspace edits first, when nothing in the map changed after them.
        auto& from=id==DUndo?s.workspaceUndo:s.workspaceRedo;
        auto& to=id==DUndo?s.workspaceRedo:s.workspaceUndo;
        if(!from.empty() && Editor::Revision()==s.workspaceRevision)
        {
            Json restored=from.back();
            from.pop_back();
            to.push_back(s.data);
            DesignSave(s,restored,false);
            s.workspaceRevision=Editor::Revision();
            s.images.clear();
            InvalidateRect(s.canvas,nullptr,FALSE);
            DesignStatus(s,id==DUndo?"Undid the last annotation, reference, guide or layer change.":"Redid the last workspace change.");
            return;
        }
        // Otherwise the editor's own history: blockout edits, layer changes and
        // native actor moves all live in it.
        if(!Editor::Exec(id==DUndo?"TRANSACTION UNDO":"TRANSACTION REDO"))
            throw std::runtime_error(id==DUndo?"Nothing left to undo.":"Nothing to redo.");
        DesignRefresh(s);
        DesignInspectorRefresh(s);
        DesignStatus(s,id==DUndo?"Undid the last editor change. Rebuild geometry if it moved BSP brushes."
                                :"Redid the last editor change. Rebuild geometry if it moved BSP brushes.");
        return;
    }
    if(id==DBuild)
    {
        Editor::BuildGeometry();
        s.builds=Editor::GeometryBuilds();
        s.builtRevision=Editor::Revision();
        DesignRefresh(s);
        DesignStatus(s,"Geometry rebuilt. Lighting is not rebuilt here; use Build > Rebuild Lighting when the layout settles.");
        return;
    }
    if(id==DUnlit)
    {
        s.unlit=!s.unlit;
        CheckMenuItem(GetMenu(s.window),DUnlit,MF_BYCOMMAND|(s.unlit?MF_CHECKED:MF_UNCHECKED));
        InvalidateRect(s.canvas,nullptr,FALSE);
        DesignStatus(s,s.unlit?"Shading the brushes that no light reaches at this view's depth. A light's reach shrinks with its height above the floor.":"Unlit shading off.");
        return;
    }
    if(id==DOverlays)
    {
        s.overlays=SendDlgItemMessage(s.window,DOverlays,BM_GETCHECK,0,0)==BST_CHECKED;
        InvalidateRect(s.canvas,nullptr,FALSE);
        DesignStatus(s,s.overlays?"Showing light reach and camera view cones.":"Overlays hidden.");
        return;
    }
    if(id==DCheck){DesignCheck(s);return;}
    if(id==DSecurity){SecurityOpen(s);return;}
    if(id==DScene){SceneOpen(s);return;}
    if(id==DKeys){DesignKeys(s);return;}
    if(id==DBlock){DesignNewBlockout(s);return;}
    if(id==DEdit){DesignEditSelected(s);return;}
    if(id==DDiscard){DesignDeactivate(s);DesignStatus(s,"Preview discarded. The map is unchanged.");return;}
    if(id==DDoorway){DesignDoorwayForm(s);return;}
    if(id==DMovement){DesignMovementForm(s);return;}
    if(id==DPlace)
    {
        DesignApply(s);
        DesignStatus(s,"Blockout placed and still selected for editing. Build geometry to update collision and rendered BSP.");
        return;
    }
    if(id==DDetach)
    {
        auto piece=DesignPiece(s);
        auto& pieces=data["pieces"];
        pieces.erase(std::remove(pieces.begin(),pieces.end(),piece),pieces.end());
        DesignSave(s,data);
        s.pending=Json{};s.previous=Json{};
        DesignStatus(s,"Detached. The brushes remain in the map for manual editing.");
        return;
    }
    if(id==DReference)
    {
        wchar_t path[32768]{};
        OPENFILENAMEW ofn{sizeof(ofn)};
        ofn.hwndOwner=s.window;ofn.lpstrTitle=L"Choose Reference Image";ofn.lpstrFile=path;ofn.nMaxFile=32768;
        ofn.lpstrFilter=L"Reference image\0*.png;*.jpg;*.jpeg;*.bmp\0\0";ofn.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetOpenFileNameW(&ofn))return;
        if(std::filesystem::file_size(path)>32*1024*1024)throw std::runtime_error("Choose a reference image smaller than 32 MiB.");
        Gdiplus::Bitmap image(path);
        if(image.GetLastStatus()!=Gdiplus::Ok || !image.GetWidth() || image.GetWidth()>16000 || image.GetHeight()>16000
           || static_cast<uint64_t>(image.GetWidth())*image.GetHeight()>16000000)
            throw std::runtime_error("Choose a readable image up to 16 megapixels and 16000 pixels per side.");
        std::vector<InputField> fields={
            {"Name","Reference "+std::to_string(data.at("references").size()+1),{}},
            {"Units per image pixel","1",{}},
            {"Opacity (0 to 1)","0.4",{}},
            {"Top-left horizontal units","0",{}},
            {"Top-left vertical units","0",{}},
            {"Floor Z (top view)",Design::Round(s.depth),{}}};
        if(!Ask(s.window,"Place Reference Image",fields))return;
        const double scale=Design::Number(fields[1].value,.0001,10000),opacity=Design::Number(fields[2].value,0,1);
        Vector origin{};
        origin[DesignHorizontal(s)]=Design::Number(fields[3].value);
        origin[DesignVertical(s)]=Design::Number(fields[4].value);
        auto file=Id()+std::filesystem::path(path).extension().string();
        auto destination=DesignReferenceFile(file);
        std::filesystem::create_directories(destination.parent_path());
        std::filesystem::copy_file(path,destination);
        data["references"].push_back({{"name",fields[0].value},{"file",file},{"scale",scale},{"opacity",opacity},{"origin",origin},
                                      {"plane",s.plane},{"floor",Design::Number(fields[5].value)}});
        DesignSave(s,data);
        DesignStatus(s,"Reference copied into ReloadedEditor/References. Each view and floor can hold its own images. Calibrate with two image points if needed.");
        return;
    }
    if(id==DRemoveReference)
    {
        auto& references=data["references"];
        if(references.empty())throw std::runtime_error("This map has no reference images.");
        std::vector<std::string> names;
        for(size_t i=0;i<references.size();++i)
            names.push_back(std::to_string(i+1)+": "+references[i].value("name",references[i].at("file").get<std::string>()));
        std::vector<InputField> f={{"Remove","",names}};
        if(!Ask(s.window,"Remove Reference Image",f))return;
        const auto index=std::find(names.begin(),names.end(),f[0].value)-names.begin();
        references.erase(index);
        DesignSave(s,data);
        s.images.clear();
        InvalidateRect(s.canvas,nullptr,FALSE);
        return;
    }
    if(id==DCalibrate || id==DMeasure)
    {
        if(id==DCalibrate)
        {
            auto visible=DesignReferencesInView(s,data);
            if(visible.empty())throw std::runtime_error("Add an image in this view first.");
            std::string file=visible.front().at("file");
            if(visible.size()>1)
            {
                std::vector<std::string> names;
                for(auto& reference:visible)names.push_back(reference.value("name",reference.at("file").get<std::string>()));
                std::vector<InputField> f={{"Reference","",names}};
                if(!Ask(s.window,"Calibrate Which Reference",f))return;
                file=visible[std::find(names.begin(),names.end(),f[0].value)-names.begin()].at("file");
            }
            s.calibrationFile=file;
        }
        s.mode=id==DCalibrate?"Calibrate":"Measure";
        s.annotationName="Measurement";
        s.points=Json::array();
        if(id==DMeasure && s.contextPointSet)s.points.push_back(s.contextPoint);
        DesignStatus(s,s.points.empty()?"Click two points in the design view. Right-click cancels.":"Click the second point. Right-click cancels.");
        return;
    }
    if(id==DGuides)
    {
        // Player references start from this map's movement limits, which come
        // from the pawn classes when they expose their collision, and can be
        // standing or crouching.
        const auto movement=Design::Movement(data.contains("movement")?data:Json{{"movement",DesignMovementDefaults(s)}});
        auto profiles=Editor::DesignClearances();
        std::vector<std::string> choices={"Standing player","Crouching player","Custom"};
        for(auto& profile:profiles)choices.push_back(profile.at("name"));
        std::vector<InputField> choose={{"Reference","Standing player",choices}};
        if(!Ask(s.window,"Add Player Reference",choose))return;
        const auto chosen=choose[0].value;
        std::string name=chosen,posture=chosen=="Crouching player"?"Crouching":"Standing";
        double width=movement.at("width"),height=movement.at("height");
        if(chosen=="Crouching player"){width=movement.at("crouchWidth");height=movement.at("crouchHeight");}
        for(auto& profile:profiles)
            if(profile.at("name")==chosen)
            {
                width=profile.at("width");
                height=profile.at("height");
                posture=chosen.find("crouch")!=std::string::npos?"Crouching":"Standing";
            }
        auto p=s.contextPointSet?s.contextPoint:Editor::BuilderPose().position;
        std::vector<InputField> f={{"Name",name,{}},{"Width (units)",Design::Round(width),{}},{"Height (units)",Design::Round(height),{}},
            {"Posture",posture,{"Standing","Crouching"}},
            {"Base X",Design::Round(p[0]),{}},{"Base Y",Design::Round(p[1]),{}},{"Base Z",Design::Round(p[2]),{}}};
        if(!Ask(s.window,"Player Reference (drag it in the design view to move it)",f))return;
        for(int i=0;i<3;++i)p[i]=Design::Number(f[i+4].value);
        data["guides"].push_back({{"name",f[0].value},{"width",Design::Number(f[1].value,1,10000)},
                                  {"height",Design::Number(f[2].value,1,10000)},{"posture",f[3].value},{"position",p}});
        DesignSave(s,data);
        DesignStatus(s,"Reference added. Drag it in a design view to move it, double-click it to rename or resize it. Sizes come from the pawn classes where they expose collision: confirm them with movement tests.");
        return;
    }
    if(id==DAlign)
    {
        DesignRefresh(s);
        Json selected=Json::array();
        for(auto& a:s.scene)if(a.at("selected").get<bool>())selected.push_back(a);
        std::vector<InputField> f={{"Operation","Align",{"Align","Distribute"}},{"Axis","X",{"X","Y","Z"}},{"Distribution spacing",Design::Round(s.grid[0]>0?s.grid[0]:128),{}}};
        if(!Ask(s.window,"Align Origins / Distribute Selection",f))return;
        Editor::DesignAlign(selected,f[1].value=="X"?0:f[1].value=="Y"?1:2,f[0].value,Design::Number(f[2].value));
        DesignRefresh(s);
        return;
    }
    if(id==DRepeat)
    {
        auto members=Editor::SelectedIdentities();
        auto frame=Editor::BuilderPose();
        std::vector<InputField> f={{"Number of new copies","4",{}},{"Spacing X",Design::Round(s.grid[0]>0?s.grid[0]*2:128),{}},{"Spacing Y","0",{}},{"Spacing Z","0",{}},
            {"Yaw step (degrees)","0",{}},{"Pivot","Builder brush",{"Builder brush","First selected actor"}}};
        if(!Ask(s.window,"Repeat Selection (new copies)",f))return;
        if(f[5].value=="First selected actor")
        {
            DesignRefresh(s);
            for(auto& a:s.scene)if(a.at("selected").get<bool>()){frame.position=a.at("position").get<Vector>();break;}
        }
        auto def=Editor::CaptureAssembly(members,frame);
        def["id"]="design-repeat";
        if(!def["bindings"].empty())throw std::runtime_error("Include actors used by external links, or use the assembly library to bind a single copy.");
        const double copies=Design::Number(f[0].value,1,128);
        if(copies!=std::floor(copies))throw std::runtime_error("Copy count must be a whole number.");
        auto repeated=Design::Repeat(def,static_cast<int>(copies),{Design::Number(f[1].value),Design::Number(f[2].value),Design::Number(f[3].value)},
                                     static_cast<int>(std::fmod(Design::Number(f[4].value),360.0)*65536/360));
        Editor::PlaceAssembly(repeated,frame,{});
        DesignRefresh(s);
        DesignStatus(s,"Repeated copies placed in one Undo step. The originals remain unchanged.");
        return;
    }
    if(id==DPlay)
    {
        auto starts=Editor::DesignSpawns();
        std::vector<std::string> choices;
        for(auto& start:starts)choices.push_back("Team "+start.at("team").get<std::string>()+" - "+start.at("path").get<std::string>());
        for(const char* team:{"0","1"})choices.push_back(std::string("Temporary start for team ")+team);
        auto cameras=Editor::CaptureView().at("cameras");
        Pose pose=Editor::BuilderPose();
        for(auto& c:cameras)
        {
            const int mode=c.at("mode");
            if(mode!=13 && mode!=14 && mode!=15){pose={c.at("position").get<Vector>(),c.at("rotation").get<Rotation>()};break;}
        }
        const std::string here="The right-clicked point ("+Design::Round(s.contextPoint[0])+", "+Design::Round(s.contextPoint[1])+", "+Design::Round(s.contextPoint[2])+")";
        std::vector<std::string> places;
        if(s.contextPointSet)places.push_back(here);
        places.push_back("Perspective viewport");
        places.push_back("Builder brush");
        std::vector<InputField> f={{"Team spawn","",choices},
            {"Test location",places.front(),places},
            {"Spawn facing (degrees)",Design::Round(pose.rotation[1]*360.0/65536),{}}};
        if(!Ask(s.window,s.contextPointSet?"Playtest From Here":"Temporary Playtest Spawn",f))return;
        if(f[1].value=="Builder brush")pose=Editor::BuilderPose();
        if(s.contextPointSet && f[1].value==here)pose.position=s.contextPoint;
        pose.rotation[1]=static_cast<int>(std::fmod(Design::Number(f[2].value),360.0)*65536/360);
        const auto index=std::find(choices.begin(),choices.end(),f[0].value)-choices.begin();
        DesignPlaytest(s,static_cast<int>(index),pose);
        return;
    }
    if(id==DRoute)
    {
        std::vector<InputField> f={{"Name","Route A",{}},{"Annotation","Route",{"Route","Objective","Spy spawn","Merc spawn"}},
            {"Team (routes)","Any",{"Any","Spy","Merc"}},{"Crouched (vents)","No",{"No","Yes"}}};
        if(!Ask(s.window,"Plan Routes and Objectives",f))return;
        s.annotationName=f[0].value;s.annotationKind=f[1].value;s.annotationTeam=f[2].value;s.annotationCrouched=f[3].value=="Yes";
        s.mode="Route";s.points=Json::array();
        if(s.contextPointSet)s.points.push_back(s.contextPoint);
        if(!s.pending.is_null())DesignDeactivate(s);
        SheetShowRoute(s);
        DesignStatus(s,"Click route points, using Depth... between floors. Each click reports the distance and both teams' times. Finish stores it; right-click cancels.");
        return;
    }
    if(id==DFinish)
    {
        if(s.mode!="Route" || s.points.empty())throw std::runtime_error("Start a route or marker and click its position first.");
        if(s.annotationKind=="Route" && s.points.size()<2)throw std::runtime_error("A route needs at least two points.");
        Json annotation={{"name",s.annotationName},{"kind",s.annotationKind},{"team",s.annotationTeam},
                         {"crouched",s.annotationCrouched},{"points",s.points}};
        data["annotations"].push_back(annotation);
        DesignSave(s,data);
        s.points=Json::array();s.mode.clear();
        DesignStatus(s,s.annotationKind=="Route"?Design::RouteSummary(annotation,data)+" saved.":"Annotation saved.");
        return;
    }
    if(id==DCompare)
    {
        std::vector<std::string> names;
        std::vector<Json> routes;
        for(auto& annotation:data.at("annotations"))
            if(annotation.at("kind")=="Route" && annotation.at("points").size()>1)
            {
                names.push_back(std::to_string(names.size()+1)+": "+annotation.at("name").get<std::string>()+" ("+annotation.value("team",std::string("Any"))+")");
                routes.push_back(annotation);
            }
        if(routes.size()<2)throw std::runtime_error("Save two routes with a spy or merc team first.");
        std::vector<InputField> f={{"First route","",names},{"Second route","",names}};
        if(!Ask(s.window,"Compare Route Timings",f))return;
        const auto first=std::find(names.begin(),names.end(),f[0].value)-names.begin();
        const auto second=std::find(names.begin(),names.end(),f[1].value)-names.begin();
        if(first==second)throw std::runtime_error("Choose two different routes.");
        DesignStatus(s,Design::RouteComparison(routes[first],routes[second],data)
            +"\r\nTimings use the speeds in Movement limits and ignore stealth, climbing, doors and waiting.");
        return;
    }
    if(id==DWorkspace)
    {
        std::vector<InputField> f={{"Workspace","Export to file...",{"Export to file...","Import from file..."}}};
        if(!Ask(s.window,"Map Design Workspace",f))return;
        const bool exporting=f[0].value=="Export to file...";
        auto suggested=DesignWorkspacePath();
        std::wstring initial=suggested.empty()?L"workspace.json":suggested.filename().wstring();
        std::vector<wchar_t> path(32768,L'\0');
        std::copy(initial.begin(),initial.end(),path.begin());
        OPENFILENAMEW ofn{sizeof(ofn)};
        ofn.hwndOwner=s.window;ofn.lpstrFile=path.data();ofn.nMaxFile=32768;
        ofn.lpstrTitle=exporting?L"Export Map Design Workspace":L"Import Map Design Workspace";
        ofn.lpstrFilter=L"Map Design workspace\0*.json\0\0";ofn.nFilterIndex=1;ofn.lpstrDefExt=L"json";
        ofn.Flags=OFN_NOCHANGEDIR|(exporting?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        if(!(exporting?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn)))return;
        if(exporting)
        {
            WriteDocument(path.data(),Design::ExportWorkspace(data,std::filesystem::path(mapKey).stem().string()));
            DesignStatus(s,"Workspace exported. Keep the ReloadedEditor/References images with it; map packaging includes both.");
            return;
        }
        auto imported=Design::ImportWorkspace(ReadDocument(path.data(),Json::object()),mapKey);
        size_t missing=0;
        for(auto& reference:imported.at("references"))
            if(!std::filesystem::exists(DesignReferenceFile(reference.at("file"))))++missing;
        DesignSave(s,NormalizeDesign(imported));
        s.images.clear();
        DesignRefresh(s);
        DesignStatus(s,missing?"Workspace imported. "+std::to_string(missing)+" reference image(s) are missing from ReloadedEditor/References.":"Workspace imported.");
        return;
    }
    if(id==DClear)
    {
        std::vector<std::string> names={"Clear player guides"};
        for(size_t i=0;i<data["annotations"].size();++i)
            names.push_back(std::to_string(i+1)+": "+data["annotations"][i].at("name").get<std::string>());
        std::vector<InputField> f={{"Remove","",names}};
        if(!Ask(s.window,"Remove Design Annotation",f))return;
        const auto i=std::find(names.begin(),names.end(),f[0].value)-names.begin();
        if(i==0)data["guides"]=Json::array();
        else data["annotations"].erase(i-1);
        DesignSave(s,data);
        InvalidateRect(s.canvas,nullptr,FALSE);
        return;
    }
    InvalidateRect(s.canvas,nullptr,FALSE);
}
// Picking: brush edges and actor origins, with Shift or Ctrl to add.
void DesignPick(DesignState& s,double x,double y,bool add)
{
    Json selected;
    double best=14;
    for(auto& actor:s.scene)
    {
        if(!DesignShows(s,actor) || actor.at("locked").get<bool>())continue;
        double distance=DesignPointDistance(DesignScreen(s,actor.at("position").get<Vector>()),x,y);
        for(auto& edge:actor.at("edges"))
        {
            if(!DesignOnFloor(s,edge[0].get<Vector>()[2],edge[1].get<Vector>()[2]))continue;
            distance=std::min(distance,DesignSegmentDistance(DesignScreen(s,edge[0].get<Vector>()),DesignScreen(s,edge[1].get<Vector>()),x,y)+6);
        }
        if(distance<best){best=distance;selected=actor;}
    }
    Json identities=Json::array();
    if(add)for(auto& actor:s.scene)if(actor.at("selected").get<bool>())identities.push_back(actor);
    if(!selected.is_null())
    {
        // A brush of a placed piece stands for the whole piece, so stairs,
        // with a brush per step, select as one from any view.
        Json group=Json::array({selected});
        if(const Json piece=DesignPieceOf(s,selected);!piece.is_null())
        {
            group=Json::array();
            for(auto& member:piece.at("members"))
                for(auto& actor:s.scene)
                    if(actor.at("path")==member.at("path"))group.push_back(actor);
        }
        // A member of a group brings the whole group, locked and hidden
        // members aside.
        if(const auto name=SceneGroupOf(s,selected.at("path").get<std::string>());!name.empty())
            for(auto& m:SceneGroupMembers(s,name))
                for(auto& actor:s.scene)
                    if(actor.at("path")==m.at("path") && !actor.at("locked").get<bool>() && DesignShows(s,actor))
                    {
                        bool present=false;
                        for(auto& g:group)if(g.at("path")==actor.at("path"))present=true;
                        if(!present)group.push_back(actor);
                    }
        const bool was=std::find_if(identities.begin(),identities.end(),[&](const Json& a){return a.at("path")==selected.at("path");})!=identities.end();
        for(auto& actor:group)
        {
            auto found=std::find_if(identities.begin(),identities.end(),[&](const Json& a){return a.at("path")==actor.at("path");});
            if(was){if(found!=identities.end())identities.erase(found);}
            else if(found==identities.end())identities.push_back(actor);
        }
    }
    else if(!add)identities=Json::array();
    Editor::Select(identities,false);
    DesignRefresh(s);
    if(selected.is_null() && !add)DesignStatus(s,"Nothing to select here. Click a brush edge, a light, a device or a game actor; drag to box-select.");
    // Clicking a generated brush picks up its whole piece for editing.
    auto piece=DesignSelectedPiece(s);
    if(s.pending.is_null() || !s.previous.is_null())
    {
        if(!piece.is_null())
        {
            DesignActivate(s,piece);
            DesignWarn(s,"Editing "+piece.at("spec").value("name",std::string("this piece"))+". Drag it, drag a square to resize, use the arrow keys, or edit its fields.");
        }
        else if(!s.previous.is_null())DesignDeactivate(s);
    }
    else if(!piece.is_null())
        DesignWarn(s,"Selected "+piece.at("spec").value("name",std::string("the piece"))+". Place the new preview (Enter) or discard it (Esc) first, then click the piece again to edit it.");
}
// A rectangle dragged from left to right selects what lies wholly inside it;
// dragged from right to left, anything it touches. Brushes of a placed piece
// count as the piece, and the devices, lights and game actors inside come too.
// One piece on its own is opened for editing.
void DesignBoxSelect(DesignState& s,bool add)
{
    const double left=std::min(s.drag.from.x,s.drag.to.x),right=std::max(s.drag.from.x,s.drag.to.x);
    const double top=std::min(s.drag.from.y,s.drag.to.y),bottom=std::max(s.drag.from.y,s.drag.to.y);
    const bool crossing=s.drag.to.x<s.drag.from.x;
    auto inside=[&](const Gdiplus::PointF& p){return p.X>=left && p.X<=right && p.Y>=top && p.Y<=bottom;};
    auto crosses=[&](const Gdiplus::PointF& a,const Gdiplus::PointF& b)
    {
        // Liang-Barsky: does any part of the segment lie in the rectangle?
        double t0=0,t1=1;
        const double dx=b.X-a.X,dy=b.Y-a.Y;
        const double p[4]={-dx,dx,-dy,dy},q[4]={a.X-left,right-a.X,a.Y-top,bottom-a.Y};
        for(int i=0;i<4;++i)
        {
            if(std::abs(p[i])<1e-9){if(q[i]<0)return false;continue;}
            const double t=q[i]/p[i];
            if(p[i]<0)t0=std::max(t0,t);else t1=std::min(t1,t);
        }
        return t0<=t1;
    };
    auto matches=[&](const Json& actor)
    {
        const auto& edges=actor.at("edges");
        if(edges.empty())return inside(DesignScreen(s,actor.at("position").get<Vector>()));
        bool all=true,any=false;
        for(auto& edge:edges)
        {
            if(!DesignOnFloor(s,edge[0].get<Vector>()[2],edge[1].get<Vector>()[2]))continue;
            const auto a=DesignScreen(s,edge[0].get<Vector>()),b=DesignScreen(s,edge[1].get<Vector>());
            if(inside(a) && inside(b))any=true;
            else{all=false;if(crosses(a,b))any=true;}
        }
        return crossing?any:all;
    };
    Json identities=Json::array();
    auto has=[&](const Json& a){return std::find_if(identities.begin(),identities.end(),[&](const Json& b){return b.at("path")==a.at("path");})!=identities.end();};
    auto push=[&](const Json& a){if(!has(a))identities.push_back(a);};
    if(add)for(auto& actor:s.scene)if(actor.at("selected").get<bool>())identities.push_back(actor);
    std::set<std::string> hits,members;
    for(auto& actor:s.scene)
        if(DesignShows(s,actor) && !actor.at("locked").get<bool>() && matches(actor))hits.insert(actor.at("path").get<std::string>());
    size_t pieces=0;
    for(const auto& piece:DesignData(s).at("pieces"))
    {
        size_t live=0,hit=0;
        for(auto& member:piece.at("members"))
        {
            const auto path=member.at("path").get<std::string>();
            members.insert(path);
            for(auto& actor:s.scene)
                if(actor.at("path")==path && DesignShows(s,actor) && !actor.at("locked").get<bool>()){++live;break;}
            if(hits.count(path))++hit;
        }
        // Wholly inside means every visible brush of the piece is inside.
        if(hit==0 || (!crossing && hit<live))continue;
        ++pieces;
        for(auto& member:piece.at("members"))
            for(auto& actor:s.scene)
                if(actor.at("path")==member.at("path"))push(actor);
    }
    size_t brushes=0;
    for(auto& actor:s.scene)
        if(hits.count(actor.at("path").get<std::string>()) && !members.count(actor.at("path").get<std::string>())){push(actor);++brushes;}
    size_t others=0;
    for(const auto* list:{&s.securityActors,&s.lights,&s.objectives})
        for(const auto& actor:*list)
        {
            const Vector position=actor.at("position");
            if(!DesignOnFloor(s,position[2],position[2]) || !inside(DesignScreen(s,position)))continue;
            if(DesignLockedPath(s,actor.at("path").get<std::string>()))continue;
            push(actor);++others;
        }
    Editor::Select(identities,false);
    DesignRefresh(s);
    const std::string hint=crossing?" Drag from left to right to select only what lies wholly inside.":" Drag from right to left to select anything the rectangle touches.";
    if(pieces+brushes+others==0)
    {
        if(!s.previous.is_null())DesignDeactivate(s);
        DesignStatus(s,std::string("Nothing ")+(crossing?"touched by":"wholly inside")+" the rectangle."+hint+" Shift or Ctrl adds to the selection.");
        return;
    }
    if(pieces==1 && brushes+others==0 && (s.pending.is_null() || !s.previous.is_null()))
    {
        if(const auto piece=DesignSelectedPiece(s);!piece.is_null())
        {
            DesignActivate(s,piece);
            DesignWarn(s,"Editing "+piece.at("spec").value("name",std::string("this piece"))+". Drag it, drag a square to resize, use the arrow keys, or edit its fields.");
            return;
        }
    }
    if(!s.previous.is_null())DesignDeactivate(s);
    std::string what;
    auto part=[&](size_t n,const char* one,const char* many){if(n==0)return;if(!what.empty())what+=", ";what+=std::to_string(n)+" "+(n==1?one:many);};
    part(pieces,"piece","pieces");part(brushes,"other brush","other brushes");part(others,"device, light or game actor","devices, lights and game actors");
    DesignStatus(s,"Selected "+what+". Delete removes them; Shift or Ctrl adds to the selection."+hint);
}
// Quick add: the wall of a placed piece nearest the cursor, if it is close
// enough to offer a neighbour there.
bool DesignHoverWall(DesignState& s,POINT at)
{
    // Moving from the wall onto its badge must not lose the badge, so a cursor
    // near the badge keeps the current hover.
    if(!s.hoverPiece.is_null() && DesignPointDistance({static_cast<float>(s.hoverAt.x),static_cast<float>(s.hoverAt.y)},at.x,at.y)<=18)return true;
    s.hoverPiece=Json{};
    s.hoverWall.clear();
    s.hoverAt=at;
    const auto& data=DesignData(s);
    const int first=DesignHorizontal(s),second=DesignVertical(s);
    const auto world=DesignWorld(s,at.x,at.y);
    double best=14;
    if(second==2)
    {
        // Side-on: a piece's top and bottom, and its walls seen edge-on.
        // Quarter-turn pieces keep a world axis along the view.
        Design::Extent chosen{};
        for(auto it=data.at("pieces").rbegin();it!=data.at("pieces").rend();++it)
        {
            const auto& piece=*it;
            const auto kind=piece.at("spec").at("kind").get<std::string>();
            if(kind!="Room" && kind!="Corridor" && !Design::Crouching(kind))continue;
            bool live=false;
            for(auto& member:piece.at("members"))
                for(auto& actor:s.scene)
                    if(actor.at("path")==member.at("path"))live=true;
            if(!live)continue;
            const Pose pose{piece.at("position").get<Vector>(),piece.at("rotation").get<Rotation>()};
            // Pieces the floor filter hides get no badge.
            if(!DesignOnFloor(s,pose.position[2],pose.position[2]+piece.at("spec").value("height",0.0)))continue;
            const int yaw=((pose.rotation[1]%65536)+65536)%65536;
            if(yaw%16384!=0)continue;
            Design::Extent bounds;
            try{bounds=DesignBoundsOf(piece.at("spec"),pose);}catch(const std::exception&){continue;}
            const double hx=world[first],z=world[2];
            if(hx<bounds.lo[first] || hx>bounds.hi[first] || z<bounds.lo[2] || z>bounds.hi[2])continue;
            auto consider=[&](const std::string& wall,double distance)
            {
                if(distance>=best)return;
                best=distance;s.hoverPiece=piece;s.hoverWall=wall;s.hoverOffset=0;chosen=bounds;
            };
            {
                consider("+Z",std::abs(z-bounds.hi[2])*s.zoom);
                consider("-Z",std::abs(z-bounds.lo[2])*s.zoom);
            }
            {
                // The local wall facing that way: turn the world side direction
                // back through the piece's yaw.
                auto localWall=[&](double sign)
                {
                    const double angle=yaw*2*3.14159265358979323846/65536;
                    const double dx=first==0?sign:0,dy=first==1?sign:0;
                    const double lx=dx*std::cos(angle)+dy*std::sin(angle),ly=-dx*std::sin(angle)+dy*std::cos(angle);
                    return std::abs(lx)>std::abs(ly)?(lx>0?std::string("+X"):std::string("-X")):(ly>0?std::string("+Y"):std::string("-Y"));
                };
                consider(localWall(-1),std::abs(hx-bounds.lo[first])*s.zoom);
                consider(localWall(1),std::abs(hx-bounds.hi[first])*s.zoom);
            }
        }
        if(s.hoverPiece.is_null())return false;
        // The badge sits just inside the edge, within the piece the cursor is
        // in, so two stacked rooms each keep their own.
        Vector edge=world;
        if(s.hoverWall=="+Z"){edge[2]=chosen.hi[2];const auto p=DesignScreen(s,edge);s.hoverAt={at.x,static_cast<LONG>(std::lround(p.Y+26))};}
        else if(s.hoverWall=="-Z"){edge[2]=chosen.lo[2];const auto p=DesignScreen(s,edge);s.hoverAt={at.x,static_cast<LONG>(std::lround(p.Y-26))};}
        else
        {
            const bool right=std::abs(world[first]-chosen.hi[first])<std::abs(world[first]-chosen.lo[first]);
            edge[first]=right?chosen.hi[first]:chosen.lo[first];
            const auto p=DesignScreen(s,edge);
            s.hoverAt={static_cast<LONG>(std::lround(p.X+(right?-26:26))),at.y};
        }
        return true;
    }
    for(auto it=data.at("pieces").rbegin();it!=data.at("pieces").rend();++it)
    {
        const auto& piece=*it;
        const auto kind=piece.at("spec").at("kind").get<std::string>();
        if(kind!="Room" && kind!="Corridor" && !Design::Crouching(kind))continue;
        bool live=false;
        for(auto& member:piece.at("members"))
            for(auto& actor:s.scene)
                if(actor.at("path")==member.at("path"))live=true;
        if(!live)continue;
        const Pose pose{piece.at("position").get<Vector>(),piece.at("rotation").get<Rotation>()};
        // Pieces the floor filter hides get no badge.
        if(!DesignOnFloor(s,pose.position[2],pose.position[2]+piece.at("spec").value("height",0.0)))continue;
        const double w=piece.at("spec").at("width"),l=piece.at("spec").at("length");
        const auto local=TransformPoint(world,pose,true);
        const std::pair<const char*,double> walls[]={{"+Y",l/2},{"-Y",-l/2},{"+X",w/2},{"-X",-w/2}};
        for(auto& [wall,at2]:walls)
        {
            const bool alongX=wall[1]=='Y';
            const double across=alongX?local[1]:local[0],along=alongX?local[0]:local[1];
            const double span=alongX?w:l;
            if(std::abs(along)>span/2)continue;
            const double distance=std::abs(across-at2)*s.zoom;
            if(distance>=best)continue;
            best=distance;
            s.hoverPiece=piece;
            s.hoverWall=wall;
            s.hoverOffset=along;
        }
    }
    if(s.hoverPiece.is_null())return false;
    // The badge sits just outside the wall rather than under the cursor, so a
    // click on the wall itself still selects the piece or drags its face.
    const Pose pose{s.hoverPiece.at("position").get<Vector>(),s.hoverPiece.at("rotation").get<Rotation>()};
    const double w=s.hoverPiece.at("spec").at("width"),l=s.hoverPiece.at("spec").at("length");
    const bool alongX=s.hoverWall[1]=='Y';
    const double sign=s.hoverWall[0]=='+'?1:-1;
    Vector onWall{0,0,world[2]},outside{0,0,world[2]};
    if(alongX){onWall[0]=s.hoverOffset;onWall[1]=sign*l/2;outside=onWall;outside[1]+=sign*64;}
    else{onWall[1]=s.hoverOffset;onWall[0]=sign*w/2;outside=onWall;outside[0]+=sign*64;}
    const auto wallAt=DesignScreen(s,TransformPoint(onWall,pose)),beyond=DesignScreen(s,TransformPoint(outside,pose));
    const double dx=beyond.X-wallAt.X,dy=beyond.Y-wallAt.Y,length=std::hypot(dx,dy);
    if(length>0)
    {
        s.hoverAt.x=static_cast<LONG>(std::lround(wallAt.X+dx/length*26));
        s.hoverAt.y=static_cast<LONG>(std::lround(wallAt.Y+dy/length*26));
    }
    return true;
}
// Adds a neighbour on the hovered wall: a doorway cuts through it, the others
// sit against its outside.
void DesignQuickAdd(DesignState& s,const std::string& kind)
{
    if(s.hoverPiece.is_null())throw std::runtime_error("Point at the wall of a placed room, corridor or vent first.");
    const auto host=s.hoverPiece.at("spec");
    const Pose hostPose{s.hoverPiece.at("position").get<Vector>(),s.hoverPiece.at("rotation").get<Rotation>()};
    const auto movement=Design::Movement(DesignData(s));
    const double thickness=host.value("thickness",16.0);
    Json spec={{"kind",kind},{"construction",host.value("construction",std::string("Carve"))},{"thickness",thickness},
               {"steps",8},{"ceiling",true},{"portal",kind=="Doorway"},{"name",kind}};
    const double clearWidth=Design::ClearanceWidth(kind,movement),clearHeight=Design::ClearanceHeight(kind,movement);
    if(kind=="Doorway")
    {
        spec["width"]=clearWidth+16;
        spec["height"]=clearHeight+16;
        spec["length"]=thickness;
    }
    else if(kind=="Room")
    {
        spec["width"]=512;spec["length"]=512;spec["height"]=host.at("height");
    }
    else
    {
        spec["width"]=clearWidth+16;
        spec["length"]=384;
        spec["height"]=Design::Crouching(kind)?clearHeight:host.at("height").get<double>();
    }
    const double offset=s.snap?Design::SnapTo(s.hoverOffset,s.grid[0]):s.hoverOffset;
    auto pose=kind=="Doorway"
        ? Design::WallDoorway(host,hostPose,spec,s.hoverWall,offset,0)
        : Design::AttachedPiece(host,hostPose,spec,s.hoverWall,offset);
    if(s.snap && kind!="Doorway")
    {
        // Snap the new piece's side walls, not its centre, to the world grid,
        // so it lines up even when its host sits off the grid or is an odd
        // width. Only quarter-turn hosts keep a world axis along the wall.
        const int yaw=((hostPose.rotation[1]%65536)+65536)%65536;
        if(yaw%16384==0)
        {
            const bool alongX=(s.hoverWall[1]=='Y')!=((yaw/16384)%2==1);
            const int axis=alongX?0:1;
            const double half=spec.at("width").get<double>()/2;
            if(s.grid[axis]>0)pose.position[axis]=Design::SnapTo(pose.position[axis]-half,s.grid[axis])+half;
        }
    }
    s.pending=spec;
    s.previous=Json{};
    s.frame=pose;
    s.drag={};
    DesignInspectorRefresh(s);
    InvalidateRect(s.canvas,nullptr,FALSE);
    DesignWarn(s,Fold(kind)+" preview placed on the "+s.hoverWall+" wall of "+host.value("name",std::string("the piece"))
        +". Adjust it, then Place / Apply.");
}
// Runs a playtest from a chosen spawn: either an existing team start, or a
// temporary one created for the test and undone as soon as it returns.
void DesignPlaytest(DesignState& s,int choice,const Pose& pose)
{
    auto starts=Editor::DesignSpawns();
    if(choice<0)throw std::runtime_error("Choose a team spawn first.");
    if(choice<static_cast<int>(starts.size()))
    {
        Editor::DesignPlay(starts[choice],pose);
        DesignStatus(s,"Play Level returned. Editor spawn locations restored. Use the matching team in Play Map Options.");
        return;
    }
    const std::string team=choice-static_cast<int>(starts.size())==0?"0":"1";
    const std::string type=starts.empty()?"Engine.PlayerStart":starts.front().at("class").get<std::string>();
    auto temporary=Editor::DesignTemporaryStart(type,team,pose);
    struct Remove
    {
        Json start;
        ~Remove(){try{Editor::DesignRemoveTemporaryStart(start);}catch(const std::exception&){}}
    } remove{temporary};
    Editor::DesignPlay(temporary,pose);
    DesignRefresh(s);
    DesignStatus(s,"Play Level returned. The temporary team "+team+" start was removed again; Redo would restore it. Use that team in Play Map Options.");
}
bool DesignGeometryStale(DesignState& s)
{
    return Editor::Revision()!=s.builtRevision;
}
// Design check: the model's layout issues for the pieces still in the map,
// plus what a Versus map needs before it can be played at all.
LRESULT CALLBACK DesignCheckProc(HWND window,UINT message,WPARAM w,LPARAM l)
{
    auto s=reinterpret_cast<DesignState*>(GetWindowLongPtr(window,GWLP_USERDATA));
    if(message==WM_NCCREATE)
    {
        s=static_cast<DesignState*>(reinterpret_cast<CREATESTRUCT*>(l)->lpCreateParams);
        SetWindowLongPtr(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));
    }
    if(!s)return DefWindowProcA(window,message,w,l);
    try
    {
        if(message==WM_CREATE)
        {
            Control(window,"STATIC","Double-click an issue to select its piece. Check again after changes.",0,0,12,10,560,20);
            Control(window,"LISTBOX","",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_BORDER,DCheckList,12,34,560,300);
            return 0;
        }
        if(message==WM_SIZE)
        {
            MoveWindow(GetDlgItem(window,DCheckList),12,34,std::max(1,LOWORD(l)-24),std::max(1,HIWORD(l)-46),TRUE);
            return 0;
        }
        if(message==WM_COMMAND && LOWORD(w)==DCheckList && HIWORD(w)==LBN_DBLCLK)
        {
            const auto row=SendDlgItemMessage(window,DCheckList,LB_GETCURSEL,0,0);
            if(row<0 || row>=static_cast<LRESULT>(s->issues.size()))return 0;
            const auto& issue=s->issues[row];
            if(issue.value("piece",-1)<0)return 0;
            const auto& pieces=DesignData(*s).at("pieces");
            const auto index=issue.at("index").get<size_t>();
            if(index>=pieces.size())return 0;
            Editor::Select(pieces[index].at("members"),true);
            DesignRefresh(*s);
            DesignActivate(*s,pieces[index]);
            return 0;
        }
        if(message==WM_CLOSE){DestroyWindow(window);return 0;}
        if(message==WM_NCDESTROY){s->checkWindow=nullptr;SetWindowLongPtr(window,GWLP_USERDATA,0);return DefWindowProcA(window,message,w,l);}
    }
    catch(const std::exception& e){DesignStatus(*s,e.what());}
    return DefWindowProcA(window,message,w,l);
}
void DesignCheck(DesignState& s)
{
    DesignRefresh(s);
    const auto& data=DesignData(s);
    // Only pieces whose brushes are still in the map count.
    Json live=Json::array();
    std::vector<size_t> indices;
    for(size_t i=0;i<data.at("pieces").size();++i)
    {
        bool present=!data.at("pieces")[i].at("members").empty();
        for(auto& member:data.at("pieces")[i].at("members"))
        {
            bool found=false;
            for(auto& actor:s.scene)if(actor.at("path")==member.at("path"))found=true;
            present=present && found;
        }
        if(present){live.push_back(data.at("pieces")[i]);indices.push_back(i);}
    }
    s.issues=Json::array();
    for(const auto& issue:Design::DesignIssues(live,data))
        s.issues.push_back({{"severity",issue.severity},{"text",issue.text},{"piece",issue.piece},{"index",issue.piece>=0?indices[issue.piece]:0}});
    // What a Versus map needs before Play Level makes sense.
    std::set<std::string> teams;
    for(auto& spawn:Editor::DesignSpawns())teams.insert(spawn.at("team").get<std::string>());
    for(const char* team:{"0","1"})
        if(!teams.count(team))s.issues.push_back({{"severity","error"},{"text",std::string("No PlayerStart for team ")+team+"."},{"piece",-1},{"index",0}});
    bool mission=false,objective=false,cameras=false;
    for(auto& actor:Editor::Actors())
    {
        const auto cls=actor.at("class").get<std::string>();
        if(cls.find("SMission")!=std::string::npos)mission=true;
        if(cls.find("SObjective")!=std::string::npos)objective=true;
        if(cls.find("SCamNetwork")!=std::string::npos)cameras=true;
    }
    if(!mission)s.issues.push_back({{"severity","error"},{"text","No SMission actor: the map has no game mode to play."},{"piece",-1},{"index",0}});
    if(!objective)s.issues.push_back({{"severity","warning"},{"text","No SObjective actor: there is nothing for the spies to do."},{"piece",-1},{"index",0}});
    if(!cameras)s.issues.push_back({{"severity","warning"},{"text","No SCamNetwork: mercs have no cameras to watch through."},{"piece",-1},{"index",0}});
    // Objective flow: every mission needs objectives, every objective a mission
    // and something to do, every flag a drop zone, and starts kept apart.
    {
        auto note=[&](const char* severity,const std::string& text){s.issues.push_back({{"severity",severity},{"text",text},{"piece",-1},{"index",0}});};
        auto label=[&](const Json& actor){return ObjectiveLabel(actor);};
        auto refers=[&](const Json& list,const std::string& path){for(auto& ref:list)if(ref.is_string() && ref.get<std::string>().find(path)!=std::string::npos)return true;return false;};
        size_t flags=0,dropZones=0;
        for(const auto& actor:s.objectives)
        {
            const auto kind=actor.value("kind",std::string());
            if(kind=="Flag")++flags;
            if(kind=="Drop zone")++dropZones;
            if(kind=="Mission" && actor.value("objectives",Json::array()).empty())note("error",label(actor)+" lists no objectives: nothing ends the round.");
            if(kind=="Objective")
            {
                bool listed=false;
                for(const auto& other:s.objectives)if(other.value("kind",std::string())=="Mission" && refers(other.value("objectives",Json::array()),actor.at("path").get<std::string>()))listed=true;
                if(!listed)note("warning",label(actor)+" is not in any mission's Objectives list, so it never counts.");
                if(actor.value("triggers",Json::array()).empty())note("warning",label(actor)+" has no terminal, bomb target or trigger: the spies cannot complete it.");
            }
        }
        if(flags>0 && dropZones==0)note("warning","A flag without a drop zone: the spies have nowhere to bring it.");
        if(dropZones>0 && flags==0)note("warning","A drop zone without a flag.");
        std::vector<Json> spyStarts,mercStarts,targets;
        for(const auto& actor:s.objectives)
        {
            const auto kind=actor.value("kind",std::string());
            if(kind=="Player start")(actor.value("team",std::string("0"))=="0"?spyStarts:mercStarts).push_back(actor); // Team 0 spies, 1 mercs.
            else if(kind=="Computer terminal" || kind=="Bomb target" || kind=="Objective trigger" || kind=="Flag")targets.push_back(actor);
        }
        auto distance=[](const Json& a,const Json& b){return Design::Distance(a.at("position").get<Vector>(),b.at("position").get<Vector>());};
        for(const auto& start:spyStarts)
            for(const auto& target:targets)
                if(distance(start,target)<512)note("warning",label(start)+" is within "+Design::Round(distance(start,target))+" units of "+label(target)+": the objective is reachable before the mercs can respond.");
        for(const auto& spy:spyStarts)
            for(const auto& merc:mercStarts)
                if(distance(spy,merc)<768)note("warning",label(spy)+" and "+label(merc)+" are only "+Design::Round(distance(spy,merc))+" units apart: the teams meet immediately.");
        if(!mercStarts.empty() && !targets.empty())
        {
            // Every objective should be closer to some merc start than 4096 units, or the mercs cannot defend it.
            for(const auto& target:targets)
            {
                double nearest=1e18;
                for(const auto& merc:mercStarts)nearest=std::min(nearest,distance(merc,target));
                if(nearest>4096)note("warning",label(target)+" is "+Design::Round(nearest)+" units from the nearest merc start: hard to defend.");
            }
        }
    }
    for(const auto& issue:Security::Issues(s.securityActors))
        s.issues.push_back({{"severity",issue.severity},{"text",issue.text},{"piece",-1},{"index",0}});
    for(const auto& text:LightIssues(s))
        s.issues.push_back({{"severity","warning"},{"text",text},{"piece",-1},{"index",0}});
    if(DesignGeometryStale(s))s.issues.push_back({{"severity","warning"},{"text","Brushes changed since the last geometry build; press B in the design view."},{"piece",-1},{"index",0}});
    if(!s.checkWindow)
    {
        WNDCLASSA wc{};
        wc.hInstance=GetModuleHandle(nullptr);
        wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
        wc.lpfnWndProc=DesignCheckProc;
        wc.lpszClassName="ReloadedDesignCheck";
        RegisterClassA(&wc);
        s.checkWindow=CreateWindowExA(WS_EX_TOOLWINDOW,wc.lpszClassName,"Design Check",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                                      CW_USEDEFAULT,CW_USEDEFAULT,600,380,s.window,nullptr,wc.hInstance,&s);
        if(!s.checkWindow)throw std::runtime_error("Cannot open the design check.");
    }
    auto list=GetDlgItem(s.checkWindow,DCheckList);
    SendMessage(list,LB_RESETCONTENT,0,0);
    for(auto& issue:s.issues)
    {
        const auto line=(issue.at("severity")=="error"?"ERROR  ":"warn   ")+issue.at("text").get<std::string>();
        SendMessageA(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(line.c_str()));
    }
    if(s.issues.empty())SendMessageA(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>("No issues found."));
    ShowWindow(s.checkWindow,SW_SHOWNORMAL);
    DesignStatus(s,std::to_string(s.issues.size())+" design check issue(s). Double-click one in the Design Check window to select its piece.");
}
// The quick-add menu raised from the badge on a hovered wall.
// From a side-on view: a room stacked on the hovered one, or stairs into it.
void FloorStairsAt(DesignState& s,const Vector& at,double base,double spacing,const std::string& kind);
void FloorLiftAt(DesignState& s,const Vector& at,double base,double spacing);
const char* const kStairKinds[]={"Stairs","Stairs L","Stairs U","Spiral"};
const char* const kStairLabels[]={"Straight","L-shaped","U-shaped (switchback)","Spiral"};
void DesignQuickAddVertical(DesignState& s,int choice)
{
    if(s.hoverPiece.is_null())throw std::runtime_error("Point at the top or bottom of a placed room first.");
    const auto host=s.hoverPiece.at("spec");
    const Pose hostPose{s.hoverPiece.at("position").get<Vector>(),s.hoverPiece.at("rotation").get<Rotation>()};
    const double spacing=host.at("height").get<double>()+host.value("thickness",16.0);
    const bool stairsUp=choice>=DAddStairsUpFirst && choice<DAddStairsUpFirst+4,stairsDown=choice>=DAddStairsDownFirst && choice<DAddStairsDownFirst+4;
    const bool up=choice==DAddRoomAbove || stairsUp || choice==DAddLiftUp;
    if(choice==DAddLiftUp || choice==DAddLiftDown)
    {
        FloorLiftAt(s,hostPose.position,up?hostPose.position[2]:hostPose.position[2]-spacing,spacing);
        return;
    }
    if(stairsUp || stairsDown)
    {
        const int shape=choice-(stairsUp?DAddStairsUpFirst:DAddStairsDownFirst);
        FloorStairsAt(s,hostPose.position,up?hostPose.position[2]:hostPose.position[2]-spacing,spacing,kStairKinds[shape]);
        return;
    }
    Json spec=host;
    spec["name"]=host.value("name",std::string("Room"))+(up?" above":" below");
    Pose pose=hostPose;
    pose.position[2]+=up?spacing:-spacing;
    s.pending=spec;
    s.previous=Json{};
    s.frame=pose;
    s.drag={};
    DesignInspectorRefresh(s);
    InvalidateRect(s.canvas,nullptr,FALSE);
    DesignWarn(s,std::string(up?"Room above":"Room below")+" previewed, "+Design::Round(spacing)+" units "+(up?"up":"down")+" with the same footprint. Adjust it, then Place / Apply. Add stairs or a ladder between them from the Floors menu.");
}
void DesignQuickAddMenu(DesignState& s,POINT at)
{
    HMENU menu=CreatePopupMenu();
    if(!menu)throw std::runtime_error("Could not open the quick-add menu.");
    if(s.hoverWall=="+Z" || s.hoverWall=="-Z")
    {
        const bool up=s.hoverWall=="+Z";
        // Is a piece already stacked on that side?
        bool stacked=false;
        try
        {
            const Pose hostPose{s.hoverPiece.at("position").get<Vector>(),s.hoverPiece.at("rotation").get<Rotation>()};
            const auto host=DesignBoundsOf(s.hoverPiece.at("spec"),hostPose);
            const double thickness=s.hoverPiece.at("spec").value("thickness",16.0);
            for(const auto& other:DesignData(s).at("pieces"))
            {
                if(other.at("members")==s.hoverPiece.at("members"))continue;
                const auto kind=other.at("spec").at("kind").get<std::string>();
                if(kind!="Room" && kind!="Corridor" && !Design::Crouching(kind))continue;
                bool live=false;
                for(auto& member:other.at("members"))
                    for(auto& actor:s.scene)
                        if(actor.at("path")==member.at("path"))live=true;
                if(!live)continue;
                const auto bounds=DesignBoundsOf(other.at("spec"),{other.at("position").get<Vector>(),other.at("rotation").get<Rotation>()});
                const bool overlaps=bounds.lo[0]<host.hi[0] && bounds.hi[0]>host.lo[0] && bounds.lo[1]<host.hi[1] && bounds.hi[1]>host.lo[1];
                const double gap=up?bounds.lo[2]-host.hi[2]:host.lo[2]-bounds.hi[2];
                if(overlaps && gap>-1 && gap<=thickness+2)stacked=true;
            }
        }
        catch(const std::exception&) { /* Treat as clear. */ }
        AppendMenuA(menu,MF_STRING|(stacked?MF_GRAYED:0),up?DAddRoomAbove:DAddRoomBelow,
            stacked?(up?"Room above (a room is already there)":"Room below (a room is already there)"):(up?"Room above this one (same size)":"Room below this one (same size)"));
        HMENU shapes=CreatePopupMenu();
        for(int i=0;i<4;++i)AppendMenuA(shapes,MF_STRING,(up?DAddStairsUpFirst:DAddStairsDownFirst)+i,kStairLabels[i]);
        AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(shapes),up?(stacked?"Stairs up into the room above":"Stairs up into a room above"):(stacked?"Stairs down into the room below":"Stairs down into a room below"));
        AppendMenuA(menu,MF_STRING,up?DAddLiftUp:DAddLiftDown,up?"Lift up into the room above":"Lift down into the room below");
    }
    else
    {
        AppendMenuA(menu,MF_STRING,DAddDoorway,"Doorway through this wall");
        AppendMenuA(menu,MF_STRING,DAddCorridor,"Corridor from this wall");
        AppendMenuA(menu,MF_STRING,DAddVent,"Vent from this wall");
        AppendMenuA(menu,MF_STRING,DAddRoom,"Room against this wall");
    }
    POINT screen=at;
    ClientToScreen(s.canvas,&screen);
    const auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN,screen.x,screen.y,0,s.canvas,nullptr);
    DestroyMenu(menu);
    if(!choice)return;
    if(choice==DAddRoomAbove || choice==DAddRoomBelow || choice==DAddLiftUp || choice==DAddLiftDown || (choice>=DAddStairsUpFirst && choice<DAddStairsDownFirst+4))
    {
        DesignQuickAddVertical(s,choice);
        return;
    }
    DesignQuickAdd(s,choice==DAddDoorway?"Doorway":choice==DAddCorridor?"Corridor":choice==DAddVent?"Vent":"Room");
}
// Renaming and resizing a reference or a route, straight from the view.
void DesignEditGuide(DesignState& s,size_t index)
{
    if(!s.pending.is_null())DesignDeactivate(s);
    SheetShowGuide(s,index);
    DesignStatus(s,"Edit the reference in the panel; drag it in the view to move it.");
}
void DesignEditAnnotation(DesignState& s,size_t index)
{
    if(!s.pending.is_null())DesignDeactivate(s);
    SheetShowAnnotation(s,index);
    DesignStatus(s,"Edit the annotation in the panel; drag its points in the view.");
}
// Guides and route points are draggable in the design views too.
bool DesignGuideAt(DesignState& s,double x,double y,size_t& index)
{
    const auto& data=DesignData(s);
    for(size_t i=data.at("guides").size();i-->0;)
    {
        const auto& guide=data.at("guides")[i];
        const Vector p=guide.at("position");
        const double width=guide.at("width"),height=guide.at("height");
        if(!DesignOnFloor(s,p[2],p[2]+height))continue;
        auto low=p,high=p;
        const int sideways=DesignHorizontal(s);
        low[sideways]-=width/2;high[sideways]+=width/2;
        if(s.plane==0){low[1]-=width/2;high[1]+=width/2;}else high[2]+=height;
        const auto a=DesignScreen(s,low),b=DesignScreen(s,high);
        if(x>=std::min(a.X,b.X) && x<=std::max(a.X,b.X) && y>=std::min(a.Y,b.Y) && y<=std::max(a.Y,b.Y))
        {
            index=i;
            return true;
        }
    }
    return false;
}
bool DesignRoutePointAt(DesignState& s,double x,double y,size_t& annotation,size_t& point)
{
    const auto& data=DesignData(s);
    for(size_t i=data.at("annotations").size();i-->0;)
    {
        const auto& points=data.at("annotations")[i].at("points");
        for(size_t j=points.size();j-->0;)
        {
            const auto position=points[j].get<Vector>();
            if(!DesignOnFloor(s,position[2],position[2]))continue;
            if(DesignPointDistance(DesignScreen(s,position),x,y)>7)continue;
            annotation=i;point=j;
            return true;
        }
    }
    return false;
}
void DesignBeginDrag(DesignState& s,POINT p,const Vector& world)
{
    s.drag={};
    s.drag.from=s.drag.to=p;
    s.drag.start=world;
    s.drag.spec=s.pending;
    s.drag.frame=s.frame;
    // Annotations and references come first: they sit on top of the plan.
    size_t index=0,point=0;
    if(DesignRoutePointAt(s,p.x,p.y,index,point))
    {
        s.drag.kind=DesignDrag::Kind::RoutePoint;
        s.drag.index=index;
        s.drag.point=point;
        return;
    }
    if(DesignGuideAt(s,p.x,p.y,index))
    {
        s.drag.kind=DesignDrag::Kind::Guide;
        s.drag.index=index;
        return;
    }
    Json device;bool aim=false;
    if(SecurityHandleAt(s,p.x,p.y,device,aim))
    {
        if(DesignLockedPath(s,device.at("path").get<std::string>())){s.drag.kind=DesignDrag::Kind::Select;return;}
        s.drag.kind=aim?DesignDrag::Kind::DeviceAim:DesignDrag::Kind::Device;
        s.drag.device=device;
        s.drag.devicePose={device.at("position").get<Vector>(),device.at("rotation").get<Rotation>()};
        s.drag.deviceLength=device.value("length",300.0);
        return;
    }
    if(Json start;ObjectiveAimHandleAt(s,p.x,p.y,start))
    {
        if(DesignLockedPath(s,start.at("path").get<std::string>())){s.drag.kind=DesignDrag::Kind::Select;return;}
        // The facing handle of a player start turns it.
        s.drag.kind=DesignDrag::Kind::DeviceAim;
        s.drag.device=start;
        s.drag.devicePose={start.at("position").get<Vector>(),start.at("rotation").get<Rotation>()};
        s.drag.deviceLength=0;
        return;
    }
    if(Json gameActor=ObjectiveAt(s,p.x,p.y);!gameActor.is_null())
    {
        if(DesignLockedPath(s,gameActor.at("path").get<std::string>())){s.drag.kind=DesignDrag::Kind::Select;return;}
        s.drag.kind=DesignDrag::Kind::Device;
        s.drag.device=gameActor;
        s.drag.devicePose={gameActor.at("position").get<Vector>(),gameActor.at("rotation").get<Rotation>()};
        s.drag.deviceLength=0;
        return;
    }
    Json light;bool rim=false;
    if(LightHandleAt(s,p.x,p.y,light,rim))
    {
        if(DesignLockedPath(s,light.at("path").get<std::string>())){s.drag.kind=DesignDrag::Kind::Select;return;}
        // A light moves like a device; its rim sets the reach.
        s.drag.kind=rim?DesignDrag::Kind::LightRadius:DesignDrag::Kind::Device;
        s.drag.device=light;
        s.drag.devicePose={light.at("position").get<Vector>(),light.at("rotation").get<Rotation>()};
        s.drag.deviceLength=light.value("radius",400.0);
        return;
    }
    if(!s.pending.is_null())
    {
        for(auto& handle:DesignHandles(s))
            if(DesignPointDistance(DesignScreen(s,handle.world),p.x,p.y)<=8)
            {
                if(handle.axis==3)
                {
                    s.drag.kind=DesignDrag::Kind::Rotate;
                    s.drag.angle=std::atan2(world[1]-s.frame.position[1],world[0]-s.frame.position[0]);
                    return;
                }
                s.drag.kind=DesignDrag::Kind::Resize;
                s.drag.axis=handle.axis;
                s.drag.side=handle.side;
                return;
            }
        if(DesignInsidePreview(s,world))
        {
            s.drag.kind=DesignDrag::Kind::Move;
            return;
        }
    }
    else if(const Json piece=DesignSelectedPieceContaining(s,world);!piece.is_null())
    {
        // Several selected pieces: dragging inside one moves them all.
        DesignActivate(s,piece);
        if(!s.pending.is_null())
        {
            s.drag.spec=s.pending;
            s.drag.frame=s.frame;
            s.drag.kind=DesignDrag::Kind::Move;
            s.drag.fromSelection=true;
            return;
        }
    }
    s.drag.kind=DesignDrag::Kind::Select;
}
// The local drag distance that also lands the dragged face on a neighbouring
// wall. Falls back to the world grid when nothing is within reach. The moving
// face is found by measuring, so any rotation works.
double DesignSnapResize(DesignState& s,double delta)
{
    const double grid=s.snap && s.grid[s.drag.axis]>0?Design::SnapTo(delta,s.grid[s.drag.axis]):delta;
    if(!s.snap || std::abs(delta)<1e-9)return grid;
    auto bounds=[&](double value)
    {
        Json spec=s.drag.spec;
        Pose frame=s.drag.frame;
        Design::Resize(spec,frame,s.drag.axis,s.drag.side,value);
        return DesignBoundsOf(spec,frame);
    };
    try
    {
        const auto rest=bounds(0),moved=bounds(delta);
        const auto neighbours=DesignNeighbourBounds(s);
        const double tolerance=DesignSnapTolerance(s);
        double best=grid,distance=tolerance;
        auto consider=[&](double before,double after,int axis)
        {
            if(std::abs(after-before)<1e-6)return; // This face is not moving.
            const double offset=Design::SnapToFaces(after,after,neighbours,axis,tolerance);
            if(offset==0 || std::abs(offset)>=distance)return;
            distance=std::abs(offset);
            best=delta+offset/((after-before)/delta);
        };
        for(int axis=0;axis<3;++axis)
        {
            consider(rest.lo[axis],moved.lo[axis],axis);
            consider(rest.hi[axis],moved.hi[axis],axis);
        }
        return best;
    }
    catch(const std::exception&){return grid;}
}
void DesignUpdateDrag(DesignState& s,POINT p)
{
    s.drag.to=p;
    if(std::abs(p.x-s.drag.from.x)>2 || std::abs(p.y-s.drag.from.y)>2)s.drag.moved=true;
    if(s.drag.kind==DesignDrag::Kind::Floor){DesignSetLevel(s,DesignSliderIndexAt(s,p.y));return;}
    if(s.drag.kind==DesignDrag::Kind::Select || !s.drag.moved)return;
    auto world=DesignWorld(s,p.x,p.y);
    const int first=DesignHorizontal(s),second=DesignVertical(s);
    if(s.drag.kind==DesignDrag::Kind::Guide || s.drag.kind==DesignDrag::Kind::RoutePoint)
    {
        // Dragged straight in the workspace; saved when the button is released.
        Json data=DesignData(s);
        auto& target=s.drag.kind==DesignDrag::Kind::Guide
            ? data["guides"][s.drag.index]["position"]
            : data["annotations"][s.drag.index]["points"][s.drag.point];
        auto position=target.get<Vector>();
        for(int axis:{first,second})position[axis]=world[axis];
        target=DesignSnap(s,position);
        s.data=data;
        InvalidateRect(s.canvas,nullptr,FALSE);
        return;
    }
    if(s.drag.kind==DesignDrag::Kind::LightRadius)
    {
        const auto& position=s.drag.devicePose.position;
        const double reach=std::hypot(world[first]-position[first],world[second]-position[second]);
        s.drag.deviceLength=std::clamp(std::round(reach/8)*8,8.0,16000.0);
        InvalidateRect(s.canvas,nullptr,FALSE);
        return;
    }
    if(s.drag.kind==DesignDrag::Kind::Device)
    {
        auto position=s.drag.device.at("position").get<Vector>();
        for(int axis:{first,second})position[axis]+=world[axis]-s.drag.start[axis];
        s.drag.devicePose.position=DesignSnap(s,position);
        InvalidateRect(s.canvas,nullptr,FALSE);
        return;
    }
    if(s.drag.kind==DesignDrag::Kind::DeviceAim)
    {
        // The handle turns the device towards the cursor; a laser's beam also
        // ends there. The top view sets yaw and keeps the pitch; the front and
        // side views set pitch along the beam's own direction.
        const double pi=3.14159265358979323846;
        const auto& position=s.drag.devicePose.position;
        const Rotation current=s.drag.devicePose.rotation;
        const Vector snapped=DesignSnap(s,world);
        Vector target=position;
        if(s.plane==0)
        {
            target[0]=snapped[0];target[1]=snapped[1];
            const double planar=std::hypot(target[0]-position[0],target[1]-position[1]);
            if(planar<1e-6)return;
            const double pitch=current[0]*2*pi/65536;
            target[2]=position[2]+(std::abs(std::cos(pitch))<0.05?0:planar*std::tan(pitch));
        }
        else
        {
            double dh=snapped[first]-position[first];
            const double dz=snapped[second]-position[second];
            if(std::abs(dh)<1e-6 && std::abs(dz)<1e-6)return;
            double yaw=current[1]*2*pi/65536;
            double along=first==0?std::cos(yaw):std::sin(yaw);
            if(std::abs(along)<0.2)
            {
                // Nearly perpendicular to this view: lay the beam along it.
                yaw=first==0?(dh>=0?0:pi):(dh>=0?pi/2:-pi/2);
                along=1;
                if(dh<0)dh=-dh;
            }
            else if(dh*along<0){yaw+=pi;along=-along;}
            const double planar=std::abs(dh)>1e-6?dh/along:0;
            target[0]=position[0]+std::cos(yaw)*planar;
            target[1]=position[1]+std::sin(yaw)*planar;
            target[2]=position[2]+dz;
        }
        const auto aim=Security::AimAt(position,target,current);
        s.drag.devicePose.rotation=aim.rotation;
        if(s.drag.device.value("kind",std::string())=="Laser")s.drag.deviceLength=std::clamp(std::round(aim.length),8.0,65536.0);
        InvalidateRect(s.canvas,nullptr,FALSE);
        return;
    }
    if(s.drag.kind==DesignDrag::Kind::Rotate)
    {
        const double pivotX=s.drag.frame.position[0],pivotY=s.drag.frame.position[1];
        const double now=std::atan2(world[1]-pivotY,world[0]-pivotX);
        double degrees=s.drag.frame.rotation[1]*360.0/65536+(now-s.drag.angle)*180/3.14159265358979323846;
        if(!(GetKeyState(VK_CONTROL)&0x8000))degrees=std::round(degrees/15)*15;
        degrees=std::fmod(degrees,360.0);
        if(degrees<0)degrees+=360;
        s.frame=s.drag.frame;
        s.frame.rotation[1]=static_cast<int>(std::lround(degrees*65536/360))%65536;
        InvalidateRect(s.canvas,nullptr,FALSE);
        return;
    }
    if(s.drag.kind==DesignDrag::Kind::Move)
    {
        auto position=s.drag.frame.position;
        for(int axis:{first,second})position[axis]+=world[axis]-s.drag.start[axis];
        s.frame.position=DesignSnap(s,position);
        s.frame.rotation=s.drag.frame.rotation;
        // Line the piece up with geometry already in the map before falling
        // back to the world grid.
        if(s.snap)try
        {
            const auto moving=DesignBoundsOf(s.pending,{position,s.drag.frame.rotation});
            const auto offset=Design::SnapBoxToNeighbours(moving,DesignNeighbourBounds(s),first,second,DesignSnapTolerance(s));
            for(int axis:{first,second})if(offset[axis]!=0)s.frame.position[axis]=position[axis]+offset[axis];
        }
        catch(const std::exception&) { /* Keep the grid-snapped position. */ }
    }
    else
    {
        // Resizing works along the preview's own axes, so a rotated blockout
        // still grows from the face being dragged. Resize takes how far that
        // face moved; it decides from the side whether the piece grows.
        auto from=TransformPoint(s.drag.start,s.drag.frame,true),to=TransformPoint(world,s.drag.frame,true);
        s.pending=s.drag.spec;
        s.frame=s.drag.frame;
        Design::Resize(s.pending,s.frame,s.drag.axis,s.drag.side,DesignSnapResize(s,to[s.drag.axis]-from[s.drag.axis]));
    }
    InvalidateRect(s.canvas,nullptr,FALSE);
}
void DesignEndDrag(DesignState& s,bool add)
{
    const auto drag=s.drag;
    s.drag={};
    if(drag.kind==DesignDrag::Kind::Floor)return;
    if(drag.kind==DesignDrag::Kind::Guide || drag.kind==DesignDrag::Kind::RoutePoint)
    {
        if(!drag.moved)
        {
            if(drag.kind==DesignDrag::Kind::Guide)DesignEditGuide(s,drag.index);
            else DesignEditAnnotation(s,drag.index);
            return;
        }
        Json data=s.data;
        DesignSave(s,data);
        if(drag.kind==DesignDrag::Kind::Guide)
            DesignStatus(s,"Moved "+data["guides"][drag.index].value("name",std::string("the reference"))+". Double-click it to rename or resize it.");
        else
        {
            auto& annotation=data["annotations"][drag.index];
            DesignStatus(s,annotation.at("kind")=="Route"?Design::RouteSummary(annotation,data):"Moved an annotation point.");
        }
        return;
    }
    if(drag.kind==DesignDrag::Kind::LightRadius)
    {
        if(!drag.moved)
        {
            Editor::Select(Json::array({drag.device}),false);
            DesignRefresh(s);
            if(!s.pending.is_null())DesignDeactivate(s);
            SheetShowLight(s,drag.device);
            DesignStatus(s,"Selected "+SecurityName(drag.device)+". Edit it in the panel; drag it to move it, drag its rim to set the reach.");
            return;
        }
        Editor::SetActorProperties(drag.device,{{"LightRadius",Design::Round(drag.deviceLength/25,3)},{"EchelonRange",Design::Round(drag.deviceLength,1)}});
        DesignRefresh(s);
        DesignStatus(s,SecurityName(drag.device)+" now reaches "+Design::Round(drag.deviceLength)+" units. One Undo step; rebuild lighting to see it.");
        return;
    }
    if(drag.kind==DesignDrag::Kind::Device || drag.kind==DesignDrag::Kind::DeviceAim)
    {
        if(!drag.moved)
        {
            Editor::Select(Json::array({drag.device}),false);
            DesignRefresh(s);
            if(!s.pending.is_null())DesignDeactivate(s);
            if(drag.device.contains("kind"))SheetShowDevice(s,drag.device);
            else SheetShowLight(s,drag.device);
            DesignStatus(s,"Selected "+SecurityName(drag.device)+". Edit it in the panel; drag it to move it, drag its handle to aim it.");
            return;
        }
        Json properties=Json::object();
        if(drag.device.value("kind",std::string())=="Laser")properties["LaserLength"]=std::to_string(static_cast<int>(drag.deviceLength));
        const Json followers=drag.kind==DesignDrag::Kind::Device?DesignGroupOthers(s,Json::array({drag.device})):Json::array();
        Editor::MoveSecurityActor(drag.device,drag.devicePose,properties,followers);
        DesignRefresh(s);
        DesignStatus(s,drag.kind==DesignDrag::Kind::Device
            ? "Moved "+SecurityName(drag.device)+" to "+Design::Round(drag.devicePose.position[0])+", "+Design::Round(drag.devicePose.position[1])+", "+Design::Round(drag.devicePose.position[2])+". One Undo step."
            : "Aimed "+SecurityName(drag.device)+": yaw "+Design::Round(drag.devicePose.rotation[1]*360.0/65536)+" degrees, pitch "
              +Design::Round((drag.devicePose.rotation[0]>32768?drag.devicePose.rotation[0]-65536:drag.devicePose.rotation[0])*360.0/65536)+" degrees"
              +(drag.device.value("kind",std::string())=="Laser"?", "+Design::Round(drag.deviceLength)+" units long.":".")+" One Undo step.");
        if(!followers.empty())DesignStatus(s,"Moved "+SecurityName(drag.device)+" and "+std::to_string(followers.size())+" other selected or grouped actor(s), in one Undo step.");
        return;
    }
    if(drag.kind==DesignDrag::Kind::Select)
    {
        if(drag.moved)DesignBoxSelect(s,add);
        else DesignPick(s,drag.from.x,drag.from.y,add);
        return;
    }
    if(drag.kind==DesignDrag::Kind::Move && drag.fromSelection && !drag.moved)
    {
        // Pressed inside a selected piece and released: an ordinary click.
        DesignDeactivate(s);
        DesignPick(s,drag.from.x,drag.from.y,add);
        return;
    }
    if(!drag.moved || s.pending.is_null())return;
    DesignInspectorRefresh(s);
    const auto summary=drag.kind==DesignDrag::Kind::Move
        ? "Moved to "+Design::Round(s.frame.position[0])+", "+Design::Round(s.frame.position[1])+", "+Design::Round(s.frame.position[2])+"."
        : drag.kind==DesignDrag::Kind::Rotate
        ? "Turned to "+Design::Round(s.frame.rotation[1]*360.0/65536)+" degrees (15 degree steps; hold Ctrl to turn freely)."
        : "Resized to "+Design::Round(s.pending.at("width"))+" x "+Design::Round(s.pending.at("length"))+" x "+Design::Round(s.pending.at("height"))+" units"
          +(Design::StairKind(s.pending.value("kind",std::string()))?", "+std::to_string(s.pending.value("steps",0))+" steps.":".");
    // A placed piece follows the drag straight away, one Undo step per drag.
    // If the brushes cannot follow, the outline goes back to them.
    if(!s.previous.is_null())
    {
        const Vector wanted=s.frame.position;
        if(drag.kind==DesignDrag::Kind::Move)
        {
            Vector delta{};
            for(int axis=0;axis<3;++axis)delta[axis]=s.frame.position[axis]-drag.frame.position[axis];
            DesignPlanFollow(s,delta);
        }
        const size_t following=s.followers.size();
        bool applied=false;
        try{applied=DesignApplyEdit(s,summary);}
        catch(const std::exception&)
        {
            s.pending=s.previous.at("spec");
            s.frame={s.previous.at("position").get<Vector>(),s.previous.at("rotation").get<Rotation>()};
            DesignInspectorRefresh(s);
            throw;
        }
        // A failed apply already explained itself in the status line, and the
        // outline is back on the brushes: nothing else to report.
        if(!applied)return;
        // A sub-unit slip between the preview and the brushes is reported, so
        // it can be seen rather than wondered about.
        try
        {
            const auto& placed=s.previous.is_null()?wanted:s.previous.at("position").get<Vector>();
            double slip=0;
            for(int axis=0;axis<3;++axis)slip=std::max(slip,std::abs(placed[axis]-wanted[axis]));
            if(slip>.05)DesignStatus(s,"Placed "+Design::Round(slip,2)+" units from the preview: the editor put the brushes at "
                +Design::Round(placed[0],2)+", "+Design::Round(placed[1],2)+", "+Design::Round(placed[2],2)+" and the piece followed them.");
        }
        catch(const std::exception&) { /* Diagnostics only. */ }
        if(following>0 && !s.pending.is_null())DesignStatus(s,summary+" "+std::to_string(following)+" other selected or grouped actor(s) moved with it, in the same Undo step.");
    }
    else DesignWarn(s,summary+" Place / Apply creates the brushes.");
}
#include "SecurityPanel.inl"
#include "LightingPanel.inl"
#include "ObjectivePanel.inl"
#include "ElementPanel.inl"
#include "PropertiesPanel.inl"
#include "ScenePanel.inl"
// The library piece whose brush is nearest a point in the view, if any.
Json DesignPieceAt(DesignState& s,double x,double y)
{
    Json nearest;
    double best=14;
    for(auto& actor:s.scene)
    {
        if(!DesignShows(s,actor))continue;
        double distance=DesignPointDistance(DesignScreen(s,actor.at("position").get<Vector>()),x,y);
        for(auto& edge:actor.at("edges"))
        {
            if(!DesignOnFloor(s,edge[0].get<Vector>()[2],edge[1].get<Vector>()[2]))continue;
            distance=std::min(distance,DesignSegmentDistance(DesignScreen(s,edge[0].get<Vector>()),DesignScreen(s,edge[1].get<Vector>()),x,y)+6);
        }
        if(distance<best){best=distance;nearest=actor;}
    }
    if(nearest.is_null())return Json{};
    const auto& data=DesignData(s);
    for(auto it=data.at("pieces").rbegin();it!=data.at("pieces").rend();++it)
        for(auto& member:it->at("members"))
            if(member.at("path")==nearest.at("path"))return *it;
    return Json{};
}
Json DesignDeviceAt(DesignState& s,double x,double y)
{
    Json found;
    double best=12;
    for(const auto& actor:s.securityActors)
    {
        const Vector position=actor.at("position");
        if(!DesignOnFloor(s,position[2],position[2]))continue;
        const double distance=DesignPointDistance(DesignScreen(s,position),x,y);
        if(distance<best){best=distance;found=actor;}
    }
    return found;
}
// A new piece previewed at a point in the view, sized from the movement limits.
void DesignBlockoutAt(DesignState& s,const std::string& kind,const Vector& at)
{
    const auto movement=Design::Movement(DesignData(s));
    Json spec={{"kind",kind},{"construction","Carve"},{"thickness",16},{"steps",8},{"ceiling",true},{"portal",false},{"name",kind}};
    if(kind=="Room"){spec["width"]=1024;spec["length"]=1024;spec["height"]=256;}
    else
    {
        spec["width"]=Design::ClearanceWidth(kind,movement)+16;
        spec["length"]=512;
        spec["height"]=Design::Crouching(kind)?Design::ClearanceHeight(kind,movement):256;
    }
    Design::Geometry(spec);
    s.pending=spec;
    s.previous=Json{};
    s.frame={at,{}};
    s.drag={};
    DesignInspectorRefresh(s);
    InvalidateRect(s.canvas,nullptr,FALSE);
    DesignWarn(s,Fold(kind)+" preview at "+Design::Round(at[0])+", "+Design::Round(at[1])+", "+Design::Round(at[2])+". Drag or edit it, then Place / Apply.");
}
// Right-click: first what applies to the thing under the cursor, then what can
// start at that point.
// Deletes a piece's brushes through the editor and drops it from the library.
void DesignDeletePiece(DesignState& s,const Json& piece)
{
    Json data=DesignData(s);
    Editor::Select(piece.at("members"),false);
    if(!Editor::Exec("ACTOR DELETE"))throw std::runtime_error("The editor refused to delete the brushes.");
    auto& pieces=data["pieces"];
    pieces.erase(std::remove(pieces.begin(),pieces.end(),piece),pieces.end());
    if(s.previous==piece)DesignDeactivate(s);
    DesignRefresh(s);
    DesignSave(s,data,false);
    InvalidateRect(s.canvas,nullptr,FALSE);
    DesignStatus(s,"Deleted "+piece.at("spec").value("name",std::string("the piece"))+" and its brushes. Undo brings them back; rebuild geometry afterwards.");
}
// The Delete key: whatever the panel is showing, then the piece being edited,
// then a new preview, then the editor's own selection.
void DesignDeleteSelection(DesignState& s)
{
    const auto kind=s.sheet.kind;
    const Json subject=s.sheet.subject;
    if(kind=="light" || kind=="device"){SheetButton(s,1);return;}
    if(kind=="guide" || kind=="annotation"){SheetButton(s,0);return;}
    if(!s.previous.is_null()){DesignDeletePiece(s,s.previous);return;}
    if(!s.pending.is_null()){DesignDeactivate(s);DesignStatus(s,"Preview discarded. The map is unchanged.");return;}
    Json selected=Json::array();
    for(const auto& actor:s.scene)if(actor.value("selected",false))selected.push_back(actor);
    if(selected.empty())throw std::runtime_error("Nothing selected: click a piece, light, device, reference or route point first.");
    if(!Editor::Exec("ACTOR DELETE"))throw std::runtime_error("The editor refused to delete the selection.");
    Json data=DesignData(s);
    auto& pieces=data["pieces"];
    const size_t before=pieces.size();
    pieces.erase(std::remove_if(pieces.begin(),pieces.end(),[&](const Json& piece)
    {
        for(auto& member:piece.at("members"))
            for(auto& actor:selected)
                if(member.at("path")==actor.at("path"))return true;
        return false;
    }),pieces.end());
    DesignRefresh(s);
    if(pieces.size()!=before)DesignSave(s,data,false);
    InvalidateRect(s.canvas,nullptr,FALSE);
    DesignStatus(s,"Deleted "+std::to_string(selected.size())+" selected actor(s). Undo brings them back; rebuild geometry if brushes went.");
}
// A tooltip for a control, through the panel's one tooltip window.
void DesignTip(DesignState& s,HWND control,const char* text)
{
    if(!control)return;
    if(!s.tips)
    {
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);
        s.tips=CreateWindowExA(WS_EX_TOPMOST,TOOLTIPS_CLASSA,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,s.window,nullptr,GetModuleHandle(nullptr),nullptr);
        if(!s.tips)return;
        SendMessage(s.tips,TTM_SETMAXTIPWIDTH,0,380);
        SendMessage(s.tips,TTM_SETDELAYTIME,TTDT_AUTOPOP,20000);
    }
    TOOLINFOA info{};
    info.cbSize=sizeof(info);
    info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
    info.hwnd=s.window;
    info.uId=reinterpret_cast<UINT_PTR>(control);
    info.lpszText=const_cast<char*>(text);
    SendMessageA(s.tips,TTM_ADDTOOLA,0,reinterpret_cast<LPARAM>(&info));
}
const char* const kDesignKeyLegend=
    "DESIGN VIEW\r\n"
    "Wheel: zoom.   Middle drag: pan.   Right-click: actions at that point.\r\n"
    "Click: select. A piece's brush selects the whole piece; a group member selects the group.\r\n"
    "Drag on empty space: box select. Left to right takes what is wholly inside, right to left what it touches.\r\n"
    "Shift or Ctrl + click: add to the selection.\r\n"
    "Drag a piece: move it (and the rest of the selection). Drag a square: resize. Drag the round handle: turn (15 degree steps; Ctrl: free).\r\n"
    "R / Shift+R: turn the edited piece 90 degrees either way.\r\n"
    "Arrow keys: nudge the edited piece by the grid. Ctrl + arrows: one unit.\r\n"
    "Enter: place a new preview.   Esc: discard it, or cancel what is being placed.\r\n"
    "Delete: delete the selection.   B: build geometry.\r\n"
    "Ctrl+Z / Ctrl+Y: undo / redo.   Ctrl+L / Ctrl+Shift+L: lock / unlock the selection.\r\n"
    "Page Up / Page Down: one storey up / down.   Home: every storey.\r\n"
    "Slider on the right: pick a storey.   + badge on a wall: add a neighbour there.\r\n"
    "\r\n"
    "SCENE PANEL\r\n"
    "Ctrl+A: select all rows.   Delete: delete.   Ctrl+G: new group from the rows.\r\n"
    "Ctrl+L / Ctrl+Shift+L: lock / unlock.   Ctrl+H / Ctrl+Shift+H: hide / show.\r\n"
    "Tick: show.   Click LOCKED: toggle a lock.   Click a header: fold it.   Double-click: show in the plan.\r\n"
    "\r\n"
    "EDITOR VIEWPORTS\r\n"
    "Ctrl + wheel over a viewport: change the grid the plan snaps to.\r\n";
LRESULT CALLBACK DesignKeysProc(HWND window,UINT message,WPARAM w,LPARAM l)
{
    auto s=reinterpret_cast<DesignState*>(GetWindowLongPtr(window,GWLP_USERDATA));
    if(message==WM_NCCREATE)
    {
        s=static_cast<DesignState*>(reinterpret_cast<CREATESTRUCT*>(l)->lpCreateParams);
        SetWindowLongPtr(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));
    }
    if(!s)return DefWindowProcA(window,message,w,l);
    if(message==WM_CREATE)
    {
        auto text=CreateWindowExA(0,"EDIT",kDesignKeyLegend,WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,8,8,600,400,window,reinterpret_cast<HMENU>(1),GetModuleHandle(nullptr),nullptr);
        SendMessage(text,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        return 0;
    }
    if(message==WM_SIZE){MoveWindow(GetDlgItem(window,1),8,8,std::max(1,LOWORD(l)-16),std::max(1,HIWORD(l)-16),TRUE);return 0;}
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}
    if(message==WM_NCDESTROY){s->keysWindow=nullptr;SetWindowLongPtr(window,GWLP_USERDATA,0);}
    return DefWindowProcA(window,message,w,l);
}
void DesignKeys(DesignState& s)
{
    if(!s.keysWindow)
    {
        WNDCLASSA wc{};
        wc.hInstance=GetModuleHandle(nullptr);
        wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
        wc.lpfnWndProc=DesignKeysProc;
        wc.lpszClassName="ReloadedDesignKeys";
        RegisterClassA(&wc);
        s.keysWindow=CreateWindowExA(WS_EX_TOOLWINDOW,wc.lpszClassName,"Keyboard and mouse",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,640,440,s.window,nullptr,wc.hInstance,&s);
        if(!s.keysWindow)throw std::runtime_error("Cannot open the key legend.");
    }
    ShowWindow(s.keysWindow,SW_SHOWNORMAL);
    SetForegroundWindow(s.keysWindow);
}
// Locks or unlocks the editor's selection. Locked actors are skipped by
// clicks, box selection and drags in the plan, and the editor refuses to
// move them.
void DesignLockSelection(DesignState& s,bool lock)
{
    Json selected=Json::array();
    for(const auto& actor:s.scene)if(actor.value("selected",false))selected.push_back(Json{{"path",actor.at("path")},{"class",actor.at("class")}});
    if(selected.empty())throw std::runtime_error(std::string("Nothing selected to ")+(lock?"lock":"unlock")+". Click or box-select first.");
    Editor::DesignSetFlags(selected,-1,lock?1:0);
    if(lock)DesignDeactivate(s);
    DesignRefresh(s);
    DesignStatus(s,(lock?"Locked ":"Unlocked ")+std::to_string(selected.size())+" actor(s)."+(lock?" They cannot be clicked, box-selected or dragged in the plan until unlocked (Ctrl+Shift+L, the right-click menu, or the Scene panel).":""));
}
// Right-click on the floor slider: every storey, or a typed height range.
void DesignSliderMenu(DesignState& s,POINT at)
{
    HMENU menu=CreatePopupMenu();
    if(!menu)throw std::runtime_error("Could not open the menu.");
    AppendMenuA(menu,MF_STRING|(s.floorFilter?0:MF_GRAYED),DCtxSliderAll,"Show every storey\tHome");
    AppendMenuA(menu,MF_STRING,DFloor,"Custom height range...");
    POINT screen=at;
    ClientToScreen(s.canvas,&screen);
    const auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN,screen.x,screen.y,0,s.canvas,nullptr);
    DestroyMenu(menu);
    if(choice)DesignCommand(s,choice);
}
void DesignContextMenu(DesignState& s,POINT at)
{
    const auto world=DesignSnap(s,DesignWorld(s,at.x,at.y));
    size_t guide=0,annotation=0,point=0;
    const bool onGuide=DesignGuideAt(s,at.x,at.y,guide);
    const bool onAnnotation=!onGuide && DesignRoutePointAt(s,at.x,at.y,annotation,point);
    const Json light=LightAt(s,at.x,at.y);
    const Json gameActor=light.is_null()?ObjectiveAt(s,at.x,at.y):Json{};
    const Json device=(light.is_null() && gameActor.is_null())?DesignDeviceAt(s,at.x,at.y):Json{};
    DesignHoverWall(s,at);
    const Json piece=DesignPieceAt(s,at.x,at.y);
    Json data=DesignData(s);
    HMENU menu=CreatePopupMenu();
    if(!menu)throw std::runtime_error("Could not open the menu.");
    auto item=[&](int id,const std::string& text){AppendMenuA(menu,MF_STRING,id,text.c_str());};
    auto separator=[&]{AppendMenuA(menu,MF_SEPARATOR,0,nullptr);};
    if(onGuide)
    {
        const auto name=data.at("guides")[guide].value("name",std::string("reference"));
        item(DCtxEditGuide,"Edit "+name+"...");
        item(DCtxRemoveGuide,"Remove "+name);
        separator();
    }
    if(onAnnotation)
    {
        const auto name=data.at("annotations")[annotation].value("name",std::string("annotation"));
        item(DCtxEditAnnotation,"Edit "+name+"...");
        item(DCtxRemoveAnnotation,"Remove "+name);
        separator();
    }
    std::vector<Json> wireTargets;
    std::vector<Json> missions;
    for(const auto& actor:s.objectives)if(actor.value("kind",std::string())=="Mission")missions.push_back(actor);
    if(!gameActor.is_null())
    {
        item(DCtxSelectObjective,"Select "+ObjectiveLabel(gameActor)+" in the editor");
        item(DCtxDeleteObjective,"Delete "+ObjectiveLabel(gameActor));
        separator();
    }
    if(!light.is_null())
    {
        item(DCtxEditLight,"Edit "+LightLabel(light)+"...");
        item(DCtxSelectLight,"Select "+LightLabel(light)+" in the editor");
        HMENU switched=CreatePopupMenu();
        for(const auto& other:s.securityActors)
        {
            if(other.value("kind",std::string())!="Alarm")continue;
            if(DCtxWireFirst+static_cast<int>(wireTargets.size())>DCtxWireLast)break;
            bool already=false;
            for(const auto& event:other.value("events",Json::array()))if(Fold(event.get<std::string>())==Fold(light.value("tag",std::string())))already=true;
            const auto label=other.value("name",std::string())+" ("+SecurityName(other)+")";
            AppendMenuA(switched,MF_STRING|(already?MF_CHECKED:0),DCtxWireFirst+static_cast<int>(wireTargets.size()),label.c_str());
            wireTargets.push_back(other);
        }
        if(wireTargets.empty())AppendMenuA(switched,MF_STRING|MF_GRAYED,0,"No alarms in the map yet");
        AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(switched),light.value("switchable",false)?"Switched on by alarm":"Switched on by alarm (needs a switchable light)");
        item(DCtxDeleteLight,"Delete "+LightLabel(light));
        separator();
    }
    if(!device.is_null())
    {
        const auto kind=device.value("kind",std::string());
        if(Security::Find(kind))item(DCtxEditDevice,"Edit "+Fold(kind)+" "+SecurityName(device)+"...");
        if(kind=="Laser" || kind=="Camera" || kind=="Motion sensor")item(DCtxSnapWall,"Snap to the nearest wall, facing in");
        item(DCtxSelectDevice,"Select "+Fold(kind.empty()?"device":kind)+" "+SecurityName(device)+" in the editor");
        auto wireMenu=[&](bool detectorsOfAlarm)
        {
            HMENU wire=CreatePopupMenu();
            for(const auto& other:s.securityActors)
            {
                const auto otherKind=other.value("kind",std::string());
                if(detectorsOfAlarm?!Security::Detector(otherKind):otherKind!="Alarm")continue;
                if(DCtxWireFirst+static_cast<int>(wireTargets.size())>DCtxWireLast)break;
                const bool wired=detectorsOfAlarm
                    ? Fold(other.value("event",std::string()))==Fold(device.value("tag",std::string()))
                    : Fold(device.value("event",std::string()))==Fold(other.value("tag",std::string()));
                const auto label=detectorsOfAlarm
                    ? otherKind+" "+SecurityName(other)
                    : other.value("name",std::string())+" ("+SecurityName(other)+", Tag "+other.value("tag",std::string("None"))+")";
                AppendMenuA(wire,MF_STRING|(wired?MF_CHECKED:0),DCtxWireFirst+static_cast<int>(wireTargets.size()),label.c_str());
                wireTargets.push_back(other);
            }
            if(wireTargets.empty())AppendMenuA(wire,MF_STRING|MF_GRAYED,0,detectorsOfAlarm?"No detectors in the map yet":"No alarms in the map yet: add one first");
            if(!detectorsOfAlarm)
            {
                AppendMenuA(wire,MF_SEPARATOR,0,nullptr);
                AppendMenuA(wire,MF_STRING,DCtxUnwire,"Nothing (unwire)");
            }
            AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(wire),detectorsOfAlarm?"Wire a detector to this alarm":"Wire to alarm");
        };
        if(Security::Detector(kind))wireMenu(false);
        if(kind=="Alarm")
        {
            wireMenu(true);
            item(DCtxUseAlarm,"Use this alarm for new detectors");
            item(DCtxAlarmOutputs,"This alarm triggers the selected actors");
        }
        if(kind=="Alarm")item(DCtxAlarmLock,"When triggered, lock the selected doors, lifts and objective triggers");
        item(DCtxDeleteDevice,"Delete "+SecurityName(device));
        separator();
    }
    if(!s.hoverPiece.is_null())
    {
        item(DAddDoorway,"Doorway through this wall");
        item(DAddCorridor,"Corridor from this wall");
        item(DAddVent,"Vent from this wall");
        item(DAddRoom,"Room against this wall");
        separator();
    }
    if(!piece.is_null())
    {
        const auto name=piece.at("spec").value("name",std::string("piece"));
        item(DCtxEditPiece,"Edit "+name);
        HMENU turn=CreatePopupMenu();
        AppendMenuA(turn,MF_STRING,DCtxTurnCW,"90 degrees\tR");
        AppendMenuA(turn,MF_STRING,DCtxTurnCCW,"-90 degrees\tShift+R");
        AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(turn),("Turn "+name).c_str());
        item(DCtxSelectPiece,"Select the brushes of "+name);
        item(DCtxDetachPiece,"Detach "+name+" (keep its brushes)");
        item(DCtxDeletePiece,"Delete "+name+" and its brushes");
        separator();
    }
    {
        // Locking what is under the cursor, then the selection.
        std::string what;
        bool locked=false;
        if(!gameActor.is_null()){what=ObjectiveLabel(gameActor);locked=DesignLockedPath(s,gameActor.at("path").get<std::string>());}
        else if(!light.is_null()){what=LightLabel(light);locked=DesignLockedPath(s,light.at("path").get<std::string>());}
        else if(!device.is_null()){what=SecurityName(device);locked=DesignLockedPath(s,device.at("path").get<std::string>());}
        else if(!piece.is_null())
        {
            what=piece.at("spec").value("name",std::string("piece"));
            locked=true;
            for(auto& member:piece.at("members"))if(!DesignLockedPath(s,member.at("path").get<std::string>()))locked=false;
        }
        size_t selected=0;
        for(const auto& actor:s.scene)if(actor.value("selected",false))++selected;
        HMENU locks=CreatePopupMenu();
        if(!what.empty())AppendMenuA(locks,MF_STRING,locked?DCtxUnlock:DCtxLock,((locked?"Unlock ":"Lock ")+what).c_str());
        if(selected>0)
        {
            AppendMenuA(locks,MF_STRING,DCtxLockSelection,("Lock the "+std::to_string(selected)+" selected actor(s)\tCtrl+L").c_str());
            AppendMenuA(locks,MF_STRING,DCtxUnlockSelection,("Unlock the "+std::to_string(selected)+" selected actor(s)\tCtrl+Shift+L").c_str());
        }
        if(!what.empty() || selected>0)AppendMenuA(locks,MF_SEPARATOR,0,nullptr);
        AppendMenuA(locks,MF_STRING,DCtxScene,"Scene panel: groups, visibility, locks...");
        AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(locks),what.empty()?"Lock, unlock, groups":("Lock, unlock, groups ("+what+")").c_str());
        separator();
    }
    if(!s.pending.is_null())
    {
        item(DPlace,s.previous.is_null()?"Place preview":"Apply changes");
        item(DDiscard,s.previous.is_null()?"Discard preview":"Stop editing");
        separator();
    }
    item(DCtxRoomHere,"New room here");
    item(DCtxCorridorHere,"New corridor here");
    item(DCtxVentHere,"New vent here");
    separator();
    // Game mode: starts, the mission, objectives and their devices.
    HMENU starts=CreatePopupMenu();
    AppendMenuA(starts,MF_STRING,DCtxStartSpy,"Spy start");
    AppendMenuA(starts,MF_STRING,DCtxStartMerc,"Merc start");
    SetMenuItemBitmaps(starts,DCtxStartSpy,MF_BYCOMMAND,ObjectiveMenuIcon(true),ObjectiveMenuIcon(true));
    SetMenuItemBitmaps(starts,DCtxStartMerc,MF_BYCOMMAND,ObjectiveMenuIcon(false),ObjectiveMenuIcon(false));
    AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(starts),"Player start here");
    if(missions.empty())item(DCtxMission,"Mission here");
    else if(missions.size()==1)item(DCtxObjective,"Objective here (under "+missions[0].value("name",std::string("the mission"))+")");
    else
    {
        HMENU under=CreatePopupMenu();
        for(size_t i=0;i<missions.size() && DCtxMissionFirst+static_cast<int>(i)<=DCtxMissionLast;++i)
            AppendMenuA(under,MF_STRING,DCtxMissionFirst+static_cast<int>(i),("Under "+missions[i].value("name",std::string())+" ("+SecurityName(missions[i])+")").c_str());
        AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(under),"Objective here");
    }
    // Devices go under the nearest objective.
    Json nearestObjective;
    {
        double best=1e18;
        for(const auto& actor:s.objectives)
        {
            if(actor.value("kind",std::string())!="Objective")continue;
            const Vector p=actor.at("position");
            const double d=std::hypot(p[0]-world[0],p[1]-world[1]);
            if(d<best){best=d;nearestObjective=actor;}
        }
    }
    HMENU devicesUnder=CreatePopupMenu();
    const UINT deviceState=nearestObjective.is_null()?MF_GRAYED:0;
    AppendMenuA(devicesUnder,MF_STRING|deviceState,DCtxComputer,"Computer terminal");
    AppendMenuA(devicesUnder,MF_STRING|deviceState,DCtxBomb,"Bomb target");
    AppendMenuA(devicesUnder,MF_STRING|deviceState,DCtxFlag,"Flag and drop zone");
    AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(devicesUnder),nearestObjective.is_null()?"Objective device here (add an objective first)":("Objective device here (for "+nearestObjective.value("name",std::string("the objective"))+")").c_str());
    HMENU elements=CreatePopupMenu();
    const auto& elementKinds=ElementKinds();
    for(size_t i=0;i<elementKinds.size() && DCtxElementFirst+static_cast<int>(i)<=DCtxElementLast;++i)
        AppendMenuA(elements,MF_STRING,DCtxElementFirst+static_cast<int>(i),elementKinds[i].name);
    AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(elements),"Gameplay element from here");
    HMENU floors=CreatePopupMenu();
    AppendMenuA(floors,MF_STRING,DCtxFloorUp,"Go up one floor");
    AppendMenuA(floors,MF_STRING,DCtxFloorDown,"Go down one floor");
    AppendMenuA(floors,MF_SEPARATOR,0,nullptr);
    AppendMenuA(floors,MF_STRING,DCtxFloorDupUp,"Duplicate this floor above");
    AppendMenuA(floors,MF_STRING,DCtxFloorDupDown,"Duplicate this floor below");
    AppendMenuA(floors,MF_SEPARATOR,0,nullptr);
    HMENU stairsUp=CreatePopupMenu(),stairsDown=CreatePopupMenu();
    for(int i=0;i<4;++i)
    {
        AppendMenuA(stairsUp,MF_STRING,DCtxStairsUpFirst+i,kStairLabels[i]);
        AppendMenuA(stairsDown,MF_STRING,DCtxStairsDownFirst+i,kStairLabels[i]);
    }
    AppendMenuA(floors,MF_POPUP,reinterpret_cast<UINT_PTR>(stairsUp),"Stairs up from here");
    AppendMenuA(floors,MF_POPUP,reinterpret_cast<UINT_PTR>(stairsDown),"Stairs down from here");
    AppendMenuA(floors,MF_STRING,DCtxLadderUp,"Ladder up from here");
    AppendMenuA(floors,MF_STRING,DCtxPipeUp,"Pipe up from here");
    AppendMenuA(floors,MF_STRING,DCtxOpening,"Opening through the floor above");
    AppendMenuA(floors,MF_STRING,DCtxLiftUp,"Lift up to the floor above");
    AppendMenuA(floors,MF_STRING,DCtxLiftDown,"Lift down to the floor below");
    AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(floors),"Floors");
    separator();
    HMENU lightsMenu=CreatePopupMenu();
    const auto& presets=LightPresets();
    for(size_t i=0;i<presets.size() && DCtxLightFirst+static_cast<int>(i)<=DCtxLightLast;++i)
        AppendMenuA(lightsMenu,MF_STRING,DCtxLightFirst+static_cast<int>(i),presets[i].name);
    AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(lightsMenu),"Light here");
    item(DCtxGuideHere,"Player reference here...");
    item(DCtxRouteHere,"Route / objective from here...");
    item(DCtxMeasureHere,"Measure from here");
    item(DCtxPlayHere,"Playtest from here...");
    HMENU devices=CreatePopupMenu();
    const auto& catalogue=Security::Devices();
    for(size_t i=0;i<catalogue.size() && DCtxDeviceFirst+static_cast<int>(i)<=DCtxDeviceLast;++i)
        AppendMenuA(devices,MF_STRING,DCtxDeviceFirst+static_cast<int>(i),catalogue[i].kind);
    AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(devices),"Security device here");
    separator();
    item(DFit,"Fit map / preview");
    item(DBuild,"Build geometry");
    POINT screen=at;
    ClientToScreen(s.canvas,&screen);
    const int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN,screen.x,screen.y,0,s.canvas,nullptr);
    DestroyMenu(menu);
    if(!choice)return;
    if(choice>=DAddDoorway && choice<=DAddRoom)
    {
        DesignQuickAdd(s,choice==DAddDoorway?"Doorway":choice==DAddCorridor?"Corridor":choice==DAddVent?"Vent":"Room");
        return;
    }
    if(choice==DCtxEditGuide){DesignEditGuide(s,guide);return;}
    if(choice==DCtxEditAnnotation){DesignEditAnnotation(s,annotation);return;}
    if(choice==DCtxRemoveGuide || choice==DCtxRemoveAnnotation)
    {
        if(choice==DCtxRemoveGuide)data["guides"].erase(guide);
        else data["annotations"].erase(annotation);
        DesignSave(s,data);
        InvalidateRect(s.canvas,nullptr,FALSE);
        DesignStatus(s,choice==DCtxRemoveGuide?"Reference removed. Undo brings it back.":"Annotation removed. Undo brings it back.");
        return;
    }
    if(choice==DCtxEditDevice){SecurityEditForm(s,device);return;}
    if(choice==DCtxSnapWall){SecuritySnapToWall(s,device);return;}
    if(choice==DCtxSelectDevice)
    {
        Editor::Select(Json::array({device}),true);
        DesignRefresh(s);
        DesignStatus(s,"Selected "+SecurityName(device)+" in the editor. Its properties are in the editor's property window.");
        return;
    }
    if(choice==DCtxFloorUp || choice==DCtxFloorDown){FloorGo(s,world,choice==DCtxFloorUp);return;}
    if(choice==DCtxFloorDupUp || choice==DCtxFloorDupDown){FloorDuplicate(s,world,choice==DCtxFloorDupUp);return;}
    if(choice>=DCtxStairsUpFirst && choice<DCtxStairsUpFirst+4){FloorStairs(s,world,true,kStairKinds[choice-DCtxStairsUpFirst]);return;}
    if(choice>=DCtxStairsDownFirst && choice<DCtxStairsDownFirst+4){FloorStairs(s,world,false,kStairKinds[choice-DCtxStairsDownFirst]);return;}
    if(choice==DCtxLadderUp || choice==DCtxPipeUp){FloorClimb(s,world,choice==DCtxLadderUp?"Ladder":"Pipe");return;}
    if(choice==DCtxOpening){FloorOpening(s,world);return;}
    if(choice==DCtxLiftUp || choice==DCtxLiftDown){FloorLift(s,world,choice==DCtxLiftUp);return;}
    if(choice>=DCtxElementFirst && choice<=DCtxElementLast && static_cast<size_t>(choice-DCtxElementFirst)<elementKinds.size())
    {
        ElementBegin(s,elementKinds[choice-DCtxElementFirst].name,&world);
        return;
    }
    if(choice==DCtxStartSpy || choice==DCtxStartMerc){ObjectivePlaceStart(s,choice==DCtxStartSpy?kSpyTeam:kMercTeam,world);return;}
    if(choice==DCtxMission){ObjectivePlaceMission(s,world);return;}
    if(choice==DCtxObjective && !missions.empty()){ObjectivePlaceUnder(s,missions[0],"SBase.SObjective",world);return;}
    if(choice>=DCtxMissionFirst && choice<=DCtxMissionLast && static_cast<size_t>(choice-DCtxMissionFirst)<missions.size())
    {
        ObjectivePlaceUnder(s,missions[choice-DCtxMissionFirst],"SBase.SObjective",world);
        return;
    }
    if((choice==DCtxComputer || choice==DCtxBomb || choice==DCtxFlag) && !nearestObjective.is_null())
    {
        ObjectivePlaceUnder(s,nearestObjective,choice==DCtxComputer?"SBase.SComputerObjectiveTrigger":choice==DCtxBomb?"SBase.SBombTargetObjectiveTrigger":"SBase.SFlag",world);
        return;
    }
    if(choice==DCtxSelectObjective)
    {
        Editor::Select(Json::array({gameActor}),true);
        DesignRefresh(s);
        DesignStatus(s,"Selected "+ObjectiveLabel(gameActor)+" in the editor; its name and settings are in the property window.");
        return;
    }
    if(choice==DCtxDeleteObjective)
    {
        Editor::DeleteSecurityActor(gameActor);
        DesignRefresh(s);
        DesignStatus(s,"Deleted "+ObjectiveLabel(gameActor)+". Undo brings it back.");
        return;
    }
    if(choice==DCtxAlarmLock)
    {
        const auto locked=Editor::AddAlarmLocks(device,Editor::SelectedIdentities());
        DesignRefresh(s);
        DesignStatus(s,SecurityName(device)+" now locks "+std::to_string(locked.value("movers",size_t{0}))+" mover(s) and "+std::to_string(locked.value("triggers",size_t{0}))+" objective trigger(s) while it sounds. One Undo step.");
        return;
    }
    if(choice==DCtxEditLight){LightEditForm(s,light);return;}
    if(choice==DCtxSelectLight)
    {
        Editor::Select(Json::array({light}),true);
        DesignRefresh(s);
        DesignStatus(s,"Selected "+SecurityName(light)+" in the editor.");
        return;
    }
    if(choice==DCtxDeleteLight)
    {
        Editor::DeleteSecurityActor(light);
        DesignRefresh(s);
        DesignStatus(s,"Deleted "+SecurityName(light)+". Undo brings it back.");
        return;
    }
    if(choice>=DCtxLightFirst && choice<=DCtxLightLast && static_cast<size_t>(choice-DCtxLightFirst)<presets.size())
    {
        LightPlace(s,presets[choice-DCtxLightFirst],world);
        return;
    }
    if(!light.is_null() && choice>=DCtxWireFirst && choice<=DCtxWireLast && static_cast<size_t>(choice-DCtxWireFirst)<wireTargets.size())
    {
        const auto& alarm=wireTargets[choice-DCtxWireFirst];
        Editor::AddAlarmOutputs(alarm,Json::array({light}));
        DesignRefresh(s);
        DesignStatus(s,alarm.value("name",std::string())+" ("+SecurityName(alarm)+") now switches "+SecurityName(light)+" on. One Undo step.");
        return;
    }
    if(choice>=DCtxWireFirst && choice<=DCtxWireLast && static_cast<size_t>(choice-DCtxWireFirst)<wireTargets.size())
    {
        const auto& other=wireTargets[choice-DCtxWireFirst];
        const bool deviceIsDetector=Security::Detector(device.value("kind",std::string()));
        const Json& detector=deviceIsDetector?device:other;
        const Json& alarm=deviceIsDetector?other:device;
        Editor::LinkDetectorToAlarm(detector,alarm);
        DesignRefresh(s);
        DesignStatus(s,SecurityName(detector)+" now triggers "+alarm.value("name",std::string())+" ("+SecurityName(alarm)+"). One Undo step.");
        return;
    }
    if(choice==DCtxUnwire)
    {
        Editor::UnwireDetector(device);
        DesignRefresh(s);
        DesignStatus(s,SecurityName(device)+" no longer triggers anything.");
        return;
    }
    if(choice==DCtxUseAlarm || choice==DCtxAlarmOutputs)
    {
        s.securityAlarm=device.at("path");
        SecurityRefreshList(s);
        if(choice==DCtxAlarmOutputs)SecurityOutputs(s);
        else DesignStatus(s,"New detectors will be wired to "+device.value("name",std::string())+" ("+SecurityName(device)+").");
        return;
    }
    if(choice==DCtxDeleteDevice)
    {
        Editor::DeleteSecurityActor(device);
        DesignRefresh(s);
        DesignStatus(s,"Deleted "+SecurityName(device)+". Undo brings it back.");
        return;
    }
    if(choice==DCtxLock || choice==DCtxUnlock)
    {
        Json members=Json::array();
        std::string what;
        if(!gameActor.is_null()){members.push_back(gameActor);what=ObjectiveLabel(gameActor);}
        else if(!light.is_null()){members.push_back(light);what=LightLabel(light);}
        else if(!device.is_null()){members.push_back(device);what=SecurityName(device);}
        else if(!piece.is_null()){members=piece.at("members");what=piece.at("spec").value("name",std::string("the piece"));}
        Editor::DesignSetFlags(members,-1,choice==DCtxLock?1:0);
        if(choice==DCtxLock && !s.previous.is_null() && !piece.is_null() && s.previous.at("members")==piece.at("members"))DesignDeactivate(s);
        DesignRefresh(s);
        DesignStatus(s,(choice==DCtxLock?"Locked ":"Unlocked ")+what+(choice==DCtxLock?": it cannot be clicked, box-selected or dragged in the plan until unlocked.":"."));
        return;
    }
    if(choice==DCtxLockSelection || choice==DCtxUnlockSelection){DesignLockSelection(s,choice==DCtxLockSelection);return;}
    if(choice==DCtxScene){SceneOpen(s);return;}
    if(choice==DCtxTurnCW || choice==DCtxTurnCCW)
    {
        if(s.previous.is_null() || s.previous.at("members")!=piece.at("members"))
        {
            Editor::Select(piece.at("members"),false);
            DesignRefresh(s);
            DesignActivate(s,piece);
        }
        DesignTurn(s,choice==DCtxTurnCW?90:-90);
        return;
    }
    if(choice==DCtxDeletePiece){DesignDeletePiece(s,piece);return;}
    if(choice==DCtxEditPiece || choice==DCtxSelectPiece)
    {
        Editor::Select(piece.at("members"),choice==DCtxSelectPiece);
        DesignRefresh(s);
        if(choice==DCtxEditPiece)
        {
            DesignActivate(s,piece);
            DesignWarn(s,"Editing "+piece.at("spec").value("name",std::string("this piece"))+". Drag it, drag a square to resize, use the arrow keys, or edit its fields.");
        }
        else DesignStatus(s,"Selected the brushes of "+piece.at("spec").value("name",std::string("the piece"))+" in the editor.");
        return;
    }
    if(choice==DCtxDetachPiece)
    {
        auto& pieces=data["pieces"];
        pieces.erase(std::remove(pieces.begin(),pieces.end(),piece),pieces.end());
        DesignSave(s,data);
        if(s.previous==piece)DesignDeactivate(s);
        InvalidateRect(s.canvas,nullptr,FALSE);
        DesignStatus(s,"Detached. The brushes remain in the map for manual editing.");
        return;
    }
    if(choice==DCtxRoomHere || choice==DCtxCorridorHere || choice==DCtxVentHere)
    {
        DesignBlockoutAt(s,choice==DCtxRoomHere?"Room":choice==DCtxCorridorHere?"Corridor":"Vent",world);
        return;
    }
    if(choice==DCtxGuideHere || choice==DCtxRouteHere || choice==DCtxMeasureHere || choice==DCtxPlayHere)
    {
        s.contextPoint=world;
        s.contextPointSet=true;
        try{DesignCommand(s,choice==DCtxGuideHere?DGuides:choice==DCtxRouteHere?DRoute:choice==DCtxMeasureHere?DMeasure:DPlay);}
        catch(...){s.contextPointSet=false;throw;}
        s.contextPointSet=false;
        return;
    }
    if(choice>=DCtxDeviceFirst && choice<=DCtxDeviceLast)
    {
        SecurityBegin(s,catalogue[choice-DCtxDeviceFirst].kind);
        SecurityClick(s,world);
        return;
    }
    DesignCommand(s,choice);
}
LRESULT CALLBACK DesignCanvasProc(HWND window,UINT message,WPARAM w,LPARAM l)
{
    auto s=reinterpret_cast<DesignState*>(GetWindowLongPtr(window,GWLP_USERDATA));
    if(!s)return DefWindowProcA(window,message,w,l);
    try
    {
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_PRINTCLIENT){DesignPaint(*s,reinterpret_cast<HDC>(w));return 0;}
        if(message==WM_PAINT)
        {
            PAINTSTRUCT ps{};
            auto dc=BeginPaint(window,&ps);
            try{DesignPaint(*s,dc);}catch(...){EndPaint(window,&ps);throw;}
            EndPaint(window,&ps);
            return 0;
        }
        if(message==WM_MBUTTONDOWN){s->panning=true;s->last={GET_X_LPARAM(l),GET_Y_LPARAM(l)};SetCapture(window);return 0;}
        if(message==WM_MBUTTONUP){s->panning=false;ReleaseCapture();return 0;}
        if(message==WM_MOUSEMOVE)
        {
            POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            if(s->panning)
            {
                s->panX+=p.x-s->last.x;s->panY+=p.y-s->last.y;s->last=p;
                InvalidateRect(window,nullptr,FALSE);
                return 0;
            }
            if(s->drag.kind!=DesignDrag::Kind::None)
            {
                DesignUpdateDrag(*s,p);
                if(s->drag.kind==DesignDrag::Kind::Select)InvalidateRect(window,nullptr,FALSE);
                return 0;
            }
            const auto wall=s->hoverWall;
            const bool had=!s->hoverPiece.is_null();
            DesignHoverWall(*s,p);
            if(had!=!s->hoverPiece.is_null() || wall!=s->hoverWall)InvalidateRect(window,nullptr,FALSE);
            return 0;
        }
        if(message==WM_MOUSEWHEEL)
        {
            POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            ScreenToClient(window,&p);
            auto world=DesignWorld(*s,p.x,p.y);
            s->zoom=std::clamp(s->zoom*(GET_WHEEL_DELTA_WPARAM(w)>0?1.2:1/1.2),.002,8.0);
            auto after=DesignScreen(*s,world);
            s->panX+=p.x-after.X;s->panY+=p.y-after.Y;
            InvalidateRect(window,nullptr,FALSE);
            return 0;
        }
        if(message==WM_RBUTTONDOWN)
        {
            if(GetCapture()==window)ReleaseCapture();
            // The menu describes what is there now, even if the timer has not
            // caught up with an edit made elsewhere.
            if(s->drag.kind==DesignDrag::Kind::None && (s->epoch!=mapEpoch || s->revision!=Editor::Revision()))DesignRefresh(*s);
            if(!s->mode.empty() || s->drag.kind!=DesignDrag::Kind::None)
            {
                // Right-click ends whatever is being placed or dragged.
                if(s->mode=="Element")ElementCancel(*s);
                s->mode.clear();s->points=Json::array();s->drag={};s->securityFirstSet=false;
                InvalidateRect(window,nullptr,FALSE);
                DesignStatus(*s,"Cancelled.");
                return 0;
            }
            Sync();
            if(s->epoch!=mapEpoch){DesignRefresh(*s);return 0;}
            DesignUpdateGrid(*s);
            if(int index=0;DesignSliderHit(*s,{GET_X_LPARAM(l),GET_Y_LPARAM(l)},index))
            {
                DesignSliderMenu(*s,{GET_X_LPARAM(l),GET_Y_LPARAM(l)});
                return 0;
            }
            DesignContextMenu(*s,{GET_X_LPARAM(l),GET_Y_LPARAM(l)});
            return 0;
        }
        if(message==WM_SETCURSOR && LOWORD(l)==HTCLIENT)
        {
            POINT at{};
            GetCursorPos(&at);
            ScreenToClient(window,&at);
            LPCTSTR cursor=IDC_ARROW;
            if(s->drag.kind==DesignDrag::Kind::Move)cursor=IDC_SIZEALL;
            else if(s->drag.kind==DesignDrag::Kind::Rotate)cursor=IDC_HAND;
            else if(s->drag.kind==DesignDrag::Kind::Resize)cursor=s->drag.axis==DesignVertical(*s)?IDC_SIZENS:IDC_SIZEWE;
            else if(!s->pending.is_null() && s->mode.empty())
            {
                for(auto& handle:DesignHandles(*s))
                    if(DesignPointDistance(DesignScreen(*s,handle.world),at.x,at.y)<=8)
                        cursor=handle.axis==3?IDC_HAND:handle.axis==DesignVertical(*s)?IDC_SIZENS:IDC_SIZEWE;
                if(cursor==IDC_ARROW && DesignInsidePreview(*s,DesignWorld(*s,at.x,at.y)))cursor=IDC_SIZEALL;
            }
            SetCursor(LoadCursor(nullptr,cursor));
            return TRUE;
        }
        if(message==WM_KEYDOWN)
        {
            const bool fine=(GetKeyState(VK_CONTROL)&0x8000)!=0;
            int axis=-1,direction=0;
            if(w==VK_LEFT || w==VK_RIGHT){axis=DesignHorizontal(*s);direction=w==VK_RIGHT?1:-1;}
            else if(w==VK_UP || w==VK_DOWN){axis=DesignVertical(*s);direction=w==VK_UP?1:-1;}
            if(axis>=0 && !s->pending.is_null())
            {
                DesignUpdateGrid(*s);
                const double step=fine?1:(s->grid[axis]>0?s->grid[axis]:16);
                s->frame.position[axis]+=step*direction;
                Design::CheckVector(s->frame.position);
                DesignInspectorRefresh(*s);
                InvalidateRect(window,nullptr,FALSE);
                const auto nudged="Nudged to "+Design::Round(s->frame.position[0])+", "+Design::Round(s->frame.position[1])+", "+Design::Round(s->frame.position[2])+". Ctrl nudges by one unit.";
                if(!s->previous.is_null())
                {
                    Vector delta{};
                    delta[axis]=step*direction;
                    DesignPlanFollow(*s,delta);
                    DesignApplyEdit(*s,nudged);
                }
                else DesignWarn(*s,nudged);
                return 0;
            }
            if(w==VK_RETURN && !s->pending.is_null() && s->previous.is_null())
            {
                DesignApply(*s);
                DesignStatus(*s,"Blockout placed and still selected for editing. Build geometry to update collision and rendered BSP.");
                return 0;
            }
            if((w=='Z' || w=='Y') && (GetKeyState(VK_CONTROL)&0x8000))
            {
                DesignCommand(*s,w=='Z'?DUndo:DRedo);
                return 0;
            }
            if(w=='B')
            {
                DesignCommand(*s,DBuild);
                return 0;
            }
            if(w=='L' && (GetKeyState(VK_CONTROL)&0x8000))
            {
                DesignLockSelection(*s,!(GetKeyState(VK_SHIFT)&0x8000));
                return 0;
            }
            if(w=='R' && !(GetKeyState(VK_CONTROL)&0x8000))
            {
                DesignTurn(*s,(GetKeyState(VK_SHIFT)&0x8000)?-90:90);
                return 0;
            }
            if(w==VK_DELETE)
            {
                DesignDeleteSelection(*s);
                return 0;
            }
            if(w==VK_PRIOR || w==VK_NEXT || w==VK_HOME)
            {
                // Page Up climbs a storey (a lower detent index), Page Down descends.
                const int count=static_cast<int>(DesignLevels(*s).size());
                if(count==0){DesignStatus(*s,"No storeys yet: place a room first, then the floor slider appears.");return 0;}
                const int current=DesignLevelIndex(*s);
                int next=w==VK_HOME?0:w==VK_PRIOR?(current==0?count:current-1):(current==0?count:current+1);
                if(next>count)next=count;
                if(next<1 && w!=VK_HOME)next=1;
                DesignSetLevel(*s,next);
                return 0;
            }
            if(w==VK_ESCAPE)
            {
                if(s->mode=="Element")ElementCancel(*s);
                s->mode.clear();s->points=Json::array();s->securityFirstSet=false;
                DesignDeactivate(*s);
                DesignStatus(*s,"Cancelled. The map is unchanged.");
                return 0;
            }
            return 0;
        }
        if(message==WM_LBUTTONUP)
        {
            if(GetCapture()==window)ReleaseCapture();
            if(s->drag.kind!=DesignDrag::Kind::None)DesignEndDrag(*s,(w&(MK_SHIFT|MK_CONTROL))!=0);
            InvalidateRect(window,nullptr,FALSE);
            return 0;
        }
        if(message==WM_LBUTTONDBLCLK)
        {
            POINT at{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            size_t index=0,point=0;
            if(DesignGuideAt(*s,at.x,at.y,index))
            {
                DesignEditGuide(*s,index);
                return 0;
            }
            if(DesignRoutePointAt(*s,at.x,at.y,index,point))
            {
                DesignEditAnnotation(*s,index);
                return 0;
            }
            return 0;
        }
        if(message==WM_LBUTTONDOWN)
        {
            Sync();
            // A click after the map or its revision changed elsewhere first
            // catches the panel up, then still counts as a click.
            if(s->epoch!=mapEpoch || s->revision!=Editor::Revision())DesignRefresh(*s);
            DesignUpdateGrid(*s);
            POINT at{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            auto p=DesignSnap(*s,DesignWorld(*s,at.x,at.y));
            if(s->mode=="Security")
            {
                SecurityClick(*s,p);
                return 0;
            }
            if(s->mode=="Element")
            {
                ElementClick(*s,p);
                return 0;
            }
            if(s->mode=="Playtest")
            {
                const auto choice=s->playtestChoice;
                const Pose spawn{p,{0,s->playtestFacing,0}};
                s->mode.clear();s->playtestChoice=-1;
                DesignPlaytest(*s,choice,spawn);
                return 0;
            }
            if(!s->mode.empty())
            {
                if(s->points.size()>=2000)throw std::runtime_error("Finish the route before adding more points.");
                if(s->mode=="Route" && s->annotationKind!="Route")s->points=Json::array();
                s->points.push_back(p);
                if(s->mode=="Route" && s->annotationKind=="Route" && s->points.size()>1)
                {
                    // Report the running distance and both teams' times as the
                    // route is drawn.
                    Json sofar={{"name",s->annotationName},{"kind","Route"},{"team",s->annotationTeam},
                                {"crouched",s->annotationCrouched},{"points",s->points}};
                    DesignStatus(*s,s->annotationName+" so far: "+Design::RouteTimes(sofar,DesignData(*s))
                        +" Click to continue, Finish route / marker to store it, right-click to cancel.");
                }
                if((s->mode=="Measure" || s->mode=="Calibrate") && s->points.size()==2)
                {
                    Json data=DesignData(*s);
                    const double distance=Design::Distance(s->points[0].get<Vector>(),p);
                    if(s->mode=="Calibrate")
                    {
                        std::vector<InputField> f={{"Known distance (world units)","256",{}}};
                        if(Ask(s->window,"Calibrate Reference Scale",f))
                        {
                            for(auto& reference:data["references"])
                                if(reference.at("file")==s->calibrationFile)
                                {
                                    const double old=reference.at("scale");
                                    reference["scale"]=Design::Calibration(distance/old,Design::Number(f[0].value,.001,1000000));
                                }
                            DesignSave(*s,data);
                        }
                    }
                    else
                    {
                        data["annotations"].push_back({{"name","Measurement"},{"kind","Measure"},{"team","Any"},{"points",s->points}});
                        DesignSave(*s,data);
                        DesignStatus(*s,"Measured "+std::to_string(distance)+" Unreal units.");
                    }
                    s->mode.clear();s->points=Json::array();
                }
                InvalidateRect(window,nullptr,FALSE);
                return 0;
            }
            // The floor slider along the right edge.
            if(int index=0;DesignSliderHit(*s,at,index))
            {
                DesignSetLevel(*s,index);
                s->drag={};
                s->drag.kind=DesignDrag::Kind::Floor;
                s->drag.from=s->drag.to=at;
                SetFocus(window);
                SetCapture(window);
                return 0;
            }
            // The place and discard orbs beside a new preview.
            if(s->orbsShown && !s->pending.is_null() && s->previous.is_null())
            {
                if(DesignPointDistance({static_cast<float>(s->placeOrb.x),static_cast<float>(s->placeOrb.y)},at.x,at.y)<=13)
                {
                    DesignApply(*s);
                    DesignStatus(*s,"Blockout placed and still selected for editing. Build geometry to update collision and rendered BSP.");
                    return 0;
                }
                if(DesignPointDistance({static_cast<float>(s->discardOrb.x),static_cast<float>(s->discardOrb.y)},at.x,at.y)<=11)
                {
                    DesignDeactivate(*s);
                    DesignStatus(*s,"Preview discarded. The map is unchanged.");
                    return 0;
                }
            }
            // The quick-add badge takes the click before anything else.
            if(!s->hoverPiece.is_null() && DesignPointDistance({static_cast<float>(s->hoverAt.x),static_cast<float>(s->hoverAt.y)},at.x,at.y)<=10)
            {
                DesignQuickAddMenu(*s,at);
                return 0;
            }
            DesignBeginDrag(*s,at,DesignWorld(*s,at.x,at.y));
            SetFocus(window); // Arrow keys nudge the piece being edited.
            SetCapture(window);
            InvalidateRect(window,nullptr,FALSE);
            return 0;
        }
    }
    catch(const std::exception& e){s->drag={};DesignStatus(*s,e.what());}
    return DefWindowProcA(window,message,w,l);
}
// Enter in an inspector field applies it without waiting for the focus to move.
LRESULT CALLBACK DesignFieldProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR reference)
{
    auto s=reinterpret_cast<DesignState*>(reference);
    if(s && message==WM_KEYDOWN && w==VK_RETURN)
    {
        try{if(id==DDepth)DesignDepthRead(*s);else if(id>=DSheetField)SheetApply(*s);else DesignInspectorRead(*s);}
        catch(const std::exception& e){DesignStatus(*s,e.what());if(id<DSheetField)DesignInspectorRefresh(*s);}
        return 0;
    }
    if(message==WM_CHAR && w==VK_RETURN)return 0; // No message beep.
    return DefSubclassProc(window,message,w,l);
}
LRESULT CALLBACK DesignProc(HWND window,UINT message,WPARAM w,LPARAM l)
{
    auto s=reinterpret_cast<DesignState*>(GetWindowLongPtr(window,GWLP_USERDATA));
    if(message==WM_NCCREATE)
    {
        s=static_cast<DesignState*>(reinterpret_cast<CREATESTRUCT*>(l)->lpCreateParams);
        s->window=window;
        SetWindowLongPtr(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));
    }
    if(!s)return DefWindowProcA(window,message,w,l);
    try
    {
        if(message==WM_CREATE)
        {
            Control(window,"COMBOBOX","",CBS_DROPDOWNLIST,DPlane,12,12,190,150);
            for(const char* plane:{"Top (XY)","Front (XZ)","Side (YZ)"})
                SendMessageA(GetDlgItem(window,DPlane),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(plane));
            SendMessage(GetDlgItem(window,DPlane),CB_SETCURSEL,0,0);
            MoveWindow(GetDlgItem(window,DPlane),12,12,110,150,TRUE);
            Control(window,"STATIC","Z",0,DDepthLabel,128,17,14,20);
            SetWindowSubclass(Control(window,"EDIT","0",ES_AUTOHSCROLL,DDepth,142,13,40,24),DesignFieldProc,DDepth,reinterpret_cast<DWORD_PTR>(s));
            Control(window,"BUTTON","Snap to grid",BS_AUTOCHECKBOX,DSnap,188,12,96,25);
            SendDlgItemMessage(window,DSnap,BM_SETCHECK,BST_CHECKED,0);
            Control(window,"BUTTON","Overlays",BS_AUTOCHECKBOX,DOverlays,290,12,70,25);
            SendDlgItemMessage(window,DOverlays,BM_SETCHECK,BST_CHECKED,0);
            s->builds=Editor::GeometryBuilds();
            s->builtRevision=Editor::Revision();
            // The menu bar holds every action; the side panel keeps the everyday
            // ones, and the right-click menu offers what applies at a point.
            HMENU bar=CreateMenu();
            auto submenu=[&](const char* title,std::initializer_list<std::pair<int,const char*>> items)
            {
                HMENU popup=CreatePopupMenu();
                for(const auto& [id,text]:items)
                    if(id)AppendMenuA(popup,MF_STRING,id,text);
                    else AppendMenuA(popup,MF_SEPARATOR,0,nullptr);
                AppendMenuA(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(popup),title);
            };
            submenu("&Workspace",{{DReference,"Reference image..."},{DCalibrate,"Calibrate image"},{DRemoveReference,"Remove reference..."},{0,nullptr},
                {DScene,"Scene panel: groups, visibility, locks..."},{DMovement,"Movement limits..."},{0,nullptr},{DWorkspace,"Export / import workspace..."}});
            submenu("&Blockout",{{DBlock,"New blockout..."},{DEdit,"Edit selected piece"},{DPlace,"Place / Apply preview"},{DDiscard,"Discard preview"},{0,nullptr},
                {DDoorway,"Doorway in room..."},{DDetach,"Detach selected piece"},{0,nullptr},{DAlign,"Align / distribute..."},{DRepeat,"Repeat selection..."},{0,nullptr},
                {DBuild,"Build geometry\tB"}});
            submenu("&Annotate",{{DGuides,"Player reference..."},{DMeasure,"Measure two points"},{0,nullptr},
                {DRoute,"Route / objective..."},{DFinish,"Finish route / marker"},{DCompare,"Compare routes..."},{0,nullptr},{DClear,"Remove annotation..."}});
            submenu("&Tools",{{DPlay,"Playtest from here..."},{DSecurity,"Security..."},{DCheck,"Check design..."},{0,nullptr},
                {DRefresh,"Refresh from editor"},{DFit,"Fit map / preview"},{DUnlit,"Show unlit areas"},{0,nullptr},{DUndo,"Undo\tCtrl+Z"},{DRedo,"Redo\tCtrl+Y"},{0,nullptr},{DKeys,"Keyboard and mouse..."}});
            SetMenu(window,bar);
            const std::pair<int,const char*> buttons[]={
                {DBlock,"New blockout..."},{DPlace,"Place / Apply preview"},
                {DBuild,"Build geometry (B)"},{DCheck,"Check design..."},
                {DUndo,"Undo"},{DRedo,"Redo"}};
            const int rows=(static_cast<int>(std::size(buttons))+1)/2;
            for(int i=0;i<static_cast<int>(std::size(buttons));++i)
                Control(window,"BUTTON",buttons[i].second,0,buttons[i].first,12+(i%2)*196,46+(i/2)*30,190,27);
            // Inspector: live fields for the preview or the selected piece.
            const int top=46+rows*30+10;
            s->inspectorTop=top;
            Control(window,"STATIC","",0,DInspectorTitle,12,top,386,20);
            const std::tuple<int,const char*,const char*> fields[]={
                {DName,"Name","EDIT"},{DShape,"Shape","COMBOBOX"},{DConstruction,"Construction","COMBOBOX"},
                {DWidth,"Width (X)","EDIT"},{DLength,"Length (Y)","EDIT"},{DHeight,"Height (Z)","EDIT"},
                {DThickness,"Thickness","EDIT"},{DSteps,"Stair count","EDIT"},
                {DPositionX,"Base X","EDIT"},{DPositionY,"Base Y","EDIT"},{DPositionZ,"Base Z","EDIT"},{DYaw,"Yaw","EDIT"}};
            for(int i=0;i<static_cast<int>(std::size(fields));++i)
            {
                const int column=i%2,row=i/2,x=12+column*196,y=top+24+row*26;
                Control(window,"STATIC",std::get<1>(fields[i]),0,DInspectorLabel+i,x,y+4,84,20);
                const bool edit=std::string(std::get<2>(fields[i]))=="EDIT";
                auto control=Control(window,std::get<2>(fields[i]),"",edit?ES_AUTOHSCROLL:CBS_DROPDOWNLIST|WS_VSCROLL,
                                     std::get<0>(fields[i]),x+86,y,100,edit?22:160);
                if(edit)SetWindowSubclass(control,DesignFieldProc,std::get<0>(fields[i]),reinterpret_cast<DWORD_PTR>(s));
            }
            for(const char* shape:kDesignShapes)SendDlgItemMessageA(window,DShape,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(shape));
            for(const char* construction:kDesignConstructions)SendDlgItemMessageA(window,DConstruction,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(construction));
            const int checks=top+24+((static_cast<int>(std::size(fields))+1)/2)*26;
            Control(window,"BUTTON","Ceiling (shell)",BS_AUTOCHECKBOX,DCeiling,12,checks,140,24);
            Control(window,"BUTTON","Zone portal (doorway)",BS_AUTOCHECKBOX,DPortal,160,checks,170,24);
            Control(window,"BUTTON","Discard preview",0,DDiscard,12,checks+28,190,26);
            Control(window,"BUTTON","Keys...",0,DKeys,208,checks+28,90,26);
            const std::pair<int,const char*> tips[]={
                {DPlane,"Which way the plan looks: top (a floor plan) or front / side (an elevation). Page Up / Page Down step between storeys."},
                {DDepth,"Where clicks land on the axis the view cannot show: the floor height in the top view, the depth in an elevation. Enter applies; the storey slider sets it too."},
                {DSnap,"Snap clicks and drags to the editor's grid. Ctrl + wheel over a viewport changes the grid."},
                {DOverlays,"Draw lights, devices, game actors, guides and annotations over the plan."},
                {DBlock,"Start a new room, corridor, vent, doorway or stairs as a preview. Right-click the plan to start one where you point."},
                {DPlace,"Create the brushes of a new preview (Enter), or apply the edits to a placed piece."},
                {DBuild,"Rebuild BSP geometry so collision and lighting match the brushes (B)."},
                {DCheck,"List what stops the map from playing: clearance, missing starts, mission and objective flow, wiring, lighting."},
                {DUndo,"Undo the last map or workspace change (Ctrl+Z)."},{DRedo,"Redo (Ctrl+Y)."},
                {DName,"The piece's name in the Scene panel and the library."},{DShape,"Room, corridor, vent, crawlway, doorway, or a stair shape."},
                {DConstruction,"Carve cuts the piece out of solid space; Shell builds walls around it."},
                {DWidth,"Local X size in units. A player is 96 wide."},{DLength,"Local Y size in units."},{DHeight,"Local Z size in units. A player stands 180 tall, crouches to 125, crawls under 105."},
                {DThickness,"Wall and floor thickness for shells and stair treads."},{DSteps,"Number of steps for stairs."},
                {DPositionX,"World X of the piece's base corner."},{DPositionY,"World Y of the piece's base corner."},{DPositionZ,"World Z of the floor. The slider on the plan sets the storey."},
                {DYaw,"Turn in degrees about Z. R turns 90 degrees, Shift+R the other way; the round handle beyond the piece's +Y edge turns it by dragging."},{DCeiling,"Shell rooms get a ceiling slab."},{DPortal,"Doorways become zone portals, which split the map for visibility and sound."},
                {DDiscard,"Drop the preview or stop editing the piece (Esc)."},{DKeys,"Every keyboard and mouse shortcut of the plan and the Scene panel."}};
            for(const auto& [id,text]:tips)DesignTip(*s,GetDlgItem(window,id),text);
            s->canvas=CreateWindowExA(WS_EX_CLIENTEDGE,"ReloadedMapDesignCanvas","",WS_CHILD|WS_VISIBLE,412,12,700,620,window,
                                      reinterpret_cast<HMENU>(DCanvas),GetModuleHandle(nullptr),nullptr);
            SetWindowLongPtr(s->canvas,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));
            s->status=Control(window,"STATIC","",0,DStatus,12,600,900,64);
            DesignRefresh(*s);
            DesignFit(*s);
            DesignInspectorRefresh(*s);
            DesignDepthShow(*s);
            DesignStatus(*s,"Wheel: zoom. Middle drag: pan. Click a piece to edit it, drag to move, drag a square to resize; arrow keys nudge by the grid. Drag empty space to box-select (right to left selects what it touches); Shift or Ctrl adds. The slider on the right shows one storey; Page Up / Page Down step between them. Right-click for actions at that point.");
            SetTimer(window,1,700,nullptr);
            return 0;
        }
        if(message==WM_SIZE)
        {
            const int width=LOWORD(l),height=HIWORD(l);
            MoveWindow(s->canvas,412,12,std::max(1,width-424),std::max(1,height-92),TRUE);
            MoveWindow(s->status,12,height-72,width-24,66,TRUE);
            return 0;
        }
        if(message==WM_GETMINMAXINFO){reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={1150,640};return 0;}
        if(message==WM_COMMAND)
        {
            const int id=LOWORD(w),notification=HIWORD(w);
            if(id==DPlane && notification!=CBN_SELCHANGE)return 0;
            if(id==DDepth)
            {
                if(notification!=EN_KILLFOCUS)return 0;
                try{DesignDepthRead(*s);}
                catch(const std::exception& e){DesignStatus(*s,e.what());DesignDepthShow(*s);}
                return 0;
            }
            // Inspector fields apply themselves; a rejected value is reported
            // in the status line and the field is put back as it was.
            if(id>=DName && id<=DYaw)
            {
                if(notification!=EN_KILLFOCUS && notification!=CBN_SELCHANGE && notification!=BN_CLICKED)return 0;
                try{DesignInspectorRead(*s);}
                catch(const std::exception& e){DesignStatus(*s,e.what());DesignInspectorRefresh(*s);}
                return 0;
            }
            if(id>=DSheetField && id<DSheetLabel)
            {
                if(notification!=EN_KILLFOCUS && notification!=CBN_SELCHANGE && notification!=BN_CLICKED)return 0;
                try{SheetApply(*s);}
                catch(const std::exception& e){DesignStatus(*s,e.what());SheetRefreshLater(*s);}
                return 0;
            }
            if(id>=DSheetButton && id<DSheetButton+10)
            {
                try{SheetButton(*s,id-DSheetButton);}
                catch(const std::exception& e){DesignStatus(*s,e.what());}
                return 0;
            }
            DesignCommand(*s,id);
            return 0;
        }
        if(message==WM_APP+7)
        {
            try{SheetRefresh(*s);}
            catch(const std::exception& e){DesignStatus(*s,e.what());}
            return 0;
        }
        if(message==WM_TIMER)
        {
            Sync();
            if(s->epoch!=mapEpoch || s->revision!=Editor::Revision())DesignRefresh(*s);
            // A finished geometry build makes the current brushes the built ones.
            const auto builds=Editor::GeometryBuilds();
            if(builds!=s->builds)
            {
                s->builds=builds;
                s->builtRevision=Editor::Revision();
                InvalidateRect(s->canvas,nullptr,FALSE);
            }
            return 0;
        }
        if(message==WM_CLOSE)
        {
            try
            {
                WINDOWPLACEMENT placement{sizeof(placement)};
                if(GetWindowPlacement(window,&placement) && placement.showCmd!=SW_SHOWMINIMIZED)
                {
                    const auto& r=placement.rcNormalPosition;
                    Json next=document;
                    next["designWindow"]=Json::array({r.left,r.top,r.right-r.left,r.bottom-r.top});
                    Save(next);
                }
            }
            catch(const std::exception&) { /* Placement memory is best effort. */ }
            if(s->checkWindow)DestroyWindow(s->checkWindow);
            if(s->securityWindow)DestroyWindow(s->securityWindow);
            DestroyWindow(window);
            return 0;
        }
        if(message==WM_NCDESTROY)
        {
            KillTimer(window,1);
            designWindow=nullptr;
            SetWindowLongPtr(window,GWLP_USERDATA,0);
            delete s;
            return DefWindowProcA(window,message,w,l);
        }
    }
    catch(const std::exception& e)
    {
        // Reported in the status line, never in a modal box: a box stops the
        // panel (and the native test suite) until someone dismisses it.
        DesignStatus(*s,e.what());
        if(message==WM_COMMAND)MessageBeep(MB_ICONINFORMATION);
    }
    return DefWindowProcA(window,message,w,l);
}
// The design view's current mapping between the map and the canvas. Tests use
// it to point at a wall; nothing in the editor depends on it.
Json DesignView()
{
    auto s=designWindow?reinterpret_cast<DesignState*>(GetWindowLongPtr(designWindow,GWLP_USERDATA)):nullptr;
    if(!s)return {{"open",false}};
    return {{"open",true},{"plane",s->plane},{"zoom",s->zoom},{"panX",s->panX},{"panY",s->panY},
            {"depth",s->depth},{"snap",s->snap},{"status",Text(s->status)},
            {"stale",DesignGeometryStale(*s)},{"issues",s->issues.size()},
            {"badge",s->hoverPiece.is_null()?Json{}:Json{{"x",s->hoverAt.x},{"y",s->hoverAt.y},{"wall",s->hoverWall}}},
            {"followed",s->followed},{"epoch",s->epoch},{"mapEpoch",mapEpoch},{"revision",s->revision},{"editorRevision",Editor::Revision()},
            {"mode",s->mode},{"drag",static_cast<int>(s->drag.kind)},{"floorFilter",s->floorFilter},{"pending",!s->pending.is_null()}};
}
void OpenDesign(HWND owner)
{
    if(designWindow){ShowWindow(designWindow,SW_RESTORE);SetForegroundWindow(designWindow);return;}
    static ULONG_PTR token=0;
    if(!token)
    {
        Gdiplus::GdiplusStartupInput input;
        if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Cannot start the design renderer.");
    }
    WNDCLASSA wc{};
    wc.hInstance=GetModuleHandle(nullptr);
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
    wc.style=CS_DBLCLKS; // Double-click edits a reference or route point.
    wc.lpfnWndProc=DesignCanvasProc;
    wc.lpszClassName="ReloadedMapDesignCanvas";
    RegisterClassA(&wc);
    wc.lpfnWndProc=DesignProc;
    wc.lpszClassName="ReloadedMapDesign";
    RegisterClassA(&wc);
    auto state=new DesignState;
    // The window reopens where it was last closed, if that is still on screen.
    int x=CW_USEDEFAULT,y=CW_USEDEFAULT,width=1400,height=820;
    try
    {
        Sync();
        const auto& placement=document.value("designWindow",Json{});
        if(placement.is_array() && placement.size()==4)
        {
            RECT rect{placement[0].get<int>(),placement[1].get<int>(),placement[0].get<int>()+placement[2].get<int>(),placement[1].get<int>()+placement[3].get<int>()};
            if(placement[2].get<int>()>=1150 && placement[3].get<int>()>=800 && MonitorFromRect(&rect,MONITOR_DEFAULTTONULL))
            {
                x=rect.left;y=rect.top;width=rect.right-rect.left;height=rect.bottom-rect.top;
            }
        }
    }
    catch(const std::exception&) { /* Default placement. */ }
    designWindow=CreateWindowExA(WS_EX_CONTROLPARENT,wc.lpszClassName,"Map Design",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                                 x,y,width,height,owner,nullptr,wc.hInstance,state);
    if(!designWindow)throw std::runtime_error("Cannot open Map Design.");
}
