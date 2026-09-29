#pragma once
#include "WorkflowModel.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <regex>
#include <set>
#include <stdexcept>
#include <utility>

// The Emitter Library: particle effects saved as canonical assembly
// definitions (CaptureAssembly + CanonicalizeAssembly) with a name, category
// and description, so they place into any map through PlaceAssembly: one Undo
// step, unique RE_ names, package auto-load. Engine-independent.
//
// Entry, the contract with the explorer and preview:
//   {"id": "builtin.<slug>" for bundled entries | 32 lower-case hex for user entries,
//    "name", "category", "description",
//    "builtin": true|false (in memory only, never written),
//    "modified": epoch milliseconds as a string,
//    "source": "<Map>.<Actor>[,...]" (optional),
//    "pivot": [x,y,z], "dependencies": [object paths], "bindings": [] (always empty),
//    "actors": [canonical assembly actor records {name,class,path,sourcePath,text,position,rotation,tag,event}],
//    "preview": {"distance","yaw","pitch","target":[x,y,z]} (optional camera hint)}
// User file, <Editor::Directory()>/emitter_library.json:
//   {"version":1,"emitters":[user entries],"hiddenBuiltins":[built-in ids the user deleted]}
// Built-in entries are compiled into the DLL (EmitterLibraryDefaults.gen.h) and are
// never written to disk; deleting one only hides it.
namespace Workflow::EmitterLibrary
{
inline const std::vector<std::string>& DefaultCategories()
{
    static const std::vector<std::string> categories={"Fire","Smoke","Steam","Water","Weather","Sparks & Electrical","Dust & Debris","Insects","Lights & Glows","Explosions & Bursts","Other"};
    return categories;
}
inline bool IsBuiltinId(const std::string& id){return std::regex_match(id,std::regex("builtin\\.[a-z0-9][a-z0-9_.-]{0,62}"));}
inline bool IsUserId(const std::string& id){return std::regex_match(id,std::regex("[0-9a-f]{32}"));}
// A lower-case identifier for a built-in id: letters and digits, with any run
// of other characters turned into one underscore.
inline std::string Slug(const std::string& name)
{
    std::string out;
    for(unsigned char c:name)
    {
        if(c<128 && std::isalnum(c))out+=static_cast<char>(std::tolower(c));
        else if(!out.empty() && out.back()!='_')out+='_';
    }
    if(out.size()>48)out.resize(48);
    while(!out.empty() && out.back()=='_')out.pop_back();
    if(out.empty())throw std::runtime_error("Enter a name containing letters or digits.");
    return out;
}
inline std::string BuiltinId(const std::string& name){return "builtin."+Slug(name);}
inline bool ValidUtf8(const std::string& text)
{
    for(size_t i=0;i<text.size();)
    {
        const auto c=static_cast<unsigned char>(text[i]);
        const size_t length=c<0x80?1:(c>>5)==6?2:(c>>4)==14?3:(c>>3)==30?4:0;
        if(!length || i+length>text.size())return false;
        for(size_t j=1;j<length;++j)if((static_cast<unsigned char>(text[i+j])&0xc0)!=0x80)return false;
        i+=length;
    }
    return true;
}
// Trims the ends and collapses runs of spaces, tabs and line breaks into one
// space; lines keeps line breaks (descriptions) instead.
inline std::string CleanText(const std::string& value,bool lines=false)
{
    if(!ValidUtf8(value))throw std::runtime_error("Text must be UTF-8.");
    std::string out;bool space=false,newline=false;
    for(unsigned char c:value)
    {
        if(c=='\n' && lines){newline=!out.empty();space=false;continue;}
        if(c==' ' || c=='\t' || c=='\r' || c=='\n'){space=!out.empty();continue;}
        if(c<32 || c==127)throw std::runtime_error("Text cannot contain control characters.");
        if(newline)out+='\n';else if(space)out+=' ';
        space=newline=false;out+=static_cast<char>(c);
    }
    return out;
}
inline std::string CleanName(const std::string& name)
{
    auto value=CleanText(name);
    if(value.empty() || value.size()>120)throw std::runtime_error("Enter a name between 1 and 120 characters.");
    return value;
}
inline std::string CleanDescription(const std::string& description)
{
    auto value=CleanText(description,true);
    if(value.size()>2000)throw std::runtime_error("Keep the description under 2000 characters.");
    return value;
}
// A category the user typed takes the spelling of a default or existing
// category that matches it ignoring case.
inline std::string CleanCategory(const std::string& category,const Json& entries=Json::array())
{
    auto value=CleanText(category);
    if(value.empty())return "Other";
    if(value.size()>48)throw std::runtime_error("Enter a category of at most 48 characters.");
    for(const auto& known:DefaultCategories())if(Fold(known)==Fold(value))return known;
    if(entries.is_array())for(const auto& entry:entries)
        if(entry.is_object() && entry.contains("category") && entry.at("category").is_string() && Fold(entry.at("category").get<std::string>())==Fold(value))return entry.at("category").get<std::string>();
    return value;
}
// Defaults first, in their order, then the other categories entries use,
// alphabetically, before Other.
inline Json Categories(const Json& entries)
{
    std::vector<std::string> extra;std::set<std::string> seen;
    for(const auto& known:DefaultCategories())seen.insert(Fold(known));
    if(entries.is_array())for(const auto& entry:entries)
    {
        if(!entry.is_object() || !entry.contains("category") || !entry.at("category").is_string())continue;
        auto category=entry.at("category").get<std::string>();
        if(!category.empty() && seen.insert(Fold(category)).second)extra.push_back(category);
    }
    std::sort(extra.begin(),extra.end(),[](const std::string& a,const std::string& b){return Fold(a)<Fold(b);});
    Json result=Json::array();
    for(const auto& known:DefaultCategories())if(known!="Other")result.push_back(known);
    for(const auto& category:extra)result.push_back(category);
    result.push_back("Other");return result;
}
namespace Detail
{
    inline bool Finite(const Json& value){return value.is_number() && std::isfinite(value.get<double>());}
    inline bool Point(const Json& value)
    {
        return value.is_array() && value.size()==3 && std::all_of(value.begin(),value.end(),[](const Json& v){return Finite(v) && std::abs(v.get<double>())<=10000000;});
    }
}
// Throws a user-facing message naming what is wrong. Built-in and user entries
// share the schema; ids tell them apart.
inline void Validate(const Json& entry)
{
    auto fail=[&](const std::string& why)
    {
        std::string name=entry.is_object() && entry.contains("name") && entry.at("name").is_string()?" '"+entry.at("name").get<std::string>()+"'":"";
        throw std::runtime_error("Emitter Library entry"+name+" is invalid: "+why);
    };
    if(!entry.is_object())fail("it is not an object.");
    auto text=[&](const char* key,bool required=true)->std::string
    {
        if(!entry.contains(key)){if(required)fail(std::string("its ")+key+" is missing.");return {};}
        if(!entry.at(key).is_string())fail(std::string("its ")+key+" is not text.");
        return entry.at(key).get<std::string>();
    };
    const auto id=text("id");const bool builtin=IsBuiltinId(id);
    if(!builtin && !IsUserId(id))fail("its id is neither a built-in nor a user id.");
    if(entry.contains("builtin") && (!entry.at("builtin").is_boolean() || entry.at("builtin").get<bool>()!=builtin))fail("its built-in flag does not match its id.");
    try{CleanName(text("name"));CleanCategory(text("category"));CleanDescription(text("description",false));}
    catch(const std::exception& e){fail(e.what());}
    const auto modified=text("modified",!builtin);
    if(!modified.empty() && (modified.size()>20 || modified.find_first_not_of("0123456789")!=std::string::npos))fail("its modified time is not a number of milliseconds.");
    if(!ValidUtf8(text("source",false)))fail("its source is not UTF-8.");
    if(!entry.contains("pivot") || !Detail::Point(entry.at("pivot")))fail("its pivot is not a finite position.");
    if(!entry.contains("bindings") || !entry.at("bindings").is_array())fail("its bindings are missing.");
    if(!entry.at("bindings").empty())fail("it links to actors outside the entry. Emitter Library entries cannot have external bindings.");
    if(!entry.contains("dependencies") || !entry.at("dependencies").is_array())fail("its dependencies are missing.");
    for(const auto& dependency:entry.at("dependencies"))
    {
        if(!dependency.is_string() || !std::regex_match(dependency.get<std::string>(),std::regex("[A-Za-z0-9_-]+(\\.[A-Za-z0-9_-]+)+")) || dependency.get<std::string>().size()>256)fail("it has an invalid dependency path.");
        if(Fold(dependency.get<std::string>()).rfind("mylevel.",0)==0)fail("it uses "+dependency.get<std::string>()+", which is stored inside a map.");
    }
    if(!entry.contains("actors") || !entry.at("actors").is_array() || entry.at("actors").empty())fail("it has no actors.");
    if(entry.at("actors").size()>256)fail("it has more than 256 actors.");
    std::set<std::string> names;
    for(const auto& actor:entry.at("actors"))
    {
        if(!actor.is_object())fail("an actor record is not an object.");
        for(const char* key:{"name","class","path","text"})if(!actor.contains(key) || !actor.at(key).is_string() || actor.at(key).get<std::string>().empty())fail(std::string("an actor has no ")+key+".");
        const auto name=actor.at("name").get<std::string>();
        if(!names.insert(Fold(name)).second)fail("two actors are both called "+name+".");
        if(Fold(actor.at("path").get<std::string>())!=Fold("Assembly."+name))fail("actor "+name+" is not in canonical form.");
        if(!Detail::Point(actor.value("position",Json{})))fail("actor "+name+" has no finite position.");
        const auto rotation=actor.value("rotation",Json{});
        if(!rotation.is_array() || rotation.size()!=3 || !std::all_of(rotation.begin(),rotation.end(),[](const Json& v){return v.is_number_integer() && std::abs(v.get<int64_t>())<=std::numeric_limits<int>::max();}))fail("actor "+name+" has no rotation.");
        for(const char* key:{"tag","event","sourcePath"})if(actor.contains(key) && !actor.at(key).is_string())fail(std::string("actor ")+name+" has an invalid "+key+".");
        std::vector<ActorText> parsed;
        try{parsed=ParseActors(actor.at("text").get<std::string>());}
        catch(const std::exception& e){fail("the text of actor "+name+" cannot be read: "+e.what());}
        if(parsed.size()!=1 || Fold(parsed[0].name)!=Fold(name))fail("the text of actor "+name+" does not declare that actor.");
    }
    if(entry.contains("preview"))
    {
        const auto& preview=entry.at("preview");
        if(!preview.is_object())fail("its preview hint is not an object.");
        for(const char* key:{"distance","yaw","pitch"})if(preview.contains(key) && !Detail::Finite(preview.at(key)))fail(std::string("its preview ")+key+" is not a number.");
        if(preview.contains("distance") && preview.at("distance").get<double>()<=0)fail("its preview distance is not positive.");
        if(preview.contains("target") && !Detail::Point(preview.at("target")))fail("its preview target is not a position.");
    }
}
inline Json EmptyDocument(){return {{"version",1},{"emitters",Json::array()},{"hiddenBuiltins",Json::array()}};}
// A user file that cannot be read, parsed or understood. Listing goes on with
// the built-ins and reports this; editing refuses rather than overwrite it.
inline const char* DamagedFile(){return "The Emitter Library file (emitter_library.json) is damaged or from a newer version, so your saved emitters are not listed. Restore a valid copy or move it aside before editing the library.";}
// The user file as read by ReadDocument (version 1 already checked); missing
// lists are added, wrong types refuse to edit a damaged file.
inline Json Document(Json document)
{
    if(!document.is_object())throw std::runtime_error(DamagedFile());
    if(!document.contains("emitters"))document["emitters"]=Json::array();
    if(!document.contains("hiddenBuiltins"))document["hiddenBuiltins"]=Json::array();
    bool valid=document.at("emitters").is_array() && document.at("hiddenBuiltins").is_array();
    if(valid)for(const auto& id:document.at("hiddenBuiltins"))valid=valid && id.is_string();
    if(!valid)throw std::runtime_error(DamagedFile());
    return document;
}
// The compiled-in defaults: {"version":1,"emitters":[built-in entries]}.
inline Json Builtins(const std::string& text)
{
    Json document;
    try{document=Json::parse(text);}catch(const std::exception& e){throw std::runtime_error(std::string("The built-in Emitter Library cannot be read: ")+e.what());}
    if(!document.is_object() || document.value("version",0)!=1 || !document.contains("emitters") || !document.at("emitters").is_array())throw std::runtime_error("The built-in Emitter Library has an unsupported format.");
    Json result=Json::array();std::set<std::string> ids;
    for(auto entry:document.at("emitters"))
    {
        Validate(entry);
        const auto id=entry.at("id").get<std::string>();
        if(!IsBuiltinId(id))throw std::runtime_error("Built-in Emitter Library entry '"+entry.at("name").get<std::string>()+"' does not have a builtin. id.");
        if(!ids.insert(id).second)throw std::runtime_error("Built-in Emitter Library id "+id+" is used twice.");
        entry["builtin"]=true;result.push_back(std::move(entry));
    }
    return result;
}
// Built-ins the user has not hidden, then the valid user entries in file
// order. A damaged user entry stays in the file untouched but is not listed;
// Problems explains it.
inline Json Merge(const Json& builtins,const Json& file)
{
    const auto document=Document(file);Json result=Json::array();std::set<std::string> hidden,ids;
    for(const auto& id:document.at("hiddenBuiltins"))hidden.insert(id.get<std::string>());
    for(auto entry:builtins)
    {
        const auto id=entry.at("id").get<std::string>();
        if(hidden.count(id) || !ids.insert(id).second)continue;
        entry["builtin"]=true;result.push_back(std::move(entry));
    }
    for(auto entry:document.at("emitters"))
    {
        try{entry.erase("builtin");Validate(entry);}catch(const std::exception&){continue;}
        const auto id=entry.at("id").get<std::string>();
        if(!IsUserId(id) || !ids.insert(id).second)continue;
        entry["builtin"]=false;result.push_back(std::move(entry));
    }
    return result;
}
inline Json Problems(const Json& file)
{
    const auto document=Document(file);Json result=Json::array();std::set<std::string> ids;
    for(auto entry:document.at("emitters"))
    {
        try
        {
            entry.erase("builtin");Validate(entry);
            if(!IsUserId(entry.at("id").get<std::string>()))throw std::runtime_error("Emitter Library entry '"+entry.at("name").get<std::string>()+"' has a built-in id in the user file.");
            if(!ids.insert(entry.at("id").get<std::string>()).second)throw std::runtime_error("Emitter Library entry '"+entry.at("name").get<std::string>()+"' repeats the id of an earlier entry.");
        }
        catch(const std::exception& e){result.push_back(e.what());}
    }
    return result;
}
// By id, else by name ignoring case (the first match).
inline const Json& Find(const Json& entries,const std::string& key)
{
    for(const auto& entry:entries)if(entry.value("id",std::string{})==key)return entry;
    for(const auto& entry:entries)if(Fold(entry.value("name",std::string{}))==Fold(key))return entry;
    throw std::runtime_error("The Emitter Library has no entry called '"+key+"'. Refresh the list.");
}
// Replaces the user entry with the same id, or adds the entry keeping its user
// id (an entry copied from another library keeps its identity). A draft or a
// built-in is saved as a new user copy with a fresh id. Returns the entry as
// stored.
inline Json Save(Json& file,Json entry)
{
    file=Document(std::move(file));
    if(!entry.is_object())throw std::runtime_error("Nothing to save to the Emitter Library.");
    entry.erase("builtin");auto& list=file["emitters"];
    entry["name"]=CleanName(entry.value("name",std::string{}));
    entry["category"]=CleanCategory(entry.value("category",std::string("Other")),list);
    entry["description"]=CleanDescription(entry.value("description",std::string{}));
    const auto id=entry.contains("id") && entry.at("id").is_string()?entry.at("id").get<std::string>():std::string{};
    auto existing=IsUserId(id)?std::find_if(list.begin(),list.end(),[&](const Json& e){return e.is_object() && e.value("id",std::string{})==id;}):list.end();
    if(!IsUserId(id))entry["id"]=Id();
    entry["modified"]=Timestamp();Validate(entry);
    if(existing!=list.end())*existing=entry;else list.push_back(entry);
    return entry;
}
// Name, category and description of a user entry.
inline Json Update(Json& file,const std::string& id,const Json& changes)
{
    file=Document(std::move(file));
    if(IsBuiltinId(id))throw std::runtime_error("Built-in emitters cannot be renamed or edited. Place one and save it as your own entry to change it.");
    if(!changes.is_object())throw std::runtime_error("Choose what to change.");
    auto& list=file["emitters"];
    auto existing=std::find_if(list.begin(),list.end(),[&](const Json& e){return e.is_object() && e.value("id",std::string{})==id;});
    if(existing==list.end())throw std::runtime_error("The selected emitter no longer exists. Refresh the list.");
    Json entry=*existing;
    for(auto it=changes.begin();it!=changes.end();++it)
    {
        if(!it.value().is_string())throw std::runtime_error("Emitter names, categories and descriptions are text.");
        const auto value=it.value().get<std::string>();
        if(it.key()=="name")entry["name"]=CleanName(value);
        else if(it.key()=="category")entry["category"]=CleanCategory(value,list);
        else if(it.key()=="description")entry["description"]=CleanDescription(value);
        else throw std::runtime_error("Only the name, category and description of an emitter can be changed here.");
    }
    entry["modified"]=Timestamp();Validate(entry);*existing=entry;return entry;
}
// A user entry is removed; a built-in one is hidden until RestoreBuiltins.
inline void Delete(Json& file,const Json& builtins,const std::string& id)
{
    file=Document(std::move(file));
    auto& list=file["emitters"];
    auto existing=std::find_if(list.begin(),list.end(),[&](const Json& e){return e.is_object() && e.value("id",std::string{})==id;});
    if(IsUserId(id) && existing!=list.end()){list.erase(existing);return;}
    if(IsBuiltinId(id) && std::any_of(builtins.begin(),builtins.end(),[&](const Json& e){return e.at("id")==id;}))
    {
        auto& hidden=file["hiddenBuiltins"];
        if(std::find(hidden.begin(),hidden.end(),Json(id))==hidden.end())hidden.push_back(id);
        return;
    }
    throw std::runtime_error("The selected emitter no longer exists. Refresh the list.");
}
inline void RestoreBuiltins(Json& file){file=Document(std::move(file));file["hiddenBuiltins"]=Json::array();}
// Placement-specific actor lines that misbehave in another map: the editor
// group, the platform filter (258 stock emitters are Platform=2, XBOX_Only),
// attachment and zone links by Tag, references to other actors, and the
// editor's own hidden and lock flags.
inline const std::vector<std::string>& PlacementProperties()
{
    static const std::vector<std::string> keys={"Platform","AttachTag","Base","Owner","PhysicsVolume","Group","ForcedVisibilityZoneTag","bHiddenEd","bHiddenEdGroup","bLockLocation"};
    return keys;
}
// A first guess from the textures, meshes and sounds an entry uses.
inline std::string GuessCategory(const Json& entry)
{
    static const std::vector<std::pair<std::string,std::vector<std::string>>> words={
        {"Fire",{"fire","flam","torch","brazier","ember","burn","flare"}},
        {"Smoke",{"smoke","fumee","fume","smog"}},
        {"Steam",{"steam","vapeur","vapor","vapour"}},
        {"Water",{"water","drop","drip","goutte","bubul","bubble","splash","giclure","onde","ripple","spray","embrun","wave","sprinkler"}},
        {"Weather",{"rain","snow","flake","schnee","mist","fog"}},
        {"Sparks & Electrical",{"spark","elec","tazer","hotspot"}},
        {"Dust & Debris",{"dust","debris","poussiere","plafond","chip","bamboo","shrapnel","morceau"}},
        {"Insects",{"butterfly","moth","insect"}},
        {"Lights & Glows",{"corona","glow","beam","halo"}},
        {"Explosions & Bursts",{"explo","frag","blast","boom"}}};
    std::set<std::string> classes;
    for(const auto& actor:entry.value("actors",Json::array()))classes.insert(Fold(actor.value("class",std::string{})));
    std::map<std::string,int> hits;
    for(const auto& dependency:entry.value("dependencies",Json::array()))
    {
        if(!dependency.is_string() || classes.count(Fold(dependency.get<std::string>())))continue;
        const auto path=Fold(dependency.get<std::string>());
        for(const auto& [category,keys]:words)
            if(std::any_of(keys.begin(),keys.end(),[&](const std::string& key){return path.find(key)!=std::string::npos;}))++hits[category];
    }
    std::string best="Other";int most=0;
    for(const auto& [category,keys]:words)if(hits[category]>most){most=hits[category];best=category;}
    return best;
}
// An unsaved entry from a fresh capture of emitters in map (CaptureAssembly
// around their centre): placement lines stripped, a class-default Tag dropped
// so paste gives the new actor its own, canonical names, and a guessed
// category. Rejects map-local assets and links to actors outside the capture.
inline Json Draft(Json captured,const std::string& map)
{
    if(captured.contains("local") && !captured.at("local").empty())
    {
        std::string list;for(const auto& path:captured.at("local"))list+="\n  "+path.get<std::string>();
        throw std::runtime_error("These emitters use assets stored inside this map:"+list+"\n\nEmitter Library entries can only use textures, meshes and sounds from shared packages. Move the assets into a package first.");
    }
    captured.erase("local");
    std::set<std::string> events;std::string source;
    for(const auto& actor:captured.at("actors"))events.insert(Fold(actor.value("event",std::string{})));
    for(auto& actor:captured.at("actors"))
    {
        auto text=actor.at("text").get<std::string>();
        for(const auto& key:PlacementProperties())text=RemoveProperty(text,key);
        // Spawning sets Tag to the class name; only a chosen Tag is kept.
        auto type=actor.at("class").get<std::string>();type=type.substr(type.find_last_of('.')+1);
        const auto tag=actor.value("tag",std::string{});
        if(Fold(tag)==Fold(type) && !events.count(Fold(tag))){text=RemoveProperty(text,"Tag");actor["tag"]="None";}
        actor["text"]=text;
        source+=(source.empty()?"":",")+map+"."+actor.at("name").get<std::string>();
    }
    // A link whose line was stripped goes with it; any other link to an actor
    // outside the capture cannot be placed into another map.
    std::set<std::string> referenced;std::string all;
    for(const auto& actor:captured.at("actors"))
    {
        all+=Fold(actor.at("text").get<std::string>());
        for(const auto& ref:References(actor.at("text").get<std::string>()))referenced.insert(Fold(ref.path));
    }
    std::string links;
    for(const auto& binding:captured.at("bindings"))
        if(binding.value("kind",std::string{})!="object" || referenced.count(Fold(binding.value("path",std::string{}))))links+="\n  "+binding.value("label",std::string{});
    if(!links.empty())throw std::runtime_error("These emitters link to actors outside the selection:"+links+"\n\nClear those links (an Event or an actor reference) or include the linked actors, then save again.");
    captured["bindings"]=Json::array();
    std::set<std::string> classes;for(const auto& actor:captured.at("actors"))classes.insert(Fold(actor.at("class").get<std::string>()));
    Json dependencies=Json::array();
    for(const auto& dependency:captured.at("dependencies"))
        if(classes.count(Fold(dependency.get<std::string>())) || all.find(Fold(dependency.get<std::string>()))!=std::string::npos)dependencies.push_back(dependency);
    captured["dependencies"]=dependencies;
    auto entry=CanonicalizeAssembly(std::move(captured),{});
    entry["name"]="New emitter";entry["category"]=GuessCategory(entry);entry["description"]="";entry["source"]=source;
    return entry;
}
}
