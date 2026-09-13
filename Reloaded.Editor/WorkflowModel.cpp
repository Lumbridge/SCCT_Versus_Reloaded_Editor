#include "WorkflowModel.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace Workflow
{
namespace
{
    std::string Trim(std::string s)
    {
        auto first = s.find_first_not_of(" \t\r\n");
        return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
    }
    std::string Token(const std::string& header, const std::string& key)
    {
        std::smatch m;
        if (!std::regex_search(header, m, std::regex("(?:^|\\s)" + key + "=(\"[^\"]*\"|[^\\s]+)", std::regex::icase))) return {};
        auto v = m[1].str();
        return v.front() == '"' ? v.substr(1, v.size() - 2) : v;
    }
    using Matrix = std::array<Vector, 3>;
    constexpr double radians = 6.2831853071795864769 / 65536.0;
    Matrix Axes(const Rotation& r)
    {
        double sp = sin(r[0]*radians), cp = cos(r[0]*radians), sy = sin(r[1]*radians), cy = cos(r[1]*radians), sr = sin(r[2]*radians), cr = cos(r[2]*radians);
        return {{{cp*cy, sr*sp*cy-cr*sy, -(cr*sp*cy+sr*sy)},
                 {cp*sy, sr*sp*sy+cr*cy, cy*sr-cr*sp*sy}, {sp, -sr*cp, cr*cp}}};
    }
    Matrix Transpose(Matrix a) { for (int i=0;i<3;++i) for(int j=i+1;j<3;++j) std::swap(a[i][j],a[j][i]); return a; }
    Vector Multiply(const Matrix& a, const Vector& v)
    {
        Vector out{};
        for(int i=0;i<3;++i) for(int j=0;j<3;++j) out[i]+=a[i][j]*v[j];
        return out;
    }
}
std::string Fold(std::string v) { for(auto& c:v) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return v; }
std::string Id()
{
    static std::mt19937_64 rng(std::random_device{}());
    std::ostringstream s; s << std::hex << std::setfill('0') << std::setw(16) << rng() << std::setw(16) << rng(); return s.str();
}
std::string Timestamp()
{
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
Json ReadDocument(const std::filesystem::path& path, const Json& empty)
{
    if (!std::filesystem::exists(path)) return empty;
    if (std::filesystem::file_size(path)>64*1024*1024) throw std::runtime_error("Workflow document exceeds 64 MiB: " + path.string());
    std::ifstream input(path, std::ios::binary);
    if(!input) throw std::runtime_error("Cannot read " + path.string());
    Json value; input >> value;
    if (!value.is_object() || value.value("version",0)!=1) throw std::runtime_error("Unsupported workflow document version: " + path.string());
    return value;
}
void WriteDocument(const std::filesystem::path& path, const Json& document)
{
    std::filesystem::create_directories(path.parent_path());
    auto temp=path; temp += "."+Id()+".tmp";
    try
    {
        const std::string text=document.dump(2);
        if(text.size()>64*1024*1024) throw std::runtime_error("Workflow library exceeds the 64 MiB limit; the previous entry is unchanged.");
#ifdef _WIN32
        HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create workflow document.");
        DWORD written=0;
        bool ok=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr) && written==text.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if(!ok || !MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not save workflow document; the previous entry is unchanged.");
#else
        std::ofstream output(temp,std::ios::binary); output << text; output.close();
        if(!output) throw std::runtime_error("Cannot write workflow document.");
        std::filesystem::rename(temp,path);
#endif
    }
    catch(...) { std::error_code ignored; std::filesystem::remove(temp,ignored); throw; }
}
void UpdateEntry(Json& entries, const std::string& id, Json replacement)
{
    if(!entries.is_array()) throw std::runtime_error("Invalid workflow entries.");
    for(auto& entry:entries) if(entry.at("id")==id)
    {
        replacement["id"]=entry.at("id"); replacement["name"]=entry.at("name"); replacement["modified"]=Timestamp();
        entry=std::move(replacement); return;
    }
    throw std::runtime_error("The selected entry no longer exists. Refresh the list.");
}
std::vector<ActorText> ParseActors(const std::string& text)
{
    if(text.size()>64*1024*1024) throw std::runtime_error("Assembly text exceeds 64 MiB.");
    std::vector<ActorText> out; std::istringstream input(text); std::string line; int depth=0; std::set<std::string> names;
    while(std::getline(input,line))
    {
        auto t=Trim(line), lower=Fold(t);
        if(lower.rfind("begin actor ",0)==0)
        {
            if(depth) throw std::runtime_error("Nested actor declaration.");
            ActorText a{Token(t,"Name"),Token(t,"Class"),{}};
            if(a.name.empty() || a.type.empty() || !names.insert(Fold(a.name)).second) throw std::runtime_error("Missing or duplicate actor identity.");
            out.push_back(std::move(a)); depth=1;
        }
        else if(depth && lower.rfind("begin ",0)==0) ++depth;
        else if(depth && lower.rfind("end ",0)==0) --depth;
        if(depth || lower=="end actor")
        {
            if(out.empty()) throw std::runtime_error("Unexpected actor terminator.");
            out.back().text += line+"\n";
        }
        if(out.size()>10000) throw std::runtime_error("An assembly may contain at most 10000 actors.");
    }
    if(depth || out.empty()) throw std::runtime_error("No complete actors were exported.");
    return out;
}
std::string NormalizeExportNames(const std::string& text)
{
    std::string out;bool quoted=false,escaped=false;
    for(size_t i=0;i<text.size();++i)
    {
        size_t marker=0;auto c=static_cast<unsigned char>(text[i]);
        if(!quoted && c==0xa7) marker=1;
        if(!quoted && c==0xc2 && i+1<text.size() && static_cast<unsigned char>(text[i+1])==0xa7) marker=2;
        if(marker && i+marker+2<text.size() && text[i+marker]=='(')
        {
            size_t end=i+marker+1;while(end<text.size() && std::isdigit(static_cast<unsigned char>(text[end])))++end;
            if(end>i+marker+1 && end<text.size() && text[end]==')'){i=end;continue;}
        }
        out+=text[i];if(text[i]=='"' && !escaped)quoted=!quoted;
        if(quoted && text[i]=='\\' && !escaped)escaped=true;else escaped=false;
    }
    return out;
}
std::vector<Reference> References(const std::string& text)
{
    std::vector<Reference> refs; bool quoted=false;
    for(size_t i=0;i<text.size();++i)
    {
        if(text[i]=='"' && (i==0 || text[i-1]!='\\')) quoted=!quoted;
        if(quoted || text[i]!='\'') continue;
        size_t start=i;
        while(start && (std::isalnum(static_cast<unsigned char>(text[start-1])) || text[start-1]=='_' || text[start-1]=='.')) --start;
        if(start==i) continue;
        auto end=text.find('\'',i+1);
        if(end==std::string::npos) throw std::runtime_error("Unclosed typed object reference.");
        auto path=text.substr(i+1,end-i-1);
        if(path.size()>=2 && path.front()=='"' && path.back()=='"') path=path.substr(1,path.size()-2);
        refs.push_back({text.substr(start,i-start),path,start,end+1}); i=end;
    }
    return refs;
}
std::string RewriteReferences(const std::string& text, const std::map<std::string,std::string>& paths)
{
    auto refs=References(text); std::string result=text;
    for(auto i=refs.rbegin();i!=refs.rend();++i)
    {
        std::string replacement; bool found=false; auto path=Fold(i->path);
        // Longest prefix wins so actor-owned nested objects keep their suffixes.
        size_t best=0;
        for(const auto& entry:paths)
        {
            auto key=Fold(entry.first);
            if(key.size()<best || (path!=key && !(path.size()>key.size() && path.compare(0,key.size(),key)==0 && path[key.size()]=='.'))) continue;
            best=key.size(); found=true;
            replacement=entry.second.empty() ? "None" : i->type+"'\""+entry.second+i->path.substr(key.size())+"\"'";
        }
        if(found) result.replace(i->begin,i->end-i->begin,replacement);
    }
    return result;
}
std::string Property(const std::string& text, const std::string& key)
{
    std::istringstream input(text); std::string line; int depth=0;
    while(std::getline(input,line))
    {
        auto t=Trim(line), l=Fold(t);
        if(l.rfind("begin ",0)==0) ++depth;
        else if(l.rfind("end ",0)==0) --depth;
        else if(depth==1 && l.rfind(Fold(key)+"=",0)==0) return t.substr(key.size()+1);
    }
    return {};
}
std::string SetProperty(std::string text,const std::string& key,const std::string& value)
{
    std::istringstream input(text); std::string line,out; int depth=0; bool written=false;
    while(std::getline(input,line))
    {
        auto l=Fold(Trim(line));
        if(l.rfind("begin ",0)==0) ++depth;
        if(depth==1 && (l.rfind(Fold(key)+"=",0)==0 || l=="end actor"))
        {
            if(!written) { out+="    "+key+"="+value+"\n"; written=true; }
            if(l!="end actor") continue;
        }
        if(l.rfind("end ",0)==0) --depth;
        out+=line+"\n";
    }
    return out;
}
std::string RemoveProperty(const std::string& text,const std::string& key)
{
    std::istringstream input(text);std::string line,out;int depth=0;
    while(std::getline(input,line))
    {
        auto l=Fold(Trim(line));
        if(l.rfind("begin ",0)==0)++depth;
        else if(l.rfind("end ",0)==0)--depth;
        else if(depth==1 && l.rfind(Fold(key)+"=",0)==0)continue;
        out+=line+"\n";
    }
    return out;
}
std::string RenameObjects(const std::string& text,const std::string& prefix)
{
    std::istringstream input(text); std::string line,out;
    const std::regex name("(\\bName=)(\"[^\"]*\"|[^\\s]+)",std::regex::icase);
    while(std::getline(input,line))
    {
        auto l=Fold(Trim(line));
        if(l.rfind("begin actor ",0)==0 || l.rfind("begin object ",0)==0 || l.rfind("begin brush ",0)==0)
        {
            std::smatch match;
            if(std::regex_search(line,match,name))
            {
                auto n=match[2].str(); if(n.front()=='"') n=n.substr(1,n.size()-2);
                line.replace(static_cast<size_t>(match.position(2)),static_cast<size_t>(match.length(2)),prefix+n);
            }
        }
        out+=line+"\n";
    }
    return out;
}
Json CanonicalizeAssembly(Json definition,const std::map<std::string,std::string>& memberNames)
{
    // Instance prefixes are insertion details, not part of a library identity.
    // Removing them prevents names and tag groups growing on every edit/save cycle.
    auto unprefix=[](std::string value) {
        const std::regex prefix("^RE_[0-9a-fA-F]{12}_");
        while(std::regex_search(value,prefix)) value.erase(0,16);
        return value;
    };
    std::map<std::string,std::string> paths,names; std::set<std::string> used;
    for(auto& actor:definition.at("actors"))
    {
        auto source=actor.at("path").get<std::string>(); auto found=memberNames.find(source);
        auto name=unprefix(found==memberNames.end()?actor.at("name").get<std::string>():found->second);
        auto base=name;int suffix=1;while(!used.insert(Fold(name)).second) name=base+"_"+std::to_string(suffix++);
        names[source]=name; paths[source]="Assembly."+name;
    }
    const std::regex actorName("(Begin Actor[^\\r\\n]*\\bName=)(\"[^\"]*\"|[^\\s]+)",std::regex::icase);
    std::map<std::string,std::string> tags; std::set<std::string> usedTags;
    for(const auto& actor:definition.at("actors"))
    {
        auto tag=actor.value("tag",std::string{});
        if(!tag.empty() && Fold(tag)!="none" && !tags.count(Fold(tag)))
        {
            auto value=unprefix(tag),base=value;int suffix=1;
            while(!usedTags.insert(Fold(value)).second) value=base+"_"+std::to_string(suffix++);
            tags[Fold(tag)]=value;
        }
    }
    for(auto& actor:definition.at("actors"))
    {
        auto path=actor.at("path").get<std::string>();auto name=names.at(path);auto text=actor.at("text").get<std::string>();
        std::smatch match;
        if(!std::regex_search(text,match,actorName)) throw std::runtime_error("Assembly actor declaration is missing.");
        text.replace(static_cast<size_t>(match.position(2)),static_cast<size_t>(match.length(2)),name);
        for(const auto& property:{"Tag","Event"})
        {
            auto key=Fold(property);auto value=actor.value(key,std::string{});auto tag=tags.find(Fold(value));
            if(tag!=tags.end()) {actor[key]=tag->second;text=SetProperty(text,property,tag->second);}
        }
        actor["sourcePath"]=path;actor["name"]=name;actor["path"]=paths.at(path);actor["text"]=RewriteReferences(text,paths);
    }
    return definition;
}
Vector TransformPoint(const Vector& point,const Pose& frame,bool inverse)
{
    for(double x:point) if(!std::isfinite(x)) throw std::runtime_error("Non-finite assembly coordinates.");
    Vector v=point;
    if(inverse) { for(int i=0;i<3;++i) v[i]-=frame.position[i]; return Multiply(Transpose(Axes(frame.rotation)),v); }
    v=Multiply(Axes(frame.rotation),v); for(int i=0;i<3;++i) v[i]+=frame.position[i]; return v;
}
Rotation TransformRotation(const Rotation& r,const Rotation& frame,bool inverse)
{
    Matrix a=Axes(frame), b=Axes(r), c{}; if(inverse) a=Transpose(a);
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) for(int k=0;k<3;++k) c[i][j]+=a[i][k]*b[k][j];
    const double pitch=asin(std::clamp(c[2][0],-1.0,1.0));
    const bool pole=std::abs(cos(pitch))<1e-7;
    double yaw=pole ? atan2(-c[0][1],c[1][1]) : atan2(c[1][0],c[0][0]);
    double roll=pole ? 0 : atan2(-c[2][1],c[2][2]);
    return {static_cast<int>(std::lround(pitch/radians)),static_cast<int>(std::lround(yaw/radians)),static_cast<int>(std::lround(roll/radians))};
}
std::string VectorText(const Vector& v) { std::ostringstream s; s<<std::setprecision(9)<<"(X="<<v[0]<<",Y="<<v[1]<<",Z="<<v[2]<<")"; return s.str(); }
std::string RotationText(const Rotation& v) { return "(Pitch="+std::to_string(v[0])+",Yaw="+std::to_string(v[1])+",Roll="+std::to_string(v[2])+")"; }
Json PreparePlacement(const Json& definition,const Pose& frame,const std::string& prefix,const std::string& levelPath,const std::map<std::string,std::string>& bindings)
{
    if(!std::regex_match(prefix,std::regex("[A-Za-z_][A-Za-z0-9_]*"))) throw std::runtime_error("Invalid assembly instance prefix.");
    Json result=definition; std::map<std::string,std::string> paths,tags;
    for(const auto& actor:definition.at("actors"))
    {
        auto name=actor.at("name").get<std::string>();
        paths[actor.at("path").get<std::string>()]=levelPath+"."+prefix+name;
        // Native copy exports actor references under the actual map root.
        auto tag=actor.value("tag",std::string{});
        if(!tag.empty() && Fold(tag)!="none") tags[Fold(tag)]=prefix+tag;
    }
    for(const auto& binding:definition.at("bindings"))
    {
        auto id=binding.at("id").get<std::string>(); auto found=bindings.find(id);
        if(found==bindings.end()) throw std::runtime_error("Choose a target or explicitly choose Unbound for "+binding.at("label").get<std::string>());
        if(binding.at("kind")=="object") paths[binding.at("path").get<std::string>()]=found->second;
        else tags[Fold(binding.at("path").get<std::string>())]=found->second.empty()?"None":found->second;
    }
    std::string t3d="Begin Map\n";
    for(auto& actor:result.at("actors"))
    {
        auto text=actor.at("text").get<std::string>();
        // Rename only nested object identities, with exact typed reference rewrites.
        std::map<std::string,std::string> owned;
        std::istringstream lines(text); std::string line;
        while(std::getline(lines,line))
        {
            auto l=Fold(Trim(line));
            if(l.rfind("begin object ",0)==0 || l.rfind("begin brush ",0)==0)
            {
                auto name=Token(line,"Name"); if(name.empty()) continue;
                for(const auto& ref:References(text)) if(Fold(ref.path)==Fold(name) || (ref.path.size()>name.size() && Fold(ref.path.substr(ref.path.size()-name.size()-1))=="."+Fold(name)))
                {
                    bool underActor=false;
                    for(const auto& entry:paths) if(Fold(ref.path).rfind(Fold(entry.first)+".",0)==0) underActor=true;
                    owned[ref.path]=(underActor?ref.path.substr(0,ref.path.size()-name.size()):levelPath+".")+prefix+name;
                }
            }
        }
        text=RewriteReferences(text,owned); text=RenameObjects(text,prefix); text=RewriteReferences(text,paths);
        for(const auto& prop:{"Tag","Event"})
        {
            auto old=actor.value(Fold(prop),std::string{}); auto found=tags.find(Fold(old));
            if(found!=tags.end()) text=SetProperty(text,prop,found->second);
        }
        auto position=TransformPoint(actor.at("position").get<Vector>(),frame);
        auto rotation=TransformRotation(actor.at("rotation").get<Rotation>(),frame.rotation);
        actor["position"]=position; actor["rotation"]=rotation; actor["name"]=prefix+actor.at("name").get<std::string>();
        text=SetProperty(text,"Location",VectorText(position)); text=SetProperty(text,"Rotation",RotationText(rotation));
        actor["text"]=text; t3d+=text;
    }
    result["t3d"]=t3d+"End Map\n"; return result;
}
Json SelectedTagChanges(const Json& preview)
{
    const auto& changes=preview.at("changes");
    auto excluded=preview.value("excluded",Json::array());
    if(!excluded.is_array())throw std::runtime_error("Invalid tag exclusions.");
    std::set<size_t> indices;
    for(const auto& value:excluded)
    {
        if(!value.is_number_unsigned() && !value.is_number_integer())throw std::runtime_error("Invalid tag exclusion index.");
        auto index=value.get<int64_t>();
        if(index<0 || static_cast<uint64_t>(index)>=changes.size() || !indices.insert(static_cast<size_t>(index)).second)
            throw std::runtime_error("Invalid or duplicate tag exclusion index.");
    }
    Json selected=Json::array();bool target=false;
    for(size_t i=0;i<changes.size();++i)if(!indices.count(i))
    {
        selected.push_back(changes[i]);if(changes[i].at("property")=="Tag")target=true;
    }
    if(!target)throw std::runtime_error("Keep at least one actor Tag checked.");
    return selected;
}
}
