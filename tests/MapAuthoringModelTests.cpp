#include "../Reloaded.Editor/MapAuthoringModel.h"
#include <iostream>
using namespace Workflow;
void Require(bool value){if(!value)throw std::runtime_error("Map authoring assertion failed");}
template<class F> void Reject(F operation){bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}Require(rejected);}
int main()
{
    Json document={{"format","scct.map-changes"},{"version",1},{"map","fixture"},{"operations",Json::array({
        {{"op","create"},{"id","Steam"},{"class","SBase.SSpawnableEmitter"},{"properties",Json::object()}},
        {{"op","component"},{"id","Particles"},{"owner","Steam"},{"class","Engine.SpriteEmitter"},{"properties",Json::object()}}
    })}};
    Authoring::Validate(document,"fixture");
    Reject([&]{Authoring::Validate(document,"other");});
    auto bad=document;bad["version"]=2;Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=document;bad["operations"][1]["id"]="steam";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=document;bad["operations"][0]["id"]="Steam\nEnd Actor";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=document;bad["operations"][0]["command"]="MAP NEW";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=document;bad["operations"][0]["op"]="delete";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=document;bad["operations"]=Json::array();Reject([&]{Authoring::Validate(bad,"fixture");});
    Json field={{"kind","ObjectProperty"},{"type","Engine.Actor"}};
    Json schema={{"kind","ArrayProperty"},{"inner",{{"kind","StructProperty"},{"fields",{{"Target",field},{"Delay",{{"kind","FloatProperty"}}}}}}}};
    Json values=Json::array({{{"Target",{{"$ref","Steam"}}},{"Delay","1.5"}}});
    auto resolve=[](const std::string& id,const Json&)->Json{if(id!="Steam")throw std::runtime_error("Unknown reference");return "SBase.SSpawnableEmitter'MyLevel.Steam_2'";};
    auto resolved=Authoring::Resolve(schema,values,resolve);
    Require(resolved[0]["Target"]=="SBase.SSpawnableEmitter'MyLevel.Steam_2'" && values[0]["Target"].is_object());
    values[0]["Target"]["$ref"]="missing";Reject([&]{Authoring::Resolve(schema,values,resolve);});
    values[0]["Target"]={{"$ref","Steam"},{"extra",true}};Reject([&]{Authoring::Resolve(schema,values,resolve);});
    values[0]["Target"]="None";values[0]["Delay"]="nan";Reject([&]{Authoring::Resolve(schema,values,resolve);});
    auto link=Json{{"op","link"},{"event","Event"},{"target","Steam"},{"trigger",false},{"delay","2.5"}};
    document["operations"].push_back(link);Authoring::Validate(document,"fixture");
    document["operations"].back()["delay"]="-1";Reject([&]{Authoring::Validate(document,"fixture");});
    document["operations"].back()["delay"]="2";document["operations"].back()["trigger"]=true;Reject([&]{Authoring::Validate(document,"fixture");});
    std::cout<<"Map authoring model tests passed\n";
}
