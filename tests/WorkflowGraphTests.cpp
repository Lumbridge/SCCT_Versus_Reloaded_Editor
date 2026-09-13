#include "../Reloaded.Editor/WorkflowGraphModel.h"
#include <iostream>
using namespace Workflow;
int main()
{
    int checks=0;
    auto check=[&](bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);};
    try
    {
        Json actors=Json::array();for(auto name:{"A","B","C","D","E","Alone"})actors.push_back({{"path",std::string("Map.")+name},{"name",name},{"class",std::string(name)=="E"?"Engine.Light":"Engine.Trigger"}});
        Json links=Json::array();
        auto edge=[&](const char* from,const char* to,const char* property,bool event=false,bool resolved=true){links.push_back({{"from",std::string("Map.")+from},{"to",std::string("Map.")+to},{"property",property},{"kind",event?"Event -> Tag":"Actor reference"},{"resolved",resolved}});};
        edge("A","B","Target");edge("B","A","Target");edge("B","C","Targets[0]");edge("A","B","Nested.Target");edge("D","E","Event",true);edge("D","Missing","Event",true,false);edge("C","C","Self");
        auto graph=Graph::Build(actors,links,{});
        auto tagged=actors;tagged[0]["tag"]="Door_Open";auto tagGraph=Graph::Build(tagged,links,{});check(tagGraph.nodes[0].tag=="Door_Open","actor tag retained on graph node");
        check(tagGraph.nodes[1].tag=="None","missing tag has explicit default");
        check(graph.islands.size()==2,"independent islands stay separate");check(graph.nodes.size()==6,"unconnected actor hidden, unresolved node visible");check(graph.edges.size()==7,"cycles, parallel references and self links retained");
        check(graph.nodes[0].island==graph.nodes[2].island,"weakly connected actor references grouped");
        check(graph.nodes[3].island!=graph.nodes[0].island,"unrelated event island separate");
        Graph::Options all;all.showUnconnected=true;check(Graph::Build(actors,links,all).islands.size()==3,"show isolated actors as singleton islands");
        Graph::Options local;local.root="map.a";local.depth=1;auto first=Graph::Build(actors,links,local);check(first.nodes.size()==2,"one-step neighbourhood is bounded");check(first.edges.size()==3,"incoming, outgoing and parallel edges retained");
        local.depth=2;check(Graph::Build(actors,links,local).nodes.size()==3,"expand adds next-step neighbour");local.root="Map.Alone";check(Graph::Build(actors,links,local).nodes.size()==1,"selected isolated actor remains visible");local.root="Deleted";check(Graph::Build(actors,links,local).nodes.empty(),"stale selection produces empty graph");
        Graph::Options filter;filter.relationship=2;auto events=Graph::Build(actors,links,filter);check(events.edges.size()==2 && events.islands.size()==1,"event filter preserves all targets");
        filter.relationship=1;check(Graph::Build(actors,links,filter).edges.size()==5,"reference filter includes arrays and structures");
        auto magicLinks=links;magicLinks.push_back({{"from","Map.A"},{"to","Map.D"},{"property","Groups[0].EventGroup[1].Event"},{"kind","EventGroup -> Tag"},{"resolved",true}});
        Graph::Options magicFilter;magicFilter.relationship=2;auto magicGraph=Graph::Build(actors,magicLinks,magicFilter);check(magicGraph.edges.size()==3,"EventGroup links included in tag relationship filter");
        magicFilter.relationship=1;check(Graph::Build(actors,magicLinks,magicFilter).edges.size()==5,"EventGroup links excluded from direct-reference filter");
        check(Graph::Build(actors,magicLinks,{}).islands.size()==1,"EventGroup relationships join formerly separate islands");
        filter.actorClass="Engine.Light";check(Graph::Build(actors,links,filter).nodes.empty(),"class filter removes incompatible endpoints");
        auto again=Graph::Build(actors,links,{});bool stable=true;for(size_t i=0;i<graph.nodes.size();++i)stable=stable && graph.nodes[i].x==again.nodes[i].x && graph.nodes[i].y==again.nodes[i].y;check(stable,"layout is deterministic");
        Json largeActors=Json::array(),star=Json::array();for(int i=0;i<600;++i){auto path="Map.N"+std::to_string(i);largeActors.push_back({{"path",path},{"class","Engine.Trigger"}});if(i)star.push_back({{"from","Map.N0"},{"to",path},{"kind","Actor reference"},{"property","Targets["+std::to_string(i)+"]"},{"resolved",true}});}
        auto large=Graph::Build(largeActors,star,{});check(large.nodes.size()==600 && large.islands.size()==1,"large fan-out remains connected");
        bool overlap=false;for(size_t i=0;i<large.nodes.size();++i)for(size_t j=i+1;j<large.nodes.size();++j)if(std::abs(large.nodes[i].x-large.nodes[j].x)<Graph::NodeWidth && std::abs(large.nodes[i].y-large.nodes[j].y)<Graph::NodeHeight)overlap=true;check(!overlap,"large fan-out layout has no overlapping nodes");
        const auto& hub=large.nodes[0];double radius=std::hypot(large.nodes[1].x-hub.x,large.nodes[1].y-hub.y);
        bool ring=true,left=false,right=false,above=false,below=false;
        for(size_t i=1;i<large.nodes.size();++i)
        {
            const auto& n=large.nodes[i];ring=ring && std::abs(std::hypot(n.x-hub.x,n.y-hub.y)-radius)<0.001;
            left=left || n.x<hub.x;right=right || n.x>hub.x;above=above || n.y<hub.y;below=below || n.y>hub.y;
        }
        check(ring && left && right && above && below,"large fan-out surrounds its hub on one ring without column wrapping");
        Graph::Options focused;focused.root="map.n0";
        auto focusedStar=Graph::Build(largeActors,star,focused);bool sameRadial=true;
        for(size_t i=0;i<large.nodes.size();++i)sameRadial=sameRadial && focusedStar.nodes[i].x==large.nodes[i].x && focusedStar.nodes[i].y==large.nodes[i].y;
        check(sameRadial,"focused fan-out uses the same radial layout as the whole-level view");
        focused.root="map.c";focused.depth=2;
        auto focusedChain=Graph::Build(actors,links,focused);
        const auto& focus=focusedChain.nodes[2];
        double nearRadius=std::hypot(focusedChain.nodes[1].x-focus.x,focusedChain.nodes[1].y-focus.y);
        double farRadius=std::hypot(focusedChain.nodes[0].x-focus.x,focusedChain.nodes[0].y-focus.y);
        check(nearRadius>Graph::NodeWidth && std::abs(farRadius-2*nearRadius)<0.001,
            "focused actor anchors radial depth even when another actor is the busiest or first in the map");
        auto reordered=largeActors;std::swap(reordered[0],reordered[25]);
        auto reorderedGraph=Graph::Build(reordered,star,{});
        check(std::abs(reorderedGraph.nodes[25].x-(reorderedGraph.islands[0].width-Graph::NodeWidth)/2)<1,
            "hub chosen by neighbour count even when it is not the first actor");
        auto duplicateLinks=star;for(int i=0;i<700;++i)duplicateLinks.push_back(star[0]);
        auto duplicated=Graph::Build(largeActors,duplicateLinks,{});
        check(duplicated.nodes[0].x==hub.x && duplicated.nodes[0].y==hub.y,"parallel properties do not change hub ranking or layout");
        Json tree=Json::array();for(int i=1;i<600;++i)tree.push_back({{"from","Map.N"+std::to_string((i-1)/3)},{"to","Map.N"+std::to_string(i)},{"kind","Event -> Tag"},{"property","Event"},{"resolved",true}});
        tree.push_back({{"from","Map.N599"},{"to","Map.N0"},{"kind","Actor reference"},{"property","Target"},{"resolved",true}});
        auto branches=Graph::Build(largeActors,tree,{});bool branchOverlap=false,bounded=true;
        for(size_t i=0;i<branches.nodes.size();++i)
        {
            const auto& n=branches.nodes[i];const auto& box=branches.islands[n.island];
            bounded=bounded && std::isfinite(n.x) && std::isfinite(n.y) && n.x>=box.x && n.y>=box.y && n.x+Graph::NodeWidth<=box.x+box.width && n.y+Graph::NodeHeight<=box.y+box.height;
            for(size_t j=i+1;j<branches.nodes.size();++j)if(std::abs(n.x-branches.nodes[j].x)<Graph::NodeWidth && std::abs(n.y-branches.nodes[j].y)<Graph::NodeHeight)branchOverlap=true;
        }
        check(!branchOverlap && bounded,"nested radial branches and cyclic cross-links retain non-overlapping bounded labels");
        check(Graph::Build(Json::array(),Json::array(),{}).islands.empty(),"empty maps supported");
        std::cout<<"PASS "<<checks<<" graph checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
