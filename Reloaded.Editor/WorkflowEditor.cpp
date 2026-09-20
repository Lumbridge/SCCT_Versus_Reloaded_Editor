#include "pch.h"
#undef min
#undef max
#include "WorkflowEditor.h"
#include "BrushGridSnapModel.h"
#include "MagicEventModel.h"
#include "MapAuthoringModel.h"
#include "CameraNetworkModel.h"
#include "MapDesignModel.h"
#include "SecurityModel.h"
#include "MapRecovery.h"
#include "BspDiagnostics.h"
#include "MemoryWriter.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>


namespace Workflow::Editor
{
namespace
{
    using Address = uintptr_t;
    std::string ConvertText(const std::string& value,UINT from,UINT to)
    {
        if(value.empty())return {};
        int count=MultiByteToWideChar(from,0,value.data(),static_cast<int>(value.size()),nullptr,0);
        if(!count)throw std::runtime_error("Cannot decode editor text.");
        std::wstring wide(count,L'\0');MultiByteToWideChar(from,0,value.data(),static_cast<int>(value.size()),wide.data(),count);
        count=WideCharToMultiByte(to,0,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr);
        std::string out(count,'\0');WideCharToMultiByte(to,0,wide.data(),static_cast<int>(wide.size()),out.data(),count,nullptr,nullptr);return out;
    }
    bool CopyMemorySafe(void* dest,const void* source,size_t size)
    {
        __try { memcpy(dest,source,size); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    template<class T> T Read(Address address)
    {
        T value{}; if(!address || !CopyMemorySafe(&value,reinterpret_cast<void*>(address),sizeof(value))) throw std::runtime_error("Editor state changed or is unavailable. Refresh and retry."); return value;
    }
    template<class T> void Write(Address address,const T& value)
    {
        if(!address || !CopyMemorySafe(reinterpret_cast<void*>(address),&value,sizeof(value))) throw std::runtime_error("Editor data is no longer writable.");
    }
    template<class R=void,class... Args> R Call(Address object,size_t slot,Args... args)
    {
        auto method=Read<Address>(Read<Address>(object)+slot);
        return reinterpret_cast<R(__thiscall*)(void*,Args...)>(method)(reinterpret_cast<void*>(object),args...);
    }
    unsigned revision=0,mapGeneration=0;
    using ExecFn=int(__thiscall*)(void*,const char*,void*);
    using EndFn=void(__thiscall*)(void*);
    ExecFn previousExec=nullptr;EndFn previousEnd=nullptr;
    int __fastcall ObserveExec(void* self,void*,const char* text,void* output)
    {
        auto command=Fold(text?std::string(text,strnlen(text,80)):std::string{});
        const bool map=command.rfind("map new",0)==0 || command.rfind("map load",0)==0 || command.rfind("map import",0)==0;
        int result=previousExec(self,text,output);
        if(map) ++mapGeneration;
        if(map || command.rfind("map rebuild",0)==0 || command.rfind("bsp ",0)==0 || command.rfind("transaction ",0)==0) ++revision;
        return result;
    }
    void __fastcall ObserveEnd(void* self,void*) { previousEnd(self);++revision; }
    void Observe(Address e)
    {
        if(!previousExec)
        {
            auto slot=Read<Address>(e+0x28);auto original=Read<ExecFn>(slot);auto hook=&ObserveExec;
            previousExec=original;
            if(!MemoryWriter::WriteBytes(slot,&hook,sizeof(hook)))previousExec=nullptr;
        }
        auto buffer=Read<Address>(e+0x148);
        if(buffer && !previousEnd)
        {
            auto slot=Read<Address>(buffer)+0x68;auto original=Read<EndFn>(slot);auto hook=&ObserveEnd;
            previousEnd=original;
            if(!MemoryWriter::WriteBytes(slot,&hook,sizeof(hook)))previousEnd=nullptr;
        }
    }
    Address Engine()
    {
        static_assert(sizeof(void*)==4,"SCCT editor requires x86");
        Address e=Read<Address>(0x1165DFA0);
        if(!e || !Read<Address>(e+0x130)) throw std::runtime_error("Open a source map first.");
        auto mainFrame=Read<Address>(0x1165df84);
        DWORD thread=mainFrame?GetWindowThreadProcessId(reinterpret_cast<HWND>(Read<Address>(mainFrame+4)),nullptr):0;
        if(thread && thread!=GetCurrentThreadId()) throw std::runtime_error("Workflow tools must run on the editor UI thread.");
        if(MapRecovery::UsesRecoveredBspLayout()) throw std::runtime_error("Convert this legacy recovered map to a normal source map first.");
        Observe(e);
        return e;
    }
    Address Level() { return Read<Address>(Engine()+0x130); }
    std::string String(Address a,size_t cap=2048)
    {
        if(!a) return {};
        std::string s;
        for(size_t i=0;i<cap;++i) { char c=Read<char>(a+i); if(!c) return ConvertText(s,CP_ACP,CP_UTF8); s+=c; }
        throw std::runtime_error("Unterminated editor string.");
    }
    std::string Name(int index)
    {
        if(index<0 || index>=Read<int>(0x1169CFC0)) throw std::runtime_error("Invalid editor name.");
        return String(Read<Address>(Read<Address>(0x1169CFBC)+index*4)+12,1024);
    }
    std::string NameOf(Address object) { return Name(Read<int>(object+0x20)); }
    std::string Path(Address object)
    {
        if(!object) return {};
        std::string path; std::set<Address> visited;
        while(object)
        {
            if(visited.size()>64 || !visited.insert(object).second) throw std::runtime_error("Invalid object ownership chain.");
            path=NameOf(object)+(path.empty()?"":"."+path); object=Read<Address>(object+0x18);
        }
        return path;
    }
    std::vector<Address> Array(Address at,size_t stride=4)
    {
        Address data=Read<Address>(at); int count=Read<int>(at+4), capacity=Read<int>(at+8);
        if(count<0 || capacity<count || capacity>2000000 || (count && !data)) throw std::runtime_error("Invalid editor array.");
        std::vector<Address> result; result.reserve(count);
        for(int i=0;i<count;++i) result.push_back(data+i*stride);
        return result;
    }
    std::vector<Address> LiveActors()
    {
        std::vector<Address> result;
        for(auto at:Array(Level()+0x2c)) if(auto a=Read<Address>(at)) result.push_back(a);
        return result;
    }
    bool IsA(Address object,const std::string& wanted)
    {
        if(!object) return false;
        std::set<Address> seen;
        for(Address c=Read<Address>(object+0x24); c && seen.insert(c).second && seen.size()<256;c=Read<Address>(c+0x28))
            if(Fold(Path(c))==Fold(wanted) || Fold(NameOf(c))==Fold(wanted)) return true;
        return false;
    }
    Address Find(const std::string& path,bool actors=false)
    {
        if(path.empty() || Fold(path)=="none") return 0;
        if(actors) { for(auto a:LiveActors()) if(Fold(Path(a))==Fold(path)) return a; }
        else
        {
            for(auto at:Array(0x11697B70))
            {
                auto a=Read<Address>(at);
                if(a && Fold(NameOf(a))==Fold(path.substr(path.find_last_of('.')+1)) && Fold(Path(a))==Fold(path)) return a;
            }
        }
        return 0;
    }
    std::vector<Address> Properties(Address type)
    {
        std::vector<Address> out; std::set<Address> types,properties;
        while(type && types.insert(type).second && types.size()<256)
        {
            Address p=Read<Address>(type+0x58); size_t count=0;
            while(p)
            {
                if(++count>4096) throw std::runtime_error("Cyclic property chain.");
                if(properties.insert(p).second) out.push_back(p); else break;
                p=Read<Address>(p+0x40);
            }
            type=Read<Address>(type+0x28);
        }
        return out;
    }
    Address Property(Address object,const std::string& name)
    {
        for(auto p:Properties(Read<Address>(object+0x24))) if(Fold(NameOf(p))==Fold(name)) return p;
        return 0;
    }
    Address Field(Address object,const std::string& name)
    {
        auto p=Property(object,name); if(!p) throw std::runtime_error("This editor does not expose "+name+" on "+NameOf(object));
        return object+Read<int>(p+0x3c);
    }
    std::string NameField(Address object,const std::string& name)
    {
        auto p=Property(object,name); return p ? Name(Read<int>(object+Read<int>(p+0x3c))) : "None";
    }
    Address StructField(Address type,const std::string& name)
    {
        // UStruct.Children / UField.Next, verified against SBase.SuperEvent.
        // Its NameProperty is not present in the +0x58 linked-property cache.
        std::set<Address> seen;
        for(auto p=Read<Address>(type+0x30);p;p=Read<Address>(p+0x2c))
        {
            if(seen.size()>4096 || !seen.insert(p).second)throw std::runtime_error("Invalid structure field chain.");
            if(IsA(p,"Property") && Fold(NameOf(p))==Fold(name))return p;
        }
        return 0;
    }
    struct NamedEvent { std::string first,second; Address slot; };
    std::vector<NamedEvent> MagicEvents(Address actor)
    {
        std::vector<NamedEvent> events;
        if(!IsA(actor,"SBase.SMagicEvent"))return events;
        auto groups=Property(actor,"Groups");
        auto inner=[](Address property,const char* expected) {
            if(!property || !IsA(property,expected))throw std::runtime_error("Unsupported SMagicEvent group schema.");
            return Read<Address>(property+0x64);
        };
        auto groupItem=inner(groups,"ArrayProperty");auto groupType=inner(groupItem,"StructProperty");
        auto groupField=StructField(groupType,"EventGroup");auto eventItem=inner(groupField,"ArrayProperty");auto eventType=inner(eventItem,"StructProperty");
        auto event=StructField(eventType,"Event");
        if(!event || !IsA(event,"NameProperty"))throw std::runtime_error("Unsupported SMagicEvent event-name schema.");
        auto groupStride=Read<unsigned short>(groupItem+0x32),eventStride=Read<unsigned short>(eventItem+0x32);
        int groupOffset=Read<int>(groupField+0x3c),eventOffset=Read<int>(event+0x3c);
        if(groupStride<12 || eventStride<4 || groupOffset<0 || groupOffset+12>groupStride || eventOffset<0 || eventOffset+4>eventStride)throw std::runtime_error("Invalid SMagicEvent group layout.");
        size_t i=0;
        for(auto group:Array(actor+Read<int>(groups+0x3c),groupStride))
        {
            size_t j=0;for(auto item:Array(group+groupOffset,eventStride))
            {
                if(events.size()>=100000)throw std::runtime_error("SMagicEvent exceeds 100,000 authored group entries.");
                events.push_back({"Groups["+std::to_string(i)+"].EventGroup["+std::to_string(j++)+"].Event",Name(Read<int>(item+eventOffset)),item+eventOffset});
            }
            ++i;
        }
        return events;
    }
    Vector Position(Address a)
    {
        auto v=Read<std::array<float,3>>(a+0x80); return {v[0],v[1],v[2]};
    }
    void SetPosition(Address a,const Vector& v)
    {
        for(auto x:v) if(!std::isfinite(x) || abs(x)>10000000) throw std::runtime_error("Position is out of range.");
        Write(a+0x80,std::array<float,3>{static_cast<float>(v[0]),static_cast<float>(v[1]),static_cast<float>(v[2])});
    }
    Rotation RotationOf(Address a) { return Read<Rotation>(Field(a,"Rotation")); }
    Json Identity(Address a) { return {{"path",Path(a)},{"class",Path(Read<Address>(a+0x24))}}; }
    Address ResolveIdentity(const Json& identity)
    {
        auto a=Find(identity.at("path").get<std::string>(),true);
        return a && Path(Read<Address>(a+0x24))==identity.at("class").get<std::string>() ? a : 0;
    }
    struct TagSlot { Address actor,slot;std::string property,value; };
    std::vector<TagSlot> TagSlots()
    {
        std::vector<TagSlot> slots;
        for(auto actor:LiveActors())
        {
            for(const auto* name:{"Tag","Event"})
            {
                auto p=Property(actor,name);if(!p || !IsA(p,"NameProperty"))throw std::runtime_error("Unsupported actor Tag/Event schema.");
                auto slot=actor+Read<int>(p+0x3c);slots.push_back({actor,slot,name,Name(Read<int>(slot))});
            }
            for(const auto& e:MagicEvents(actor))slots.push_back({actor,e.slot,e.first,e.second});
            if(slots.size()>200000)throw std::runtime_error("Too many tag assignments to rename safely.");
        }
        return slots;
    }
    struct Ref { Address owner{}, property{}, slot{}, target{}; std::string key; bool editable{}; };
    void WalkProperty(Address owner,Address p,Address slot,const std::string& key,bool editable,int depth,std::vector<Ref>& out)
    {
        if(depth>16 || out.size()>100000) throw std::runtime_error("Property traversal limit reached.");
        auto kind=NameOf(Read<Address>(p+0x24));
        if(kind=="ObjectProperty" || kind=="ClassProperty") out.push_back({owner,p,slot,Read<Address>(slot),key,editable});
        else if(kind=="ArrayProperty")
        {
            auto inner=Read<Address>(p+0x64); auto stride=Read<unsigned short>(inner+0x32);
            if(!stride) return;
            size_t i=0; for(auto at:Array(slot,stride)) WalkProperty(owner,inner,at,key+"["+std::to_string(i++)+"]",editable,depth+1,out);
        }
        else if(kind=="StructProperty")
        {
            for(auto child:Properties(Read<Address>(p+0x64)))
            {
                int dim=Read<unsigned short>(child+0x30), size=Read<unsigned short>(child+0x32), offset=Read<int>(child+0x3c);
                if(dim<1 || dim>4096 || offset<0 || offset>1000000) throw std::runtime_error("Invalid struct property.");
                for(int i=0;i<dim;++i) WalkProperty(owner,child,slot+offset+i*size,key+"."+NameOf(child)+(dim>1?"["+std::to_string(i)+"]":""),editable,depth+1,out);
            }
        }
    }
    std::vector<Ref> ReferencesOf(Address a,bool authoredOnly=true)
    {
        std::vector<Ref> result;
        static const std::set<std::string> ignored={"level","xlevel","region","physicsvolume","touching","deleted","base","owner"};
        for(auto p:Properties(Read<Address>(a+0x24)))
        {
            auto name=NameOf(p); auto flags=Read<unsigned>(p+0x34);
            if(authoredOnly && (!(flags&1) || (flags&0x2000) || ignored.count(Fold(name)))) continue;
            if(!authoredOnly && (flags&0x2000)) continue;
            int dim=Read<unsigned short>(p+0x30), stride=Read<unsigned short>(p+0x32), offset=Read<int>(p+0x3c);
            if(dim<1 || dim>4096 || offset<0 || offset>1000000) throw std::runtime_error("Invalid reflected property.");
            for(int i=0;i<dim;++i) WalkProperty(a,p,a+offset+i*stride,name+(dim>1?"["+std::to_string(i)+"]":""),(flags&1) && !(flags&(2|0x20000)),0,result);
        }
        return result;
    }
    bool DependsOn(Address object,Address target,std::set<Address>& visited,int depth=0)
    {
        if(object==target) return true;
        if(!object || depth>16 || !visited.insert(object).second) return false;
        if(visited.size()>10000) throw std::runtime_error("Asset dependency traversal limit reached.");
        if(!IsA(object,"Material") && !IsA(object,"StaticMesh")) return false;
        for(const auto& ref:ReferencesOf(object,false)) if(ref.target && DependsOn(ref.target,target,visited,depth+1)) return true;
        return false;
    }
    void Modify(Address a) { Call(a,0x20); }
    struct Transaction
    {
        Address buffer; bool ended=false;
        explicit Transaction(const char* label):buffer(Read<Address>(Engine()+0x148))
        {
            if(!buffer || Read<int>(buffer+0x44)!=0) throw std::runtime_error("Finish the current editor operation first.");
            Call(buffer,0x64,label);
        }
        void Commit() { if(!ended) { Call(buffer,0x68); ended=true; } }
        ~Transaction()
        {
            if(!ended) { Call(buffer,0x68); Call<int>(buffer,0x78); }
        }
    };
    thread_local const char* insertionText=nullptr;
    bool pasteHookReady=false;
    void* __cdecl PasteSource(void* out)
    {
        if(!insertionText) return reinterpret_cast<void*(__cdecl*)(void*)>(0x10fdac20)(out);
        // appClipboardPaste returns an FString through this caller-owned buffer.
        return reinterpret_cast<void*(__thiscall*)(void*,const char*)>(0x10e04be8)(out,insertionText);
    }
    struct TextOutput
    {
        void** vtable;
        std::string text;
        bool overflow=false;
        static void __fastcall Serialize(TextOutput* self,void*,const char* value,int)
        {
            if(!value || self->overflow) return;
            const size_t length=strnlen(value,64*1024*1024);
            if(self->text.size()+length>64*1024*1024) {self->overflow=true;return;}
            self->text.append(value,length);
        }
        static void __fastcall Flush(TextOutput*,void*) {}
        TextOutput() { static void* methods[]={reinterpret_cast<void*>(&Serialize),reinterpret_cast<void*>(&Flush)};vtable=methods; }
    };
    std::string CopySelectedText()
    {
        TextOutput output;
        // Same UExporter::ExportToOutputDevice call as edactCopySelected,
        // with a private output device instead of writing the user's clipboard.
        reinterpret_cast<int(__cdecl*)(void*,void*,void*,const char*,int)>(0x10ff6a40)(reinterpret_cast<void*>(Level()),nullptr,&output,"copy",0);
        if(output.overflow) throw std::runtime_error("Assembly exceeds the 64 MiB export limit.");
        return ConvertText(NormalizeExportNames(output.text),CP_ACP,CP_UTF8);
    }
    std::vector<Address> Viewports()
    {
        std::vector<Address> result; auto configs=Read<Address>(0x1165e8d4); int count=Read<int>(0x1165e8d8);
        if(count<0 || count>64) throw std::runtime_error("Invalid level viewport list.");
        for(int i=0;i<count;++i)
        {
            auto frame=Read<Address>(configs+i*0x28+0x24); auto view=frame?Read<Address>(frame+0x3c):0;
            result.push_back(view?Read<Address>(view+0x30):0);
        }
        return result;
    }
}
void Initialize()
{
    // Call site in edactPasteSelected, verified against the supported executable.
    // Unrelated native paste commands continue to use appClipboardPaste.
    constexpr uintptr_t site=0x10eb8356;
    const int originalDisplacement=static_cast<int>(0x10fdac20-site-5);
    if(Read<unsigned char>(site)!=0xe8 || Read<int>(site+1)!=originalDisplacement) return;
    unsigned char bytes[5]={0xe8};
    const int displacement=static_cast<int>(reinterpret_cast<uintptr_t>(&PasteSource)-site-5);
    memcpy(bytes+1,&displacement,4);
    pasteHookReady=MemoryWriter::WriteBytes(site,bytes,sizeof(bytes));
}
std::filesystem::path Directory()
{
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768); return std::filesystem::path(path).parent_path()/"ReloadedEditor";
}
uintptr_t LevelIdentity() { return Level(); }
unsigned Revision() { Engine();return revision; }
unsigned MapGeneration() { Engine();return mapGeneration; }
std::string LevelPath() { return Path(Read<Address>(Level()+0x18)); }
std::string MapKey()
{
    auto window=Read<Address>(0x1165e80c); std::string file=window?String(window+0x58,1024):"";
    if(file.empty()) return {};
    std::filesystem::path path(file);
    if(path.is_relative()) path=Directory().parent_path().parent_path()/"Packages"/"MapsEd"/path;
    return Fold(std::filesystem::weakly_canonical(path).string());
}
std::string MapFile()
{
    auto window=Read<Address>(0x1165e80c); std::string file=window?String(window+0x58,1024):"";
    if(file.empty()) return {};
    std::filesystem::path path(file);
    if(path.is_relative()) path=Directory().parent_path().parent_path()/"Packages"/"MapsEd"/path;
    std::error_code error; const auto canonical=std::filesystem::weakly_canonical(path,error);
    return (error?path:canonical).string();
}
void SetMapFile(const std::string& path)
{
    // The stock Save As setter: it also becomes the next Save target, which a
    // raw MAP LOAD leaves stale.
    if(auto window=Read<Address>(0x1165e80c))
        reinterpret_cast<void(__thiscall*)(void*,const char*)>(0x10E05E1C)(reinterpret_cast<void*>(window),path.c_str());
}
bool Exec(const std::string& command)
{
    auto e=Engine(); return Call<int>(e+0x28,0,command.c_str(),reinterpret_cast<void*>(Read<Address>(0x115BEFB0)))!=0;
}
void Redraw() { auto e=Engine(); Call(e,0xe8,reinterpret_cast<void*>(Level())); }
Json Actors(bool selectedOnly)
{
    Json result=Json::array();
    auto live=LiveActors();
    for(auto a:live)
    {
        bool selected=(Read<unsigned>(a+0x2f4)&0x40)!=0;
        if(selectedOnly && !selected) continue;
        auto j=Identity(a); j["selected"]=selected; j["name"]=NameOf(a); j["tag"]=NameField(a,"Tag"); j["event"]=NameField(a,"Event");
        j["magicEvent"]=IsA(a,"SBase.SMagicEvent");j["authorable"]=a!=live[0] && (live.size()<2 || a!=live[1]) && !IsA(a,"Camera");
        result.push_back(std::move(j));
    }
    return result;
}
Json SelectedIdentities() { return Actors(true); }
namespace
{
    // Exclusive categories: specialized brushes must precede ordinary CSG.
    int VisibilityCategory(Address actor, Address builder)
    {
        if(actor==builder || IsA(actor,"LevelInfo") || IsA(actor,"Camera"))return -1;
        if(IsA(actor,"ZoneInfo"))return 0;
        if(IsA(actor,"Volume"))return 1;
        if(IsA(actor,"Mover"))return 4;
        if(!IsA(actor,"Brush"))return 6;
        if(Read<unsigned>(actor+0x344)&0x04000000u)return 0;
        // Portals can also be flagged on individual brush polygons.
        auto model=Read<Address>(actor+0x238);
        auto polys=model?Read<Address>(model+0x50):0;
        if(polys)for(auto poly:Array(polys+0x28,0x14c))
            if(Read<unsigned>(poly+0x140)&0x04000000u)return 0;
        const auto operation=Read<unsigned char>(actor+0x34c);
        return operation==1?2:operation==2?3:5;
    }
    std::vector<std::pair<Address,int>> VisibilityActors()
    {
        auto level=Level();
        auto builder=reinterpret_cast<Address>(MapRecovery::ResolveBuilderBrushActor(reinterpret_cast<void*>(level)));
        std::vector<std::pair<Address,int>> result;
        for(auto actor:LiveActors())
        {
            auto category=VisibilityCategory(actor,builder);
            if(category>=0)result.emplace_back(actor,category);
        }
        return result;
    }
}
Json BrushVisibility()
{
    Json rows=Json::array();
    for(const char* name:{"Zones / portals","Volumes","Additive brushes","Subtractive brushes","Movers","Other brushes","Other actors"})
        rows.push_back({{"name",name},{"total",0},{"visible",0}});
    for(auto [actor,category]:VisibilityActors())
    {
        auto& row=rows[category];row["total"]=row["total"].get<int>()+1;
        if(!(Read<unsigned>(actor+0x2f4)&0x10u))row["visible"]=row["visible"].get<int>()+1;
    }
    return rows;
}
void SetBrushVisibility(int category,const std::string& action)
{
    if(category<0 || category>=7 || (action!="show" && action!="hide" && action!="only" && action!="select" && action!="all"))
        throw std::runtime_error("Invalid brush visibility action.");
    const auto actors=VisibilityActors();
    Json selection=Json::array();
    Transaction transaction("Change brush visibility");
    for(auto [actor,type]:actors)
    {
        if(type!=category && action!="only" && action!="all")continue;
        auto flags=Read<unsigned>(actor+0x2f4);
        const bool hidden=action=="hide" || (action=="only" && type!=category);
        const auto next=hidden?(flags|0x10u)&~0x40u:flags&~0x10u;
        if(next!=flags){Modify(actor);Write(actor+0x2f4,next);}
        if(action=="select")selection.push_back(Identity(actor));
    }
    transaction.Commit();
    if(action=="select") { Exec("POLY SELECT NONE");Select(selection); }
    else { Call(Engine(),0xe4);Redraw(); }
}
// Built BSP surfaces per owning brush, by path: which brushes the last
// geometry build actually kept faces from. Tests use it to see a shape survive.
Json BspSurfaceOwners()
{
    auto level=Level(),model=Read<Address>(level+0x13c);
    auto live=LiveActors();std::set<Address> actors(live.begin(),live.end());
    std::map<std::string,size_t> counts;
    for(auto surface:Array(model+0x94,0x2c))
    {
        auto poly=Read<Address>(surface+0x20),actor=poly?Read<Address>(poly+0x14c):0;
        if(!actor || !actors.count(actor))continue;
        ++counts[Path(actor)];
    }
    Json result=Json::object();
    for(const auto& [path,count]:counts)result[path]=count;
    return result;
}
Json SelectedSurfaceBrushes()
{
    auto level=Level(),model=Read<Address>(level+0x13c);
    auto live=LiveActors();std::set<Address> actors(live.begin(),live.end()),brushes;
    auto builder=reinterpret_cast<Address>(MapRecovery::ResolveBuilderBrushActor(reinterpret_cast<void*>(level)));
    size_t count=0;
    for(auto surface:Array(model+0x94,0x2c))
    {
        if(!(Read<unsigned>(surface+0x14)&0x02000000u))continue;
        ++count;
        // Same master-polygon link used by native polyUpdateMaster (0x110883b0).
        auto poly=Read<Address>(surface+0x20),actor=poly?Read<Address>(poly+0x14c):0;
        if(!actor || !actors.count(actor) || actor==builder || !IsA(actor,"Brush"))
            throw std::runtime_error("A selected face has no editable source brush. The brush selection was not changed.");
        brushes.insert(actor);
    }
    if(!count)throw std::runtime_error("Select a BSP face first.");
    Json result=Json::array();for(auto actor:brushes)result.push_back(Identity(actor));return result;
}
void Select(const Json& identities,bool focus)
{
    auto e=Engine(), level=Level(); std::set<Address> selected;
    for(const auto& id:identities) if(auto a=ResolveIdentity(id)) selected.insert(a);
    for(auto a:LiveActors()) reinterpret_cast<void(__thiscall*)(void*,void*,void*,int,int)>(0x10eb9a20)(reinterpret_cast<void*>(e),reinterpret_cast<void*>(level),reinterpret_cast<void*>(a),selected.count(a)?1:0,0);
    if(focus && !selected.empty()) Exec("CAMERA ALIGN");
    Call(e,0xe4); Redraw();
}
Json CaptureView()
{
    Json view={{"cameras",Json::array()},{"actors",Json::array()}};
    auto cameras=Viewports();
    for(size_t i=0;i<cameras.size();++i) if(auto a=cameras[i])
    {
        Json c={{"slot",i},{"position",Position(a)},{"rotation",RotationOf(a)},{"mode",Read<int>(a+0x4fc)},{"flags",Read<unsigned>(a+0x4f0)}};
        for(const auto& name:{"OrthoZoom","FovAngle"}) if(auto p=Property(a,name)) c[name]=Read<float>(a+Read<int>(p+0x3c));
        view["cameras"].push_back(c);
    }
    for(auto a:LiveActors()) { auto j=Identity(a); j["hidden"]=(Read<unsigned>(a+0x2f4)&0x10)!=0; view["actors"].push_back(j); }
    auto actors=LiveActors(); if(!actors.empty()) view["groups"]=String(Read<Address>(actors[0]+0x41c),1000000);
    return view;
}
std::string RestoreView(const Json& view)
{
    auto cameras=Viewports(); size_t skipped=0;
    // Validate every value before changing a live camera.
    for(const auto& c:view.at("cameras"))
    {
        c.at("slot").get<size_t>();
        for(double n:c.at("position").get<Vector>()) if(!std::isfinite(n) || abs(n)>10000000) throw std::runtime_error("Invalid saved camera position.");
        c.at("rotation").get<Rotation>(); c.at("flags").get<unsigned>();
        int mode=c.at("mode"); if(mode<0 || mode>64) throw std::runtime_error("Invalid saved viewport mode.");
        for(const auto& name:{"OrthoZoom","FovAngle"}) if(c.contains(name) && (!std::isfinite(c.at(name).get<float>()) || c.at(name).get<float>()<=0)) throw std::runtime_error("Invalid saved camera zoom.");
    }
    for(const auto& a:view.at("actors")) { ResolveIdentity(a); a.at("hidden").get<bool>(); }
    if(view.contains("groups")) view.at("groups").get<std::string>();
    for(const auto& c:view.at("cameras"))
    {
        size_t i=c.at("slot"); if(i>=cameras.size() || !cameras[i]) { ++skipped; continue; }
        auto a=cameras[i]; SetPosition(a,c.at("position").get<Vector>()); Write(Field(a,"Rotation"),c.at("rotation").get<Rotation>());
        Write(a+0x4fc,c.at("mode").get<int>()); Write(a+0x4f0,c.at("flags").get<unsigned>());
        for(const auto& name:{"OrthoZoom","FovAngle"}) if(c.contains(name)) if(auto p=Property(a,name)) Write(a+Read<int>(p+0x3c),c.at(name).get<float>());
    }
    for(const auto& j:view.at("actors"))
    {
        auto a=ResolveIdentity(j); if(!a) { ++skipped; continue; }
        auto flags=Read<unsigned>(a+0x2f4); Write(a+0x2f4,(flags&~0x10u)|(j.at("hidden").get<bool>()?0x10u:0));
    }
    auto actors=LiveActors();
    if(!actors.empty() && view.contains("groups"))
    {
        auto groups=ConvertText(view.at("groups").get<std::string>(),CP_UTF8,CP_ACP);
        reinterpret_cast<void*(__thiscall*)(void*,const char*)>(0x10e03770)(reinterpret_cast<void*>(actors[0]+0x41c),groups.c_str());
    }
    Redraw(); return "View restored. Skipped "+std::to_string(skipped)+" unavailable actors/viewports.";
}
std::string CurrentAsset(bool mesh) { return Path(Read<Address>(Engine()+(mesh?0x13c:0x138))); }
bool Compatible(const std::string& actorPath,const std::string& type) { auto a=Find(actorPath); return a && IsA(a,type); }
Json FindUsages(const std::string& asset)
{
    auto target=Find(asset); if(!target) throw std::runtime_error("Load the source asset in its browser first.");
    Json rows=Json::array();
    for(auto a:LiveActors())
    {
        for(const auto& ref:ReferencesOf(a))
        {
            if(!ref.target) continue; std::set<Address> visited;
            bool direct=ref.target==target;
            if(!direct && !DependsOn(ref.target,target,visited)) continue;
            bool assignable=direct && ref.editable && (Fold(ref.key)=="staticmesh" || Fold(ref.key)=="texture" || Fold(ref.key)=="skin" || Fold(ref.key).rfind("skins[",0)==0);
            rows.push_back({{"actor",Identity(a)},{"property",ref.key},{"direct",direct},{"replaceable",assignable},{"selected",(Read<unsigned>(a+0x2f4)&0x40)!=0},{"surface",-1}});
        }
    }
    auto model=Read<Address>(Level()+0x13c); int index=0;
    for(auto surface:Array(model+0x94,0x2c))
    {
        auto material=Read<Address>(surface+0x10); std::set<Address> visited; bool direct=material==target;
        if(direct || DependsOn(material,target,visited))
        {
            // Native polyUpdateMaster (0x110883b0): +0x20 is FPoly*,
            // whose +0x14c Actor identifies the source construction brush.
            auto poly=Read<Address>(surface+0x20);
            auto actor=poly?Read<Address>(poly+0x14c):0;
            rows.push_back({{"actor",actor?Identity(actor):Json{}},{"property","BSP surface "+std::to_string(index)},{"surface",index},{"direct",direct},{"replaceable",direct},{"selected",(Read<unsigned>(surface+0x14)&0x02000000)!=0}});
        }
        ++index;
    }
    return rows;
}
void ReplaceUsages(const Json& usages,const std::string& source,const std::string& replacement)
{
    auto fresh=FindUsages(source); auto target=Find(replacement); if(!target) throw std::runtime_error("Load a replacement in the browser first.");
    if(Fold(source)==Fold(replacement)) throw std::runtime_error("Choose a different replacement.");
    std::vector<Ref> refs; std::set<int> surfaces; std::set<Address> changed;
    for(const auto& row:usages)
    {
        auto match=std::find_if(fresh.begin(),fresh.end(),[&](const Json& j){return j.at("actor")==row.at("actor") && j.at("property")==row.at("property") && j.at("surface")==row.at("surface");});
        if(match==fresh.end() || !match->at("replaceable").get<bool>()) throw std::runtime_error("A usage changed or cannot be replaced. Refresh the preview.");
        int surface=row.at("surface");
        if(surface>=0) { if(!IsA(target,"Material")) throw std::runtime_error("BSP surfaces require a material."); surfaces.insert(surface); }
        else
        {
            auto a=ResolveIdentity(row.at("actor")); if(!a) throw std::runtime_error("A target actor no longer exists.");
            for(auto ref:ReferencesOf(a)) if(ref.key==row.at("property").get<std::string>())
            {
                auto type=Path(Read<Address>(ref.property+0x64));
                if(!IsA(target,type)) throw std::runtime_error("Replacement is incompatible with "+ref.key+" (requires "+type+").");
                refs.push_back(ref); changed.insert(a);
            }
        }
    }
    if(refs.empty() && surfaces.empty()) throw std::runtime_error("No replaceable usages are checked.");
    auto e=Engine(), model=Read<Address>(Level()+0x13c); auto allSurfaces=Array(model+0x94,0x2c);
    std::vector<unsigned> flags; for(auto s:allSurfaces) flags.push_back(Read<unsigned>(s+0x14));
    Address previous=Read<Address>(e+0x138);
    struct Restore { Address e; Address previous; std::vector<Address>& surfaces; std::vector<unsigned>& flags;
        ~Restore() { Write(e+0x138,previous); for(size_t i=0;i<surfaces.size();++i) Write(surfaces[i]+0x14,(Read<unsigned>(surfaces[i]+0x14)&~0x02000000u)|(flags[i]&0x02000000u)); }
    } restore{e,previous,allSurfaces,flags};
    Transaction transaction("Replace asset usages");
    for(auto a:changed) Modify(a);
    for(const auto& ref:refs) Write(ref.slot,target);
    if(!surfaces.empty())
    {
        for(size_t i=0;i<allSurfaces.size();++i) Write(allSurfaces[i]+0x14,(flags[i]&~0x02000000u)|(surfaces.count(static_cast<int>(i))?0x02000000u:0));
        Write(e+0x138,target);
        if(!Exec("POLY SETTEXTURE")) throw std::runtime_error("Native texture replacement failed.");
    }
    for(auto a:changed) Call(a,0x44); // Native PostEditChange: refresh render/collision state.
    transaction.Commit(); Redraw();
}
Json Connections(const std::string& actor)
{
    auto actors=Actors(); Json result=Json::array(); std::map<std::string,std::vector<Json>> tags;
    std::set<std::string> livePaths;for(const auto& a:actors) livePaths.insert(Fold(a.at("path").get<std::string>()));
    auto append=[&](Json row){if(result.size()>=100000)throw std::runtime_error("Connection limit exceeded (100,000).");result.push_back(std::move(row));};
    for(const auto& a:actors) if(Fold(a.at("tag").get<std::string>())!="none") tags[Fold(a.at("tag").get<std::string>())].push_back(a);
    auto tagLinks=[&](const std::string& from,const std::string& event,const std::string& property,const std::string& kind) {
        if(event.empty() || Fold(event)=="none")return;
        auto matches=tags.find(Fold(event));
        if(matches==tags.end()) {if(actor.empty() || Fold(from)==Fold(actor))append({{"from",from},{"to",event},{"property",property},{"kind",kind},{"resolved",false}});}
        else for(const auto& to:matches->second)if(actor.empty() || Fold(from)==Fold(actor) || Fold(to.at("path").get<std::string>())==Fold(actor))append({{"from",from},{"to",to.at("path")},{"property",property},{"kind",kind},{"resolved",true}});
    };
    for(auto a:LiveActors())
    {
        auto from=Path(a);
        for(const auto& ref:ReferencesOf(a)) if(ref.target && IsA(ref.target,"Actor"))
        {
            auto to=Path(ref.target);
            if(actor.empty() || Fold(from)==Fold(actor) || Fold(to)==Fold(actor)) append({{"from",from},{"to",to},{"property",ref.key},{"kind","Actor reference"},{"resolved",livePaths.count(Fold(to))!=0}});
        }
        tagLinks(from,NameField(a,"Event"),"Event","Event -> Tag");
        for(const auto& event:MagicEvents(a))tagLinks(from,event.second,event.first,"EventGroup -> Tag");
    }
    return result;
}
Json PreviewTagRename(const Json& identity,const std::string& newTag)
{
    auto actor=ResolveIdentity(identity);if(!actor)throw std::runtime_error("That actor no longer exists. Refresh the graph.");
    if(IsA(actor,"Camera") || IsA(actor,"LevelInfo"))throw std::runtime_error("Editor infrastructure Tags cannot be renamed here.");
    if(!std::regex_match(newTag,std::regex("[A-Za-z_][A-Za-z0-9_]{0,62}")) || Fold(newTag)=="none")throw std::runtime_error("Use 1-63 letters, digits or underscores, starting with a letter or underscore. None is not a target Tag.");
    auto old=NameField(actor,"Tag");if(Fold(old)==Fold(newTag))throw std::runtime_error("Tag matching ignores case. Enter a different Tag.");
    Json preview={{"actor",Identity(actor)},{"old",old},{"new",newTag},{"level",LevelIdentity()},{"generation",MapGeneration()},{"revision",Revision()},{"changes",Json::array()}};
    bool untagged=old.empty() || Fold(old)=="none";
    for(const auto& s:TagSlots())
    {
        if(Fold(s.value)==Fold(newTag))throw std::runtime_error("The new Tag is already used by an actor or event: "+Path(s.actor)+"."+s.property+". Choose an unused Tag to avoid joining unrelated connections.");
        if(untagged ? (s.actor!=actor || s.property!="Tag") : Fold(s.value)!=Fold(old))continue;
        if(s.property=="Tag" && (IsA(s.actor,"Camera") || IsA(s.actor,"LevelInfo")))throw std::runtime_error("This Tag is shared with editor infrastructure and cannot be renamed as a group.");
        preview["changes"].push_back({{"actor",Identity(s.actor)},{"property",s.property},{"before",s.value},{"after",newTag}});
    }
    return preview;
}
void RenameTag(const Json& preview)
{
    auto fresh=PreviewTagRename(preview.at("actor"),preview.at("new"));
    auto original=preview;original.erase("excluded");
    if(fresh!=original)throw std::runtime_error("The map or its links changed after the preview. Review the rename again.");
    const auto selected=SelectedTagChanges(preview);
    auto slots=TagSlots();std::vector<TagSlot> changes;std::set<Address> owners;
    for(const auto& change:selected)
    {
        auto actor=ResolveIdentity(change.at("actor"));
        auto found=std::find_if(slots.begin(),slots.end(),[&](const TagSlot& s){return s.actor==actor && s.property==change.at("property").get<std::string>() && s.value==change.at("before").get<std::string>();});
        if(found==slots.end())throw std::runtime_error("A tag assignment is no longer available. Review the rename again.");
        changes.push_back(*found);owners.insert(actor);
    }
    // Verified FName::FName(const char*, EFindName), NAME_Add=1. This only
    // interns a name; it does not alter the spelling of any existing FName.
    int name=0;auto value=preview.at("new").get<std::string>();
    reinterpret_cast<void*(__thiscall*)(void*,const char*,int)>(0x10fb9610)(&name,value.c_str(),1);
    if(Fold(Name(name))!=Fold(value))throw std::runtime_error("The editor could not create the new Tag.");
    Transaction transaction("Rename Tag and dependent events");
    for(auto actor:owners)Modify(actor); // Includes nested dynamic EventGroup arrays.
    for(const auto& change:changes)Write(change.slot,name);
    for(auto actor:owners)Call(actor,0x44);
    transaction.Commit();Redraw();
}
Pose BuilderPose()
{
    auto actors=LiveActors(); if(actors.size()<2) throw std::runtime_error("No builder brush is available.");
    return {Position(actors[1]),{}};
}
Json SelectedMeshBounds()
{
    Vector minimum{},maximum{};size_t count=0;
    for(auto actor:LiveActors())
    {
        if(!(Read<unsigned>(actor+0x2f4)&0x40))continue;
        auto property=Property(actor,"StaticMesh");
        auto mesh=property?Read<Address>(actor+Read<int>(property+0x3c)):0;
        if(!mesh || !IsA(mesh,"StaticMesh"))throw std::runtime_error("Select only static meshes with valid mesh assets.");
        // UPrimitive's local FBox, also returned by GetRenderBoundingBox(nullptr)
        // at 0x110c2b60. SCCT stores six floats (no IsValid byte).
        auto box=Read<std::array<float,6>>(mesh+0x28);
        auto scale=Read<float>(Field(actor,"DrawScale"));
        auto scale3=Read<std::array<float,3>>(Field(actor,"DrawScale3D"));
        auto pivot=Read<std::array<float,3>>(Field(actor,"PrePivot"));
        for(int axis=0;axis<3;++axis)
            if(!std::isfinite(box[axis]) || !std::isfinite(box[axis+3]) || box[axis]>box[axis+3]
                || !std::isfinite(scale) || !std::isfinite(scale3[axis]) || !std::isfinite(pivot[axis]))
                throw std::runtime_error("The selected mesh has invalid bounds or scale.");
        Pose pose{Position(actor),RotationOf(actor)};
        for(int corner=0;corner<8;++corner)
        {
            Vector local{};
            for(int axis=0;axis<3;++axis)local[axis]=(static_cast<double>(box[axis+((corner&(1<<axis))?3:0)])-pivot[axis])*scale*scale3[axis];
            auto world=TransformPoint(local,pose);
            for(int axis=0;axis<3;++axis)
            {
                if(!std::isfinite(world[axis]) || std::abs(world[axis])>10000000)throw std::runtime_error("The selected mesh bounds are out of range.");
                if(!count && !corner)minimum[axis]=maximum[axis]=world[axis];
                else {minimum[axis]=std::min(minimum[axis],world[axis]);maximum[axis]=std::max(maximum[axis],world[axis]);}
            }
        }
        ++count;
    }
    if(!count)throw std::runtime_error("Select a static mesh first.");
    return {{"min",minimum},{"max",maximum},{"count",count}};
}
Json SelectedBrushVertices()
{
    if(Read<int>(Engine()+0x1ac)!=0x19)throw std::runtime_error("Switch to Vertex Editing and select vertices first.");
    Json result=Json::array();
    std::set<Address> brushes;
    for(auto actor:LiveActors())if(IsA(actor,"Brush"))brushes.insert(actor);
    // Native vertexedit_AddPosition stores (brush, polygon index, vertex index).
    for(auto entry:Array(0x11685a9c,12))
    {
        auto actor=Read<Address>(entry);if(!brushes.count(actor) || !(Read<unsigned>(actor+0x2f4)&0x40))throw std::runtime_error("Vertex selection is stale. Reselect the vertices.");
        if(auto p=Property(actor,"bLockLocation"))if(Read<unsigned>(actor+Read<int>(p+0x3c))&Read<unsigned>(p+0x64))throw std::runtime_error("Unlock the selected brush before snapping.");
        auto model=Read<Address>(actor+0x238),polys=model?Read<Address>(model+0x50):0;
        const int pi=Read<int>(entry+4),vi=Read<int>(entry+8);
        if(!polys || pi<0 || pi>=Read<int>(polys+0x2c))throw std::runtime_error("Selected polygon is no longer available.");
        auto poly=Read<Address>(polys+0x28)+pi*0x14c;const int count=Read<unsigned short>(poly+0x148);
        if(count<3 || count>16 || vi<0 || vi>=count)throw std::runtime_error("Selected vertex is no longer available.");
        auto local=Read<std::array<float,3>>(poly+0x18+vi*12);std::array<float,12> coords{};std::array<float,3> world{};
        // Same actor ToWorld coordinates and FVector::TransformPointBy as the native vertex tool.
        Call<void*>(actor,0xac,coords.data());
        reinterpret_cast<void*(__thiscall*)(void*,void*,const void*)>(0x10eb2ba0)(local.data(),world.data(),coords.data());
        for(float v:world)if(!std::isfinite(v) || std::abs(v)>10000000)throw std::runtime_error("Vertex position is out of range.");
        result.push_back({{"actor",Identity(actor)},{"polygon",pi},{"vertex",vi},{"local",local},{"world",world}});
    }
    if(result.empty())throw std::runtime_error("Select vertices with the Vertex Editing tool first.");
    return result;
}
void SnapSelectedBrushVertices(unsigned axes)
{
    const auto selected=SelectedBrushVertices();const auto grid=Read<std::array<float,3>>(Engine()+0x200);
    struct Edit {Address actor,model,polys,poly,vertex;std::array<float,3> next;};std::vector<Edit> edits;
    for(const auto& item:selected)
    {
        auto world=item.at("world").get<Vector>();Vector spacing{grid[0],grid[1],grid[2]};
        auto delta=BrushGridSnap::Translation(world,world,spacing,axes);
        if(delta==Vector{})continue;
        auto actor=ResolveIdentity(item.at("actor")),model=Read<Address>(actor+0x238),polys=Read<Address>(model+0x50);
        std::array<float,3> target{},local{};std::array<float,12> coords{};
        for(int axis=0;axis<3;++axis)target[axis]=static_cast<float>(world[axis]+delta[axis]);
        Call<void*>(actor,0xa8,coords.data());
        reinterpret_cast<void*(__thiscall*)(void*,void*,const void*)>(0x10eb2ba0)(target.data(),local.data(),coords.data());
        for(float v:local)if(!std::isfinite(v) || std::abs(v)>10000000)throw std::runtime_error("Snapped vertex is out of range.");
        auto poly=Read<Address>(polys+0x28)+item.at("polygon").get<int>()*0x14c;
        auto vertex=poly+0x18+item.at("vertex").get<int>()*12;
        if(local!=Read<std::array<float,3>>(vertex))edits.push_back({actor,model,polys,poly,vertex,local});
    }
    if(edits.empty())return;
    Transaction transaction("Snap selected vertices to grid");std::set<Address> objects,models,polygons,arrays;
    for(const auto& e:edits){objects.insert(e.actor);objects.insert(e.model);objects.insert(e.polys);models.insert(e.model);polygons.insert(e.poly);arrays.insert(e.polys+0x28);}
    for(auto object:objects)Modify(object);
    // UPolys skips its TTransArray payload during UObject transaction serialization.
    // Record the array separately, exactly as native MoveVertex does.
    auto undo=Read<Address>(0x11691d6c);if(!undo)throw std::runtime_error("Vertex Undo transaction is unavailable.");
    for(auto array:arrays)Call(undo,4,Read<Address>(array+12),array,0,Read<int>(array+4),0,0x14c,Address(0x10e0303b),Address(0x10e04c47));
    for(const auto& e:edits)Write(e.vertex,e.next);
    // Refresh polygon normals and brush bounds using the native vertex-release path.
    for(auto poly:polygons)reinterpret_cast<void(__cdecl*)(void*)>(0x10eb8d70)(reinterpret_cast<void*>(poly));
    for(auto model:models)reinterpret_cast<void(__thiscall*)(void*)>(0x110ce0d0)(reinterpret_cast<void*>(model));
    transaction.Commit();Redraw();
}
Json BrushSnapBounds(bool surfaces)
{
    Engine();
    auto identities=surfaces?SelectedSurfaceBrushes():SelectedIdentities();
    if(identities.empty())throw std::runtime_error("Select an editable brush first.");
    Vector minimum{},maximum{};size_t vertices=0;
    for(const auto& identity:identities)
    {
        auto actor=ResolveIdentity(identity);
        if(!actor || !IsA(actor,"Brush"))throw std::runtime_error("Select only editable brushes to snap.");
        if(auto p=Property(actor,"bLockLocation"))
            if(Read<unsigned>(actor+Read<int>(p+0x3c))&Read<unsigned>(p+0x64))
                throw std::runtime_error("Unlock the selected brush location before snapping.");
        auto model=Read<Address>(actor+0x238),polys=model?Read<Address>(model+0x50):0;
        if(!polys || Read<int>(polys+0x2c)<=0)throw std::runtime_error("A selected brush has no editable polygons.");
        // Use the same native BuildCoords/point transform as polyUpdateMaster.
        // This build bakes brush rotation/scale into authored polygons; the
        // native helper is authoritative rather than an actor mesh transform.
        std::array<float,24> coords{};
        reinterpret_cast<float(__thiscall*)(void*,void*,void*)>(0x10eb2eb0)(reinterpret_cast<void*>(actor),coords.data(),nullptr);
        auto pivot=Read<std::array<float,3>>(Field(actor,"PrePivot"));auto location=Position(actor);
        size_t count=0;
        for(auto poly:Array(polys+0x28,0x14c))
        {
            const auto n=Read<unsigned short>(poly+0x148);
            if(n<3 || n>16)throw std::runtime_error("A selected brush has invalid polygon vertices.");
            for(unsigned i=0;i<n;++i)
            {
                auto local=Read<std::array<float,3>>(poly+0x18+i*12);std::array<float,3> transformed{};
                for(int axis=0;axis<3;++axis)local[axis]-=pivot[axis];
                reinterpret_cast<void(__cdecl*)(const void*,const void*,void*)>(0x10eb2a70)(coords.data(),local.data(),transformed.data());
                for(int axis=0;axis<3;++axis)
                {
                    const double value=transformed[axis]+location[axis];
                    if(!std::isfinite(value) || std::abs(value)>10000000)throw std::runtime_error("Brush bounds are out of range.");
                    if(!vertices)minimum[axis]=maximum[axis]=value;
                    else {minimum[axis]=std::min(minimum[axis],value);maximum[axis]=std::max(maximum[axis],value);}
                }
                ++vertices;++count;
            }
        }
        if(!count)throw std::runtime_error("A selected brush has no vertices.");
    }
    auto nativeGrid=Read<std::array<float,3>>(Engine()+0x200);
    return {{"min",minimum},{"max",maximum},{"actors",identities},{"grid",nativeGrid}};
}
void SnapBrushesToGrid(unsigned axes,bool surfaces)
{
    const auto bounds=BrushSnapBounds(surfaces);
    const auto delta=BrushGridSnap::Translation(bounds.at("min").get<Vector>(),bounds.at("max").get<Vector>(),bounds.at("grid").get<Vector>(),axes);
    std::vector<std::pair<Address,Vector>> moves;
    for(const auto& identity:bounds.at("actors"))
    {
        auto actor=ResolveIdentity(identity);if(!actor)throw std::runtime_error("The selected brush is no longer available.");
        auto old=Position(actor),next=old;
        for(int axis=0;axis<3;++axis)
        {
            next[axis]=static_cast<float>(old[axis]+delta[axis]);
            if(!std::isfinite(next[axis]) || std::abs(next[axis])>10000000)throw std::runtime_error("Snapped position is out of range.");
        }
        if(next!=old)moves.emplace_back(actor,next);
    }
    if(moves.empty())return;
    Transaction transaction("Snap brush edges to grid");
    for(const auto& move:moves){Modify(move.first);SetPosition(move.first,move.second);Call(move.first,0x44);}
    transaction.Commit();Redraw();
}
void FitBuilderBrushToMeshes()
{
    // Re-resolve at activation, never retain actor pointers from the popup.
    auto bounds=SelectedMeshBounds();auto minimum=bounds.at("min").get<Vector>(),maximum=bounds.at("max").get<Vector>();
    Vector center{},extent{};
    for(int axis=0;axis<3;++axis)
    {
        center[axis]=static_cast<float>((minimum[axis]+maximum[axis])*0.5);
        // Round outwards, including the rounding of the brush's float location.
        double half=std::max({center[axis]-minimum[axis],maximum[axis]-center[axis],0.5});
        extent[axis]=std::nextafter(static_cast<float>(half),std::numeric_limits<float>::infinity());
    }
    auto material=Read<Address>(Engine()+0x138);
    if(!material || !IsA(material,"Material"))material=Find("Engine.DefaultTexture");
    if(!material)throw std::runtime_error("Select a material before fitting the builder brush.");
    auto texture=ConvertText(Path(material),CP_UTF8,CP_ACP);
    const int faces[6][4][3]={{{1,-1,-1},{1,1,-1},{1,1,1},{1,-1,1}},{{-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1}},{{-1,1,-1},{-1,1,1},{1,1,1},{1,1,-1}},{{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}},{{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},{{-1,-1,-1},{-1,1,-1},{1,1,-1},{1,-1,-1}}};
    std::ostringstream text;text.imbue(std::locale::classic());text<<std::setprecision(9)<<"BRUSH SET\r\nBegin PolyList\r\n";
    for(const auto& face:faces)
    {
        text<<"Begin Polygon Texture="<<texture<<" Flags=0\r\n";
        for(const auto& vertex:face)text<<"Vertex "<<vertex[0]*extent[0]<<','<<vertex[1]*extent[1]<<','<<vertex[2]*extent[2]<<"\r\n";
        text<<"End Polygon\r\n";
    }
    text<<"End PolyList\r\n";
    auto builder=reinterpret_cast<Address>(MapRecovery::ResolveBuilderBrushActor(reinterpret_cast<void*>(Level())));
    if(!builder)throw std::runtime_error("The builder brush is unavailable.");
    auto model=Read<Address>(builder+0x238),polys=model?Read<Address>(model+0x50):0;
    if(!polys)throw std::runtime_error("Rebuild the builder brush before using this command.");
    Transaction transaction("Position builder brush around static meshes");
    Modify(builder);Modify(model);Modify(polys);
    if(!Exec("BRUSH RESET") || !Exec(text.str()))throw std::runtime_error("Could not resize the builder brush.");
    SetPosition(builder,center);
    Write(Field(builder,"Rotation"),Rotation{});
    Write(Field(builder,"PrePivot"),std::array<float,3>{});
    Write(Field(builder,"DrawScale"),1.0f);
    Write(Field(builder,"DrawScale3D"),std::array<float,3>{1,1,1});
    Call(builder,0x44);transaction.Commit();Redraw();
}
Json CaptureAssembly(const Json& members,const Pose& frame)
{
    if(members.empty()) throw std::runtime_error("Select at least one actor or brush.");
    std::map<std::string,Address> included; auto live=LiveActors();
    for(const auto& member:members)
    {
        auto a=ResolveIdentity(member); if(!a) continue;
        if(a==live[0] || (live.size()>1 && a==live[1]) || IsA(a,"Camera") || IsA(a,"LevelInfo")) throw std::runtime_error("Editor infrastructure cannot be saved in an assembly: "+NameOf(a));
        included[Fold(Path(a))]=a;
    }
    if(included.empty()) throw std::runtime_error("All assembly members have been deleted.");
    auto previous=SelectedIdentities();
    struct Restore { Json previous; ~Restore(){try {Select(previous);} catch(...) {}} } restore{previous};
    Select(members);
    auto texts=ParseActors(CopySelectedText());
    if(texts.size()!=included.size()) throw std::runtime_error("Native copy omitted a selected actor; the assembly was not saved.");
    Json result={{"actors",Json::array()},{"bindings",Json::array()},{"pivot",frame.position},{"dependencies",Json::array()}};
    std::set<std::string> bindingPaths,dependencies; std::map<std::string,std::vector<Address>> tags;
    for(auto a:live) tags[Fold(NameField(a,"Tag"))].push_back(a);
    for(const auto& actor:texts)
    {
        auto found=std::find_if(included.begin(),included.end(),[&](const auto& pair){return Fold(NameOf(pair.second))==Fold(actor.name);});
        if(found==included.end()) throw std::runtime_error("Native copy returned an unexpected actor.");
        Address a=found->second;
        auto position=TransformPoint(Position(a),frame,true); auto rotation=TransformRotation(RotationOf(a),frame.rotation,true);
        auto text=Workflow::SetProperty(Workflow::SetProperty(actor.text,"Location",VectorText(position)),"Rotation",RotationText(rotation));
        for(const auto& field:{"Level","Region","XLevel","Name","bSelected","bLightChanged","bMustInitNetChannels"}) text=RemoveProperty(text,field);
        auto tag=NameField(a,"Tag"),event=NameField(a,"Event");
        auto actorClass=Path(Read<Address>(a+0x24));
        result["actors"].push_back({{"name",actor.name},{"class",actorClass},{"path",Path(a)},{"text",text},{"position",position},{"rotation",rotation},{"tag",tag},{"event",event}});
        dependencies.insert(actorClass);
        const std::regex polygonMaterial("\\bTexture=(\"([^\"]+)\"|([^\\s]+))",std::regex::icase);
        std::istringstream lines(text);std::string line;
        while(std::getline(lines,line)) if(Fold(line).find("begin polygon")!=std::string::npos)
        {
            std::smatch match;if(std::regex_search(line,match,polygonMaterial))
            { auto material=match[2].matched?match[2].str():match[3].str();if(Fold(material)!="none")dependencies.insert(material); }
        }
        for(const auto& ref:Workflow::References(text))
        {
            auto target=Find(ref.path);
            if(!target) throw std::runtime_error("Unresolved exported dependency: "+ref.path);
            if(IsA(a,"Brush") && target==Read<Address>(a+0x238)) continue;
            Address root=target;
            while(root && root!=Level() && !IsA(root,"Actor")) root=Read<Address>(root+0x18);
            if(root && IsA(root,"Actor"))
            {
                if(included.count(Fold(Path(root)))) continue;
                if(root!=target) throw std::runtime_error("Include the actor owning external subobject "+ref.path+" in the assembly.");
                if(bindingPaths.insert(Fold(ref.path)).second)
                    result["bindings"].push_back({{"id","object:"+Fold(ref.path)},{"label",NameOf(a)+" -> "+NameOf(target)},{"kind","object"},{"path",ref.path},{"type",Path(Read<Address>(target+0x24))}});
            }
            else if(root==Level()) throw std::runtime_error("Unsupported map-owned dependency: "+ref.path);
            else dependencies.insert(ref.path);
        }
        if(Fold(event)!="none" && !event.empty())
        {
            bool internal=false,external=false;
            for(auto target:tags[Fold(event)]) { if(included.count(Fold(Path(target)))) internal=true; else external=true; }
            if(internal && external) throw std::runtime_error("Event '"+event+"' targets both included and excluded actors. Include all targets or separate their tags first.");
            if(!internal && bindingPaths.insert("event:"+Fold(event)).second)
                result["bindings"].push_back({{"id","event:"+Fold(event)},{"label",NameOf(a)+" Event -> "+event},{"kind","event"},{"path",event},{"type","Actor"}});
        }
    }
    for(const auto& dependency:dependencies) result["dependencies"].push_back(dependency);
    return result;
}
Json PlaceAssembly(const Json& definition,const Pose& frame,const std::map<std::string,std::string>& bindings)
{
    auto resolved=bindings;
    for(const auto& dependency:definition.at("dependencies"))
    {
        auto path=dependency.get<std::string>();
        if(!Find(path))
        {
            auto package=path.substr(0,path.find('.'));
            if(package.find_first_of("\"\r\n/\\")!=std::string::npos) throw std::runtime_error("Invalid dependency package.");
            // Loading the class/object by package leaves existing assets untouched.
            for(const char* directory:{"Textures","StaticMeshes","Sounds","Animations"})
            {
                auto extension=std::string(directory)=="Textures"?".utx":std::string(directory)=="StaticMeshes"?".usx":std::string(directory)=="Sounds"?".uas":".ukx";
                auto file=Directory().parent_path().parent_path()/"Packages"/directory/(package+extension);
                if(std::filesystem::exists(file)) Exec("OBJ LOAD FILE=\""+file.string()+"\"");
            }
            if(!Find(path)) throw std::runtime_error("Load the required package before placing: "+path);
        }
    }
    for(const auto& binding:definition.at("bindings"))
    {
        auto id=binding.at("id").get<std::string>(); auto it=resolved.find(id);
        if(it==resolved.end()) throw std::runtime_error("Missing binding: "+binding.at("label").get<std::string>());
        if(it->second.empty()) continue;
        auto target=Find(it->second,true);
        if(!target || !IsA(target,binding.at("type").get<std::string>())) throw std::runtime_error("Incompatible external actor: "+it->second);
        if(binding.at("kind")=="event")
        {
            it->second=NameField(target,"Tag");
            if(Fold(it->second)=="none") throw std::runtime_error("The bound Event target must have a Tag. Set its Tag before placement.");
        }
    }
    auto prefix="RE_"+Id().substr(0,12)+"_";
    auto prepared=PreparePlacement(definition,frame,prefix,LevelPath(),resolved);
    if(!pasteHookReady) throw std::runtime_error("This editor build does not support verified assembly insertion.");
    auto before=SelectedIdentities(); const std::string text=ConvertText(prepared.at("t3d").get<std::string>(),CP_UTF8,CP_ACP);
    struct TextScope { TextScope(const char* value) {if(insertionText) throw std::runtime_error("Assembly insertion is already running.");insertionText=value;} ~TextScope(){insertionText=nullptr;} } source(text.c_str());
    Json members=Json::array(), names=Json::object();
    try
    {
        Transaction transaction("Place actor assembly");
        Select(Json::array());
        // Verified UUnrealEdEngine::edactPasteSelected. The native routine
        // recentres pasted actors; restore the requested world poses before End.
        Call(Engine(),0x26c,reinterpret_cast<void*>(Level()),0);
        for(const auto& actor:prepared.at("actors"))
        {
            auto a=Find(LevelPath()+"."+actor.at("name").get<std::string>(),true);
            if(!a) throw std::runtime_error("Native paste did not create all assembly actors.");
            Modify(a); SetPosition(a,actor.at("position").get<Vector>()); Write(Field(a,"Rotation"),actor.at("rotation").get<Rotation>()); Call(a,0x44);
            members.push_back(Identity(a));
            names[Path(a)]=actor.at("name").get<std::string>().substr(prefix.size());
        }
        if(SelectedIdentities().size()!=members.size()) throw std::runtime_error("Native paste created an unexpected actor count.");
        transaction.Commit();
    }
    catch(...) { Select(before); throw; }
    Select(members); return {{"id",Id()},{"assembly",definition.at("id")},{"members",members},{"names",names},{"position",frame.position},{"rotation",frame.rotation}};
}
#include "MagicEventNative.inl"
#include "MapAuthoringNative.inl"
Json AddObjectiveActor(const Json& owner,const std::string& type)
{
    MagicCheck(owner);
    auto parent=ResolveIdentity(owner.at("actor"));
    const bool objective=type=="SBase.SObjective",flag=type=="SBase.SFlag";
    if(!objective && !flag && type!="SBase.SComputerObjectiveTrigger" && type!="SBase.SBombTargetObjectiveTrigger")
        throw std::runtime_error("Unsupported objective actor type.");
    if(!parent || (objective?!IsA(parent,"SBase.SMission"):(!IsA(parent,"SBase.SObjective") || IsA(parent,"SBase.SMission"))))
        throw std::runtime_error(objective?"Select one SMission to add an SObjective.":"Select one SObjective to add a trigger.");
    const char* property=objective?"Objectives":"Triggers";
    auto links=owner.at("values").at(property);
    if(!links.is_array())throw std::runtime_error("Objective links are unavailable.");
    std::set<std::string> used;for(const auto& slot:TagSlots())used.insert(Fold(slot.value));
    std::string id,dropId;const auto stem=type.substr(type.find_last_of('.')+1)+"_";
    for(int suffix=1;;++suffix)
    {
        id=stem+std::to_string(suffix);dropId=id+"_DropZone";
        if(!used.count(Fold(id)) && !Find(LevelPath()+"."+id) &&
            (!flag || (!used.count(Fold(dropId)) && !Find(LevelPath()+"."+dropId))))break;
    }
    auto position=Position(parent);position[0]+=32;
    auto location=[&](const Vector& p)->Json{return {{"X",std::to_string(p[0])},{"Y",std::to_string(p[1])},{"Z",std::to_string(p[2])}};};
    Json properties={{"Location",location(position)}};
    Json operations=Json::array();
    if(flag)
    {
        auto cls=Find(type);auto schema=AuthoringSchema(cls).at("DropZone");
        if(schema.at("kind")!="FixedArray" || schema.at("dimension").get<int>()<1)throw std::runtime_error("Flag drop-zone slots are unavailable.");
        auto zones=Json::array();for(int i=0;i<schema.at("dimension").get<int>();++i)zones.push_back("None");
        zones[0]={{"$ref",dropId}};properties["DropZone"]=zones;
        auto dropPosition=Position(parent);dropPosition[1]+=32;
        operations.push_back({{"op","create"},{"id",dropId},{"class","SBase.SFlagDropZone"},{"properties",{{"Location",location(dropPosition)}}}});
    }
    operations.push_back({{"op","create"},{"id",id},{"class",type},{"properties",properties}});
    links.push_back({{"$ref",id}});
    operations.push_back({{"op","update"},{"actor",owner.at("actor")},{"before",owner.at("values")},{"properties",{{property,links}}}});
    auto result=ApplyMapAuthoring({{"format","scct.map-changes"},{"version",1},{"map",AuthoringMapKey()},{"description","Add objective actors and connections"},{"operations",operations}});
    Select(result.at("created"));Redraw();return result.at("created");
}
#include "CameraNetworkNative.inl"
#include "MapDesignNative.inl"
#include "SecurityNative.inl"
}
