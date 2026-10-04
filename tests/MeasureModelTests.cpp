// Standalone: cl /std:c++20 /W4 /WX /EHsc tests\MeasureModelTests.cpp Reloaded.Editor\WorkflowModel.cpp
#include "../Reloaded.Editor/MeasureModel.h"
#include <iostream>
#include <source_location>
using namespace Workflow;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool Near(double a,double b,double tolerance=1e-6){return std::abs(a-b)<=tolerance;}
template<class F> void Reject(F f,std::source_location where=std::source_location::current())
{
    bool caught=false;
    try{f();}catch(const std::exception&){caught=true;}
    if(!caught)throw std::runtime_error("invalid input accepted at line "+std::to_string(where.line()));
}
int main()
{
  try
  {
    using namespace Measure;
    const auto movement=Design::DefaultMovement();

    // 2D views look along one axis each; the perspective view hides nothing.
    Check(DepthAxis(12)==2 && DepthAxis(13)==1 && DepthAxis(14)==0,"2D view axes");
    Check(DepthAxis(5)==-1 && DepthAxis(0)==-1 && DepthAxis(15)==-1,"a 3D view hides no axis");

    // Grid snap: all axes, except along a surface normal and a 2D view's axis.
    const std::array<double,3> grid{16,16,16};
    auto snapped=Snap({7,-9,23.9},grid);
    Check(snapped==Vector{0,-16,16},"snap to the grid per axis");
    snapped=Snap({7,-9,23.9},grid,{0,0,1});
    Check(snapped==Vector{0,-16,23.9},"a floor click keeps its height");
    snapped=Snap({7,-9,23.9},grid,{},2);
    Check(snapped==Vector{0,-16,23.9},"a top view click keeps its unknown height");
    snapped=Snap({7,-9,23.9},{0,16,16});
    Check(snapped[0]==7,"a zero grid axis is left alone");

    // A builder brush's opposite corners: 256 x 256 x 128.
    auto r=Compare({{-128,-128,0}},{{128,128,128}},movement);
    Check(Near(r.distance,std::sqrt(256.0*256*2+128*128)),"straight-line distance");
    Check(Near(r.horizontal,std::sqrt(256.0*256*2)),"horizontal distance");
    Check(r.delta==Vector{256,256,128} && r.plane==-1,"axis deltas");
    auto lines=Lines(r,movement);
    Check(lines.size()==4,"four readout lines");
    Check(lines[0]=="384 uu (2.1 player heights)","distance in units and player heights");
    Check(lines[1]=="dX 256   dY 256   dZ 128","delta line");
    Check(lines[2]=="Horizontal 362 uu (2.0 player heights)","horizontal line");
    // Map Design's timing: 128 up over 362 along is walked, at 300 uu/s.
    Check(Near(r.journey.spy,384/300.0) && r.journey.climb==0,"route time for a walkable slope");
    Check(lines[3]=="Run 1.3 s (spy or merc)","run time line");

    // One player height straight up is climbed at Map Design's climb speed.
    r=Compare({{0,0,0}},{{0,0,180}},movement);
    lines=Lines(r,movement);
    Check(lines[0]=="180 uu (1.0 player height)","one player height, singular");
    Check(Near(r.journey.climb,180) && Near(r.journey.spy,180/120.0),"a vertical line is climbed");
    Check(lines[3]=="Run 1.5 s (spy or merc), climbing 180 uu","climb noted");

    // Different team speeds are shown separately.
    auto quick=movement;quick["mercSpeed"]=200.0;
    lines=Lines(Compare({{0,0,0}},{{600,0,0}},quick),quick);
    Check(lines[3]=="Run: spy 2.0 s, merc 3.0 s","both teams' times");

    // Fractions: a tenth is shown, noise is not.
    Check(Units(512)=="512" && Units(511.96)=="512" && Units(12.25)=="12.3" && Units(-0.01)=="0","unit formatting");
    Check(Units(-64)=="-64","negative deltas keep their sign");

    // 2D clicks: both in the top view measure in its plane, heights unknown.
    r=Compare({{0,0,999},2},{{300,400,-5},2},movement);
    Check(r.plane==2 && Near(r.distance,500) && r.delta[2]==0,"top view measures in XY");
    lines=Lines(r,movement);
    Check(lines.size()==3,"no horizontal line in the top view: it is the distance");
    Check(lines[1]=="dX 300   dY 400   (in the top (XY) view)","top view deltas omit Z");
    Check(Ends(r)=="From (0, 0) to (300, 400)","top view ends omit Z");
    Check(Position({1.25,-2,3})=="(1.3, -2, 3)","a 3D position");
    // Top then side: each takes the hidden axis from the other click.
    r=Compare({{0,0,999},2},{{777,100,50},0},movement);
    Check(r.plane==-1 && r.from==Vector{0,0,50} && r.to==Vector{0,100,50},"mixed 2D views fill each other's axis");
    // A 2D click against a 3D click takes the 3D click's depth.
    r=Compare({{10,20,30}},{{40,555,70},1},movement);
    Check(r.to==Vector{40,20,70} && Near(r.distance,50),"front view click takes Y from the 3D click");

    Reject([&]{Compare({{0,0,0},3},{{1,1,1}},movement);});
    Reject([&]{Compare({{0,0,std::numeric_limits<double>::infinity()}},{{1,1,1}},movement);});
    Reject([&]{Compare({{0,0,0}},{{5000000,0,0}},movement);});

    // Screen maths. An orthographic top view: 1 screen unit = 512 world units,
    // camera over (1000, 2000). World X right, world Y down the screen.
    Matrix worldToScreen{1/512.f,0,0,0, 0,-1/512.f,0,0, 0,0,0.0001f,0, -1000/512.f,2000/512.f,0.5f,1};
    Matrix screenToWorld{512,0,0,0, 0,-512,0,0, 0,0,10000,0, 1000,2000,-5000,1};
    auto pixel=Project(worldToScreen,{1000,2000,0},800,600);
    Check(pixel && Near((*pixel)[0],400) && Near((*pixel)[1],300),"the camera's centre is the viewport's centre");
    pixel=Project(worldToScreen,{1512,2000,0},800,600);
    Check(pixel && Near((*pixel)[0],800),"screen +1 is the right edge");
    pixel=Project(worldToScreen,{1000,2512,0},800,600);
    Check(pixel && Near((*pixel)[1],600),"world +Y is down the top view");
    auto mouse=MouseInPlane(screenToWorld,600,150,800,600,2);
    Check(Near(mouse.at[0],1256,1e-3) && Near(mouse.at[1],1744,1e-3) && mouse.hidden==2,"mouse to the top view plane");
    auto back=FromPixels(600,150,800,600);auto again=ToPixels(back[0],back[1],800,600);
    Check(Near(again[0],600) && Near(again[1],150),"pixel round trip");
    Reject([&]{MouseInPlane(screenToWorld,1,1,800,600,-1);});
    Reject([&]{FromPixels(1,1,0,600);});

    // A perspective camera at (0,0,500) looking down -Z (screen X = world X,
    // screen Y = world Y): w is the distance below the camera.
    Matrix look{1,0,0,0, 0,1,0,0, 0,0,0,-1, 0,0,1,500};
    // Its inverse maps (sx,sy,z,1) to the world: z picks the depth.
    Matrix unlook{1,0,0,0, 0,1,0,0, 0,0,500,1, 0,0,-1,0};
    pixel=Project(look,{50,0,400},800,600);
    Check(pixel && Near((*pixel)[0],600),"a point 100 below the camera, 50 across, is half way right");
    Check(!Project(look,{0,0,600},800,600),"a point behind the camera has no pixel");
    // Screen x 0.5 at the floor, 500 below the camera, is world x 250.
    auto floor=MouseOnFloor(unlook,{0,0,500},600,300,800,600,0);
    Check(Near(floor.at[0],250,1e-3) && Near(floor.at[1],0,1e-3) && floor.at[2]==0 && floor.hidden==-1,"mouse ray onto the floor");
    Reject([&]{MouseOnFloor(unlook,{0,0,500},600,300,800,600,900);});

    // Sessions: Start/To, and the shortcut's chain.
    Session s;
    Check(!s.Complete() && !s.Last(),"empty session");
    s.To({{1,0,0}});Check(s.start && !s.end,"To with no start starts");
    s.To({{2,0,0}});Check(s.Complete() && s.end->at[0]==2,"To ends");
    s.Chain({{5,0,0}});Check(s.start->at[0]==2 && s.end->at[0]==5,"Chain continues from the last end");
    s.Start({{9,0,0}});Check(!s.end && s.Last()->at[0]==9,"Start drops the old end");
    s.Clear();Check(!s.start && !s.end,"Clear");
    s.Chain({{3,0,0}});Check(s.start && !s.end,"Chain from nothing starts");

    std::cout<<"Measure model tests passed\n";
    return 0;
  }
  catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}
