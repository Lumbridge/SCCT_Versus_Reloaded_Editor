#include "../Reloaded.Editor/WorkflowModel.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace Workflow;
static int checks=0;
void Check(bool condition,const char* what) { ++checks; if(!condition) throw std::runtime_error(what); }
template<class F> void Reject(F f,const char* what) { bool failed=false;try{f();}catch(const std::exception&){failed=true;}Check(failed,what); }
int main()
{
    try
    {
        std::string text="Begin Map\nBegin Actor Class=Engine.Trigger Name=A\n Tag=OpenDoor\n Event=CloseDoor\n Message=\"Actor'MyLevel.B'\"\n Target=Actor'\"MyLevel.B\"'\n Begin Object Class=Engine.Object Name=Owned\n Target=Actor'MyLevel.B'\n End Object\n Location=(X=0,Y=0,Z=0)\nEnd Actor\nBegin Actor Class=Engine.Mover Name=B\n Tag=CloseDoor\n Event=OpenDoor\nEnd Actor\nEnd Map\n";
        auto actors=ParseActors(text);Check(actors.size()==2,"parse actors with owned subobject");
        Check(References(text).size()==2,"do not treat authored strings as typed references");
        auto rewritten=RewriteReferences(text,{{"mylevel.b","Other.NewB"}});
        Check(rewritten.find("Message=\"Actor'MyLevel.B'\"")!=std::string::npos,"authored message preserved");
        Check(rewritten.find("Target=Actor'\"Other.NewB\"'")!=std::string::npos,"case insensitive path rewrite");
        Check(RewriteReferences("X=Actor'MyLevel.Bar'",{{"MyLevel.B","New.B"}})=="X=Actor'MyLevel.Bar'","reference prefix boundary");
        Check(RewriteReferences("X=Actor'MyLevel.B'",{{"MyLevel.B",""}})=="X=None","explicit unbound");
        auto set=SetProperty(actors[0].text,"Target","None");
        Check(Property(set,"Target")=="None","set top-level property");
        Check(set.find(" Target=Actor'MyLevel.B'")!=std::string::npos,"nested property preserved");
        Check(Property(SetProperty(set,"Location","(X=7,Y=8,Z=9)"),"Location")=="(X=7,Y=8,Z=9)","update existing property");
        Reject([]{ParseActors("Begin Actor Class=Engine.Actor Name=A\n");},"reject incomplete text");
        Reject([&]{ParseActors(text+actors[0].text);},"reject duplicate names");
        for(const Rotation r: {Rotation{0,16384,0},Rotation{7000,12000,5000},Rotation{16384,4000,0}})
        {
            Pose frame{{20,-15,40},r};Vector point{70,3,-90};auto back=TransformPoint(TransformPoint(point,frame),frame,true);
            for(int i=0;i<3;++i) Check(std::abs(back[i]-point[i])<1e-8,"point transform roundtrip");
            auto rotated=TransformRotation({4000,9000,-3000},r);auto restored=TransformRotation(rotated,r,true);
            Pose first{{},{4000,9000,-3000}},second{{},restored};
            auto v1=TransformPoint({1,2,3},first),v2=TransformPoint({1,2,3},second);
            for(int i=0;i<3;++i) Check(std::abs(v1[i]-v2[i])<0.002,"rotation composition roundtrip");
        }
        Json definition={{"id","stable"},{"name","Door"},{"actors",Json::array()},{"bindings",Json::array()}};
        for(auto& actor:actors) definition["actors"].push_back({{"name",actor.name},{"path","MyLevel."+actor.name},{"text",actor.text},{"tag",Property(actor.text,"Tag")},{"event",Property(actor.text,"Event")},{"position",Vector{1,2,3}},{"rotation",Rotation{}}});
        auto placement=PreparePlacement(definition,{{100,200,300},{0,16384,0}},"I1_","Other",{});
        std::string placed=placement.at("t3d");
        Check(placed.find("Name=I1_A")!=std::string::npos,"unique actor names");
        Check(placed.find("Event=I1_CloseDoor")!=std::string::npos,"event remap");
        Check(placed.find("Tag=I1_CloseDoor")!=std::string::npos,"tag remap");
        Check(placed.find("Other.I1_B")!=std::string::npos,"internal actor reference remap");
        Check(definition.at("actors")[0].at("name")=="A","placement leaves saved definition unchanged");
        auto canonical=CanonicalizeAssembly(definition,{{"MyLevel.A","SavedA"},{"MyLevel.B","SavedB"}});
        Check(canonical["actors"][0]["name"]=="SavedA" && canonical["actors"][0]["path"]=="Assembly.SavedA","canonical member identity retained during update");
        Check(canonical["actors"][0]["text"].get<std::string>().find("Assembly.SavedB")!=std::string::npos,"canonical internal references remapped");
        auto prefixed=definition;prefixed["actors"][0]["tag"]="RE_0123456789ab_Group";prefixed["actors"][1]["tag"]="RE_abcdef012345_Group";
        prefixed["actors"][0]["event"]="RE_abcdef012345_Group";
        auto groups=CanonicalizeAssembly(prefixed,{});
        Check(groups["actors"][0]["tag"]!=groups["actors"][1]["tag"],"canonicalization preserves distinct instance tag groups");
        Check(groups["actors"][0]["event"]==groups["actors"][1]["tag"],"canonicalization preserves tag-group relationships");
        Check(NormalizeExportNames("Name=Brush\xA7(123)\nMessage=\"Brush\xA7(123)\"\n")=="Name=Brush\nMessage=\"Brush\xA7(123)\"\n","native diagnostic name suffix removed only outside strings");
        auto bound=definition;bound["bindings"].push_back({{"id","external"},{"label","External target"},{"kind","object"},{"path","Elsewhere.Actor"}});
        Reject([&]{PreparePlacement(bound,Pose{},"I_","Map",{});},"external binding must be explicitly resolved or unbound");
        Check(!PreparePlacement(bound,Pose{},"I_","Map",{{"external",""}}).empty(),"explicit optional unbound accepted");
        Json entries=Json::array({{{"id","view1"},{"name","Roof"},{"cameras",Json::array()}}});
        UpdateEntry(entries,"view1",{{"id","wrong"},{"name","wrong"},{"cameras",Json::array({1})}});
        Check(entries.size()==1 && entries[0]["id"]=="view1" && entries[0]["name"]=="Roof" && entries[0]["cameras"].size()==1,"view update retains identity without duplication");
        auto before=entries; Reject([&]{UpdateEntry(entries,"missing",Json{});},"reject stale entry");Check(entries==before,"stale update does not mutate entries");
        Json assemblyEntries=Json::array({definition});auto changed=definition;changed["actors"].erase(0);UpdateEntry(assemblyEntries,"stable",changed);
        Check(assemblyEntries.size()==1 && assemblyEntries[0]["id"]=="stable" && assemblyEntries[0]["actors"].size()==1,"assembly membership update in place");
        auto dir=std::filesystem::temp_directory_path()/("workflow-test-"+Id());auto path=dir/"library.json";
        Json doc={{"version",1},{"entries",entries}};WriteDocument(path,doc);Check(ReadDocument(path,Json{})==doc,"disk roundtrip");
        doc["entries"]=assemblyEntries;WriteDocument(path,doc);Check(ReadDocument(path,Json{})==doc,"atomic overwrite");
        auto bad=dir/"bad.json";{std::ofstream out(bad);out<<"{broken";}Reject([&]{ReadDocument(bad,Json{});},"corrupt document does not become empty library");
        {std::ofstream out(bad);out<<"{\"version\":2}";}Reject([&]{ReadDocument(bad,Json{});},"future document version rejected");
        auto blocked=path/"child.json";Reject([&]{WriteDocument(blocked,doc);},"failed filesystem write reported");Check(ReadDocument(path,Json{})==doc,"failed write preserves existing document");
        std::filesystem::remove(path);std::filesystem::remove(bad);std::filesystem::remove(dir);
        std::cout<<"PASS "<<checks<<" workflow model checks\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
