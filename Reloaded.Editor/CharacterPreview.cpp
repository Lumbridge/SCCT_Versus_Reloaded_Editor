#include "pch.h"
#undef min
#undef max
#include "CharacterPreview.h"
#include "CharacterPreviewModel.h"
#include "EditorConfigBits.h"
#include "EmitterPreview.h"
#include "Rendering.h"
#include "logger.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

// The engine plumbing (private level, viewport lifetime, guarded native calls, the
// T3D import) follows EmitterPreview.cpp, whose comments give the addresses' reasons.
namespace CharacterPreview
{
namespace
{
    using Address=uintptr_t;
    using Workflow::Json;using Workflow::Vector;
    namespace Frame=Workflow::EmitterPreviewModel;
    // ChaosTheory_Editor.exe, image base 0x10e00000.
    constexpr Address kEditor=0x1165DFA0,kUnrealEd=0x117a59b0,kUndo=0x11691d6c,kError=0x115befb4,kLog=0x115BEFB0,kWarn=0x11691d64,kTransient=0x11697b20;
    constexpr Address kObjects=0x11697B70,kNames=0x1169CFBC,kCriticalError=0x11692eb0,kErrorHistory=0x11691d88,kUnwindCount=0x116913c0,kShutDown=0x116987d0;
    constexpr Address kPackageClass=0x11698488,kLevelClass=0x118235f8,kWindowsViewportClass=0x1168d898;
    constexpr unsigned kTransactional=0x1,kTransientFlag=0x4000,kStandalone=0x80000;
    // Actors 0x8, static meshes 0x20000, child window 0x200, no mouse capture 0x8000,
    // standard view 0x80; never realtime (0x800/0x4000).
    constexpr unsigned kShowFlags=0x20000|0x8000|0x200|0x80|0x8;
    // Light neutral grey: dark camouflage stays readable against it.
    constexpr uint32_t kBackground=0xff5a5e64;
    const char* const kPackage="ReloadedCharacterPreview";
    const char* const kLevelName="ReloadedCharacterPreviewLevel";
    const char* const kViewportName="ReloadedCharacterPreview";
    // SBase's placeable animated mesh actor first; the last resort draws a mesh too.
    const char* const kActorClasses[]={"SAnimatedMesh","StaticMeshActor"};

    using FindFn=Address(__cdecl*)(Address cls,Address outer,const char* name,int exact);                                        // StaticFindObject 0x10face00
    using AllocateFn=Address(__cdecl*)(Address cls,Address outer,int name,unsigned flags,Address from,Address error,Address at,Address root); // StaticAllocateObject 0x10fad900
    using ConstructFn=Address(__cdecl*)(Address cls,Address outer,int name,unsigned flags,Address from,Address error,Address root);          // StaticConstructObject 0x10fadf80
    using NameFn=int*(__thiscall*)(int* self,const char* text,int find);                                                         // FName::FName 0x10fb9610
    using LevelFn=Address(__thiscall*)(Address self,Address engine,int rootOutside);                                             // ULevel::ULevel 0x11128e90
    using SpawnViewFn=void(__thiscall*)(Address level,Address viewport);                                                         // ULevel::SpawnViewActor 0x110bf650
    using DestroyFn=int(__thiscall*)(Address level,Address actor,int netForce);                                                  // ULevel::DestroyActor 0x110baf10
    using FactoryNewFn=Address(__cdecl*)(unsigned size,Address outer,int name,unsigned flags);                                   // ULevelFactory operator new 0x10e05a84
    using FactoryFn=Address(__thiscall*)(Address self);                                                                          // ULevelFactory::ULevelFactory 0x110559f0
    using CreateTextFn=Address(__thiscall*)(Address self,Address level,Address cls,Address parent,int name,unsigned flags,Address context,const char* type,const char** buffer,const char* end,Address warn);
    using NewViewportFn=Address(__thiscall*)(Address client,int name);
    using OpenWindowFn=void(__thiscall*)(Address viewport,HWND parent,int temporary,int width,int height,int x,int y);
    using RepaintFn=void(__thiscall*)(Address viewport,int blit);
    using InitFn=void(__thiscall*)(Address input,Address viewport);
    using DeleteFn=void(__thiscall*)(Address object,int flags);
    using ExecFn=int(__thiscall*)(Address self,const char* command,Address output);
    // UMesh::MeshGetInstance(AActor*) 0x11137e70: the actor's mesh instance (+0x1d4), made
    // for its Mesh (+0x1bc) when missing, as AActor::PlayAnim (0x111892bf) calls it.
    using MeshInstanceFn=Address(__thiscall*)(Address mesh,Address actor);
    // USkeletalMeshInstance::SetSkelAnim(UMeshAnimation*,USkeletalMesh*) 0x111004c0: adds the
    // animation to the instance's linkups (+0xa8), as AActor::execLinkSkelAnim (0x11189546) does.
    using SkelAnimFn=int(__thiscall*)(Address instance,Address animation,Address mesh);
    // AActor::PlayAnim, vtable +0x134 (0x11189250): channel, sequence, rate, tween time,
    // loop and three flags execLoopAnim (0x11185e60) passes as 0.
    using PlayAnimFn=int(__thiscall*)(Address actor,int channel,int sequence,float rate,float tween,int loop,int a,int b,int c);

