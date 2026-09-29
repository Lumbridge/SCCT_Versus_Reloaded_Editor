#include "pch.h"
#undef min
#undef max
#include "EmitterPreview.h"
#include "EmitterPreviewModel.h"
#include "MemoryWriter.h"
#include "Rendering.h"
#include "logger.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
    const char* const kPackage="ReloadedEmitterPreview";
    const char* const kLevelName="ReloadedEmitterPreviewLevel";
    const char* const kViewportName="ReloadedEmitterPreview";

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
    using OpenWindowFn=void(__thiscall*)(Address viewport,HWND parent,int temporary,int width,int height,int x,int y);         // UWindowsViewport::OpenWindow, vtable +0xb0
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
    // Native sequences run under SEH: an access violation, or an engine error thrown
    // through its guard/unguard chain, becomes a message instead of a crash. The
    // callables hold only native calls and plain values.
    template<class F> DWORD Native(F& f)
    {
        __try { f(); return 0; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
    }
    template<class F> void Run(const char* what,F f)
    {
        if(DWORD code=Native(f))
        {
            char text[200];sprintf_s(text,"%s failed inside the editor (exception 0x%08lX).",what,code);
            Logger::log(std::string("Emitter preview: ")+text);throw std::runtime_error(text);
        }
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

    struct Shown { Address actor;std::string name;float dead=0;int resets=0; };
    struct State
    {
        Address package=0,level=0,viewport=0;
        HWND host=nullptr,park=nullptr,test=nullptr,testHost=nullptr;
        std::vector<Shown> actors;std::vector<std::string> warnings;
        int serial=0;bool faulted=false;std::string fault;
        // Camera: orbit round target at distance; radius is the automatic frame.
        Vector target{},goal{};double distance=256,goalDistance=256,radius=128;int pitch=kPitch,yaw=kYaw;
        bool user=false,hinted=false;Vector autoTarget{};double autoRadius=128;
        double age=0;int refits=0;bool fitted=false;Model::Box seen;
        // Input
        int drag=0;POINT last{};DWORD click=0;POINT clickAt{};
        // Timing
        LARGE_INTEGER frequency{},previous{},second{};unsigned long long ticks=0,frames=0,loops=0,framesAtSecond=0;double frameRate=0;float idle=0;
    } state;
    Address wireExempt=0;   // the preview viewport, read by IsWireHook

    Address CameraActor(){return Read<Address>(state.viewport+0x30);}
    HWND Window()
    {
        if(!state.viewport)return nullptr;
        Address window=0;if(!Copy(&window,reinterpret_cast<void*>(state.viewport+0x1b4),4) || !window)return nullptr;
        HWND hwnd=nullptr;return Copy(&hwnd,reinterpret_cast<void*>(window+4),4)?hwnd:nullptr;
    }
    // UWindowsViewport::ViewportWndProc checks the same Client->Viewports membership (0x10f7e391).
    bool ViewportAlive()
    {
        if(!Alive(state.viewport,kWindowsViewportClass) || !Alive(state.level,kLevelClass))return false;
        Address editor=0,client=0,data=0,camera=0,level=0;int count=0;
        if(!Copy(&editor,reinterpret_cast<void*>(kEditor),4) || !editor || !Copy(&client,reinterpret_cast<void*>(editor+0x30),4) || !client)return false;
        if(!Copy(&data,reinterpret_cast<void*>(client+0x2c),4) || !Copy(&count,reinterpret_cast<void*>(client+0x30),4) || count<0 || count>256)return false;
        bool member=false;
        for(int i=0;i<count && !member;++i){Address v=0;member=Copy(&v,reinterpret_cast<void*>(data+i*4),4) && v==state.viewport;}
        return member && Copy(&camera,reinterpret_cast<void*>(state.viewport+0x30),4) && Alive(camera)
            && Copy(&level,reinterpret_cast<void*>(camera+0x1a4),4) && level==state.level;
    }
    // A host destroyed without Detach takes the viewport window with it, and
    // UWindowsClient::Tick then deletes the viewport (0x10f7b325) and its camera. Forget it
    // so the next Attach opens a new one.
    bool ViewportLost()
    {
        if(!state.viewport || Alive(state.viewport,kWindowsViewportClass))return false;
        Logger::log("Emitter preview: the viewport closed with its host window; the next Attach opens a new one");
        state.viewport=0;wireExempt=0;state.host=nullptr;state.drag=0;return true;
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
    // camera is in the map (0x1111ee39), so map changes never reach it.
    void EnsureLevel()
    {
        if(state.faulted)throw std::runtime_error(state.fault.empty()?"The emitter preview stopped after an editor error. Restart the editor to use it again.":state.fault);
        if(Alive(state.level,kLevelClass) && Alive(state.package,kPackageClass))return;
        if(state.viewport)
        {
            state.faulted=true;state.fault="The editor released the preview level. Restart the editor to use the emitter preview again.";
            Logger::log("Emitter preview: "+state.fault);throw std::runtime_error(state.fault);
        }
        Editor();
        Address package=0,level=0;
        Run("Creating the preview level",[&]{
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
    void ApplyCamera()
    {
        Address camera=CameraActor();
        auto eye=Model::Eye(state.target,state.distance,state.pitch,state.yaw);
        Write(camera+0x80,std::array<float,3>{static_cast<float>(eye[0]),static_cast<float>(eye[1]),static_cast<float>(eye[2])});
        Write(camera+0xe0,std::array<int,3>{state.pitch,state.yaw,0});
        Write(camera+0x308,kFov);
    }
    double Aspect(){int w=Read<int>(state.viewport+0xa0),h=Read<int>(state.viewport+0xa4);return w>0 && h>0?static_cast<double>(w)/h:4.0/3;}
    void Frame(const Vector& target,double radius,bool jump)
    {
        state.goal=target;state.radius=radius;state.goalDistance=Model::Distance(radius,kFov,state.viewport?Aspect():4.0/3);
        if(jump){state.target=state.goal;state.distance=state.goalDistance;}
    }
    HWND Park()
    {
        if(state.park && IsWindow(state.park))return state.park;
        WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName="ReloadedEmitterPreviewPark";RegisterClassA(&wc);
        // Hidden for the whole session: the viewport window waits here between hosts,
        // so UWindowsClient::Tick never deletes the viewport for a destroyed HWND (0x10f7b325).
        state.park=CreateWindowExA(WS_EX_TOOLWINDOW,"ReloadedEmitterPreviewPark","",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
        return state.park;
    }
    void EnsureViewport(HWND host)
    {
        RECT r{};GetClientRect(host,&r);const int w=std::max<int>(r.right,16),h=std::max<int>(r.bottom,16);
        ViewportLost();
        if(state.viewport)
        {
            if(!ViewportAlive())
            {
                state.faulted=true;state.fault="The editor closed the preview viewport. Restart the editor to use the emitter preview again.";
                Logger::log("Emitter preview: "+state.fault);throw std::runtime_error(state.fault);
            }
            HWND window=Window();if(!window)throw std::runtime_error("The preview viewport has no window.");
            SetParent(window,host);SetWindowPos(window,HWND_TOP,0,0,w,h,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            return;
        }
        Address viewport=0;const Address level=state.level;
        Run("Opening the preview viewport",[&]{
            Address editor=*reinterpret_cast<Address*>(kEditor),client=*reinterpret_cast<Address*>(editor+0x30);
            // Same sequence as WBrowserStaticMesh::OnCreate (0x10e8516d..0x10e852e6), with the
            // camera spawned in the preview level instead of GEditor->Level.
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
        });
        if(!viewport)throw std::runtime_error("The editor could not open the preview viewport.");
        state.viewport=viewport;wireExempt=viewport;
        Mark(CameraActor(),kTransientFlag,kTransactional);
        if(!Read<Address>(viewport+0x70))Logger::log("Emitter preview: viewport opened without a render device");
        ApplyCamera();
        Logger::log("Emitter preview: viewport opened");
    }
    DWORD Draw(int blit)
    {
        // The perspective clear colour is the editor's own setting (0x10ecc0c6/0x10ecc0db):
        // swap in the preview background for this synchronous draw only.
        Address editor=*reinterpret_cast<Address*>(kEditor);uint32_t saved[2]{};
        if(!editor || !Copy(saved,reinterpret_cast<void*>(editor+0xfc),4) || !Copy(saved+1,reinterpret_cast<void*>(editor+0x118),4))return 1;
        Copy(reinterpret_cast<void*>(editor+0xfc),&kBackground,4);Copy(reinterpret_cast<void*>(editor+0x118),&kBackground,4);
        // The UseSizingBox overlay (GUnrealEd+0x21c bit 2, 0x10eccef7) prints the map's
        // selected actor into every viewport; it has no place in the preview.
        Address unrealEd=0;unsigned char sizing=0;
        const bool box=Copy(&unrealEd,reinterpret_cast<void*>(kUnrealEd),4) && unrealEd && Copy(&sizing,reinterpret_cast<void*>(unrealEd+0x21c),1);
        if(box){const unsigned char off=sizing&~2;Copy(reinterpret_cast<void*>(unrealEd+0x21c),&off,1);}
        const Address viewport=state.viewport;
        auto paint=[&]{reinterpret_cast<RepaintFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(viewport)+0xc8))(viewport,blit);};
        DWORD code=Native(paint);
        if(box)Copy(reinterpret_cast<void*>(unrealEd+0x21c),&sizing,1);
        Copy(reinterpret_cast<void*>(editor+0xfc),saved,4);Copy(reinterpret_cast<void*>(editor+0x118),saved+1,4);
        return code;
    }
    void Fault(const char* what,DWORD code)
    {
        char text[200];sprintf_s(text,"The emitter preview stopped: %s failed inside the editor (exception 0x%08lX).",what,code);
        state.faulted=true;state.fault=text;Logger::log(std::string("Emitter preview: ")+text);
        if(HWND window=Window())ShowWindow(window,SW_HIDE);
    }
    // One-shot sub-emitters (RespawnDeadParticles off, +0x1e0 bit 0x100) and trigger
    // spawners (SpawnOnTriggerRange +0x320/+0x324) end with AllParticlesDead (+0x1e4 bit
    // 0x10). Once every such sub-emitter of an actor is dead for 0.6 s they are Reset
    // together, as AEmitter::Tick's AutoReset does (0x110dbc82), and trigger spawners are
    // re-armed the way execTrigger does (CurrentSpawnOnTrigger +0x3e4, 0x110eb378).
    void Loop(float delta)
    {
        for(auto& p:state.actors)
        {
            if(!Alive(p.actor) || (Read<unsigned>(p.actor+0x2e8)&0x8000))continue;
            std::vector<Address> bursts;bool dead=true;
            for(auto e:Emitters(p.actor))
            {
                const bool trigger=std::max(Read<float>(e+0x320),Read<float>(e+0x324))>=1;
                if((Read<unsigned>(e+0x1e0)&0x100) && !trigger)continue;
                bursts.push_back(e);
                if(!(Read<unsigned>(e+0x1e4)&0x10) && !(Read<unsigned>(e+0x1e0)&0x800))dead=false;
            }
            if(bursts.empty() || !dead){p.dead=0;continue;}
            if((p.dead+=delta)<0.6f)continue;
            p.dead=0;++p.resets;++state.loops;
            for(auto e:bursts)
            {
                Write(e+0x1e0,Read<unsigned>(e+0x1e0)&~0x800u);
                auto reset=[&]{reinterpret_cast<ResetFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(e)+0x68))(e);};
                if(DWORD code=Native(reset)){Fault("resetting a burst",code);return;}
                const float lo=Read<float>(e+0x320),hi=Read<float>(e+0x324);
                if(std::max(lo,hi)>=1)
                {
                    Write(e+0x3e4,std::max(1,static_cast<int>(std::lround((lo+hi)*0.5))));
                    Write(e+0x1e4,Read<unsigned>(e+0x1e4)&~0x18u);
                }
            }
        }
    }
    // Frames what the particles actually reach: the union of every live particle seen
    // since Show (location +- size), fitted at 1, 2.5, 5 and 9 s. After the first fit the
    // camera only pulls back, so growing plumes and looping bursts stay in view. Skipped
    // once the user moves the camera or when the entry carries its own camera.
    void Refit(float delta)
    {
        state.age+=delta;
        if(!state.user && !state.hinted && state.refits<4)
        {
            for(auto a:LiveActors())for(auto e:Emitters(a))EachParticle(e,[&](Address p){
                auto at=Read<std::array<float,3>>(p);auto size=std::abs(Read<float>(p+108));
                if(!std::isfinite(at[0]) || !std::isfinite(at[1]) || !std::isfinite(at[2]) || !std::isfinite(size))return;
                size=std::min(size,2048.0f);
                Model::Include(state.seen,{at[0]-size,at[1]-size,at[2]-size});Model::Include(state.seen,{at[0]+size,at[1]+size,at[2]+size});
            });
            constexpr double stages[]={1.0,2.5,5.0,9.0};
            if(state.seen.valid && state.age>=stages[state.refits])
            {
                ++state.refits;
                Vector center{};double squared=0;
                for(int i=0;i<3;++i){center[i]=(state.seen.min[i]+state.seen.max[i])*0.5;squared+=(state.seen.max[i]-state.seen.min[i])*(state.seen.max[i]-state.seen.min[i]);}
                const double radius=std::clamp(std::sqrt(squared)*0.5,24.0,4096.0);
                double moved=0;for(int i=0;i<3;++i)moved+=(center[i]-state.autoTarget[i])*(center[i]-state.autoTarget[i]);
                if(!state.fitted || radius>state.autoRadius*1.1 || std::sqrt(moved)>state.autoRadius*0.25)
                {
                    state.autoTarget=center;state.autoRadius=state.fitted?std::max(radius,state.autoRadius):radius;state.fitted=true;
                    Frame(state.autoTarget,state.autoRadius,false);
                }
            }
        }
        const double k=std::min(1.0,delta*4.0);
        for(int i=0;i<3;++i)state.target[i]+=(state.goal[i]-state.target[i])*k;
        state.distance+=(state.goalDistance-state.distance)*k;
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
            if(DWORD code=Native(destroy)){Fault("removing a preview emitter",code);return;}
        }
    }
    // Emitter class test on the T3D Class= token (short or Package.Class), resolved
    // like ParseObject<UClass>(...,ANY_PACKAGE) in ULevelFactory.
    bool IsEmitterClass(const std::string& type)
    {
        const Address classClass=Read<Address>(kLevelClass+0x24);
        Address cls=0,emitter=0;
        Run("Finding the emitter class",[&]{
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
        Run("Loading a package",[&]{result=reinterpret_cast<ExecFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(exec)))(exec,text,*reinterpret_cast<Address*>(kLog));});
        return result;
    }
    Address FindPath(const std::string& path)
    {
        Address found=0;const char* text=path.c_str();
        Run("Finding an emitter asset",[&]{found=Find(0,0,text);});
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
        Address factory=0;
        // The factory half of edactPasteSelected (0x10eb838a..0x10eb849d) without its
        // Remember/ReconcileActors, selection and redraw: InParent is the preview package,
        // so inline objects and actors are created there. Flags 0: not transactional.
        Run("Importing the emitter",[&]{
            factory=reinterpret_cast<FactoryNewFn>(0x10e05a84)(0x68,*reinterpret_cast<Address*>(kTransient),0,0);
            if(!factory)return;
            reinterpret_cast<FactoryFn>(0x110559f0)(factory);
            const char* cursor=begin;
            reinterpret_cast<CreateTextFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(factory)+0x60))(factory,level,kLevelClass,package,levelName,0,0,"paste",&cursor,end,*reinterpret_cast<Address*>(kWarn));
            reinterpret_cast<DeleteFn>(*reinterpret_cast<Address*>(*reinterpret_cast<Address*>(factory)+0xc))(factory,1);
        });
        if(!factory)throw std::runtime_error("The editor could not create the emitter importer.");
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
        EnsureLevel();EnsureViewport(host);state.host=host;
        Resize();
        return true;
    }
    catch(const std::exception& e){error=e.what();return false;}
}
void Resize()
{
    if(!state.host || !state.viewport || state.faulted)return;
    HWND window=Window();if(!window)return;
    RECT r{};GetClientRect(state.host,&r);
    SetWindowPos(window,HWND_TOP,0,0,std::max<int>(r.right,16),std::max<int>(r.bottom,16),SWP_NOACTIVATE|SWP_SHOWWINDOW);
    try{if(!state.user && !state.hinted)Frame(state.goal,state.radius,false);}catch(const std::exception&){}
    state.idle=1;
}
void Detach()
{
    if(HWND window=Window())
    {
        if(GetCapture()==window)ReleaseCapture();
        ShowWindow(window,SW_HIDE);SetParent(window,Park());
    }
    state.host=nullptr;state.drag=0;
}
void Show(const Json& entry)
{
    if(!entry.is_object() || !entry.contains("actors") || !entry.at("actors").is_array() || entry.at("actors").empty())throw std::runtime_error("This entry has no actors to preview.");
    if(Read<Address>(kUndo))throw std::runtime_error("Finish the current editor operation before previewing an emitter.");
    EnsureLevel();
    const auto prefix="PV"+std::to_string(++state.serial)+"_";
    std::set<std::string> roots{"mylevel","assembly"};if(auto map=MapPackage();!map.empty())roots.insert(map);
    std::vector<Model::Actor> prepared;std::vector<std::pair<std::string,Rotation>> framing;std::vector<std::string> warnings;int index=0,skipped=0;
    for(const auto& item:entry.at("actors"))
    {
        if(!item.is_object() || !item.contains("text") || !item.at("text").is_string())throw std::runtime_error("An entry actor has no T3D text.");
        Rotation rotation{};if(item.contains("rotation"))rotation=item.at("rotation").get<Rotation>();
        auto actor=Model::Prepare(item.at("text").get<std::string>(),prefix,index,kPackage,roots,rotation);
        if(!IsEmitterClass(actor.type)){++skipped;continue;}
        ++index;framing.push_back({actor.text,rotation});
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
        for(auto e:Emitters(a))Mark(e,kTransientFlag,kTransactional);
        state.actors.push_back({a,actor.name});
    }
    state.warnings=warnings;state.age=0;state.refits=0;state.fitted=false;state.seen=Model::Box();state.user=false;state.pitch=kPitch;state.yaw=kYaw;
    auto hint=entry.value("preview",Json::object());
    state.hinted=hint.is_object() && hint.contains("target");
    if(state.hinted)
    {
        auto target=hint.at("target").get<Vector>();double radius=hint.value("radius",0.0);
        state.pitch=hint.value("pitch",kPitch);state.yaw=hint.value("yaw",kYaw);
        state.autoTarget=target;state.autoRadius=radius>0?radius:128;Frame(target,state.autoRadius,true);
        if(hint.contains("distance")){state.distance=state.goalDistance=std::clamp(hint.at("distance").get<double>(),8.0,20000.0);}
    }
    else
    {
        auto frame=Model::Estimate(framing);
        state.autoTarget=frame.target;state.autoRadius=frame.radius;Frame(frame.target,frame.radius,true);
    }
    if(state.viewport && ViewportAlive())ApplyCamera();
}
void Clear()
{
    if(!state.level || !Alive(state.level,kLevelClass)){state.actors.clear();return;}
    std::vector<Address> actors;for(auto a:LiveActors())actors.push_back(a);
    Destroy(actors);state.actors.clear();state.warnings.clear();state.idle=1;
}
bool Active(){return !state.faulted && state.host && state.viewport && !state.actors.empty();}
void Step(double seconds)
{
    if(state.faulted)throw std::runtime_error(state.fault);
    if(!state.viewport || !ViewportAlive())throw std::runtime_error("Open the emitter preview first.");
    if(Read<Address>(kUndo))throw std::runtime_error("Finish the current editor operation before stepping the preview.");
    for(double left=std::clamp(seconds,0.0,30.0);left>1e-6 && !state.faulted;left-=1.0/30)Advance(static_cast<float>(std::min(left,1.0/30)),false);
    if(state.faulted)throw std::runtime_error(state.fault);
}
Json Capture(const std::string& bmpPath)
{
    if(state.faulted)throw std::runtime_error(state.fault);
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
    Json out={{"faulted",state.faulted},{"fault",state.fault},{"level",Alive(state.level,kLevelClass)},{"package",Alive(state.package,kPackageClass)},{"viewport",state.viewport!=0 && ViewportAlive()},
        {"attached",state.host!=nullptr},{"active",Active()},{"serial",state.serial},{"ticks",state.ticks},{"frames",state.frames},{"frameRate",state.frameRate},{"loops",state.loops},{"warnings",state.warnings}};
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
                actors.push_back({{"name",NameOf(a)},{"class",NameOf(Read<Address>(a+0x24))},{"outer",NameOf(Read<Address>(a+0x18))},{"objectFlags",Read<unsigned>(a+0x1c)},{"selected",(Read<unsigned>(a+0x2f4)&0x40)!=0},{"resets",loop==state.actors.end()?0:loop->resets},{"emitters",subs}});
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
    return {{"target",state.target},{"distance",state.distance},{"pitch",state.pitch},{"yaw",state.yaw},{"user",state.user},{"radius",state.radius}};
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
    auto reset=[&]{state.user=false;state.pitch=kPitch;state.yaw=kYaw;try{Frame(state.autoTarget,state.autoRadius,false);}catch(const std::exception&){}};
    switch(message)
    {
    case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:case WM_MBUTTONDOWN:
    {
        // A second left click within the double-click time and distance resets the camera
        // (the viewport window class may not have CS_DBLCLKS).
        const DWORD time=GetMessageTime();
        if(message==WM_LBUTTONDOWN && state.click && time-state.click<=GetDoubleClickTime()
            && std::abs(at.x-state.clickAt.x)<=GetSystemMetrics(SM_CXDOUBLECLK) && std::abs(at.y-state.clickAt.y)<=GetSystemMetrics(SM_CYDOUBLECLK)){state.click=0;reset();return true;}
        if(message==WM_LBUTTONDOWN){state.click=time;state.clickAt=at;}
        state.drag=message==WM_LBUTTONDOWN?1:2;state.last=at;if(window)SetCapture(window);
        return true;
    }
    case WM_LBUTTONDBLCLK:state.click=0;reset();return true;
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
    // The native focus handlers make this the current viewport (Client vtable +0x84 at
    // 0x10f7e831) and re-acquire input; editor commands must keep acting on the map views.
    case WM_SETFOCUS:case WM_KILLFOCUS:return true;
    }
    return message>=WM_KEYFIRST && message<=WM_KEYLAST;
}
HWND OpenTestWindow(std::string& error)
{
    ViewportLost();
    if(state.test && IsWindow(state.test)){if(!state.host && !Attach(state.testHost,error))return nullptr;return state.test;}
    static bool registered=false;
    if(!registered)
    {
        WNDCLASSA wc{};wc.hInstance=GetModuleHandle(nullptr);wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        wc.lpfnWndProc=[](HWND w,UINT m,WPARAM wp,LPARAM lp)->LRESULT{
            if(m==WM_SIZE){if(HWND host=GetDlgItem(w,100)){MoveWindow(host,0,0,LOWORD(lp),HIWORD(lp),TRUE);Resize();}return 0;}
            if(m==WM_DESTROY){if(state.host==GetDlgItem(w,100))Detach();if(state.test==w){state.test=nullptr;state.testHost=nullptr;}return 0;}
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
