// Native reads for the Lighting Budget window (LightingBudgetModel.h). The raw
// fields are the ones the stock editor's own light checks read.
namespace
{
    constexpr size_t kActorRegionZone=0x1ac, kActorRegionZoneNumber=0x1b4;
    constexpr size_t kLightType=0x2d1, kLightEffect=0x2d2, kLightAnimA=0x174, kLightAnimB=0x178;
    constexpr size_t kLightFlagsA=0x2e8, kLightFlagsB=0x2ec;
    constexpr unsigned kZoneLimitedBit=0x200, kDynamicLightBit=0x20000, kStaticBit=0x400000, kInGameBit=0x800000;
    // AActor::GetLightRenderData, refreshed when the light changed; the stock
    // intersection check reads the render centre at +0x1C and radius at +0x34.
    constexpr Address kLightRenderData=0x110b1510;
    // UEditorEngine vtable: TestVisibility(Level, Model, mode, all) regenerates
    // the BSP leaf light lists. Mode 1 lists in-game and dynamic lights, mode 0
    // static ones; geometry and lighting builds leave mode 0 behind.
    constexpr size_t kTestVisibilitySlot=0x218;
    using TestVisibilityFn=void(__thiscall*)(void*,void*,void*,int,int);

    bool budgetFieldsLogged=false;
    // One log line naming the reflected properties at the raw offsets, so a
    // different build of the editor shows up in the log rather than as nonsense.
    void LogBudgetFields(Address light)
    {
        if(budgetFieldsLogged)return;
        budgetFieldsLogged=true;
        std::string text="LightingBudget: light fields";
        for(auto p:Properties(Read<Address>(light+0x24)))
        {
            const int offset=Read<int>(p+0x3c);
            const bool flag=IsA(p,"BoolProperty");
            const unsigned mask=flag?Read<unsigned>(p+0x64):0;
            const bool wanted=(!flag && (offset==int(kLightType) || offset==int(kLightEffect) || offset==int(kLightAnimA) || offset==int(kLightAnimB)))
                || (flag && offset==int(kLightFlagsA) && (mask==kZoneLimitedBit || mask==kDynamicLightBit))
                || (flag && offset==int(kLightFlagsB) && (mask==kStaticBit || mask==kInGameBit));
            if(wanted)
            {
                char where[32];snprintf(where,sizeof(where),"+0x%X",offset);
                text+=" "+NameOf(p)+"@"+where+(flag?"/"+std::to_string(mask):"");
            }
        }
        Logger::log(text);
    }
    std::string BudgetZoneName(Address zone,int number)
    {
        if(!zone || IsA(zone,"LevelInfo") || !IsA(zone,"ZoneInfo"))return "Zone "+std::to_string(number)+" (no ZoneInfo)";
        auto tag=NameField(zone,"Tag");
        if(!tag.empty() && Fold(tag)!="none" && Fold(tag)!=Fold(NameOf(Read<Address>(zone+0x24))))return tag;
        return NameOf(zone);
    }
}
Json LightingBudgetScene()
{
    auto e=Engine();auto level=Level();
    Json lights=Json::array(),leaves=Json::array(),zones=Json::object();
    std::map<Address,int> index;
    std::map<int,Address> zoneActors;
    std::map<Address,bool> lightClasses;
    std::map<std::string,int> strengthFields; // Actor properties: one offset for every class
    for(auto actor:LiveActors())
    {
        // Every actor's PointRegion names its zone actor and number.
        const int zone=Read<unsigned char>(actor+kActorRegionZoneNumber);
        if(auto zoneActor=Read<Address>(actor+kActorRegionZone);zoneActor && !zoneActors.count(zone))zoneActors[zone]=zoneActor;
        const auto cls=Read<Address>(actor+0x24);
        auto known=lightClasses.find(cls);
        if(known==lightClasses.end())known=lightClasses.emplace(cls,IsA(actor,"Light")).first;
        // Any actor can emit light (a mover with a LightType, say); the stock
        // Select Lights and light checks count those too.
        if(!known->second && !Read<unsigned char>(actor+kLightType))continue;
        LogBudgetFields(actor);
        const auto a=Read<unsigned>(actor+kLightFlagsA),b=Read<unsigned>(actor+kLightFlagsB);
        LightingBudget::LightFlags flags;
        flags.type=Read<unsigned char>(actor+kLightType);flags.effect=Read<unsigned char>(actor+kLightEffect);
        flags.staticFlag=(b&kStaticBit)!=0;flags.inGameFlag=(b&kInGameBit)!=0;
        flags.dynamicFlag=(a&kDynamicLightBit)!=0;flags.zoneLimited=(a&kZoneLimitedBit)!=0;
        flags.animA=Read<float>(actor+kLightAnimA);flags.animB=Read<float>(actor+kLightAnimB);
        Json item={{"path",Path(actor)},{"name",NameOf(actor)},{"zone",zone},
            {"type",flags.type},{"effect",flags.effect},{"static",flags.staticFlag},{"inGame",flags.inGameFlag},
            {"dynamic",flags.dynamicFlag},{"zoneLimited",flags.zoneLimited},{"animA",flags.animA},{"animB",flags.animB}};
        // How strong the light is, for the "turn off the weakest" fix.
        for(const char* name:{"LightBrightness","LightRadius"})
        {
            auto& offset=strengthFields[name];
            if(!offset){auto p=Property(actor,name);offset=p?Read<int>(p+0x3c):-1;}
            item[name[5]=='B'?"brightness":"lightRadius"]=offset>0?Read<float>(actor+offset):0.0f;
        }
        if(LightingBudget::CountsInGame(flags))
        {
            // The same render sphere the stock intersection check compares.
            auto render=reinterpret_cast<Address>(reinterpret_cast<void*(__thiscall*)(void*)>(kLightRenderData)(reinterpret_cast<void*>(actor)));
            if(render)
            {
                item["position"]={Read<float>(render+0x1c),Read<float>(render+0x20),Read<float>(render+0x24)};
                item["radius"]=Read<float>(render+0x34);
            }
        }
        index[actor]=static_cast<int>(lights.size());
        lights.push_back(item);
    }
    for(const auto& [number,zone]:zoneActors)zones[std::to_string(number)]=BudgetZoneName(zone,number);

    bool leavesKnown=false;
    auto model=Read<Address>(level+0x13c);
    if(model && Read<int>(model+0x58)>0)
    {
        auto testVisibility=reinterpret_cast<TestVisibilityFn>(Read<Address>(Read<Address>(e)+kTestVisibilitySlot));
        // Restore the build's own lists however the read ends, as the stock check does.
        struct Restore
        {
            TestVisibilityFn fn;Address e,level,model;
            ~Restore(){fn(reinterpret_cast<void*>(e),reinterpret_cast<void*>(level),reinterpret_cast<void*>(model),0,0);}
        } restore{testVisibility,e,level,model};
        testVisibility(reinterpret_cast<void*>(e),reinterpret_cast<void*>(level),reinterpret_cast<void*>(model),1,0);
        const auto table=Array(model+0xc8);
        int leafIndex=0;
        for(auto leaf:Array(model+0xbc))
        {
            const int zone=Read<unsigned short>(leaf),start=Read<short>(leaf+2);
            const int current=leafIndex++;
            if(start<0)continue;
            Json members=Json::array();
            for(size_t i=static_cast<size_t>(start);i<table.size();++i)
            {
                auto light=Read<Address>(table[i]);
                if(!light)break;
                if(auto found=index.find(light);found!=index.end())members.push_back(found->second);
            }
            if(!members.empty())leaves.push_back({{"index",current},{"zone",zone},{"lights",members}});
        }
        leavesKnown=leafIndex>0;
    }
    return {{"lights",lights},{"leaves",leaves},{"zones",zones},{"leavesKnown",leavesKnown},{"map",MapKey()},{"generation",MapGeneration()}};
}
size_t SelectActorPaths(const std::vector<std::string>& paths,bool focus)
{
    auto e=Engine(),level=Level();
    std::set<std::string> wanted;for(const auto& path:paths)wanted.insert(Fold(path));
    size_t selected=0;
    for(auto a:LiveActors())
    {
        const bool on=wanted.count(Fold(Path(a)))!=0;selected+=on;
        reinterpret_cast<void(__thiscall*)(void*,void*,void*,int,int)>(0x10eb9a20)(reinterpret_cast<void*>(e),reinterpret_cast<void*>(level),reinterpret_cast<void*>(a),on?1:0,0);
    }
    if(focus && selected)Exec("CAMERA ALIGN");
    Call(e,0xe4);Redraw();
    return selected;
}
size_t LightingBudgetFix(const std::vector<std::string>& paths,int fix)
{
    Engine();
    std::set<std::string> wanted;for(const auto& path:paths)wanted.insert(Fold(path));
    std::vector<Address> targets;
    for(auto actor:LiveActors())if(wanted.count(Fold(Path(actor))))targets.push_back(actor);
    if(targets.empty())return 0;
    const auto kind=fix==1?LightingBudget::Fix::TurnOff:LightingBudget::Fix::MakeStatic;
    Transaction transaction(kind==LightingBudget::Fix::TurnOff?"Lighting Budget: turn off lights":"Lighting Budget: make lights static");
    for(auto actor:targets)
    {
        const auto a=Read<unsigned>(actor+kLightFlagsA),b=Read<unsigned>(actor+kLightFlagsB);
        LightingBudget::LightFlags flags;
        flags.type=Read<unsigned char>(actor+kLightType);
        flags.staticFlag=(b&kStaticBit)!=0;flags.inGameFlag=(b&kInGameBit)!=0;flags.dynamicFlag=(a&kDynamicLightBit)!=0;
        const auto next=LightingBudget::Applied(flags,kind);
        Modify(actor);
        Write(actor+kLightType,next.type);
        Write(actor+kLightFlagsA,(a&~kDynamicLightBit)|(next.dynamicFlag?kDynamicLightBit:0u));
        Write(actor+kLightFlagsB,(b&~(kStaticBit|kInGameBit))|(next.staticFlag?kStaticBit:0u)|(next.inGameFlag?kInGameBit:0u));
        Call(actor,0x44); // PostEditChange: the light's render data follows.
    }
    transaction.Commit();
    Redraw();
    Logger::log("LightingBudget: "+std::string(kind==LightingBudget::Fix::TurnOff?"turned off ":"made static ")+std::to_string(targets.size())+" light(s)");
    return targets.size();
}
