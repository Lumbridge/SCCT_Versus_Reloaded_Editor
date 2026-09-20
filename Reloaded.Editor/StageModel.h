#pragma once
#include "SecurityModel.h"
#include "MagicEventModel.h"
#include <map>
#include <set>

// Stages of a Versus match: a stage is a set of objectives; when enough of
// them are complete the stage ends and things happen (doors open, lights
// switch, sounds play, both HUDs get a message, other actors are triggered)
// and the next stage's objective terminals unlock. The map carries all of it
// as ordinary actors wired through Tag and Event, so the game needs nothing
// new and the editor's own tools still show every link:
//
//   objective.Event = Stage<N>_Gate  --->  Stage<N>_Gate (SMagicEvent)
//     a Sequence group with one entry per completion needed; the last entry
//     fires Stage<N>_Complete, so it counts "K of N" with no counter actor.
//   Stage<N>_Complete (SMagicEvent) holds the stage's actions, plus one
//     Trigger per terminal of stage N+1, which start locked (bInitialyUsable
//     False, TriggerMethode TriggerControl) and become usable when triggered.
//
// A plan is a small JSON document; Read derives it from a map's actors and
// Plan turns it back into a scct.map-changes batch, so the map can be
// re-read and re-applied at any time. Engine-independent.
namespace Workflow::Stages
{
inline std::string GateTag(int n){return "Stage"+std::to_string(n)+"_Gate";}
inline std::string CompleteTag(int n){return "Stage"+std::to_string(n)+"_Complete";}
inline std::string AnnounceTag(int n){return "Stage"+std::to_string(n)+"_Announce";}
inline std::string SoundTag(int n,int i){return "Stage"+std::to_string(n)+"_Sound"+std::to_string(i);}
// The stage number of a generated Tag such as Stage2_Gate, or 0 when the Tag
// is not one of ours. Sound tags carry an index after the suffix.
inline int StageOf(const std::string& tag,const std::string& suffix)
{
    const auto folded=Fold(tag);
    const std::string prefix="stage";
    if(folded.size()<=prefix.size() || folded.compare(0,prefix.size(),prefix)!=0)return 0;
    size_t i=prefix.size();const size_t start=i;
    while(i<folded.size() && std::isdigit(static_cast<unsigned char>(folded[i])))++i;
    if(i==start || i-start>6)return 0;
    const auto rest=folded.substr(i),wanted="_"+Fold(suffix);
    if(rest.compare(0,wanted.size(),wanted)!=0)return 0;
    const auto index=rest.substr(wanted.size());
    if(!index.empty() && (suffix!="Sound" || index.find_first_not_of("0123456789")!=std::string::npos))return 0;
    return std::stoi(folded.substr(start,i-start));
}
inline bool NoneTag(const std::string& tag){return tag.empty() || Fold(tag)=="none";}
inline std::string ShortName(const std::string& path){return path.substr(path.find_last_of('.')+1);}
inline Json Empty(){return {{"format","scct.stages"},{"version",1},{"lockLater",true},{"stages",Json::array()}};}
inline Json NewStage(){return {{"objectives",Json::array()},{"required",0},{"actions",Json::array()}};}
inline const Json* ByPath(const Json& actors,const std::string& path)
{
    for(const auto& actor:actors)if(actor.value("path",std::string())==path)return &actor;
    return nullptr;
}
inline const Json* ByTag(const Json& actors,const std::string& tag)
{
    if(NoneTag(tag))return nullptr;
    for(const auto& actor:actors)if(Fold(actor.value("tag",std::string()))==Fold(tag))return &actor;
    return nullptr;
}
// Property text compares as numbers where both sides are numbers, so "0" and
// "0.000000" read back from the map count as the same value.
inline bool Equivalent(const Json& a,const Json& b)
{
    if(a.is_string() && b.is_string())
    {
        if(a==b)return true;
        try{size_t x=0,y=0;const auto s=a.get<std::string>(),t=b.get<std::string>();const double u=std::stod(s,&x),v=std::stod(t,&y);return x==s.size() && y==t.size() && u==v;}
        catch(const std::exception&){return false;}
    }
    if(a.is_array() && b.is_array())
    {
        if(a.size()!=b.size())return false;
        for(size_t i=0;i<a.size();++i)if(!Equivalent(a[i],b[i]))return false;
        return true;
    }
    if(a.is_object() && b.is_object())
    {
        if(a.size()!=b.size())return false;
        for(auto it=a.begin();it!=a.end();++it)if(!b.contains(it.key()) || !Equivalent(it.value(),b.at(it.key())))return false;
        return true;
    }
    return a==b;
}
inline std::string Quoted(const std::string& text){return Json(text).dump();}
inline std::string Unquoted(const std::string& text)
{
    auto decoded=Json::parse(text,nullptr,false);
    return decoded.is_string()?decoded.get<std::string>():text;
}
inline void CheckText(const std::string& text,const char* what)
{
    if(text.size()>200 || text.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error(std::string(what)+" uses up to 200 characters on one line.");
}
// A plan document: format, lockLater and stages, each with objectives (actor
// paths), required (0 means all) and actions.
inline void Validate(const Json& plan)
{
    if(!plan.is_object() || plan.value("format",std::string())!="scct.stages" || plan.value("version",0)!=1)
        throw std::runtime_error("This is not a stages file (scct.stages, version 1).");
    if(!plan.contains("stages") || !plan.at("stages").is_array() || plan.at("stages").size()>64)throw std::runtime_error("A plan lists up to 64 stages.");
    if(plan.contains("lockLater") && !plan.at("lockLater").is_boolean())throw std::runtime_error("lockLater must be true or false.");
    std::set<std::string> seen;
    int n=0;
    for(const auto& stage:plan.at("stages"))
    {
        ++n;
        const auto label="Stage "+std::to_string(n);
        if(!stage.is_object() || !stage.contains("objectives") || !stage.at("objectives").is_array() || !stage.contains("actions") || !stage.at("actions").is_array())
            throw std::runtime_error(label+" needs objectives and actions lists.");
        for(const auto& objective:stage.at("objectives"))
        {
            if(!objective.is_string() || objective.get<std::string>().empty())throw std::runtime_error(label+" lists an objective without a path.");
            if(!seen.insert(Fold(objective.get<std::string>())).second)throw std::runtime_error(ShortName(objective.get<std::string>())+" is in two stages. An objective belongs to one stage.");
        }
        const int required=stage.value("required",0);
        if(required<0 || required>static_cast<int>(stage.at("objectives").size()))
            throw std::runtime_error(label+" needs "+std::to_string(required)+" completions but has "+std::to_string(stage.at("objectives").size())+" objectives.");
        for(const auto& action:stage.at("actions"))
        {
            if(!action.is_object())throw std::runtime_error(label+" has an action that is not an object.");
            const auto kind=action.value("kind",std::string());
            if(action.contains("delay"))Magic::Seconds(Magic::Number(action.at("delay").get<std::string>()));
            if(kind=="open" || kind=="light")
            {
                if(!action.contains("target") || !action.at("target").is_string())throw std::runtime_error(label+": an "+kind+" action needs a target actor.");
            }
            else if(kind=="sound")
            {
                const bool asset=action.contains("sound") && action.at("sound").is_string();
                if(!asset && !(action.contains("target") && action.at("target").is_string()))throw std::runtime_error(label+": a sound action needs a sound asset or a sound trigger to fire.");
                if(asset)
                {
                    const auto ref=action.at("sound").get<std::string>();
                    if(ref.size()<8 || ref.compare(0,6,"Sound'")!=0 || ref.back()!='\'' || ref.find('\'',6)!=ref.size()-1)throw std::runtime_error(label+": name the sound as Sound'Package.Group.Name'.");
                }
            }
            else if(kind=="announce")
            {
                for(const char* field:{"title","merc","spy"})CheckText(action.value(field,std::string()),field);
                const double seconds=Magic::Number(action.value("seconds",std::string("8")));
                if(seconds<1 || seconds>3600 || std::floor(seconds)!=seconds)throw std::runtime_error(label+": an announcement shows for a whole number of seconds from 1 to 3600.");
            }
            else if(kind=="trigger")
            {
                const bool byPath=action.contains("target") && action.at("target").is_string(),byTag=action.contains("tag") && action.at("tag").is_string();
                if(!byPath && !byTag)throw std::runtime_error(label+": a trigger action needs a target actor or a Tag.");
                if(byTag)Magic::Validate({{"kind","NameProperty"}},action.at("tag"));
            }
            else throw std::runtime_error(label+": unknown action kind '"+kind+"'. Use open, light, sound, announce or trigger.");
        }
    }
}
inline bool Untriggers(const std::string& type){const auto t=Fold(type);return t=="evt_untrigger" || t=="evt_untriggertrigger";}
// Reads the stages a map already carries: every Stage<N>_Gate names a stage,
// the objectives whose Event feeds it belong to it, the gate's entry count is
// the completions it needs, and Stage<N>_Complete's actions are listed by what
// each target is.
inline Json Read(const Json& actors)
{
    Json plan=Empty();
    int last=0;
    for(const auto& actor:actors)
        if(actor.value("kind",std::string())=="Magic event")
            last=std::max({last,StageOf(actor.value("tag",std::string()),"Gate"),StageOf(actor.value("tag",std::string()),"Complete")});
    bool unlocks=false;
    for(int n=1;n<=last;++n)
    {
        Json stage=NewStage();
        for(const auto& actor:actors)
            if(actor.value("kind",std::string())=="Objective" && Fold(actor.value("event",std::string()))==Fold(GateTag(n)))stage["objectives"].push_back(actor.at("path"));
        if(const auto* gate=ByTag(actors,GateTag(n)))
        {
            const auto groups=gate->value("groups",Json::array());
            if(!groups.empty() && groups[0].is_object() && groups[0].contains("EventGroup"))
            {
                const int needed=static_cast<int>(groups[0].at("EventGroup").size());
                if(needed>0 && needed!=static_cast<int>(stage["objectives"].size()))stage["required"]=needed;
            }
        }
        if(const auto* complete=ByTag(actors,CompleteTag(n)))
        {
            const auto groups=complete->value("groups",Json::array());
            if(!groups.empty() && groups[0].is_object() && groups[0].contains("EventGroup"))
                for(const auto& entry:groups[0].at("EventGroup"))
                {
                    const auto tag=entry.value("Event",std::string("None"));
                    if(NoneTag(tag))continue;
                    Json action={{"delay",entry.value("Delay",std::string("0"))}};
                    const auto* target=ByTag(actors,tag);
                    if(!target){action["kind"]="trigger";action["tag"]=tag;action["untrigger"]=Untriggers(entry.value("Type",std::string()));stage["actions"].push_back(action);continue;}
                    const auto kind=target->value("kind",std::string());
                    if(kind=="Objective trigger"){unlocks=true;continue;}
                    action["target"]=target->at("path");
                    if(kind=="Mover"){action["kind"]="open";action["hold"]=Fold(target->value("state",std::string()))=="triggertoggle";}
                    else if(kind=="Light")action["kind"]="light";
                    else if(kind=="Sound")
                    {
                        action["kind"]="sound";
                        if(StageOf(tag,"Sound")==n){action.erase("target");action["sound"]=target->value("sound",std::string());}
                    }
                    else if(kind=="Alarm" && StageOf(tag,"Announce")==n)
                    {
                        action.erase("target");action["kind"]="announce";
                        action["title"]=target->value("title",std::string());action["merc"]=target->value("merc",std::string());
                        action["spy"]=target->value("spy",std::string());action["seconds"]=target->value("duration",std::string("8"));
                    }
                    else {action["kind"]="trigger";action["untrigger"]=Untriggers(entry.value("Type",std::string()));}
                    stage["actions"].push_back(action);
                }
        }
        plan["stages"].push_back(stage);
    }
    plan["lockLater"]=last<=1 || unlocks;
    return plan;
}
// One line per action for lists and previews.
inline std::string Describe(const Json& action,const Json& actors)
{
    const auto kind=action.value("kind",std::string());
    auto name=[&](const std::string& path){const auto* actor=ByPath(actors,path);return actor?(actor->value("name",std::string()).empty()?ShortName(path):actor->value("name",std::string())+" ("+ShortName(path)+")"):ShortName(path)+" (missing)";};
    std::string text;
    if(kind=="open")text="open "+name(action.value("target",std::string()))+(action.value("hold",true)?", stays open":"");
    else if(kind=="light")text="switch light "+name(action.value("target",std::string()));
    else if(kind=="sound")text=action.contains("sound")?"play "+action.value("sound",std::string()):"fire sound trigger "+name(action.value("target",std::string()));
    else if(kind=="announce")text="announce \""+action.value("title",std::string())+"\" on both HUDs for "+action.value("seconds",std::string("8"))+" s";
    else if(kind=="trigger")text=std::string(action.value("untrigger",false)?"untrigger ":"trigger ")+(action.contains("target")?name(action.value("target",std::string())):"Tag "+action.value("tag",std::string()));
    else text="unknown action";
    const auto delay=action.value("delay",std::string("0"));
    try{if(Magic::Number(delay)>0)text+=" after "+delay+" s";}catch(const std::exception&){}
    return text;
}
// The map-changes operations that make a map match a plan, and notes on what
// they do. Update operations carry an empty "before"; the caller fills it
// from the live actor so the batch rejects a map that changed meanwhile.
struct Build { Json operations=Json::array(); std::vector<std::string> notes; size_t creates=0,updates=0; };
inline Build Plan(const Json& actors,std::set<std::string> used,const Json& plan)
{
    Validate(plan);
    Build build;
    std::map<std::string,Json> updates;
    std::map<std::string,std::string> classes;
    auto update=[&](const Json& actor,const std::string& key,const Json& value)
    {
        const auto path=actor.at("path").get<std::string>();
        updates[path][key]=value;classes[path]=actor.value("class",std::string());
    };
    auto create=[&](const std::string& id,const std::string& type,Json properties)
    {
        build.operations.push_back({{"op","create"},{"id",id},{"class",type},{"properties",std::move(properties)}});
        used.insert(Fold(id));++build.creates;
    };
    auto location=[](const Vector& at){return Json{{"X",std::to_string(at[0])},{"Y",std::to_string(at[1])},{"Z",std::to_string(at[2])}};};
    // Anything an event reaches needs a Tag; one is allocated where missing.
    std::map<std::string,std::string> tags;
    auto tagFor=[&](const Json& actor)->std::string
    {
        const auto path=actor.at("path").get<std::string>();
        if(auto found=tags.find(path);found!=tags.end())return found->second;
        auto tag=actor.value("tag",std::string());
        if(NoneTag(tag) || actor.value("shared",false))
        {
            const auto type=actor.value("class",std::string());
            auto stem=type.substr(type.find_last_of('.')+1);
            if(stem.size()>1 && stem[0]=='S' && std::isupper(static_cast<unsigned char>(stem[1])))stem=stem.substr(1);
            tag=Security::NextTag(stem,used);
            update(actor,"Tag",tag);
            build.notes.push_back(ShortName(path)+" gets Tag "+tag+(NoneTag(actor.value("tag",std::string()))?" so the stage can reach it.":" of its own; its old Tag "+actor.value("tag",std::string())+" is shared with other actors, which the stage must not trigger too."));
        }
        used.insert(Fold(tag));tags[path]=tag;
        return tag;
    };
    const auto& stages=plan.at("stages");
    const int count=static_cast<int>(stages.size());
    const bool lock=plan.value("lockLater",true);
    std::map<std::string,int> stageOf;
    for(int n=1;n<=count;++n)
    {
        if(stages[n-1].at("objectives").empty())throw std::runtime_error("Stage "+std::to_string(n)+" has no objectives. Add one or remove the stage.");
        for(const auto& path:stages[n-1].at("objectives"))
        {
            const auto* actor=ByPath(actors,path.get<std::string>());
            if(!actor || actor->value("kind",std::string())!="Objective")throw std::runtime_error("Stage "+std::to_string(n)+" lists "+ShortName(path.get<std::string>())+", which is not an objective in this map.");
            stageOf[path.get<std::string>()]=n;
        }
    }
    // Objectives feed their stage's gate; the terminals of later stages start
    // locked and the rest are usable from the start.
    for(const auto& actor:actors)
    {
        if(actor.value("kind",std::string())!="Objective")continue;
        const auto path=actor.at("path").get<std::string>(),current=actor.value("event",std::string("None"));
        const int n=stageOf.count(path)?stageOf.at(path):0;
        const bool fedAGate=StageOf(current,"Gate")!=0;
        const std::string desired=n?GateTag(n):(fedAGate?"None":current);
        if(Fold(desired)!=Fold(current))
        {
            update(actor,"Event",desired);
            if(n && !NoneTag(current) && !fedAGate)build.notes.push_back(ShortName(path)+": Event "+current+" replaced by "+desired+"; that event is no longer fired by this objective.");
        }
        for(const auto& trigger:actor.value("triggers",Json::array()))
        {
            const auto* terminal=ByPath(actors,trigger.get<std::string>());
            if(!terminal)continue;
            const bool usable=terminal->value("usable",true),locked=!usable && Fold(terminal->value("method",std::string()))=="triggercontrol";
            if(n>=2 && lock)
            {
                tagFor(*terminal);
                if(usable)update(*terminal,"bInitialyUsable","False");
                if(Fold(terminal->value("method",std::string()))!="triggercontrol")update(*terminal,"TriggerMethode","TriggerControl");
                if(usable)build.notes.push_back(ShortName(trigger.get<std::string>())+" starts locked until stage "+std::to_string(n)+" begins.");
            }
            else if(locked && (n==1 || (n==0 && fedAGate) || (n>=2 && !lock)))
            {
                update(*terminal,"bInitialyUsable","True");
                build.notes.push_back(ShortName(trigger.get<std::string>())+" is usable from the start again.");
            }
        }
    }
    auto entry=[](const std::string& tag,const std::string& delay,bool untrigger)
    {
        return Json{{"Delay",delay},{"Event",tag},{"Type",untrigger?"EVT_Untrigger":"EVT_Trigger"},{"ValidOn","EVT_Trigger"}};
    };
    auto event=[&](const std::string& tag,const Json& groups,const Vector& at)
    {
        if(const auto* existing=ByTag(actors,tag))
        {
            if(existing->value("kind",std::string())!="Magic event")throw std::runtime_error("Tag "+tag+" belongs to "+ShortName(existing->value("path",std::string()))+", which is not an SMagicEvent. Rename it first.");
            if(!Equivalent(existing->value("groups",Json::array()),groups))update(*existing,"Groups",groups);
            return;
        }
        create(tag,"SBase.SMagicEvent",{{"Location",location(at)},{"Groups",groups}});
    };
    for(int n=1;n<=count;++n)
    {
        const auto& stage=stages[n-1];
        const int total=static_cast<int>(stage.at("objectives").size());
        const int needed=stage.value("required",0)?stage.value("required",0):total;
        Vector anchor{};
        for(const auto& path:stage.at("objectives"))
        {
            const auto at=ByPath(actors,path.get<std::string>())->value("position",Vector{});
            for(int axis=0;axis<3;++axis)anchor[axis]+=at[axis]/total;
        }
        anchor[2]+=96;
        // The gate: a Sequence that steps once per completion and fires the
        // stage's completion on the last step, then retires (Repeat 1).
        Json steps=Json::array();
        for(int i=0;i<needed;++i)steps.push_back(entry(i+1==needed?CompleteTag(n):"None","0",false));
        event(GateTag(n),Json::array({{{"EventGroup",steps},{"Repeat","1"},{"Sequence","True"},{"SequenceIndex","0"}}}),{anchor[0]-48,anchor[1],anchor[2]});
        // The completion: unlock the next stage, then the author's actions.
        Json actions=Json::array();
        if(lock && n<count)
            for(const auto& path:stages[n].at("objectives"))
                for(const auto& trigger:ByPath(actors,path.get<std::string>())->value("triggers",Json::array()))
                    if(const auto* terminal=ByPath(actors,trigger.get<std::string>()))actions.push_back(entry(tagFor(*terminal),"0",false));
        int sounds=0;
        for(const auto& action:stage.at("actions"))
        {
            const auto kind=action.value("kind",std::string());
            const auto delay=Magic::Seconds(Magic::Number(action.value("delay",std::string("0"))));
            const auto label="Stage "+std::to_string(n);
            auto target=[&](const char* wanted)->const Json&
            {
                const auto* actor=ByPath(actors,action.value("target",std::string()));
                if(!actor || (wanted && actor->value("kind",std::string())!=wanted))throw std::runtime_error(label+": "+ShortName(action.value("target",std::string()))+" is not a"+(wanted?std::string(" ")+Fold(wanted):std::string("n actor"))+" in this map.");
                return *actor;
            };
            if(kind=="open")
            {
                const auto& mover=target("Mover");
                actions.push_back(entry(tagFor(mover),delay,false));
                const auto state=Fold(mover.value("state",std::string()));
                if(action.value("hold",true) && (state.empty() || state=="none" || state.find("timed")!=std::string::npos))
                {
                    update(mover,"InitialState","TriggerToggle");
                    build.notes.push_back(ShortName(mover.at("path").get<std::string>())+" stays open once triggered (InitialState TriggerToggle).");
                }
            }
            else if(kind=="light")actions.push_back(entry(tagFor(target("Light")),delay,false));
            else if(kind=="sound")
            {
                if(action.contains("target"))actions.push_back(entry(tagFor(target("Sound")),delay,false));
                else
                {
                    const auto id=SoundTag(n,++sounds),ref=action.at("sound").get<std::string>();
                    if(const auto* existing=ByTag(actors,id))
                    {
                        if(existing->value("kind",std::string())!="Sound")throw std::runtime_error("Tag "+id+" belongs to "+ShortName(existing->value("path",std::string()))+", which is not a sound trigger. Rename it first.");
                        if(existing->value("sound",std::string())!=ref)update(*existing,"Sound",ref);
                    }
                    else create(id,"Engine.SoundTrigger",{{"Location",location({anchor[0],anchor[1]+48.0*sounds,anchor[2]})},{"Sound",ref}});
                    actions.push_back(entry(id,delay,false));
                }
            }
            else if(kind=="announce")
            {
                const auto id=AnnounceTag(n);
                const Json fields={{"AlarmName",Quoted(action.value("title",std::string()))},{"AlarmDescription",Quoted(action.value("merc",std::string()))},
                    {"AlarmDescriptionSpy",Quoted(action.value("spy",std::string()))},{"Duration",std::to_string(static_cast<int>(Magic::Number(action.value("seconds",std::string("8")))))}};
                if(const auto* existing=ByTag(actors,id))
                {
                    if(existing->value("kind",std::string())!="Alarm")throw std::runtime_error("Tag "+id+" belongs to "+ShortName(existing->value("path",std::string()))+", which is not an alarm. Rename it first.");
                    const std::map<std::string,std::string> current={{"AlarmName",Quoted(existing->value("title",std::string()))},{"AlarmDescription",Quoted(existing->value("merc",std::string()))},
                        {"AlarmDescriptionSpy",Quoted(existing->value("spy",std::string()))},{"Duration",existing->value("duration",std::string())}};
                    for(auto it=fields.begin();it!=fields.end();++it)if(!Equivalent(Json(current.at(it.key())),it.value()))update(*existing,it.key(),it.value());
                }
                else
                {
                    Json properties=fields;properties["Location"]=location({anchor[0],anchor[1]-48,anchor[2]});properties["Events"]=Json::array();
                    create(id,"SBase.SAlarm",properties);
                }
                actions.push_back(entry(id,delay,action.value("untrigger",false)));
            }
            else if(kind=="trigger")
            {
                if(action.contains("target"))actions.push_back(entry(tagFor(target(nullptr)),delay,action.value("untrigger",false)));
                else actions.push_back(entry(action.at("tag").get<std::string>(),delay,action.value("untrigger",false)));
            }
        }
        event(CompleteTag(n),Json::array({{{"EventGroup",actions},{"Repeat","1"},{"Sequence","False"},{"SequenceIndex","0"}}}),{anchor[0]+48,anchor[1],anchor[2]});
        build.notes.push_back("Stage "+std::to_string(n)+" completes when "+(needed==total?(total==1?std::string("its objective is"):"all "+std::to_string(total)+" objectives are"):std::to_string(needed)+" of "+std::to_string(total)+" objectives are")+" done, then "+
            (actions.empty()?std::string("triggers nothing")+(n<count?".":" (it is the last stage)."):std::to_string(actions.size())+" action(s) fire."));
    }
    for(const auto& actor:actors)
    {
        if(actor.value("kind",std::string())!="Magic event")continue;
        const auto tag=actor.value("tag",std::string());
        const int n=std::max(StageOf(tag,"Gate"),StageOf(tag,"Complete"));
        if(n>count)build.notes.push_back(ShortName(actor.at("path").get<std::string>())+" belongs to stage "+std::to_string(n)+", which no longer exists; delete it in the editor.");
    }
    for(auto& [path,properties]:updates)
    {
        build.operations.push_back({{"op","update"},{"actor",{{"path",path},{"class",classes.at(path)}}},{"before",Json::object()},{"properties",properties}});
        ++build.updates;
    }
    return build;
}
// Design-check issues for the stages a map carries: gates nothing feeds or
// that can never complete, events that reach nothing, terminals usable before
// their stage, objectives outside every stage, and numbering gaps.
inline std::vector<Security::Issue> Issues(const Json& actors)
{
    std::vector<Security::Issue> issues;
    std::map<int,const Json*> gates,completes;
    for(const auto& actor:actors)
    {
        if(actor.value("kind",std::string())!="Magic event")continue;
        const auto tag=actor.value("tag",std::string());
        if(const int n=StageOf(tag,"Gate"))gates[n]=&actor;
        if(const int n=StageOf(tag,"Complete"))completes[n]=&actor;
    }
    if(gates.empty() && completes.empty())return issues;
    int last=0;
    for(const auto& [n,gate]:gates)last=std::max(last,n);
    for(const auto& [n,complete]:completes)last=std::max(last,n);
    bool unlocks=false;
    std::set<std::string> lateTerminals;
    for(int n=1;n<=last;++n)
    {
        const auto label="Stage "+std::to_string(n);
        int feeding=0;
        for(const auto& actor:actors)
            if(actor.value("kind",std::string())=="Objective" && Fold(actor.value("event",std::string()))==Fold(GateTag(n)))
            {
                ++feeding;
                if(n>=2)for(const auto& trigger:actor.value("triggers",Json::array()))lateTerminals.insert(trigger.get<std::string>());
            }
        if(!gates.count(n)){issues.push_back({"error",label+" has no "+GateTag(n)+" event, yet stage "+std::to_string(last)+" exists. Open Stages and apply the plan again.",""});continue;}
        const auto& gate=*gates.at(n);
        const auto groups=gate.value("groups",Json::array());
        const Json steps=(!groups.empty() && groups[0].is_object())?groups[0].value("EventGroup",Json::array()):Json::array();
        if(feeding==0)issues.push_back({"error",label+": no objective's Event feeds "+GateTag(n)+", so the stage can never complete.",gate.value("path",std::string())});
        else if(static_cast<int>(steps.size())>feeding)issues.push_back({"error",label+" needs "+std::to_string(steps.size())+" completions but only "+std::to_string(feeding)+" objectives feed it, so it can never complete.",gate.value("path",std::string())});
        if(steps.empty() || Fold(steps.back().value("Event",std::string()))!=Fold(CompleteTag(n)))issues.push_back({"warning",label+": "+GateTag(n)+" does not end by firing "+CompleteTag(n)+".",gate.value("path",std::string())});
        if(!completes.count(n)){issues.push_back({"error",label+" has no "+CompleteTag(n)+" event to fire when it ends.",""});continue;}
        const auto& complete=*completes.at(n);
        const auto cgroups=complete.value("groups",Json::array());
        const Json actions=(!cgroups.empty() && cgroups[0].is_object())?cgroups[0].value("EventGroup",Json::array()):Json::array();
        if(actions.empty() && n<last)issues.push_back({"warning",label+" completing triggers nothing, so nothing opens the way to stage "+std::to_string(n+1)+".",complete.value("path",std::string())});
        for(const auto& action:actions)
        {
            const auto tag=action.value("Event",std::string("None"));
            if(NoneTag(tag))continue;
            const auto* target=ByTag(actors,tag);
            if(!target)issues.push_back({"error",label+" fires Event "+tag+" but nothing has that Tag.",complete.value("path",std::string())});
            else if(target->value("kind",std::string())=="Objective trigger")unlocks=true;
        }
    }
    for(const auto& actor:actors)
    {
        const auto kind=actor.value("kind",std::string()),path=actor.value("path",std::string());
        if(kind=="Objective" && !StageOf(actor.value("event",std::string()),"Gate"))
            issues.push_back({"info",ShortName(path)+" (objective) is in no stage, so it counts from the start of the match.",path});
        if(kind=="Objective trigger" && unlocks && lateTerminals.count(path) && actor.value("usable",true))
            issues.push_back({"warning",ShortName(path)+" is usable from the start although its stage begins later. Apply the stages plan again to lock it.",path});
    }
    return issues;
}
}
