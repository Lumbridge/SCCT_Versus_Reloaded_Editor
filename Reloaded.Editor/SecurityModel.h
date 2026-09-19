#pragma once
#include "WorkflowModel.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

// Security devices for Versus maps: what each one is, how it is placed from
// clicks in a plan, and how detectors, alarms and the things alarms trigger
// are wired together through Event and Tag. Engine-independent.
namespace Workflow::Security
{
struct Device
{
    const char* kind;   // Name used by the panel and the workspace.
    const char* type;   // Actor class.
    int clicks;         // Clicks that place it: 1 for a point, 2 for from/to.
    const char* hint;   // What the second click means.
};
// The catalogue. Detectors send Event; an alarm receives it on Tag and sends
// its own Events list on.
inline const std::vector<Device>& Devices()
{
    static const std::vector<Device> devices={
        {"Laser","SBase.SLaserTrigger",2,"Click where the beam starts, then where it ends."},
        {"Camera","SBase.SCamera",2,"Click the camera, then a point it watches."},
        {"Motion sensor","SBase.SVolumetricSensor",2,"Click two opposite corners of the area it covers."},
        {"Presence detector","SBase.SPresenceTrigger",1,"Click where it sits; it detects within its radius."},
        {"Mine","SBase.SMineProx",1,"Click where the mine is planted."},
        {"Alarm","SBase.SAlarm",1,"Click where the alarm sits; detectors wire to it."}};
    return devices;
}
inline const Device* Find(const std::string& kind)
{
    for(const auto& device:Devices())if(kind==device.kind)return &device;
    return nullptr;
}
inline std::string KindOf(const std::string& type)
{
    for(const auto& device:Devices())if(type==device.type)return device.kind;
    return "";
}
inline bool Detector(const std::string& kind)
{
    return kind=="Laser" || kind=="Camera" || kind=="Motion sensor" || kind=="Presence detector" || kind=="Mine";
}
// Unreal yaw (65536 per turn) from one plan point towards another.
inline int YawTowards(const Vector& from,const Vector& to)
{
    for(const auto& v:{from,to})for(double n:v)if(!std::isfinite(n))throw std::runtime_error("Aim point is out of range.");
    const double dx=to[0]-from[0],dy=to[1]-from[1];
    if(std::abs(dx)<1e-9 && std::abs(dy)<1e-9)throw std::runtime_error("Click a second point away from the first.");
    const double turns=std::atan2(dy,dx)/(2*3.14159265358979323846);
    int yaw=static_cast<int>(std::lround(turns*65536))%65536;
    return yaw<0?yaw+65536:yaw;
}
// A laser fires from its actor along its yaw for LaserLength units, so two
// clicks give its pose and length.
struct Beam { Pose pose; int length; };
inline Beam LaserFromClicks(const Vector& from,const Vector& to)
{
    Beam beam;
    beam.pose.position=from;
    beam.pose.rotation={0,YawTowards(from,to),0};
    const double length=std::hypot(to[0]-from[0],to[1]-from[1]);
    if(length<8 || length>65536)throw std::runtime_error("A laser needs a beam between 8 and 65536 units long.");
    beam.length=static_cast<int>(std::lround(length));
    return beam;
}
// The end of a laser beam, for drawing it.
inline Vector BeamEnd(const Vector& position,int yaw,double length)
{
    const double angle=yaw*2*3.14159265358979323846/65536;
    return {position[0]+std::cos(angle)*length,position[1]+std::sin(angle)*length,position[2]};
}
// A beam in three dimensions: yaw turns it, pitch lifts it.
inline Vector BeamEnd(const Vector& position,const Rotation& rotation,double length)
{
    const double pi=3.14159265358979323846;
    const double yaw=rotation[1]*2*pi/65536,pitch=rotation[0]*2*pi/65536;
    return {position[0]+std::cos(pitch)*std::cos(yaw)*length,position[1]+std::cos(pitch)*std::sin(yaw)*length,position[2]+std::sin(pitch)*length};
}
// The rotation and length that point a beam from one point at another. A target
// straight above or below keeps the current yaw.
struct Aim { Rotation rotation; double length; };
inline Aim AimAt(const Vector& from,const Vector& to,const Rotation& current)
{
    for(const auto& v:{from,to})for(double n:v)if(!std::isfinite(n))throw std::runtime_error("Aim point is out of range.");
    const double pi=3.14159265358979323846;
    const double dx=to[0]-from[0],dy=to[1]-from[1],dz=to[2]-from[2];
    const double planar=std::hypot(dx,dy);
    Aim aim{current,std::hypot(planar,dz)};
    if(aim.length<1e-9)throw std::runtime_error("Aim at a point away from the device.");
    if(planar>1e-9)aim.rotation[1]=YawTowards(from,to);
    int pitch=static_cast<int>(std::lround(std::atan2(dz,planar)/(2*pi)*65536))%65536;
    aim.rotation[0]=pitch<0?pitch+65536:pitch;
    return aim;
}
// A motion sensor covers a box between two clicked corners, from the floor up
// to the given height.
struct Coverage { Vector lo,hi; };
inline Coverage SensorBox(const Vector& first,const Vector& second,double height)
{
    if(!std::isfinite(height) || height<8 || height>4096)throw std::runtime_error("Enter a sensor height between 8 and 4096 units.");
    Coverage box;
    for(int axis=0;axis<2;++axis)
    {
        box.lo[axis]=std::min(first[axis],second[axis]);
        box.hi[axis]=std::max(first[axis],second[axis]);
        if(box.hi[axis]-box.lo[axis]<8)throw std::runtime_error("Drag a sensor area at least 8 units across.");
    }
    box.lo[2]=std::min(first[2],second[2]);
    box.hi[2]=box.lo[2]+height;
    return box;
}
// Tags must be unique names; new ones count up from a readable stem.
inline std::string NextTag(const std::string& stem,const std::set<std::string>& used)
{
    std::string base;
    for(char c:stem)if(std::isalnum(static_cast<unsigned char>(c)) || c=='_')base+=c;
    if(base.empty() || !std::isalpha(static_cast<unsigned char>(base[0])))base="Sec"+base;
    for(int i=1;i<100000;++i)
    {
        auto tag=base+"_"+std::to_string(i);
        if(!used.count(Fold(tag)))return tag;
    }
    throw std::runtime_error("Could not allocate a Tag.");
}
// Wiring across the security actors in a map: each row is a detector, the
// alarm its Event reaches, and what that alarm's Events reach in turn.
struct Issue { std::string severity,text,path; };
inline Json Wiring(const Json& actors)
{
    Json rows=Json::array();
    auto tagged=[&](const std::string& tag)
    {
        Json matches=Json::array();
        if(tag.empty() || Fold(tag)=="none")return matches;
        for(const auto& actor:actors)if(Fold(actor.value("tag",std::string()))==Fold(tag))matches.push_back(actor);
        return matches;
    };
    for(const auto& actor:actors)
    {
        const auto kind=actor.value("kind",std::string());
        if(kind!="Alarm")continue;
        Json outputs=Json::array();
        for(const auto& event:actor.value("events",Json::array()))
            for(const auto& target:tagged(event.get<std::string>()))outputs.push_back(target.at("path"));
        Json detectors=Json::array();
        for(const auto& other:actors)
            if(Detector(other.value("kind",std::string())) && Fold(other.value("event",std::string()))==Fold(actor.value("tag",std::string())) && Fold(actor.value("tag",std::string()))!="none")
                detectors.push_back(other.at("path"));
        rows.push_back({{"alarm",actor.at("path")},{"detectors",detectors},{"outputs",outputs}});
    }
    return rows;
}
inline std::vector<Issue> Issues(const Json& actors)
{
    std::vector<Issue> issues;
    std::set<std::string> alarmTags;
    for(const auto& actor:actors)
        if(actor.value("kind",std::string())=="Alarm" && Fold(actor.value("tag",std::string()))!="none")alarmTags.insert(Fold(actor.value("tag",std::string())));
    for(const auto& actor:actors)
    {
        const auto kind=actor.value("kind",std::string());
        const auto path=actor.at("path").get<std::string>();
        const auto name=path.substr(path.find_last_of('.')+1);
        if(Detector(kind))
        {
            const auto event=actor.value("event",std::string());
            if(event.empty() || Fold(event)=="none")issues.push_back({"warning",name+" ("+kind+") triggers nothing: give it an alarm.",path});
            else if(!alarmTags.count(Fold(event)))
            {
                bool reaches=false;
                for(const auto& other:actors)if(Fold(other.value("tag",std::string()))==Fold(event))reaches=true;
                if(!reaches)issues.push_back({"error",name+" ("+kind+") sends Event "+event+" but nothing has that Tag.",path});
            }
        }
        if(kind=="Alarm")
        {
            bool fed=false;
            for(const auto& other:actors)
                if(Detector(other.value("kind",std::string())) && Fold(other.value("event",std::string()))==Fold(actor.value("tag",std::string())) && Fold(actor.value("tag",std::string()))!="none")fed=true;
            if(!fed)issues.push_back({"warning",name+" (alarm) has no detector wired to its Tag.",path});
            if(actor.value("events",Json::array()).empty())issues.push_back({"warning",name+" (alarm) triggers nothing when it goes off; add outputs or lock movers and objectives.",path});
        }
        if(kind=="Motion sensor" && actor.value("volumes",Json::array()).empty())
            issues.push_back({"error",name+" (motion sensor) covers no volume, so it never fires.",path});
    }
    return issues;
}
}
