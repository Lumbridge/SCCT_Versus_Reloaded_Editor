// Player count rules (ObjectivePlayersModel.h): compiles SMatchSizeRules into the
// open map's package and keeps the one actor of it, whose tables list the objectives
// and movers with a player rule and the missions' objectives to win by match size. A
// map's actor of an earlier version (SPlayerCountRules, SObjectivePlayers) is read as
// the current tables and replaced on the next change. Included by WorkflowEditor.cpp
// after CharacterSkinsNative.inl, whose map class compiler it uses.
namespace
{
    Address ObjectivePlayersClass() { return Find(LevelPath()+"."+ObjectivePlayers::ClassName); }
    // One var() static array of the class: its first element, checked against the
    // size the script declares.
    Address PlayersArray(Address actor,const char* name,int stride,int count=ObjectivePlayers::Capacity)
    {
        auto p=Property(actor,name);
        if(!p)throw std::runtime_error(std::string("The map's ")+NameOf(Read<Address>(actor+0x24))+" class has no "+name+".");
        // UProperty +0x30: ArrayDim in the low word, ElementSize in the high word.
        const auto dims=Read<unsigned>(p+0x30);
        if(static_cast<int>(dims&0xffff)!=count || static_cast<int>(dims>>16)!=stride)
            throw std::runtime_error(std::string("Unexpected ")+NameOf(Read<Address>(actor+0x24))+"."+name+" layout.");
        return actor+Read<int>(p+0x3c);
    }
    bool ObjectivePlayersCompiled(Address c)
    {
        if(!c || !(Read<unsigned>(c+0x8c)&2))return false;
        for(const char* name:{"Subject","MinPlayers","MaxPlayers","OpenInRange","PlayLevelPlayers","WinMission","WinTarget"})if(!StructField(c,name))return false;
        return true;
    }
    std::vector<Address> ObjectivePlayersActors() { return ActorsOf(ObjectivePlayersClass()); }
    // Actors of earlier versions, newest first.
    std::vector<Address> LegacyPlayersActors()
    {
        std::vector<Address> out;
        for(const auto* name:ObjectivePlayers::LegacyClassNames)
            for(auto a:ActorsOf(Find(LevelPath()+"."+name)))out.push_back(a);
        return out;
    }
    // The rule table as entries in use. An entry whose actor was deleted from the map
    // reads as empty. Version 1 has Objective for Subject and no OpenInRange.
    struct PlayersEntry { Address subject; int minimum,maximum; bool open; };
    std::vector<PlayersEntry> ReadPlayersTable(Address actor)
    {
        std::vector<PlayersEntry> out;
        const bool first=!Property(actor,"Subject");
        auto subjects=PlayersArray(actor,first?"Objective":"Subject",4),minimum=PlayersArray(actor,"MinPlayers",1),maximum=PlayersArray(actor,"MaxPlayers",1);
        const Address open=Property(actor,"OpenInRange")?PlayersArray(actor,"OpenInRange",1):0;
        auto live=LiveActors();
        for(int i=0;i<ObjectivePlayers::Capacity;++i)
        {
            auto subject=Read<Address>(subjects+i*4);
            if(!subject || std::find(live.begin(),live.end(),subject)==live.end())continue;
            out.push_back({subject,Read<unsigned char>(minimum+i),Read<unsigned char>(maximum+i),open && Read<unsigned char>(open+i)!=0});
        }
        return out;
    }
    void WritePlayersTable(Address actor,const std::vector<PlayersEntry>& entries)
    {
        if(entries.size()>static_cast<size_t>(ObjectivePlayers::Capacity))
            throw std::runtime_error("A map can give "+std::to_string(ObjectivePlayers::Capacity)+" objectives and movers a player count.");
        auto subjects=PlayersArray(actor,"Subject",4),minimum=PlayersArray(actor,"MinPlayers",1),maximum=PlayersArray(actor,"MaxPlayers",1),open=PlayersArray(actor,"OpenInRange",1);
        for(int i=0;i<ObjectivePlayers::Capacity;++i)
        {
            const bool used=i<static_cast<int>(entries.size());
            Write(subjects+i*4,used?entries[i].subject:Address{0});
            Write(minimum+i,static_cast<unsigned char>(used?entries[i].minimum:0));
            Write(maximum+i,static_cast<unsigned char>(used?entries[i].maximum:0));
            Write(open+i,static_cast<unsigned char>(used && entries[i].open?1:0));
        }
    }
    // The objectives-to-win table: a mission and its number for each match size. Earlier
    // versions have none.
    struct TargetEntry { Address mission; std::array<int,ObjectivePlayers::TeamSizes> sizes; };
    std::vector<TargetEntry> ReadTargetTable(Address actor)
    {
        std::vector<TargetEntry> out;
        if(!Property(actor,"WinMission"))return out;
        auto missions=PlayersArray(actor,"WinMission",4,ObjectivePlayers::TargetMissions);
        auto targets=PlayersArray(actor,"WinTarget",1,ObjectivePlayers::TargetMissions*ObjectivePlayers::TeamSizes);
        auto live=LiveActors();
        for(int i=0;i<ObjectivePlayers::TargetMissions;++i)
        {
            auto mission=Read<Address>(missions+i*4);
            if(!mission || std::find(live.begin(),live.end(),mission)==live.end())continue;
            TargetEntry entry{mission,{}};
            for(int k=0;k<ObjectivePlayers::TeamSizes;++k)entry.sizes[k]=Read<unsigned char>(targets+i*ObjectivePlayers::TeamSizes+k);
            if(!ObjectivePlayers::EmptyTargets(entry.sizes))out.push_back(entry);
        }
        return out;
    }
    void WriteTargetTable(Address actor,const std::vector<TargetEntry>& entries)
    {
        if(entries.size()>static_cast<size_t>(ObjectivePlayers::TargetMissions))
            throw std::runtime_error("A map can give "+std::to_string(ObjectivePlayers::TargetMissions)+" missions objectives to win by match size.");
        auto missions=PlayersArray(actor,"WinMission",4,ObjectivePlayers::TargetMissions);
        auto targets=PlayersArray(actor,"WinTarget",1,ObjectivePlayers::TargetMissions*ObjectivePlayers::TeamSizes);
        for(int i=0;i<ObjectivePlayers::TargetMissions;++i)
        {
            const bool used=i<static_cast<int>(entries.size());
            Write(missions+i*4,used?entries[i].mission:Address{0});
            for(int k=0;k<ObjectivePlayers::TeamSizes;++k)
                Write(targets+i*ObjectivePlayers::TeamSizes+k,static_cast<unsigned char>(used?entries[i].sizes[k]:0));
        }
    }
    int PlayLevelPlayers(Address actor)
    {
        auto p=Property(actor,"PlayLevelPlayers");
        return p?Read<unsigned char>(actor+Read<int>(p+0x3c)):0;
    }
    void DeletePlayersActors(const std::vector<Address>& doomed)
    {
        if(doomed.empty())return;
        auto previous=SelectedIdentities();
        Json identities=Json::array();for(auto a:doomed)identities.push_back(Identity(a));
        Select(identities);
        if(!Exec("ACTOR DELETE"))throw std::runtime_error("The editor refused to delete the player count actor.");
        Json remaining=Json::array();
        for(const auto& kept:previous)
            if(std::none_of(doomed.begin(),doomed.end(),[&](Address a){return kept.at("path")==Path(a);}))remaining.push_back(kept);
        Select(remaining);
    }
    // The actor whose tables the map uses: the current one, else the newest earlier one.
    Address RulesActor()
    {
        auto actors=ObjectivePlayersActors();
        if(!actors.empty())return actors[0];
        auto legacy=LegacyPlayersActors();
        return legacy.empty()?0:legacy[0];
    }
    // The current actor, compiled and placed when asked for and not there yet, with an
    // earlier version's tables moved into it and that actor removed.
    Address ObjectivePlayersActor(bool create)
    {
        auto actors=ObjectivePlayersActors();
        if(!actors.empty() || !create)return actors.empty()?0:actors[0];
        std::vector<PlayersEntry> carried;std::vector<TargetEntry> carriedTargets;int carriedPlayLevel=0;
        const auto legacy=LegacyPlayersActors();
        if(!legacy.empty()){carried=ReadPlayersTable(legacy[0]);carriedTargets=ReadTargetTable(legacy[0]);carriedPlayLevel=PlayLevelPlayers(legacy[0]);}
        CompileMapClass(ObjectivePlayers::ClassName,ObjectivePlayers::Script(),"Info",ObjectivePlayersCompiled);
        // ACTOR ADD selects the new actor; what the author right-clicked stays selected,
        // so its menu still offers what it did.
        const auto previous=SelectedIdentities();
        if(!Exec(std::string("ACTOR ADD CLASS=")+ObjectivePlayers::ClassName))throw std::runtime_error("The editor refused to place the player count actor.");
        Select(previous);
        actors=ObjectivePlayersActors();
        if(actors.empty())throw std::runtime_error("The editor did not place the player count actor.");
        // Named SMatchSizeRules0 rather than from ACTOR ADD's running count, as the
        // Character Skins actor is (UObject::Rename, thiscall name, new outer).
        for(int n=0;n<100;++n)
        {
            const auto name=std::string(ObjectivePlayers::ClassName)+std::to_string(n);
            if(NameOf(actors[0])==name)break;
            if(Find(LevelPath()+"."+name))continue;
            reinterpret_cast<void(__thiscall*)(void*,const char*,void*)>(0x10faef00)(reinterpret_cast<void*>(actors[0]),name.c_str(),nullptr);
            break;
        }
        if(!legacy.empty())
        {
            Transaction transaction("Player counts: move to SMatchSizeRules");
            Modify(actors[0]);
            WritePlayersTable(actors[0],carried);
            WriteTargetTable(actors[0],carriedTargets);
            Write(Field(actors[0],"PlayLevelPlayers"),static_cast<unsigned char>(carriedPlayLevel));
            Call(actors[0],0x44);
            transaction.Commit();
            DeletePlayersActors(legacy);
        }
        return actors[0];
    }
    // A map with no rules, no objectives to win and no Play Level count carries no actor.
    void RemoveObjectivePlayersIfUnused(Address actor)
    {
        if(!actor || !ReadPlayersTable(actor).empty() || !ReadTargetTable(actor).empty() || PlayLevelPlayers(actor))return;
        DeletePlayersActors({actor});
    }
    bool PlayersSubject(Address a) { return a && (IsA(a,"SBase.SObjective") || IsA(a,"Mover")); }
}

