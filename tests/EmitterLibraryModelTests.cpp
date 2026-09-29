#include "../Reloaded.Editor/EmitterLibraryModel.h"
#include "../Reloaded.Editor/EmitterLibraryDefaults.gen.h"
#include <functional>
#include <iostream>
#include <source_location>
using namespace Workflow;
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
// A steam vent as CaptureAssembly returns it from a stock map: the sub-emitter
// is exported inline under the map package, and the actor carries lines that
// only make sense where it was placed.
Json Capture(const std::string& extra="",const std::string& tag="Emitter")
{
    const std::string text=
        "Begin Actor Class=Emitter Name=Emitter6308\r\n"
        "    Begin Object Class=SpriteEmitter Name=SpriteEmitter6315\r\n"
        "        DrawStyle=PTDS_AlphaBlend\r\n"
        "        Texture=Texture'sfx.Emitter.smoke_grenade'\r\n"
        "        Name=\"SpriteEmitter6315\"\r\n"
        "    End Object\r\n"
        "    Emitters(0)=SpriteEmitter'MyLevel.SpriteEmitter6315'\r\n"
        "    Location=(X=0,Y=0,Z=16)\r\n"
        "    Rotation=(Pitch=16384,Yaw=7852,Roll=0)\r\n"
        "    AmbientSound=Sound'Amb_Gare.Ambiances.Mis_vapeur'\r\n"
        "    Tag=\""+tag+"\"\r\n"
        "    Platform=PLATFORM_Xbox\r\n"
        "    Group=\"None,EmitterWave\"\r\n"
        "    AttachTag=Lamp\r\n"
        "    Base=Mover'MyLevel.Mover3'\r\n"
        +extra+
        "End Actor\r\n";
    return {{"actors",Json::array({{{"name","Emitter6308"},{"class","Engine.Emitter"},{"path","MyLevel.Emitter6308"},{"text",text},{"position",Vector{0,0,16}},{"rotation",Rotation{16384,7852,0}},{"tag",tag},{"event","None"}}})},
            {"bindings",Json::array({{{"id","object:mylevel.mover3"},{"label","Emitter6308 -> Mover3"},{"kind","object"},{"path","MyLevel.Mover3"},{"type","Engine.Mover"}}})},
            {"pivot",Vector{2381,-222,0}},
            {"dependencies",Json::array({"Amb_Gare.Ambiances.Mis_vapeur","Engine.Emitter","sfx.Emitter.smoke_grenade"})}};
}
int main()
{
    try
    {
        // Categories and text.
        Check(Library::DefaultCategories().size()==11 && Library::DefaultCategories().back()=="Other","default categories end with Other");
        Json used=Json::array({{{"category","Neon"}},{{"category","fire"}},{{"category","Alarms"}}});
        auto categories=Library::Categories(used);
        Check(categories.size()==13 && categories[10]=="Alarms" && categories[11]=="Neon" && categories[12]=="Other","used categories join the defaults alphabetically before Other");
        Check(Library::CleanCategory("  sparks &   ELECTRICAL ")=="Sparks & Electrical" && Library::CleanCategory("")=="Other","typed categories take the default spelling");
        Check(Library::CleanCategory("neon",used)=="Neon" && Library::CleanCategory("Holograms",used)=="Holograms","typed categories reuse an existing spelling or start a new one");
        Reject([]{Library::CleanCategory(std::string(49,'c'));});
        Check(Library::CleanName("  Steam \t vent\r\n ")=="Steam vent","names are trimmed and collapsed");
        Check(Library::CleanName("Vapeur d'\xc3\xa9t\xc3\xa9")=="Vapeur d'\xc3\xa9t\xc3\xa9","UTF-8 names are kept");
        Reject([]{Library::CleanName("   ");});Reject([]{Library::CleanName(std::string(121,'n'));});
        Reject([]{Library::CleanName("bad\x01name");});Reject([]{Library::CleanName("ANSI \xe9t\xe9");});
        Check(Library::CleanDescription(" First line\r\n\r\n  second\tline ")=="First line\nsecond line","descriptions keep line breaks");
        Reject([]{Library::CleanDescription(std::string(2001,'d'));});
        Check(Library::Slug("Wall torch -- fire!")=="wall_torch_fire" && Library::BuiltinId("Steam vent (grate)")=="builtin.steam_vent_grate","built-in ids are slugs");
        Reject([]{Library::Slug("!!!");});
        Check(Library::IsBuiltinId("builtin.fire_torch") && !Library::IsBuiltinId("builtin.") && !Library::IsBuiltinId("fire_torch"),"built-in id form");
        Check(Library::IsUserId(Id()) && !Library::IsUserId("builtin.fire_torch") && !Library::IsUserId(std::string(32,'G')),"user id form");

        // Draft: a fresh capture becomes a placeable entry for any map.
        auto draft=Library::Draft(Capture(),"StatD");
        const auto& actor=draft["actors"][0];const std::string text=actor["text"];
        for(const char* stripped:{"Platform=","Group=","AttachTag=","Base=","Tag="})Check(!Has(text,std::string("\n    ")+stripped),"placement lines and the class-default Tag are stripped");
        Check(Has(text,"AmbientSound=Sound'Amb_Gare.Ambiances.Mis_vapeur'") && Has(text,"DrawStyle=PTDS_AlphaBlend") && Has(text,"Rotation=(Pitch=16384"),"effect settings, sound and rotation are kept");
        Check(Has(text,"Begin Object Class=SpriteEmitter Name=SpriteEmitter6315\r\n") && Has(text,"Emitters(0)=SpriteEmitter'\"Assembly.SpriteEmitter6315\"'"),"sub-emitter is canonical and referenced inside the entry");
        Check(actor["path"]=="Assembly.Emitter6308" && actor["sourcePath"]=="MyLevel.Emitter6308" && actor["tag"]=="None","actor record is canonical");
        Check(draft["bindings"].empty() && draft["dependencies"]==Json::array({"Amb_Gare.Ambiances.Mis_vapeur","Engine.Emitter","sfx.Emitter.smoke_grenade"}),"a stripped link drops its binding; assets stay dependencies");
        Check(draft["source"]=="StatD.Emitter6308" && draft["name"]=="New emitter" && draft["category"]=="Smoke" && draft["description"]=="" && !draft.contains("id"),"draft carries source and a guessed category, and no id yet");
        auto chosen=Library::Draft(Capture("","boomgaz3"),"SteeD");
        Check(chosen["actors"][0]["tag"]=="boomgaz3" && Has(chosen["actors"][0]["text"],"Tag=boomgaz3"),"a chosen Tag is kept for triggered effects");
        auto local=Capture();local["local"]=Json::array({"MyLevel.SteamTexture"});
        Check(Has(Reject([&]{Library::Draft(local,"StatD");}),"MyLevel.SteamTexture"),"map-local assets are named when rejected");
        auto linked=Capture("    Target=Light'MyLevel.Light3'\r\n");
        linked["bindings"].push_back({{"id","object:mylevel.light3"},{"label","Emitter6308 -> Light3"},{"kind","object"},{"path","MyLevel.Light3"},{"type","Engine.Light"}});
        Check(Has(Reject([&]{Library::Draft(linked,"StatD");}),"Emitter6308 -> Light3"),"links to outside actors are rejected by name");
        auto evented=Capture();evented["bindings"].push_back({{"id","event:door"},{"label","Emitter6308 Event -> Door"},{"kind","event"},{"path","Door"},{"type","Actor"}});
        Check(Has(Reject([&]{Library::Draft(evented,"StatD");}),"Event -> Door"),"events to outside actors are rejected");
        auto placement=PreparePlacement(draft,{{100,0,0},{}},"RE_0123456789ab_","MyLevel",{});
        const std::string t3d=placement["t3d"];
        Check(Has(t3d,"Begin Object Class=SpriteEmitter Name=RE_0123456789ab_SpriteEmitter6315\r\n") && Has(t3d,"Emitters(0)=SpriteEmitter'\"MyLevel.RE_0123456789ab_SpriteEmitter6315\"'"),"a drafted entry places its sub-emitter in the destination map");
        Check(Library::GuessCategory({{"dependencies",{"crypt_txt.Torch.FirePart"}}})=="Fire" && Library::GuessCategory({{"dependencies",{"Aquarium_TXT.Mer.bubulles"}}})=="Water"
            && Library::GuessCategory({{"dependencies",{"sfx.Emitter.alu"}}})=="Other" && Library::GuessCategory({{"actors",Json::array({{{"class","SBase.SSmokeEmitter"}}})},{"dependencies",{"SBase.SSmokeEmitter"}}})=="Other","categories are guessed from assets, never from the actor class");

        // Validation.
        auto valid=draft;valid["id"]=Id();valid["modified"]=Timestamp();valid["name"]="Steam vent";
        Library::Validate(valid);
        auto invalid=[&](const std::function<void(Json&)>& change){auto copy=valid;change(copy);return Reject([&]{Library::Validate(copy);});};
        Check(Has(invalid([](Json& e){e["bindings"].push_back({{"id","x"}});}),"external bindings"),"bindings are never allowed");
        Check(Has(invalid([](Json& e){e["dependencies"].push_back("MyLevel.SteamTexture");}),"stored inside a map"),"map-local dependencies are rejected");
        invalid([](Json& e){e["actors"][0]["path"]="MyLevel.Emitter6308";});
        invalid([](Json& e){e["actors"][0]["name"]="Other";});
        invalid([](Json& e){e["actors"][0]["position"]=Json::array({0,"x",0});});
        invalid([](Json& e){e["actors"][0]["text"]="Begin Actor Class=Emitter Name=Emitter6308\r\n";});
        invalid([](Json& e){e["actors"]=Json::array();});
        invalid([](Json& e){e["pivot"]=Json::array({0,0});});
        invalid([](Json& e){e.erase("id");});
        invalid([](Json& e){e["id"]="builtin.steam";e["builtin"]=false;});
        invalid([](Json& e){e["modified"]="yesterday";});
        invalid([](Json& e){e["preview"]={{"distance",-5}};});
        invalid([](Json& e){e["name"]="";});
        auto hinted=valid;hinted["preview"]={{"distance",256},{"yaw",45},{"pitch",-15},{"target",{0,0,64}}};Library::Validate(hinted);

        // Built-ins compiled into the DLL.
        auto builtinEntry=draft;builtinEntry["id"]=Library::BuiltinId("Steam vent");builtinEntry["name"]="Steam vent";builtinEntry["category"]="Steam";
        auto second=builtinEntry;second["id"]="builtin.pipe_steam";second["name"]="Pipe steam";
        auto builtins=Library::Builtins(Json({{"version",1},{"emitters",{builtinEntry,second}}}).dump());
        Check(builtins.size()==2 && builtins[0]["builtin"]==true && builtins[0]["id"]=="builtin.steam_vent","built-ins parse and are flagged in memory");
        Reject([&]{Library::Builtins(Json({{"version",1},{"emitters",{builtinEntry,builtinEntry}}}).dump());});
        Reject([&]{Library::Builtins(Json({{"version",1},{"emitters",{valid}}}).dump());});
        Reject([&]{Library::Builtins("{\"version\":2,\"emitters\":[]}");});
        // The bundled defaults (tools/emitter_library): stock emitters captured natively, each
        // placeable into any map. SCCT's T3D import drops numeric enum values, so every enum
        // must be spelled by name.
        auto embedded=Library::Builtins(Library::DefaultsText());
        Check(embedded.size()>=30,"the built-in library holds the default set");
        const std::regex numericEnum("(^|\\n)\\s*(DrawStyle|UseRotationFrom|CoordinateSystem|UseDirectionAs|GetVelocityDirectionFrom|SpawningSound|StartLocationShape|EffectAxis)=[0-9-]");
        std::set<std::string> embeddedIds,embeddedCategories;int water=0,triggers=0;
        for(const auto& entry:embedded)
        {
            const std::string id=entry["id"],name=entry["name"],category=entry["category"];
            Check(Library::IsBuiltinId(id) && entry["bindings"].empty() && embeddedIds.insert(id).second,"every embedded default is a valid built-in entry with its own id");
            Check(category!="Other" && std::count(Library::DefaultCategories().begin(),Library::DefaultCategories().end(),category)==1 && !entry["description"].get<std::string>().empty(),"every embedded default has a default category and a description");
            Check(entry.contains("preview") && entry["preview"].contains("target") && entry["preview"].contains("distance"),"every embedded default has a preview camera hint");
            embeddedCategories.insert(category);water+=category=="Water";
            const bool trigger=Has(name,"(triggered)");triggers+=trigger;
            std::set<std::string> inline_,paths;std::string all;
            for(const auto& record:entry["actors"])
            {
                const std::string body=record["text"];all+=body;
                Check(record["class"]=="Engine.Emitter","built-in entries hold emitters only");
                for(const char* key:{"Platform","Group","AttachTag","Base","ForcedVisibilityZoneTag"})Check(RemoveProperty(body,key)==body,"built-in emitters carry no placement lines");
                Check(!std::regex_search(body,numericEnum),"built-in emitters spell enums by name");
                Check(trigger==(Has(body,"Disabled=True") && record["tag"]!="None"),"only triggered entries start disabled, and they keep a Tag to be triggered by");
                for(const auto& object:InlineObjectNames(body))inline_.insert(Fold(object));
                for(const auto& ref:References(body))paths.insert(ref.path);
            }
            for(const auto& path:paths)
            {
                if(Fold(path).rfind("assembly.",0)==0)Check(inline_.count(Fold(path.substr(9))),"every sub-emitter reference resolves inside the entry");
                else Check(std::count(entry["dependencies"].begin(),entry["dependencies"].end(),Json(path))==1,"every texture and sound is a listed dependency");
            }
            for(const auto& dependency:entry["dependencies"])Check(dependency=="Engine.Emitter" || Has(all,dependency.get<std::string>()),"every dependency is used by the entry");
            auto placed=PreparePlacement(entry,{{512,0,64},{0,16384,0}},"RE_0123456789ab_","MyLevel",{});
            for(const auto& object:inline_)Check(Has(Fold(placed["t3d"].get<std::string>()),"name=re_0123456789ab_"+object) && object.size()+16<64,"sub-emitters place under the map package within the native name buffer");
            Check(!Has(placed["t3d"].get<std::string>(),"Assembly."),"a placed built-in keeps no reference to the library");
        }
        Check(embeddedIds.count("builtin.fire_with_smoke") && water>=10 && triggers>=4 && embeddedCategories.size()==Library::DefaultCategories().size()-1,"the defaults cover fire with smoke, a broad water set, triggered bursts and every default category");

        // The user file: save, update, delete, hide and restore.
        Json file=Library::EmptyDocument();
        auto saved=Library::Save(file,valid);
        Check(file["emitters"].size()==1 && saved["id"]==valid["id"] && Library::IsUserId(saved["id"]),"new entry keeps its user id");
        auto fresh=draft;fresh["name"]="  Exhaust  puffs ";fresh["category"]="smoke";
        auto added=Library::Save(file,fresh);
        Check(file["emitters"].size()==2 && Library::IsUserId(added["id"]) && added["name"]=="Exhaust puffs" && added["category"]=="Smoke","a draft is saved with a fresh id and cleaned text");
        auto renamed=saved;renamed["name"]="Steam grate";
        Library::Save(file,renamed);
        Check(file["emitters"].size()==2 && file["emitters"][0]["name"]=="Steam grate" && file["emitters"][0]["id"]==saved["id"],"saving an existing id updates in place");
        auto copied=Library::Save(file,builtins[0]);
        Check(file["emitters"].size()==3 && Library::IsUserId(copied["id"]) && !file["emitters"][2].contains("builtin"),"saving a built-in makes a user copy; the flag is never stored");
        auto updated=Library::Update(file,added["id"],{{"name","Engine exhaust"},{"category","Vehicles"},{"description","Idling engine."}});
        Check(updated["name"]=="Engine exhaust" && updated["category"]=="Vehicles" && file["emitters"][1]["description"]=="Idling engine.","name, category and description update");
        Check(Library::Categories(file["emitters"]).size()==12,"a new category appears in the category list");
        Check(Has(Reject([&]{Library::Update(file,"builtin.steam_vent",{{"name","Mine"}});}),"Built-in"),"built-ins cannot be renamed");
        Reject([&]{Library::Update(file,added["id"],{{"actors",Json::array()}});});
        Reject([&]{Library::Update(file,Id(),{{"name","Gone"}});});
        Library::Delete(file,builtins,"builtin.steam_vent");Library::Delete(file,builtins,"builtin.steam_vent");
        Check(file["hiddenBuiltins"]==Json::array({"builtin.steam_vent"}),"deleting a built-in hides it once");
        Library::Delete(file,builtins,copied["id"]);
        Check(file["emitters"].size()==2,"deleting a user entry removes it");
        Reject([&]{Library::Delete(file,builtins,copied["id"]);});Reject([&]{Library::Delete(file,builtins,"builtin.unknown");});
        file["emitters"].push_back({{"id",Id()},{"name","Damaged"}});
        auto merged=Library::Merge(builtins,file);
        Check(merged.size()==3 && merged[0]["id"]=="builtin.pipe_steam" && merged[0]["builtin"]==true && merged[1]["builtin"]==false && merged[2]["name"]=="Engine exhaust","hidden built-ins and damaged entries are left out of the list");
        Check(Library::Problems(file).size()==1 && Has(Library::Problems(file)[0].get<std::string>(),"Damaged"),"damaged entries are reported by name");
        Check(Library::Find(merged,"engine EXHAUST")["id"]==added["id"] && Library::Find(merged,"builtin.pipe_steam")["name"]=="Pipe steam","entries are found by id or name");
        Reject([&]{Library::Find(merged,"Nothing");});
        Library::RestoreBuiltins(file);
        Check(Library::Merge(builtins,file).size()==4,"restore brings hidden built-ins back");
        Check(Library::Merge(builtins,Json{{"version",1}}).size()==2,"a file without lists is an empty library");
        Reject([]{Library::Document(Json{{"version",1},{"emitters",Json::object()}});});
        Reject([]{Library::Document(Json{{"version",1},{"hiddenBuiltins",Json::array({1})}});});
        auto dir=std::filesystem::temp_directory_path()/("emitter-library-test-"+Id());auto path=dir/"emitter_library.json";
        WriteDocument(path,file);
        Check(Library::Merge(builtins,ReadDocument(path,Library::EmptyDocument()))==Library::Merge(builtins,file),"the user file round-trips through the atomic writer");
        Check(ReadDocument(dir/"missing.json",Library::EmptyDocument())==Library::EmptyDocument(),"a missing file is an empty library");
        std::filesystem::remove(path);std::filesystem::remove(dir);
        std::cout<<"PASS "<<checks<<" emitter library model checks\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
