#include "../Reloaded.Editor/CameraNetworkModel.h"
#include <cassert>
#include <iostream>
using namespace Workflow;
Json Camera(const char* name)
{
    return {{"actor",{{"path",std::string("Map.")+name},{"class","SBase.SCamNetwork"}}},
        {"values",{{"CamName",Json(name).dump()},{"NextCam","None"},{"PrevCam","None"},{"bFirstCam","False"},{"Tag","PreserveTag"}}}};
}
Json Wire(Json cameras,bool loop)
{
    auto changes=Cameras::Wiring(cameras,loop);
    for(size_t i=0;i<cameras.size();++i)cameras[i]["values"].update(changes[i]);return cameras;
}
template<class F> void Reject(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main()
{
    assert(Cameras::Networks(Json::array()).empty());
    auto a=Camera("A"),b=Camera("B"),c=Camera("C");
    auto chain=Wire(Json::array({a,b,c}),false);auto networks=Cameras::Networks(chain);
    assert(networks.size()==1 && networks[0].warning.empty() && !networks[0].loop);
    assert(Cameras::Path(networks[0].cameras[0])=="Map.A");
    auto shuffled=Json::array({chain[2],chain[0],chain[1]});
    assert(Cameras::Networks(shuffled)[0].cameras==chain);
    auto ring=Wire(Json::array({c,a,b}),true);networks=Cameras::Networks(ring);
    assert(networks.size()==1 && networks[0].loop && networks[0].warning.empty());
    assert(Cameras::Link(ring[0],"PrevCam")=="Map.B" && Cameras::Link(ring[2],"NextCam")=="Map.C");
    auto single=Wire(Json::array({a}),true);assert(Cameras::Networks(single)[0].warning.empty());
    assert(Cameras::Link(single[0],"NextCam")=="Map.A");
    auto separate=chain;separate.push_back(Wire(Json::array({Camera("D")}),false)[0]);assert(Cameras::Networks(separate).size()==2);
    assert(Cameras::Ordered(separate,Json::array({"Map.C","Map.B","Map.A"})).size()==3);
    Reject([&]{Cameras::Ordered(chain,Json::array({"Map.A"}));});
    Reject([&]{Cameras::Ordered(chain,Json::array({"Map.A","Map.A"}));});
    Reject([&]{Cameras::Ordered(chain,Json::array({"Map.Missing"}));});
    Reject([&]{Cameras::Wiring(Json::array({a,a}),false);});
    auto broken=chain;broken[1]["values"]["PrevCam"]="None";assert(!Cameras::Networks(broken)[0].warning.empty());
    broken=chain;broken[2]["values"]["NextCam"]="SBase.SCamNetwork'Map.Deleted'";assert(!Cameras::Networks(broken)[0].warning.empty());
    broken=chain;broken[1]["values"]["bFirstCam"]="True";assert(!Cameras::Networks(broken)[0].warning.empty());
    auto fixed=Wire(Cameras::Networks(broken)[0].cameras,false);assert(Cameras::Networks(fixed)[0].warning.empty());
    for(const auto& camera:fixed)assert(camera["values"]["Tag"]=="PreserveTag");
    a["values"]["CamName"]=Json("Loading bay \"east\"").dump();assert(Cameras::Name(a)=="Loading bay \"east\"");
    std::cout<<"Camera network model tests passed\n";
}