    bool Copy(void* to,const void* from,size_t size)
    {
        __try { memcpy(to,from,size); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    template<class T> T Read(Address address)
    {
        T value{};if(!address || !Copy(&value,reinterpret_cast<void*>(address),sizeof(T)))throw std::runtime_error("The character preview lost its editor objects. Close and reopen Character Skins.");return value;
    }
    template<class T> void Write(Address address,const T& value)
    {
        if(!address || !Copy(reinterpret_cast<void*>(address),&value,sizeof(T)))throw std::runtime_error("The character preview lost its editor objects. Close and reopen Character Skins.");
    }

    struct Shown { Address actor;std::string team,mesh,pose; };
    struct State
    {
        Address package=0,level=0,viewport=0;
        HWND host=nullptr,test=nullptr,testHost=nullptr;
        std::vector<Shown> figures;Json warnings=Json::array();std::string actorClass;
        int serial=0,viewportsOpened=0,viewportsClosed=0,draws=0;bool faulted=false,dirty=false;std::string fault;
        Vector target{};double distance=300;int pitch=CharacterPreview::Model::kPitch,yaw=CharacterPreview::Model::kYaw;bool user=false;
        int drag=0;POINT last{};DWORD click=0;POINT clickAt{};
    } state;

    HWND Window()
    {
        if(!state.viewport)return nullptr;
        Address window=0;if(!Copy(&window,reinterpret_cast<void*>(state.viewport+0x1b4),4) || !window)return nullptr;
        HWND hwnd=nullptr;return Copy(&hwnd,reinterpret_cast<void*>(window+4),4)?hwnd:nullptr;
    }
    template<class F> DWORD Guarded(F& f)
    {
        __try { f(); return 0; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
    }
    bool ShutDown(){int done=0;return Copy(&done,reinterpret_cast<void*>(kShutDown),4) && done;}
    // A caught native failure undoes the error trail appUnwindThrow left, unless the engine
    // really is going down (EmitterPreview.cpp Native).
    template<class F> DWORD Native(F& f)
    {
        int critical=0,count=0;Copy(&critical,reinterpret_cast<void*>(kCriticalError),4);Copy(&count,reinterpret_cast<void*>(kUnwindCount),4);
        const size_t history=strnlen(reinterpret_cast<const char*>(kErrorHistory),0x1000);
        const DWORD code=Guarded(f);
        if(code && !critical && !ShutDown())
        {
            const char end=0;
            Copy(reinterpret_cast<void*>(kCriticalError),&critical,4);Copy(reinterpret_cast<void*>(kUnwindCount),&count,4);
            if(history<0x1000)Copy(reinterpret_cast<void*>(kErrorHistory+history),&end,1);
        }
        return code;
    }
    void Fault(const char* what,DWORD code)
    {
        char text[400];
        if(ShutDown())sprintf_s(text,"The editor hit an internal error while %s and has shut its engine down. Save your map under a new name now, then restart the editor.",what);
        else sprintf_s(text,"The character preview stopped after an error while %s (exception 0x%08lX). Save your map, then restart the editor to use the preview again.",what,code);
        state.faulted=true;state.fault=text;Logger::log("Character preview: "+state.fault);
        if(HWND window=Window())ShowWindow(window,SW_HIDE);
    }
    template<class F> void Run(const char* what,F f)
    {
        if(DWORD code=Native(f)){Fault(what,code);throw std::runtime_error(state.fault);}
    }
    // GUndo held at null: ULevel::DestroyActor would otherwise save the preview level into
    // a recording transaction (EmitterPreview.cpp Unrecorded).
    template<class F> DWORD Unrecorded(F& f)
    {
        Address undo=0;Copy(&undo,reinterpret_cast<void*>(kUndo),4);
        if(undo){const Address none=0;Copy(reinterpret_cast<void*>(kUndo),&none,4);}
        const DWORD code=Native(f);
        if(undo)Copy(reinterpret_cast<void*>(kUndo),&undo,4);
        return code;
    }
    void CheckEngine()
    {
        if(!state.faulted && ShutDown()){state.faulted=true;state.fault="The editor has shut its engine down after an internal error. Save your map under a new name now, then restart the editor.";if(HWND window=Window())ShowWindow(window,SW_HIDE);}
        if(state.faulted)throw std::runtime_error(state.fault.empty()?"The character preview stopped after an editor error. Restart the editor to use it again.":state.fault);
    }
    bool Alive(Address object,Address cls=0)
    {
        int index=0,count=0;Address table=0,slot=0,type=0;
        return object && Copy(&index,reinterpret_cast<void*>(object+4),4) && Copy(&table,reinterpret_cast<void*>(kObjects),4) && Copy(&count,reinterpret_cast<void*>(kObjects+4),4)
            && index>=0 && index<count && Copy(&slot,reinterpret_cast<void*>(table+index*4),4) && slot==object
            && (!cls || (Copy(&type,reinterpret_cast<void*>(object+0x24),4) && type==cls));
    }
    std::string Name(int index)
    {
        if(index<0 || index>=Read<int>(kNames+4))return {};
        Address entry=Read<Address>(Read<Address>(kNames)+index*4);std::string text;
        for(int i=0;entry && i<1024;++i){char c=Read<char>(entry+12+i);if(!c)break;text+=c;}
        return text;
    }
    std::string NameOf(Address object){return object?Name(Read<int>(object+0x20)):std::string();}
    std::string ClassOf(Address object){return object?NameOf(Read<Address>(object+0x24)):std::string();}
    std::string PathOf(Address object)
    {
        std::string path;
        for(Address o=object;o && path.size()<512;o=Read<Address>(o+0x18))path=NameOf(o)+(path.empty()?"":"."+path);
        return path;
    }
    int MakeName(const char* text){int name=0;reinterpret_cast<NameFn>(0x10fb9610)(&name,text,1);return name;}
    Address Find(Address cls,Address outer,const char* name){return reinterpret_cast<FindFn>(0x10face00)(cls,outer,name,0);}
    Address Editor()
    {
        auto editor=Read<Address>(kEditor);
        if(!editor || !Read<Address>(editor+0x30))throw std::runtime_error("The editor is still starting. Try again in a moment.");
        return editor;
    }
    Address CameraActor(){return Read<Address>(state.viewport+0x30);}
    std::vector<Address> ClientViewports()
    {
        std::vector<Address> out;Address editor=0,client=0,data=0;int count=0;
        if(!Copy(&editor,reinterpret_cast<void*>(kEditor),4) || !editor || !Copy(&client,reinterpret_cast<void*>(editor+0x30),4) || !client)return out;
        if(!Copy(&data,reinterpret_cast<void*>(client+0x2c),4) || !Copy(&count,reinterpret_cast<void*>(client+0x30),4) || count<0 || count>256)return out;
        for(int i=0;i<count;++i){Address v=0;if(Copy(&v,reinterpret_cast<void*>(data+i*4),4))out.push_back(v);}
        return out;
    }
    bool ViewportAlive()
    {
        if(!Alive(state.viewport,kWindowsViewportClass) || !Alive(state.level,kLevelClass))return false;
        auto viewports=ClientViewports();Address camera=0,level=0;
        return std::find(viewports.begin(),viewports.end(),state.viewport)!=viewports.end() && Copy(&camera,reinterpret_cast<void*>(state.viewport+0x30),4) && Alive(camera)
            && Copy(&level,reinterpret_cast<void*>(camera+0x1a4),4) && level==state.level;
    }
    void Forget()
    {
        state.viewport=0;state.host=nullptr;state.drag=0;EmitterPreview::ExemptViewport(nullptr);
    }
    // A host destroyed without Detach took the viewport with it (UWindowsClient::Tick deletes it).
    bool ViewportLost()
    {
        if(!state.viewport || Alive(state.viewport,kWindowsViewportClass))return false;
        Logger::log("Character preview: the viewport closed with its host window");
        Forget();++state.viewportsClosed;return true;
    }
    void Mark(Address object,unsigned set,unsigned clear)
    {
        if(object)Write(object+0x1c,(Read<unsigned>(object+0x1c)|set)&~clear);
    }
    std::vector<Address> LevelActors()
    {
        std::vector<Address> out;Address data=Read<Address>(state.level+0x2c);int count=Read<int>(state.level+0x30);
        if(count<0 || count>100000)throw std::runtime_error("The preview level is damaged. Close and reopen Character Skins.");
        for(int i=0;i<count;++i)out.push_back(Read<Address>(data+i*4));
        return out;
    }
    // A transient, standalone package and level of their own, made once for the session
    // (EmitterPreview.cpp EnsureLevel gives the reasons the map never reaches it).
    void EnsureLevel()
    {
        CheckEngine();
        if(Alive(state.level,kLevelClass) && Alive(state.package,kPackageClass))return;
        if(state.viewport){state.faulted=true;state.fault="The editor released the character preview level. Restart the editor to use the preview again.";throw std::runtime_error(state.fault);}
        Editor();
        Address package=0,level=0;
        Run("creating the preview level",[&]{
            package=Find(kPackageClass,0,kPackage);
            if(!package)package=reinterpret_cast<ConstructFn>(0x10fadf80)(kPackageClass,0,MakeName(kPackage),kTransientFlag|kStandalone,0,*reinterpret_cast<Address*>(kLog),0);
            if(!package)return;
            level=Find(kLevelClass,package,kLevelName);
            if(!level)
            {
                level=reinterpret_cast<AllocateFn>(0x10fad900)(kLevelClass,package,MakeName(kLevelName),0,0,*reinterpret_cast<Address*>(kError),0,0);
                if(level)reinterpret_cast<LevelFn>(0x11128e90)(level,*reinterpret_cast<Address*>(kEditor),0);
            }
        });
        if(!package || !level)throw std::runtime_error("The editor could not create the character preview level.");
        Mark(package,kTransientFlag|kStandalone,0);Mark(level,kTransientFlag|kStandalone,kTransactional);Mark(Read<Address>(level+0x13c),kTransientFlag,kTransactional);
        for(auto actor:std::vector<Address>{Read<Address>(Read<Address>(level+0x2c)),Read<Address>(Read<Address>(level+0x2c)+4)})Mark(actor,kTransientFlag,kTransactional);
        state.package=package;state.level=level;
        Logger::log("Character preview: level created");
    }
    double Aspect()
    {
        int w=0,h=0;
        if(!state.viewport || !Copy(&w,reinterpret_cast<void*>(state.viewport+0xa0),4) || !Copy(&h,reinterpret_cast<void*>(state.viewport+0xa4),4) || w<=0 || h<=0)return 3.0/4;
        return static_cast<double>(w)/h;
    }
    void ApplyCamera()
    {
        if(!state.viewport)return;
        Address camera=CameraActor();
        auto eye=Frame::Eye(state.target,state.distance,state.pitch,state.yaw);
        Write(camera+0x80,std::array<float,3>{static_cast<float>(eye[0]),static_cast<float>(eye[1]),static_cast<float>(eye[2])});
        Write(camera+0xe0,std::array<int,3>{state.pitch,state.yaw,0});
        Write(camera+0x308,static_cast<float>(CharacterPreview::Model::kFov));
        state.dirty=true;
    }
    // Both characters fill the view from the starting angle.
    void Home()
    {
        namespace M=CharacterPreview::Model;
        const auto box=M::Bounds();
        state.user=false;state.pitch=M::kPitch;state.yaw=M::kYaw;
        for(int i=0;i<3;++i)state.target[i]=(box.min[i]+box.max[i])*0.5;
        state.distance=std::clamp(Frame::FitDistance(box,state.target,state.pitch,state.yaw,M::kFov,Aspect(),0.9),40.0,4000.0);
        if(state.viewport)ApplyCamera();
    }
    // A new viewport for host, as EmitterPreview.cpp OpenViewport (the WBrowserStaticMesh::OnCreate
    // sequence with the camera in the preview level). Rendered lit and textured (RendMap 5).
    void OpenViewport(HWND host)
    {
        RECT r{};GetClientRect(host,&r);const int w=std::max<int>(r.right,16),h=std::max<int>(r.bottom,16);
        Address viewport=0;const Address level=state.level;
        auto open=[&]{
            Address editor=*reinterpret_cast<Address*>(kEditor),client=*reinterpret_cast<Address*>(editor+0x30);
            viewport=reinterpret_cast<NewViewportFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(client)+0x80))(client,MakeName(kViewportName));
            if(!viewport)return;
            reinterpret_cast<SpawnViewFn>(0x110bf650)(level,viewport);
            Address camera=*reinterpret_cast<Address*>(viewport+0x30);if(!camera)return;
            *reinterpret_cast<unsigned*>(camera+0x4f0)=kShowFlags;*reinterpret_cast<int*>(camera+0x4fc)=5;
            *reinterpret_cast<int*>(camera+0x4f4)=0;*reinterpret_cast<int*>(camera+0x4f8)=0;
            *reinterpret_cast<int*>(viewport+0x90)=0;*reinterpret_cast<int*>(viewport+0x8c)=0;
            Address input=*reinterpret_cast<Address*>(viewport+0x6c);
            reinterpret_cast<InitFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(input)+0x64))(input,viewport);
            reinterpret_cast<OpenWindowFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(viewport)+0xb0))(viewport,host,0,w,h,0,0);
            Address canvas=*reinterpret_cast<Address*>(viewport+0x68);
            for(int i=0;i<3;++i)*reinterpret_cast<Address*>(canvas+0x60+i*4)=*reinterpret_cast<Address*>(editor+0x250+i*4);
        };
        const DWORD code=Native(open);
        if(viewport){state.viewport=viewport;state.host=host;++state.viewportsOpened;EmitterPreview::ExemptViewport(reinterpret_cast<void*>(viewport));}
        if(code){Fault("opening the preview viewport",code);throw std::runtime_error(state.fault);}
        if(!viewport || !Read<Address>(viewport+0x30))throw std::runtime_error("The editor could not open the character preview viewport.");
        Mark(CameraActor(),kTransientFlag,kTransactional);
        if(state.user)ApplyCamera();else Home();
        Logger::log("Character preview: viewport opened");
    }
    // Deleted through the engine while its window exists, as EmitterPreview.cpp DeleteViewport.
    void DeleteViewport()
    {
        const Address viewport=state.viewport;
        if(!viewport)return;
        if(HWND window=Window();window && GetCapture()==window)ReleaseCapture();
        if(Alive(viewport,kWindowsViewportClass) && !ShutDown())
        {
            Address camera=0,level=0;
            if(Copy(&camera,reinterpret_cast<void*>(viewport+0x30),4) && camera && !(Alive(camera) && Alive(state.level,kLevelClass) && Copy(&level,reinterpret_cast<void*>(camera+0x1a4),4) && level==state.level))
                {const Address none=0;Copy(reinterpret_cast<void*>(viewport+0x30),&none,4);}
            auto remove=[&]{reinterpret_cast<DeleteFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(viewport)+0xc))(viewport,1);};
            if(DWORD code=Unrecorded(remove))Fault("closing the preview viewport",code);
        }
        Forget();++state.viewportsClosed;
        Logger::log("Character preview: viewport closed");
    }
    // One synchronous frame with the preview's own background and without the
    // UseSizingBox overlay (EmitterPreview.cpp Draw).
    DWORD Draw(int blit)
    {
        Address editor=*reinterpret_cast<Address*>(kEditor);uint32_t saved[2]{};
        if(!editor || !Copy(saved,reinterpret_cast<void*>(editor+0xfc),4) || !Copy(saved+1,reinterpret_cast<void*>(editor+0x118),4))return 1;
        Copy(reinterpret_cast<void*>(editor+0xfc),&kBackground,4);Copy(reinterpret_cast<void*>(editor+0x118),&kBackground,4);
        uint32_t sizingAt=0x21c;uint8_t sizingBit=2;EditorConfigBits::Locate("UseSizingBox",sizingAt,sizingBit);
        Address unrealEd=0;unsigned char sizing=0;
        const bool box=Copy(&unrealEd,reinterpret_cast<void*>(kUnrealEd),4) && unrealEd && Copy(&sizing,reinterpret_cast<void*>(unrealEd+sizingAt),1);
        if(box){const unsigned char off=sizing&~sizingBit;Copy(reinterpret_cast<void*>(unrealEd+sizingAt),&off,1);}
        const Address viewport=state.viewport;
        auto paint=[&]{reinterpret_cast<RepaintFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(viewport)+0xc8))(viewport,blit);};
        DWORD code=Native(paint);
        if(box)Copy(reinterpret_cast<void*>(unrealEd+sizingAt),&sizing,1);
        Copy(reinterpret_cast<void*>(editor+0xfc),saved,4);Copy(reinterpret_cast<void*>(editor+0x118),saved+1,4);
        if(!code)++state.draws;
        return code;
    }
    int Exec(const std::string& command)
    {
        Address exec=Editor()+0x28;int result=0;const char* text=command.c_str();
        Run("loading a package",[&]{result=reinterpret_cast<ExecFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(exec)))(exec,text,*reinterpret_cast<Address*>(kLog));});
        return result;
    }
    Address FindPath(const std::string& path)
    {
        Address found=0;const char* text=path.c_str();
        Run("finding a character asset",[&]{found=Find(0,0,text);});
        return found;
    }
    // A model or material whose package is not loaded yet is loaded the way the emitter
    // preview does (OBJ LOAD from Packages\<kind>); the map's own textures are always in.
    Address Load(const std::string& path)
    {
        if(auto found=FindPath(path))return found;
        auto package=path.substr(0,path.find('.'));
        if(package.empty() || package.find_first_of("\"\r\n/\\:")!=std::string::npos)return 0;
        wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
        auto packages=std::filesystem::path(exe).parent_path().parent_path()/"Packages";
        for(const auto& [folder,extension]:std::initializer_list<std::pair<const char*,const char*>>{{"Animations",".ukx"},{"Textures",".utx"},{"StaticMeshes",".usx"}})
        {
            auto file=packages/folder/(package+extension);
            if(std::filesystem::exists(file)){Exec("OBJ LOAD FILE=\""+file.string()+"\"");if(auto found=FindPath(path))return found;}
        }
        return FindPath(path);
    }
    bool IsA(Address object,const char* className)
    {
        std::set<Address> seen;
        for(Address c=object?Read<Address>(object+0x24):0;c && seen.insert(c).second && seen.size()<64;c=Read<Address>(c+0x28))if(NameOf(c)==className)return true;
        return false;
    }
    void Destroy(const std::vector<Address>& actors)
    {
        const Address level=state.level;
        for(auto actor:actors)
        {
            auto destroy=[&]{reinterpret_cast<DestroyFn>(0x110baf10)(level,actor,0);};
            if(DWORD code=Unrecorded(destroy)){Fault("removing a preview character",code);return;}
        }
    }
    // The ULevelFactory import of EmitterPreview.cpp Import, into the preview package.
    void Import(const std::string& t3d)
    {
        const Address level=state.level,package=state.package;const int levelName=Read<int>(level+0x20);
        const char* begin=t3d.c_str();const char* end=begin+t3d.size();
        Address factory=0;
        Run("creating the character importer",[&]{
            factory=reinterpret_cast<FactoryNewFn>(0x10e05a84)(0x68,*reinterpret_cast<Address*>(kTransient),0,0);
            if(factory)reinterpret_cast<FactoryFn>(0x110559f0)(factory);
        });
        if(!factory)throw std::runtime_error("The editor could not create the character importer.");
        const char* cursor=begin;
        auto create=[&]{reinterpret_cast<CreateTextFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(factory)+0x60))(factory,level,kLevelClass,package,levelName,0,0,"paste",&cursor,end,*reinterpret_cast<Address*>(kWarn));};
        const DWORD failed=Native(create);
        auto remove=[&]{reinterpret_cast<DeleteFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(factory)+0xc))(factory,1);};
        const DWORD removed=ShutDown()?0:Native(remove);
        if(failed){Fault("importing the characters",failed);throw std::runtime_error(state.fault);}
        if(removed){Fault("releasing the character importer",removed);throw std::runtime_error(state.fault);}
    }
    // UMeshAnimation::AnimSeqs {+0x44 data,+0x48 num}, 0x2c bytes each with its FName first
    // (AnimationBrowser.cpp UMA_OFF_ANIMSEQS).
    std::vector<std::pair<int,std::string>> Sequences(Address animation)
    {
        std::vector<std::pair<int,std::string>> out;
        const Address data=Read<Address>(animation+0x44);const int count=Read<int>(animation+0x48);
        if(!data || count<0 || count>4096)return out;
        for(int i=0;i<count;++i){const int name=Read<int>(data+i*0x2c);out.push_back({name,Name(name)});}
        return out;
    }
    // The figure moves with its team's animations (a model keeps them, as the map script's
    // LinkSkelAnim does) and holds the pose Model::Pose picks, at its first frame: the
    // preview level never ticks, so nothing plays on, notifies included.
    std::string Animate(Address actor,const std::string& animationPath)
    {
        const Address mesh=Read<Address>(actor+0x1bc);
        if(!mesh || ClassOf(mesh)!="SkeletalMesh")throw std::runtime_error("The preview character has no skeletal mesh.");
        const Address animation=Load(animationPath);
        if(!animation || ClassOf(animation)!="MeshAnimation")throw std::runtime_error(animationPath+" (the team's animations) is not loaded.");
        auto sequences=Sequences(animation);
        std::vector<std::string> names;for(const auto& s:sequences)names.push_back(s.second);
        const int pick=CharacterPreview::Model::Pose(names);
        Address instance=0;int played=0;const int sequence=pick>=0?sequences[pick].first:0;
        Run("posing a preview character",[&]{
            instance=reinterpret_cast<MeshInstanceFn>(0x11137e70)(mesh,actor);
            if(!instance)return;
            reinterpret_cast<SkelAnimFn>(0x111004c0)(instance,animation,0);
            if(pick>=0)played=reinterpret_cast<PlayAnimFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(actor)+0x134))(actor,0,sequence,1.0f,0.0f,1,0,0,0);
        });
        if(!instance || ClassOf(instance)!="SkeletalMeshInstance")throw std::runtime_error("The editor did not make a skeletal mesh instance for the preview character.");
        Mark(instance,kTransientFlag,kTransactional);
        return pick>=0 && played?names[pick]:std::string();
    }
}

