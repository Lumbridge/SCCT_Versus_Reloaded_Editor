// Native side of Smooth Staircase (StairSmoothModel.h holds the geometry). All
// calls run on the UI thread; smoothing and removing are one Undo step each.
namespace
{
    namespace Stairs = Workflow::StairSmooth;

    // A brush's polygons in the world, as the viewports draw it.
    std::vector<Design::Face> StairBrushFaces(Address actor)
    {
        std::vector<Design::Face> faces;
        auto model=Read<Address>(actor+0x238),polys=model?Read<Address>(model+0x50):0;
        if(!polys)return faces;
        std::array<float,12> coords{};Call<void*>(actor,0xac,coords.data());
        for(auto poly:Array(polys+0x28,0x14c))
        {
            const auto count=Read<unsigned short>(poly+0x148);if(count<3 || count>16)continue;
            Design::Face face;
            for(unsigned i=0;i<count;++i)
            {
                auto local=Read<std::array<float,3>>(poly+0x18+i*12);std::array<float,3> world{};
                reinterpret_cast<void*(__thiscall*)(void*,void*,const void*)>(0x10eb2ba0)(local.data(),world.data(),coords.data());
                face.push_back({world[0],world[1],world[2]});
            }
            faces.push_back(std::move(face));
        }
        return faces;
    }
    // The CSG brushes the menu acts on: selected ones, and on the surface menu
    // the brushes of the selected faces too. Volumes and movers are brushes
    // that build nothing, so they are left out.
    std::vector<Address> StairCandidates(bool surfaces)
    {
        std::vector<Address> result;
        auto add=[&](Address a)
        {
            if(a && IsA(a,"Brush") && !IsA(a,"Volume") && !IsA(a,"Mover") && std::find(result.begin(),result.end(),a)==result.end())result.push_back(a);
        };
        for(auto a:LiveActors())if(Read<unsigned>(a+0x2f4)&0x40)add(a);
        if(surfaces)
        {
            try{for(const auto& id:SelectedSurfaceBrushes())add(ResolveIdentity(id));}
            catch(const std::exception&) { /* No faces selected, or faces of cooked BSP. */ }
        }
        return result;
    }
    // Ramps are named after the brush they smooth, so smoothing it again
    // replaces them and Remove finds them. A name too long to prefix (names
    // stop at 63 characters) goes by a hash of itself.
    std::string StairRampPrefix(Address brush)
    {
        auto name=NameOf(brush);
        if(name.size()>24 || !std::regex_match(name,std::regex("[A-Za-z0-9_]+")))
        {
            unsigned hash=2166136261u;for(unsigned char c:Fold(name)){hash^=c;hash*=16777619u;}
            std::ostringstream s;s<<"H"<<std::hex<<hash;name=s.str();
        }
        return "StairRamp_"+name+"_";
    }
    std::vector<Address> StairRamps(const std::vector<Address>& brushes)
    {
        std::vector<std::string> prefixes;for(auto b:brushes)prefixes.push_back(Fold(StairRampPrefix(b)));
        std::vector<Address> result;
        for(auto a:LiveActors())
        {
            if(!IsA(a,"BlockingVolume"))continue;
            const auto name=Fold(NameOf(a));
            for(const auto& p:prefixes)if(name.rfind(p,0)==0){result.push_back(a);break;}
        }
        return result;
    }
    Json StairIdentities(const std::vector<Address>& actors)
    {
        Json result=Json::array();for(auto a:actors)result.push_back(Identity(a));return result;
    }
    // A volume collides with actors (bCollideActors, +0x2f0 bit 0x1000), so
    // it lives in the level's collision hash (ULevel+0x3a4c), filed by where
    // it was when added. Stock Undo takes every colliding actor out of the
    // hash first and files them all again after (0x10e59c92, 0x10e59d01);
    // smoothing does the same around its edits, with the hash detached in
    // between so a pasted volume is not filed before its brush is built. A
    // level opened without the frame has no hash at all.
    constexpr unsigned kCollideActors=0x1000;
    struct DetachedCollisionHash
    {
        Address level,hash;
        DetachedCollisionHash():level(Level()),hash(Read<Address>(Level()+0x3a4c))
        {
            if(!hash)return;
            for(auto a:LiveActors())if(Read<unsigned>(a+0x2f0)&kCollideActors)
                reinterpret_cast<void(__thiscall*)(void*,void*)>(0x1115e7b0)(reinterpret_cast<void*>(hash),reinterpret_cast<void*>(a));
            Write<Address>(level+0x3a4c,0);
        }
        ~DetachedCollisionHash()
        {
            if(!hash)return;
            try
            {
                Write(level+0x3a4c,hash);
                for(auto a:LiveActors())if(Read<unsigned>(a+0x2f0)&kCollideActors)
                    reinterpret_cast<void(__thiscall*)(void*,void*)>(0x1115fd70)(reinterpret_cast<void*>(hash),reinterpret_cast<void*>(a));
            }
            catch(const std::exception&) { Logger::log("Smooth Staircase: could not restore the collision hash"); }
        }
        DetachedCollisionHash(const DetachedCollisionHash&)=delete;
        DetachedCollisionHash& operator=(const DetachedCollisionHash&)=delete;
    };
    // Native ACTOR DELETE takes a colliding actor out of the hash without
    // checking there is one; with the hash detached, the ramps stop colliding
    // first (Undo puts the flag back with the actor).
    void DeleteStairRamps(const std::vector<Address>& ramps)
    {
        for(auto a:ramps){Modify(a);Write(a+0x2f0,Read<unsigned>(a+0x2f0)&~kCollideActors);}
        Select(StairIdentities(ramps));
        if(!Exec("ACTOR DELETE"))throw std::runtime_error("Could not delete the staircase ramp.");
    }
}
Json StairSmoothState(bool surfaces)
{
    Engine();
    const auto candidates=StairCandidates(surfaces);
    Json state={{"stairs",false},{"steps",0},{"winding",false},{"ramps",StairRamps(candidates).size()}};
    if(candidates.empty())return state;
    std::vector<std::vector<Design::Face>> faces;for(auto a:candidates)faces.push_back(StairBrushFaces(a));
    try
    {
        const auto result=Stairs::Smooth(faces);
        state["stairs"]=true;state["steps"]=result.steps;state["winding"]=result.winding;
    }
    catch(const std::exception&) { /* Not a staircase. */ }
    return state;
}
Json SmoothStairs(bool surfaces)
{
    Engine();
    const auto candidates=StairCandidates(surfaces);
    if(candidates.empty())throw std::runtime_error("Select a staircase brush first.");
    std::vector<std::vector<Design::Face>> faces;for(auto a:candidates)faces.push_back(StairBrushFaces(a));
    const auto result=Stairs::Smooth(faces);
    std::vector<Address> owners;for(auto i:result.brushes)owners.push_back(candidates[i]);
    if(!pasteHookReady || insertionText)throw std::runtime_error("Native brush insertion is unavailable or busy.");
    // One paste per ramp: each is its own volume, centred on its points so
    // Location plus each relative vertex lands back on the world vertex.
    struct Pending { std::string text; Json prepared; };
    std::vector<Pending> pending;
    for(const auto& ramp:result.ramps)
    {
        Design::Solid solid=ramp.solid;Vector center{};size_t count=0;
        for(const auto& f:solid.faces)for(const auto& p:f){for(int a=0;a<3;++a)center[a]+=p[a];++count;}
        for(auto& c:center)c=static_cast<float>(std::round(c/static_cast<double>(count)));
        for(auto& f:solid.faces)for(auto& p:f)for(int a=0;a<3;++a)p[a]-=center[a];
        auto definition=Design::SolidDefinition({solid});
        // The blockout brush text, made a blocking volume: a volume builds no
        // BSP, so there is no CSG operation, and traces with no extent (shots,
        // footstep and sight checks) pass through to the visible steps.
        auto& actor=definition.at("actors").at(0);
        auto text=actor.at("text").get<std::string>();
        auto replace=[&](const std::string& from,const std::string& to)
        {
            const auto at=text.find(from);
            if(at==std::string::npos)throw std::runtime_error("Unexpected brush text.");
            text.replace(at,from.size(),to);
        };
        replace("Class=Engine.Brush","Class=Engine.BlockingVolume");
        replace("CsgOper=CSG_Add\n","bBlockZeroExtentTraces=False\n");
        actor["text"]=text;actor["class"]="Engine.BlockingVolume";
        const auto prefix=StairRampPrefix(candidates[ramp.brush])+Id().substr(0,6)+"_";
        auto prepared=PreparePlacement(definition,{center,{}},prefix,LevelPath(),{});
        pending.push_back({ConvertText(prepared.at("t3d").get<std::string>(),CP_UTF8,CP_ACP),prepared});
    }
    const auto before=SelectedIdentities();
    const auto old=StairRamps(owners);
    Json ramps=Json::array();
    struct Scope{~Scope(){insertionText=nullptr;}} scope;
    try
    {
        DetachedCollisionHash hash;
        Transaction transaction("Smooth staircase");
        if(!old.empty())DeleteStairRamps(old);
        for(size_t r=0;r<pending.size();++r)
        {
            insertionText=pending[r].text.c_str();Select(Json::array());Call(Engine(),0x26c,reinterpret_cast<void*>(Level()),0);insertionText=nullptr;
            const auto& item=pending[r].prepared.at("actors").at(0);
            auto a=Find(LevelPath()+"."+item.at("name").get<std::string>(),true);
            if(!a || !IsA(a,"BlockingVolume"))throw std::runtime_error("Could not create the staircase ramp.");
            Modify(a);
            Write(Field(a,"PrePivot"),std::array<float,3>{});
            SetPosition(a,item.at("position").get<Vector>());
            Write(Field(a,"Rotation"),Rotation{});Call(a,0x44);
            auto model=Read<Address>(a+0x238),polys=model?Read<Address>(model+0x50):0;
            // The polygon importer drops a polygon that fails its checks; a
            // ramp missing one would leak.
            if(!polys || Read<int>(polys+0x2c)!=static_cast<int>(result.ramps[r].solid.faces.size()))
                throw std::runtime_error("The staircase ramp's polygons were not accepted by the editor.");
            // UEditorEngine::csgPrepMovingBrush (0x11087ef0, the step BRUSH
            // ADDVOLUME ends with): rebuilds the volume's own BSP, which is
            // what its collision tests against.
            reinterpret_cast<void(__thiscall*)(void*,void*)>(0x11087ef0)(reinterpret_cast<void*>(Engine()),reinterpret_cast<void*>(a));
            if(Read<int>(model+0x58)<=0)throw std::runtime_error("The editor could not build the staircase ramp's collision.");
            if(Property(a,"Group"))DesignSetGroup(a,"StairRamps");
            ramps.push_back(Identity(a));
        }
        Select(before);
        transaction.Commit();
    }
    catch(...){insertionText=nullptr;try{Select(before);}catch(...){}throw;}
    Redraw();
    return {{"ramps",ramps},{"replaced",old.size()},{"steps",result.steps},{"winding",result.winding},{"brushes",StairIdentities(owners)}};
}
size_t RemoveStairSmoothing(bool surfaces)
{
    Engine();
    const auto ramps=StairRamps(StairCandidates(surfaces));
    if(ramps.empty())throw std::runtime_error("The selected staircase has no smoothing ramp.");
    const auto before=SelectedIdentities();
    try
    {
        DetachedCollisionHash hash;
        Transaction transaction("Remove staircase smoothing");
        DeleteStairRamps(ramps);
        Select(before);
        transaction.Commit();
    }
    catch(...){try{Select(before);}catch(...){}throw;}
    Redraw();
    return ramps.size();
}
