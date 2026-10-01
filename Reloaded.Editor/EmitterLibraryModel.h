#pragma once
#include "WorkflowModel.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

// The Emitter Library: particle effects saved as canonical assembly
// definitions (CaptureAssembly + CanonicalizeAssembly) with a name, category
// and description, so they place into any map through PlaceAssembly: one Undo
// step, unique RE_ names, package auto-load. Engine-independent.
//
// Entry, the contract with the explorer and preview:
//   {"id": "builtin.<slug>" for bundled entries | "pack.<pack id>.<slug>" for effect pack
//          entries | 32 lower-case hex for user entries,
//    "name", "category", "description",
//    "builtin": true|false, "readonly": true for built-in and pack entries,
//    "pack": "<pack id>" for pack entries (these three in memory only, never written),
//    "modified": epoch milliseconds as a string (required for user entries),
//    "source": "<Map>.<Actor>[,...]" (optional),
//    "pivot": [x,y,z], "dependencies": [object paths], "bindings": [] (always empty),
//    "actors": [canonical assembly actor records {name,class,path,sourcePath,text,position,rotation,tag,event}],
//    "preview": {"distance","yaw","pitch","target":[x,y,z]} (optional camera hint)}
// User file, <Editor::Directory()>/emitter_library.json:
//   {"version":1,"emitters":[user entries],"hiddenBuiltins":[built-in ids the user deleted],
//    "hiddenPacks":[pack entry ids the user deleted]}
// Files without hiddenPacks (older editors) read as an empty list; older editors keep
// the key when they rewrite the file.
// Built-in entries are compiled into the DLL (EmitterLibraryDefaults.gen.h) and are
// never written to disk; deleting one only hides it.
// Effect packs are read-only entry sets installed (by the SCCT Map Manager, with the
// packages they use) as <Editor::Directory()>/EmitterPacks/*.json:
//   {"version":1,
//    "pack":{"id":"<[a-z0-9_-]{1,32}>","name":"<1-120 characters>","description":"<optional>",
//            "requires":["<Package>.utx"|".usx"|".uax"|".ukx", ...] (optional)},
//    "emitters":[entries whose ids are "pack.<pack id>.<[a-z0-9_.-]+>", at most 64 characters]}
// Unknown keys are ignored. A pack whose document or header is invalid is not listed;
// a damaged entry is skipped and reported, as a damaged user entry is. Pack entries
// list after the built-ins and before the user's; like built-ins they are never
// written: saving one makes a user copy, editing refuses and deleting hides it.
namespace Workflow::EmitterLibrary
{
inline const std::vector<std::string>& DefaultCategories()
{
    static const std::vector<std::string> categories={"Fire","Smoke","Steam","Water","Weather","Sparks & Electrical","Dust & Debris","Insects","Lights & Glows","Explosions & Bursts","Other"};
    return categories;
}
inline bool IsBuiltinId(const std::string& id){static const std::regex form("builtin\\.[a-z0-9][a-z0-9_.-]{0,62}");return std::regex_match(id,form);}
inline bool IsUserId(const std::string& id){static const std::regex form("[0-9a-f]{32}");return std::regex_match(id,form);}
// An effect pack's own id, and the id of one of its entries.
inline bool IsPackName(const std::string& id){static const std::regex form("[a-z0-9_-]{1,32}");return std::regex_match(id,form);}
inline bool IsPackEntryId(const std::string& id){static const std::regex form("pack\\.[a-z0-9_-]{1,32}\\.[a-z0-9_.-]+");return id.size()<=64 && std::regex_match(id,form);}
// The pack id inside a pack entry id; empty for any other id.
inline std::string PackOfId(const std::string& id){return IsPackEntryId(id)?id.substr(5,id.find('.',5)-5):std::string{};}
// Built-in and pack entries are never written: they are copied, hidden and restored.
inline bool IsReadOnlyId(const std::string& id){return IsBuiltinId(id) || IsPackEntryId(id);}
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
    const auto id=text("id");const bool builtin=IsBuiltinId(id),user=IsUserId(id);
    if(!builtin && !user && !IsPackEntryId(id))fail("its id is not a built-in, effect pack or user id.");
    if(entry.contains("builtin") && (!entry.at("builtin").is_boolean() || entry.at("builtin").get<bool>()!=builtin))fail("its built-in flag does not match its id.");
    if(entry.contains("readonly") && (!entry.at("readonly").is_boolean() || entry.at("readonly").get<bool>()==user))fail("its read-only flag does not match its id.");
    if(entry.contains("pack") && (!entry.at("pack").is_string() || entry.at("pack").get<std::string>()!=PackOfId(id)))fail("its pack does not match its id.");
    try{CleanName(text("name"));CleanCategory(text("category"));CleanDescription(text("description",false));}
    catch(const std::exception& e){fail(e.what());}
    const auto modified=text("modified",user);
    if(!modified.empty() && (modified.size()>20 || modified.find_first_not_of("0123456789")!=std::string::npos))fail("its modified time is not a number of milliseconds.");
    if(!ValidUtf8(text("source",false)))fail("its source is not UTF-8.");
    if(!entry.contains("pivot") || !Detail::Point(entry.at("pivot")))fail("its pivot is not a finite position.");
    if(!entry.contains("bindings") || !entry.at("bindings").is_array())fail("its bindings are missing.");
    if(!entry.at("bindings").empty())fail("it links to actors outside the entry. Emitter Library entries cannot have external bindings.");
    if(!entry.contains("dependencies") || !entry.at("dependencies").is_array())fail("its dependencies are missing.");
    static const std::regex dependencyPath("[A-Za-z0-9_-]+(\\.[A-Za-z0-9_-]+)+");
    for(const auto& dependency:entry.at("dependencies"))
    {
        if(!dependency.is_string() || dependency.get<std::string>().size()>256 || !std::regex_match(dependency.get<std::string>(),dependencyPath))fail("it has an invalid dependency path.");
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
inline Json EmptyDocument(){return {{"version",1},{"emitters",Json::array()},{"hiddenBuiltins",Json::array()},{"hiddenPacks",Json::array()}};}
// A user file that cannot be read, parsed or understood. Listing goes on with
// the built-ins and reports this; editing refuses rather than overwrite it.
inline const char* DamagedFile(){return "The Emitter Library file (emitter_library.json) is damaged or from a newer version, so your saved emitters are not listed. Restore a valid copy or move it aside before editing the library.";}
// The user file as read by ReadDocument (version 1 already checked); missing
// lists are added, wrong types refuse to edit a damaged file.
inline Json Document(Json document)
{
    if(!document.is_object())throw std::runtime_error(DamagedFile());
    for(const char* key:{"emitters","hiddenBuiltins","hiddenPacks"})if(!document.contains(key))document[key]=Json::array();
    bool valid=document.at("emitters").is_array() && document.at("hiddenBuiltins").is_array() && document.at("hiddenPacks").is_array();
    for(const char* key:{"hiddenBuiltins","hiddenPacks"})if(valid)for(const auto& id:document.at(key))valid=valid && id.is_string();
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
        entry["builtin"]=true;entry["readonly"]=true;result.push_back(std::move(entry));
    }
    return result;
}
namespace Detail
{
    // The flags an entry carries in memory only.
    inline void Unflag(Json& entry){if(entry.is_object())for(const char* key:{"builtin","readonly","pack"})entry.erase(key);}
}
// The Packages folder a required package file installs into, by extension.
inline std::string PackageFolder(const std::string& file)
{
    const auto dot=file.rfind('.');const auto extension=Fold(dot==std::string::npos?std::string{}:file.substr(dot));
    if(extension==".utx")return "Textures";
    if(extension==".usx")return "StaticMeshes";
    if(extension==".uax")return "Sounds";
    if(extension==".ukx")return "Animations";
    return {};
}
inline bool IsPackageFile(const std::string& file)
{
    static const std::regex form("[A-Za-z0-9_-]{1,64}\\.[A-Za-z]{3}");
    return std::regex_match(file,form) && !PackageFolder(file).empty();
}
// An effect pack document (see the top of this file) read from file, a name for
// messages. Returns {"id","name","description","requires","emitters":[valid entries
// flagged "builtin":false,"readonly":true,"pack":id],"problems":[one sentence per
// skipped entry]}. Throws a sentence naming file when the pack cannot be listed.
inline Json ReadPack(Json document,const std::string& file)
{
    auto fail=[&](const std::string& why){throw std::runtime_error("Effect pack "+file+" is not listed: "+why);};
    if(!document.is_object())fail("it is not an effect pack document.");
    if(!document.contains("version") || !document.at("version").is_number_integer() || document.at("version").get<int64_t>()!=1)fail("it is from a newer version or is not an effect pack (version 1 expected).");
    if(!document.contains("pack") || !document.at("pack").is_object())fail("its pack header is missing.");
    const auto& header=document.at("pack");
    auto text=[&](const char* key,bool required)->std::string
    {
        if(!header.contains(key)){if(required)fail(std::string("its pack ")+key+" is missing.");return {};}
        if(!header.at(key).is_string())fail(std::string("its pack ")+key+" is not text.");
        return header.at(key).get<std::string>();
    };
    const auto id=text("id",true);
    if(!IsPackName(id))fail("its pack id must be 1 to 32 lower-case letters, digits, '_' or '-'.");
    const auto rawName=text("name",true),rawDescription=text("description",false);std::string name,description;
    try{name=CleanName(rawName);description=CleanDescription(rawDescription);}
    catch(const std::exception& e){fail(std::string("its pack name or description is invalid: ")+e.what());}
    Json packages=Json::array();std::set<std::string> seen;
    if(header.contains("requires"))
    {
        if(!header.at("requires").is_array())fail("its requires list is not a list.");
        for(const auto& package:header.at("requires"))
        {
            if(!package.is_string() || !IsPackageFile(package.get<std::string>()))fail("its requires list has "+package.dump()+", which is not a package file name such as SWRC_effecttex.utx.");
            if(seen.insert(Fold(package.get<std::string>())).second)packages.push_back(package);
        }
    }
    if(!document.contains("emitters") || !document.at("emitters").is_array())fail("its emitters list is missing.");
    Json entries=Json::array(),problems=Json::array();std::set<std::string> ids;
    for(auto& entry:document.at("emitters"))
    {
        try
        {
            // The id first: an entry of another pack or a user entry gets that reason rather than another.
            Detail::Unflag(entry);
            auto field=[&](const char* key){return entry.is_object() && entry.contains(key) && entry.at(key).is_string()?entry.at(key).get<std::string>():std::string{};};
            const auto entryId=field("id"),entryName=field("name");
            const auto prefix="pack."+id+".";
            if(entryId.empty() || entryId.rfind(prefix,0)!=0)throw std::runtime_error("Emitter Library entry '"+entryName+"' is invalid: its id "+(entryId.empty()?std::string("is missing."):entryId+" does not start with "+prefix));
            if(!IsPackEntryId(entryId))throw std::runtime_error("Emitter Library entry '"+entryName+"' is invalid: its id "+entryId+(entryId.size()>64?" is longer than 64 characters.":" may only use a-z, 0-9, '_', '.' and '-' after "+prefix));
            Validate(entry);
            // One spelling per category: a default's, else the first this pack uses.
            entry["category"]=CleanCategory(entry.at("category").get<std::string>(),entries);
            if(!ids.insert(entryId).second)throw std::runtime_error("Emitter Library entry '"+entryName+"' repeats the id of an earlier entry ("+entryId+").");
            entry["builtin"]=false;entry["readonly"]=true;entry["pack"]=id;entries.push_back(std::move(entry));
        }
        catch(const std::exception& e){problems.push_back("Effect pack '"+name+"' ("+file+"): "+e.what());}
    }
    return {{"id",id},{"name",name},{"description",description},{"requires",packages},{"emitters",std::move(entries)},{"problems",std::move(problems)}};
}
// A read pack without its entries: {"id","name","description","requires","entries":count,"problems":count}.
inline Json PackSummary(const Json& pack)
{
    return {{"id",pack.at("id")},{"name",pack.at("name")},{"description",pack.at("description")},{"requires",pack.at("requires")},
            {"entries",pack.at("emitters").size()},{"problems",pack.at("problems").size()}};
}
// The files of a pack's requires list that an entry uses: those whose package
// is the package of one of the entry's dependencies.
inline Json PackNeeds(const Json& entry,const Json& packages)
{
    std::set<std::string> used;
    if(entry.is_object() && entry.contains("dependencies") && entry.at("dependencies").is_array())
        for(const auto& dependency:entry.at("dependencies"))if(dependency.is_string())used.insert(Fold(dependency.get<std::string>().substr(0,dependency.get<std::string>().find('.'))));
    Json result=Json::array();
    if(packages.is_array())for(const auto& file:packages)
        if(file.is_string() && used.count(Fold(file.get<std::string>().substr(0,file.get<std::string>().rfind('.')))))result.push_back(file);
    return result;
}
// "A.utx", "A.utx and B.usx", "A.utx, B.usx and C.utx".
inline std::string PackageList(const Json& files)
{
    std::string out;
    for(size_t i=0;i<files.size();++i)out+=(i==0?"":i+1==files.size()?" and ":", ")+files[i].get<std::string>();
    return out;
}
// The explorer's details line and the placement refusal for a pack entry whose
// required packages are not installed.
inline std::string MissingPackagesText(const Json& missing){return "Needs "+PackageList(missing)+" (install the pack with the SCCT Map Manager)";}
inline std::string MissingPackagesMessage(const std::string& pack,const Json& missing)
{
    return "This effect needs "+PackageList(missing)+", which "+(missing.size()==1?"is":"are")+" not installed. Install the effect pack '"+pack+"' with the SCCT Map Manager, then try again.";
}
// Built-ins the user has not hidden, then the packs' entries the user has not
// hidden (pack by pack, in the order given), then the valid user entries in file
// order. A damaged user entry stays in the file untouched but is not listed;
// Problems explains it. packs are ReadPack results.
inline Json Merge(const Json& builtins,const std::vector<const Json*>& packs,const Json& file)
{
    const auto document=Document(file);Json result=Json::array();std::set<std::string> hidden,ids;
    for(const char* key:{"hiddenBuiltins","hiddenPacks"})for(const auto& id:document.at(key))hidden.insert(id.get<std::string>());
    for(auto entry:builtins)
    {
        const auto id=entry.at("id").get<std::string>();
        if(hidden.count(id) || !ids.insert(id).second)continue;
        entry["builtin"]=true;entry["readonly"]=true;result.push_back(std::move(entry));
    }
    for(const auto* pack:packs)for(const auto& listed:pack->at("emitters"))
    {
        const auto id=listed.at("id").get<std::string>();
        if(hidden.count(id) || !ids.insert(id).second)continue;
        auto entry=listed;entry["builtin"]=false;entry["readonly"]=true;entry["pack"]=pack->at("id");result.push_back(std::move(entry));
    }
    for(auto entry:document.at("emitters"))
    {
        try{Detail::Unflag(entry);Validate(entry);}catch(const std::exception&){continue;}
        const auto id=entry.at("id").get<std::string>();
        if(!IsUserId(id) || !ids.insert(id).second)continue;
        entry["builtin"]=false;entry["readonly"]=false;result.push_back(std::move(entry));
    }
    return result;
}
inline std::vector<const Json*> PackList(const Json& packs)
{
    std::vector<const Json*> list;if(packs.is_array())for(const auto& pack:packs)list.push_back(&pack);
    return list;
}
inline Json Merge(const Json& builtins,const Json& packs,const Json& file){return Merge(builtins,PackList(packs),file);}
inline Json Merge(const Json& builtins,const Json& file){return Merge(builtins,std::vector<const Json*>{},file);}
inline Json Problems(const Json& file)
{
    const auto document=Document(file);Json result=Json::array();std::set<std::string> ids;
    for(auto entry:document.at("emitters"))
    {
        try
        {
            Detail::Unflag(entry);Validate(entry);
            if(!IsUserId(entry.at("id").get<std::string>()))throw std::runtime_error("Emitter Library entry '"+entry.at("name").get<std::string>()+"' has a built-in or effect pack id in the user file.");
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
// id (an entry copied from another library keeps its identity). A draft, a
// built-in or a pack entry is saved as a new user copy with a fresh id. Returns
// the entry as stored.
inline Json Save(Json& file,Json entry)
{
    file=Document(std::move(file));
    if(!entry.is_object())throw std::runtime_error("Nothing to save to the Emitter Library.");
    Detail::Unflag(entry);auto& list=file["emitters"];
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
    if(IsPackEntryId(id))throw std::runtime_error("Effects from an effect pack cannot be renamed or edited. Save a copy of one as your own entry to change it.");
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
// A user entry is removed; a built-in or pack entry is hidden until
// RestoreBuiltins. packs are ReadPack results.
inline void Delete(Json& file,const Json& builtins,const std::vector<const Json*>& packs,const std::string& id)
{
    file=Document(std::move(file));
    auto& list=file["emitters"];
    auto existing=std::find_if(list.begin(),list.end(),[&](const Json& e){return e.is_object() && e.value("id",std::string{})==id;});
    if(IsUserId(id) && existing!=list.end()){list.erase(existing);return;}
    auto hide=[&](const char* key){auto& hidden=file[key];if(std::find(hidden.begin(),hidden.end(),Json(id))==hidden.end())hidden.push_back(id);};
    auto listed=[&](const Json& entries){return std::any_of(entries.begin(),entries.end(),[&](const Json& e){return e.at("id")==id;});};
    if(IsBuiltinId(id) && listed(builtins)){hide("hiddenBuiltins");return;}
    if(IsPackEntryId(id) && std::any_of(packs.begin(),packs.end(),[&](const Json* pack){return pack->at("id")==PackOfId(id) && listed(pack->at("emitters"));})){hide("hiddenPacks");return;}
    throw std::runtime_error("The selected emitter no longer exists. Refresh the list.");
}
inline void Delete(Json& file,const Json& builtins,const Json& packs,const std::string& id){Delete(file,builtins,PackList(packs),id);}
inline void Delete(Json& file,const Json& builtins,const std::string& id){Delete(file,builtins,std::vector<const Json*>{},id);}
// Lists every hidden built-in and pack entry again.
inline void RestoreBuiltins(Json& file){file=Document(std::move(file));file["hiddenBuiltins"]=Json::array();file["hiddenPacks"]=Json::array();}
// Placement-specific actor lines that misbehave in another map: the editor
// group, the platform filter (258 stock emitters are Platform=2, XBOX_Only),
// attachment and zone links by Tag, references to other actors, and the
// editor's own hidden and lock flags.
inline const std::vector<std::string>& PlacementProperties()
{
    static const std::vector<std::string> keys={"Platform","AttachTag","Base","Owner","PhysicsVolume","Group","ForcedVisibilityZoneTag","bHiddenEd","bHiddenEdGroup","bLockLocation"};
    return keys;
}
// Loop: the effect plays again a wait of min..max seconds after every particle of all
// its systems has died. Each emitter actor gets AEmitter's own AutoReset with
// TimeTillResetRange: AEmitter::Tick counts the wait down once all its systems are
// dead (0x110dbec5) and then resets them together (0x110dbf40); while it is set a
// system's own AutoReset is ignored (0x110dbc2b), so the layers stay in step. So that
// every system ends and starts again:
//  - Disabled (waiting for a trigger) is dropped, unless TriggerDisabled=False keeps
//    the system off for good;
//  - AutoDestroy is dropped: a system's disables it when it ends (0x110dbc95), the
//    actor's destroys the actor;
//  - RespawnDeadParticles=False, so a continuous system plays MaxParticles once a round;
//  - a trigger spawner (SpawnOnTriggerRange) spawns its trigger burst at the start of
//    each round instead (MaxParticles, at SpawnOnTriggerPPS or at once).
inline constexpr double MaxLoopWait=3600;
inline void CheckLoopWait(double min,double max)
{
    if(!std::isfinite(min) || !std::isfinite(max) || min<0 || max>MaxLoopWait || min>max)
        throw std::runtime_error("Enter a loop wait between 0 and 3600 seconds, with the first number no larger than the second.");
}
namespace Detail
{
    inline std::string Trimmed(const std::string& s)
    {
        const auto first=s.find_first_not_of(" \t\r\n");
        return first==std::string::npos?std::string():s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);
    }
    // Folded key of a T3D property line: "SizeScale(0)=(...)" -> "sizescale".
    inline std::string LineKey(const std::string& line)
    {
        const auto t=Trimmed(line);const auto end=t.find_first_of("=(");
        return end==std::string::npos?std::string():Fold(Trimmed(t.substr(0,end)));
    }
    inline std::string LineValue(const std::string& line){const auto t=Trimmed(line);const auto at=t.find('=');return at==std::string::npos?std::string():Trimmed(t.substr(at+1));}
    // A number field of a T3D range "(Min=1.0,Max=2.0)"; fallback when absent.
    inline double RangeField(const std::string& value,const char* field,double fallback)
    {
        const std::regex form(std::string("\\b")+field+"=([-+0-9.eE]+)",std::regex::icase);std::smatch match;
        if(!std::regex_search(value,match,form))return fallback;
        try{const double v=std::stod(match[1].str());return std::isfinite(v)?v:fallback;}catch(const std::exception&){return fallback;}
    }
    inline double Number(const std::string& value,double fallback)
    {
        try{size_t used=0;const double v=std::stod(value,&used);return used && std::isfinite(v)?v:fallback;}catch(const std::exception&){return fallback;}
    }
    inline std::string Seconds(double value){char text[32];std::snprintf(text,sizeof(text),"%.6f",value);return text;}
    // One inline particle system: its lines between Begin Object and End Object, each
    // with its depth below the system (0 for the system's own properties).
    struct SystemLine{std::string text;int depth;};
    inline std::vector<std::string> LoopSystem(const std::vector<SystemLine>& lines,const std::string& indent)
    {
        std::map<std::string,std::string> values;
        for(const auto& line:lines)if(!line.depth)if(auto key=LineKey(line.text);!key.empty())values[key]=LineValue(line.text);
        auto value=[&](const char* key){auto found=values.find(key);return found==values.end()?std::string():found->second;};
        const bool keptOff=Fold(value("triggerdisabled"))=="false" && Fold(value("disabled"))=="true";
        const auto spawn=value("spawnontriggerrange");
        // The round's burst is the largest trigger burst, whole particles only.
        const double count=spawn.empty()?0:std::floor(std::max(RangeField(spawn,"Min",0),RangeField(spawn,"Max",0)));
        const bool spawner=count>=1 && count<=100000;
        std::set<std::string> drop{"autodestroy","respawndeadparticles"};
        if(!keptOff)drop.insert("disabled");
        if(spawner)for(const char* key:{"spawnontriggerrange","spawnontriggerpps","maxparticles","automaticinitialspawning","initialparticlespersecond"})drop.insert(key);
        std::vector<std::string> out;
        for(const auto& line:lines)if(line.depth || !drop.count(LineKey(line.text)))out.push_back(line.text);
        out.push_back(indent+"RespawnDeadParticles=False");
        if(spawner)
        {
            const double rate=Number(value("spawnontriggerpps"),0);
            out.push_back(indent+"MaxParticles="+std::to_string(static_cast<long long>(count)));
            out.push_back(indent+"AutomaticInitialSpawning=False");
            out.push_back(indent+"InitialParticlesPerSecond="+Seconds(rate>0?rate:10000));
        }
        return out;
    }
    // An emitter actor's text with the loop applied; unchanged without particle systems.
    inline std::string LoopActor(const std::string& text,double min,double max,bool& systems)
    {
        std::istringstream input(text);std::string line,out,indent;std::vector<SystemLine> block;int depth=0;bool any=false,inside=false;
        while(std::getline(input,line))
        {
            if(!line.empty() && line.back()=='\r')line.pop_back();
            const auto l=Fold(Trimmed(line));
            const bool begin=l.rfind("begin ",0)==0,end=l.rfind("end ",0)==0;
            if(inside)
            {
                if(end && depth==2)
                {
                    for(const auto& kept:LoopSystem(block,indent))out+=kept+"\n";
                    out+=line+"\n";inside=false;--depth;continue;
                }
                // A nested block's own Begin and End lines count as below the system.
                if(end)--depth;
                block.push_back({line,begin || end?depth-1:depth-2});
                if(begin)++depth;
                continue;
            }
            if(begin && ++depth==2 && l.rfind("begin object ",0)==0)
            {
                inside=any=true;block.clear();indent=line.substr(0,line.find_first_not_of(" \t"))+"    ";
            }
            else if(end)--depth;
            out+=line+"\n";
        }
        if(!any)return text;
        systems=true;
        return SetProperty(SetProperty(RemoveProperty(out,"AutoDestroy"),"AutoReset","True"),"TimeTillResetRange","(Min="+Seconds(min)+",Max="+Seconds(max)+")");
    }
}
inline Json Looped(Json entry,double min,double max)
{
    CheckLoopWait(min,max);
    if(!entry.is_object() || !entry.contains("actors") || !entry.at("actors").is_array())throw std::runtime_error("Select an effect to loop.");
    bool systems=false;
    for(auto& actor:entry.at("actors"))if(actor.is_object() && actor.contains("text") && actor.at("text").is_string())
        actor["text"]=Detail::LoopActor(actor.at("text").get<std::string>(),min,max,systems);
    if(!systems)throw std::runtime_error("This effect has no particle systems to loop.");
    return entry;
}
// The longest start delay (InitialDelayRange) of an entry's particle systems, in
// seconds: a looping effect waits this long again at the start of each round.
inline double StartDelay(const Json& entry)
{
    double delay=0;
    if(entry.is_object())for(const auto& actor:entry.value("actors",Json::array()))
    {
        std::istringstream input(actor.value("text",std::string{}));std::string line;
        while(std::getline(input,line))if(Detail::LineKey(line)=="initialdelayrange")
        {
            const auto value=Detail::LineValue(line);
            delay=std::max({delay,Detail::RangeField(value,"Min",0),Detail::RangeField(value,"Max",0)});
        }
    }
    return std::isfinite(delay)?delay:0;
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
