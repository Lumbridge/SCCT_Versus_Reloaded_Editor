#include "pch.h"
#undef min
#undef max
#include "EmitterPreview.h"
#include "EditorConfigBits.h"
#include "EmitterPreviewModel.h"
#include "MemoryWriter.h"
#include "Rendering.h"
#include "logger.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace EmitterPreview
{
namespace
{
    using Address=uintptr_t;
    using Workflow::Json;using Workflow::Vector;using Workflow::Rotation;using Workflow::Fold;
    namespace Model=Workflow::EmitterPreviewModel;
    // ChaosTheory_Editor.exe, image base 0x10e00000.
    constexpr Address kEditor=0x1165DFA0;          // GEditor (UUnrealEdEngine*)
    constexpr Address kUnrealEd=0x117a59b0;        // GUnrealEd, read by UUnrealEdEngine::Draw's overlays
    constexpr Address kUndo=0x11691d6c;            // GUndo: non-null while a transaction records
    constexpr Address kError=0x115befb4;           // GError, as WBrowserPrefab::RefreshLevel passes it
    constexpr Address kLog=0x115BEFB0;             // output device for UEditorEngine::Exec
    constexpr Address kWarn=0x11691d64;            // GWarn, as edactPasteSelected passes it to FactoryCreateText
    constexpr Address kTransient=0x11697b20;       // transient package: edactPasteSelected's factory outer
    constexpr Address kObjects=0x11697B70;         // GObjObjects {data,count}
    constexpr Address kNames=0x1169CFBC;           // FName table {data,count}; entry text at +12
    constexpr Address kCriticalError=0x11692eb0;   // GIsCriticalError: set by appUnwindThrow (0x10f9c04c) and the error device (0x10e31443)
    constexpr Address kErrorHistory=0x11691d88;    // GErrorHist, char[0x1000]; appUnwindThrow appends " <- Function" (0x10f9c090)
    constexpr Address kUnwindCount=0x116913c0;     // appUnwindThrow's call count, which adds the " <- " separator (0x10f9c05b)
    constexpr Address kShutDown=0x116987d0;        // set once UObject::StaticShutdownAfterError ran (0x10fa6237); the error device calls it on the first appError (0x10e3148a)
    constexpr Address kPackageClass=0x11698488,kLevelClass=0x118235f8,kWindowsViewportClass=0x1168d898;
    constexpr Address kEditorTick=0x1104b1f0;      // UEditorEngine::Tick(float), thiscall
    constexpr unsigned kTransactional=0x1,kTransientFlag=0x4000,kStandalone=0x80000;
    // Particles 0x01000000 (AEmitter::Render gate 0x110db54e), actors 0x8, static meshes
    // 0x20000 (mesh emitters), child window 0x200 (OpenWindow 0x10f7dd3f), no mouse
    // capture 0x8000, standard view 0x80. Never realtime 0x800/0x4000: UEditorEngine::Tick
    // would then tick the whole map (0x1104b235).
    constexpr unsigned kShowFlags=0x01000000|0x20000|0x8000|0x200|0x80|0x8;
    constexpr float kFov=75.0f;
    // Dark neutral grey, the static mesh browser's own clear (0x10ecc0f0): additive fire and
    // sparks stay bright on it and dark AlphaBlend smoke stays visible.
    constexpr uint32_t kBackground=0xff404040;     // FColor for the perspective clear (GEditor+0xfc/+0x118)
    constexpr int kPitch=-2730,kYaw=8192;          // 15 degrees down, 45 degrees round
    // Framing: Show runs the effect kWarm seconds before its first frame so that frame is
    // measured, the camera frames the particles of the last kWindow seconds so they span
    // kFill of the view, and never so far that a typical particle is under kMinPixels wide.
    constexpr double kWarm=1.0,kWindow=3.0,kFill=0.9,kMinPixels=6;
    const char* const kPackage="ReloadedEmitterPreview";
    const char* const kLevelName="ReloadedEmitterPreviewLevel";
    const char* const kViewportName="ReloadedEmitterPreview";
    const char* const kShutDownText="The editor has shut its engine down after an internal error. Save your map under a new name now, then restart the editor.";

    using FindFn=Address(__cdecl*)(Address cls,Address outer,const char* name,int exact);                                        // StaticFindObject 0x10face00
    using AllocateFn=Address(__cdecl*)(Address cls,Address outer,int name,unsigned flags,Address from,Address error,Address at,Address root); // StaticAllocateObject 0x10fad900
    using ConstructFn=Address(__cdecl*)(Address cls,Address outer,int name,unsigned flags,Address from,Address error,Address root);          // StaticConstructObject 0x10fadf80
    using NameFn=int*(__thiscall*)(int* self,const char* text,int find);                                                         // FName::FName 0x10fb9610, FNAME_Add=1
    using LevelFn=Address(__thiscall*)(Address self,Address engine,int rootOutside);                                             // ULevel::ULevel 0x11128e90
    using SpawnViewFn=void(__thiscall*)(Address level,Address viewport);                                                         // ULevel::SpawnViewActor 0x110bf650
    using DestroyFn=int(__thiscall*)(Address level,Address actor,int netForce);                                                  // ULevel::DestroyActor 0x110baf10
    using TickFn=void(__thiscall*)(Address level,int type,float delta);                                                          // ULevel::Tick 0x11184820
    using FactoryNewFn=Address(__cdecl*)(unsigned size,Address outer,int name,unsigned flags);                                   // ULevelFactory operator new 0x10e05a84
    using FactoryFn=Address(__thiscall*)(Address self);                                                                          // ULevelFactory::ULevelFactory 0x110559f0
    using CreateTextFn=Address(__thiscall*)(Address self,Address level,Address cls,Address parent,int name,unsigned flags,Address context,const char* type,const char** buffer,const char* end,Address warn); // vtable +0x60
    using NewViewportFn=Address(__thiscall*)(Address client,int name);                                                           // UWindowsClient::NewViewport, vtable +0x80
    using OpenWindowFn=void(__thiscall*)(Address viewport,HWND parent,int temporary,int width,int height,int x,int y);         // UWindowsViewport::OpenWindow 0x10f7db70, vtable +0xb0
    using RepaintFn=void(__thiscall*)(Address viewport,int blit);                                                                // UWindowsViewport::Repaint, vtable +0xc8
    using InitFn=void(__thiscall*)(Address input,Address viewport);                                                             // UInput::Init, vtable +0x64
    using ResetFn=void(__thiscall*)(Address emitter);                                                                            // UParticleEmitter::Reset, vtable +0x68 (0x110eb480)
    using DeleteFn=void(__thiscall*)(Address object,int flags);                                                                  // scalar deleting destructor, vtable +0xc
    using ExecFn=int(__thiscall*)(Address self,const char* command,Address output);

    bool Copy(void* to,const void* from,size_t size)
    {
        __try { memcpy(to,from,size); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    template<class T> T Read(Address address)
    {
        T value{};if(!address || !Copy(&value,reinterpret_cast<void*>(address),sizeof(T)))throw std::runtime_error("The emitter preview lost its editor objects. Close and reopen the preview.");return value;
    }
    template<class T> void Write(Address address,const T& value)
    {
        if(!address || !Copy(reinterpret_cast<void*>(address),&value,sizeof(T)))throw std::runtime_error("The emitter preview lost its editor objects. Close and reopen the preview.");
    }

    struct Shown { Address actor;std::string name;float dead=0;int resets=0; };
    struct State
    {
        Address package=0,level=0,viewport=0;
        HWND host=nullptr,test=nullptr,testHost=nullptr;
        std::vector<Shown> actors;std::vector<std::string> warnings;
        int serial=0,viewportsOpened=0,viewportsClosed=0;bool faulted=false;std::string fault;
        // Camera: orbits target at distance, easing to goal/goalDistance. home* is the entry's
        // starting view (its preview hint, else the estimate from its ranges); extent is the
        // framed half width/height, measured once it comes from particles.
        Vector target{},goal{},homeTarget{};double distance=256,goalDistance=256,homeDistance=256;int pitch=kPitch,yaw=kYaw,homePitch=kPitch,homeYaw=kYaw;
        bool user=false,measured=false;double extent[2]{};
        // Particles of the last kWindow seconds of preview time, one snapshot each 0.1 s.
        double age=0,sampledAt=-1e9,fittedAt=-1e9,shrinkSince=-1;std::deque<std::pair<double,std::vector<Model::Sample>>> samples;
        // Input
        int drag=0;POINT last{};DWORD click=0;POINT clickAt{};
        // Timing
        LARGE_INTEGER frequency{},previous{},second{};unsigned long long ticks=0,frames=0,loops=0,framesAtSecond=0;double frameRate=0;float idle=0;
    } state;
    Address wireExempt=0;   // the preview viewport, read by IsWireHook and SelectionInfoHook

    HWND Window()
    {
        if(!state.viewport)return nullptr;
        Address window=0;if(!Copy(&window,reinterpret_cast<void*>(state.viewport+0x1b4),4) || !window)return nullptr;
        HWND hwnd=nullptr;return Copy(&hwnd,reinterpret_cast<void*>(window+4),4)?hwnd:nullptr;
    }
    // Native sequences run under SEH: an access violation, or an engine error thrown
    // through its guard/unguard chain, becomes a message instead of a crash. The
    // callables hold only native calls and plain values.
    template<class F> DWORD Guarded(F& f)
    {
        __try { f(); return 0; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
    }
    bool ShutDown(){int done=0;return Copy(&done,reinterpret_cast<void*>(kShutDown),4) && done;}
    // Every native unguard an exception passes runs appUnwindThrow (0x10f9c030), which sets
    // GIsCriticalError and appends to GErrorHist. An appError has also run
    // UObject::StaticShutdownAfterError (0x10fa61e0) on every object, and the engine cannot
    // go on; otherwise the trail is undone, so a later real crash reports its own history.
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
    std::string Failure(const char* what,DWORD code)
    {
        char text[400];
        if(ShutDown())sprintf_s(text,"The editor hit an internal error while %s and has shut its engine down. Save your map under a new name now, then restart the editor.",what);
        else sprintf_s(text,"The emitter preview stopped after an error while %s (exception 0x%08lX). Save your map, then restart the editor to use the preview again.",what,code);
        return text;
    }
    // A native failure leaves engine state unknown: the preview stops for the session.
    void Fault(const char* what,DWORD code)
    {
        state.faulted=true;state.fault=Failure(what,code);Logger::log("Emitter preview: "+state.fault);
        if(HWND window=Window())ShowWindow(window,SW_HIDE);
    }
    template<class F> void Run(const char* what,F f)
    {
        if(DWORD code=Native(f)){Fault(what,code);throw std::runtime_error(state.fault);}
    }
    // ULevel::DestroyActor saves the level's Actors into GUndo whenever a transaction records,
    // transactional or not (0x110bafc8..0x110baff5). Preview objects never belong in the map's
    // Undo history, so GUndo is held at null around the native call.
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
        if(!state.faulted && ShutDown()){state.faulted=true;state.fault=kShutDownText;Logger::log(std::string("Emitter preview: ")+kShutDownText);if(HWND window=Window())ShowWindow(window,SW_HIDE);}
        if(state.faulted)throw std::runtime_error(state.fault.empty()?"The emitter preview stopped after an editor error. Restart the editor to use it again.":state.fault);
    }
    // Still registered in GObjObjects at its own index (UObject::Index +4) and, when given,
    // still of its class (+0x24), so a recycled slot and address cannot pass for it.
    bool Alive(Address object,Address cls=0)
    {
        int index=0,count=0;Address table=0,slot=0,type=0;
        return object && Copy(&index,reinterpret_cast<void*>(object+4),4) && Copy(&table,reinterpret_cast<void*>(kObjects),4) && Copy(&count,reinterpret_cast<void*>(kObjects+4),4)
            && index>=0 && index<count && Copy(&slot,reinterpret_cast<void*>(table+index*4),4) && slot==object
            && (!cls || (Copy(&type,reinterpret_cast<void*>(object+0x24),4) && type==cls));
    }
    std::string NameOf(Address object)
    {
        int index=Read<int>(object+0x20);if(index<0 || index>=Read<int>(kNames+4))return {};
        Address entry=Read<Address>(Read<Address>(kNames)+index*4);std::string text;
        for(int i=0;entry && i<1024;++i){char c=Read<char>(entry+12+i);if(!c)break;text+=c;}
        return text;
    }
    std::string Convert(const std::string& value,UINT from,UINT to)
    {
        if(value.empty())return {};
        int count=MultiByteToWideChar(from,0,value.data(),static_cast<int>(value.size()),nullptr,0);
        if(!count)throw std::runtime_error("The emitter text could not be decoded.");
        std::wstring wide(count,L'\0');MultiByteToWideChar(from,0,value.data(),static_cast<int>(value.size()),wide.data(),count);
        count=WideCharToMultiByte(to,0,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr);
        std::string out(count,'\0');WideCharToMultiByte(to,0,wide.data(),static_cast<int>(wide.size()),out.data(),count,nullptr,nullptr);return out;
    }
    int Name(const char* text){int name=0;reinterpret_cast<NameFn>(0x10fb9610)(&name,text,1);return name;}
    Address Find(Address cls,Address outer,const char* name){return reinterpret_cast<FindFn>(0x10face00)(cls,outer,name,0);}
    Address Editor()
    {
        auto editor=Read<Address>(kEditor);
        if(!editor || !Read<Address>(editor+0x30))throw std::runtime_error("The editor is still starting. Try the preview again in a moment.");
        return editor;
    }

    Address CameraActor(){return Read<Address>(state.viewport+0x30);}
    // UWindowsClient::Viewports {+0x2c data,+0x30 num}, the list ViewportWndProc checks (0x10f7e391).
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
    // A host destroyed without Detach takes the viewport window with it, and
    // UWindowsClient::Tick then deletes the viewport (0x10f7b339) and its camera. Forget it
    // so the next Attach opens a new one.
    bool ViewportLost()
    {
        if(!state.viewport || Alive(state.viewport,kWindowsViewportClass))return false;
        Logger::log("Emitter preview: the viewport closed with its host window; the next Attach opens a new one");
        state.viewport=0;wireExempt=0;state.host=nullptr;state.drag=0;++state.viewportsClosed;return true;
    }
    void Mark(Address object,unsigned set,unsigned clear)
    {
        if(!object)return;
        Write(object+0x1c,(Read<unsigned>(object+0x1c)|set)&~clear);
    }
    std::vector<Address> LevelActors()
    {
        std::vector<Address> out;Address data=Read<Address>(state.level+0x2c);int count=Read<int>(state.level+0x30);
        if(count<0 || count>100000)throw std::runtime_error("The preview level is damaged. Close and reopen the preview.");
        for(int i=0;i<count;++i)out.push_back(Read<Address>(data+i*4));
        return out;
    }
    std::vector<Address> Emitters(Address actor)
    {
        std::vector<Address> out;Address data=Read<Address>(actor+0x2f8);int count=Read<int>(actor+0x2fc);
        if(count<0 || count>256)return out;
        for(int i=0;i<count;++i)if(auto e=Read<Address>(data+i*4))out.push_back(e);
        return out;
    }
    // Emitter actors still in the preview level (not destroyed, bDeleteMe +0x2e8 bit 0x8000 clear).
    std::vector<Address> LiveActors()
    {
        std::vector<Address> out;if(!state.level)return out;
        auto actors=LevelActors();
        for(auto& p:state.actors)
            if(std::find(actors.begin(),actors.end(),p.actor)!=actors.end() && Alive(p.actor) && !(Read<unsigned>(p.actor+0x2e8)&0x8000))out.push_back(p.actor);
        return out;
    }
    // Live FParticles (stride 200, flags +188 bit 1) below ActiveParticles (+0x3b0) of Particles {+0x3a4 data,+0x3a8 num}.
    template<class F> int EachParticle(Address emitter,F f)
    {
        Address data=Read<Address>(emitter+0x3a4);int count=std::min(Read<int>(emitter+0x3a8),Read<int>(emitter+0x3b0)),live=0;
        if(!data || count<0 || count>100000)return 0;
        for(int i=0;i<count;++i){Address p=data+i*200;if(Read<unsigned>(p+188)&1){++live;f(p);}}
        return live;
    }
    int ParticleCount()
    {
        int total=0;
        for(auto a:LiveActors())for(auto e:Emitters(a))total+=EachParticle(e,[](Address){});
        return total;
    }

    // Private level in a transient, standalone package: UEditorEngine::Cleanse keeps
    // RF_Standalone|RF_Native objects (0x11049413), map saves only walk the map's own
    // package, and ULevel::RememberActors/ReconcileActors only visit viewports whose
    // camera is in the map (0x1111ee39), so map changes never reach it. It lives for the
    // session; viewports come and go with their hosts.
    void EnsureLevel()
    {
        CheckEngine();
        if(Alive(state.level,kLevelClass) && Alive(state.package,kPackageClass))return;
        if(state.viewport)
        {
            state.faulted=true;state.fault="The editor released the preview level. Restart the editor to use the emitter preview again.";
            Logger::log("Emitter preview: "+state.fault);throw std::runtime_error(state.fault);
        }
        Editor();
        Address package=0,level=0;
        Run("creating the preview level",[&]{
            package=Find(kPackageClass,0,kPackage);
            if(!package)package=reinterpret_cast<ConstructFn>(0x10fadf80)(kPackageClass,0,Name(kPackage),kTransientFlag|kStandalone,0,*reinterpret_cast<Address*>(kLog),0);
            if(!package)return;
            level=Find(kLevelClass,package,kLevelName);
            if(!level)
            {
                level=reinterpret_cast<AllocateFn>(0x10fad900)(kLevelClass,package,Name(kLevelName),0,0,*reinterpret_cast<Address*>(kError),0,0);
                if(level)reinterpret_cast<LevelFn>(0x11128e90)(level,*reinterpret_cast<Address*>(kEditor),0);
            }
        });
        if(!package || !level)throw std::runtime_error("The editor could not create the preview level.");
        // ULevel::ULevel sets RF_Transactional on the level and model (0x11129016, 0x11129060).
        Mark(package,kTransientFlag|kStandalone,0);Mark(level,kTransientFlag|kStandalone,kTransactional);Mark(Read<Address>(level+0x13c),kTransientFlag,kTransactional);
        for(auto actor:std::vector<Address>{Read<Address>(Read<Address>(level+0x2c)),Read<Address>(Read<Address>(level+0x2c)+4)})Mark(actor,kTransientFlag,kTransactional);
        state.package=package;state.level=level;
        Logger::log("Emitter preview: level created");
    }
    int Pixels(){int w=0;return state.viewport && Copy(&w,reinterpret_cast<void*>(state.viewport+0xa0),4) && w>0?w:480;}
    double Aspect()
    {
        int w=0,h=0;
        if(!state.viewport || !Copy(&w,reinterpret_cast<void*>(state.viewport+0xa0),4) || !Copy(&h,reinterpret_cast<void*>(state.viewport+0xa4),4) || w<=0 || h<=0)return 4.0/3;
        return static_cast<double>(w)/h;
    }
    void ApplyCamera()
    {
        if(!state.viewport)return;
        Address camera=CameraActor();
        auto eye=Model::Eye(state.target,state.distance,state.pitch,state.yaw);
        Write(camera+0x80,std::array<float,3>{static_cast<float>(eye[0]),static_cast<float>(eye[1]),static_cast<float>(eye[2])});
        Write(camera+0xe0,std::array<int,3>{state.pitch,state.yaw,0});
        Write(camera+0x308,kFov);
    }
    // One snapshot of up to 256 live particles (evenly strided) in world space.
    // CoordinateSystem (+0xd8) PTCS_Relative (1) keeps particles relative to the owner's
    // Location (+0x80); Independent and Absolute ones are stored in world space
    // (SpawnParticle 0x110ebc10). A mesh particle's Size scales its mesh, taken as 64
    // units as Model::Reach does. Visibility is the faded FParticle Color (+168, BGRA):
    // alpha for AlphaBlend, Modulated and AlphaModulate (DrawStyle +0xde 1, 2, 4), which fade
    // alpha only; the brightest channel for the additive styles, which fade the colour.
    void Collect()
    {
        std::vector<Model::Sample> all;
        for(auto a:LiveActors())
        {
            const auto origin=Read<std::array<float,3>>(a+0x80);
            for(auto e:Emitters(a))
            {
                const bool relative=Read<unsigned char>(e+0xd8)==1,mesh=NameOf(Read<Address>(e+0x24))=="MeshEmitter";
                const unsigned char style=Read<unsigned char>(e+0xde);const bool alpha=style==1 || style==2 || style==4;
                EachParticle(e,[&](Address p){
                    const auto at=Read<std::array<float,3>>(p);const double size=std::abs(static_cast<double>(Read<float>(p+108)))*(mesh?64:1);
                    if(!std::isfinite(at[0]) || !std::isfinite(at[1]) || !std::isfinite(at[2]) || !std::isfinite(size))return;
                    const unsigned color=Read<unsigned>(p+168);
                    const unsigned level=alpha?color>>24:std::max({color&0xff,(color>>8)&0xff,(color>>16)&0xff});
                    all.push_back({{at[0]+(relative?origin[0]:0.0f),at[1]+(relative?origin[1]:0.0f),at[2]+(relative?origin[2]:0.0f)},std::min(size,2048.0),level/255.0});
                });
            }
        }
        if(all.size()>256){std::vector<Model::Sample> kept(256);for(size_t i=0;i<256;++i)kept[i]=all[i*all.size()/256];all.swap(kept);}
        state.samples.emplace_back(state.age,std::move(all));
        while(!state.samples.empty() && state.age-state.samples.front().first>kWindow)state.samples.pop_front();
    }
    // Frames what the visible particles of the last kWindow seconds cover (Model::Fit), on
    // the view axes. The first measured frame replaces the starting view, closer or
    // further; after that the camera pulls back at once for a growing plume but only
    // closes in once the effect has stayed clearly smaller for 1.5 s, so looping bursts do
    // not pump the view. Nothing moves once the user has moved the camera.
    void Reframe(bool jump)
    {
        if(state.user)return;
        std::vector<Model::Sample> all;for(const auto& snapshot:state.samples)all.insert(all.end(),snapshot.second.begin(),snapshot.second.end());
        if(all.size()<3)return;
        const auto view=Model::Fit(all,state.pitch,state.yaw,kFov,Aspect(),kFill,Pixels(),kMinPixels);
        if(!view.valid)return;
        const auto& center=view.target;const double want=view.distance;
        if(jump || !state.measured)
        {
            state.goal=center;state.goalDistance=want;state.extent[0]=view.width;state.extent[1]=view.height;state.measured=true;state.shrinkSince=-1;
            if(jump){state.target=state.goal;state.distance=state.goalDistance;}
            return;
        }
        bool changed=false;
        if(want>state.goalDistance*1.08){state.goalDistance=want;state.shrinkSince=-1;changed=true;}
        else if(want<state.goalDistance*0.8)
        {
            if(state.shrinkSince<0)state.shrinkSince=state.age;
            else if(state.age-state.shrinkSince>=1.5){state.goalDistance=want;state.shrinkSince=-1;changed=true;}
        }
        else state.shrinkSince=-1;
        double offset=0;for(int i=0;i<3;++i)offset+=(center[i]-state.goal[i])*(center[i]-state.goal[i]);
        if(changed || std::sqrt(offset)>state.goalDistance*0.06){state.goal=center;state.extent[0]=view.width;state.extent[1]=view.height;}
    }
    void Refit(float delta)
    {
        state.age+=delta;
        if(state.age-state.sampledAt>=0.1){state.sampledAt=state.age;Collect();}
        if(state.age-state.fittedAt>=0.25){state.fittedAt=state.age;Reframe(false);}
        const double k=std::min(1.0,delta*3.0);
        for(int i=0;i<3;++i)state.target[i]+=(state.goal[i]-state.target[i])*k;
        state.distance+=(state.goalDistance-state.distance)*k;
    }
    // The entry's own view again (double-click): its starting orientation, framed on what
    // the particles cover now.
    void Home()
    {
        state.user=false;state.pitch=state.homePitch;state.yaw=state.homeYaw;state.goal=state.homeTarget;state.goalDistance=state.homeDistance;
        state.measured=false;state.shrinkSince=-1;
        try{Reframe(false);}catch(const std::exception&){}
    }
    // A new viewport for host, the sequence of WBrowserStaticMesh::OnCreate (0x10e8516d..
    // 0x10e852e6) with the camera spawned in the preview level instead of GEditor->Level.
    // OpenWindow creates the window as host's child (0x10f7dd45) and keeps host as
    // ParentWindow (0x10f7ddb5), so the viewport lives and dies in this one host.
    void OpenViewport(HWND host)
    {
        RECT r{};GetClientRect(host,&r);const int w=std::max<int>(r.right,16),h=std::max<int>(r.bottom,16);
        Address viewport=0;const Address level=state.level;
        auto open=[&]{
            Address editor=*reinterpret_cast<Address*>(kEditor),client=*reinterpret_cast<Address*>(editor+0x30);
            viewport=reinterpret_cast<NewViewportFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(client)+0x80))(client,Name(kViewportName));
            if(!viewport)return;
            reinterpret_cast<SpawnViewFn>(0x110bf650)(level,viewport);
            Address camera=*reinterpret_cast<Address*>(viewport+0x30);if(!camera)return;
            *reinterpret_cast<unsigned*>(camera+0x4f0)=kShowFlags;*reinterpret_cast<int*>(camera+0x4fc)=5;
            *reinterpret_cast<int*>(camera+0x4f4)=0;*reinterpret_cast<int*>(camera+0x4f8)=0;
            *reinterpret_cast<int*>(viewport+0x90)=0;*reinterpret_cast<int*>(viewport+0x8c)=0;
            Address input=*reinterpret_cast<Address*>(viewport+0x6c);
            reinterpret_cast<InitFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(input)+0x64))(input,viewport);
            // OpenWindow creates RenDev itself when it is null (0x10f7de64); the browsers'
            // extra vtable +0xcc call only adds SetForegroundWindow/AttachThreadInput.
            reinterpret_cast<OpenWindowFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(viewport)+0xb0))(viewport,host,0,w,h,0,0);
            Address canvas=*reinterpret_cast<Address*>(viewport+0x68);
            for(int i=0;i<3;++i)*reinterpret_cast<Address*>(canvas+0x60+i*4)=*reinterpret_cast<Address*>(editor+0x250+i*4);
        };
        const DWORD code=Native(open);
        if(viewport){state.viewport=viewport;wireExempt=viewport;state.host=host;++state.viewportsOpened;}
        if(code){Fault("opening the preview viewport",code);throw std::runtime_error(state.fault);}
        if(!viewport || !Read<Address>(viewport+0x30))throw std::runtime_error("The editor could not open the preview viewport.");
        Mark(CameraActor(),kTransientFlag,kTransactional);
        if(!Read<Address>(viewport+0x70))Logger::log("Emitter preview: viewport opened without a render device");
        ApplyCamera();
        Logger::log("Emitter preview: viewport opened");
    }
    // Deletes the viewport the way WBrowserStaticMesh::OnDestroy does (0x10e6b49f: scalar
    // deleting destructor, vtable +0xc), while its window still exists. UWindowsViewport::
    // Destroy (0x10f7b680) runs UViewport::Destroy (0x1109d3c0): CloseWindow destroys the
    // window (0x10f7b8b1); Input, Console and Canvas are deleted; RenDev->Exit(this) only
    // flushes the shared device's caches (0x10f0bb27) and GRenDev is kept (0x1109d485); the
    // viewport leaves Client->Viewports (0x1109d4a8); UPlayer::Destroy destroys the camera in
    // its XLevel, the preview level (0x1112d987).
    void DeleteViewport()
    {
        const Address viewport=state.viewport;
        if(!viewport)return;
        if(HWND window=Window();window && GetCapture()==window)ReleaseCapture();
        state.drag=0;
        if(Alive(viewport,kWindowsViewportClass) && !ShutDown())
        {
            // UPlayer::Destroy skips a null Actor (0x1112d961): a camera that is not alive in
            // the preview level is left alone.
            Address camera=0,level=0;
            if(Copy(&camera,reinterpret_cast<void*>(viewport+0x30),4) && camera && !(Alive(camera) && Alive(state.level,kLevelClass) && Copy(&level,reinterpret_cast<void*>(camera+0x1a4),4) && level==state.level))
                {const Address none=0;Copy(reinterpret_cast<void*>(viewport+0x30),&none,4);}
            auto remove=[&]{reinterpret_cast<DeleteFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(viewport)+0xc))(viewport,1);};
            if(DWORD code=Unrecorded(remove))Fault("closing the preview viewport",code);
        }
        state.viewport=0;wireExempt=0;state.host=nullptr;++state.viewportsClosed;
        Logger::log("Emitter preview: viewport closed");
    }
    DWORD Draw(int blit)
    {
        // The perspective clear colour is the editor's own setting (0x10ecc0c6/0x10ecc0db):
        // swap in the preview background for this synchronous draw only.
        Address editor=*reinterpret_cast<Address*>(kEditor);uint32_t saved[2]{};
        if(!editor || !Copy(saved,reinterpret_cast<void*>(editor+0xfc),4) || !Copy(saved+1,reinterpret_cast<void*>(editor+0x118),4))return 1;
        Copy(reinterpret_cast<void*>(editor+0xfc),&kBackground,4);Copy(reinterpret_cast<void*>(editor+0x118),&kBackground,4);
        // The UseSizingBox overlay (0x10eccef7; GUnrealEd+0x21c bit 2 under the stock
        // packages, wherever EditorConfigBits found it otherwise) prints the map's
        // selected actor into every viewport; it has no place in the preview.
        uint32_t sizingAt=0x21c;uint8_t sizingBit=2;EditorConfigBits::Locate("UseSizingBox",sizingAt,sizingBit);
        Address unrealEd=0;unsigned char sizing=0;
        const bool box=Copy(&unrealEd,reinterpret_cast<void*>(kUnrealEd),4) && unrealEd && Copy(&sizing,reinterpret_cast<void*>(unrealEd+sizingAt),1);
        if(box){const unsigned char off=sizing&~sizingBit;Copy(reinterpret_cast<void*>(unrealEd+sizingAt),&off,1);}
        const Address viewport=state.viewport;
        auto paint=[&]{reinterpret_cast<RepaintFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(viewport)+0xc8))(viewport,blit);};
        DWORD code=Native(paint);
        if(box)Copy(reinterpret_cast<void*>(unrealEd+sizingAt),&sizing,1);
        Copy(reinterpret_cast<void*>(editor+0xfc),saved,4);Copy(reinterpret_cast<void*>(editor+0x118),saved+1,4);
        return code;
    }
    // Sub-emitters that end on their own: one-shots (RespawnDeadParticles off, +0x1e0 bit
    // 0x100) and trigger spawners (SpawnOnTriggerRange +0x320/+0x324). dead: every one of
    // them has AllParticlesDead (+0x1e4 bit 0x10) or is Disabled (+0x1e0 bit 0x800).
    std::vector<Address> Bursts(Address actor,bool& dead)
    {
        std::vector<Address> bursts;dead=true;
        for(auto e:Emitters(actor))
        {
            const bool trigger=std::max(Read<float>(e+0x320),Read<float>(e+0x324))>=1;
            if((Read<unsigned>(e+0x1e0)&0x100) && !trigger)continue;
            bursts.push_back(e);
            if(!(Read<unsigned>(e+0x1e4)&0x10) && !(Read<unsigned>(e+0x1e0)&0x800))dead=false;
        }
        return bursts;
    }
    // Starts bursts again together, as AEmitter::Tick's AutoReset does (0x110dbc82), with
    // trigger spawners re-armed the way execTrigger does (CurrentSpawnOnTrigger +0x3e4,
    // 0x110eb378).
    bool Restart(const std::vector<Address>& bursts)
    {
        for(auto e:bursts)
        {
            Write(e+0x1e0,Read<unsigned>(e+0x1e0)&~0x800u);
            auto reset=[&]{reinterpret_cast<ResetFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(e)+0x68))(e);};
            if(DWORD code=Native(reset)){Fault("resetting a burst",code);return false;}
            const float lo=Read<float>(e+0x320),hi=Read<float>(e+0x324);
            if(std::max(lo,hi)>=1)
            {
                Write(e+0x3e4,std::max(1,static_cast<int>(std::lround((lo+hi)*0.5))));
                Write(e+0x1e4,Read<unsigned>(e+0x1e4)&~0x18u);
            }
        }
        return true;
    }
    // Once every burst of an actor has been dead for 0.6 s they start again, so one-shot
    // and triggered effects loop as in-game triggers would fire them. An actor with its
    // own AutoReset (+0x304 bit 2, a looped placement) waits its TimeTillResetRange
    // (+0x320/+0x324) and restarts itself in AEmitter::Tick (0x110dbec5); the preview
    // only steps in if that has not happened 0.6 s after the longest wait.
    void Loop(float delta)
    {
        for(auto& p:state.actors)
        {
            if(!Alive(p.actor) || (Read<unsigned>(p.actor+0x2e8)&0x8000))continue;
            bool dead=true;auto bursts=Bursts(p.actor,dead);
            if(bursts.empty() || !dead){p.dead=0;continue;}
            const bool own=(Read<unsigned>(p.actor+0x304)&2)!=0;
            const float wait=0.6f+(own?std::clamp(std::max(Read<float>(p.actor+0x320),Read<float>(p.actor+0x324)),0.0f,3600.0f):0.0f);
            if((p.dead+=delta)<wait)continue;
            p.dead=0;++p.resets;++state.loops;
            if(!Restart(bursts))return;
        }
    }
    // One preview frame: the preview level ticks in ViewportsOnly mode (1), as
    // UEditorEngine::Tick ticks a realtime map (0x1104b279): emitters update and no game
    // logic runs. Only the preview level is touched.
    bool Advance(float delta,bool draw)
    {
        try
        {
            const Address level=state.level;
            auto tick=[&]{reinterpret_cast<TickFn>(0x11184820)(level,1,delta);};
            if(DWORD code=Native(tick)){Fault("ticking the preview level",code);return false;}
            Loop(delta);if(state.faulted)return false;
            Refit(delta);ApplyCamera();
            if(draw)if(DWORD code=Draw(1)){Fault("drawing the preview",code);return false;}
            return true;
        }
        catch(const std::exception& e){state.faulted=true;state.fault=e.what();Logger::log(std::string("Emitter preview: ")+e.what());return false;}
    }
    void Tick()
    {
        if(state.faulted || !state.viewport || !state.host)return;
        if(ShutDown()){try{CheckEngine();}catch(const std::exception&){}return;}
        LARGE_INTEGER now{};QueryPerformanceCounter(&now);
        if(!state.frequency.QuadPart){QueryPerformanceFrequency(&state.frequency);state.previous=state.second=now;return;}
        const double elapsed=static_cast<double>(now.QuadPart-state.previous.QuadPart)/state.frequency.QuadPart;
        if(elapsed<1.0/60)return;
        state.previous=now;++state.ticks;
        if(*reinterpret_cast<Address*>(kUndo))return;   // a transaction is recording: leave every object alone
        if(ViewportLost())return;
        HWND window=Window();
        if(!window || !IsWindowVisible(window) || IsIconic(GetAncestor(state.host,GA_ROOT)))return;
        if(!ViewportAlive()){state.faulted=true;state.fault="The editor released the emitter preview. Restart the editor to use it again.";Logger::log("Emitter preview: "+state.fault);return;}
        const float delta=static_cast<float>(std::min(elapsed,0.1));
        // With nothing shown, the empty frame is redrawn twice a second only.
        if(state.actors.empty() && (state.idle+=delta)<0.5f)return;
        state.idle=0;
        if(Advance(delta,true))++state.frames;
        const double second=static_cast<double>(now.QuadPart-state.second.QuadPart)/state.frequency.QuadPart;
        if(second>=1){state.frameRate=(state.frames-state.framesAtSecond)/second;state.framesAtSecond=state.frames;state.second=now;}
    }
    void __cdecl PreviewTick(){Tick();}
    __declspec(naked) void EditorTickHook()
    {
        static Address resume=kEditorTick+5;
        __asm
        {
            pushfd
            pushad
            call PreviewTick
            popad
            popfd
            // Replay the five displaced bytes of the native SEH prologue.
            push ebp
            mov ebp,esp
            push -1
            jmp dword ptr [resume]
        }
    }
    // UViewport::IsWire (0x1109c700) calls every level without BSP nodes (Model+0x58)
    // wireframe, so the empty preview level would draw its particles as wire
    // (UParticleMaterial.Wireframe, set at 0x110f03bf). The preview is never wire.
    constexpr Address kIsWire=0x1109c700;
    __declspec(naked) void IsWireHook()
    {
        static Address resume=kIsWire+5;
        __asm
        {
            cmp ecx,dword ptr [wireExempt]
            je solid
            // Replay the five displaced bytes: mov eax,[ecx+0x30]; test eax,eax.
            mov eax,dword ptr [ecx+0x30]
            test eax,eax
            jmp dword ptr [resume]
        solid:
            xor eax,eax
            ret
        }
    }
    // UUnrealEdEngine::Draw prints the map's selected actor (Name/Tag/Platform, loop at
    // 0x10ecd319, reached with and without UseSizingBox) into every perspective viewport;
    // the preview skips to the no-selection path at 0x10ecd828 (edx = GUnrealEd, edi = 0
    // on both routes in).
    constexpr Address kSelectionInfo=0x10ecd319;
    __declspec(naked) void SelectionInfoHook()
    {
        static Address resume=kSelectionInfo+5,skip=0x10ecd828;
        __asm
        {
            // Replay the five displaced bytes: mov esi,[ebp-0x30]; xor ecx,ecx.
            mov esi,dword ptr [ebp-0x30]
            xor ecx,ecx
            mov eax,dword ptr [ebp+8]
            cmp eax,dword ptr [wireExempt]
            je no_info
            jmp dword ptr [resume]
        no_info:
            jmp dword ptr [skip]
        }
    }
    void Destroy(const std::vector<Address>& actors)
    {
        const Address level=state.level;
        for(auto actor:actors)
        {
            auto destroy=[&]{reinterpret_cast<DestroyFn>(0x110baf10)(level,actor,0);};
            if(DWORD code=Unrecorded(destroy)){Fault("removing a preview emitter",code);return;}
        }
    }
    // Emitter class test on the T3D Class= token (short or Package.Class), resolved
    // like ParseObject<UClass>(...,ANY_PACKAGE) in ULevelFactory.
    bool IsEmitterClass(const std::string& type)
    {
        const Address classClass=Read<Address>(kLevelClass+0x24);
        Address cls=0,emitter=0;
        Run("finding the emitter class",[&]{
            cls=Find(classClass,type.find('.')==std::string::npos?static_cast<Address>(-1):0,type.c_str());
            emitter=Find(classClass,static_cast<Address>(-1),"Emitter");
        });
        std::set<Address> seen;
        for(Address c=cls;c && seen.insert(c).second && seen.size()<256;c=Read<Address>(c+0x28))if(c==emitter)return true;
        return false;
    }
    std::string MapPackage()
    {
        Address editor=Read<Address>(kEditor),level=editor?Read<Address>(editor+0x130):0,outer=level?Read<Address>(level+0x18):0;
        return outer?Fold(NameOf(outer)):std::string();
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
        Run("finding an emitter asset",[&]{found=Find(0,0,text);});
        return found;
    }
    // Loads a missing asset's package the way PlaceAssembly does (OBJ LOAD from Packages\<kind>).
    bool Load(const std::string& path)
    {
        if(FindPath(path))return true;
        auto package=path.substr(0,path.find('.'));
        if(package.empty() || package.find_first_of("\"\r\n/\\:")!=std::string::npos)return false;
        wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
        auto packages=std::filesystem::path(exe).parent_path().parent_path()/"Packages";
        for(const auto& [folder,extension]:std::initializer_list<std::pair<const char*,const char*>>{{"Textures",".utx"},{"StaticMeshes",".usx"},{"Sounds",".uas"},{"Animations",".ukx"}})
        {
            auto file=packages/folder/(package+extension);
            if(std::filesystem::exists(file)){Exec("OBJ LOAD FILE=\""+file.string()+"\"");if(FindPath(path))return true;}
        }
        return FindPath(path)!=0;
    }
    void Import(const std::string& t3d)
    {
        const Address level=state.level,package=state.package;const int levelName=Read<int>(level+0x20);
        const std::string text=Convert(t3d,CP_UTF8,CP_ACP);const char* begin=text.c_str();const char* end=begin+text.size();
        // The factory half of edactPasteSelected (0x10eb838a..0x10eb849d) without its
        // Remember/ReconcileActors, selection and redraw: InParent is the preview package,
        // so inline objects and actors are created there. Flags 0: not transactional.
        Address factory=0;
        Run("creating the emitter importer",[&]{
            factory=reinterpret_cast<FactoryNewFn>(0x10e05a84)(0x68,*reinterpret_cast<Address*>(kTransient),0,0);
            if(factory)reinterpret_cast<FactoryFn>(0x110559f0)(factory);
        });
        if(!factory)throw std::runtime_error("The editor could not create the emitter importer.");
        const char* cursor=begin;
        auto create=[&]{reinterpret_cast<CreateTextFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(factory)+0x60))(factory,level,kLevelClass,package,levelName,0,0,"paste",&cursor,end,*reinterpret_cast<Address*>(kWarn));};
        const DWORD failed=Native(create);
        // Deleted after a failed import too (edactPasteSelected deletes it after CreateText,
        // 0x10eb849d), unless the engine has shut down.
        auto remove=[&]{reinterpret_cast<DeleteFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(factory)+0xc))(factory,1);};
        const DWORD removed=ShutDown()?0:Native(remove);
        if(failed){Fault("importing the emitter",failed);throw std::runtime_error(state.fault);}
        if(removed){Fault("releasing the emitter importer",removed);throw std::runtime_error(state.fault);}
    }
    // Where the entry item puts its actor, relative to the entry's pivot.
    Vector Position(const Json& item)
    {
        if(!item.contains("position"))return {};
        try{return item.at("position").get<Vector>();}
        catch(const std::exception&){throw std::runtime_error("An entry actor has an invalid position.");}
    }
}

