#pragma once
#include "WorkflowModel.h"
#include <set>
#include <stdexcept>

// Which actors RE+: Select and RE+: Visibility act on, from the editor's actor
// list. Hidden actors (another floor, Isolate, Brush Visibility) are never
// selected, so the next Delete or move cannot change what the user cannot see.
// Engine-independent; each rule returns indices into the list it was given.
namespace Workflow::Selection
{
struct Actor
{
    std::string path,cls,tag;
    bool selected=false,hidden=false;
    bool authorable=true; // False for the level info, the builder brush and viewport cameras.
};
inline Actor FromJson(const Json& actor)
{
    Actor result;
    result.path=actor.value("path",std::string());
    result.cls=actor.value("class",std::string());
    result.tag=actor.value("tag",std::string());
    result.selected=actor.value("selected",false);
    result.hidden=actor.value("hidden",false);
    result.authorable=actor.value("authorable",true);
    return result;
}
// The actors on show that share a value with the selection; a blank or None
// value matches nothing.
inline std::vector<size_t> Matching(const std::vector<Actor>& actors,std::string Actor::*value,const std::string& what)
{
    std::set<std::string> seeds;
    for(const auto& actor:actors)
        if(actor.selected && actor.authorable && !(actor.*value).empty() && Fold(actor.*value)!="none")seeds.insert(Fold(actor.*value));
    if(seeds.empty())throw std::runtime_error("Select an actor with a "+what+" first.");
    std::vector<size_t> result;
    for(size_t i=0;i<actors.size();++i)
        if(actors[i].authorable && !actors[i].hidden && seeds.count(Fold(actors[i].*value)))result.push_back(i);
    return result;
}
inline std::vector<size_t> SameClass(const std::vector<Actor>& actors){return Matching(actors,&Actor::cls,"class");}
inline std::vector<size_t> SameTag(const std::vector<Actor>& actors){return Matching(actors,&Actor::tag,"Tag");}
// The rest of the selection, among the actors on show.
inline std::vector<size_t> Inverted(const std::vector<Actor>& actors)
{
    std::vector<size_t> result;
    for(size_t i=0;i<actors.size();++i)if(actors[i].authorable && !actors[i].hidden && !actors[i].selected)result.push_back(i);
    return result;
}
inline std::vector<size_t> ToHide(const std::vector<Actor>& actors)
{
    std::vector<size_t> result;
    for(size_t i=0;i<actors.size();++i)if(actors[i].authorable && !actors[i].hidden && actors[i].selected)result.push_back(i);
    if(result.empty())throw std::runtime_error("Select something to hide first.");
    return result;
}
// Without a selection, isolating would hide the whole map in one step. With
// everything else hidden already there is nothing to do, which is no error.
inline std::vector<size_t> ToIsolate(const std::vector<Actor>& actors)
{
    bool kept=false;
    for(const auto& actor:actors)if(actor.authorable && actor.selected)kept=true;
    if(!kept)throw std::runtime_error("Select what should stay visible first.");
    return Inverted(actors);
}
// A hidden builder brush alone is not something Unhide all can show.
inline std::vector<size_t> ToUnhide(const std::vector<Actor>& actors)
{
    std::vector<size_t> result;
    for(size_t i=0;i<actors.size();++i)if(actors[i].authorable && actors[i].hidden)result.push_back(i);
    if(result.empty())throw std::runtime_error("Nothing is hidden.");
    return result;
}
}
