#include "../Reloaded.Editor/EmitterPreviewModel.h"
#include <cmath>
#include <iostream>
using namespace Workflow;
using namespace Workflow::EmitterPreviewModel;
static int checks=0;
constexpr int kFitPitch=-2730;
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
        Check(Estimate({}).radius==128 && Estimate({}).box.valid,"empty entries get a default frame");
        // Entry actors keep their own position: in the preview text and in the estimate.
        auto placed=Prepare(kBurst,"PV9_",1,"Pkg",{"mylevel"},{0,0,0},{7.906982421875,-18.8984375,50});
        Check(Property(placed.text,"Location")=="(X=7.906982,Y=-18.898438,Z=50.000000)","entry actor position written as its Location");
        Reject([]{Prepare(kBurst,"P",0,"Pkg",{},{},{0,0,std::nan("")});},"non-finite position rejected");
        auto raised=Estimate({{drift,Rotation{},Vector{0,0,500}}});
        Check(std::abs(raised.target[2]-frame.target[2]-500)<1e-6 && std::abs(raised.box.max[2]-frame.box.max[2]-500)<1e-6,"estimate follows the actor position");

        // Particle fit on the view axes: a 21x11x41 grid of points looked at along +X.
        std::vector<Sample> cloud;
        for(int x=-10;x<=10;++x)for(int y=-5;y<=5;++y)for(int z=0;z<=40;++z)cloud.push_back({{static_cast<double>(x),static_cast<double>(y),static_cast<double>(z)},1});
        auto grid=Fit(cloud,0,0,90,1,1,0,0);
        Check(grid.valid && std::abs(grid.target[1])<1e-9 && std::abs(grid.target[2]-20)<1e-9 && std::abs(grid.target[0])<1e-9,"target is the middle of the particles on the view axes");
        Check(grid.distance>20 && grid.distance<32 && std::abs(grid.height-18.5)<0.6,"distance fits 96% of the particles, each with its size, into the view");
        auto strays=cloud;strays.push_back({{9000,-9000,9000},1});strays.push_back({{0,4000,0},1});
        for(int i=0;i<200;++i)strays.push_back({{0,0,5000.0+i},1,0.05});
        auto kept=Fit(strays,0,0,90,1,1,0,0);
        Check(std::abs(kept.distance-grid.distance)<2 && std::abs(kept.target[2]-grid.target[2])<2,"strays and faded particles leave the frame alone");
        std::vector<Sample> faded;for(int i=0;i<5;++i)faded.push_back({{0,0,100.0*i},1,0});
        Check(Fit(faded,0,0,90,1,1,0,0).valid && !Fit({{{0,0,0},1}},0,0,90,1,1,0,0).valid,"fully faded particles still frame; fewer than three do not");
        Check(Fit(cloud,0,0,90,2,1,0,0).distance>grid.distance*1.5 && Fit(cloud,0,0,90,1,0.5,0,0).distance>grid.distance*1.5,"narrow views and a smaller fill pull back");
        auto side=Fit(cloud,-16384,0,90,1,1,0,0);
        Check(std::abs(side.target[2]-20)<1 && side.height<grid.height,"seen from above the tall cloud is framed by its footprint");
        std::vector<Sample> snow;
        for(int i=0;i<600;++i)snow.push_back({{std::fmod(i*37.0,2000)-1000,std::fmod(i*91.0,2000)-1000,std::fmod(i*53.0,1200)},2.5});
        const double far=Fit(snow,kFitPitch,8192,75,4.0/3,0.9,0,0).distance,near=Fit(snow,kFitPitch,8192,75,4.0/3,0.9,480,6).distance;
        Check(far>1500 && std::abs(near-2.5*480/(std::tan(75*3.14159265358979323846/360)*6))<1e-6,"tiny particles in a large volume are framed close enough to be seen");

        // Fit on the view axes: a 100-unit cube straight ahead at 90 degrees fills the view
        // from 100 units (its near face spans the frustum); narrow views and a smaller
        // fill need more distance, and a wide flat sheet seen from above is framed by its width.
        Box cube;Include(cube,{-50,-50,-50});Include(cube,{50,50,50});
        Check(std::abs(FitDistance(cube,{0,0,0},0,0,90,1,1)-100)<1e-6,"cube fills a square 90 degree view from 100 units");
        Check(std::abs(FitDistance(cube,{0,0,0},0,0,90,2,1)-150)<1e-6,"a wide view is limited by its vertical angle");
        Check(std::abs(FitDistance(cube,{0,0,0},0,0,90,1,0.5)-150)<1e-6,"a smaller fill pulls back");
        Box sheet;Include(sheet,{-400,-400,-5});Include(sheet,{400,400,5});
        const double above=FitDistance(sheet,{0,0,0},-16384,0,90,1,1);
        Check(std::abs(above-405)<1e-6 && above<Distance(std::sqrt(800.0*800+800*800+10*10)*0.5,90,1)*0.5,"a flat sheet seen from above is framed by its silhouette, not by a bounding sphere");
        Box dot;Include(dot,{-1,-1,-1});Include(dot,{1,1,1});
        Box huge;Include(huge,{-1e5,-1e5,-1e5});Include(huge,{1e5,1e5,1e5});
        Check(FitDistance(dot,{0,0,0},kFitPitch,8192,75,4.0/3,0.85)==16 && FitDistance(huge,{0,0,0},kFitPitch,8192,75,4.0/3,0.85)==20000,"distance kept between 16 and 20000 units");
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
