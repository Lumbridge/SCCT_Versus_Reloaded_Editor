#include "../Reloaded.Editor/MapDesignModel.h"
#include <iostream>
#include <limits>
using namespace Workflow;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void Reject(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}Check(caught,"invalid input accepted");}
double Volume(const Design::Solid& solid)
{
    double result=0;
    for(auto& f:solid.faces)for(size_t i=1;i+1<f.size();++i){auto a=f[0],b=f[i],c=f[i+1];result+=(a[0]*(b[1]*c[2]-b[2]*c[1])+a[1]*(b[2]*c[0]-b[0]*c[2])+a[2]*(b[0]*c[1]-b[1]*c[0]))/6;}
    return result;
}
int main()
{
    Json spec={{"kind","Platform"},{"width",128},{"length",256},{"height",64},{"thickness",16},{"steps",8},{"ceiling",true}};
    auto platform=Design::Geometry(spec);Check(std::abs(Volume(platform[0])-128*256*64)<.01,"box winding or dimensions wrong");
    spec["kind"]="Ramp";Check(std::abs(Volume(Design::Geometry(spec)[0])-128*256*64/2)<.01,"ramp winding wrong");
    spec["kind"]="Stairs";auto stairs=Design::Geometry(spec);Check(stairs.size()==8,"stair count wrong");for(auto& step:stairs)Check(Volume(step)>0,"inverted step");
    Check(stairs.back().faces[0][2][2]==64,"stairs do not reach total rise");
    spec["kind"]="Room";auto room=Design::Geometry(spec);Check(room.size()==7 && room[0].subtract,"room must carve before adding walls");
    spec["ceiling"]=false;Check(Design::Geometry(spec).size()==6,"ceiling opt-out failed");
    spec["kind"]="Corridor";Check(Design::Geometry(spec).size()==4,"corridor must have open ends");
    spec["kind"]="Doorway";Check(Design::Geometry(spec)[0].subtract,"doorway must subtract");
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
    Check(linkedText.find("Owner=Actor'Map.Batch_Copy1_A'")!=std::string::npos,"repeated object reference escapes its copy");
    Reject([&]{Design::Repeat(def,129,{0,0,0},0);});
    std::cout<<"Map Design model tests passed\n";
}
