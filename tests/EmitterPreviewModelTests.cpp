#include "../Reloaded.Editor/EmitterPreviewModel.h"
#include <iostream>
using namespace Workflow;
using namespace Workflow::EmitterPreviewModel;
static int checks=0;
void Check(bool condition,const char* what) { ++checks; if(!condition) throw std::runtime_error(what); }
template<class F> void Reject(F f,const char* what) { bool failed=false;try{f();}catch(const std::exception&){failed=true;}Check(failed,what); }
// OffsD Emitter530 as the native exporter writes it: a triggered one-shot burst.
const char* kBurst=
    "Begin Actor Class=Emitter Name=Emitter530\n"
    "    Begin Object Class=SpriteEmitter Name=SpriteEmitter531\n"
    "        Acceleration=(Z=150.000000)\n"
    "        UseRotationFrom=PTRS_Actor\n"
    "        DrawStyle=PTDS_AlphaBlend\n"
    "        StartLocationRange=(Y=(Min=-150.000000,Max=150.000000))\n"
    "        RespawnDeadParticles=False\n"
    "        Disabled=True\n"
    "        AutoDestroy=True\n"
    "        UseCollision=True\n"
    "        UseSizeScale=True\n"
    "        UseRegularSizeScale=False\n"
    "        SizeScale(0)=(RelativeTime=1.000000,RelativeSize=4.000000)\n"
    "        StartSizeRange=(X=(Min=30.000000,Max=50.000000))\n"
    "        Texture=Texture'sfx.Emitter.smoke_grenade'\n"
    "        MeshSpawningStaticMesh=StaticMesh'MyLevel.LocalMesh'\n"
    "        Sounds(0)=(Sound=Sound'Fx.Steam.Hiss',Radius=(Min=64,Max=64))\n"
    "        LifetimeRange=(Min=3.000000,Max=3.000000)\n"
    "        StartVelocityRange=(X=(Min=400.000000,Max=600.000000),Y=(Min=-100.000000,Max=100.000000))\n"
    "        VelocityLossRange=(X=(Min=1.000000,Max=2.000000),Y=(Min=1.000000,Max=2.000000),Z=(Min=1.000000,Max=2.000000))\n"
    "        Name=\"SpriteEmitter531\"\n"
    "    End Object\n"
    "    Emitters(0)=SpriteEmitter'MyLevel.SpriteEmitter531'\n"
    "    bNetworkTrigger=True\n"
    "    AmbientSound=Sound'Fx.Steam.Loop'\n"
    "    Base=StaticMeshActor'MyLevel.StaticMeshActor12'\n"
    "    Location=(X=-3080.787598,Y=299.155579,Z=817.000000)\n"
    "    Rotation=(Yaw=55372)\n"
    "    Tag=\"container_down\"\n"
    "End Actor\n";
