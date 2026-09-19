#include "../Reloaded.Editor/SecurityModel.h"
#include <iostream>
#include <source_location>
using namespace Workflow;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
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
    // The catalogue names the classes the map editor loads.
    Check(Security::Find("Laser")->type==std::string("SBase.SLaserTrigger") && Security::Find("Alarm")->clicks==1,"catalogue incorrect");
    Check(Security::KindOf("SBase.SCamera")=="Camera" && Security::KindOf("Engine.Light").empty(),"class lookup incorrect");
    Check(Security::Detector("Mine") && !Security::Detector("Alarm"),"detector classification incorrect");

    // Yaw from two plan points, in Unreal units.
    Check(Security::YawTowards({0,0,0},{100,0,0})==0,"east must be yaw 0");
    Check(Security::YawTowards({0,0,0},{0,100,0})==16384,"north must be a quarter turn");
    Check(Security::YawTowards({0,0,0},{-100,0,0})==32768,"west must be a half turn");
    Check(Security::YawTowards({0,0,0},{0,-100,0})==49152,"south must be three quarters");
    Reject([]{Security::YawTowards({1,1,0},{1,1,0});});

    // A laser from two clicks: pose at the first, aimed at the second, as long
    // as the distance between them.
    auto beam=Security::LaserFromClicks({100,200,48},{400,600,48});
    Check(beam.pose.position==Vector{100,200,48} && beam.length==500,"laser pose or length incorrect");
    Check(std::abs(beam.pose.rotation[1]-Security::YawTowards({100,200,48},{400,600,48}))<=1,"laser aims at the second click");
    auto end=Security::BeamEnd({100,200,48},beam.pose.rotation[1],beam.length);
    // Aiming in three dimensions: pitch lifts the beam, and the end follows.
    auto aim=Security::AimAt({0,0,0},{100,0,100},{0,0,0});
    Check(aim.rotation[0]==8192 && aim.rotation[1]==0 && std::abs(aim.length-141.42)<0.1,"aiming at a point above sets pitch and length");
    auto lifted=Security::BeamEnd({0,0,0},Rotation{8192,0,0},aim.length);
    Check(std::abs(lifted[0]-100)<0.1 && std::abs(lifted[1])<0.1 && std::abs(lifted[2]-100)<0.1,"a pitched beam ends where it was aimed");
    auto below=Security::AimAt({0,0,0},{0,0,-50},{0,16384,0});
    Check(below.rotation[1]==16384 && below.rotation[0]==49152 && below.length==50,"aiming straight down keeps the yaw");
    Reject([]{Security::AimAt({0,0,0},{0,0,0},{0,0,0});});
    Check(std::abs(end[0]-400)<.5 && std::abs(end[1]-600)<.5 && end[2]==48,"beam end must land on the second click");
    Reject([]{Security::LaserFromClicks({0,0,0},{2,2,0});});

    // A motion sensor's box from two corners and a height.
    auto box=Security::SensorBox({300,50,0},{100,250,0},96);
    Check(box.lo==Vector{100,50,0} && box.hi==Vector{300,250,96},"sensor box incorrect");
    Reject([]{Security::SensorBox({0,0,0},{4,100,0},96);});
    Reject([]{Security::SensorBox({0,0,0},{100,100,0},2);});

    // Tags count up from a readable stem and skip names in use.
    std::set<std::string> used={"alarm_1"};
    Check(Security::NextTag("Alarm",used)=="Alarm_2","tags must skip used names case-insensitively");
    Check(Security::NextTag("9 bad*name",{})=="Sec9badname_1","tag stems are cleaned into valid names");

    // Wiring and its problems.
    Json actors=Json::array({
        {{"path","MyLevel.Laser0"},{"kind","Laser"},{"tag","Laser_1"},{"event","Alarm_1"}},
        {{"path","MyLevel.Cam0"},{"kind","Camera"},{"tag","Cam_1"},{"event","None"}},
        {{"path","MyLevel.Alarm0"},{"kind","Alarm"},{"tag","Alarm_1"},{"events",Json::array({"Door_1","Light_1"})}},
        {{"path","MyLevel.Alarm1"},{"kind","Alarm"},{"tag","Alarm_2"},{"events",Json::array()}},
        {{"path","MyLevel.Door0"},{"kind","Mover"},{"tag","Door_1"}},
        {{"path","MyLevel.Sensor0"},{"kind","Motion sensor"},{"tag","Sensor_1"},{"event","Nowhere"},{"volumes",Json::array()}}});
    auto wiring=Security::Wiring(actors);
    Check(wiring.size()==2 && wiring[0]["detectors"].size()==1 && wiring[0]["detectors"][0]=="MyLevel.Laser0","the laser reaches its alarm");
    Check(wiring[0]["outputs"].size()==1 && wiring[0]["outputs"][0]=="MyLevel.Door0","only tags that exist count as outputs");
    auto issues=Security::Issues(actors);
    auto has=[&](const char* text){for(auto& issue:issues)if(issue.text.find(text)!=std::string::npos)return true;return false;};
    Check(has("Cam0 (Camera) triggers nothing"),"an unwired camera is reported");
    Check(has("Alarm1 (alarm) has no detector"),"an alarm nobody feeds is reported");
    Check(has("Alarm1 (alarm) triggers nothing"),"an alarm with no outputs is reported");
    Check(has("Sensor0 (Motion sensor) sends Event Nowhere") && has("covers no volume"),"a dangling event and an empty sensor are reported");
    Check(!has("Laser0"),"a correctly wired laser is not reported");
    std::cout<<"Security model tests passed\n";
  }
  catch(const std::exception& e)
  {
    std::cerr<<"Security model tests failed: "<<e.what()<<"\n";
    return 1;
  }
}
