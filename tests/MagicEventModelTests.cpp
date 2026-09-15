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
    Json groupSchema={{"kind","ArrayProperty"},{"category","SMagicEvent"},{"inner",action}};
    Json snapshot={{"actor",{{"path","MyLevel.EventA"},{"class","SBase.SMagicEvent"}}},
        {"schema",{{"Groups",groupSchema},{"Tag",name},{"Location",{{"kind","FloatProperty"}}}}},
        {"values",{{"Groups",Json::array({value})},{"Tag","EventA"},{"Location","12"}}}};
    Json targets=Json::array({{{"path","MyLevel.Door"},{"class","Engine.Mover"},{"tag","DoorA"},{"event","None"},{"authorable",true}}});
    auto document=Magic::ExportEvent(snapshot,targets,"MyLevel");
    assert(document["properties"].size()==1 && document["context"]["actors"][0]["tag"]=="DoorA");
    assert(Magic::ImportEventChanges(snapshot,Json::parse(document.dump(2))).empty());
    document["properties"]["Groups"][0]["Delay"]="2.5";
    assert(Magic::ImportEventChanges(snapshot,document)["Groups"][0]["Delay"]=="2.5");
    auto invalid=document;invalid["version"]=2;Reject([&]{Magic::ImportEventChanges(snapshot,invalid);});
    invalid=document;invalid["class"]="Engine.Mover";Reject([&]{Magic::ImportEventChanges(snapshot,invalid);});
    invalid=document;invalid["properties"]["Tag"]="OtherEvent";Reject([&]{Magic::ImportEventChanges(snapshot,invalid);});
    invalid=document;invalid["properties"]["Location"]="999";Reject([&]{Magic::ImportEventChanges(snapshot,invalid);});
    invalid=document;invalid["properties"]["Groups"][0]["Delay"]="NaN";invalid["schema"]["Groups"]={{"kind","StrProperty"}};Reject([&]{Magic::ImportEventChanges(snapshot,invalid);});
    invalid=document;invalid["properties"]["Groups"][0].erase("Type");Reject([&]{Magic::ImportEventChanges(snapshot,invalid);});
    invalid=document;invalid["properties"]["Groups"][0]["Event"]="DoorA,Injected=1";Reject([&]{Magic::ImportEventChanges(snapshot,invalid);});
    invalid=document;invalid["properties"]=Json::array();Reject([&]{Magic::ImportEventChanges(snapshot,invalid);});
    std::cout<<"Magic event model tests passed\n";
}
