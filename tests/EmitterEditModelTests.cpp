#include "../Reloaded.Editor/EmitterEditModel.h"
#include "../Reloaded.Editor/EmitterLibraryDefaults.gen.h"
#include <iostream>
#include <source_location>
using namespace Workflow;
namespace Edit=Workflow::EmitterEdit;
namespace Library=Workflow::EmitterLibrary;
static int checks=0;
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F> std::string Reject(F f,std::source_location where=std::source_location::current())
{
    ++checks;
    try{f();}catch(const std::exception& e){return e.what();}
    throw std::runtime_error("invalid input accepted at line "+std::to_string(where.line()));
}
bool Has(const std::string& text,const std::string& part){return text.find(part)!=std::string::npos;}
size_t Count(const std::string& text,const std::string& part){size_t n=0;for(size_t at=text.find(part);at!=std::string::npos;at=text.find(part,at+1))++n;return n;}
// The burning barrel's first two systems as the built-ins store them (CRLF), with a
// nested block and an actor property that must never be edited.
const std::string Barrel=
    "Begin Actor Class=Emitter Name=Emitter6332\r\n"
    "    Begin Object Class=SpriteEmitter Name=SpriteEmitter6333\r\n"
    "        UseRotationFrom=PTRS_Actor\r\n"
    "        ColorScale(1)=(RelativeTime=0.200000,Color=(B=200,G=200,R=200,A=200))\r\n"
    "        ColorScale(2)=(RelativeTime=0.300000,Color=(G=150,R=200,A=200))\r\n"
    "        ColorScale(4)=(RelativeTime=1.000000)\r\n"
    "        MaxParticles=20\r\n"
    "        UseColorScale=True\r\n"
    "        UniformSize=True\r\n"
    "        StartSizeRange=(X=(Min=5.000000,Max=20.000000))\r\n"
    "        Texture=Texture'SQU13_txt.stm_na.flammes'\r\n"
    "        LifetimeRange=(Min=0.300000,Max=0.800000)\r\n"
    "        StartVelocityRange=(Z=(Min=30.000000,Max=60.000000))\r\n"
    "        Name=\"SpriteEmitter6333\"\r\n"
    "    End Object\r\n"
    "    Emitters(0)=SpriteEmitter'\"Assembly.SpriteEmitter6333\"'\r\n"
    "    Begin Object Class=SpriteEmitter Name=SpriteEmitter6334\r\n"
    "        MaxParticles=4\r\n"
    "        StartSizeRange=(X=(Max=140.000000),Y=(Min=2.000000,Max=3.000000))\r\n"
    "        Begin Object Class=Nested Name=Inner\r\n"
    "            MaxParticles=99\r\n"
    "        End Object\r\n"
    "        Texture=Texture'sfx.Flare.flare_fixe'\r\n"
    "        LifetimeRange=(Min=3.000000)\r\n"
    "        Name=\"SpriteEmitter6334\"\r\n"
    "    End Object\r\n"
    "    Emitters(1)=SpriteEmitter'\"Assembly.SpriteEmitter6334\"'\r\n"
    "    MaxParticles=7\r\n"
    "    Location=(X=0,Y=0,Z=50)\r\n"
    "End Actor\r\n";
