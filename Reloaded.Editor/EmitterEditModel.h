#pragma once
#include "EmitterLibraryModel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

// Editing the particle systems of an Emitter Library entry: the Emitter Library window's
// edit panel reads Settings, and each change goes through Edit, which validates the value
// and rewrites only the property line it names in the actor's T3D text (every other byte
// is kept, CRLF included). Engine-independent.
//
// A sub-emitter is one "Begin Object" block directly inside an actor ("system"); systems
// are numbered across the entry's actors in order. A property line is "<indent>Key=Value"
// directly inside the block; Key keeps its index ("ColorScale(1)"). The editor exports
// only values that differ from the class default, and only the members of a struct that
// differ from the default's members, so a missing line or member reads as the
// ParticleEmitter default below (UE2's: 10 particles, 4 s, size 100, unit tint).
namespace Workflow::EmitterEdit
{
inline constexpr int MaxParticlesCap=2000;           // stock effects use at most 600
inline constexpr double MaxRate=10000;               // particles a second; stock bursts use 10000 for "all at once"
inline constexpr double MaxLifetime=600,MinLifetime=0.01;
inline constexpr double MaxSize=10000,MaxSpeed=50000;
inline constexpr size_t MaxColourKeys=16;
namespace Detail
{
    using Workflow::EmitterLibrary::Detail::Trimmed;
    struct Line{size_t begin=0,end=0,next=0;};       // end excludes the line break; next starts the next line
    struct Block{std::string name,type,indent,eol;Line open;size_t end=0;std::vector<Line> lines;};
    inline std::string Word(const std::string& line,const char* key)
    {
        const std::regex form(std::string("\\b")+key+"=\"?([^\\s\"]+)",std::regex::icase);std::smatch match;
        return std::regex_search(line,match,form)?match[1].str():std::string{};
    }
    // The sub-emitter blocks of an actor's text, with their own property lines.
    inline std::vector<Block> Blocks(const std::string& text)
    {
        std::vector<Block> out;int depth=0;bool inside=false;
        for(size_t at=0;at<text.size();)
        {
            Line line;line.begin=at;auto stop=text.find('\n',at);
            line.next=stop==std::string::npos?text.size():stop+1;line.end=stop==std::string::npos?text.size():stop;
            if(line.end>line.begin && text[line.end-1]=='\r')--line.end;
            at=line.next;
            const auto raw=text.substr(line.begin,line.end-line.begin);const auto l=Fold(Trimmed(raw));
            const bool begin=l.rfind("begin ",0)==0,end=l.rfind("end ",0)==0;
            if(inside)
            {
                if(end && depth==2){out.back().end=line.begin;inside=false;--depth;continue;}
                if(begin){++depth;continue;}
                if(end){--depth;continue;}
                if(depth==2 && raw.find('=')!=std::string::npos)out.back().lines.push_back(line);
                continue;
            }
            if(begin && ++depth==2 && l.rfind("begin object ",0)==0)
            {
                Block block;block.open=line;block.name=Word(raw,"Name");block.type=Word(raw,"Class");
                block.indent=raw.substr(0,raw.find_first_not_of(" \t"))+"    ";
                block.eol=line.next>line.end+1?"\r\n":"\n";
                out.push_back(std::move(block));inside=true;
            }
            else if(end)--depth;
        }
        if(inside)throw std::runtime_error("A particle system's text has no End Object.");
        return out;
    }
    inline std::string LineKey(const std::string& text,const Line& line)
    {
        const auto raw=text.substr(line.begin,line.end-line.begin);
        return Trimmed(raw.substr(0,raw.find('=')));
    }
    inline std::string Base(const std::string& key){return Fold(Trimmed(key.substr(0,key.find('('))));}
    inline const Block& At(const std::vector<Block>& blocks,size_t system)
    {
        if(system>=blocks.size())throw std::runtime_error("The selected particle system no longer exists.");
        return blocks[system];
    }
    inline const Line* Find(const std::string& text,const Block& block,const std::string& key)
    {
        for(const auto& line:block.lines)if(Fold(LineKey(text,line))==Fold(key))return &line;
        return nullptr;
    }
    // Top-level members of a T3D struct "(A=1,B=(X=2))": [("A","1"),("B","(X=2)")].
    inline std::vector<std::pair<std::string,std::string>> Members(const std::string& value)
    {
        std::vector<std::pair<std::string,std::string>> out;const auto v=Trimmed(value);
        if(v.size()<2 || v.front()!='(' || v.back()!=')')return out;
        int depth=0;bool quoted=false;size_t start=1;
        auto take=[&](size_t stop)
        {
            const auto part=v.substr(start,stop-start);const auto equals=part.find('=');
            if(equals!=std::string::npos)out.push_back({Trimmed(part.substr(0,equals)),Trimmed(part.substr(equals+1))});
        };
        for(size_t i=1;i+1<v.size();++i)
        {
            const char c=v[i];
            if(c=='"')quoted=!quoted;
            if(quoted)continue;
            if(c=='(')++depth;else if(c==')')--depth;
            else if(c==',' && !depth){take(i);start=i+1;}
        }
        take(v.size()-1);
        return out;
    }
    inline std::string Struct(const std::vector<std::pair<std::string,std::string>>& members)
    {
        std::string out="(";
        for(const auto& [key,value]:members)out+=(out.size()>1?",":"")+key+"="+value;
        return out+")";
    }
    inline std::optional<std::string> Member(const std::string& value,const std::string& key)
    {
        for(const auto& [k,v]:Members(value))if(Fold(k)==Fold(key))return v;
        return std::nullopt;
    }
    // value with key set (in place, else appended), or removed when replacement is empty.
    inline std::string WithMember(const std::string& value,const std::string& key,const std::string& replacement)
    {
        auto members=Members(value);bool found=false;
        for(auto it=members.begin();it!=members.end();)
        {
            if(Fold(it->first)!=Fold(key)){++it;continue;}
            if(replacement.empty() || found){it=members.erase(it);continue;}
            it->second=replacement;found=true;++it;
        }
        if(!found && !replacement.empty())members.push_back({key,replacement});
        return Struct(members);
    }
    inline double Parse(const std::optional<std::string>& value,double fallback)
    {
        if(!value)return fallback;
        try{size_t used=0;const double v=std::stod(*value,&used);return used && std::isfinite(v)?v:fallback;}catch(const std::exception&){return fallback;}
    }
    // "%.6f" as the editor exports floats; -0 is written as 0.
    inline std::string Float(double value){char text[64];std::snprintf(text,sizeof(text),"%.6f",value==0?0.0:value);return text;}
    inline std::pair<double,double> Range(const std::optional<std::string>& value,double min,double max)
    {
        if(!value)return {min,max};
        return {Parse(Member(*value,"Min"),min),Parse(Member(*value,"Max"),max)};
    }
    inline std::string RangeText(double min,double max){return "(Min="+Float(min)+",Max="+Float(max)+")";}
    inline bool Flag(const std::optional<std::string>& value,bool fallback){return value?Fold(Trimmed(*value))=="true":fallback;}
}
// The raw value of a property line of one particle system of an actor's text.
inline std::optional<std::string> GetValue(const std::string& text,size_t system,const std::string& key)
{
    const auto blocks=Detail::Blocks(text);const auto& block=Detail::At(blocks,system);
    const auto* line=Detail::Find(text,block,key);
    if(!line)return std::nullopt;
    const auto raw=text.substr(line->begin,line->end-line->begin);
    return Detail::Trimmed(raw.substr(raw.find('=')+1));
}
// Sets Key=value: the value of an existing line is replaced in place; a new line goes
// after the last line of the same property (another index of an array), else before the
// block's Name= line, else before End Object, with the block's indentation and line breaks.
inline std::string SetValue(std::string text,size_t system,const std::string& key,const std::string& value)
{
    if(value.find_first_of("\r\n")!=std::string::npos || Detail::Trimmed(key).empty())throw std::runtime_error("A property value cannot span lines.");
    const auto blocks=Detail::Blocks(text);const auto& block=Detail::At(blocks,system);
    if(const auto* line=Detail::Find(text,block,key))
    {
        const auto equals=text.find('=',line->begin)+1;
        return text.replace(equals,line->end-equals,value);
    }
    size_t at=block.end;const Detail::Line* last=nullptr;
    for(const auto& line:block.lines)if(Detail::Base(Detail::LineKey(text,line))==Detail::Base(key))last=&line;
    if(last)at=last->next;
    else for(const auto& line:block.lines)if(Fold(Detail::LineKey(text,line))=="name"){at=line.begin;break;}
    std::string indent=block.indent;
    if(!block.lines.empty()){const auto& first=block.lines.front();const auto raw=text.substr(first.begin,first.end-first.begin);indent=raw.substr(0,raw.find_first_not_of(" \t"));}
    return text.insert(at,indent+key+"="+value+block.eol);
}
inline std::string RemoveValue(std::string text,size_t system,const std::string& key)
{
    const auto blocks=Detail::Blocks(text);const auto& block=Detail::At(blocks,system);
    if(const auto* line=Detail::Find(text,block,key))text.erase(line->begin,line->next-line->begin);
    return text;
}
// Number of particle systems in an actor's text.
inline size_t SystemCount(const std::string& text){return Detail::Blocks(text).size();}

// A number typed into the edit panel; what names the field in the refusal.
inline double ParseNumber(const std::string& text,const std::string& what)
{
    const auto t=Detail::Trimmed(text);size_t used=0;double value=0;
    try{value=std::stod(t,&used);}catch(const std::exception&){used=0;}
    if(t.empty() || used!=t.size() || !std::isfinite(value))throw std::runtime_error("Enter a number for "+what+".");
    return value;
}
// A number as the edit panel shows it: at most four decimals, no trailing zeros.
inline std::string Shown(double value)
{
    char text[64];std::snprintf(text,sizeof(text),"%.4f",std::abs(value)<0.00005?0.0:value);
    std::string out=text;
    if(out.find('.')!=std::string::npos){while(out.back()=='0')out.pop_back();if(out.back()=='.')out.pop_back();}
    return out;
}
namespace Detail
{
    struct Where{size_t actor,system;};
    inline std::vector<Where> Index(const Json& entry)
    {
        if(!entry.is_object() || !entry.contains("actors") || !entry.at("actors").is_array())throw std::runtime_error("Select an effect to edit.");
        std::vector<Where> out;
        for(size_t a=0;a<entry.at("actors").size();++a)
        {
            const auto& actor=entry.at("actors")[a];
            if(!actor.is_object() || !actor.contains("text") || !actor.at("text").is_string())continue;
            for(size_t s=0,n=SystemCount(actor.at("text").get<std::string>());s<n;++s)out.push_back({a,s});
        }
        return out;
    }
    // Texture value "Texture'Pkg.Group.Name'" -> class and path; None -> empty.
    inline std::pair<std::string,std::string> Reference(const std::string& value)
    {
        const auto quote=value.find('\'');
        if(quote==std::string::npos)return {"",Fold(Trimmed(value))=="none"?std::string{}:Trimmed(value)};
        const auto close=value.find('\'',quote+1);
        return {Trimmed(value.substr(0,quote)),value.substr(quote+1,close==std::string::npos?std::string::npos:close-quote-1)};
    }
    inline std::array<int,4> Colour(const std::optional<std::string>& key)
    {
        std::array<int,4> out{};if(!key)return out;
        const auto colour=Member(*key,"Color");if(!colour)return out;
        const char* channels[]={"R","G","B","A"};
        for(int i=0;i<4;++i)out[i]=static_cast<int>(std::clamp(std::lround(Parse(Member(*colour,channels[i]),0)),0L,255L));
        return out;
    }
    // FColor as the editor exports it: B, G, R, A, zero channels left out.
    inline std::string ColourText(const std::array<int,4>& rgba)
    {
        std::vector<std::pair<std::string,std::string>> members;
        const std::pair<const char*,int> order[]={{"B",rgba[2]},{"G",rgba[1]},{"R",rgba[0]},{"A",rgba[3]}};
        for(const auto& [name,value]:order)if(value)members.push_back({name,std::to_string(value)});
        return members.empty()?std::string{}:Struct(members);
    }
    inline int ColourKeys(const std::string& text,size_t system)
    {
        const auto blocks=Blocks(text);const auto& block=At(blocks,system);int count=0;
        static const std::regex form("colorscale\\s*\\(\\s*(\\d+)\\s*\\)",std::regex::icase);
        for(const auto& line:block.lines)
        {
            std::smatch match;const auto key=LineKey(text,line);
            if(std::regex_match(key,match,form))count=std::max(count,std::min(static_cast<int>(MaxColourKeys),std::stoi(match[1].str())+1));
        }
        return count;
    }
    inline std::string TextureName(const std::string& path){const auto dot=path.find_last_of('.');return dot==std::string::npos?path:path.substr(dot+1);}
}
// The edit panel's view of every particle system of entry, in order:
// [{"actor","system","name","type","label","maxParticles","particlesPerSecond",
//   "initialParticlesPerSecond" (null = automatic),"lifetime":[min,max],"size":[min,max],
//   "height":[min,max] (null while UniformSize, when only X counts),"velocity":[[min,max] x3],
//   "acceleration":[x,y,z],"tint":[r,g,b],"useColorScale","colours":[[r,g,b,a],...],"texture"}]
inline Json Settings(const Json& entry)
{
    using namespace Detail;
    Json out=Json::array();const auto where=Index(entry);
    for(size_t i=0;i<where.size();++i)
    {
        const auto& actor=entry.at("actors")[where[i].actor];const auto text=actor.at("text").get<std::string>();const auto s=where[i].system;
        const auto blocks=Blocks(text);const auto& block=blocks[s];
        auto value=[&](const char* key){return GetValue(text,s,key);};
        const auto sizes=value("StartSizeRange"),velocity=value("StartVelocityRange"),acceleration=value("Acceleration"),tint=value("ColorMultiplierRange");
        auto axis=[&](const std::optional<std::string>& range,const char* name,double fallback)
        {
            const auto member=range?Member(*range,name):std::nullopt;
            const auto [min,max]=Range(member,fallback,fallback);return Json::array({min,max});
        };
        const auto lifetime=Range(value("LifetimeRange"),4,4);
        const bool uniform=Flag(value("UniformSize"),false);
        Json tintRgb=Json::array();
        for(const char* name:{"X","Y","Z"})
        {
            const auto [min,max]=Range(tint?Member(*tint,name):std::nullopt,1,1);
            tintRgb.push_back(static_cast<int>(std::clamp(std::lround((min+max)*0.5*255),0L,255L)));
        }
        Json colours=Json::array();
        for(int k=0,n=ColourKeys(text,s);k<n;++k){const auto c=Colour(value(("ColorScale("+std::to_string(k)+")").c_str()));colours.push_back({c[0],c[1],c[2],c[3]});}
        const auto texture=Reference(value("Texture").value_or("None")).second;
        const bool automatic=Flag(value("AutomaticInitialSpawning"),true);
        std::string label=std::to_string(i+1)+". "+(texture.empty()?block.name:TextureName(texture));
        if(entry.at("actors").size()>1)label+=" ("+actor.value("name",std::string("actor"))+")";
        out.push_back({{"actor",where[i].actor},{"system",s},{"name",block.name},{"type",block.type},{"label",label},
            {"maxParticles",static_cast<int>(std::lround(Parse(value("MaxParticles"),10)))},
            {"particlesPerSecond",Parse(value("ParticlesPerSecond"),0)},
            {"initialParticlesPerSecond",automatic?Json():Json(Parse(value("InitialParticlesPerSecond"),0))},
            {"lifetime",{lifetime.first,lifetime.second}},{"size",axis(sizes,"X",100)},{"height",uniform?Json():axis(sizes,"Y",100)},
            {"velocity",{axis(velocity,"X",0),axis(velocity,"Y",0),axis(velocity,"Z",0)}},
            {"acceleration",{Parse(acceleration?Member(*acceleration,"X"):std::nullopt,0),Parse(acceleration?Member(*acceleration,"Y"):std::nullopt,0),Parse(acceleration?Member(*acceleration,"Z"):std::nullopt,0)}},
            {"tint",tintRgb},{"useColorScale",Flag(value("UseColorScale"),false)},{"colours",colours},{"texture",texture}});
    }
    return out;
}
namespace Detail
{
    inline double Number(const Json& value,const std::string& what)
    {
        if(!value.is_number() || !std::isfinite(value.get<double>()))throw std::runtime_error("Enter a number for "+what+".");
        return value.get<double>();
    }
    inline std::pair<double,double> Pair(const Json& value,const std::string& what,double low,double high)
    {
        if(!value.is_array() || value.size()!=2)throw std::runtime_error("Enter the smallest and largest "+what+".");
        const double min=Number(value[0],what),max=Number(value[1],what);
        if(min<low || max>high)throw std::runtime_error("Enter "+what+" between "+Shown(low)+" and "+Shown(high)+".");
        if(min>max)throw std::runtime_error("The first "+what+" must be no larger than the second.");
        return {min,max};
    }
    inline std::array<int,3> Rgb(const Json& value)
    {
        if(!value.is_array() || value.size()!=3)throw std::runtime_error("Choose a colour.");
        std::array<int,3> out{};
        for(int i=0;i<3;++i){if(!value[i].is_number_integer() || value[i].get<int64_t>()<0 || value[i].get<int64_t>()>255)throw std::runtime_error("Colour channels run from 0 to 255.");out[i]=static_cast<int>(value[i].get<int64_t>());}
        return out;
    }
    // A sub-emitter's texture, as typed ("Pkg.Group.Name") or with its class ("Texture'Pkg.Group.Name'").
    inline std::pair<std::string,std::string> TextureValue(const Json& value)
    {
        if(!value.is_string())throw std::runtime_error("Enter a texture such as sfx.Emitter.smoke_grenade.");
        const auto text=Trimmed(value.get<std::string>());
        static const std::regex typed("(?:([A-Za-z_][A-Za-z0-9_]*)'([^']*)')|([^']*)");std::smatch match;
        if(!std::regex_match(text,match,typed))throw std::runtime_error("Enter a texture such as sfx.Emitter.smoke_grenade.");
        const std::string type=match[1].matched?match[1].str():std::string("Texture"),path=match[1].matched?match[2].str():match[3].str();
        static const std::regex form("[A-Za-z0-9_-]+(\\.[A-Za-z0-9_-]+){1,3}");
        if(path.size()>200 || !std::regex_match(path,form))throw std::runtime_error("Enter a texture as Package.Group.Name, such as sfx.Emitter.smoke_grenade.");
        if(Fold(path).rfind("mylevel.",0)==0)throw std::runtime_error("Choose a texture from a shared package; "+path+" is stored inside a map.");
        return {type,path};
    }
    inline bool Uses(const Json& entry,const std::string& path)
    {
        const auto wanted=Fold(path);
        for(const auto& actor:entry.at("actors"))if(Fold(actor.value("text",std::string{})).find("'"+wanted+"'")!=std::string::npos)return true;
        return false;
    }
}
// Fields of Edit: "maxParticles" (whole number), "particlesPerSecond" (0 = automatic,
// MaxParticles over the lifetime), "initialParticlesPerSecond" (null = automatic),
// "lifetime"/"size"/"height"/"velocityX"/"velocityY"/"velocityZ" ([min,max]),
// "acceleration" ([x,y,z]), "tint" ([r,g,b], ColorMultiplierRange), "colour"
// ({"index":k,"rgb":[r,g,b]}, ColorScale(k)'s colour, its alpha and time kept) and
// "texture" (path, or Class'path'; the entry's dependencies follow it).
// Returns the edited entry; throws a sentence for a value it refuses.
inline Json Edit(Json entry,size_t index,const std::string& field,const Json& value)
{
    using namespace Detail;
    const auto where=Index(entry);
    if(index>=where.size())throw std::runtime_error("The selected particle system no longer exists.");
    auto& actor=entry["actors"][where[index].actor];auto text=actor.at("text").get<std::string>();const auto s=where[index].system;
    auto set=[&](const char* key,const std::string& v){text=SetValue(text,s,key,v);};
    auto vectorAxis=[&](const char* key,const char* axis,const std::pair<double,double>& range)
    {
        set(key,WithMember(GetValue(text,s,key).value_or("()"),axis,RangeText(range.first,range.second)));
    };
    if(field=="maxParticles")
    {
        const double n=Number(value,"the particle count");
        if(n!=std::floor(n) || n<1 || n>MaxParticlesCap)throw std::runtime_error("Enter a whole number of particles from 1 to "+std::to_string(MaxParticlesCap)+".");
        set("MaxParticles",std::to_string(static_cast<int>(n)));
    }
    else if(field=="particlesPerSecond")
    {
        const double rate=Number(value,"particles a second");
        if(rate<0 || rate>MaxRate)throw std::runtime_error("Enter particles a second from 0 (automatic) to "+Shown(MaxRate)+".");
        text=rate==0?RemoveValue(text,s,"ParticlesPerSecond"):SetValue(text,s,"ParticlesPerSecond",Float(rate));
    }
    else if(field=="initialParticlesPerSecond")
    {
        if(value.is_null()){text=RemoveValue(RemoveValue(text,s,"InitialParticlesPerSecond"),s,"AutomaticInitialSpawning");}
        else
        {
            const double rate=Number(value,"the starting rate");
            if(rate<0 || rate>MaxRate)throw std::runtime_error("Enter a starting rate from 0 to "+Shown(MaxRate)+" particles a second, or leave it empty for automatic.");
            set("AutomaticInitialSpawning","False");set("InitialParticlesPerSecond",Float(rate));
        }
    }
    else if(field=="lifetime"){const auto r=Pair(value,"lifetime",MinLifetime,MaxLifetime);set("LifetimeRange",RangeText(r.first,r.second));}
    else if(field=="size")vectorAxis("StartSizeRange","X",Pair(value,"size",0,MaxSize));
    else if(field=="height")
    {
        if(Flag(GetValue(text,s,"UniformSize"),false))throw std::runtime_error("This particle system has uniform size: its size sets the height too.");
        vectorAxis("StartSizeRange","Y",Pair(value,"height",0,MaxSize));
    }
    else if(field=="velocityX" || field=="velocityY" || field=="velocityZ")
    {
        const std::string axis=field.substr(8);
        vectorAxis("StartVelocityRange",axis.c_str(),Pair(value,axis+" velocity",-MaxSpeed,MaxSpeed));
    }
    else if(field=="acceleration")
    {
        if(!value.is_array() || value.size()!=3)throw std::runtime_error("Enter the acceleration along X, Y and Z.");
        std::vector<std::pair<std::string,std::string>> members;const char* axes[]={"X","Y","Z"};
        for(int i=0;i<3;++i)
        {
            const double v=Number(value[i],"the acceleration");
            if(std::abs(v)>MaxSpeed)throw std::runtime_error("Enter an acceleration between "+Shown(-MaxSpeed)+" and "+Shown(MaxSpeed)+".");
            if(v!=0)members.push_back({axes[i],Float(v)});
        }
        text=members.empty()?RemoveValue(text,s,"Acceleration"):SetValue(text,s,"Acceleration",Struct(members));
    }
    else if(field=="tint")
    {
        const auto rgb=Rgb(value);
        if(rgb[0]==255 && rgb[1]==255 && rgb[2]==255)text=RemoveValue(text,s,"ColorMultiplierRange");
        else
        {
            std::vector<std::pair<std::string,std::string>> members;const char* axes[]={"X","Y","Z"};
            for(int i=0;i<3;++i){const double m=rgb[i]/255.0;members.push_back({axes[i],RangeText(m,m)});}
            set("ColorMultiplierRange",Struct(members));
        }
    }
    else if(field=="colour")
    {
        if(!value.is_object() || !value.contains("index") || !value.at("index").is_number_integer())throw std::runtime_error("Choose which colour to change.");
        const auto k=value.at("index").get<int64_t>();
        if(k<0 || k>=static_cast<int64_t>(MaxColourKeys))throw std::runtime_error("This particle system has no colour "+std::to_string(k+1)+".");
        const auto rgb=Rgb(value.value("rgb",Json()));
        const auto key="ColorScale("+std::to_string(k)+")";const auto old=GetValue(text,s,key);
        const auto kept=Colour(old);
        const auto colour=ColourText({rgb[0],rgb[1],rgb[2],kept[3]});
        const auto updated=WithMember(old.value_or("()"),"Color",colour);
        // A key left without members would read as nothing; it stays explicit so the array keeps its length.
        if(updated!="()")text=SetValue(text,s,key,updated);
        else if(old)text=SetValue(text,s,key,"(RelativeTime="+Float(0)+")");
    }
    else if(field=="texture")
    {
        const auto [type,path]=TextureValue(value);
        const auto previous=Reference(GetValue(text,s,"Texture").value_or("None")).second;
        set("Texture",type+"'"+path+"'");
        actor["text"]=text;
        auto& dependencies=entry["dependencies"];
        if(!dependencies.is_array())dependencies=Json::array();
        if(std::none_of(dependencies.begin(),dependencies.end(),[&](const Json& d){return d.is_string() && Fold(d.get<std::string>())==Fold(path);}))dependencies.push_back(path);
        if(!previous.empty() && Fold(previous)!=Fold(path) && !Uses(entry,previous))
            dependencies.erase(std::remove_if(dependencies.begin(),dependencies.end(),[&](const Json& d){return d.is_string() && Fold(d.get<std::string>())==Fold(previous);}),dependencies.end());
        return entry;
    }
    else throw std::runtime_error("The edit panel cannot change "+field+".");
    actor["text"]=text;
    return entry;
}
// True when edited differs from original in any actor text or dependency.
inline bool Changed(const Json& original,const Json& edited)
{
    return original.value("actors",Json::array())!=edited.value("actors",Json::array()) || original.value("dependencies",Json::array())!=edited.value("dependencies",Json::array());
}
// A copy of an edited entry to save as the user's own: no id (Save gives it a fresh one),
// none of the in-memory flags, and the name, category and description chosen.
inline Json NewEffect(Json edited,const Json& details)
{
    if(!edited.is_object())throw std::runtime_error("Select an effect to save.");
    for(const char* key:{"id","modified","builtin","readonly","pack"})edited.erase(key);
    if(details.is_object())for(const char* key:{"name","category","description"})if(details.contains(key))edited[key]=details.at(key);
    return edited;
}
}
