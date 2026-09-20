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
// A small Versus map: a mission with three objectives (two terminals, one
// flag), a door, a switchable light and a sound trigger, none of it wired.
Json Map()
{
    auto actor=[](const char* path,const char* type,const char* kind,const char* tag,const char* event,Vector at)
    {
        return Json{{"path",std::string("MyLevel.")+path},{"class",type},{"kind",kind},{"tag",tag},{"event",event},{"name",""},{"position",at}};
    };
    Json actors=Json::array();
    auto mission=actor("SMission0","SBase.SMission","Mission","SMission0","None",{0,0,0});
    mission["objectives"]={"MyLevel.SObjective_1","MyLevel.SObjective_2","MyLevel.SObjective_3"};
    actors.push_back(mission);
    auto o1=actor("SObjective_1","SBase.SObjective","Objective","SObjective_1","None",{100,100,0});o1["name"]="Hack";o1["triggers"]={"MyLevel.SComputerObjectiveTrigger_1"};
    auto o2=actor("SObjective_2","SBase.SObjective","Objective","SObjective_2","OldEvent",{300,100,0});o2["name"]="Bomb";o2["triggers"]={"MyLevel.SBombTargetObjectiveTrigger_1"};
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
    Json first=Stages::NewStage();
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
    Json second=Stages::NewStage();
    second["objectives"]={"MyLevel.SObjective_3"};
    plan["stages"]={first,second};
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
int main()
{
  try
  {
    // Generated Tags and what they belong to.
    Check(Stages::GateTag(2)=="Stage2_Gate" && Stages::CompleteTag(3)=="Stage3_Complete" && Stages::SoundTag(1,2)=="Stage1_Sound2","tag naming incorrect");
    Check(Stages::StageOf("stage12_gate","Gate")==12 && Stages::StageOf("Stage2_Complete","Gate")==0 && Stages::StageOf("Stage3_Sound7","Sound")==3 && Stages::StageOf("Stage3_Sound","Gate")==0 && Stages::StageOf("Stagex_Gate","Gate")==0 && Stages::StageOf("Stage1_Gate2","Gate")==0,"tag parsing incorrect");
    Check(Stages::Equivalent("0","0.000000") && Stages::Equivalent(Json{{"a","1.5"}},Json{{"a","1.50"}}) && !Stages::Equivalent("None","0") && !Stages::Equivalent(Json::array({"1"}),Json::array({"1","2"})),"numeric text comparison incorrect");

    // Validation.
    Stages::Validate(Plan());
    Reject([]{auto p=Plan();p["stages"][1]["objectives"]={"MyLevel.SObjective_1"};Stages::Validate(p);});
    Reject([]{auto p=Plan();p["stages"][0]["required"]=3;Stages::Validate(p);});
    Reject([]{auto p=Plan();p["stages"][0]["actions"][0]["delay"]="-1";Stages::Validate(p);});
    Reject([]{auto p=Plan();p["stages"][0]["actions"][2]["sound"]="Pkg.Grp.Bell";Stages::Validate(p);});
    Reject([]{auto p=Plan();p["stages"][0]["actions"][4]["seconds"]="0";Stages::Validate(p);});
    Reject([]{auto p=Plan();p["stages"][0]["actions"][4]["merc"]="two\nlines";Stages::Validate(p);});
    Reject([]{auto p=Plan();p["stages"][0]["actions"][6]["tag"]="has space";Stages::Validate(p);});
    Reject([]{auto p=Plan();p["stages"][0]["actions"].push_back({{"kind","dance"}});Stages::Validate(p);});
    Reject([]{auto p=Plan();p["format"]="scct.smagic-event";Stages::Validate(p);});

    // Building the change batch for a fresh map.
    auto build=Stages::Plan(Map(),{"smission0","sobjective_1","sobjective_2","sobjective_3","lamp","klaxon","fireworks"},Plan());
    const auto& ops=build.operations;
    Check(build.creates==6 && Has(ops,"create","Stage1_Gate") && Has(ops,"create","Stage1_Complete") && Has(ops,"create","Stage2_Gate") && Has(ops,"create","Stage2_Complete") && Has(ops,"create","Stage1_Sound1") && Has(ops,"create","Stage1_Announce"),"one gate and one completion per stage, plus the stage's own sound and announcement");
    // Objectives feed their gate; an existing Event is replaced and noted.
    Check(Op(ops,"update","MyLevel.SObjective_1")->at("properties").at("Event")=="Stage1_Gate" && Op(ops,"update","MyLevel.SObjective_2")->at("properties").at("Event")=="Stage1_Gate" && Op(ops,"update","MyLevel.SObjective_3")->at("properties").at("Event")=="Stage2_Gate","objectives feed their stage gate");
    bool noted=false;for(const auto& note:build.notes)if(note.find("OldEvent replaced by Stage1_Gate")!=std::string::npos)noted=true;
    Check(noted,"a replaced objective Event is reported");
    // The gate counts one completion (1 of 2) and fires the completion.
    auto gate=Op(ops,"create","Stage1_Gate")->at("properties").at("Groups");
    Check(gate.size()==1 && gate[0].at("Sequence")=="True" && gate[0].at("Repeat")=="1" && gate[0].at("EventGroup").size()==1 && gate[0].at("EventGroup")[0].at("Event")=="Stage1_Complete","a 1-of-2 gate is a one-step sequence");
    auto gate2=Op(ops,"create","Stage2_Gate")->at("properties").at("Groups");
    Check(gate2[0].at("EventGroup").size()==1 && gate2[0].at("EventGroup")[0].at("Event")=="Stage2_Complete","an all-of-one gate fires on its only completion");
    // The completion: the flag of stage 2 unlocks first, then the actions.
    auto actions=Op(ops,"create","Stage1_Complete")->at("properties").at("Groups")[0].at("EventGroup");
    Check(actions.size()==8,"unlock plus seven actions");
    Check(actions[0].at("Event")=="Flag_1" && Op(ops,"update","MyLevel.SFlag_1")->at("properties").at("Tag")=="Flag_1" && Op(ops,"update","MyLevel.SFlag_1")->at("properties").at("bInitialyUsable")=="False" && Op(ops,"update","MyLevel.SFlag_1")->at("properties").at("TriggerMethode")=="TriggerControl","the next stage's terminal gets a Tag, starts locked and is unlocked first");
    Check(!Has(ops,"update","MyLevel.SComputerObjectiveTrigger_1") && !Has(ops,"update","MyLevel.SBombTargetObjectiveTrigger_1"),"stage 1 terminals are left usable");
    Check(actions[1].at("Event")=="Mover_1" && actions[1].at("Delay")=="1.5" && Op(ops,"update","MyLevel.Mover3")->at("properties").at("Tag")=="Mover_1" && Op(ops,"update","MyLevel.Mover3")->at("properties").at("InitialState")=="TriggerToggle","a timed door gets a Tag, opens after its delay and stays open");
    Check(actions[2].at("Event")=="Lamp" && actions[3].at("Event")=="Stage1_Sound1" && actions[4].at("Event")=="Klaxon" && actions[4].at("Delay")=="2","lights and sounds are reached by Tag");
    Check(Op(ops,"create","Stage1_Sound1")->at("properties").at("Sound")=="Sound'Pkg.Grp.Bell'","a new sound trigger carries its sound");
    auto announce=Op(ops,"create","Stage1_Announce")->at("properties");
    Check(actions[5].at("Event")=="Stage1_Announce" && announce.at("AlarmName")=="\"Zone 2\"" && announce.at("AlarmDescriptionSpy")=="\"The east wing is open.\"" && announce.at("Duration")=="8" && announce.at("Events").empty(),"the announcement is an alarm with per-team text and a duration");
    Check(actions[6].at("Event")=="Fireworks" && actions[6].at("Type")=="EVT_Trigger" && actions[7].at("Event")=="Custom" && actions[7].at("Type")=="EVT_Untrigger","other actors are reached by Tag, untriggered on request");
    for(const auto& op:ops)if(op.at("op")=="update")Check(op.at("before").is_object() && op.at("before").empty(),"updates leave before empty for the caller");
    // Stage 2's completion is the last stage: its list is empty and noted.
    Check(Op(ops,"create","Stage2_Complete")->at("properties").at("Groups")[0].at("EventGroup").empty(),"the last stage completing triggers nothing");
    // Placement: near the stage's objectives, above the floor.
    auto at=Op(ops,"create","Stage1_Gate")->at("properties").at("Location");
    Check(std::stod(at.at("X").get<std::string>())==152 && std::stod(at.at("Y").get<std::string>())==100 && std::stod(at.at("Z").get<std::string>())==96,"the gate sits beside the objectives' centre");
    // Rejections.
    Reject([]{auto p=Plan();p["stages"][1]["objectives"]=Json::array();Stages::Plan(Map(),{},p);});
    Reject([]{auto p=Plan();p["stages"][0]["objectives"][0]="MyLevel.Mover3";Stages::Plan(Map(),{},p);});
    Reject([]{auto p=Plan();p["stages"][0]["actions"][0]["target"]="MyLevel.STriggerLight0";Stages::Plan(Map(),{},p);});
    Reject([]{auto p=Plan();p["stages"][0]["actions"][5]["target"]="MyLevel.Nothing";Stages::Plan(Map(),{},p);});

    // Reading a map that already carries stages round-trips the plan.
    Json wired=Map();
    auto set=[&](const std::string& path,const char* key,const Json& value){for(auto& actor:wired)if(actor.at("path")==path)actor[key]=value;};
    for(const auto& op:ops)if(op.at("op")=="update")for(auto it=op.at("properties").begin();it!=op.at("properties").end();++it)
    {
        const char* key=it.key()=="Event"?"event":it.key()=="Tag"?"tag":it.key()=="Groups"?"groups":it.key()=="bInitialyUsable"?"usable":it.key()=="TriggerMethode"?"method":it.key()=="InitialState"?"state":nullptr;
        if(key)set(op.at("actor").at("path"),key,it.key()=="bInitialyUsable"?Json(it.value()=="True"):it.value());
    }
    for(const auto& op:ops)if(op.at("op")=="create")
    {
        const auto id=op.at("id").get<std::string>(),type=op.at("class").get<std::string>();
        Json actor={{"path","MyLevel."+id},{"class",type},{"tag",id},{"event","None"},{"name",""},{"position",Vector{0,0,0}}};
        if(type=="SBase.SMagicEvent"){actor["kind"]="Magic event";actor["groups"]=op.at("properties").at("Groups");}
        else if(type=="Engine.SoundTrigger"){actor["kind"]="Sound";actor["sound"]=op.at("properties").at("Sound");}
        else {actor["kind"]="Alarm";actor["title"]=Stages::Unquoted(op.at("properties").at("AlarmName"));actor["merc"]=Stages::Unquoted(op.at("properties").at("AlarmDescription"));actor["spy"]=Stages::Unquoted(op.at("properties").at("AlarmDescriptionSpy"));actor["duration"]=op.at("properties").at("Duration");}
        wired.push_back(actor);
    }
    auto read=Stages::Read(wired);
    Check(read.at("stages").size()==2 && read.at("lockLater")==true,"two stages read back, with locking");
    Check(read.at("stages")[0].at("objectives")==Json::array({"MyLevel.SObjective_1","MyLevel.SObjective_2"}) && read.at("stages")[0].at("required")==1 && read.at("stages")[1].at("objectives")==Json::array({"MyLevel.SObjective_3"}) && read.at("stages")[1].at("required")==0,"objectives and thresholds read back");
    auto readActions=read.at("stages")[0].at("actions");
    Check(readActions.size()==7,"the unlock entry is implicit; the seven actions read back");
    Check(readActions[0].at("kind")=="open" && readActions[0].at("target")=="MyLevel.Mover3" && readActions[0].at("hold")==true && Stages::Equivalent(readActions[0].at("delay"),"1.5"),"the door action reads back with its delay and hold");
    Check(readActions[1].at("kind")=="light" && readActions[2].at("kind")=="sound" && readActions[2].at("sound")=="Sound'Pkg.Grp.Bell'" && !readActions[2].contains("target") && readActions[3].at("kind")=="sound" && readActions[3].at("target")=="MyLevel.SoundTrigger0","sound actions distinguish the stage's own trigger from an existing one");
    Check(readActions[4].at("kind")=="announce" && readActions[4].at("title")=="Zone 2" && readActions[4].at("seconds")=="8","the announcement reads back as text");
    Check(readActions[5].at("kind")=="trigger" && readActions[5].at("target")=="MyLevel.SMagicEvent7" && readActions[5].at("untrigger")==false && readActions[6].at("kind")=="trigger" && readActions[6].at("tag")=="Custom" && readActions[6].at("untrigger")==true,"other targets read back by actor or by raw Tag");
    Check(Stages::Describe(readActions[0],wired)=="open Mover3, stays open after 1.5 s" && Stages::Describe(readActions[4],wired)=="announce \"Zone 2\" on both HUDs for 8 s","action descriptions");
    // Applying the plan read back changes nothing.
    auto again=Stages::Plan(wired,{},read);
    Check(again.creates==0 && again.updates==0,"a map that matches its plan needs no changes");
    // Moving an objective out of every stage releases it and retires its Event.
    auto shrunk=read;shrunk["stages"][1]["objectives"]=Json::array({"MyLevel.SObjective_3"});shrunk["stages"][0]["objectives"]=Json::array({"MyLevel.SObjective_1"});shrunk["stages"][0]["required"]=0;
    auto released=Stages::Plan(wired,{},shrunk);
    Check(Op(released.operations,"update","MyLevel.SObjective_2")->at("properties").at("Event")=="None","an objective removed from its stage stops feeding the gate");
    Check(!Has(released.operations,"update","MyLevel.Stage1_Gate"),"a gate that still needs one completion is left alone");
    auto everyone=read;everyone["stages"][0]["required"]=0;
    auto recounted=Stages::Plan(wired,{},everyone);
    Check(recounted.updates==1 && Op(recounted.operations,"update","MyLevel.Stage1_Gate")->at("properties").at("Groups")[0].at("EventGroup").size()==2,"asking for every objective recounts the gate to two steps");
    // Turning locking off releases the later terminal.
    auto unlocked=read;unlocked["lockLater"]=false;
    auto free=Stages::Plan(wired,{},unlocked);
    Check(Op(free.operations,"update","MyLevel.SFlag_1")->at("properties").at("bInitialyUsable")=="True","without locking, a locked terminal becomes usable again");
    Check(Op(free.operations,"update","MyLevel.Stage1_Complete")->at("properties").at("Groups")[0].at("EventGroup")[0].at("Event")=="Mover_1","without locking, the completion holds only the author's actions");
    // Dropping the last stage leaves its events behind and says so.
    auto fewer=read;fewer["stages"].erase(1);
    auto dropped=Stages::Plan(wired,{},fewer);
    bool stale=false;for(const auto& note:dropped.notes)if(note.find("Stage2_Gate belongs to stage 2, which no longer exists")!=std::string::npos)stale=true;
    Check(stale && Op(dropped.operations,"update","MyLevel.SObjective_3")->at("properties").at("Event")=="None","stale stage events are reported and their objectives released");
    // A door whose Tag other actors share gets one of its own.
    Json crowded=Map();crowded[7]["tag"]="Mover";crowded[7]["shared"]=true;
    auto own=Stages::Plan(crowded,{"mover"},Plan());
    Check(Op(own.operations,"update","MyLevel.Mover3")->at("properties").at("Tag")=="Mover_1","a shared Tag is replaced so only this door opens");
    // A Tag of ours on the wrong kind of actor is refused.
    Json clash=Map();clash[7]["tag"]="Stage1_Gate";
    Reject([&]{Stages::Plan(clash,{},Plan());});

    // Design-check issues.
    Check(Stages::Issues(Map()).empty(),"a map without stages raises no stage issues");
    // The raw Tag action reaches nothing until an actor carries that Tag.
    Check(Stages::Issues(wired).size()==1 && Stages::Issues(wired)[0].text.find("fires Event Custom but nothing has that Tag")!=std::string::npos,"a raw Tag nothing carries is reported");
    wired.push_back({{"path","MyLevel.SSpawnableEmitter0"},{"class","SBase.SSpawnableEmitter"},{"kind","Other"},{"tag","Custom"},{"event","None"},{"name",""},{"position",Vector{0,0,0}}});
    if(!Stages::Issues(wired).empty())throw std::runtime_error("a consistent staged map raised: "+Stages::Issues(wired)[0].text);
    Json broken=wired;
    for(auto& actor:broken)
    {
        if(actor.at("path")=="MyLevel.SObjective_1")actor["event"]="None";
        if(actor.at("path")=="MyLevel.SFlag_1")actor["usable"]=true;
        if(actor.at("path")=="MyLevel.Stage1_Complete")actor["groups"][0]["EventGroup"].push_back({{"Delay","0"},{"Event","Ghost"},{"Type","EVT_Trigger"},{"ValidOn","EVT_Trigger"}});
    }
    auto issues=Stages::Issues(broken);
    auto has=[&](const char* fragment){for(const auto& issue:issues)if(issue.text.find(fragment)!=std::string::npos)return true;return false;};
    Check(has("SObjective_1 (objective) is in no stage") && has("fires Event Ghost but nothing has that Tag") && has("SFlag_1 is usable from the start although its stage begins later"),"issues name unstaged objectives, dangling events and unlocked terminals");
    for(auto& actor:broken)if(actor.at("path")=="MyLevel.SObjective_2")actor["event"]="None";
    Check([&]{for(const auto& issue:Stages::Issues(broken))if(issue.text.find("no objective's Event feeds Stage1_Gate")!=std::string::npos)return true;return false;}(),"a gate nothing feeds is an error");
    Json gapped=wired;for(auto& actor:gapped)if(actor.at("path")=="MyLevel.Stage1_Gate")actor["tag"]="Stage1_Gate_old";
    Check([&]{for(const auto& issue:Stages::Issues(gapped))if(issue.text.find("Stage 1 has no Stage1_Gate event")!=std::string::npos)return true;return false;}(),"a missing gate is reported");
  }
  catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
  std::cout<<"StageModelTests passed\n";
  return 0;
}