Json ObjectivePlayerRules()
{
    const Address actor=RulesActor();
    Json rules=Json::array(),targets=Json::array();
    if(actor)
    {
        for(const auto& entry:ReadPlayersTable(actor))
            rules.push_back({{"path",Path(entry.subject)},{"min",entry.minimum},{"max",entry.maximum},{"open",entry.open},
                             {"mover",IsA(entry.subject,"Mover")}});
        for(const auto& entry:ReadTargetTable(actor))
            targets.push_back({{"path",Path(entry.mission)},{"sizes",entry.sizes}});
    }
    return {{"placed",actor!=0},{"legacy",actor && ObjectivePlayersActors().empty()},{"extra",ObjectivePlayersActors().size()+LegacyPlayersActors().size()>1},
            {"compiled",ObjectivePlayersCompiled(ObjectivePlayersClass())},
            {"actor",actor?Json(Identity(actor)):Json()},{"playLevel",actor?PlayLevelPlayers(actor):0},{"rules",rules},{"targets",targets}};
}

Json SetObjectivePlayers(const Json& subject,int minimum,int maximum,bool open)
{
    ObjectivePlayers::Rule rule{minimum,maximum};
    ObjectivePlayers::Check(rule);
    auto target=ResolveIdentity(subject);
    if(!PlayersSubject(target))throw std::runtime_error("Choose an objective, a zone's mission or a mover first.");
    if(!IsA(target,"Mover"))open=false;
    // An earlier version's tables move to the current actor even when this rule is cleared.
    const bool keep=!ObjectivePlayers::Empty(rule) || !LegacyPlayersActors().empty();
    auto actor=ObjectivePlayersActor(keep);
    if(!actor)return ObjectivePlayerRules();
    auto entries=ReadPlayersTable(actor);
    entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const PlayersEntry& e){return e.subject==target;}),entries.end());
    if(!ObjectivePlayers::Empty(rule))entries.push_back({target,minimum,maximum,open});
    {
        Transaction transaction("Player count");
        Modify(actor);
        WritePlayersTable(actor,entries);
        Call(actor,0x44);
        transaction.Commit();
    }
    RemoveObjectivePlayersIfUnused(actor);
    Redraw();
    return ObjectivePlayerRules();
}

