#include "../Reloaded.Editor/StageModel.h"
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
// A small Versus map: one mission holding three objectives (two terminals and
// a flag), a door, a switchable light and a sound trigger, with no zones yet.
Json Map()
{
    auto actor=[](const char* path,const char* type,const char* kind,const char* tag,const char* event,Vector at)
    {
        return Json{{"path",std::string("MyLevel.")+path},{"class",type},{"kind",kind},{"tag",tag},{"event",event},{"name",""},{"position",at}};
    };
    Json actors=Json::array();
    auto mission=actor("SMission0","SBase.SMission","Mission","SMission0","None",{0,0,0});
    mission["objectives"]={"MyLevel.SObjective_1","MyLevel.SObjective_2","MyLevel.SObjective_3"};
    mission["chained"]=false;mission["minimum"]="0";mission["mode"]="GM_Multi";
    mission["name"]="Mission";mission["brief"]="";mission["briefDefend"]="";
    actors.push_back(mission);
    auto o1=actor("SObjective_1","SBase.SObjective","Objective","SObjective_1","None",{100,100,0});o1["name"]="Hack";o1["triggers"]={"MyLevel.SComputerObjectiveTrigger_1"};
    auto o2=actor("SObjective_2","SBase.SObjective","Objective","SObjective_2","None",{300,100,0});o2["name"]="Bomb";o2["triggers"]={"MyLevel.SBombTargetObjectiveTrigger_1"};
    auto o3=actor("SObjective_3","SBase.SObjective","Objective","SObjective_3","None",{2000,100,0});o3["name"]="Steal";o3["triggers"]={"MyLevel.SFlag_1"};
    actors.push_back(o1);actors.push_back(o2);actors.push_back(o3);
    auto t1=actor("SComputerObjectiveTrigger_1","SBase.SComputerObjectiveTrigger","Objective trigger","None","None",{110,100,0});t1["usable"]=true;t1["method"]="TriggerToggle";
    auto t2=actor("SBombTargetObjectiveTrigger_1","SBase.SBombTargetObjectiveTrigger","Objective trigger","None","None",{310,100,0});t2["usable"]=true;t2["method"]="TriggerToggle";
    auto t3=actor("SFlag_1","SBase.SFlag","Objective trigger","None","None",{2010,100,0});t3["usable"]=true;t3["method"]="TriggerToggle";
    actors.push_back(t1);actors.push_back(t2);actors.push_back(t3);
    auto door=actor("Mover3","Engine.Mover","Mover","None","None",{1000,100,0});door["state"]="TriggerOpenTimed";actors.push_back(door);
    auto light=actor("STriggerLight0","SBase.STriggerLight","Light","Lamp","None",{1000,200,0});actors.push_back(light);
    auto sound=actor("SoundTrigger0","Engine.SoundTrigger","Sound","Klaxon","None",{1000,300,0});sound["sound"]="Sound'Pkg.Grp.Horn'";actors.push_back(sound);
    auto custom=actor("SMagicEvent7","SBase.SMagicEvent","Magic event","Fireworks","None",{1000,400,0});custom["groups"]=Json::array();actors.push_back(custom);
    return actors;
}
Json Plan()
{
    Json plan=Stages::Empty();
    Json first=Stages::NewZone();
    first["name"]="Docks";
    first["objectives"]={"MyLevel.SObjective_1","MyLevel.SObjective_2"};
    first["required"]=1;
    first["actions"]={
        {{"kind","open"},{"target","MyLevel.Mover3"},{"delay","1.5"}},
        {{"kind","light"},{"target","MyLevel.STriggerLight0"}},
        {{"kind","sound"},{"sound","Sound'Pkg.Grp.Bell'"}},
        {{"kind","sound"},{"target","MyLevel.SoundTrigger0"},{"delay","2"}},
        {{"kind","announce"},{"title","Zone 2"},{"merc","The east wing is open."},{"spy","The east wing is open."},{"seconds","8"}},
        {{"kind","trigger"},{"target","MyLevel.SMagicEvent7"},{"delay","3"}},
        {{"kind","trigger"},{"tag","Custom"},{"untrigger",true}}};
    Json second=Stages::NewZone();
    second["objectives"]={"MyLevel.SObjective_3"};
    plan["zones"]={first,second};
    return plan;
}
const Json* Find(const Json& operations,const char* kind,const std::string& id)
{
    for(const auto& op:operations)
        if(op.at("op")==kind && (kind==std::string("create")?op.at("id")==id:op.at("actor").at("path")==id))return &op;
    return nullptr;
}
bool Has(const Json& operations,const char* kind,const std::string& id){return Find(operations,kind,id)!=nullptr;}
const Json* Op(const Json& operations,const char* kind,const std::string& id)
{
    auto op=Find(operations,kind,id);
    if(!op)throw std::runtime_error(std::string("missing ")+kind+" operation for "+id);
    return op;
}
// The paths an object-reference array names; new actors are created as
// MyLevel.<creation id>.
Json Paths(const Json& references)
{
    Json paths=Json::array();
    for(const auto& reference:references)
    {
        if(reference.is_object()){paths.push_back("MyLevel."+reference.at("$ref").get<std::string>());continue;}
        const auto text=reference.get<std::string>();
        paths.push_back(text.substr(text.find('\'')+1,text.rfind('\'')-text.find('\'')-1));
    }
    return paths;
}
// The map a batch leaves behind, as the inventory would read it back.
Json Applied(Json actors,const Json& operations)
{
    for(const auto& op:operations)
    {
        if(op.at("op")!="create")continue;
        const auto id=op.at("id").get<std::string>(),type=op.at("class").get<std::string>();
        const auto& p=op.at("properties");
        Json actor={{"path","MyLevel."+id},{"class",type},{"tag",id},{"event",p.value("Event",std::string("None"))},{"name",""},{"position",Vector{0,0,0}}};
        if(type=="SBase.SMission")
        {
            actor["kind"]="Mission";actor["objectives"]=Paths(p.at("Objectives"));
            actor["chained"]=p.at("bChained")=="True";actor["minimum"]=p.at("MinimumObjectives");actor["mode"]=p.at("GameMode");
            actor["name"]=Stages::Unquoted(p.at("ObjectiveName"));actor["brief"]=Stages::Unquoted(p.at("Description"));actor["briefDefend"]=Stages::Unquoted(p.at("DescriptionDEF"));
        }
        else if(type=="SBase.SMagicEvent"){actor["kind"]="Magic event";actor["groups"]=p.at("Groups");}
        else if(type=="Engine.SoundTrigger"){actor["kind"]="Sound";actor["sound"]=p.at("Sound");}
        else
        {
            actor["kind"]="Alarm";actor["title"]=Stages::Unquoted(p.at("AlarmName"));actor["merc"]=Stages::Unquoted(p.at("AlarmDescription"));
            actor["spy"]=Stages::Unquoted(p.at("AlarmDescriptionSpy"));actor["duration"]=p.at("Duration");
        }
        actors.push_back(actor);
    }
    for(const auto& op:operations)
    {
        if(op.at("op")!="update")continue;
        for(auto& actor:actors)
        {
            if(actor.at("path")!=op.at("actor").at("path"))continue;
            for(auto it=op.at("properties").begin();it!=op.at("properties").end();++it)
            {
                const auto key=it.key();
                if(key=="Objectives")actor["objectives"]=Paths(it.value());
                else if(key=="MinimumObjectives")actor["minimum"]=it.value();
                else if(key=="bChained")actor["chained"]=it.value()=="True";
                else if(key=="GameMode")actor["mode"]=it.value();
                else if(key=="ObjectiveName")actor["name"]=Stages::Unquoted(it.value());
                else if(key=="Description")actor["brief"]=Stages::Unquoted(it.value());
                else if(key=="DescriptionDEF")actor["briefDefend"]=Stages::Unquoted(it.value());
                else if(key=="Event")actor["event"]=it.value();
                else if(key=="Tag")actor["tag"]=it.value();
                else if(key=="Groups")actor["groups"]=it.value();
                else if(key=="Sound")actor["sound"]=it.value();
                else if(key=="InitialState")actor["state"]=it.value();
                else if(key=="bInitialyUsable")actor["usable"]=it.value()=="True";
                else if(key=="TriggerMethode")actor["method"]=it.value();
                else if(key=="AlarmName")actor["title"]=Stages::Unquoted(it.value());
            }
        }
    }
    return actors;
}
int main()
{
  try
  {
    // Generated Tags and what they belong to.
    Check(Stages::ZoneTag(2)=="Zone2Mission" && Stages::CompleteTag(3)=="Zone3_Complete" && Stages::SoundTag(1,2)=="Zone1_Sound2","tag naming incorrect");
    Check(Stages::ZoneOf("zone12mission","Mission")==12 && Stages::ZoneOf("Zone2_Complete","Mission")==0 && Stages::ZoneOf("Zone3_Sound7","_Sound")==3 &&
          Stages::ZoneOf("Zone3_Sound","Mission")==0 && Stages::ZoneOf("Zonex_Complete","_Complete")==0 && Stages::ZoneOf("Zone1_Complete2","_Complete")==0,"tag parsing incorrect");
    Check(Stages::LegacyOf("Stage2_Gate","Gate")==2 && Stages::LegacyOf("Zone2Mission","Gate")==0,"the earlier window's tags are still recognised");
    Check(Stages::Equivalent("0","0.000000") && Stages::Equivalent(Json{{"a","1.5"}},Json{{"a","1.50"}}) && !Stages::Equivalent("None","0") && !Stages::Equivalent(Json::array({"1"}),Json::array({"1","2"})),"numeric text comparison incorrect");

    // Validation.
    Stages::Validate(Plan());
    Reject([]{auto p=Plan();p["zones"][1]["objectives"]={"MyLevel.SObjective_1"};Stages::Validate(p);});
    Reject([]{auto p=Plan();p["zones"][0]["required"]=3;Stages::Validate(p);});
    Reject([]{auto p=Plan();p["zones"][0]["name"]="two\nlines";Stages::Validate(p);});
    Reject([]{auto p=Plan();p["zones"][0]["actions"][0]["delay"]="-1";Stages::Validate(p);});
    Reject([]{auto p=Plan();p["zones"][0]["actions"][2]["sound"]="Pkg.Grp.Bell";Stages::Validate(p);});
    Reject([]{auto p=Plan();p["zones"][0]["actions"][4]["seconds"]="0";Stages::Validate(p);});
    Reject([]{auto p=Plan();p["zones"][0]["actions"][4]["merc"]="two\nlines";Stages::Validate(p);});
    Reject([]{auto p=Plan();p["zones"][0]["actions"][6]["tag"]="has space";Stages::Validate(p);});
    Reject([]{auto p=Plan();p["zones"][0]["actions"].push_back({{"kind","dance"}});Stages::Validate(p);});
    Reject([]{auto p=Plan();p["format"]="scct.smagic-event";Stages::Validate(p);});
    // A plan from the counting-event version says so rather than half-applying.
    Reject([]{Json p={{"format","scct.stages"},{"version",1},{"lockLater",true},{"stages",Json::array()}};Stages::Validate(p);});

    // How many objectives a plan holds, how many end its zones, and how many
    // win the match.
    Check(Stages::Objectives(Plan())==3 && Stages::Thresholds(Plan())==2 && Stages::WinsAfter(Plan())==2,"the match total defaults to every zone's threshold added up");
    Check([]{auto p=Plan();p["required"]=1;return Stages::WinsAfter(p)==1;}(),"a plan can name a smaller total so the spies win before the last zone ends");
    Reject([]{auto p=Plan();p["required"]=4;Stages::Validate(p);});
    Reject([]{auto p=Plan();p["required"]=-1;Stages::Validate(p);});

    // A map with one flat mission carries no zones.
    Check(Stages::Read(Map()).at("zones").empty() && Stages::Read(Map()).at("mission")=="MyLevel.SMission0","a flat mission reads as no zones");

    // Building the change batch for a fresh map.
    auto build=Stages::Plan(Map(),{"smission0","sobjective_1","sobjective_2","sobjective_3","lamp","klaxon","fireworks"},Plan());
    const auto& ops=build.operations;
    Check(build.creates==5 && Has(ops,"create","Zone1Mission") && Has(ops,"create","Zone2Mission") && Has(ops,"create","Zone1_Complete") && Has(ops,"create","Zone1_Sound1") && Has(ops,"create","Zone1_Announce"),"a mission per zone, plus the first zone's event, sound and announcement");
    // Each zone's mission holds its objectives, all live at once, and ends on
    // the threshold the plan asks for.
    auto zone1=Op(ops,"create","Zone1Mission")->at("properties");
    Check(zone1.at("Objectives")==Json::array({"SBase.SObjective'MyLevel.SObjective_1'","SBase.SObjective'MyLevel.SObjective_2'"}),"the zone mission holds the zone's objectives");
    Check(zone1.at("MinimumObjectives")=="1" && zone1.at("bChained")=="False" && zone1.at("GameMode")=="GM_Undefined","one of the two objectives ends zone 1 and both are available at once");
    Check(zone1.at("ObjectiveName")=="\"Docks\"" && zone1.at("Description")=="\"Complete the objectives\"" && zone1.at("DescriptionDEF")=="\"Defend the objectives\"","the zone carries its name and both briefings");
    Check(zone1.at("Event")=="Zone1_Complete","the zone fires its completion event when it ends");
    auto zone2=Op(ops,"create","Zone2Mission")->at("properties");
    Check(zone2.at("MinimumObjectives")=="1" && zone2.at("ObjectiveName")=="\"Zone 2\"" && zone2.at("Event")=="None","a zone with no actions fires nothing and is named after its place");
    // The top mission runs the zones in order and needs all of them.
    auto top=Op(ops,"update","MyLevel.SMission0")->at("properties");
    Check(top.at("Objectives")==Json::array({{{"$ref","Zone1Mission"}},{{"$ref","Zone2Mission"}}}) && top.at("bChained")=="True","the top mission holds both zones in order and chains them");
    Check(top.at("MinimumObjectives")=="2","the spies win after the objectives the zones' own thresholds add up to, not the number of zones");
    Check(!top.contains("GameMode"),"a mission that is already GM_Multi is left alone");
    // Nothing is locked and no objective is rewired: the chain does that.
    Check(!Has(ops,"update","MyLevel.SObjective_1") && !Has(ops,"update","MyLevel.SFlag_1") && !Has(ops,"update","MyLevel.SComputerObjectiveTrigger_1"),"objectives and terminals are left as they are");
    // The completion event: the author's actions, in order.
    auto actions=Op(ops,"create","Zone1_Complete")->at("properties").at("Groups")[0].at("EventGroup");
    Check(actions.size()==7,"the seven actions of zone 1");
    Check(actions[0].at("Event")=="Mover_1" && actions[0].at("Delay")=="1.5" && Op(ops,"update","MyLevel.Mover3")->at("properties").at("Tag")=="Mover_1" && Op(ops,"update","MyLevel.Mover3")->at("properties").at("InitialState")=="TriggerToggle","a timed door gets a Tag, opens after its delay and stays open");
    Check(actions[1].at("Event")=="Lamp" && actions[2].at("Event")=="Zone1_Sound1" && actions[3].at("Event")=="Klaxon" && actions[3].at("Delay")=="2","lights and sounds are reached by Tag");
    Check(Op(ops,"create","Zone1_Sound1")->at("properties").at("Sound")=="Sound'Pkg.Grp.Bell'","a new sound trigger carries its sound");
    auto announce=Op(ops,"create","Zone1_Announce")->at("properties");
    Check(actions[4].at("Event")=="Zone1_Announce" && announce.at("AlarmName")=="\"Zone 2\"" && announce.at("AlarmDescriptionSpy")=="\"The east wing is open.\"" && announce.at("Duration")=="8" && announce.at("Events").empty(),"the announcement is an alarm with per-team text and a duration");
    Check(actions[5].at("Event")=="Fireworks" && actions[5].at("Type")=="EVT_Trigger" && actions[6].at("Event")=="Custom" && actions[6].at("Type")=="EVT_Untrigger","other actors are reached by Tag, untriggered on request");
    for(const auto& op:ops)if(op.at("op")=="update")Check(op.at("before").is_object() && op.at("before").empty(),"updates leave before empty for the caller");
    // Placement: near the zone's objectives, above the floor.
    auto at=Op(ops,"create","Zone1Mission")->at("properties").at("Location");
    Check(std::stod(at.at("X").get<std::string>())==200 && std::stod(at.at("Y").get<std::string>())==100 && std::stod(at.at("Z").get<std::string>())==96,"the zone mission sits above its objectives");

    // Rejections.
    Reject([]{auto p=Plan();p["zones"][1]["objectives"]=Json::array();Stages::Plan(Map(),{},p);});
    Reject([]{auto p=Plan();p["zones"][0]["objectives"][0]="MyLevel.Mover3";Stages::Plan(Map(),{},p);});
    Reject([]{auto p=Plan();p["zones"][0]["actions"][0]["target"]="MyLevel.STriggerLight0";Stages::Plan(Map(),{},p);});
    Reject([]{auto p=Plan();p["zones"][0]["actions"][5]["target"]="MyLevel.Nothing";Stages::Plan(Map(),{},p);});
    // Every objective belongs to a zone, and the map needs a mission to hold them.
    Reject([]{auto p=Plan();p["zones"][1]["objectives"]=Json::array({"MyLevel.SObjective_3"});p["zones"][0]["objectives"]=Json::array({"MyLevel.SObjective_1"});Stages::Plan(Map(),{},p);});
    Reject([]{Json actors=Json::array();for(const auto& a:Map())if(a.at("kind")!="Mission")actors.push_back(a);Stages::Plan(actors,{},Plan());});

    // Reading the map the batch leaves behind gives back the same plan.
    auto wired=Applied(Map(),ops);
    auto read=Stages::Read(wired);
    Check(read.at("zones").size()==2 && read.at("mission")=="MyLevel.SMission0","two zones read back under the map's mission");
    Check(read.at("zones")[0].at("name")=="Docks" && read.at("zones")[0].at("objectives")==Json::array({"MyLevel.SObjective_1","MyLevel.SObjective_2"}) && read.at("zones")[0].at("required")==1,"a zone reads back with its name, objectives and threshold");
    Check(read.at("zones")[0].at("mission")=="MyLevel.Zone1Mission" && read.at("zones")[1].at("required")==0,"each zone names its mission; needing every objective reads back as all");
    Check(Stages::ZoneOfObjective(wired,"MyLevel.SObjective_3")==2 && Stages::ZoneOfObjective(wired,"MyLevel.Mover3")==0,"an objective knows its zone");
    auto readActions=read.at("zones")[0].at("actions");
    Check(readActions.size()==7,"the seven actions read back");
    Check(readActions[0].at("kind")=="open" && readActions[0].at("target")=="MyLevel.Mover3" && readActions[0].at("hold")==true && Stages::Equivalent(readActions[0].at("delay"),"1.5"),"the door action reads back with its delay and hold");
    Check(readActions[1].at("kind")=="light" && readActions[2].at("kind")=="sound" && readActions[2].at("sound")=="Sound'Pkg.Grp.Bell'" && !readActions[2].contains("target") && readActions[3].at("kind")=="sound" && readActions[3].at("target")=="MyLevel.SoundTrigger0","sound actions distinguish the zone's own trigger from an existing one");
    Check(readActions[4].at("kind")=="announce" && readActions[4].at("title")=="Zone 2" && readActions[4].at("seconds")=="8","the announcement reads back as text");
    Check(readActions[5].at("kind")=="trigger" && readActions[5].at("target")=="MyLevel.SMagicEvent7" && readActions[5].at("untrigger")==false && readActions[6].at("kind")=="trigger" && readActions[6].at("tag")=="Custom" && readActions[6].at("untrigger")==true,"other targets read back by actor or by raw Tag");
    Check(Stages::Describe(readActions[0],wired)=="open Mover3, stays open after 1.5 s" && Stages::Describe(readActions[4],wired)=="announce \"Zone 2\" on both HUDs for 8 s","action descriptions");
    Check(read.at("required")==0,"a map whose total is its zones' own reads back as 0");
    // Applying the plan read back changes nothing.
    auto again=Stages::Plan(wired,{},read);
    Check(again.creates==0 && again.updates==0,"a map that matches its plan needs no changes");
    // Winning before the last zone ends is the author's to set and keep.
    auto early=read;early["required"]=1;
    auto rushed=Stages::Plan(wired,{},early);
    Check(rushed.updates==1 && Op(rushed.operations,"update","MyLevel.SMission0")->at("properties").at("MinimumObjectives")=="1","a smaller match total reaches the top mission");
    Json hurried=Applied(wired,rushed.operations);
    Check(Stages::Read(hurried).at("required")==1,"a total that is not the zones' own reads back as it is");

    // Editing an existing plan touches only what moved.
    auto everyone=read;everyone["zones"][0]["required"]=0;
    auto recounted=Stages::Plan(wired,{},everyone);
    Check(recounted.updates==2 && Op(recounted.operations,"update","MyLevel.Zone1Mission")->at("properties").at("MinimumObjectives")=="2","asking for every objective raises the zone's threshold");
    Check(Op(recounted.operations,"update","MyLevel.SMission0")->at("properties").at("MinimumObjectives")=="3","raising a zone's threshold raises what the spies need to win with it");
    auto moved=read;
    moved["zones"][0]["objectives"]=Json::array({"MyLevel.SObjective_1"});
    moved["zones"][0]["required"]=0;
    moved["zones"][1]["objectives"]=Json::array({"MyLevel.SObjective_2","MyLevel.SObjective_3"});
    auto shifted=Stages::Plan(wired,{},moved);
    Check(Op(shifted.operations,"update","MyLevel.Zone1Mission")->at("properties").at("Objectives")==Json::array({"SBase.SObjective'MyLevel.SObjective_1'"}) &&
          Op(shifted.operations,"update","MyLevel.Zone2Mission")->at("properties").at("Objectives").size()==2,"an objective moved between zones moves between their missions");
    // Reordering the zones keeps their missions and re-orders the top one.
    auto swapped=read;std::swap(swapped["zones"][0],swapped["zones"][1]);
    auto reordered=Stages::Plan(wired,{},swapped);
    Check(Op(reordered.operations,"update","MyLevel.SMission0")->at("properties").at("Objectives")==Json::array({"SBase.SMission'MyLevel.Zone2Mission'","SBase.SMission'MyLevel.Zone1Mission'"}),"moving a zone re-orders the top mission, not the zone missions");
    Check(!Has(reordered.operations,"create","Zone1Mission"),"a reordered zone keeps the mission it already had");
    // Dropping a zone leaves its mission behind for the author to delete.
    auto fewer=read;fewer["zones"].erase(1);fewer["zones"][0]["objectives"].push_back("MyLevel.SObjective_3");
    auto dropped=Stages::Plan(wired,{},fewer);
    Check(Op(dropped.operations,"update","MyLevel.SMission0")->at("properties").at("Objectives").size()==1,"the top mission holds only the zones that are left");
    // A zone whose actions are all removed keeps its event, emptied.
    auto silent=read;silent["zones"][0]["actions"]=Json::array();
    auto quiet=Stages::Plan(wired,{},silent);
    Check(Op(quiet.operations,"update","MyLevel.Zone1_Complete")->at("properties").at("Groups")[0].at("EventGroup").empty(),"a zone that fires nothing empties its event");

    // A door whose Tag other actors share gets one of its own.
    Json crowded=Map();crowded[7]["tag"]="Mover";crowded[7]["shared"]=true;
    auto own=Stages::Plan(crowded,{"mover"},Plan());
    Check(Op(own.operations,"update","MyLevel.Mover3")->at("properties").at("Tag")=="Mover_1","a shared Tag is replaced so only this door opens");
    // A Tag of ours on the wrong kind of actor is refused.
    Json clash=Map();clash[7]["tag"]="Zone1_Complete";
    Reject([&]{Stages::Plan(clash,{},Plan());});
    // A zone mission the author tagged and wired by hand is reused as it is.
    Json byHand=Map();
    byHand[0]["objectives"]=Json::array({"MyLevel.SMission1"});
    auto hand=byHand[0];hand["path"]="MyLevel.SMission1";hand["tag"]="Zone1Mission";hand["event"]="BeginZone2";hand["name"]="Docks";
    hand["objectives"]=Json::array({"MyLevel.SObjective_1","MyLevel.SObjective_2"});hand["minimum"]="1";hand["mode"]="GM_Undefined";
    byHand.push_back(hand);
    byHand.push_back({{"path","MyLevel.SMagicEvent9"},{"class","SBase.SMagicEvent"},{"kind","Magic event"},{"tag","BeginZone2"},{"event","None"},{"name",""},{"position",Vector{0,0,0}},{"groups",Json::array()}});
    auto reused=Stages::Plan(byHand,{"zone1mission","beginzone2"},Plan());
    Check(!Has(reused.operations,"create","Zone1Mission") && !Has(reused.operations,"create","Zone1_Complete"),"a mission already tagged Zone1Mission and the event it fires are reused");
    Check(Op(reused.operations,"update","MyLevel.SMagicEvent9")->at("properties").at("Groups")[0].at("EventGroup").size()==7,"the author's own completion event carries the zone's actions");

    // A map still carrying the earlier counting wiring is cleaned up.
    Json old=Map();
    for(auto& actor:old)
    {
        if(actor.at("path")=="MyLevel.SObjective_1")actor["event"]="Stage1_Gate";
        if(actor.at("path")=="MyLevel.SFlag_1"){actor["usable"]=false;actor["method"]="TriggerControl";}
    }
    old.push_back({{"path","MyLevel.Stage1_Gate"},{"class","SBase.SMagicEvent"},{"kind","Magic event"},{"tag","Stage1_Gate"},{"event","None"},{"name",""},{"position",Vector{0,0,0}},{"groups",Json::array()}});
    auto cleaned=Stages::Plan(old,{},Plan());
    Check(Op(cleaned.operations,"update","MyLevel.SObjective_1")->at("properties").at("Event")=="None","an objective stops feeding the old counting event");
    Check(Op(cleaned.operations,"update","MyLevel.SFlag_1")->at("properties").at("bInitialyUsable")=="True","a terminal the old wiring locked is usable again");
    Check([&]{for(const auto& note:cleaned.notes)if(note.find("Stage1_Gate is left over")!=std::string::npos)return true;return false;}(),"the leftover counting event is named");

    // Design-check issues.
    Check(Stages::Issues(Map()).empty(),"a map without zones raises no zone issues");
    // The raw Tag action reaches nothing until an actor carries that Tag.
    Check(Stages::Issues(wired).size()==1 && Stages::Issues(wired)[0].text.find("fires Event Custom but nothing has that Tag")!=std::string::npos,"a raw Tag nothing carries is reported");
    wired.push_back({{"path","MyLevel.SSpawnableEmitter0"},{"class","SBase.SSpawnableEmitter"},{"kind","Other"},{"tag","Custom"},{"event","None"},{"name",""},{"position",Vector{0,0,0}}});
    if(!Stages::Issues(wired).empty())throw std::runtime_error("a consistent zoned map raised: "+Stages::Issues(wired)[0].text);
    Json broken=wired;
    for(auto& actor:broken)
    {
        if(actor.at("path")=="MyLevel.SMission0")actor["chained"]=false;
        if(actor.at("path")=="MyLevel.Zone1Mission"){actor["chained"]=true;actor["mode"]="GM_Multi";actor["minimum"]="4";}
        if(actor.at("path")=="MyLevel.Zone2Mission")actor["event"]="Nowhere";
        if(actor.at("path")=="MyLevel.SMission0")actor["minimum"]="9";
    }
    auto issues=Stages::Issues(broken);
    auto has=[&](const char* fragment){for(const auto& issue:issues)if(issue.text.find(fragment)!=std::string::npos)return true;return false;};
    Check(has("holds 2 zones but is not chained") && has("Zone 1 is chained") && has("Zone 1 has GameMode GM_Multi") &&
          has("Zone 1 needs 4 completions but holds 2 objectives") && has("Zone 2 fires Event Nowhere"),"issues name the mission settings that break a zoned map");
    Check(has("needs 9 objectives to win, but finishing every zone completes only 3"),"a match total the zones cannot reach is an error");
    Json loose=wired;
    for(auto& actor:loose)if(actor.at("path")=="MyLevel.SMission0")actor["objectives"].push_back("MyLevel.SObjective_1");
    Check([&]{for(const auto& issue:Stages::Issues(loose))if(issue.text.find("hangs straight off the top mission")!=std::string::npos)return true;return false;}(),"an objective beside the zones is reported");
    Json orphan=wired;
    for(auto& actor:orphan)if(actor.at("path")=="MyLevel.Zone2Mission")actor["objectives"]=Json::array();
    Check([&]{for(const auto& issue:Stages::Issues(orphan))if(issue.text.find("has no objectives")!=std::string::npos)return true;return false;}(),"an empty zone can never be completed");
    Check([&]{for(const auto& issue:Stages::Issues(orphan))if(issue.text.find("SObjective_3 (objective) is in no mission")!=std::string::npos)return true;return false;}(),"an objective in no mission is reported");
  }
  catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
  std::cout<<"StageModelTests passed\n";
  return 0;
}