void Initialize()
{
    constexpr unsigned char prologue[]={0x55,0x8b,0xec,0x6a,0xff},wire[]={0x8b,0x41,0x30,0x85,0xc0},info[]={0x8b,0x75,0xd0,0x33,0xc9};
    if(memcmp(reinterpret_cast<const void*>(kEditorTick),prologue,sizeof(prologue))!=0 || memcmp(reinterpret_cast<const void*>(kIsWire),wire,sizeof(wire))!=0
        || memcmp(reinterpret_cast<const void*>(kSelectionInfo),info,sizeof(info))!=0)
    {
        Logger::log("Emitter preview: unsupported UEditorEngine::Tick or UViewport::IsWire; live preview disabled");state.faulted=true;state.fault="This editor build does not support the live emitter preview.";return;
    }
    if(!MemoryWriter::WriteJump(kIsWire,IsWireHook) || !MemoryWriter::WriteJump(kSelectionInfo,SelectionInfoHook) || !MemoryWriter::WriteJump(kEditorTick,EditorTickHook)){state.faulted=true;state.fault="The live emitter preview could not be installed.";}
}
bool Attach(HWND host,std::string& error)
{
    try
    {
        if(!host || !IsWindow(host))throw std::runtime_error("The preview panel is not available.");
        EnsureLevel();ViewportLost();
        HWND window=Window();
        if(state.viewport && state.host==host && window && IsWindow(window) && GetAncestor(window,GA_PARENT)==host && ViewportAlive()){Resize();return true;}
        // A viewport never moves between windows: one shown elsewhere is deleted and host
        // gets its own.
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
    try{Reframe(false);}catch(const std::exception&){}
    state.idle=1;
}
void Detach()
{
    // Called from the host's WM_DESTROY, while the viewport window still exists: the
    // viewport is deleted through the engine and a later Attach opens a new one.
    try{ViewportLost();DeleteViewport();}
    catch(const std::exception& e){Logger::log(std::string("Emitter preview: ")+e.what());state.viewport=0;wireExempt=0;}
    state.host=nullptr;state.drag=0;
}
void Show(const Json& entry,bool keepCamera)
{
    if(!entry.is_object() || !entry.contains("actors") || !entry.at("actors").is_array() || entry.at("actors").empty())throw std::runtime_error("This entry has no actors to preview.");
    // The view of the version shown, restored after the edited one has warmed up.
    struct View{Vector target,goal,homeTarget;double distance,goalDistance,homeDistance,extent[2];int pitch,yaw,homePitch,homeYaw;bool user,measured;};
    const bool keep=keepCamera && !state.actors.empty();
    const View kept{state.target,state.goal,state.homeTarget,state.distance,state.goalDistance,state.homeDistance,{state.extent[0],state.extent[1]},state.pitch,state.yaw,state.homePitch,state.homeYaw,state.user,state.measured};
    if(Read<Address>(kUndo))throw std::runtime_error("Finish the current editor operation before previewing an emitter.");
    EnsureLevel();
    const auto prefix="PV"+std::to_string(++state.serial)+"_";
    std::set<std::string> roots{"mylevel","assembly"};if(auto map=MapPackage();!map.empty())roots.insert(map);
    std::vector<Model::Actor> prepared;std::vector<Model::Placed> framing;std::vector<std::string> warnings;int index=0,skipped=0;
    for(const auto& item:entry.at("actors"))
    {
        if(!item.is_object() || !item.contains("text") || !item.at("text").is_string())throw std::runtime_error("An entry actor has no T3D text.");
        Rotation rotation{};if(item.contains("rotation"))rotation=item.at("rotation").get<Rotation>();
        const Vector position=Position(item);
        auto actor=Model::Prepare(item.at("text").get<std::string>(),prefix,index,kPackage,roots,rotation,position);
        if(!IsEmitterClass(actor.type)){++skipped;continue;}
        ++index;framing.push_back({actor.text,rotation,position});
        for(const auto& d:actor.dropped)warnings.push_back("Not shown in the preview (belongs to a map): "+d);
        prepared.push_back(std::move(actor));
    }
    if(prepared.empty())throw std::runtime_error("This entry has no emitters to preview.");
    if(skipped)warnings.push_back(std::to_string(skipped)+" non-emitter actor(s) are not shown in the preview.");
    std::vector<std::string> missing;std::set<std::string> checked;
    for(const auto& actor:prepared)for(const auto& asset:actor.assets)if(checked.insert(Fold(asset)).second && !Load(asset))missing.push_back(asset);
    if(!missing.empty())
        throw std::runtime_error("The emitter preview needs "+missing[0]+(missing.size()>1?" and "+std::to_string(missing.size()-1)+" more asset(s)":std::string())+". Load the package that holds it, then select the entry again.");
    Clear();
    CheckEngine();
    std::string t3d="Begin Map\n";for(const auto& actor:prepared)t3d+=actor.text;t3d+="End Map\n";
    Import(t3d);
    auto actors=LevelActors();std::vector<Address> made;std::string lost;
    for(const auto& actor:prepared)
    {
        auto found=std::find_if(actors.begin(),actors.end(),[&](Address a){return a && Alive(a) && NameOf(a)==actor.name;});
        if(found==actors.end()){if(lost.empty())lost=actor.name;}else made.push_back(*found);
    }
    if(!lost.empty())
    {
        Destroy(made);
        throw std::runtime_error("The editor did not import every emitter of this entry. Check the entry's T3D text.");
    }
    for(size_t i=0;i<made.size();++i)
    {
        Address a=made[i];const auto& actor=prepared[i];
        // Not selected (+0x2f4 bit 0x40, SelectActor 0x10eb9a64), no editor sprite
        // (Texture +0x228, AEmitter::RenderEditorInfo 0x110a411b), not transactional.
        Write(a+0x2f4,Read<unsigned>(a+0x2f4)&~0x40u);Write<Address>(a+0x228,0);Mark(a,kTransientFlag,kTransactional);
        // SecondsBeforeInactive 0 keeps every sub-emitter updating while unseen, as AEmitter::Tick
        // skips the InactiveTime test for it (0x110dbb6f..0x110dbb80): the warm-up and Step run
        // without drawing, and rain's 0.01 s would freeze it after one tick.
        for(auto e:Emitters(a)){Mark(e,kTransientFlag,kTransactional);Write(e+0x300,0.0f);}
        state.actors.push_back({a,actor.name});
    }
    state.warnings=warnings;
    // Starting view: the entry's preview hint gives the orientation and, for an effect with
    // nothing to measure yet, a frame; otherwise the estimate from the emitters' ranges.
    state.age=0;state.samples.clear();state.sampledAt=state.fittedAt=-1e9;state.shrinkSince=-1;state.measured=false;state.user=false;
    const auto estimate=Model::Estimate(framing);
    state.homePitch=kPitch;state.homeYaw=kYaw;state.homeTarget=estimate.target;state.extent[0]=state.extent[1]=0;
    state.homeDistance=Model::FitDistance(estimate.box,estimate.target,kPitch,kYaw,kFov,Aspect(),kFill);
    if(auto hint=entry.value("preview",Json::object());hint.is_object())
    {
        try
        {
            state.homePitch=std::clamp(hint.value("pitch",kPitch),-16000,16000);state.homeYaw=hint.value("yaw",kYaw)&0xffff;
            if(hint.contains("target"))
            {
                state.homeTarget=hint.at("target").get<Vector>();
                state.homeDistance=hint.contains("distance")?hint.at("distance").get<double>():Model::Distance(std::max(hint.value("radius",128.0),8.0),kFov,Aspect());
            }
            else state.homeDistance=Model::FitDistance(estimate.box,estimate.target,state.homePitch,state.homeYaw,kFov,Aspect(),kFill);
        }
        catch(const std::exception&){Logger::log("Emitter preview: the entry's preview hint is malformed and was ignored");}
        state.homeDistance=std::clamp(state.homeDistance,8.0,20000.0);
    }
    state.pitch=state.homePitch;state.yaw=state.homeYaw;state.target=state.goal=state.homeTarget;state.distance=state.goalDistance=state.homeDistance;
    // Run the effect before its first frame, so that frame is framed on where its particles
    // actually are rather than on the estimate or hint.
    for(double left=kWarm;left>1e-6 && !state.faulted;left-=1.0/30)Advance(static_cast<float>(std::min(left,1.0/30)),false);
    // One-shot bursts then start again for the first frame, framed on where that run took
    // them; continuous emitters stay warmed up.
    for(auto& p:state.actors)if(!state.faulted && Alive(p.actor)){bool dead=true;p.dead=0;Restart(Bursts(p.actor,dead));}
    if(state.faulted)throw std::runtime_error(state.fault);
    if(keep)
    {
        state.target=kept.target;state.goal=kept.goal;state.homeTarget=kept.homeTarget;state.distance=kept.distance;state.goalDistance=kept.goalDistance;state.homeDistance=kept.homeDistance;
        state.extent[0]=kept.extent[0];state.extent[1]=kept.extent[1];state.pitch=kept.pitch;state.yaw=kept.yaw;state.homePitch=kept.homePitch;state.homeYaw=kept.homeYaw;
        state.user=kept.user;state.measured=kept.measured;state.shrinkSince=-1;
    }
    else Reframe(true);
    if(state.viewport && ViewportAlive())ApplyCamera();
    state.idle=1;
}
std::string SelectedTexture()
{
    CheckEngine();
    // GEditor->CurrentMaterial (+0x138), the texture browser's selection (BspTextureClipboard.cpp).
    const Address material=Read<Address>(Editor()+0x138);
    if(!material || !Alive(material))throw std::runtime_error("Select a texture in the texture browser first.");
    bool texture=false;std::string type=NameOf(Read<Address>(material+0x24));std::set<Address> seen;
    for(Address c=Read<Address>(material+0x24);c && seen.insert(c).second && seen.size()<64;c=Read<Address>(c+0x28))if(NameOf(c)=="Texture"){texture=true;break;}
    if(!texture)throw std::runtime_error("The texture browser's selection ("+type+") is not a texture. Particle systems draw textures only.");
    std::string path;
    for(Address o=material;o && path.size()<512;o=Read<Address>(o+0x18))path=NameOf(o)+(path.empty()?"":"."+path);
    if(Fold(path).rfind(MapPackage()+".",0)==0 || Fold(path).rfind("mylevel.",0)==0)throw std::runtime_error("The selected texture is stored inside the map. Choose one from a shared package.");
    return type+"'"+path+"'";
}
void Clear()
{
    try
    {
        if(!state.level || !Alive(state.level,kLevelClass) || ShutDown()){state.actors.clear();return;}
        std::vector<Address> actors;for(auto a:LiveActors())actors.push_back(a);
        Destroy(actors);
    }
    catch(const std::exception& e){Logger::log(std::string("Emitter preview: ")+e.what());}
    state.actors.clear();state.warnings.clear();state.samples.clear();state.idle=1;
}
bool Active(){return !state.faulted && state.host && state.viewport && !state.actors.empty();}
void Step(double seconds)
{
    CheckEngine();
    if(!state.viewport || !ViewportAlive())throw std::runtime_error("Open the emitter preview first.");
    if(Read<Address>(kUndo))throw std::runtime_error("Finish the current editor operation before stepping the preview.");
    for(double left=std::clamp(seconds,0.0,30.0);left>1e-6 && !state.faulted;left-=1.0/30)Advance(static_cast<float>(std::min(left,1.0/30)),false);
    if(state.faulted)throw std::runtime_error(state.fault);
}
Json Capture(const std::string& bmpPath)
{
    CheckEngine();
    ViewportLost();
    if(!state.viewport || !ViewportAlive())throw std::runtime_error("Open the emitter preview first.");
    const int width=Read<int>(state.viewport+0xa0),height=Read<int>(state.viewport+0xa4);
    ApplyCamera();
    // Blit=0: UUnrealEdEngine::Draw renders without Present, so the frame is still in the back buffer.
    if(DWORD code=Draw(0)){Fault("drawing the preview",code);throw std::runtime_error(state.fault);}
    std::vector<uint32_t> pixels;std::string error;
    const bool read=Rendering::ReadBackBuffer(width,height,pixels,error);
    Draw(1);
    if(!read)throw std::runtime_error(error);
    auto stats=Model::ImageStats(pixels,width,height,kBackground);
    stats["particles"]=ParticleCount();
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
    int critical=0;Copy(&critical,reinterpret_cast<void*>(kCriticalError),4);
    Json out={{"faulted",state.faulted},{"fault",state.fault},{"level",Alive(state.level,kLevelClass)},{"package",Alive(state.package,kPackageClass)},{"viewport",state.viewport!=0 && ViewportAlive()},
        {"attached",state.host!=nullptr},{"active",Active()},{"serial",state.serial},{"ticks",state.ticks},{"frames",state.frames},{"frameRate",state.frameRate},{"loops",state.loops},{"warnings",state.warnings},
        {"viewportsOpened",state.viewportsOpened},{"viewportsClosed",state.viewportsClosed},{"clientViewports",ClientViewports().size()},{"criticalError",critical!=0},{"engineShutDown",ShutDown()}};
    try
    {
        // UTransBuffer (GEditor+0x148): UndoBuffer {+0x28 data,+0x2c num}, UndoCount +0x34.
        if(Address editor=Read<Address>(kEditor))
        {
            if(Address trans=Read<Address>(editor+0x148))out["undo"]={{"transactions",Read<int>(trans+0x2c)},{"undoCount",Read<int>(trans+0x34)}};
            out["editorMode"]=Read<int>(editor+0x1ac);out["unrealEdIsEditor"]=Read<Address>(kUnrealEd)==editor;
        }
        if(Alive(state.level,kLevelClass))
        {
            out["levelPath"]=NameOf(state.package)+"."+NameOf(state.level);
            out["levelFlags"]=Read<unsigned>(state.level+0x1c);out["levelActors"]=LevelActors().size();
            // Actors still alive in the preview level by class: the camera leaves with its viewport.
            std::map<std::string,int> classes;
            for(auto a:LevelActors())if(a && Alive(a) && !(Read<unsigned>(a+0x2e8)&0x8000))++classes[NameOf(Read<Address>(a+0x24))];
            out["liveActorClasses"]=classes;
            Json actors=Json::array();int particles=0;
            for(auto a:LiveActors())
            {
                Json subs=Json::array();
                for(auto e:Emitters(a))
                {
                    Json sample=Json::array();
                    int live=EachParticle(e,[&](Address p){if(sample.size()<3)sample.push_back({{"location",Read<std::array<float,3>>(p)},{"size",Read<std::array<float,3>>(p+108)},{"color",Read<unsigned>(p+168)},{"time",Read<float>(p+172)},{"lifetime",Read<float>(p+176)}});});particles+=live;
                    subs.push_back({{"name",NameOf(e)},{"outer",NameOf(Read<Address>(e+0x18))},{"particles",live},{"flags",Read<unsigned>(e+0x1e0)},{"state",Read<unsigned>(e+0x1e4)},{"objectFlags",Read<unsigned>(e+0x1c)},{"texture",Read<Address>(e+0x2e0)?NameOf(Read<Address>(e+0x2e0)):std::string("None")},{"sample",sample}});
                }
                auto loop=std::find_if(state.actors.begin(),state.actors.end(),[&](const Shown& p){return p.actor==a;});
                actors.push_back({{"name",NameOf(a)},{"class",NameOf(Read<Address>(a+0x24))},{"outer",NameOf(Read<Address>(a+0x18))},{"location",Read<std::array<float,3>>(a+0x80)},{"objectFlags",Read<unsigned>(a+0x1c)},{"selected",(Read<unsigned>(a+0x2f4)&0x40)!=0},{"resets",loop==state.actors.end()?0:loop->resets},{"emitters",subs}});
            }
            out["actors"]=actors;out["particles"]=particles;
        }
        if(state.viewport && ViewportAlive())
        {
            HWND window=Window();Address camera=CameraActor();
            out["hwnd"]=reinterpret_cast<uintptr_t>(window);out["host"]=reinterpret_cast<uintptr_t>(state.host);
            // OpenWindow restyles child viewports to WS_POPUP|WS_VISIBLE (0x10f7de15), so IsChild
            // is false although the window is parented, clipped and moved by the host.
            out["parent"]=reinterpret_cast<uintptr_t>(GetAncestor(window,GA_PARENT));out["parentIsHost"]=state.host && GetAncestor(window,GA_PARENT)==state.host;
            out["parentWindow"]=Read<Address>(state.viewport+0x1bc)==reinterpret_cast<Address>(state.host);   // OpenWindow's ParentWindow (0x10f7ddb5)
            out["style"]=static_cast<unsigned>(GetWindowLongA(window,GWL_STYLE));out["visible"]=IsWindowVisible(window)!=0;
            out["renDev"]=Read<Address>(state.viewport+0x70)!=0;out["sizeX"]=Read<int>(state.viewport+0xa0);out["sizeY"]=Read<int>(state.viewport+0xa4);
            out["cameraInPreviewLevel"]=Read<Address>(camera+0x1a4)==state.level;out["showFlags"]=Read<unsigned>(camera+0x4f0);out["rendMap"]=Read<int>(camera+0x4fc);
            out["camera"]=Camera();
            RECT a{},b{};GetWindowRect(window,&a);if(state.host)GetWindowRect(state.host,&b);
            out["windowRect"]={a.left,a.top,a.right,a.bottom};out["hostRect"]={b.left,b.top,b.right,b.bottom};out["fillsHost"]=state.host && EqualRect(&a,&b);
        }
    }
    catch(const std::exception& e){out["error"]=e.what();}
    return out;
}
Json Camera()
{
    return {{"target",state.target},{"distance",state.distance},{"pitch",state.pitch},{"yaw",state.yaw},{"user",state.user},{"radius",std::hypot(state.extent[0],state.extent[1])},{"extent",state.extent},{"measured",state.measured},
        {"goal",state.goal},{"goalDistance",state.goalDistance},{"home",{{"target",state.homeTarget},{"distance",state.homeDistance},{"pitch",state.homePitch},{"yaw",state.homeYaw}}}};
}
void SetCamera(const Json& camera)
{
    if(camera.contains("target"))state.target=state.goal=camera.at("target").get<Vector>();
    if(camera.contains("distance"))state.distance=state.goalDistance=std::clamp(camera.at("distance").get<double>(),8.0,20000.0);
    state.pitch=std::clamp(camera.value("pitch",state.pitch),-16000,16000);state.yaw=camera.value("yaw",state.yaw)&0xffff;state.user=true;
    if(state.viewport && ViewportAlive())ApplyCamera();
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
        // A second left click within the double-click time and distance resets the camera
        // (the viewport window class may not have CS_DBLCLKS).
        const DWORD time=GetMessageTime();
        if(message==WM_LBUTTONDOWN && state.click && time-state.click<=GetDoubleClickTime()
            && std::abs(at.x-state.clickAt.x)<=GetSystemMetrics(SM_CXDOUBLECLK) && std::abs(at.y-state.clickAt.y)<=GetSystemMetrics(SM_CYDOUBLECLK)){state.click=0;Home();return true;}
        if(message==WM_LBUTTONDOWN){state.click=time;state.clickAt=at;}
        state.drag=message==WM_LBUTTONDOWN?1:2;state.last=at;if(window)SetCapture(window);
        return true;
    }
    case WM_LBUTTONDBLCLK:state.click=0;Home();return true;
    case WM_RBUTTONDBLCLK:case WM_MBUTTONDBLCLK:return true;
    case WM_MOUSEMOVE:
        if(state.drag && window && GetCapture()==window)
        {
            const int dx=at.x-state.last.x,dy=at.y-state.last.y;state.last=at;
            if(dx || dy)
            {
                state.user=true;
                if(state.drag==1){state.yaw=(state.yaw+dx*80)&0xffff;state.pitch=std::clamp(state.pitch-dy*80,-16000,16000);}
                else state.distance=state.goalDistance=std::clamp(state.distance*std::exp(dy*0.01),8.0,20000.0);
            }
        }
        return true;
    case WM_LBUTTONUP:case WM_RBUTTONUP:case WM_MBUTTONUP:
        state.drag=0;if(window && GetCapture()==window)ReleaseCapture();
        return true;
    case WM_MOUSEWHEEL:
        state.user=true;state.distance=state.goalDistance=std::clamp(state.distance*std::pow(0.85,GET_WHEEL_DELTA_WPARAM(wParam)/120.0),8.0,20000.0);
        return true;
    case WM_CAPTURECHANGED:state.drag=0;return true;
    // The native handler answers MA_ACTIVATE (0x10f7ed0f) and OpenWindow's WS_POPUP restyle
    // (0x10f7de15) makes the window activatable, so a click would take activation and the
    // keyboard from the host. The hook (GridSizeShortcut.cpp) answers MA_NOACTIVATE instead.
    case WM_MOUSEACTIVATE:return true;
    // The native focus handlers make this the current viewport (Client vtable +0x84 at
    // 0x10f7e831) and re-acquire input; editor commands must keep acting on the map views.
    case WM_SETFOCUS:case WM_KILLFOCUS:return true;
    }
    return message>=WM_KEYFIRST && message<=WM_KEYLAST;
}
HWND OpenTestWindow(std::string& error)
{
    ViewportLost();
    // One test window at a time, never attached a second time: preview.close deletes its
    // viewport and the next preview.open opens a new window and viewport.
    if(state.test && IsWindow(state.test))return state.test;
    if(state.viewport && state.host && IsWindow(state.host)){error="The emitter preview is already shown in another window. Close that window first.";return nullptr;}
    static bool registered=false;
    if(!registered)
    {
        WNDCLASSA wc{};wc.hInstance=GetModuleHandle(nullptr);wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        wc.lpfnWndProc=[](HWND w,UINT m,WPARAM wp,LPARAM lp)->LRESULT{
            if(m==WM_SIZE){if(HWND host=GetDlgItem(w,100)){MoveWindow(host,0,0,LOWORD(lp),HIWORD(lp),TRUE);Resize();}return 0;}
            if(m==WM_DESTROY){if(state.host && state.host==GetDlgItem(w,100))Detach();if(state.test==w){state.test=nullptr;state.testHost=nullptr;}return 0;}
            return DefWindowProcA(w,m,wp,lp);
        };
        wc.lpszClassName="ReloadedEmitterPreviewTest";RegisterClassA(&wc);
        WNDCLASSA host{};host.hInstance=GetModuleHandle(nullptr);host.lpfnWndProc=DefWindowProcA;host.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));host.lpszClassName="ReloadedEmitterPreviewHost";RegisterClassA(&host);
        registered=true;
    }
    state.test=CreateWindowExA(0,"ReloadedEmitterPreviewTest","Emitter Preview Test",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,496,398,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    if(!state.test){error="The preview test window could not be created.";return nullptr;}
    RECT r{};GetClientRect(state.test,&r);
    state.testHost=CreateWindowExA(0,"ReloadedEmitterPreviewHost","",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,r.right,r.bottom,state.test,reinterpret_cast<HMENU>(100),GetModuleHandle(nullptr),nullptr);
    if(!state.testHost || !Attach(state.testHost,error)){DestroyWindow(state.test);state.test=nullptr;state.testHost=nullptr;if(error.empty())error="The preview panel could not be created.";return nullptr;}
    return state.test;
}
void CloseTestWindow()
{
    if(state.test && IsWindow(state.test))DestroyWindow(state.test);
    state.test=nullptr;state.testHost=nullptr;
}
}
