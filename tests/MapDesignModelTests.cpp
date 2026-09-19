#include "../Reloaded.Editor/MapDesignModel.h"
#include <iostream>
#include <limits>
#include <source_location>
using namespace Workflow;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
// Reports which call accepted something it should have refused.
template<class F> void Reject(F f,std::source_location where=std::source_location::current())
{
    bool caught=false;
    try{f();}catch(const std::exception&){caught=true;}
    if(!caught)throw std::runtime_error("invalid input accepted at line "+std::to_string(where.line()));
}
double Volume(const Design::Solid& solid)
{
    double result=0;
    for(auto& f:solid.faces)for(size_t i=1;i+1<f.size();++i){auto a=f[0],b=f[i],c=f[i+1];result+=(a[0]*(b[1]*c[2]-b[2]*c[1])+a[1]*(b[2]*c[0]-b[0]*c[2])+a[2]*(b[0]*c[1]-b[1]*c[0]))/6;}
    return result;
}
int main()
{
  try
  {
    Json spec={{"kind","Platform"},{"width",128},{"length",256},{"height",64},{"thickness",16},{"steps",8},{"ceiling",true}};
    auto platform=Design::Geometry(spec);Check(std::abs(Volume(platform[0])-128*256*64)<.01,"box winding or dimensions wrong");
    spec["kind"]="Ramp";Check(std::abs(Volume(Design::Geometry(spec)[0])-128*256*64/2)<.01,"ramp winding wrong");
    spec["kind"]="Stairs";auto stairs=Design::Geometry(spec);Check(stairs.size()==9,"stair count wrong: eight steps and their glide ramp");for(auto& step:stairs)Check(Volume(step)>0,"inverted step");
    Check(stairs.back().flags==Design::kGlideFlags && Volume(stairs.back())>0,"the glide ramp is invisible and semi-solid");
    // Turning and spiral stairs: two flights and a landing, or wedges round a post.
    Json turning={{"kind","Stairs L"},{"width",128},{"length",384},{"height",256},{"thickness",16},{"steps",8}};
    auto lStairs=Design::Geometry(turning);Check(lStairs.size()==11,"an L stair has two flights, a landing and two glide ramps");for(auto& step:lStairs)Check(Volume(step)>0,"inverted L step");
    turning["kind"]="Stairs U";auto uStairs=Design::Geometry(turning);Check(uStairs.size()==11,"a U stair has two flights, a landing and two glide ramps");for(auto& step:uStairs)Check(Volume(step)>0,"inverted U step");
    Json spiral={{"kind","Spiral"},{"width",256},{"length",256},{"height",256},{"thickness",16},{"steps",12}};
    auto spiralSteps=Design::Geometry(spiral);Check(spiralSteps.size()==35,"a spiral has its steps, a post and two glide prisms per step but the last");for(auto& step:spiralSteps)Check(Volume(step)>0,"inverted spiral wedge or prism");
    Check(spiralSteps.back().flags==Design::kGlideFlags,"the spiral's glide prisms are invisible and semi-solid");
    {
        // Resizing stairs recounts their treads; a spiral stays round.
        Json flight={{"kind","Stairs"},{"width",128},{"length",256},{"height",128},{"thickness",16},{"steps",8}};
        Pose p{};Design::Resize(flight,p,1,1,256);
        Check(flight["length"]==512 && flight["steps"]==16,"a longer flight gains treads");
        Design::Resize(flight,p,2,1,384);
        Check(flight["height"]==512 && flight["steps"]==22,"a taller flight gains treads so no rise exceeds 24 units");
        Json lShaped={{"kind","Stairs L"},{"width",128},{"length",256},{"height",128},{"thickness",16},{"steps",8}};
        Design::Resize(lShaped,p,1,1,128);
        Check(lShaped["steps"]==16,"a turning stair counts both flights from its run");
        Json round=spiral;Design::Resize(round,p,0,1,64);
        Check(round["width"]==320 && round["length"]==320,"a spiral resized on one axis stays round");
        Design::Resize(round,p,2,1,256);
        Check(round["steps"]==22,"a taller spiral gains steps");
    }
    Reject([]{Design::Geometry(Json{{"kind","Stairs L"},{"width",128},{"length",130},{"height",256},{"thickness",16},{"steps",8}});});
    Reject([]{Design::Geometry(Json{{"kind","Spiral"},{"width",32},{"length",32},{"height",256},{"thickness",16},{"steps",12}});});
    Check(stairs.back().faces[0][2][2]==64,"stairs do not reach total rise");
    spec["kind"]="Room";auto room=Design::Geometry(spec);Check(room.size()==7 && room[0].subtract,"room must carve before adding walls");
    spec["ceiling"]=false;Check(Design::Geometry(spec).size()==6,"ceiling opt-out failed");
    spec["kind"]="Corridor";Check(Design::Geometry(spec).size()==4,"corridor must have open ends");
    spec["kind"]="Doorway";Check(Design::Geometry(spec)[0].subtract,"doorway must subtract");

    // Carve construction: one subtractive brush instead of a carved shell.
    auto carve=spec;carve["construction"]="Carve";carve["ceiling"]=true;carve["kind"]="Room";
    auto carved=Design::Geometry(carve);
    Check(carved.size()==1 && carved[0].subtract,"carve construction must use a single subtractive brush");
    Check(std::abs(Volume(carved[0])-128.0*256*64)<.01,"carved room must subtract exactly its interior");
    carve["ceiling"]=false;Check(Design::Geometry(carve).size()==1,"ceiling is a shell-only option");
    carve["kind"]="Corridor";auto corridor=Design::Geometry(carve);
    Check(corridor.size()==1 && std::abs(Volume(corridor[0])-128.0*(256+2*16)*64)<.01,"carved corridor must cut through both end walls");
    Check(!Design::Carved(spec) && Design::Carved(carve),"construction default must stay Shell for saved pieces");

    // Doorways can carry a zone portal sheet.
    auto door=carve;door["kind"]="Doorway";door["portal"]=true;
    auto doorway=Design::Geometry(door);
    Check(doorway.size()==2 && doorway[0].subtract,"portal doorway must subtract and add a sheet");
    Check(!doorway[1].subtract && doorway[1].faces.size()==1 && doorway[1].flags==Design::kPortalPolyFlags,"zone portal must be a single flagged sheet");
    auto portalText=Design::Definition(door)["actors"][1]["text"].get<std::string>();
    Check(portalText.find("PolyFlags=67108873")!=std::string::npos && portalText.find("Begin Polygon Flags=67108873")!=std::string::npos,"portal flags missing from brush text");
    door["portal"]=false;Check(Design::Geometry(door).size()==1,"portal opt-out failed");

    // A piece's rotation is baked into its brush polygons.
    Json slab={{"kind","Platform"},{"width",200},{"length",100},{"height",10},{"thickness",16},{"steps",8}};
    auto extent=[](const std::string& text)
    {
        Vector most{};
        std::istringstream lines(text);std::string line;
        while(std::getline(lines,line))
        {
            if(line.rfind("Vertex ",0)!=0)continue;
            Vector v{};char comma=0;std::istringstream values(line.substr(7));
            values>>v[0]>>comma>>v[1]>>comma>>v[2];
            for(int axis=0;axis<3;++axis)most[axis]=std::max(most[axis],std::abs(v[axis]));
        }
        return most;
    };
    Check(extent(Design::Definition(slab)["actors"][0]["text"])==Vector{100,50,10},"an unrotated piece keeps its local shape");
    auto quarter=extent(Design::Definition(slab,{0,16384,0})["actors"][0]["text"]);
    Check(std::abs(quarter[0]-50)<1e-6 && std::abs(quarter[1]-100)<1e-6 && quarter[2]==10,"a quarter turn swaps the piece's footprint in its polygons");
    Check(Design::Definition(slab,{0,16384,0})["actors"][0]["rotation"]==Rotation{},"the brush actor itself stays unrotated");

    // Grid snapping and preview handles.
    Check(Design::SnapTo(100,64)==128 && Design::SnapTo(-100,64)==-128 && Design::SnapTo(100,0)==100,"grid snapping incorrect");
    Check(Design::SnapVector({10,70,-33},{16,32,0})==Vector{16,64,-33},"per-axis snapping incorrect");
    Json resized={{"kind","Platform"},{"width",128},{"length",256},{"height",64},{"thickness",16},{"steps",8}};
    Pose frame{};
    Design::Resize(resized,frame,0,1,64);
    Check(resized["width"]==192 && std::abs(frame.position[0]-32)<1e-9,"resizing must keep the opposite face fixed");
    Design::Resize(resized,frame,0,-1,-64);
    Check(resized["width"]==256 && std::abs(frame.position[0])<1e-9,"resizing the other face must keep its opposite fixed");
    Pose rotated{{0,0,0},{0,16384,0}};Json turned=resized;
    Design::Resize(turned,rotated,1,1,128);
    Check(turned["length"]==384 && std::abs(rotated.position[0])>63 && std::abs(rotated.position[1])<1e-6,"rotated resize must move along the piece's own axis");
    Json tall=resized;Pose floorAnchored{};
    Design::Resize(tall,floorAnchored,2,1,32);
    Check(tall["height"]==96 && floorAnchored.position[2]==0,"height resize must stay anchored on the floor");
    Reject([&]{Json copy=resized;Pose p{};Design::Resize(copy,p,2,-1,10);});
    Json clamped={{"kind","Platform"},{"width",8},{"length",256},{"height",64},{"thickness",16},{"steps",8}};
    Pose clampFrame{};Design::Resize(clamped,clampFrame,0,-1,100);
    Check(clamped["width"]==1 && std::abs(clampFrame.position[0]-3.5)<1e-9,"clamped resize must keep the fixed face in place");

    // Snapping a dragged piece to geometry already in the map.
    auto bounds=Design::Bounds({Vector{10,20,30},Vector{-5,50,0},Vector{0,0,0}});
    Check(bounds.lo==Vector{-5,0,0} && bounds.hi==Vector{10,50,30},"bounds incorrect");
    Reject([]{Design::Bounds({});});
    std::vector<Design::Extent> neighbours={{{100,0,0},{200,100,100}},{{-400,0,0},{-300,100,100}}};
    Check(Design::SnapToFaces(94,150,neighbours,0,10)==6,"a face just short of a wall must snap onto it");
    Check(Design::SnapToFaces(94,150,neighbours,0,4)==0,"a face outside the tolerance must not move");
    Check(Design::SnapToFaces(205,260,neighbours,0,10)==-5,"a face just past a wall must snap back onto it");
    Check(Design::SnapToFaces(0,50,neighbours,1,10)==0,"an already aligned face must not move");
    auto offset=Design::SnapBoxToNeighbours({{94,-8,0},{150,40,60}},neighbours,0,1,10);
    Check(offset[0]==6 && offset[1]==8 && offset[2]==0,"box snapping must work per visible axis only");
    Check(Design::SnapBoxToNeighbours({{94,-8,0},{150,40,60}},{},0,1,10)==Vector{},"no neighbours means no snap");

    // Projected outlines for filled plan drawing.
    auto hull=Design::ConvexHull({{0,0},{10,0},{10,10},{0,10},{5,5},{5,0}});
    Check(hull.size()==4,"convex hull must drop interior and collinear points");
    Check(Design::ConvexHull({{1,1},{2,2}}).size()==2 && Design::ConvexHull({}).empty(),"degenerate hulls must survive");
    Reject([]{Design::ConvexHull({{std::numeric_limits<double>::quiet_NaN(),0},{1,1},{2,2}});});

    // Traversal checks against the map's movement limits.
    // This test's own limits, so the checks below do not move with the defaults.
    Json design={{"movement",{{"stepHeight",35},{"slope",45},{"width",48},{"height",96},{"crouchWidth",48},{"crouchHeight",56},{"crawlHeight",28},{"spySpeed",300},{"mercSpeed",250}}}};
    Json steepStairs={{"kind","Stairs"},{"width",128},{"length",256},{"height",512},{"thickness",16},{"steps",8}};
    Check(Design::TraversalWarnings(steepStairs,design).size()==1,"steep stairs must warn");
    steepStairs["steps"]=16;Check(Design::TraversalWarnings(steepStairs,design).empty(),"acceptable stairs must not warn");
    Json ramp={{"kind","Ramp"},{"width",128},{"length",128},{"height",256},{"thickness",16},{"steps",8}};
    Check(Design::TraversalWarnings(ramp,design).size()==1,"steep ramp must warn");
    Check(std::abs(Design::RampSlope(128,128)-45)<1e-9,"ramp slope incorrect");
    Json narrow={{"kind","Doorway"},{"width",32},{"length",16},{"height",64},{"thickness",16},{"steps",8}};
    Check(Design::TraversalWarnings(narrow,design).size()==2,"a narrow, low doorway must warn about both");
    Check(Design::Movement(Json::object()).at("stepHeight")==35,"default movement limits missing");
    Reject([]{Design::Movement(Json{{"movement",{{"slope",120}}}});});

    // Vents and crawlways: corridor shapes checked against crouch clearance.
    Json vent={{"kind","Vent"},{"construction","Carve"},{"width",64},{"length",256},{"height",56},{"thickness",16},{"steps",8}};
    auto ventSolids=Design::Geometry(vent);
    Check(ventSolids.size()==1 && std::abs(Volume(ventSolids[0])-64.0*(256+32)*56)<.01,"a vent carves an open-ended corridor");
    Check(Design::Crouching("Vent") && Design::Crouching("Crawlway") && !Design::Crouching("Corridor"),"posture classification incorrect");
    Check(Design::ClearanceHeight("Vent",Design::Movement(design))==56 && Design::ClearanceHeight("Crawlway",Design::Movement(design))==28,"crouch clearances incorrect");
    Check(Design::TraversalWarnings(vent,design).empty(),"a vent at crouch height must not warn");
    Json lowVent=vent;lowVent["height"]=32;
    Check(Design::TraversalWarnings(lowVent,design).size()==1,"a vent below crouch height must warn");
    Json tallVent=vent;tallVent["height"]=120;
    Check(Design::TraversalWarnings(tallVent,design).size()==1,"a vent tall enough to walk through must warn");
    Json crawl=vent;crawl["kind"]="Crawlway";crawl["height"]=28;
    Check(Design::TraversalWarnings(crawl,design).empty(),"a crawlway at half crouch height must not warn");
    Json shellVent=vent;shellVent["construction"]="Shell";shellVent["ceiling"]=true;
    Check(Design::Geometry(shellVent).size()==5,"a shell vent keeps its ends open");
    shellVent["ceiling"]=false;
    Check(Design::Geometry(shellVent).size()==4,"a shell vent without a ceiling has a floor and two sides");

    // Quick-add: a new piece attached to the outside of a wall.
    Json hostRoom={{"kind","Room"},{"construction","Carve"},{"width",512},{"length",768},{"height",256},{"thickness",16},{"steps",8},{"ceiling",true}};
    Pose hostPose{{1000,2000,64},{0,0,0}};
    Json link={{"kind","Corridor"},{"construction","Carve"},{"width",96},{"length",256},{"height",128},{"thickness",16},{"steps",8}};
    auto attached=Design::AttachedPiece(hostRoom,hostPose,link,"+Y",32);
    Check(attached.position==Vector{1032,2528,64} && attached.rotation[1]==0,"an attached corridor meets the outside of the wall");
    auto sideways=Design::AttachedPiece(hostRoom,hostPose,link,"-X",0);
    Check(sideways.position==Vector{600,2000,64} && sideways.rotation[1]==16384,"a side attachment turns to run away from its wall");
    Reject([&]{Design::AttachedPiece(hostRoom,hostPose,link,"up",0);});
    Reject([&]{Json platform=hostRoom;platform["kind"]="Platform";Design::AttachedPiece(platform,hostPose,link,"+Y",0);});

    // A material on every face.
    auto textured=Design::Definition(slab,{},"MyTex.Group.Grey")["actors"][0]["text"].get<std::string>();
    Check(textured.find("Begin Polygon Texture=MyTex.Group.Grey Flags=0")!=std::string::npos,"a material must reach every polygon");
    Check(Design::Definition(slab,{},"None")["actors"][0]["text"].get<std::string>().find("Texture=")==std::string::npos,"None means no texture");
    Reject([&]{Design::Definition(slab,{},"bad path");});

    // Design check: layout problems the plan hides.
    {
        Json roomA={{"spec",{{"kind","Room"},{"name","A"},{"construction","Carve"},{"width",512},{"length",512},{"height",256},{"thickness",16},{"steps",8}}},{"position",Vector{0,0,0}},{"rotation",Rotation{}}};
        Json roomB=roomA;roomB["spec"]["name"]="B";roomB["position"]=Vector{1000,0,0};
        auto lonely=Design::DesignIssues(Json::array({roomA,roomB}),design);
        Check(lonely.size()==2 && lonely[0].severity=="error" && lonely[0].piece==0,"unreached rooms are reported");
        Json doorPiece={{"spec",{{"kind","Doorway"},{"name","D"},{"construction","Carve"},{"width",64},{"length",16},{"height",112},{"thickness",16},{"steps",8}}},{"position",Vector{0,264,0}},{"rotation",Rotation{}}};
        auto reached=Design::DesignIssues(Json::array({roomA,doorPiece}),design);
        Check(reached.empty(),"a doorway in a wall satisfies both the room and the doorway");
        Json orphan=doorPiece;orphan["position"]=Vector{5000,5000,0};
        auto orphaned=Design::DesignIssues(Json::array({roomA,orphan}),design);
        Check(orphaned.size()==2 && orphaned[1].text.find("does not cut into")!=std::string::npos,"an orphan doorway is reported");
        Json roomC=roomA;roomC["spec"]["name"]="C";roomC["position"]=Vector{200,0,0};
        auto merged=Design::DesignIssues(Json::array({roomA,roomC,doorPiece}),design);
        bool warned=false;for(auto& issue:merged)if(issue.text.find("carve into each other")!=std::string::npos)warned=true;
        Check(warned,"overlapping carves are reported");
        Json tallVentPiece={{"spec",tallVent},{"position",Vector{0,0,0}},{"rotation",Rotation{}}};
        tallVentPiece["spec"]["name"]="V";
        auto walkable=Design::DesignIssues(Json::array({tallVentPiece}),design);
        Check(!walkable.empty() && walkable[0].severity=="warning","traversal warnings are included");
    }

    // Doorways placed in a selected room's wall.
    Json roomSpec={{"kind","Room"},{"width",512},{"length",768},{"height",256},{"thickness",16},{"steps",8},{"ceiling",true},{"construction","Carve"}};
    Json doorSpec={{"kind","Doorway"},{"width",96},{"length",16},{"height",128},{"thickness",16},{"steps",8}};
    Pose roomPose{{1000,2000,64},{0,0,0}};
    auto north=Design::WallDoorway(roomSpec,roomPose,doorSpec,"+Y",0,0);
    Check(north.position==Vector{1000,2392,64} && north.rotation[1]==0,"doorway must sit in the +Y wall");
    auto east=Design::WallDoorway(roomSpec,roomPose,doorSpec,"+X",32,16);
    Check(east.position==Vector{1264,2032,80} && east.rotation[1]==16384,"side wall doorway must turn with the wall");
    Pose turnedRoom{{0,0,0},{0,16384,0}};
    auto rotatedDoor=Design::WallDoorway(roomSpec,turnedRoom,doorSpec,"+Y",0,0);
    Check(std::abs(std::abs(rotatedDoor.position[0])-392)<1e-6 && std::abs(rotatedDoor.position[1])<1e-6,"a rotated room must place its doorway with it");
    Reject([&]{Design::WallDoorway(roomSpec,roomPose,doorSpec,"+Y",240,0);});
    Reject([&]{Design::WallDoorway(roomSpec,roomPose,doorSpec,"+Y",0,200);});
    Reject([&]{Json shallow=doorSpec;shallow["length"]=8;Design::WallDoorway(roomSpec,roomPose,shallow,"+Y",0,0);});
    Reject([&]{Design::WallDoorway(roomSpec,roomPose,doorSpec,"up",0,0);});
    Reject([&]{Json notRoom=roomSpec;notRoom["kind"]="Platform";Design::WallDoorway(notRoom,roomPose,doorSpec,"+Y",0,0);});

    // Route lengths and timings.
    Json route={{"name","Route A"},{"kind","Route"},{"team","Spy"},{"points",Json::array({Vector{0,0,0},Vector{300,0,0},Vector{300,400,0}})}};
    Check(Design::PathLength(route.at("points"))==700,"route length incorrect");
    Check(std::abs(Design::TravelSeconds(700,350)-2)<1e-9,"travel time incorrect");
    Reject([]{Design::TravelSeconds(100,0);});
    Check(Design::RouteSummary(route,design).find("spy 2.3 s")!=std::string::npos,"route summary must report its team timing");
    Json mercRoute={{"name","Route B"},{"kind","Route"},{"team","Merc"},{"points",Json::array({Vector{0,0,0},Vector{500,0,0}})}};
    auto comparison=Design::RouteComparison(route,mercRoute,design);
    Check(comparison.find("Route B arrives 0.3 s earlier")!=std::string::npos,"route comparison must name the earlier team");
    // A route with no team of its own is timed as the team the other is not.
    Json anyRoute=route;anyRoute["team"]="Any";
    Check(Design::RouteComparison(anyRoute,mercRoute,design).find("(spy)")!=std::string::npos,"an unteamed route is timed for the other team");

    // Every route reports both teams, so one drawing answers both questions.
    auto times=Design::RouteTimes(route,design);
    Check(times.find("700 units")!=std::string::npos && times.find("spy 2.3 s")!=std::string::npos && times.find("merc 2.8 s")!=std::string::npos,"a route must report both teams' times");
    Json crouchedRoute=route;crouchedRoute["crouched"]=true;
    Check(std::abs(Design::RouteJourney(crouchedRoute,design).spy-2*Design::RouteJourney(route,design).spy)<1e-9,"a crouched route takes twice as long at half speed");
    Check(Design::RouteTimes(crouchedRoute,design).find("crouched")!=std::string::npos,"a crouched route says so");
    // A ladder or a drop is climbed, not run.
    Json ladder={{"name","Ladder"},{"kind","Route"},{"team","Spy"},{"points",Json::array({Vector{0,0,0},Vector{0,0,240}})}};
    auto climbed=Design::RouteJourney(ladder,design);
    Check(climbed.climb==240 && climbed.ground==0 && std::abs(climbed.spy-2)<1e-9,"a climb is timed at the climb speed");
    Json stepUp={{"name","Step"},{"kind","Route"},{"team","Spy"},{"points",Json::array({Vector{0,0,0},Vector{300,0,20}})}};
    Check(Design::RouteJourney(stepUp,design).climb==0,"a step-up is taken in stride");

    // Floor filters.
    Check(Design::WithinFloor(0,256,-100,10) && !Design::WithinFloor(0,256,300,400) && Design::WithinFloor(0,256,256,900),"floor range filter incorrect");

    // Layers written into the map's native Group field.
    Check(Design::ValidGroupName("Upper_Floor") && !Design::ValidGroupName("Upper Floor") && !Design::ValidGroupName("") && !Design::ValidGroupName("None"),"group name validation incorrect");
    Check(Design::AddGroup("None","Roof")=="Roof","first group must replace None");
    Check(Design::AddGroup("Roof","Vents")=="Roof,Vents" && Design::AddGroup("Roof,Vents","roof")=="Roof,Vents","group membership must be unique and case-insensitive");
    Check(Design::RemoveGroup("Roof,Vents","Roof")=="Vents" && Design::RemoveGroup("Vents","Vents")=="None","removing the last group must restore None");
    Check(Design::GroupNames(" Roof , Vents ,None").size()==2,"group parsing must trim names and ignore None");

    // Portable workspaces.
    Json legacy={{"pieces",Json::array()},{"layers",Json::array()},{"annotations",Json::array()},
                 {"reference",{{"path","C:\\Game\\System\\ReloadedEditor\\References\\abc.png"},{"scale",2},{"opacity",.5},{"origin",Vector{}},{"plane",0}}}};
    auto references=Design::References(legacy);
    Check(references.size()==1 && references[0].at("file")=="abc.png" && !references[0].contains("path"),"legacy reference must migrate to a file name");
    auto exported=Design::ExportWorkspace(legacy,"HELI02");
    Check(exported.at("map")=="HELI02" && exported.at("design").at("references").size()==1 && !exported.at("design").contains("reference"),"workspace export incorrect");
    auto importedDesign=Design::ImportWorkspace(exported,"c:/packages/mapsed/heli02.sdc");
    Check(importedDesign.at("references")[0].at("file")=="abc.png" && importedDesign.at("pieces").is_array(),"workspace import incorrect");
    Json withPiece=exported;withPiece["design"]["pieces"].push_back({{"map","other"},{"spec",spec},{"members",Json::array()}});
    Check(Design::ImportWorkspace(withPiece,"newmap").at("pieces")[0].at("map")=="newmap","imported pieces must belong to this map");
    Reject([]{Design::ImportWorkspace(Json{{"version",2}},"map");});
    Reject([]{Design::References(Json{{"references",Json::array({{{"file","../evil.png"},{"plane",0}}})}});});
    for(auto bad:{0.0,-1.0,std::numeric_limits<double>::infinity()}){auto copy=spec;copy["height"]=bad;Reject([&]{Design::Geometry(copy);});}
    Reject([]{Design::Number("12 units");});Reject([]{Design::Number("nan");});Reject([]{Design::Calibration(0,128);});
    Check(Design::Calibration(256,1024)==4,"reference scale incorrect");Check(Design::Distance({0,0,0},{3,4,12})==13,"3D ruler incorrect");
    auto distributed=Design::Align({{20,0,0},{-10,9,0},{4,8,0}},0,"Distribute",64);
    Check(distributed[1][0]==-10 && distributed[2][0]==54 && distributed[0][0]==118 && distributed[1][1]==9,"distribution order or other axes changed");
    spec["kind"]="Platform";auto def=Design::Definition(spec);auto repeated=Design::Repeat(def,3,{128,0,0},16384);
    Check(repeated["actors"].size()==3 && repeated["actors"][2]["position"][0]==256,"repeated placement spacing incorrect");
    auto placed=PreparePlacement(repeated,{{100,200,300},{0,0,0}},"Test_","Level",{});
    Check(placed["actors"][2]["position"][0]==356,"second placement lost local spacing");
    auto text=placed["t3d"].get<std::string>();Check(text.find("Level.Test_Copy0_Block0Model")!=std::string::npos,"brush model references not remapped");
    Json linked={{"id","linked"},{"dependencies",Json::array()},{"bindings",Json::array()},{"actors",Json::array({
        {{"name","A"},{"path","MyLevel.A"},{"class","Engine.Trigger"},{"tag","Signal"},{"event","None"},{"position",Vector{}},{"rotation",Rotation{}},{"text","Begin Actor Class=Engine.Trigger Name=A\nTag=Signal\nEnd Actor\n"}},
        {{"name","B"},{"path","MyLevel.B"},{"class","Engine.Trigger"},{"tag","Other"},{"event","Signal"},{"position",Vector{10,0,0}},{"rotation",Rotation{}},{"text","Begin Actor Class=Engine.Trigger Name=B\nTag=Other\nEvent=Signal\nOwner=Actor'MyLevel.A'\nEnd Actor\n"}}
    })}};
    auto linkedCopies=PreparePlacement(Design::Repeat(linked,2,{100,0,0},0),{},"Batch_","Map",{});
    auto linkedText=linkedCopies["t3d"].get<std::string>();
    Check(linkedText.find("Event=Batch_Copy0_Signal")!=std::string::npos && linkedText.find("Event=Batch_Copy1_Signal")!=std::string::npos,"repeated event tags cross-link copies");
    // Rewritten references keep the engine's quoted object path form.
    Check(linkedText.find("Owner=Actor'\"Map.Batch_Copy1_A\"'")!=std::string::npos,"repeated object reference escapes its copy");
    Reject([&]{Design::Repeat(def,129,{0,0,0},0);});
    {
        // Mirroring: an unhanded piece's reflection is the same shape at the mirrored pose, whichever way it was turned.
        auto rounded=[](Vector v){for(auto& x:v)x=std::round(x*1000)/1000;return v;};
        auto vertices=[&](const Json& shape,const Pose& pose){std::vector<Vector> out;for(auto& solid:Design::Geometry(shape))for(auto& f:solid.faces)for(auto& v:f)out.push_back(rounded(TransformPoint(v,pose)));std::sort(out.begin(),out.end());return out;};
        Json flight={{"kind","Stairs"},{"width",128},{"length",512},{"height",256},{"thickness",16},{"steps",16},{"construction","Carve"}};
        const Pose pose{{300,-200,64},{0,12345,0}};
        for(int axis:{0,1})
        {
            const double at=axis==0?100:-50;
            auto expected=vertices(flight,pose);
            for(auto& v:expected)v[axis]=std::round((2*at-v[axis])*1000)/1000;
            std::sort(expected.begin(),expected.end());
            Check(vertices(flight,Design::MirrorPose(pose,axis,at))==expected,"a mirrored flight is the reflection of the original");
        }
        size_t handed=0;
        Json pieces=Json::array({{{"spec",flight},{"position",pose.position},{"rotation",pose.rotation}},{{"spec",spiral},{"position",Vector{0,0,0}},{"rotation",Rotation{}}}});
        auto mirrored=Design::MirrorItems(pieces,0,100,handed);
        Check(mirrored.size()==2 && handed==1 && mirrored[0]["position"][0]==-100,"mirrored items reflect positions and count handed shapes");
        Reject([&]{Design::MirrorPose(pose,2,0);});
        // Clipboard: copies keep their offsets round the centre of their bases and paste round a point.
        auto clip=Design::ClipPieces(pieces);
        Check(clip["anchor"][0]==150 && clip["anchor"][1]==-100 && clip["anchor"][2]==0,"clip anchor is the centre of the bases at the lowest floor");
        auto pasted=Design::PasteItems(clip,{1000,1000,512});
        Check(pasted[0]["position"][0]==1150 && pasted[0]["position"][1]==900 && pasted[0]["position"][2]==576 && pasted[1]["position"][2]==512,"pasted pieces keep their offsets");
        Reject([&]{Design::ClipPieces(Json::array());});
    }
    {
        // Sightlines: two carved rooms joined by a doorway see each other through it, not through the wall beside it.
        Json sightRoom={{"kind","Room"},{"width",512},{"length",512},{"height",256},{"thickness",16},{"construction","Carve"}};
        Json sightDoor={{"kind","Doorway"},{"width",96},{"length",16},{"height",128},{"thickness",16},{"construction","Carve"}};
        Json pieces=Json::array({
            {{"spec",sightRoom},{"position",Vector{0,0,0}},{"rotation",Rotation{}}},
            {{"spec",sightRoom},{"position",Vector{528,0,0}},{"rotation",Rotation{}}},
            {{"spec",sightDoor},{"position",Vector{264,0,0}},{"rotation",Rotation{0,16384,0}}}});
        auto solids=Design::CarvedSolids(pieces);
        Check(solids.size()==3,"every carved piece contributes its open space");
        Check(Design::InsideCarved(solids,{0,0,64}) && Design::InsideCarved(solids,{264,0,64}) && !Design::InsideCarved(solids,{264,200,64}),"points in a room, in a turned doorway and in a wall are told apart");
        auto through=Design::Sightline(solids,{-200,0,64},{700,0,64});
        Check(through.runs.empty() && through.blocked==0 && std::abs(through.length-900)<.01,"a line through the doorway is clear");
        auto wall=Design::Sightline(solids,{-200,200,64},{700,200,64});
        Check(wall.runs.size()==1 && wall.blocked>=8 && wall.blocked<=40,"a line through the wall is blocked for about the wall's thickness");
        Reject([&]{Design::Sightline(solids,{0,0,0},{1,0,0},0);});
    }
    {
        // Presets and names.
        Json stair={{"kind","Stairs"},{"width",128},{"length",256},{"height",128},{"thickness",16},{"steps",8}};
        Design::ApplyPreset(stair,Design::Presets("Stairs")[0]);
        Check(stair["length"]==544 && stair["height"]==272 && stair["steps"]==17,"a stair preset sets its size and recounts treads");
        Json duct={{"kind","Vent"},{"width",96},{"length",512},{"height",125},{"thickness",16}};
        Design::ApplyPreset(duct,Design::Presets("Vent")[2]);
        Check(duct["length"]==1024 && duct["height"]==125,"a vent preset keeps the crouch height");
        Check(Design::Presets("Nothing").empty(),"unknown shapes have no presets");
        Json named=Json::array({{{"spec",{{"name","Room 3"}}}},{{"spec",{{"name","Room"}}}},{{"spec",{{"name","Room 12 (above)"}}}},{{"spec",{{"name","Corridor 4"}}}}});
        Check(Design::NextName(named,"Room")=="Room 13" && Design::NextName(named,"Vent")=="Vent 1","names count up from the highest of their kind");
    }
    std::cout<<"Map Design model tests passed\n";
  }
  catch(const std::exception& e)
  {
    std::cerr<<"Map Design model tests failed: "<<e.what()<<"\n";
    return 1;
  }
}
