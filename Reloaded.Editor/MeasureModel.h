#pragma once
#include "MapDesignModel.h"
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

// The viewport measure tool: two clicked points, the distance between them in
// Unreal units and in player terms, and the screen maths for its overlay. The
// player sizes and run speeds are Map Design's movement limits, so a length
// reads the same in both tools.
namespace Workflow::Measure
{
// One end of a measurement. A click in a 2D viewport says nothing about the
// axis that view looks along, so that axis is hidden and takes the other end's
// value; two ends hidden on the same axis measure in that view's plane.
struct Point { Vector at{}; int hidden=-1; };

// The axis a 2D viewport looks along, from its camera's RendMap (this
// editor's Top, Front and Side are 12, 13 and 14, checked in the editor):
// Top (XY) looks down Z, Front (XZ) along Y, Side (YZ) along X. -1 for a 3D view.
inline int DepthAxis(int rendMap)
{
    switch(rendMap)
    {
    case 12: return 2;
    case 13: return 1;
    case 14: return 0;
    default: return -1;
    }
}
inline const char* PlaneName(int hidden)
{
    return hidden==2?"top (XY)":hidden==1?"front (XZ)":hidden==0?"side (YZ)":"";
}

// The grid snap Builder Brush > Place Here uses: every axis to the grid,
// except one along a clicked surface's normal (snapping across a surface would
// lift the point off it) and the hidden axis of a 2D view.
inline Vector Snap(Vector at,const std::array<double,3>& grid,const Vector& normal={},int hidden=-1)
{
    for(int axis=0;axis<3;++axis)
    {
        if(axis==hidden || !(grid[axis]>0) || !std::isfinite(grid[axis]) || std::abs(normal[axis])>=0.001)continue;
        at[axis]=std::round(at[axis]/grid[axis])*grid[axis];
    }
    return at;
}

struct Result
{
    Vector from{},to{},delta{};
    double distance=0,horizontal=0;
    int plane=-1;             // the hidden axis both ends share, or -1
    Design::Journey journey;  // Map Design's route timing for the straight line
};
// Fills each end's hidden axis from the other end, then measures.
inline Result Compare(Point a,Point b,const Json& movement=Design::DefaultMovement())
{
    for(const auto* p:{&a,&b})
    {
        if(p->hidden<-1 || p->hidden>2)throw std::runtime_error("Invalid measurement point.");
        Design::CheckVector(p->at);
    }
    Result r;
    if(a.hidden>=0 && a.hidden==b.hidden){r.plane=a.hidden;b.at[a.hidden]=a.at[a.hidden];}
    else
    {
        if(a.hidden>=0)a.at[a.hidden]=b.at[a.hidden];
        if(b.hidden>=0)b.at[b.hidden]=a.at[b.hidden];
    }
    r.from=a.at;r.to=b.at;
    for(int axis=0;axis<3;++axis)r.delta[axis]=b.at[axis]-a.at[axis];
    r.distance=Design::Distance(a.at,b.at);
    r.horizontal=std::hypot(r.delta[0],r.delta[1]);
    Json route={{"points",Json::array({r.from,r.to})}};
    r.journey=Design::RouteJourney(route,Json{{"movement",movement}});
    return r;
}

// Whole units unless the length has a fraction worth showing.
inline std::string Units(double value)
{
    if(!std::isfinite(value))return "?";
    const double tenths=std::round(value*10)/10;
    if(std::abs(tenths-std::round(tenths))<1e-9)return Design::Round(std::round(tenths)+0.0);
    return Design::Round(tenths,1);
}
inline std::string Heights(double length,const Json& movement)
{
    const double height=movement.at("height").get<double>();
    if(!(height>0))return "";
    const auto text=Design::Round(length/height,1);
    return text+(text=="1.0"?" player height":" player heights");
}
inline std::string Seconds(double value) { return Design::Round(value,1)+" s"; }

// The overlay's lines, also the readout's: the straight line in units and
// player heights, the axis deltas (without the one a 2D view cannot see), the
// horizontal run, and Map Design's route time for the line.
inline std::vector<std::string> Lines(const Result& r,const Json& movement=Design::DefaultMovement())
{
    std::vector<std::string> lines;
    lines.push_back(Units(r.distance)+" uu ("+Heights(r.distance,movement)+")");
    std::string deltas;const char* names[3]={"dX ","dY ","dZ "};
    for(int axis=0;axis<3;++axis)
    {
        if(axis==r.plane)continue;
        if(!deltas.empty())deltas+="   ";
        deltas+=names[axis]+Units(r.delta[axis]+0.0);
    }
    if(r.plane>=0)deltas+="   (in the "+std::string(PlaneName(r.plane))+" view)";
    lines.push_back(deltas);
    if(r.plane!=2)lines.push_back("Horizontal "+Units(r.horizontal)+" uu ("+Heights(r.horizontal,movement)+")");
    const double spy=movement.at("spySpeed").get<double>(),merc=movement.at("mercSpeed").get<double>();
    std::string times=spy==merc?"Run "+Seconds(r.journey.spy)+" (spy or merc)":"Run: spy "+Seconds(r.journey.spy)+", merc "+Seconds(r.journey.merc);
    if(r.journey.climb>0)times+=", climbing "+Units(r.journey.climb)+" uu";
    lines.push_back(times);
    return lines;
}
// Where the ends are, without the axis a 2D view cannot see.
inline std::string Position(const Vector& at,int hidden=-1)
{
    std::string text="(";
    for(int axis=0;axis<3;++axis)
    {
        if(axis==hidden)continue;
        if(text.size()>1)text+=", ";
        text+=Units(at[axis]+0.0);
    }
    return text+")";
}
inline std::string Ends(const Result& r)
{
    return "From "+Position(r.from,r.plane)+" to "+Position(r.to,r.plane);
}
inline std::string Text(const std::vector<std::string>& lines,const char* separator="\r\n")
{
    std::string text;
    for(const auto& line:lines){if(!text.empty())text+=separator;text+=line;}
    return text;
}

// Screen maths. A scene node's matrices transform row vectors (FMatrix::
// TransformFPlane); its screen space runs -1..1 with Y up.
using Matrix = std::array<float,16>;
using Plane = std::array<double,4>;
inline Plane Transform(const Matrix& m,const Plane& p)
{
    Plane out{};
    for(int column=0;column<4;++column)
        for(int row=0;row<4;++row)out[column]+=p[row]*m[row*4+column];
    return out;
}
// Screen (-1..1) to the viewport's pixels and back.
inline std::array<double,2> ToPixels(double x,double y,int width,int height)
{ return {(x+1)*0.5*width,(1-y)*0.5*height}; }
inline std::array<double,2> FromPixels(double px,double py,int width,int height)
{
    if(width<=0 || height<=0)throw std::runtime_error("The viewport has no size.");
    return {px/width*2-1,1-py/height*2};
}
// A world point's pixel, or nothing when it is behind the camera.
inline std::optional<std::array<double,2>> Project(const Matrix& worldToScreen,const Vector& at,int width,int height)
{
    const auto p=Transform(worldToScreen,{at[0],at[1],at[2],1});
    if(!(p[3]>1e-6) || !std::isfinite(p[3]))return std::nullopt;
    return ToPixels(p[0]/p[3],p[1]/p[3],width,height);
}
// The world point under a pixel at screen depth z (homogeneous divide included).
inline Vector Deproject(const Matrix& screenToWorld,double px,double py,double z,int width,int height)
{
    const auto s=FromPixels(px,py,width,height);
    const auto p=Transform(screenToWorld,{s[0],s[1],z,1});
    if(!std::isfinite(p[3]) || std::abs(p[3])<1e-12)throw std::runtime_error("The point is out of view.");
    return {p[0]/p[3],p[1]/p[3],p[2]/p[3]};
}
// The mouse in a 2D view: the point under it, its view axis hidden.
inline Point MouseInPlane(const Matrix& screenToWorld,double px,double py,int width,int height,int hidden)
{
    if(hidden<0 || hidden>2)throw std::runtime_error("Not a 2D view.");
    Point p{Deproject(screenToWorld,px,py,0.5,width,height),hidden};
    Design::CheckVector(p.at);
    return p;
}
// The mouse in a 3D view: where its ray from the camera meets the horizontal
// plane at height z (the floor of the last point measured from).
inline Point MouseOnFloor(const Matrix& screenToWorld,const Vector& camera,double px,double py,int width,int height,double z)
{
    auto nearer=Deproject(screenToWorld,px,py,0.1,width,height),farther=Deproject(screenToWorld,px,py,0.9,width,height);
    auto distance=[&](const Vector& v){return Design::Distance(v,camera);};
    if(distance(farther)<distance(nearer))std::swap(nearer,farther);
    Vector direction{};for(int axis=0;axis<3;++axis)direction[axis]=farther[axis]-nearer[axis];
    if(std::abs(direction[2])<1e-9)throw std::runtime_error("The mouse is level with the floor; look down at it.");
    const double t=(z-camera[2])/direction[2];
    if(!(t>0))throw std::runtime_error("The mouse points away from the floor; look down at it.");
    Point p;for(int axis=0;axis<3;++axis)p.at[axis]=camera[axis]+direction[axis]*t;
    p.at[2]=z;Design::CheckVector(p.at);
    return p;
}

// The two ends being measured. The keyboard shortcut chains: each press
// measures from the last end to the mouse.
struct Session
{
    std::optional<Point> start,end;
    void Start(const Point& p){start=p;end.reset();}
    void To(const Point& p){if(!start)start=p;else end=p;}
    void Chain(const Point& p)
    {
        if(!start){start=p;return;}
        if(end)start=end;
        end=p;
    }
    // The end a chained measurement continues from.
    std::optional<Point> Last() const { return end?end:start; }
    void Clear(){start.reset();end.reset();}
    bool Complete() const { return start && end; }
};
}