Json Entry()
{
    return {{"id","builtin.fire_barrel"},{"name","Burning barrel"},{"category","Fire"},{"description",""},{"builtin",true},{"readonly",true},
            {"pivot",Vector{0,0,0}},{"bindings",Json::array()},{"dependencies",Json::array({"Engine.Emitter","sfx.Flare.flare_fixe","SQU13_txt.stm_na.flammes"})},
            {"actors",Json::array({{{"name","Emitter6332"},{"class","Engine.Emitter"},{"path","Assembly.Emitter6332"},{"text",Barrel},{"position",Vector{0,0,50}},{"rotation",Rotation{0,0,0}},{"tag","None"},{"event","None"}}})}};
}
std::string TextOf(const Json& entry,size_t actor=0){return entry.at("actors")[actor].at("text").get<std::string>();}
// What remains when one line is taken out: the edit touched nothing else.
std::string Without(std::string text,const std::string& line){auto at=text.find(line);if(at!=std::string::npos)text.erase(at,line.size());return text;}
int main()
{
    try
    {
        // Property text primitives.
        Check(Edit::SystemCount(Barrel)==2,"two particle systems; the nested block is not one");
        Check(Edit::GetValue(Barrel,0,"MaxParticles")==std::optional<std::string>("20") && Edit::GetValue(Barrel,1,"maxparticles")==std::optional<std::string>("4"),"values are read from their own system, keys ignoring case");
        Check(!Edit::GetValue(Barrel,1,"UseColorScale") && Edit::GetValue(Barrel,0,"ColorScale(2)")==std::optional<std::string>("(RelativeTime=0.300000,Color=(G=150,R=200,A=200))"),"indexed keys are exact; a missing line reads as nothing");
        Check(Edit::GetValue(Barrel,1,"MaxParticles")!=std::optional<std::string>("99"),"a nested block's line is not the system's");
        Reject([]{Edit::GetValue(Barrel,2,"MaxParticles");});
        auto replaced=Edit::SetValue(Barrel,1,"MaxParticles","12");
        Check(Without(replaced,"        MaxParticles=12\r\n")==Without(Barrel,"        MaxParticles=4\r\n") && Has(replaced,"            MaxParticles=99\r\n") && Has(replaced,"    MaxParticles=7\r\n"),"replacing a value keeps every other byte, the nested block's and the actor's line too");
        auto inserted=Edit::SetValue(Barrel,1,"ParticlesPerSecond","5.000000");
        Check(Has(inserted,"        LifetimeRange=(Min=3.000000)\r\n        ParticlesPerSecond=5.000000\r\n        Name=\"SpriteEmitter6334\"\r\n") && Without(inserted,"        ParticlesPerSecond=5.000000\r\n")==Barrel,"a new line goes before Name= with the system's indentation and CRLF");
        auto keyed=Edit::SetValue(Barrel,0,"ColorScale(3)","(RelativeTime=0.9)");
        Check(Has(keyed,"ColorScale(4)=(RelativeTime=1.000000)\r\n        ColorScale(3)=(RelativeTime=0.9)\r\n        MaxParticles=20"),"a new array element goes after the property's last element");
        const std::string lf="Begin Actor Class=Emitter Name=E\n    Begin Object Class=SpriteEmitter Name=S\n    End Object\nEnd Actor\n";
        Check(Edit::SetValue(lf,0,"MaxParticles","3")=="Begin Actor Class=Emitter Name=E\n    Begin Object Class=SpriteEmitter Name=S\n        MaxParticles=3\n    End Object\nEnd Actor\n","an empty system takes LF text's line breaks and a deeper indentation");
        Check(Edit::RemoveValue(Barrel,0,"UniformSize")==Without(Barrel,"        UniformSize=True\r\n") && Edit::RemoveValue(Barrel,1,"UniformSize")==Barrel,"removing drops exactly one line, and nothing when it is absent");
        Reject([]{Edit::SetValue(Barrel,0,"Texture","None\r\nMaxParticles=1");});
        Check(Edit::Detail::Members("(X=(Min=1,Max=2),Y=\"a,b\",Z=3)").size()==3 && Edit::Detail::WithMember("(X=(Min=1),Z=3)","x","(Min=4,Max=5)")=="(X=(Min=4,Max=5),Z=3)" && Edit::Detail::WithMember("(X=1)","Y","2")=="(X=1,Y=2)","struct members are replaced in place or appended");
        Check(Edit::ParseNumber(" 2.5 ","size")==2.5 && Edit::Shown(0.30000001)=="0.3" && Edit::Shown(20)=="20" && Edit::Shown(-0.00001)=="0","numbers parse and show plainly");
        Check(Has(Reject([]{Edit::ParseNumber("2,5","the size");}),"Enter a number for the size") && !Reject([]{Edit::ParseNumber("","x");}).empty(),"text that is not a number is refused");

        // Settings: stored values, and class defaults for what the export left out.
        auto entry=Entry();auto settings=Edit::Settings(entry);
        Check(settings.size()==2 && settings[0].at("label")=="1. flammes" && settings[1].at("label")=="2. flare_fixe" && settings[1].at("name")=="SpriteEmitter6334","one row per system, named by its texture");
        Check(settings[0].at("maxParticles")==20 && settings[0].at("lifetime")==Json({0.3,0.8}) && settings[0].at("size")==Json({5.0,20.0}) && settings[0].at("height").is_null(),"stored values read back; uniform size has no height");
        Check(settings[0].at("velocity")==Json({{0.0,0.0},{0.0,0.0},{30.0,60.0}}) && settings[0].at("acceleration")==Json({0.0,0.0,0.0}) && settings[0].at("particlesPerSecond")==0.0 && settings[0].at("initialParticlesPerSecond").is_null(),"missing ranges read as zero, spawning as automatic");
        Check(settings[1].at("lifetime")==Json({3.0,4.0}) && settings[1].at("size")==Json({100.0,140.0}) && settings[1].at("height")==Json({2.0,3.0}),"a member the export left out takes the class default");
        Check(settings[0].at("colours")==Json({{0,0,0,0},{200,200,200,200},{200,150,0,200},{0,0,0,0},{0,0,0,0}}) && settings[0].at("useColorScale")==true && settings[1].at("colours").empty(),"colour keys up to the last one, missing ones black");
        Check(settings[0].at("tint")==Json({255,255,255}) && settings[0].at("texture")=="SQU13_txt.stm_na.flammes","an untinted system reads white");

        // Edits: only the named line changes.
        auto more=Edit::Edit(entry,0,"maxParticles",60);
        Check(TextOf(more)==Edit::SetValue(Barrel,0,"MaxParticles","60") && Edit::Settings(more)[0].at("maxParticles")==60,"the particle count replaces its line");
        Check(Has(Reject([&]{Edit::Edit(entry,0,"maxParticles",0);}),"from 1 to 2000") && !Reject([&]{Edit::Edit(entry,0,"maxParticles",2001);}).empty() && !Reject([&]{Edit::Edit(entry,0,"maxParticles",2.5);}).empty() && !Reject([&]{Edit::Edit(entry,0,"maxParticles","9");}).empty(),"counts are whole, positive and capped");
        auto rate=Edit::Edit(entry,0,"particlesPerSecond",40);
        Check(Has(TextOf(rate),"        ParticlesPerSecond=40.000000\r\n") && Edit::Edit(rate,0,"particlesPerSecond",0).at("actors")==entry.at("actors"),"a rate is written and 0 removes it again");
        Check(!Reject([&]{Edit::Edit(entry,0,"particlesPerSecond",-1);}).empty() && !Reject([&]{Edit::Edit(entry,0,"particlesPerSecond",10001);}).empty(),"rates are non-negative and capped");
        auto initial=Edit::Edit(entry,1,"initialParticlesPerSecond",25);
        Check(Has(TextOf(initial),"AutomaticInitialSpawning=False\r\n") && Has(TextOf(initial),"InitialParticlesPerSecond=25.000000\r\n") && Edit::Settings(initial)[1].at("initialParticlesPerSecond")==25.0,"a starting rate turns automatic spawning off");
        Check(Edit::Edit(initial,1,"initialParticlesPerSecond",nullptr).at("actors")==entry.at("actors"),"clearing the starting rate restores automatic spawning");
        auto life=Edit::Edit(entry,1,"lifetime",Json({1,2.5}));
        Check(Has(TextOf(life),"        LifetimeRange=(Min=1.000000,Max=2.500000)\r\n") && Without(TextOf(life),"        LifetimeRange=(Min=1.000000,Max=2.500000)\r\n")==Without(Barrel,"        LifetimeRange=(Min=3.000000)\r\n"),"a lifetime writes both ends in place");
        Check(Has(Reject([&]{Edit::Edit(entry,1,"lifetime",Json({3,1}));}),"no larger than the second") && Has(Reject([&]{Edit::Edit(entry,1,"lifetime",Json({0,1}));}),"between 0.01 and 600") && !Reject([&]{Edit::Edit(entry,1,"lifetime",Json({1,601}));}).empty() && !Reject([&]{Edit::Edit(entry,1,"lifetime",2);}).empty(),"lifetimes are ordered, positive and capped");
        auto size=Edit::Edit(entry,1,"size",Json({10,30}));
        Check(Has(TextOf(size),"StartSizeRange=(X=(Min=10.000000,Max=30.000000),Y=(Min=2.000000,Max=3.000000))\r\n") && Edit::Settings(size)[1].at("height")==Json({2.0,3.0}),"size changes X and keeps Y as it was");
        auto height=Edit::Edit(entry,1,"height",Json({4,8}));
        Check(Has(TextOf(height),"StartSizeRange=(X=(Max=140.000000),Y=(Min=4.000000,Max=8.000000))\r\n"),"height changes Y only");
        Check(Has(Reject([&]{Edit::Edit(entry,0,"height",Json({4,8}));}),"uniform size") && !Reject([&]{Edit::Edit(entry,0,"size",Json({-1,8}));}).empty() && !Reject([&]{Edit::Edit(entry,0,"size",Json({1,10001}));}).empty(),"sizes are non-negative, capped, and a uniform system has no height");
        auto velocity=Edit::Edit(entry,0,"velocityX",Json({-20,20}));
        Check(Has(TextOf(velocity),"StartVelocityRange=(Z=(Min=30.000000,Max=60.000000),X=(Min=-20.000000,Max=20.000000))\r\n"),"a velocity axis is added beside the others");
        Check(Has(TextOf(Edit::Edit(entry,1,"velocityZ",Json({5,10}))),"        StartVelocityRange=(Z=(Min=5.000000,Max=10.000000))\r\n        Name=\"SpriteEmitter6334\""),"a missing velocity range is added");
        Check(!Reject([&]{Edit::Edit(entry,0,"velocityY",Json({5,-5}));}).empty() && !Reject([&]{Edit::Edit(entry,0,"velocityZ",Json({0,60000}));}).empty(),"velocities are ordered and capped");
        auto accel=Edit::Edit(entry,0,"acceleration",Json({0,-5,30}));
        Check(Has(TextOf(accel),"        Acceleration=(Y=-5.000000,Z=30.000000)\r\n") && Edit::Edit(accel,0,"acceleration",Json({0,0,0})).at("actors")==entry.at("actors"),"acceleration writes its non-zero axes and none removes it");
        Check(!Reject([&]{Edit::Edit(entry,0,"acceleration",Json({0,0}));}).empty() && !Reject([&]{Edit::Edit(entry,0,"acceleration",Json({0,0,1e9}));}).empty(),"acceleration has three capped axes");

        // Colours.
        auto tint=Edit::Edit(entry,0,"tint",Json({255,128,0}));
        Check(Has(TextOf(tint),"ColorMultiplierRange=(X=(Min=1.000000,Max=1.000000),Y=(Min=0.501961,Max=0.501961),Z=(Min=0.000000,Max=0.000000))\r\n") && Edit::Settings(tint)[0].at("tint")==Json({255,128,0}),"a tint is a colour multiplier per channel");
        Check(Edit::Edit(tint,0,"tint",Json({255,255,255})).at("actors")==entry.at("actors"),"white removes the tint");
        Check(!Reject([&]{Edit::Edit(entry,0,"tint",Json({256,0,0}));}).empty() && !Reject([&]{Edit::Edit(entry,0,"tint",Json({1.5,0,0}));}).empty(),"colour channels are whole numbers up to 255");
        auto blue=Edit::Edit(entry,0,"colour",Json{{"index",2},{"rgb",{0,64,255}}});
        Check(Has(TextOf(blue),"        ColorScale(2)=(RelativeTime=0.300000,Color=(B=255,G=64,A=200))\r\n") && Edit::Settings(blue)[0].at("colours")[2]==Json({0,64,255,200}),"a colour key keeps its time and alpha and is written B, G, R, A without zeros");
        auto first=Edit::Edit(entry,0,"colour",Json{{"index",0},{"rgb",{10,20,30}}});
        Check(Has(TextOf(first),"        ColorScale(0)=(Color=(B=30,G=20,R=10))\r\n        MaxParticles=20"),"a key the export left out is added");
        Check(Has(TextOf(Edit::Edit(entry,0,"colour",Json{{"index",4},{"rgb",{9,9,9}}})),"ColorScale(4)=(RelativeTime=1.000000,Color=(B=9,G=9,R=9))\r\n"),"a key without colour gains one");
        Check(Has(TextOf(Edit::Edit(first,0,"colour",Json{{"index",0},{"rgb",{0,0,0}}})),"        ColorScale(0)=(RelativeTime=0.000000)\r\n"),"black without alpha stays an explicit key so the array keeps its length");
        Check(!Reject([&]{Edit::Edit(entry,0,"colour",Json{{"index",-1},{"rgb",{0,0,0}}});}).empty() && !Reject([&]{Edit::Edit(entry,0,"colour",Json{{"rgb",{0,0,0}}});}).empty(),"a colour names its key");

        // Texture and dependencies.
        auto smoke=Edit::Edit(entry,1,"texture","sfx.Emitter.smoke_grenade");
        Check(Has(TextOf(smoke),"        Texture=Texture'sfx.Emitter.smoke_grenade'\r\n") && smoke.at("dependencies")==Json({"Engine.Emitter","SQU13_txt.stm_na.flammes","sfx.Emitter.smoke_grenade"}),"a new texture replaces the old one's dependency when nothing else uses it");
        auto both=Edit::Edit(entry,1,"texture","FinalBlend'SQU13_txt.stm_na.flammes'");
        Check(Has(TextOf(both),"Texture=FinalBlend'SQU13_txt.stm_na.flammes'") && both.at("dependencies")==Json({"Engine.Emitter","SQU13_txt.stm_na.flammes"}),"a class may be given and a shared texture stays listed once");
        Check(Has(Reject([&]{Edit::Edit(entry,1,"texture","MyLevel.Fx.smoke");}),"inside a map") && !Reject([&]{Edit::Edit(entry,1,"texture","smoke");}).empty() && !Reject([&]{Edit::Edit(entry,1,"texture","a.b' Name=x");}).empty(),"textures are package paths outside the map");
        Library::Validate(smoke);Library::Validate(Edit::Edit(Edit::Edit(blue,1,"lifetime",Json({1,2})),0,"acceleration",Json({0,0,-9})));
        Check(true,"edited entries stay valid library entries");

        // Edits never reach another system or field, and Changed sees them.
        Check(!Edit::Changed(entry,entry) && Edit::Changed(entry,more) && Edit::Changed(entry,smoke),"Changed compares actors and dependencies");
        Check(Has(Reject([&]{Edit::Edit(entry,2,"maxParticles",5);}),"no longer exists") && Has(Reject([&]{Edit::Edit(entry,0,"DrawStyle","PTDS_Brighten");}),"cannot change"),"unknown systems and fields are refused");

        // Saving a copy: no id or flags, the chosen details.
        auto copy=Edit::NewEffect(more,{{"name","Big barrel fire"},{"category","Fire"},{"description","edited"}});
        Check(!copy.contains("id") && !copy.contains("builtin") && !copy.contains("readonly") && copy.at("name")=="Big barrel fire" && copy.at("actors")==more.at("actors"),"a new effect is an unsaved copy of the edit");
        Json file=Library::EmptyDocument();auto saved=Library::Save(file,copy);
        Check(Library::IsUserId(saved.at("id")) && file.at("emitters").size()==1 && TextOf(saved)==TextOf(more),"saving the copy adds a user entry with the edited text");
        auto again=Edit::Edit(saved,0,"maxParticles",30);auto resaved=Library::Save(file,again);
        Check(resaved.at("id")==saved.at("id") && file.at("emitters").size()==1 && Edit::Settings(file.at("emitters")[0])[0].at("maxParticles")==30,"saving the user's own entry replaces it");
        auto user=Library::Save(file,Edit::NewEffect(resaved,{{"name","Second"}}));
        Check(user.at("id")!=saved.at("id") && file.at("emitters").size()==2,"save as new from a user entry makes another entry");

        // Every built-in reads, and an edit of each of its systems changes only that line.
        const auto builtins=Library::Builtins(Library::DefaultsText());size_t systems=0;
        for(const auto& builtin:builtins)
        {
            const auto rows=Edit::Settings(builtin);systems+=rows.size();
            for(size_t i=0;i<rows.size();++i)
            {
                const auto edited=Edit::Edit(builtin,i,"maxParticles",rows[i].at("maxParticles").get<int>()+1);
                Check(Edit::Settings(edited)[i].at("maxParticles")==rows[i].at("maxParticles").get<int>()+1,"a built-in's particle count changes");
                const auto a=rows[i].at("actor").get<size_t>();
                // The edited text is the stock text with its MaxParticles line replaced, or with one added.
                const auto before=TextOf(builtin,a),after=TextOf(edited,a);
                const auto old=Edit::GetValue(before,rows[i].at("system").get<size_t>(),"MaxParticles");
                const auto line="MaxParticles="+std::to_string(rows[i].at("maxParticles").get<int>()+1);
                auto at=after.find(line);while(at!=std::string::npos && after[at-1]!=' ')at=after.find(line,at+1);
                const auto restored=old?std::string(after).replace(at,line.size(),"MaxParticles="+*old):Without(after,"        "+line+"\r\n");
                if(restored!=before)throw std::runtime_error("editing "+builtin.at("id").get<std::string>()+" system "+std::to_string(i)+" changed more than its MaxParticles line");
                Check(true,"one line changes");
                Library::Validate(edited);
            }
        }
        Check(systems==62,"the built-ins hold 62 particle systems");
        std::cout<<"PASS "<<checks<<" emitter edit model checks\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