Json SetMissionTargets(const Json& mission,const Json& sizes)
{
    auto target=ResolveIdentity(mission);
    if(!target || !IsA(target,"SBase.SMission"))throw std::runtime_error("Choose a mission first.");
    if(!sizes.is_array() || sizes.size()!=static_cast<size_t>(ObjectivePlayers::TeamSizes))
        throw std::runtime_error("Give the objectives to win for 1v1 to "+ObjectivePlayers::SizeLabel(ObjectivePlayers::TeamSizes)+".");
    std::array<int,ObjectivePlayers::TeamSizes> values{};
    for(int k=0;k<ObjectivePlayers::TeamSizes;++k)
    {
        const int value=sizes[k].is_number_integer()?sizes[k].get<int>():-1;
        if(value<0 || value>ObjectivePlayers::MostTargets)throw std::runtime_error("Objectives to win run from 1 to "+std::to_string(ObjectivePlayers::MostTargets)+", or 0 for the map's own.");
        values[k]=value;
    }
    const bool keep=!ObjectivePlayers::EmptyTargets(values) || !LegacyPlayersActors().empty();
    auto actor=ObjectivePlayersActor(keep);
    if(!actor)return ObjectivePlayerRules();
    auto entries=ReadTargetTable(actor);
    entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const TargetEntry& e){return e.mission==target;}),entries.end());
    if(!ObjectivePlayers::EmptyTargets(values))entries.push_back({target,values});
    {
        Transaction transaction("Objectives to win by match size");
        Modify(actor);
        WriteTargetTable(actor,entries);
        Call(actor,0x44);
        transaction.Commit();
    }
    RemoveObjectivePlayersIfUnused(actor);
    Redraw();
    return ObjectivePlayerRules();
}

Json ObjectivePlayersSubject(uintptr_t object)
{
    if(!object)return Json();
    auto live=LiveActors();
    if(std::find(live.begin(),live.end(),static_cast<Address>(object))==live.end() || !PlayersSubject(object))return Json();
    auto identity=Identity(object);
    identity["mover"]=IsA(object,"Mover");
    identity["mission"]=IsA(object,"SBase.SMission");
    return identity;
}

Json SetObjectivePlayLevelPlayers(int players)
{
    if(players<0 || players>ObjectivePlayers::MostPlayers)throw std::runtime_error("Play Level can count 1 to "+std::to_string(ObjectivePlayers::MostPlayers)+" players, or every objective.");
    auto actor=ObjectivePlayersActor(players>0 || !LegacyPlayersActors().empty());
    if(!actor)return ObjectivePlayerRules();
    {
        Transaction transaction("Player count");
        Modify(actor);
        Write(Field(actor,"PlayLevelPlayers"),static_cast<unsigned char>(players));
        Call(actor,0x44);
        transaction.Commit();
    }
    RemoveObjectivePlayersIfUnused(actor);
    Redraw();
    return ObjectivePlayerRules();
}
