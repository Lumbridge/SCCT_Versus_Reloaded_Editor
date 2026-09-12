#pragma once
#include "WorkflowModel.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <stdexcept>

namespace Workflow::Graph
{
inline constexpr double NodeWidth=220,NodeHeight=84;
inline bool IsTagRelationship(const std::string& kind) {return kind=="Event -> Tag" || kind=="EventGroup -> Tag";}
struct Options
{
    std::string root, actorClass;
    int depth=1, relationship=0; // 0 all, 1 actor references, 2 Event/Tag
    bool showUnconnected=false;
};
struct Node
{
    std::string id,name,type;
    Json identity;
    bool missing=false;
    double x=0,y=0;
    size_t island=0;
    std::string tag="None";
};
struct Edge { size_t from,to; std::string property,kind; bool resolved; };
struct Island { double x=0,y=0,width=0,height=0; size_t members=0; };
struct Model { std::vector<Node> nodes; std::vector<Edge> edges; std::vector<Island> islands; };

// Root focused views at the selected actor, and whole-level islands at their busiest actor. Allocate angular sectors
// by branch size so descendants stay together, then space rings for label bounds.
inline void LayoutRadial(Model& model,const std::vector<size_t>& members,
    const std::vector<std::vector<size_t>>& neighbours,Island& island,const std::string& focusedRoot)
{
    constexpr double Pi=3.14159265358979323846;
    const double spacing=std::hypot(NodeWidth,NodeHeight)+44;
    std::map<size_t,size_t> local;
    size_t root=0;
    for(size_t i=0;i<members.size();++i)
    {
        local[members[i]]=i;
        if(!model.nodes[members[i]].missing && (model.nodes[members[root]].missing ||
            neighbours[members[i]].size()>neighbours[members[root]].size()))root=i;
    }
    if(!focusedRoot.empty())for(size_t i=0;i<members.size();++i)
        if(Fold(model.nodes[members[i]].id)==Fold(focusedRoot)){root=i;break;}
    std::vector<std::vector<size_t>> children(members.size()),rings(1);
    std::vector<size_t> order{root},depth(members.size()),weight(members.size(),1);
    std::vector<bool> seen(members.size());seen[root]=true;rings[0].push_back(root);
    for(size_t cursor=0;cursor<order.size();++cursor)
    {
        auto i=order[cursor];
        for(auto neighbour:neighbours[members[i]])
        {
            auto n=local.at(neighbour);if(seen[n])continue;
            seen[n]=true;children[i].push_back(n);order.push_back(n);depth[n]=depth[i]+1;
            if(rings.size()<=depth[n])rings.resize(depth[n]+1);
            rings[depth[n]].push_back(n);
        }
    }
    for(auto it=order.rbegin();it!=order.rend();++it)if(!children[*it].empty())
    {weight[*it]=0;for(auto n:children[*it])weight[*it]+=weight[n];}
    std::vector<double> begin(members.size()),span(members.size()),angle(members.size());
    span[root]=2*Pi;
    for(auto i:order)
    {
        double next=begin[i];
        for(auto n:children[i])
        {
            begin[n]=next;span[n]=span[i]*double(weight[n])/double(weight[i]);
            angle[n]=next+span[n]/2;next+=span[n];
        }
    }
    double radius=0,minX=0,minY=0,maxX=0,maxY=0;
    for(size_t d=0;d<rings.size();++d)
    {
        if(d)
        {
            radius+=spacing;
            std::vector<double> angles;for(auto i:rings[d])angles.push_back(angle[i]);
            std::sort(angles.begin(),angles.end());
            if(angles.size()>1)
            {
                double gap=2*Pi-angles.back()+angles.front();
                for(size_t i=1;i<angles.size();++i)gap=std::min(gap,angles[i]-angles[i-1]);
                radius=std::max(radius,spacing/(2*std::sin(gap/2)));
            }
        }
        for(auto i:rings[d])
        {
            auto& node=model.nodes[members[i]];
            node.x=radius*std::cos(angle[i]-Pi/2);node.y=radius*std::sin(angle[i]-Pi/2);
            minX=std::min(minX,node.x);minY=std::min(minY,node.y);
            maxX=std::max(maxX,node.x);maxY=std::max(maxY,node.y);
        }
    }
    for(auto i:members){model.nodes[i].x+=40-minX;model.nodes[i].y+=60-minY;}
    island.width=maxX-minX+NodeWidth+80;island.height=maxY-minY+NodeHeight+100;
}

// Weakly connected components deliberately preserve cyclic and one-to-many links.
// Layout is bounded and deterministic; no iterative force simulation on the UI thread.
inline Model Build(const Json& actors,const Json& links,const Options& options)
{
    if(actors.size()>20000 || links.size()>100000) throw std::runtime_error("Graph exceeds 20,000 actors or 100,000 links. Use the connection list for this map.");
    Model source; std::map<std::string,size_t> ids;
    auto add=[&](Node n) { auto id=Fold(n.id);auto found=ids.find(id);if(found!=ids.end())return found->second;size_t i=source.nodes.size();ids[id]=i;source.nodes.push_back(std::move(n));return i; };
    for(const auto& a:actors)
    {
        std::string type=a.at("class"),path=a.at("path");
        if(type=="Engine.Camera" || type=="Engine.LevelInfo")continue;
        if(!options.actorClass.empty() && Fold(type)!=Fold(options.actorClass))continue;
        Node node{path,a.value("name",path.substr(path.find_last_of('.')+1)),type,a};node.tag=a.value("tag",std::string("None"));add(std::move(node));
    }
    for(const auto& link:links)
    {
        auto kind=link.at("kind").get<std::string>();bool event=IsTagRelationship(kind);
        if((options.relationship==1 && event) || (options.relationship==2 && !event))continue;
        auto from=ids.find(Fold(link.at("from").get<std::string>()));if(from==ids.end())continue;
        size_t first=from->second,last=0;bool resolved=link.at("resolved");auto target=link.at("to").get<std::string>();
        if(resolved) {auto to=ids.find(Fold(target));if(to==ids.end())continue;last=to->second;}
        else last=add({"?"+kind+":"+target,target,"Unresolved target",Json{},true});
        source.edges.push_back({first,last,link.at("property"),kind,resolved});
    }
    std::vector<std::vector<size_t>> neighbours(source.nodes.size());
    for(const auto& e:source.edges) {neighbours[e.from].push_back(e.to);neighbours[e.to].push_back(e.from);}
    std::vector<bool> keep(source.nodes.size(),false);
    if(!options.root.empty())
    {
        auto root=ids.find(Fold(options.root));
        if(root!=ids.end())
        {
            std::queue<std::pair<size_t,int>> q;q.push({root->second,0});keep[root->second]=true;
            while(!q.empty()) {auto [i,d]=q.front();q.pop();if(d>=std::clamp(options.depth,1,10))continue;for(auto n:neighbours[i])if(!keep[n]){keep[n]=true;q.push({n,d+1});}}
        }
    }
    else for(size_t i=0;i<keep.size();++i)keep[i]=options.showUnconnected || !neighbours[i].empty();
    Model result;std::vector<size_t> remap(keep.size());
    for(size_t i=0;i<keep.size();++i)if(keep[i]){remap[i]=result.nodes.size();result.nodes.push_back(source.nodes[i]);}
    for(auto e:source.edges)if(keep[e.from] && keep[e.to]){e.from=remap[e.from];e.to=remap[e.to];result.edges.push_back(e);}
    neighbours.assign(result.nodes.size(),{});
    for(const auto& e:result.edges){neighbours[e.from].push_back(e.to);neighbours[e.to].push_back(e.from);}
    // Parallel properties do not make an actor a busier hub; self-links add no neighbour.
    for(size_t i=0;i<neighbours.size();++i)
    {
        auto& list=neighbours[i];std::sort(list.begin(),list.end());
        list.erase(std::unique(list.begin(),list.end()),list.end());
        list.erase(std::remove(list.begin(),list.end(),i),list.end());
    }
    std::vector<bool> seen(result.nodes.size(),false);
    for(size_t start=0;start<result.nodes.size();++start)if(!seen[start])
    {
        std::queue<size_t> q;q.push(start);seen[start]=true;
        std::vector<size_t> members;Island island;size_t index=result.islands.size();
        while(!q.empty())
        {
            auto i=q.front();q.pop();result.nodes[i].island=index;
            members.push_back(i);++island.members;
            for(auto n:neighbours[i])if(!seen[n]){seen[n]=true;q.push(n);}
        }
        LayoutRadial(result,members,neighbours,island,options.root);
        result.islands.push_back(island);
    }
    // Pack islands in rows; each retains an explicit boundary and heading.
    double area=0;for(auto& i:result.islands)area+=(i.width+60)*(i.height+60);
    double rowWidth=std::max(1100.0,std::sqrt(area)*1.4),x=0,y=0,height=0;
    for(auto& i:result.islands){if(x && x+i.width>rowWidth){x=0;y+=height+60;height=0;}i.x=x;i.y=y;x+=i.width+60;height=std::max(height,i.height);}
    for(auto& n:result.nodes){const auto& i=result.islands[n.island];n.x+=i.x;n.y+=i.y;}
    return result;
}
}
