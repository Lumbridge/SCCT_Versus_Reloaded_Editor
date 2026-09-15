// Included after MagicEventNative.inl inside Workflow::Editor.
Json CameraNetworks()
{
    Json result=Json::array();
    for(auto actor:LiveActors())if(IsA(actor,"SBase.SCamNetwork"))result.push_back(InspectActor(Identity(actor)));
    std::sort(result.begin(),result.end(),[](const Json& a,const Json& b){return Cameras::Path(a)<Cameras::Path(b);});return result;
}
namespace
{
    void CheckCameras(const Json& snapshot)
    {
        if(snapshot!=CameraNetworks())throw std::runtime_error("Cameras or map changed. Refresh before editing the network.");
    }
    void WireCameras(const Json& ordered,bool loop)
    {
        auto changes=Cameras::Wiring(ordered,loop);
        for(size_t i=0;i<ordered.size();++i)
        {
            auto actor=MagicResolve(ordered[i].at("actor"));if(!actor)throw std::runtime_error("Camera no longer exists.");
            Modify(actor);
            for(auto it=changes[i].begin();it!=changes[i].end();++it)MagicSet(actor,it.key(),it.value());
            Call(actor,0x44);
        }
    }
}
void OrderCameras(const Json& snapshot,const Json& paths,bool loop,const std::string& detach)
{
    CheckCameras(snapshot);auto ordered=Cameras::Ordered(snapshot,paths);Json removed;
    if(!detach.empty())
    {
        auto found=std::find_if(ordered.begin(),ordered.end(),[&](const Json& c){return Cameras::Path(c)==detach;});
        if(found==ordered.end())throw std::runtime_error("Select a camera in this network.");removed=*found;ordered.erase(found);
    }
    Transaction transaction("Arrange camera network");WireCameras(ordered,loop);
    if(!removed.is_null())WireCameras(Json::array({removed}),false);
    transaction.Commit();Redraw();
}
Json AddNetworkCamera(const Json& snapshot,const Json& paths,bool loop)
{
    CheckCameras(snapshot);auto ordered=Cameras::Ordered(snapshot,paths);
    if(!pasteHookReady)throw std::runtime_error("Native actor insertion is unavailable.");
    auto classes=EventClasses();if(std::none_of(classes.begin(),classes.end(),[](const Json& c){return c.at("class")=="SBase.SCamNetwork";}))throw std::runtime_error("SCamNetwork is not loaded.");
    int suffix=1;std::string name;
    do{name="SCamNetwork_"+std::to_string(suffix++);}while(Find(LevelPath()+"."+name));
    auto position=BuilderPose().position;
    auto text="Begin Map\r\nBegin Actor Class=SBase.SCamNetwork Name="+name+"\r\nLocation="+VectorText(position)+"\r\nEnd Actor\r\nEnd Map\r\n";
    if(insertionText)throw std::runtime_error("Actor insertion is busy.");
    struct Scope{~Scope(){insertionText=nullptr;}} scope;insertionText=text.c_str();
    auto before=SelectedIdentities();Json identity;
    try
    {
        Transaction transaction("Add network camera");Select(Json::array());Call(Engine(),0x26c,reinterpret_cast<void*>(Level()),0);
        auto actor=Find(LevelPath()+"."+name,true);if(!actor || SelectedIdentities().size()!=1)throw std::runtime_error("Camera creation failed.");
        Modify(actor);MagicSet(actor,"CamName",Json("Camera "+std::to_string(ordered.size()+1)).dump());identity=Identity(actor);
        ordered.push_back(InspectActor(identity));WireCameras(ordered,loop);transaction.Commit();
    }
    catch(...){Select(before);throw;}
    Select(Json::array({identity}));Redraw();return identity;
}
