#pragma once
#include "WorkflowModel.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace Workflow::Design
{
using Face = std::vector<Vector>;
struct Solid { std::vector<Face> faces; bool subtract=false; };
inline double Number(const std::string& text,double minimum=-100000,double maximum=100000)
{
    size_t used=0;double n=std::stod(text,&used);
    if(used!=text.size() || !std::isfinite(n) || n<minimum || n>maximum)throw std::runtime_error("Enter a finite number in the supported range.");
    return n;
}
inline void CheckVector(const Vector& v) { for(auto n:v)if(!std::isfinite(n) || std::abs(n)>1000000)throw std::runtime_error("Position is out of range."); }
inline Solid Box(Vector lo,Vector hi,bool subtract=false)
{
    CheckVector(lo);CheckVector(hi);for(int i=0;i<3;++i)if(hi[i]<=lo[i])throw std::runtime_error("A brush must have positive dimensions.");
    const int corners[6][4]={{1,3,7,5},{0,4,6,2},{2,6,7,3},{0,1,5,4},{4,5,7,6},{0,2,3,1}};
    Solid s;s.subtract=subtract;
    for(auto& face:corners){Face f;for(auto c:face)f.push_back({c&1?hi[0]:lo[0],c&2?hi[1]:lo[1],c&4?hi[2]:lo[2]});s.faces.push_back(f);}return s;
}
inline std::vector<Solid> Geometry(const Json& spec)
{
    auto kind=spec.at("kind").get<std::string>();
    const double w=spec.at("width"),l=spec.at("length"),h=spec.at("height"),t=spec.value("thickness",16.0);
    for(auto n:{w,l,h,t})if(!std::isfinite(n) || n<1 || n>65536)throw std::runtime_error("Dimensions must be between 1 and 65536 units.");
    std::vector<Solid> out;
    auto box=[&](Vector a,Vector b,bool sub=false){out.push_back(Box(a,b,sub));};
    if(kind=="Room" || kind=="Corridor")
    {
        box({-w/2-t,-l/2-t,-t},{w/2+t,l/2+t,h+t},true);
        box({-w/2-t,-l/2-t,-t},{w/2+t,l/2+t,0});
        box({-w/2-t,-l/2-t,0},{-w/2,l/2+t,h});box({w/2,-l/2-t,0},{w/2+t,l/2+t,h});
        if(kind=="Room"){box({-w/2,-l/2-t,0},{w/2,-l/2,h});box({-w/2,l/2,0},{w/2,l/2+t,h});}
        if(spec.value("ceiling",true))box({-w/2-t,-l/2-t,h},{w/2+t,l/2+t,h+t});
    }
    else if(kind=="Doorway")box({-w/2,-l/2,0},{w/2,l/2,h},true);
    else if(kind=="Platform")box({-w/2,-l/2,0},{w/2,l/2,h});
    else if(kind=="Stairs")
    {
        int steps=spec.value("steps",8);if(steps<1 || steps>128)throw std::runtime_error("Use 1 to 128 steps.");
        for(int i=0;i<steps;++i)box({-w/2,-l/2+l*i/steps,0},{w/2,-l/2+l*(i+1)/steps,h*(i+1)/steps});
    }
    else if(kind=="Ramp")
    {
        Vector a{-w/2,-l/2,0},b{w/2,-l/2,0},c{w/2,l/2,0},d{-w/2,l/2,0},e{w/2,l/2,h},f{-w/2,l/2,h};
        out.push_back({{{a,d,c,b},{a,b,e,f},{b,c,e},{a,f,d},{d,f,e,c}},false});
    }
    else throw std::runtime_error("Unknown blockout shape.");
    return out;
}
inline Json Definition(const Json& spec)
{
    auto geometry=Geometry(spec);Json actors=Json::array();size_t index=0;
    for(auto& solid:geometry)
    {
        auto name="Block"+std::to_string(index++);std::ostringstream text;
        text<<"Begin Actor Class=Engine.Brush Name="<<name<<"\nCsgOper="<<(solid.subtract?"CSG_Subtract":"CSG_Add")<<"\nBegin Brush Name="<<name<<"Model\nBegin PolyList\n";
        for(auto& face:solid.faces){text<<"Begin Polygon Flags=0\n";for(auto& v:face)text<<"Vertex "<<v[0]<<","<<v[1]<<","<<v[2]<<"\n";text<<"End Polygon\n";}
        text<<"End PolyList\nEnd Brush\nBrush=Model'MyLevel."<<name<<"Model'\nEnd Actor\n";
        actors.push_back({{"name",name},{"class","Engine.Brush"},{"path","MyLevel."+name},{"text",text.str()},{"position",Vector{}},{"rotation",Rotation{}},{"tag","None"},{"event","None"}});
    }
    return {{"id","blockout"},{"actors",actors},{"bindings",Json::array()},{"dependencies",Json::array()}};
}
inline double Distance(const Vector& a,const Vector& b)
{CheckVector(a);CheckVector(b);double d=0;for(int i=0;i<3;++i)d+=(a[i]-b[i])*(a[i]-b[i]);return std::sqrt(d);}
inline double Calibration(double pixels,double units)
{if(!std::isfinite(pixels)||!std::isfinite(units)||pixels<1 || units<=0 || units>1000000)throw std::runtime_error("Calibration needs two distinct image points and a positive world distance.");return units/pixels;}
inline std::vector<Vector> Align(const std::vector<Vector>& positions,int axis,const std::string& mode,double spacing)
{
    if(positions.empty() || axis<0 || axis>2 || !std::isfinite(spacing))throw std::runtime_error("Select actors and a valid axis.");
    auto result=positions;for(auto& p:positions)CheckVector(p);
    if(mode=="Align")for(auto& p:result)p[axis]=positions.front()[axis];
    else if(mode=="Distribute")
    {
        std::vector<size_t> order;for(size_t i=0;i<positions.size();++i)order.push_back(i);
        std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return positions[a][axis]<positions[b][axis];});
        for(size_t i=0;i<order.size();++i)result[order[i]][axis]=positions[order[0]][axis]+spacing*i;
    }
    else throw std::runtime_error("Unknown alignment mode.");
    for(auto& p:result)CheckVector(p);return result;
}
inline Json Repeat(const Json& definition,int count,const Vector& spacing,int yawStep)
{
    if(count<1 || count>128 || definition.at("actors").size()*count>2000)throw std::runtime_error("Use up to 128 copies and 2000 actors.");
    CheckVector(spacing);Json result=definition;result["actors"]=Json::array();
    for(int i=0;i<count;++i)
    {
        Pose pose;for(int a=0;a<3;++a)pose.position[a]=spacing[a]*i;CheckVector(pose.position);pose.rotation[1]=static_cast<int>((static_cast<int64_t>(yawStep)*i)%65536);
        std::map<std::string,std::string> bindings;for(auto& b:definition.at("bindings"))bindings[b.at("id")]=b.at("path");
        auto copy=PreparePlacement(definition,pose,"Copy"+std::to_string(i)+"_","MyLevel",bindings);
        for(auto actor:copy["actors"])
        {
            actor["path"]="MyLevel."+actor.at("name").get<std::string>();
            for(const char* prop:{"Tag","Event"}){auto value=Property(actor.at("text"),prop);if(!value.empty())actor[Fold(prop)]=value;}
            result["actors"].push_back(actor);
        }
    }
    return result;
}
}
