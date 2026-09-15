#pragma once
#include "WorkflowModel.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace Workflow::Cameras
{
inline std::string Path(const Json& camera) { return camera.at("actor").at("path"); }
inline std::string Link(const Json& camera,const char* field)
{
    auto refs=References(camera.at("values").at(field));
    return refs.empty()?std::string{}:refs.front().path;
}
inline std::string Name(const Json& camera)
{
    auto value=Json::parse(camera.at("values").at("CamName").get<std::string>(),nullptr,false);
    return value.is_string()?value.get<std::string>():Path(camera);
}
inline std::string Reference(const Json& camera)
{
    return camera.at("actor").at("class").get<std::string>()+"'"+Path(camera)+"'";
}
struct Network { Json cameras=Json::array(); bool loop=false; std::string warning; };
// Discover connected components using both directions so broken reciprocal links
// remain visible together. Never silently modify a malformed network on refresh.
inline std::vector<Network> Networks(const Json& cameras)
{
    std::map<std::string,size_t> indices;
    for(size_t i=0;i<cameras.size();++i)indices[Fold(Path(cameras[i]))]=i;
    std::vector<std::set<size_t>> neighbours(cameras.size());
    for(size_t i=0;i<cameras.size();++i)for(auto field:{"NextCam","PrevCam"})
    {
        auto found=indices.find(Fold(Link(cameras[i],field)));
        if(found!=indices.end()){neighbours[i].insert(found->second);neighbours[found->second].insert(i);}
    }
    std::set<size_t> used;std::vector<Network> result;
    for(size_t seed=0;seed<cameras.size();++seed)
    {
        if(used.count(seed))continue;
        std::set<size_t> members;std::vector<size_t> pending{seed};
        while(!pending.empty())
        {
            auto i=pending.back();pending.pop_back();if(!members.insert(i).second)continue;
            for(auto n:neighbours[i])pending.push_back(n);
        }
        size_t start=*members.begin();int firstCount=0;
        for(auto i:members)if(Link(cameras[i],"PrevCam").empty())start=i;
        for(auto i:members)if(Fold(cameras[i].at("values").at("bFirstCam"))=="true"){start=i;++firstCount;}
        Network network;std::set<size_t> ordered;
        auto walk=[&](size_t i)
        {
            while(members.count(i) && ordered.insert(i).second)
            {
                network.cameras.push_back(cameras[i]);auto next=indices.find(Fold(Link(cameras[i],"NextCam")));
                if(next==indices.end())break;i=next->second;
            }
        };
        walk(start);for(auto i:members)walk(i);
        network.loop=!network.cameras.empty() && Fold(Link(network.cameras.back(),"NextCam"))==Fold(Path(network.cameras.front()));
        bool valid=firstCount==1;
        for(size_t i=0;i<network.cameras.size();++i)
        {
            auto count=network.cameras.size();
            auto next=i+1<count?Path(network.cameras[i+1]):network.loop?Path(network.cameras[0]):"";
            auto prev=i>0?Path(network.cameras[i-1]):network.loop?Path(network.cameras[count-1]):"";
            valid=valid && Fold(Link(network.cameras[i],"NextCam"))==Fold(next) && Fold(Link(network.cameras[i],"PrevCam"))==Fold(prev);
        }
        if(!valid)network.warning="Links or first-camera flags need repair. Review the list, then Apply order.";
        used.insert(members.begin(),members.end());result.push_back(std::move(network));
    }
    return result;
}
inline Json Wiring(const Json& cameras,bool loop)
{
    Json changes=Json::array();std::set<std::string> unique;
    for(size_t i=0;i<cameras.size();++i)
    {
        if(!unique.insert(Fold(Path(cameras[i]))).second)throw std::runtime_error("A camera cannot appear twice in a network.");
        auto count=cameras.size();
        changes.push_back({{"NextCam",i+1<count?Reference(cameras[i+1]):loop?Reference(cameras[0]):"None"},
            {"PrevCam",i>0?Reference(cameras[i-1]):loop?Reference(cameras[count-1]):"None"},{"bFirstCam",i==0?"True":"False"}});
    }
    return changes;
}
// An edit must include complete components; otherwise it would leave an outside
// camera pointing into the rewritten chain. Multiple complete components may merge.
inline Json Ordered(const Json& snapshot,const Json& paths)
{
    Json result=Json::array();std::set<std::string> selected;
    for(const auto& path:paths)
    {
        auto key=Fold(path.get<std::string>());
        if(!selected.insert(key).second)throw std::runtime_error("Duplicate camera in order.");
        auto found=std::find_if(snapshot.begin(),snapshot.end(),[&](const Json& c){return Fold(Path(c))==key;});
        if(found==snapshot.end())throw std::runtime_error("Camera no longer exists. Refresh the panel.");result.push_back(*found);
    }
    for(const auto& network:Networks(snapshot))
    {
        size_t count=0;for(const auto& c:network.cameras)if(selected.count(Fold(Path(c))))++count;
        if(count && count!=network.cameras.size())throw std::runtime_error("Include every camera of each connected network before linking them.");
    }
    return result;
}
}
