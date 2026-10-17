#include "../Reloaded.Editor/SelectionModel.h"
#include <iostream>
#include <source_location>
using namespace Workflow;
using Names=std::set<std::string>;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void Reject(F f,const std::string& text,std::source_location where=std::source_location::current())
{
    std::string caught;
    try{f();}catch(const std::exception& e){caught=e.what();}
    if(caught!=text)throw std::runtime_error("line "+std::to_string(where.line())+": expected '"+text+"', got '"+caught+"'");
}
// A map as Editor::Actors lists it: the level info and the builder brush come
// first and are not authorable, and neither is a viewport camera.
std::vector<Selection::Actor> Map(const Names& selected)
{
    const Json rows=Json::array({
        {{"path","MyLevel.LevelInfo0"},{"class","Engine.LevelInfo"},{"tag","LevelInfo"},{"authorable",false}},
        {{"path","MyLevel.Brush0"},{"class","Engine.Brush"},{"tag","Brush"},{"authorable",false}},
        {{"path","MyLevel.Camera0"},{"class","Engine.Camera"},{"tag","Cam_1"},{"authorable",false}},
        {{"path","MyLevel.SCamera0"},{"class","SBase.SCamera"},{"tag","Cam_1"}},
        {{"path","MyLevel.SCamera1"},{"class","SBase.SCamera"},{"tag","Cam_2"}},
        {{"path","MyLevel.SCamera2"},{"class","SBase.SCamera"},{"tag","Cam_1"},{"hidden",true}},
        {{"path","MyLevel.Light0"},{"class","Engine.Light"},{"tag","None"}},
        {{"path","MyLevel.Light1"},{"class","Engine.Light"},{"tag","None"},{"hidden",true}},
        {{"path","MyLevel.Mover0"},{"class","Engine.Mover"},{"tag","CAM_1"}},
        {{"path","MyLevel.Mover1"},{"class","Engine.Mover"},{"tag",""}}});
    std::vector<Selection::Actor> actors;
    for(auto row:rows)
    {
        if(selected.count(row.at("path").get<std::string>()))row["selected"]=true;
        actors.push_back(Selection::FromJson(row));
    }
    return actors;
}
Names Picked(const std::vector<Selection::Actor>& actors,const std::vector<size_t>& indices)
{
    Names paths;
    for(auto i:indices)paths.insert(actors.at(i).path);
    return paths;
}
int main()
{
  try
  {
    // A row reads class into cls; selected, hidden and authorable have defaults.
    auto plain=Selection::FromJson({{"path","MyLevel.Pickup0"},{"class","SBase.SPickup"},{"tag","Ammo"}});
    Check(plain.cls=="SBase.SPickup" && plain.tag=="Ammo" && !plain.selected && !plain.hidden && plain.authorable,"row defaults incorrect");
    auto flagged=Selection::FromJson({{"path","MyLevel.Brush0"},{"selected",true},{"hidden",true},{"authorable",false}});
    Check(flagged.selected && flagged.hidden && !flagged.authorable,"row flags are read");

    // Security cameras are ordinary actors; a viewport camera never is.
    auto map=Map({"MyLevel.SCamera0"});
    Check(Picked(map,Selection::SameClass(map))==Names{"MyLevel.SCamera0","MyLevel.SCamera1"},"All of this class takes the visible security cameras");
    Check(Picked(map,Selection::SameTag(map))==Names{"MyLevel.SCamera0","MyLevel.Mover0"},"All with this Tag folds case and skips hidden actors and viewport cameras");
    Check(Picked(map,Selection::Inverted(map))==Names{"MyLevel.SCamera1","MyLevel.Light0","MyLevel.Mover0","MyLevel.Mover1"},"Invert takes visible authorable actors only");
    Check(Picked(map,Selection::ToHide(map))==Names{"MyLevel.SCamera0"},"a selected security camera can be hidden");

    // A viewport camera is no seed, and blank or None tags match nothing.
    auto viewport=Map({"MyLevel.Camera0"});
    Reject([&]{Selection::SameTag(viewport);},"Select an actor with a Tag first.");
    Reject([&]{Selection::SameClass(viewport);},"Select an actor with a class first.");
    Reject([&]{Selection::ToHide(viewport);},"Select something to hide first.");
    auto untagged=Map({"MyLevel.Light0","MyLevel.Mover1"});
    Reject([&]{Selection::SameTag(untagged);},"Select an actor with a Tag first.");
    auto mixed=Map({"MyLevel.Light0","MyLevel.SCamera1"});
    Check(Picked(mixed,Selection::SameTag(mixed))==Names{"MyLevel.SCamera1"},"a None tag among the selection adds nothing");
    Check(Picked(mixed,Selection::SameClass(mixed))==Names{"MyLevel.Light0","MyLevel.SCamera0","MyLevel.SCamera1"},"several classes at once, hidden ones left out");

    // Isolate needs a selection: with none, or only the builder brush, it
    // would hide the whole map.
    auto none=Map({});
    Reject([&]{Selection::ToIsolate(none);},"Select what should stay visible first.");
    Reject([&]{Selection::ToHide(none);},"Select something to hide first.");
    Reject([&]{Selection::SameClass(none);},"Select an actor with a class first.");
    Check(Selection::Inverted(none).size()==5,"inverting nothing takes every visible actor");
    auto builder=Map({"MyLevel.Brush0"});
    Reject([&]{Selection::ToIsolate(builder);},"Select what should stay visible first.");
    Reject([&]{Selection::ToHide(builder);},"Select something to hide first.");

    // Isolate leaves already-hidden actors alone. Hide selected takes a hidden
    // part of the selection along, so it leaves the selection too, but it
    // still needs something on show to hide.
    auto movers=Map({"MyLevel.Mover0","MyLevel.Mover1"});
    Check(Picked(movers,Selection::ToIsolate(movers))==Names{"MyLevel.SCamera0","MyLevel.SCamera1","MyLevel.Light0"},"isolate hides the visible rest only");
    auto hiddenToo=Map({"MyLevel.SCamera0","MyLevel.SCamera2"});
    Check(Picked(hiddenToo,Selection::ToHide(hiddenToo))==Names{"MyLevel.SCamera0","MyLevel.SCamera2"},"a hidden actor in the selection is deselected with the rest");
    auto hiddenLight=Map({"MyLevel.Mover0","MyLevel.Light1","MyLevel.Camera0"});
    Check(Picked(hiddenLight,Selection::ToHide(hiddenLight))==Names{"MyLevel.Mover0","MyLevel.Light1"},"a viewport camera is not taken along");
    auto hiddenOnly=Map({"MyLevel.Light1"});
    Reject([&]{Selection::ToHide(hiddenOnly);},"Select something to hide first.");
    Reject([&]{Selection::ToHide(Map({"MyLevel.SCamera2","MyLevel.Light1","MyLevel.Brush0"}));},"Select something to hide first.");
    // A hidden selection (a Scene panel row, a restored Working View) keeps
    // nothing on show, so isolating it would hide the whole map too.
    Reject([&]{Selection::ToIsolate(Map({"MyLevel.SCamera2"}));},"Select what should stay visible first.");
    Reject([&]{Selection::ToIsolate(hiddenOnly);},"Select what should stay visible first.");
    Check(Picked(hiddenToo,Selection::ToIsolate(hiddenToo))==Names{"MyLevel.SCamera1","MyLevel.Light0","MyLevel.Mover0","MyLevel.Mover1"},"one visible actor in the selection is enough to isolate");
    auto everyone=Map({"MyLevel.SCamera0","MyLevel.SCamera1","MyLevel.Light0","MyLevel.Mover0","MyLevel.Mover1"});
    Check(Selection::ToIsolate(everyone).empty(),"isolating with the rest already hidden is nothing to do");

    // Unhide all shows hidden authorable actors, and says so when there are none:
    // a hidden builder brush alone does not count.
    Check(Picked(map,Selection::ToUnhide(map))==Names{"MyLevel.SCamera2","MyLevel.Light1"},"Unhide all lists the hidden actors");
    auto shown=map;
    for(auto& actor:shown)actor.hidden=!actor.authorable;
    Reject([&]{Selection::ToUnhide(shown);},"Nothing is hidden.");
    std::cout<<"Selection model tests passed\n";
  }
  catch(const std::exception& e)
  {
    std::cerr<<"Selection model tests failed: "<<e.what()<<"\n";
    return 1;
  }
}