int main()
{
    try
    {
        auto actor=Prepare(kBurst,"PV7_",0,"ReloadedEmitterPreview",{"mylevel","assembly"},{0,55372,0});
        const auto& text=actor.text;
        Check(actor.name=="PV7_0" && actor.type=="Emitter" && actor.objects==1,"actor identity and object count");
        Check(text.find("Begin Actor Class=Emitter Name=PV7_0\n")==0,"actor renamed into the preview namespace");
        Check(text.find("Begin Object Class=SpriteEmitter Name=PV7_0_0\n")!=std::string::npos,"inline object renamed");
        Check(text.find("Emitters(0)=SpriteEmitter'ReloadedEmitterPreview.PV7_0_0'")!=std::string::npos,"inline reference bound to the preview package");
        Check(text.find("MyLevel")==std::string::npos,"no reference can bind to the open map");
        Check(text.find("Name=\"SpriteEmitter531\"")!=std::string::npos,"ParticleEmitter.Name string is not an identity");
        for(const char* gone:{"Disabled=","AutoDestroy=","UseCollision=","Sounds(","AmbientSound=","Tag=","Base=","817.0"})
            Check(text.find(gone)==std::string::npos,"trigger, sound, collision and map placement state removed");
        Check(Property(text,"Location")=="(X=0.000000,Y=0.000000,Z=0.000000)","actor placed at the preview origin");
        Check(Property(text,"Rotation")=="(Pitch=0,Yaw=55372,Roll=0)","entry rotation kept for PTRS_Actor emitters");
        Check(text.find("RespawnDeadParticles=False")!=std::string::npos && text.find("bNetworkTrigger=True")!=std::string::npos,"emitter behaviour kept");
        Check(actor.assets==std::vector<std::string>{"sfx.Emitter.smoke_grenade"},"texture listed for package loading");
        Check(actor.dropped==std::vector<std::string>{"MyLevel.LocalMesh"},"map-owned asset reference dropped");
        Check(ParseActors(text).size()==1,"prepared text stays a single balanced actor");

        // Library definitions carry canonical Assembly.* paths and actor-owned sub-objects.
        std::string canonical="Begin Actor Class=SSmokeEmitter Name=Smoke\n Begin Object Class=SpriteEmitter Name=SpriteEmitter9\n  Texture=Texture'sfx.Emitter.smoke'\n End Object\n Begin Object Class=MeshEmitter Name=MeshEmitter3\n  StaticMesh=StaticMesh'Pkg.Group.Rock'\n End Object\n"
            " Emitters(0)=SpriteEmitter'Assembly.Smoke.SpriteEmitter9'\n Emitters(1)=MeshEmitter'\"OrphD.MeshEmitter3\"'\n Event=Boom\nEnd Actor\n";
        auto owned=Prepare(canonical,"PV8_",2,"Pkg0",{"mylevel","assembly","orphd"},{});
        Check(owned.text.find("Emitters(0)=SpriteEmitter'Pkg0.PV8_2_0'")!=std::string::npos,"nested canonical path rebound");
        Check(owned.text.find("Emitters(1)=MeshEmitter'Pkg0.PV8_2_1'")!=std::string::npos,"quoted map-package path rebound");
        Check(owned.assets.size()==2 && owned.dropped.empty() && owned.text.find("Event=")==std::string::npos,"assets kept, event dropped");

        Reject([]{Prepare("Begin Object Class=SpriteEmitter Name=A\nEnd Object\n","P",0,"Pkg",{},{});},"only actors are previewed");
        Reject([]{Prepare("Begin Actor Class=Emitter Name=A\n Begin Object Class=SpriteEmitter Name=B\nEnd Actor\n","P",0,"Pkg",{},{});},"unbalanced text rejected");
        Reject([]{Prepare("Begin Actor Class=Emitter Name=A\nEnd Actor\nBegin Actor Class=Emitter Name=B\nEnd Actor\n","P",0,"Pkg",{},{});},"one actor per entry item");
        Reject([]{Prepare("Begin Actor Class=Emitter\nEnd Actor\n","P",0,"Pkg",{},{});},"actor name required");

        auto value=Value("(X=(Min=-20.000000,Max=20.000000),Z=(Min=5,Max=7),Name=\"a,b)\")");
        Check(Number(Member(Member(value,"x"),"min"),0)==-20 && Number(Member(Member(value,"z"),"max"),0)==7,"nested ranges parsed");
        Check(Member(value,"name")=="\"a,b)\"","quoted scalars keep separators");
        Check(Number(Member(Member(value,"y"),"min"),3)==3,"absent fields use defaults");
        Reject([]{Value("(X=(Min=1");},"malformed structure rejected");

        auto objects=Objects(actor.text);
        Check(objects.size()==1 && objects[0].type=="spriteemitter" && objects[0].values.count("sizescale(0)"),"inline object properties collected");
        // Smoke drifting down -X and rising: start box, travel and particle size all included.
        std::string drift="Begin Actor Class=Emitter Name=E\n Begin Object Class=SpriteEmitter Name=S\n  Acceleration=(Z=20.000000)\n  StartLocationRange=(Y=(Min=-20.000000,Max=20.000000),Z=(Min=-20.000000,Max=20.000000))\n"
            "  UseSizeScale=True\n  UseRegularSizeScale=False\n  SizeScale(0)=(RelativeTime=3.000000,RelativeSize=8.000000)\n  StartSizeRange=(X=(Min=30.000000,Max=30.000000))\n  LifetimeRange=(Min=6.000000,Max=6.000000)\n  StartVelocityRange=(X=(Min=-10.000000,Max=-10.000000))\n End Object\nEnd Actor\n";
        auto box=Reach(Objects(drift)[0],{});
        Check(box.valid && box.min[0]<=-60-240 && box.max[0]>=240 && box.max[2]>=360+240 && box.min[2]<=-20-240,"reach covers start box, drift, rise and size");
        auto frame=Estimate({{drift,Rotation{}}});
        Check(frame.target[2]>100 && frame.target[0]<0 && frame.radius>300 && frame.radius<1200,"frame centred on the plume");
        auto spray=Estimate({{kBurst,Rotation{0,16384,0}}});
        Check(spray.target[1]>100 && std::abs(spray.target[0])<200,"PTRS_Actor velocity follows the actor yaw");
        Check(Estimate({}).radius==128,"empty entries get a default frame");
        Check(Distance(100,90,1)>Distance(100,90,0.5)*0.99 && Distance(100,90,4.0/3)>115 && Distance(200,90,1)>Distance(100,90,1),"distance grows with radius and narrow views");
        auto eye=Eye({10,20,30},100,0,0);Check(std::abs(eye[0]+90)<1e-9 && std::abs(eye[1]-20)<1e-9 && std::abs(eye[2]-30)<1e-9,"yaw 0 looks along +X");
        auto down=Eye({0,0,0},100,-16384,0);Check(std::abs(down[2]-100)<1e-6 && std::abs(down[0])<1e-6,"pitch -90 degrees looks straight down");

        std::vector<uint32_t> pixels(4*2,0xff181818);pixels[3]=0xffffffff;pixels[5]=0xff804020;pixels[6]=0xff1a1a1c;pixels[7]=0xff0a0a0a;
        auto stats=ImageStats(pixels,4,2,0xff181818);
        Check(stats.at("nonBlackPixels")==3 && stats.at("width")==4 && stats.at("meanLuma").get<double>()>40,"frame statistics count pixels that differ from the background, darker ones too");
        pixels[5]=0xff804021;Check(ImageStats(pixels,4,2,0xff181818).at("checksum")!=stats.at("checksum"),"checksum follows pixel changes");
        Reject([&]{ImageStats(pixels,3,2,0);},"frame size mismatch rejected");
        std::cout<<"PASS "<<checks<<" emitter preview model checks\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
