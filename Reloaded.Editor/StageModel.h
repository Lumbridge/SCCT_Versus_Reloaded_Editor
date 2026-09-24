#pragma once
#include "SecurityModel.h"
#include "MagicEventModel.h"
#include <map>
#include <set>

// Zones of a Versus match: a zone is a set of objectives that are all
// available at once, and the match works through the zones in order. The game
// already does this with its own mission actors, so the editor builds that
// shape rather than a counting machine of its own:
//
//   TopLevelMission (SMission, bChained True, GameMode GM_Multi)
//     Objectives = Zone1Mission, Zone2Mission, ...   in match order
//   Zone<N>Mission (SMission, bChained False, GameMode GM_Undefined)
//     Objectives = the zone's SObjectives, all live from the moment the zone
//     starts; MinimumObjectives is how many of them end the zone.
//
// MinimumObjectives on the top mission is not a count of zones: it is how many
// objectives the spies need to win the match. Working through every zone
// completes each zone's own threshold, so the match total is those added up
// unless the author wants the spies to win before the last zone ends.
//
// Because the parent is chained, the game runs the zones one after another and
// nothing has to be locked by hand. A zone's Event fires when it is complete,
// so the things an author wants then (doors, lights, sounds, a HUD message,
// any other actor) hang off one SMagicEvent with that Tag.
//
// A plan is a small JSON document; Read derives it from a map's actors and
// Plan turns it back into a scct.map-changes batch, so the map can be
// re-read and re-applied at any time. Engine-independent.
namespace Workflow::Stages
{
inline std::string ZoneTag(int n){return "Zone"+std::to_string(n)+"Mission";}
inline std::string CompleteTag(int n){return "Zone"+std::to_string(n)+"_Complete";}
inline std::string AnnounceTag(int n){return "Zone"+std::to_string(n)+"_Announce";}
inline std::string SoundTag(int n,int i){return "Zone"+std::to_string(n)+"_Sound"+std::to_string(i);}
// The number in a generated Tag such as Zone2Mission or Zone2_Complete, or 0
// when the Tag is not one of ours. Sound tags carry an index after the suffix.
inline int TagNumber(const std::string& tag,const std::string& prefix,const std::string& suffix)
{
    const auto folded=Fold(tag),head=Fold(prefix);
    if(folded.size()<=head.size() || folded.compare(0,head.size(),head)!=0)return 0;
    size_t i=head.size();const size_t start=i;
    while(i<folded.size() && std::isdigit(static_cast<unsigned char>(folded[i])))++i;
    if(i==start || i-start>6)return 0;
    const auto rest=folded.substr(i),wanted=Fold(suffix);
    if(rest.compare(0,wanted.size(),wanted)!=0)return 0;
    const auto index=rest.substr(wanted.size());
    if(!index.empty() && (wanted!="_sound" || index.find_first_not_of("0123456789")!=std::string::npos))return 0;
    return std::stoi(folded.substr(start,i-start));
}
inline int ZoneOf(const std::string& tag,const std::string& suffix){return TagNumber(tag,"Zone",suffix);}
// Tags the earlier counting-event version of this window generated, which a
// map may still carry.
inline int LegacyOf(const std::string& tag,const std::string& suffix){return TagNumber(tag,"Stage","_"+suffix);}
inline bool NoneTag(const std::string& tag){return tag.empty() || Fold(tag)=="none";}
inline std::string ShortName(const std::string& path){return path.substr(path.find_last_of('.')+1);}
inline Json Empty(){return {{"format","scct.stages"},{"version",2},{"required",0},{"zones",Json::array()}};}
inline Json NewZone(){return {{"name",""},{"brief",""},{"briefDefend",""},{"objectives",Json::array()},{"required",0},{"actions",Json::array()}};}
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
// An actor as an object-reference property value.
inline std::string Reference(const Json& actor)
{
    return actor.value("class",std::string())+"'"+actor.value("path",std::string())+"'";
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
inline int Whole(const std::string& text)
{
    try{return std::stoi(text);}catch(const std::exception&){return 0;}
}
// How many of a zone's objectives end it, 0 meaning all of them.
inline int Needed(const Json& zone)
{
    const int total=static_cast<int>(zone.value("objectives",Json::array()).size()),required=zone.value("required",0);
    return required?required:total;
}
// Every objective the plan's zones hold.
inline int Objectives(const Json& plan)
{
    int total=0;
    for(const auto& zone:plan.value("zones",Json::array()))total+=static_cast<int>(zone.value("objectives",Json::array()).size());
    return total;
}
// The objectives the spies complete by working through every zone: what the
// top mission's MinimumObjectives is unless the plan names a smaller number.
inline int Thresholds(const Json& plan)
{
    int needed=0;
    for(const auto& zone:plan.value("zones",Json::array()))needed+=Needed(zone);
    return needed;
}
inline int WinsAfter(const Json& plan)
{
    const int required=plan.value("required",0);
    return required?required:Thresholds(plan);
}
// A plan document: format, version and zones, each with a name, the briefing
// both teams read, objectives (actor paths), required (0 means all) and the
// actions its completion fires.
inline void Validate(const Json& plan)
{
    if(!plan.is_object() || plan.value("format",std::string())!="scct.stages")throw std::runtime_error("This is not a zones file (scct.stages).");
    const int version=plan.value("version",0);
    if(version==1)throw std::runtime_error("This plan comes from the earlier version of this window, which counted objectives with its own events instead of giving each zone a mission. Build the zones again from the map.");
    if(version!=2)throw std::runtime_error("This is not a zones file (scct.stages, version 2).");
    if(!plan.contains("zones") || !plan.at("zones").is_array() || plan.at("zones").size()>64)throw std::runtime_error("A plan lists up to 64 zones.");
    if(plan.contains("required") && (!plan.at("required").is_number_integer() || plan.at("required").get<int>()<0))
        throw std::runtime_error("The objectives the spies need to win must be a whole number, or 0 for every zone's threshold added up.");
    std::set<std::string> seen;
    int n=0;
    for(const auto& zone:plan.at("zones"))
    {
        ++n;
        const auto label="Zone "+std::to_string(n);
        if(!zone.is_object() || !zone.contains("objectives") || !zone.at("objectives").is_array() || !zone.contains("actions") || !zone.at("actions").is_array())
            throw std::runtime_error(label+" needs objectives and actions lists.");
        for(const char* field:{"name","brief","briefDefend"})CheckText(zone.value(field,std::string()),field);
        for(const auto& objective:zone.at("objectives"))
        {
            if(!objective.is_string() || objective.get<std::string>().empty())throw std::runtime_error(label+" lists an objective without a path.");
            if(!seen.insert(Fold(objective.get<std::string>())).second)throw std::runtime_error(ShortName(objective.get<std::string>())+" is in two zones. An objective belongs to one zone.");
        }
        const int required=zone.value("required",0);
        if(required<0 || required>static_cast<int>(zone.at("objectives").size()))
            throw std::runtime_error(label+" needs "+std::to_string(required)+" completions but has "+std::to_string(zone.at("objectives").size())+" objectives.");
        for(const auto& action:zone.at("actions"))
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
    if(const int required=plan.value("required",0);required>Objectives(plan))
        throw std::runtime_error("The spies need "+std::to_string(required)+" objectives to win, but the zones hold "+std::to_string(Objectives(plan))+".");
}
inline bool Untriggers(const std::string& type){const auto t=Fold(type);return t=="evt_untrigger" || t=="evt_untriggertrigger";}
// The mission the game plays: the one no other mission holds. Where a map has
// several such missions the multiplayer one wins.
inline const Json* TopMission(const Json& actors)
{
    std::set<std::string> nested;
    for(const auto& actor:actors)
        if(actor.value("kind",std::string())=="Mission")
            for(const auto& child:actor.value("objectives",Json::array()))
                if(child.is_string())nested.insert(Fold(child.get<std::string>()));
    const Json* top=nullptr;
    for(const auto& actor:actors)
    {
        if(actor.value("kind",std::string())!="Mission" || nested.count(Fold(actor.value("path",std::string()))))continue;
        if(!top || (Fold(actor.value("mode",std::string()))=="gm_multi" && Fold(top->value("mode",std::string()))!="gm_multi"))top=&actor;
    }
    return top;
}
// The zone missions a map carries, in the order the top mission holds them.
inline std::vector<const Json*> ZoneMissions(const Json& actors)
{
    std::vector<const Json*> zones;
    const auto* top=TopMission(actors);
    if(!top)return zones;
    for(const auto& child:top->value("objectives",Json::array()))
    {
        if(!child.is_string())continue;
        const auto* actor=ByPath(actors,child.get<std::string>());
        if(actor && actor->value("kind",std::string())=="Mission")zones.push_back(actor);
    }
    return zones;
}
// The actions an SMagicEvent with this Tag fires, listed by what each target
// is. The zone number tells its own sound and announcement actors apart from
// ones the author placed.
inline Json ReadActions(const Json& actors,const std::string& tag,int n)
{
    Json actions=Json::array();
    const auto* event=ByTag(actors,tag);
    if(!event || event->value("kind",std::string())!="Magic event")return actions;
    const auto groups=event->value("groups",Json::array());
    if(groups.empty() || !groups[0].is_object() || !groups[0].contains("EventGroup"))return actions;
    for(const auto& entry:groups[0].at("EventGroup"))
    {
        const auto fired=entry.value("Event",std::string("None"));
        if(NoneTag(fired))continue;
        Json action={{"delay",entry.value("Delay",std::string("0"))}};
        const auto* target=ByTag(actors,fired);
        if(!target){action["kind"]="trigger";action["tag"]=fired;action["untrigger"]=Untriggers(entry.value("Type",std::string()));actions.push_back(action);continue;}
        const auto kind=target->value("kind",std::string());
        action["target"]=target->at("path");
        if(kind=="Mover"){action["kind"]="open";action["hold"]=Fold(target->value("state",std::string()))=="triggertoggle";}
        else if(kind=="Light")action["kind"]="light";
        else if(kind=="Sound")
        {
            action["kind"]="sound";
            if(ZoneOf(fired,"_Sound")==n){action.erase("target");action["sound"]=target->value("sound",std::string());}
        }
        else if(kind=="Alarm" && ZoneOf(fired,"_Announce")==n)
        {
            action.erase("target");action["kind"]="announce";
            action["title"]=target->value("title",std::string());action["merc"]=target->value("merc",std::string());
            action["spy"]=target->value("spy",std::string());action["seconds"]=target->value("duration",std::string("8"));
        }
        else {action["kind"]="trigger";action["untrigger"]=Untriggers(entry.value("Type",std::string()));}
        actions.push_back(action);
    }
    return actions;
}
// Reads the zones a map already carries: the missions the top mission holds,
// in its order, each with its own objectives, threshold and completion
// actions.
inline Json Read(const Json& actors)
{
    Json plan=Empty();
    const auto* top=TopMission(actors);
    if(!top)return plan;
    plan["mission"]=top->at("path");
    int n=0;
    for(const auto* mission:ZoneMissions(actors))
    {
        ++n;
        Json zone=NewZone();
        zone["mission"]=mission->at("path");
        zone["name"]=mission->value("name",std::string());
        zone["brief"]=mission->value("brief",std::string());
        zone["briefDefend"]=mission->value("briefDefend",std::string());
        for(const auto& path:mission->value("objectives",Json::array()))
        {
            if(!path.is_string())continue;
            const auto* objective=ByPath(actors,path.get<std::string>());
            if(objective && objective->value("kind",std::string())=="Objective")zone["objectives"].push_back(objective->at("path"));
        }
        const int total=static_cast<int>(zone["objectives"].size()),minimum=Whole(mission->value("minimum",std::string("0")));
        zone["required"]=(minimum>0 && minimum<total)?minimum:0;
        zone["actions"]=ReadActions(actors,mission->value("event",std::string("None")),n);
        plan["zones"].push_back(zone);
    }
    // The match total, kept only where it is not simply the zones' own.
    const int minimum=Whole(top->value("minimum",std::string("0")));
    plan["required"]=(minimum>0 && minimum!=Thresholds(plan))?minimum:0;
    return plan;
}
// The zone an objective belongs to, or 0 when it is in none.
inline int ZoneOfObjective(const Json& actors,const std::string& path)
{
    int n=0;
    for(const auto* mission:ZoneMissions(actors))
    {
        ++n;
        for(const auto& member:mission->value("objectives",Json::array()))if(member.is_string() && member.get<std::string>()==path)return n;
    }
    return 0;
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
    // A Tag of ours, or the next free variation of it where the map already
    // uses that name.
    auto reserve=[&](const std::string& wanted)
    {
        auto tag=used.count(Fold(wanted))?Security::NextTag(wanted,used):wanted;
        used.insert(Fold(tag));
        return tag;
    };
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
            build.notes.push_back(ShortName(path)+" gets Tag "+tag+(NoneTag(actor.value("tag",std::string()))?" so the zone can reach it.":" of its own; its old Tag "+actor.value("tag",std::string())+" is shared with other actors, which the zone must not trigger too."));
        }
        used.insert(Fold(tag));tags[path]=tag;
        return tag;
    };
    const auto& zones=plan.at("zones");
    const int count=static_cast<int>(zones.size());
    const auto* top=TopMission(actors);
    if(!top)throw std::runtime_error("This map has no SMission, so there is nothing to hold the zones. Right-click the plan and choose Mission here first.");
    if(count==0)
    {
        build.notes.push_back("No zones in the plan: the mission and its objectives are left as they are.");
        return build;
    }
    // Every objective in the map belongs to exactly one zone, so the mission
    // tree stays the whole picture of what the spies have to do.
    std::map<std::string,int> zoneOf;
    for(int n=1;n<=count;++n)
    {
        if(zones[n-1].at("objectives").empty())throw std::runtime_error("Zone "+std::to_string(n)+" has no objectives. Tick some or remove the zone.");
        for(const auto& path:zones[n-1].at("objectives"))
        {
            const auto* actor=ByPath(actors,path.get<std::string>());
            if(!actor || actor->value("kind",std::string())!="Objective")throw std::runtime_error("Zone "+std::to_string(n)+" lists "+ShortName(path.get<std::string>())+", which is not an objective in this map.");
            zoneOf[path.get<std::string>()]=n;
        }
    }
    for(const auto& actor:actors)
        if(actor.value("kind",std::string())=="Objective" && !zoneOf.count(actor.value("path",std::string())))
            throw std::runtime_error(ShortName(actor.value("path",std::string()))+" is in no zone. Every objective belongs to one zone; tick it in the zone it is part of.");
    // Which actor is each zone's mission: the one the plan names, else one
    // already tagged Zone<N>Mission, else a new mission.
    std::vector<const Json*> missions(count,nullptr);
    std::vector<std::string> made(count);
    std::set<std::string> claimed{Fold(top->at("path").get<std::string>())};
    auto claim=[&](const Json* actor)
    {
        return actor && actor->value("kind",std::string())=="Mission" && claimed.insert(Fold(actor->value("path",std::string()))).second;
    };
    for(int n=1;n<=count;++n)
    {
        const auto named=zones[n-1].value("mission",std::string());
        const auto* actor=named.empty()?nullptr:ByPath(actors,named);
        if(claim(actor))missions[n-1]=actor;
    }
    for(int n=1;n<=count;++n)
        if(!missions[n-1])
        {
            const auto* actor=ByTag(actors,ZoneTag(n));
            if(claim(actor))missions[n-1]=actor;
        }
    // Each zone's mission: its objectives, how many end it, and the fact that
    // they are all live at once. The zone's Event fires when it ends.
    std::vector<std::string> firing(count);
    std::vector<Vector> anchors(count);
    for(int n=1;n<=count;++n)
    {
        const auto& zone=zones[n-1];
        const auto label="Zone "+std::to_string(n);
        const int total=static_cast<int>(zone.at("objectives").size());
        const int needed=Needed(zone);
        Vector anchor{};
        for(const auto& path:zone.at("objectives"))
        {
            const auto at=ByPath(actors,path.get<std::string>())->value("position",Vector{});
            for(int axis=0;axis<3;++axis)anchor[axis]+=at[axis]/total;
        }
        anchor[2]+=96;
        anchors[n-1]=anchor;
        Json members=Json::array();
        for(const auto& path:zone.at("objectives"))members.push_back(Reference(*ByPath(actors,path.get<std::string>())));
        const auto name=zone.value("name",std::string()).empty()?label:zone.value("name",std::string());
        const auto brief=zone.value("brief",std::string()).empty()?std::string("Complete the objectives"):zone.value("brief",std::string());
        const auto briefDefend=zone.value("briefDefend",std::string()).empty()?std::string("Defend the objectives"):zone.value("briefDefend",std::string());
        // Where the zone's completion actions live: the Tag the mission
        // already fires if it has one, else a new event of ours.
        const auto* mission=missions[n-1];
        auto fires=mission?mission->value("event",std::string("None")):std::string("None");
        const bool hasActions=!zone.at("actions").empty();
        if(NoneTag(fires))fires=hasActions?reserve(CompleteTag(n)):std::string("None");
        firing[n-1]=fires;
        Json properties={{"Objectives",members},{"MinimumObjectives",std::to_string(needed)},{"bChained","False"},
                         {"GameMode","GM_Undefined"},{"ObjectiveName",Quoted(name)},{"Description",Quoted(brief)},
                         {"DescriptionDEF",Quoted(briefDefend)},{"Event",fires}};
        if(mission)
        {
            Json current=Json::array();
            for(const auto& path:mission->value("objectives",Json::array()))if(path.is_string())current.push_back(path);
            if(current!=zone.at("objectives"))update(*mission,"Objectives",members);
            if(!Equivalent(Json(mission->value("minimum",std::string("0"))),properties.at("MinimumObjectives")))update(*mission,"MinimumObjectives",properties.at("MinimumObjectives"));
            if(mission->value("chained",false))
            {
                update(*mission,"bChained","False");
                build.notes.push_back(label+"'s objectives are all available at once now (bChained off).");
            }
            if(Fold(mission->value("mode",std::string()))!="gm_undefined")
            {
                update(*mission,"GameMode","GM_Undefined");
                build.notes.push_back(label+"'s mission is no longer a game mode of its own; the top mission plays it.");
            }
            if(mission->value("name",std::string())!=name)update(*mission,"ObjectiveName",properties.at("ObjectiveName"));
            if(mission->value("brief",std::string())!=brief)update(*mission,"Description",properties.at("Description"));
            if(mission->value("briefDefend",std::string())!=briefDefend)update(*mission,"DescriptionDEF",properties.at("DescriptionDEF"));
            if(Fold(mission->value("event",std::string("None")))!=Fold(fires))update(*mission,"Event",fires);
            if(NoneTag(mission->value("tag",std::string())))
            {
                const auto tag=reserve(ZoneTag(n));
                update(*mission,"Tag",tag);
                build.notes.push_back(ShortName(mission->at("path").get<std::string>())+" gets Tag "+tag+".");
            }
        }
        else
        {
            made[n-1]=reserve(ZoneTag(n));
            properties["Location"]=location(anchor);
            create(made[n-1],"SBase.SMission",properties);
            build.notes.push_back(label+" gets its own mission "+made[n-1]+", holding "+std::to_string(total)+" objective(s).");
        }
        build.notes.push_back(label+" ends when "+(needed==total?(total==1?std::string("its objective is"):"all "+std::to_string(total)+" objectives are"):std::to_string(needed)+" of "+std::to_string(total)+" objectives are")+" done"+(NoneTag(fires)?", and triggers nothing.":", then fires "+fires+"."));
    }
    // The top mission holds the zones and runs them in order.
    {
        Json order=Json::array(),paths=Json::array();
        for(int n=1;n<=count;++n)
        {
            if(missions[n-1]){order.push_back(Reference(*missions[n-1]));paths.push_back(missions[n-1]->at("path"));}
            else {order.push_back(Json{{"$ref",made[n-1]}});paths.push_back(nullptr);}
        }
        Json current=Json::array();
        for(const auto& path:top->value("objectives",Json::array()))if(path.is_string())current.push_back(path);
        if(current!=paths)update(*top,"Objectives",order);
        if(!top->value("chained",false))
        {
            update(*top,"bChained","True");
            build.notes.push_back(ShortName(top->at("path").get<std::string>())+" runs its zones in order (bChained on).");
        }
        // How many objectives win the match, not how many zones there are.
        const int wins=WinsAfter(plan);
        if(!Equivalent(Json(top->value("minimum",std::string("0"))),Json(std::to_string(wins))))update(*top,"MinimumObjectives",std::to_string(wins));
        build.notes.push_back("The spies win after "+std::to_string(wins)+" of the "+std::to_string(Objectives(plan))+" objectives"+
            (wins<Thresholds(plan)?", before the last zone they are in ends.":(count>1?", which is every zone's threshold added up.":".")));
        if(Fold(top->value("mode",std::string()))=="gm_undefined")
        {
            update(*top,"GameMode","GM_Multi");
            build.notes.push_back(ShortName(top->at("path").get<std::string>())+" is the multiplayer mission (GameMode GM_Multi).");
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
    // What each zone's completion fires.
    for(int n=1;n<=count;++n)
    {
        const auto& zone=zones[n-1];
        if(zone.at("actions").empty())continue;
        const auto fires=firing[n-1];
        const auto anchor=anchors[n-1];
        Json actions=Json::array();
        int sounds=0;
        for(const auto& action:zone.at("actions"))
        {
            const auto kind=action.value("kind",std::string());
            const auto delay=Magic::Seconds(Magic::Number(action.value("delay",std::string("0"))));
            const auto label="Zone "+std::to_string(n);
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
        event(fires,Json::array({{{"EventGroup",actions},{"Repeat","0"},{"Sequence","False"},{"SequenceIndex","0"}}}),{anchor[0]+48,anchor[1],anchor[2]});
    }
    // A zone that no longer fires anything leaves its event behind rather than
    // firing the last plan's actions.
    for(int n=1;n<=count;++n)
    {
        if(!zones[n-1].at("actions").empty() || !missions[n-1])continue;
        const auto fires=missions[n-1]->value("event",std::string("None"));
        const auto* existing=ByTag(actors,fires);
        if(existing && existing->value("kind",std::string())=="Magic event" && !ReadActions(actors,fires,n).empty())
        {
            update(*existing,"Groups",Json::array({{{"EventGroup",Json::array()},{"Repeat","0"},{"Sequence","False"},{"SequenceIndex","0"}}}));
            build.notes.push_back("Zone "+std::to_string(n)+" fires nothing now; "+fires+" is left in the map with no actions.");
        }
    }
    // Anything the earlier counting-event version of this window left behind.
    bool legacy=false;
    for(const auto& actor:actors)
        if(actor.value("kind",std::string())=="Magic event" && (LegacyOf(actor.value("tag",std::string()),"Gate") || LegacyOf(actor.value("tag",std::string()),"Complete")))legacy=true;
    if(legacy)
    {
        for(const auto& actor:actors)
        {
            const auto kind=actor.value("kind",std::string()),path=actor.value("path",std::string());
            if(kind=="Objective" && LegacyOf(actor.value("event",std::string()),"Gate"))
            {
                update(actor,"Event","None");
                build.notes.push_back(ShortName(path)+" no longer feeds a counting event; its zone's mission counts it now.");
            }
            if(kind=="Objective trigger" && !actor.value("usable",true) && Fold(actor.value("method",std::string()))=="triggercontrol")
            {
                update(actor,"bInitialyUsable","True");
                build.notes.push_back(ShortName(path)+" is usable again; the chained mission decides when its zone is live.");
            }
            if(kind=="Magic event" && (LegacyOf(actor.value("tag",std::string()),"Gate") || LegacyOf(actor.value("tag",std::string()),"Complete")))
                build.notes.push_back(ShortName(path)+" is left over from the earlier counting wiring; delete it in the editor once its actions are in a zone.");
        }
    }
    for(auto& [path,properties]:updates)
    {
        build.operations.push_back({{"op","update"},{"actor",{{"path",path},{"class",classes.at(path)}}},{"before",Json::object()},{"properties",properties}});
        ++build.updates;
    }
    return build;
}
// Design-check issues for the zones a map carries: missions that can never
// complete, a top mission that does not run its zones in order, zone missions
// the game would treat as separate modes, completion events that reach
// nothing, objectives in no mission, and leftovers of the earlier wiring.
inline std::vector<Security::Issue> Issues(const Json& actors)
{
    std::vector<Security::Issue> issues;
    const auto* top=TopMission(actors);
    const auto zones=ZoneMissions(actors);
    std::set<std::string> held;
    for(const auto& actor:actors)
        if(actor.value("kind",std::string())=="Mission")
            for(const auto& child:actor.value("objectives",Json::array()))if(child.is_string())held.insert(child.get<std::string>());
    if(top && zones.size()>1 && !top->value("chained",false))
        issues.push_back({"warning",ShortName(top->at("path").get<std::string>())+" holds "+std::to_string(zones.size())+" zones but is not chained, so every zone is live from the start. Open Zones and apply the plan again.",top->value("path",std::string())});
    if(top && !zones.empty() && Fold(top->value("mode",std::string()))=="gm_undefined")
        issues.push_back({"warning",ShortName(top->at("path").get<std::string>())+" has GameMode GM_Undefined, so the match has no mission to play.",top->value("path",std::string())});
    if(top && !zones.empty())
    {
        // Working through every zone completes each zone's own threshold; the
        // spies can never win if the match asks for more than that.
        const int minimum=Whole(top->value("minimum",std::string("0")));
        int reachable=0;
        for(const auto* mission:zones)
        {
            int members=0;
            for(const auto& child:mission->value("objectives",Json::array()))
                if(const auto* actor=child.is_string()?ByPath(actors,child.get<std::string>()):nullptr;actor && actor->value("kind",std::string())=="Objective")++members;
            const int threshold=Whole(mission->value("minimum",std::string("0")));
            reachable+=(threshold>0 && threshold<members)?threshold:members;
        }
        if(minimum>reachable)
            issues.push_back({"error",ShortName(top->at("path").get<std::string>())+" needs "+std::to_string(minimum)+" objectives to win, but finishing every zone completes only "+std::to_string(reachable)+", so the match can never be won.",top->value("path",std::string())});
        for(const auto& child:top->value("objectives",Json::array()))
        {
            if(!child.is_string())continue;
            const auto* actor=ByPath(actors,child.get<std::string>());
            if(actor && actor->value("kind",std::string())=="Objective")
                issues.push_back({"warning",ShortName(child.get<std::string>())+" hangs straight off the top mission beside the zones, so it becomes a step of its own in the chain. Put it in a zone.",child.get<std::string>()});
        }
    }
    int n=0;
    for(const auto* mission:zones)
    {
        ++n;
        const auto label="Zone "+std::to_string(n),path=mission->value("path",std::string());
        int members=0;
        for(const auto& child:mission->value("objectives",Json::array()))
            if(const auto* actor=child.is_string()?ByPath(actors,child.get<std::string>()):nullptr;actor && actor->value("kind",std::string())=="Objective")++members;
        if(!members)issues.push_back({"error",label+" ("+ShortName(path)+") has no objectives, so it can never be completed.",path});
        else
        {
            const int minimum=Whole(mission->value("minimum",std::string("0")));
            if(minimum>members)issues.push_back({"error",label+" needs "+std::to_string(minimum)+" completions but holds "+std::to_string(members)+" objectives, so it can never be completed.",path});
        }
        if(mission->value("chained",false))
            issues.push_back({"warning",label+" is chained, so its objectives must be done in a fixed order rather than being available together.",path});
        if(Fold(mission->value("mode",std::string()))!="gm_undefined")
            issues.push_back({"warning",label+" has GameMode "+mission->value("mode",std::string())+"; a zone inside another mission should be GM_Undefined.",path});
        const auto fires=mission->value("event",std::string("None"));
        if(!NoneTag(fires) && !ByTag(actors,fires))
            issues.push_back({"error",label+" fires Event "+fires+" when it ends, but nothing in the map has that Tag.",path});
        for(const auto& action:ReadActions(actors,fires,n))
            if(action.value("kind",std::string())=="trigger" && action.contains("tag") && !ByTag(actors,action.at("tag").get<std::string>()))
                issues.push_back({"error",label+" fires Event "+action.at("tag").get<std::string>()+" but nothing has that Tag.",path});
    }
    for(const auto& actor:actors)
    {
        const auto kind=actor.value("kind",std::string()),path=actor.value("path",std::string());
        if(kind=="Objective" && !held.count(path))
            issues.push_back({"info",ShortName(path)+" (objective) is in no mission, so no zone counts it.",path});
        if(kind=="Magic event" && (LegacyOf(actor.value("tag",std::string()),"Gate") || LegacyOf(actor.value("tag",std::string()),"Complete")))
            issues.push_back({"warning",ShortName(path)+" is left over from the earlier counting wiring; the zones' missions do the counting now. Move its actions into a zone and delete it.",path});
        if(kind=="Objective" && LegacyOf(actor.value("event",std::string()),"Gate"))
            issues.push_back({"warning",ShortName(path)+" still feeds "+actor.value("event",std::string())+"; apply the zones plan again to clear it.",path});
    }
    return issues;
}
}
