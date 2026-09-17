// Native operations for the Map Design workspace. All calls run on the UI thread.
namespace
{
    bool DesignBool(Address actor,const char* name)
    {
        auto p=Property(actor,name);return p && IsA(p,"BoolProperty") && (Read<unsigned>(actor+Read<int>(p+0x3c))&Read<unsigned>(p+0x64));
    }
    void DesignSetBool(Address actor,const char* name,bool value)
    {
        auto p=Property(actor,name);if(!p || !IsA(p,"BoolProperty"))throw std::runtime_error(std::string("Unavailable editor flag: ")+name);
        auto slot=actor+Read<int>(p+0x3c),mask=Read<unsigned>(p+0x64);auto flags=Read<unsigned>(slot);Write(slot,value?flags|mask:flags&~mask);
    }
    Json DesignFingerprint(const Json& members)
    {
        for(auto& id:members)if(!ResolveIdentity(id))throw std::runtime_error("A blockout brush is missing. Undo/redo or restore it before editing this piece.");
        auto result=CaptureAssembly(members,{});
        for(auto& a:result["actors"])for(const char* flag:{"bHiddenEd","bLockLocation"})a["text"]=RemoveProperty(a.at("text"),flag);
        return result;
    }
}
Json DesignScene()
{
    Json result=Json::array();size_t edges=0;auto live=LiveActors();
    for(size_t i=2;i<live.size();++i)
    {
        auto actor=live[i];if(IsA(actor,"Camera"))continue;
        auto item=Identity(actor);item["level"]=LevelIdentity();item["generation"]=MapGeneration();item["position"]=Position(actor);item["selected"]=(Read<unsigned>(actor+0x2f4)&0x40)!=0;
        item["hidden"]=(Read<unsigned>(actor+0x2f4)&0x10)!=0;item["locked"]=DesignBool(actor,"bLockLocation");item["edges"]=Json::array();
        if(IsA(actor,"Brush"))
        {
            auto model=Read<Address>(actor+0x238),polys=model?Read<Address>(model+0x50):0;
            if(polys)
            {
                std::array<float,12> coords{};Call<void*>(actor,0xac,coords.data());
                for(auto poly:Array(polys+0x28,0x14c))
                {
                    auto count=Read<unsigned short>(poly+0x148);if(count<3 || count>16)continue;
                    std::vector<Vector> vertices;
                    for(int j=0;j<count;++j)
                    {
                        auto local=Read<std::array<float,3>>(poly+0x18+j*12);std::array<float,3> world{};
                        reinterpret_cast<void*(__thiscall*)(void*,void*,const void*)>(0x10eb2ba0)(local.data(),world.data(),coords.data());
                        Vector v{world[0],world[1],world[2]};Design::CheckVector(v);vertices.push_back(v);
                    }
                    for(int j=0;j<count;++j){if(++edges>150000)throw std::runtime_error("Design view exceeds 150,000 brush edges. Hide detail or use a smaller map.");item["edges"].push_back({vertices[j],vertices[(j+1)%count]});}
                }
            }
        }
        result.push_back(item);
    }
    return result;
}
Json DesignBlockout(const Json& spec,const Pose& frame,const Json& previous)
{
    Design::CheckVector(frame.position);auto definition=Design::Definition(spec);
    auto prepared=PreparePlacement(definition,frame,"Design_"+Id().substr(0,12)+"_",LevelPath(),{});
    if(!pasteHookReady || insertionText)throw std::runtime_error("Native brush insertion is unavailable or busy.");
    if(!previous.is_null())
    {
        if(previous.at("map")!=AuthoringMapKey() || DesignFingerprint(previous.at("members"))!=previous.at("fingerprint"))
            throw std::runtime_error("This piece was edited outside the toolkit. Detach it for manual editing or undo those edits first.");
        for(auto& id:previous.at("members"))if(DesignBool(ResolveIdentity(id),"bLockLocation"))throw std::runtime_error("Unlock this blockout layer first.");
    }
    auto selection=SelectedIdentities();Json members=Json::array(),fingerprint;
    const auto text=prepared.at("t3d").get<std::string>();
    struct Scope{~Scope(){insertionText=nullptr;}} scope;
    try
    {
        Transaction transaction("Create or resize blockout");
        if(!previous.is_null()) {Select(previous.at("members"));if(!Exec("ACTOR DELETE"))throw std::runtime_error("Could not replace the old blockout brushes.");}
        insertionText=text.c_str();Select(Json::array());Call(Engine(),0x26c,reinterpret_cast<void*>(Level()),0);
        for(auto& item:prepared.at("actors"))
        {
            auto a=Find(LevelPath()+"."+item.at("name").get<std::string>(),true);if(!a)throw std::runtime_error("Blockout creation failed.");
            Modify(a);SetPosition(a,item.at("position").get<Vector>());Write(Field(a,"Rotation"),item.at("rotation").get<Rotation>());Call(a,0x44);members.push_back(Identity(a));
        }
        if(SelectedIdentities().size()!=members.size())throw std::runtime_error("Unexpected number of blockout brushes.");
        fingerprint=DesignFingerprint(members);
        transaction.Commit();
    }
    catch(...){Select(selection);throw;}
    Select(members);Redraw();
    return {{"map",AuthoringMapKey()},{"spec",spec},{"position",frame.position},{"rotation",frame.rotation},{"members",members},{"fingerprint",fingerprint}};
}
void DesignAlign(const Json& scene,int axis,const std::string& mode,double spacing)
{
    std::vector<Vector> positions;std::vector<Address> actors;
    for(auto& item:scene)
    {
        if(item.at("level").get<uintptr_t>()!=LevelIdentity() || item.at("generation").get<unsigned>()!=MapGeneration())throw std::runtime_error("The map changed. Refresh the design view.");
        auto a=ResolveIdentity(item);if(!a || Position(a)!=item.at("position").get<Vector>())throw std::runtime_error("Selection moved. Refresh the design view.");
        if(DesignBool(a,"bLockLocation"))throw std::runtime_error("Unlock selected actors before aligning.");
        actors.push_back(a);positions.push_back(Position(a));
    }
    auto after=Design::Align(positions,axis,mode,spacing);Transaction transaction("Align or distribute actors");
    for(size_t i=0;i<actors.size();++i){Modify(actors[i]);SetPosition(actors[i],after[i]);Call(actors[i],0x44);}transaction.Commit();Redraw();
}
void DesignLayer(const Json& members,bool hidden,bool locked)
{
    std::vector<Address> actors;auto live=LiveActors();
    for(auto& id:members)
    {
        auto a=ResolveIdentity(id);if(!a)continue;
        if(a==live[0] || a==live[1] || IsA(a,"Camera"))throw std::runtime_error("Layers cannot contain editor infrastructure.");
        if(!Property(a,"bLockLocation"))throw std::runtime_error("Actor does not expose location locking.");actors.push_back(a);
    }
    Transaction transaction("Change design layer visibility and lock");
    for(auto a:actors){Modify(a);DesignSetBool(a,"bLockLocation",locked);auto flags=Read<unsigned>(a+0x2f4);Write(a+0x2f4,(flags&~0x10u)|(hidden?0x10u:0));if(hidden||locked)Write(a+0x2f4,Read<unsigned>(a+0x2f4)&~0x40u);}
    transaction.Commit();Redraw();
}
Json DesignSpawns()
{
    Json result=Json::array();for(auto a:LiveActors())if(IsA(a,"PlayerStart"))
    {
        auto item=Identity(a);item["position"]=Position(a);item["rotation"]=RotationOf(a);auto p=Property(a,"TeamNumber");
        item["team"]=p?MagicValue(p,a+Read<int>(p+0x3c)):Json("0");result.push_back(item);
    }
    return result;
}
Json DesignClearances()
{
    Json profiles=Json::array();
    for(const auto& entry:EventAssets("Engine.Pawn",true))
    {
        auto type=entry.at("path").get<std::string>();auto folded=Fold(type);
        if(folded.find("spy")==std::string::npos && folded.find("merc")==std::string::npos)continue;
        auto value=[&](const char* property)->double
        {
            TextOutput output;auto command="GET "+type+" "+property;
            if(!Call<int>(Engine()+0x28,0,command.c_str(),&output))throw std::runtime_error("Class default lookup unavailable.");
            auto text=output.text;auto end=text.find_last_not_of(" \r\n\t");if(end==std::string::npos)throw std::runtime_error("Empty class default.");text.resize(end+1);
            return Design::Number(text,1,10000);
        };
        try
        {
            double radius=value("CollisionRadius"),height=value("CollisionHeight");
            profiles.push_back({{"name",type+" standing"},{"width",radius*2},{"height",height*2}});
            try{profiles.push_back({{"name",type+" crouching"},{"width",value("CrouchRadius")*2},{"height",value("CrouchHeight")*2}});}catch(const std::exception&){}
        }
        catch(const std::exception&) { /* No guessed collision presets. */ }
    }
    return profiles;
}
void DesignPlay(const Json& start,const Pose& pose,bool launch)
{
    Design::CheckVector(pose.position);auto available=DesignSpawns();
    if(std::find(available.begin(),available.end(),start)==available.end())throw std::runtime_error("Spawn changed. Select a current team spawn.");
    // Native Play Level serializes a temporary map synchronously. Move only the
    // chosen team's starts during serialization, then restore exact live values.
    std::vector<std::pair<Address,Pose>> originals;
    for(auto& s:available)if(s.at("team")==start.at("team"))originals.push_back({ResolveIdentity(s),{s.at("position").get<Vector>(),s.at("rotation").get<Rotation>()}});
    struct Restore{std::vector<std::pair<Address,Pose>>& values;~Restore(){for(auto& [a,p]:values){try{SetPosition(a,p.position);Write(Field(a,"Rotation"),p.rotation);}catch(...){}}}} restore{originals};
    for(auto& [a,p]:originals){SetPosition(a,pose.position);Write(Field(a,"Rotation"),pose.rotation);}
    if(launch)PlayLevel();
}