bool Attach(HWND host,std::string& error)
{
    try
    {
        if(!host || !IsWindow(host))throw std::runtime_error("The preview panel is not available.");
        EnsureLevel();ViewportLost();
        HWND window=Window();
        if(state.viewport && state.host==host && window && IsWindow(window) && GetAncestor(window,GA_PARENT)==host && ViewportAlive()){Resize();return true;}
        if(state.viewport)DeleteViewport();
        CheckEngine();
        OpenViewport(host);
        Resize();
        return true;
    }
    catch(const std::exception& e){error=e.what();return false;}
}
void Resize()
{
    if(!state.host || !state.viewport || state.faulted)return;
    HWND window=Window();if(!window || !IsWindow(window))return;
    RECT r{};GetClientRect(state.host,&r);
    SetWindowPos(window,HWND_TOP,0,0,std::max<int>(r.right,16),std::max<int>(r.bottom,16),SWP_NOACTIVATE|SWP_SHOWWINDOW);
    try{if(state.user)ApplyCamera();else Home();}catch(const std::exception&){}
    state.dirty=true;
}
void Detach()
{
    // The figures go with the panel: the map's own textures are never held by the preview
    // level once Character Skins closes.
    Clear();
    try{ViewportLost();DeleteViewport();}
    catch(const std::exception& e){Logger::log(std::string("Character preview: ")+e.what());Forget();}
    state.host=nullptr;state.drag=0;
}
Json Show(const Json& slots,const Json& models)
{
    namespace M=CharacterPreview::Model;
    const auto figures=M::Figures(slots,models);
    if(Read<Address>(kUndo))throw std::runtime_error("Finish the current editor operation first.");
    EnsureLevel();
    // Every asset is found (or its package loaded) before anything is replaced, so a
    // field naming something missing leaves the previous look up.
    Json warnings=Json::array();
    for(const auto& f:figures)
    {
        const Address mesh=Load(f.mesh);
        if(!mesh)throw std::runtime_error(f.team+" model: "+f.mesh+" is not loaded. Open its package in the Animation Browser first.");
        if(ClassOf(mesh)!="SkeletalMesh")throw std::runtime_error(f.team+" model: "+f.mesh+" is not a skeletal mesh.");
        for(const auto& skin:f.skins)
        {
            if(skin.empty())continue;
            const Address material=Load(skin);
            if(!material)throw std::runtime_error(skin+" is not loaded. Open its package in the Texture Browser first.");
            if(!IsA(material,"Material"))throw std::runtime_error(skin+" is not a material.");
        }
    }
    Clear();
    CheckEngine();
    const auto prefix="CP"+std::to_string(++state.serial)+"_";
    std::vector<Address> made;std::string used;
    for(const char* actorClass:kActorClasses)
    {
        Import(M::T3D(figures,actorClass,prefix));
        made.clear();
        auto actors=LevelActors();
        for(const auto& f:figures)
        {
            auto found=std::find_if(actors.begin(),actors.end(),[&](Address a){return a && Alive(a) && !(Read<unsigned>(a+0x2e8)&0x8000) && NameOf(a)==prefix+f.team;});
            if(found!=actors.end())made.push_back(*found);
        }
        if(made.size()==figures.size()){used=actorClass;break;}
        Destroy(made);made.clear();
        Logger::log(std::string("Character preview: the editor did not import ")+actorClass+" figures");
    }
    if(made.empty())throw std::runtime_error("The editor did not create the preview characters.");
    state.actorClass=used;
    Json poses=Json::object();
    for(size_t i=0;i<made.size();++i)
    {
        const Address a=made[i];
        // Not selected (+0x2f4 bit 0x40) and not transactional, as the emitter preview's actors.
        Write(a+0x2f4,Read<unsigned>(a+0x2f4)&~0x40u);Mark(a,kTransientFlag,kTransactional);
        state.figures.push_back({a,figures[i].team,figures[i].mesh,""});
        try
        {
            state.figures.back().pose=Animate(a,figures[i].animation);
            if(state.figures.back().pose.empty())warnings.push_back(figures[i].team+": "+figures[i].animation+" has no pose to hold; shown in its reference pose.");
        }
        catch(const std::exception& e)
        {
            if(state.faulted)throw;
            warnings.push_back(figures[i].team+": "+e.what());
        }
        poses[figures[i].team]=state.figures.back().pose;
    }
    // A slot under a team that has a model is kept but not worn, in game as here.
    for(const auto& slot:M::UnusedSlots(slots,models))
        warnings.push_back(slot+" is not worn: that team has a model, which wears its own materials. Clear the model to use the slot.");
    state.warnings=warnings;
    if(state.viewport && ViewportAlive()){if(state.user)ApplyCamera();else Home();}
    state.dirty=true;
    return {{"poses",poses},{"warnings",warnings},{"actorClass",used}};
}
void Clear()
{
    try
    {
        if(state.level && Alive(state.level,kLevelClass) && !ShutDown())
        {
            std::vector<Address> actors;
            auto present=LevelActors();
            for(const auto& f:state.figures)
                if(std::find(present.begin(),present.end(),f.actor)!=present.end() && Alive(f.actor) && !(Read<unsigned>(f.actor+0x2e8)&0x8000))actors.push_back(f.actor);
            Destroy(actors);
        }
    }
    catch(const std::exception& e){Logger::log(std::string("Character preview: ")+e.what());}
    state.figures.clear();state.warnings=Json::array();state.dirty=true;
}
void Tick()
{
    if(state.faulted || !state.viewport || !state.host || !state.dirty)return;
    if(ShutDown()){try{CheckEngine();}catch(const std::exception&){}return;}
    if(*reinterpret_cast<Address*>(kUndo))return;
    if(ViewportLost())return;
    HWND window=Window();
    if(!window || !IsWindowVisible(window))return;
    if(!ViewportAlive()){state.faulted=true;state.fault="The editor released the character preview. Close and reopen Character Skins.";Logger::log("Character preview: "+state.fault);return;}
    try{ApplyCamera();}catch(const std::exception& e){state.faulted=true;state.fault=e.what();return;}
    state.dirty=false;
    if(DWORD code=Draw(1))Fault("drawing the preview",code);
}
bool ViewportMessage(void* viewport,UINT message,WPARAM wParam,LPARAM lParam)
{
    if(!state.viewport || reinterpret_cast<Address>(viewport)!=state.viewport)return false;
    HWND window=Window();
    const POINT at{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
    switch(message)
    {
    case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:case WM_MBUTTONDOWN:
    {
        const DWORD time=GetMessageTime();
        if(message==WM_LBUTTONDOWN && state.click && time-state.click<=GetDoubleClickTime()
            && std::abs(at.x-state.clickAt.x)<=GetSystemMetrics(SM_CXDOUBLECLK) && std::abs(at.y-state.clickAt.y)<=GetSystemMetrics(SM_CYDOUBLECLK)){state.click=0;try{Home();}catch(const std::exception&){}return true;}
        if(message==WM_LBUTTONDOWN){state.click=time;state.clickAt=at;}
        state.drag=message==WM_LBUTTONDOWN?1:message==WM_RBUTTONDOWN?2:3;state.last=at;if(window)SetCapture(window);
        return true;
    }
    case WM_LBUTTONDBLCLK:state.click=0;try{Home();}catch(const std::exception&){}return true;
    case WM_RBUTTONDBLCLK:case WM_MBUTTONDBLCLK:return true;
    case WM_MOUSEMOVE:
        if(state.drag && window && GetCapture()==window)
        {
            const int dx=at.x-state.last.x,dy=at.y-state.last.y;state.last=at;
            if(dx || dy)
            {
                state.user=true;
                if(state.drag==1){state.yaw=(state.yaw+dx*80)&0xffff;state.pitch=std::clamp(state.pitch-dy*80,-15000,15000);}
                else if(state.drag==2)state.distance=std::clamp(state.distance*std::exp(dy*0.01),40.0,4000.0);
                else
                {
                    // Middle drag slides the view across and up, a pixel's worth at the target.
                    const auto axes=Frame::ViewAxes(state.pitch,state.yaw);
                    int height=0;Copy(&height,reinterpret_cast<void*>(state.viewport+0xa4),4);
                    const double pixel=2*state.distance*std::tan(CharacterPreview::Model::kFov*3.14159265358979323846/360)/std::max(height,1)/std::max(Aspect(),0.1);
                    for(int i=0;i<3;++i)state.target[i]+=(-axes.right[i]*dx+axes.up[i]*dy)*pixel;
                }
                state.dirty=true;
            }
        }
        return true;
    case WM_LBUTTONUP:case WM_RBUTTONUP:case WM_MBUTTONUP:
        state.drag=0;if(window && GetCapture()==window)ReleaseCapture();
        return true;
    case WM_MOUSEWHEEL:
        state.user=true;state.distance=std::clamp(state.distance*std::pow(0.85,GET_WHEEL_DELTA_WPARAM(wParam)/120.0),40.0,4000.0);state.dirty=true;
        return true;
    case WM_CAPTURECHANGED:state.drag=0;return true;
    // Never activated or focused by a click: the panel keeps the keyboard and editor
    // commands keep acting on the map views (EmitterPreview.cpp ViewportMessage).
    case WM_MOUSEACTIVATE:return true;
    case WM_SETFOCUS:case WM_KILLFOCUS:return true;
    // The editor does not tick under the modal panel; the panel's timer redraws.
    case WM_PAINT:if(window){PAINTSTRUCT paint{};BeginPaint(window,&paint);EndPaint(window,&paint);}state.dirty=true;return true;
    }
    return message>=WM_KEYFIRST && message<=WM_KEYLAST;
}
void* Viewport(){return reinterpret_cast<void*>(state.viewport);}
Json Capture(const std::string& bmpPath)
{
    CheckEngine();
    ViewportLost();
    if(!state.viewport || !ViewportAlive())throw std::runtime_error("Open the character preview first.");
    const int width=Read<int>(state.viewport+0xa0),height=Read<int>(state.viewport+0xa4);
    ApplyCamera();
    if(DWORD code=Draw(0)){Fault("drawing the preview",code);throw std::runtime_error(state.fault);}
    std::vector<uint32_t> pixels;std::string error;
    const bool read=Rendering::ReadBackBuffer(width,height,pixels,error);
    Draw(1);state.dirty=false;
    if(!read)throw std::runtime_error(error);
    auto stats=Frame::ImageStats(pixels,width,height,kBackground);
    if(!bmpPath.empty())
    {
        BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};
        info.biSize=sizeof(info);info.biWidth=width;info.biHeight=-height;info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
        file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size()*4);
        std::ofstream out(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(bmpPath.data()),bmpPath.size())),std::ios::binary);
        out.write(reinterpret_cast<const char*>(&file),sizeof(file));out.write(reinterpret_cast<const char*>(&info),sizeof(info));
        out.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()*4));
        if(!out)throw std::runtime_error("Could not write the preview image: "+bmpPath);
        stats["path"]=bmpPath;
    }
    return stats;
}
Json State()
{
    ViewportLost();
    Json out={{"faulted",state.faulted},{"fault",state.fault},{"level",Alive(state.level,kLevelClass)},{"package",Alive(state.package,kPackageClass)},{"viewport",state.viewport!=0 && ViewportAlive()},
        {"attached",state.host!=nullptr},{"serial",state.serial},{"draws",state.draws},{"actorClass",state.actorClass},{"warnings",state.warnings},
        {"viewportsOpened",state.viewportsOpened},{"viewportsClosed",state.viewportsClosed},{"clientViewports",ClientViewports().size()},{"engineShutDown",ShutDown()}};
    try
    {
        if(Address editor=Read<Address>(kEditor))
            if(Address trans=Read<Address>(editor+0x148))out["undo"]={{"transactions",Read<int>(trans+0x2c)},{"undoCount",Read<int>(trans+0x34)}};
        if(Alive(state.level,kLevelClass))
        {
            out["levelPath"]=PathOf(state.level);out["levelActors"]=LevelActors().size();
            std::map<std::string,int> classes;
            for(auto a:LevelActors())if(a && Alive(a) && !(Read<unsigned>(a+0x2e8)&0x8000))++classes[ClassOf(a)];
            out["liveActorClasses"]=classes;
            Json figures=Json::array();
            for(const auto& f:state.figures)
            {
                if(!Alive(f.actor))continue;
                const Address mesh=Read<Address>(f.actor+0x1bc),instance=Read<Address>(f.actor+0x1d4);
                figures.push_back({{"team",f.team},{"name",NameOf(f.actor)},{"class",ClassOf(f.actor)},{"outer",NameOf(Read<Address>(f.actor+0x18))},{"mesh",mesh?PathOf(mesh):std::string()},
                    {"instance",ClassOf(instance)},{"pose",f.pose},{"location",Read<std::array<float,3>>(f.actor+0x80)},{"rotation",Read<std::array<int,3>>(f.actor+0xe0)},
                    {"objectFlags",Read<unsigned>(f.actor+0x1c)},{"selected",(Read<unsigned>(f.actor+0x2f4)&0x40)!=0}});
            }
            out["figures"]=figures;
        }
        if(state.viewport && ViewportAlive())
        {
            HWND window=Window();Address camera=CameraActor();
            out["parentIsHost"]=state.host && GetAncestor(window,GA_PARENT)==state.host;out["visible"]=IsWindowVisible(window)!=0;
            out["renDev"]=Read<Address>(state.viewport+0x70)!=0;out["sizeX"]=Read<int>(state.viewport+0xa0);out["sizeY"]=Read<int>(state.viewport+0xa4);
            out["cameraInPreviewLevel"]=Read<Address>(camera+0x1a4)==state.level;out["showFlags"]=Read<unsigned>(camera+0x4f0);out["rendMap"]=Read<int>(camera+0x4fc);
            out["camera"]=Camera();
            RECT a{},b{};GetWindowRect(window,&a);if(state.host)GetWindowRect(state.host,&b);out["fillsHost"]=state.host && EqualRect(&a,&b);
        }
    }
    catch(const std::exception& e){out["error"]=e.what();}
    return out;
}
Json Camera(){return {{"target",state.target},{"distance",state.distance},{"pitch",state.pitch},{"yaw",state.yaw},{"user",state.user}};}
void SetCamera(const Json& camera)
{
    if(camera.contains("target"))state.target=camera.at("target").get<Vector>();
    if(camera.contains("distance"))state.distance=std::clamp(camera.at("distance").get<double>(),40.0,4000.0);
    state.pitch=std::clamp(camera.value("pitch",state.pitch),-15000,15000);state.yaw=camera.value("yaw",state.yaw)&0xffff;state.user=true;
    if(state.viewport && ViewportAlive())ApplyCamera();
}
HWND OpenTestWindow(std::string& error)
{
    ViewportLost();
    if(state.test && IsWindow(state.test))return state.test;
    if(state.viewport && state.host && IsWindow(state.host)){error="The character preview is already shown in another window. Close that window first.";return nullptr;}
    static bool registered=false;
    if(!registered)
    {
        WNDCLASSA wc{};wc.hInstance=GetModuleHandle(nullptr);wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        wc.lpfnWndProc=[](HWND w,UINT m,WPARAM wp,LPARAM lp)->LRESULT{
            if(m==WM_SIZE){if(HWND host=GetDlgItem(w,100)){MoveWindow(host,0,0,LOWORD(lp),HIWORD(lp),TRUE);Resize();}return 0;}
            if(m==WM_DESTROY){if(state.host && state.host==GetDlgItem(w,100))Detach();if(state.test==w){state.test=nullptr;state.testHost=nullptr;}return 0;}
            return DefWindowProcA(w,m,wp,lp);
        };
        wc.lpszClassName="ReloadedCharacterPreviewTest";RegisterClassA(&wc);
        WNDCLASSA host{};host.hInstance=GetModuleHandle(nullptr);host.lpfnWndProc=DefWindowProcA;host.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));host.lpszClassName="ReloadedCharacterPreviewHost";RegisterClassA(&host);
        registered=true;
    }
    state.test=CreateWindowExA(0,"ReloadedCharacterPreviewTest","Character Preview Test",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,416,536,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    if(!state.test){error="The preview test window could not be created.";return nullptr;}
    RECT r{};GetClientRect(state.test,&r);
    state.testHost=CreateWindowExA(0,"ReloadedCharacterPreviewHost","",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,r.right,r.bottom,state.test,reinterpret_cast<HMENU>(100),GetModuleHandle(nullptr),nullptr);
    if(!state.testHost || !Attach(state.testHost,error)){DestroyWindow(state.test);state.test=nullptr;state.testHost=nullptr;if(error.empty())error="The preview panel could not be created.";return nullptr;}
    return state.test;
}
void CloseTestWindow()
{
    if(state.test && IsWindow(state.test))DestroyWindow(state.test);
    state.test=nullptr;state.testHost=nullptr;
}
}
