#include "../Reloaded.Editor/MagicEventModel.h"
#include <cassert>
#include <iostream>
#include <functional>
using namespace Workflow;
static void Reject(const std::function<void()>& fn){bool rejected=false;try{fn();}catch(...){rejected=true;}assert(rejected);}
int main()
{
    Json name={{"kind","NameProperty"}},delay={{"kind","FloatProperty"}};
    Json action={{"kind","StructProperty"},{"fields",{{"Event",name},{"Delay",delay},{"Type",{{"kind","ByteProperty"},{"choices",{"EVT_Trigger","EVT_Untrigger"}}}}}}};
    auto value=Magic::Default(action);assert(value["Event"]=="None" && value["Delay"]=="0" && value["Type"]=="EVT_Trigger");
    value["Delay"]="1.25";value["Event"]="DoorA";Magic::Validate(action,value);
    assert(Magic::Text(value)=="(Delay=1.25,Event=DoorA,Type=EVT_Trigger)");
    auto copy=value;copy["Type"]="Unknown";Reject([&]{Magic::Validate(action,copy);});
    Reject([&]{Magic::Number("1.2 seconds");});Reject([&]{Magic::Number("nan");});Reject([&]{Magic::Seconds(-0.1);});
    Reject([&]{Magic::Validate(name,"DoorA,Delay=3");});
    Json groups=Json::array({{{"SequenceIndex","2"},{"EventGroup",Json::array({value,value})},{"Repeat","4"}},{{"SequenceIndex","0"},{"EventGroup",Json::array()},{"Repeat","0"}}});
    auto original=groups;Magic::Move(groups,0,1);assert(groups[1]==original[0]);Magic::Move(groups,1,-1);assert(groups==original);Magic::Move(groups,0,-1);assert(groups==original);
    groups[0]["EventGroup"][1]["Delay"]="2.5";assert(groups[0]["SequenceIndex"]=="2" && groups[0]["Repeat"]=="4" && groups[0]["EventGroup"][0]==value);
    assert(Magic::Text(Json::array())=="()");auto box=Magic::BoxPolygons();size_t count=0,position=0;while((position=box.find("Begin Polygon",position))!=std::string::npos){++count;++position;}assert(count==6);Reject([&]{Magic::BoxPolygons(0);});
    assert(Magic::Category("SBase.SAmbientSoundTrigger")=="Sounds");
    std::cout<<"Magic event model tests passed\n";
}
