// Standalone: cl /std:c++20 /W4 /WX /EHsc tests\PlacementModelTests.cpp
#include "../Reloaded.Editor/PlacementModel.h"
#include <iostream>
#include <source_location>
using namespace Workflow;
using namespace Workflow::Placement;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool Near(double a,double b,double tolerance=1e-6){return std::abs(a-b)<=tolerance;}
bool Near(const Vector& a,const Vector& b,double tolerance=1e-6){return Near(a[0],b[0],tolerance)&&Near(a[1],b[1],tolerance)&&Near(a[2],b[2],tolerance);}
template<class F> void Reject(F f,std::source_location where=std::source_location::current())
{
    bool caught=false;
    try{f();}catch(const std::exception&){caught=true;}
    if(!caught)throw std::runtime_error("invalid input accepted at line "+std::to_string(where.line()));
}
int YawDistance(int a,int b){int d=((a-b)%65536+65536)%65536;return std::min(d,65536-d);}
int main()
{
  try
  {
    // Axes and back: the engine's rotator conventions survive a round trip.
    for(Rotation r:{Rotation{0,0,0},Rotation{0,16384,0},Rotation{2000,-12000,700},Rotation{-8000,30000,-5000}})
    {
        auto back=FromAxes(Axes(r));
        Check(std::abs(back[0]-r[0])<=1 && YawDistance(back[1],r[1])<=1 && YawDistance(back[2],r[2])<=1,"rotation round trip");
    }
    // Yaw 90 degrees turns +X towards +Y.
    Check(Near(Column(Axes({0,16384,0}),0),Vector{0,1,0}),"yaw turns X towards Y");
    Check(Near(Turn({100,0,5},16384),Vector{0,100,5}),"Turn about Z");

    // A static mesh 64 long, 32 wide, 16 tall with its pivot at its base,
    // turned 90 degrees and scaled 2x: its world box.
    Box mesh{{-32,-16,0},{32,16,16}};
    auto world=WorldBox(mesh,{1000,0,100},{0,16384,0},{2,2,2});
    Check(Near(world.lo,Vector{968,-64,100},1e-6) && Near(world.hi,Vector{1032,64,132},1e-6),"turned, scaled mesh box");
    // PrePivot comes off before the scale.
    world=WorldBox(mesh,{0,0,0},{},{1,1,1},{0,0,8});
    Check(Near(world.lo[2],-8) && Near(world.hi[2],8),"pre-pivot shifts the mesh");
    auto pawn=CylinderBox({0,0,50},22,40);
    Check(Near(pawn.lo,Vector{-22,-22,10}) && Near(pawn.hi,Vector{22,22,90}),"collision cylinder box");
    Reject([]{CylinderBox({0,0,0},-1,5);});

    // Align: to the key actor's minimum, centre or maximum along one axis.
    std::vector<Box> boxes{{{0,0,0},{10,10,10}},{{50,5,20},{70,15,40}},{{100,0,0},{110,10,100}}};
    auto deltas=AlignDeltas(boxes,2,0,Edge::Min);
    Check(Near(deltas[0],Vector{100,0,0}) && Near(deltas[1],Vector{50,0,0}) && Near(deltas[2],Vector{0,0,0}),"align X minimum to the key");
    deltas=AlignDeltas(boxes,0,2,Edge::Max);
    Check(Near(deltas[1],Vector{0,0,-30}) && Near(deltas[2],Vector{0,0,-90}),"align Z maximum");
    deltas=AlignDeltas(boxes,1,1,Edge::Centre);
    Check(Near(deltas[0],Vector{0,5,0}) && Near(deltas[2],Vector{0,5,0}) && Near(deltas[1],Vector{0,0,0}),"align Y centre");
    Reject([&]{AlignDeltas({boxes[0]},0,0,Edge::Min);});
    Reject([&]{AlignDeltas(boxes,3,0,Edge::Min);});
    Reject([&]{AlignDeltas(boxes,0,3,Edge::Min);});

    // Distribute along X: outermost stay, the rest evenly between, in order.
    std::vector<Box> row{{{90,0,0},{110,10,10}},{{-10,0,0},{10,10,10}},{{20,0,0},{30,0,0}},{{60,7,0},{80,9,0}}};
    deltas=DistributeAxisDeltas(row,0);
    Check(Near(deltas[1],Vector{0,0,0}) && Near(deltas[0],Vector{0,0,0}),"the ends stay");
    Check(Near(deltas[2],Vector{100.0/3-25,0,0}) && Near(deltas[3],Vector{200.0/3-70,0,0}),"evenly spaced centres");
    Reject([&]{DistributeAxisDeltas({row[0],row[1]},0);});
    // Distribute on the line between the first and last selected.
    std::vector<Box> line{{{0,0,0},{0,0,0}},{{90,40,0},{90,40,0}},{{20,-30,0},{20,-30,0}},{{300,0,300},{300,0,300}}};
    deltas=DistributeLineDeltas(line,0,3);
    Check(Near(Add(Centre(line[2]),deltas[2]),Vector{100,0,100}),"first in line order is a third of the way");
    Check(Near(Add(Centre(line[1]),deltas[1]),Vector{200,0,200}),"second is two thirds of the way");
    Check(Near(deltas[0],Vector{0,0,0}) && Near(deltas[3],Vector{0,0,0}),"line ends stay");
    Reject([&]{DistributeLineDeltas(line,1,1);});
    Reject([&]{DistributeLineDeltas({line[0],line[0],line[1]},0,1);});

    // Drop: rays from the box's centre depth; the nearest surface wins.
    Box crate{{-16,-16,100},{16,16,132}};
    const Vector down=DropDirection(Surface::Floor,{});
    Check(Near(down,Vector{0,0,-1}) && Near(DropDirection(Surface::Ceiling,{}),Vector{0,0,1}),"floor and ceiling directions");
    Check(Near(DropDirection(Surface::Wall,{0,16384,0}),Vector{0,1,0}),"wall ahead follows the yaw");
    Check(Near(DropDirection(Surface::Wall,{3000,0,0}),Vector{1,0,0}),"a pitched actor still looks level for its wall");
    Reject([]{DropDirection(Surface::Wall,{16384,0,0});});
    Check(Near(Support(crate,down),16) && Near(Support(crate,Vector{1,0,0}),16),"support along an axis");
    auto starts=DropStarts(crate,down);
    Check(starts.size()==5 && Near(starts[0],Vector{0,0,116}),"five rays from the centre");
    for(size_t i=1;i<5;++i)Check(Near(std::abs(starts[i][0]),12.8) && Near(std::abs(starts[i][1]),12.8) && Near(starts[i][2],116),"corner rays at 80% of the footprint");
    // Floor 116 below the centre: the box's bottom lands on it.
    auto move=DropDistance(crate,down,{116.0,116.0,std::nullopt,116.0,116.0});
    Check(move && Near(*move,100),"drop rests the bottom on the floor");
    // A step under one corner is nearer: that one holds the box.
    move=DropDistance(crate,down,{116.0,116.0,40.0,116.0,116.0});
    Check(move && Near(*move,24),"the nearest hit holds the box up");
    // A ray that hit at once started inside a wall; it is ignored.
    move=DropDistance(crate,down,{0.0,116.0,116.0,116.0,116.0});
    Check(move && Near(*move,100),"start-solid rays are ignored");
    Check(!DropDistance(crate,down,{std::nullopt,std::nullopt,std::nullopt,std::nullopt,std::nullopt}),"nothing below");
    // Sunk into the floor: it comes up.
    move=DropDistance(crate,down,{8.0,8.0,8.0,8.0,8.0});
    Check(move && Near(*move,-8),"a sunk box rises");
    // A pawn's cylinder: its location ends one collision height above the floor.
    auto standing=CylinderBox({0,0,500},22,40);
    move=DropDistance(standing,down,{500.0});
    Check(move && Near(500-*move,40),"pawn rests on its collision height");

    // Aligning to a surface: up along a floor normal, yaw kept on a level floor.
    auto aligned=AlignToSurface({0,12000,0},{0,0,1},Surface::Floor);
    Check(aligned==Rotation({0,12000,0}),"a level floor changes nothing");
    const double s=std::sqrt(0.5);
    aligned=AlignToSurface({0,0,0},{s,0,s},Surface::Floor);
    Check(Near(Column(Axes(aligned),2),Vector{s,0,s},1e-4),"up axis follows a 45 degree slope");
    aligned=AlignToSurface({0,16384,0},{0,s,s},Surface::Floor);
    Check(Near(Column(Axes(aligned),2),Vector{0,s,s},1e-4),"slope across a turned actor");
    Check(Near(Column(Axes(aligned),0),Vector{0,s,-s},1e-4) || Near(Column(Axes(aligned),0),Vector{0,s,s},1e-4) || Dot(Column(Axes(aligned),0),Vector{0,1,0})>0.5,"the front still points the same way");
    aligned=AlignToSurface({0,0,0},{0,0,-1},Surface::Ceiling);
    Check(aligned==Rotation({0,0,0}),"a level ceiling keeps an upright actor");
    aligned=AlignToSurface({0,0,0},{0,-1,0},Surface::Wall);
    Check(Near(Column(Axes(aligned),0),Vector{0,1,0},1e-4),"facing into the wall");
    aligned=AlignToSurface({0,0,0},{0,0,-1},Surface::Floor);
    Check(Near(Column(Axes(aligned),2),Vector{0,0,-1},1e-4),"upside-down surface flips the actor");

    // Copies in a line.
    auto copies=Linear(3,{128,0,0});
    Check(copies.size()==3 && Near(Apply(copies[2],Vector{10,20,30}),Vector{394,20,30}),"linear copies");
    Check(Apply(copies[0],Rotation{1,2,3})==Rotation({1,2,3}),"linear copies keep rotation");
    Reject([]{Linear(0,{1,0,0});});
    Reject([]{Linear(3,{0,0,0});});
    Reject([]{Linear(kMaxCopies+1,{8,0,0});});

    // Round a centre, on the selection's own circle: 3 copies of a quarter turn each.
    copies=Radial(3,{100,0,0},{0,0,0},360,0,true);
    Check(copies.size()==3,"three radial copies");
    Check(Near(Apply(copies[0],Vector{100,0,0}),Vector{0,100,0},1e-6),"first copy a quarter turn round");
    Check(Near(Apply(copies[2],Vector{100,0,0}),Vector{0,-100,0},1e-6),"last copy three quarters round");
    Check(Apply(copies[1],Rotation{0,0,0})[1]==32768,"copies turn to face the centre as the original does");
    copies=Radial(3,{100,0,0},{0,0,0},360,0,false);
    Check(Apply(copies[1],Rotation{0,500,0})[1]==500,"unturned copies keep their yaw");
    // A half circle of 2 copies: the last on the arc's end.
    copies=Radial(2,{100,0,0},{0,0,0},180,0,false);
    Check(Near(Apply(copies[1],Vector{100,0,0}),Vector{-100,0,0},1e-6),"the arc ends on the last copy");
    // With a radius: every copy that far out, level with the selection.
    copies=Radial(4,{0,0,50},{0,0,0},360,200,false);
    Check(Near(Apply(copies[0],Vector{0,0,50}),Vector{200,0,50},1e-6),"a selection on the centre starts along +X");
    Check(Near(Apply(copies[1],Vector{0,0,50}),Vector{0,200,50},1e-6),"then round in quarter turns");
    copies=Radial(2,{0,300,0},{0,0,0},360,100,false);
    Check(Near(Apply(copies[0],Vector{0,300,0}),Vector{0,100,0},1e-6),"radius copies start in the selection's direction");
    Check(Near(Apply(copies[0],Vector{10,300,0}),Vector{10,100,0},1e-6),"a group keeps its shape");
    Reject([]{Radial(3,{0,0,0},{0,0,0},360,0,false);});
    Reject([]{Radial(3,{10,0,0},{0,0,0},0,0,false);});
    Reject([]{Radial(3,{10,0,0},{0,0,0},400,0,false);});
    Reject([]{Radial(3,{10,0,0},{0,0,0},360,-5,false);});

    // Along a path: count with the last on the end, or by spacing.
    copies=Path({0,0,0},{300,0,30},3,0);
    Check(copies.size()==3 && Near(Apply(copies[0],Vector{0,0,0}),Vector{100,0,10}) && Near(Apply(copies[2],Vector{5,5,5}),Vector{305,5,35}),"path copies by count");
    copies=Path({0,0,0},{250,0,0},0,64);
    Check(copies.size()==3 && Near(Apply(copies[2],Vector{0,0,0}),Vector{192,0,0}),"path copies by spacing");
    copies=Path({0,0,0},{256,0,0},0,64);
    Check(copies.size()==4,"a spacing that divides the path reaches its end");
    Reject([]{Path({0,0,0},{0,0,0},3,0);});
    Reject([]{Path({0,0,0},{10,0,0},0,64);});
    Reject([]{Path({0,0,0},{100000,0,0},0,1);});

    CheckCount(10,4);
    Reject([]{CheckCount(0,1);});
    Reject([]{CheckCount(5,0);});
    Reject([]{CheckCount(500,5);});

    // Preview edges: turned with a turning copy, else carried along.
    auto edges=BoxEdges({{-10,-10,0},{10,10,0}},Copy{{},{0,0,0},16384,true,{}});
    Check(edges.size()==12,"twelve box edges");
    edges=BoxEdges({{90,-5,0},{110,5,0}},Copy{{},{0,0,0},16384,false,{}});
    Check(Near(edges[0].first,Vector{-10,95,0},1e-6) || Near(edges[0].first,Vector{-10,95,0},1e-6),"an unturned copy keeps its box square");
    Check(Near(edges[0].second[0]-edges[0].first[0],20,1e-6),"box edge stays along X");

    // Selection order: new ones append, deselected ones go.
    std::vector<int> order;
    UpdateOrder(order,{5,3});
    UpdateOrder(order,{3,5,9});
    Check(order==std::vector<int>({5,3,9}),"newly selected goes last");
    UpdateOrder(order,{9,5});
    Check(order==std::vector<int>({5,9}),"deselected leaves the order");
    UpdateOrder(order,{9,5,3});
    Check(order.back()==3,"reselected comes last");

    std::cout<<"PlacementModelTests passed\n";
    return 0;
  }
  catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}
