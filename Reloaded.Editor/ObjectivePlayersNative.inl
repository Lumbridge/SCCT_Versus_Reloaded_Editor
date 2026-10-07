// Objective player counts (ObjectivePlayersModel.h): compiles SObjectivePlayers into
// the open map's package and keeps the one actor of it, whose table lists the
// objectives with a player rule. Included by WorkflowEditor.cpp after
// CharacterSkinsNative.inl, whose map class compiler it uses.
namespace
{
    Address ObjectivePlayersClass() { return Find(LevelPath()+"."+ObjectivePlayers::ClassName); }
    // One var() static array of the class: its first element and stride, checked
    // against the size the script declares.
    struct PlayersColumn { Address first=0; int stride=0; };
    PlayersColumn PlayersArray(Address actor,const char* name,int stride)
    {
        auto p=Property(actor,name);
        if(!p)throw std::runtime_error(std::string("The map's ")+ObjectivePlayers::ClassName+" class has no "+name+".");
        // UProperty +0x30: ArrayDim in the low word, ElementSize in the high word.
        const auto dims=Read<unsigned>(p+0x30);
        if(static_cast<int>(dims&0xffff)!=ObjectivePlayers::Capacity || static_cast<int>(dims>>16)!=stride)
            throw std::runtime_error(std::string("Unexpected ")+ObjectivePlayers::ClassName+"."+name+" layout.");
        return {actor+Read<int>(p+0x3c),stride};
    }
    bool ObjectivePlayersCompiled(Address c)
    {
        if(!c || !(Read<unsigned>(c+0x8c)&2))return false;
        for(const char* name:{"Objective","MinPlayers","MaxPlayers","PlayLevelPlayers"})if(!StructField(c,name))return false;
        return true;
    }
    std::vector<Address> ObjectivePlayersActors() { return ActorsOf(ObjectivePlayersClass()); }
    // The table as entries in use: objective, minimum, maximum. An entry whose objective
    // was deleted from the map reads as empty.
    struct PlayersEntry { Address objective; int minimum,maximum; };
    std::vector<PlayersEntry> ReadPlayersTable(Address actor)
    {
        std::vector<PlayersEntry> out;
        auto objectives=PlayersArray(actor,"Objective",4),minimum=PlayersArray(actor,"MinPlayers",1),maximum=PlayersArray(actor,"MaxPlayers",1);
        auto live=LiveActors();
        for(int i=0;i<ObjectivePlayers::Capacity;++i)
        {
            auto objective=Read<Address>(objectives.first+i*4);
            if(!objective || std::find(live.begin(),live.end(),objective)==live.end())continue;
            out.push_back({objective,Read<unsigned char>(minimum.first+i),Read<unsigned char>(maximum.first+i)});
        }
        return out;
    }
    void WritePlayersTable(Address actor,const std::vector<PlayersEntry>& entries)
    {
        if(entries.size()>static_cast<size_t>(ObjectivePlayers::Capacity))
            throw std::runtime_error("A map can give "+std::to_string(ObjectivePlayers::Capacity)+" objectives a player count.");
        auto objectives=PlayersArray(actor,"Objective",4),minimum=PlayersArray(actor,"MinPlayers",1),maximum=PlayersArray(actor,"MaxPlayers",1);
        for(int i=0;i<ObjectivePlayers::Capacity;++i)
        {
            const bool used=i<static_cast<int>(entries.size());
            Write(objectives.first+i*4,used?entries[i].objective:Address{0});
            Write(minimum.first+i,static_cast<unsigned char>(used?entries[i].minimum:0));
            Write(maximum.first+i,static_cast<unsigned char>(used?entries[i].maximum:0));
        }
    }
    int PlayLevelPlayers(Address actor)
    {
        auto p=Property(actor,"PlayLevelPlayers");
        return p?Read<unsigned char>(actor+Read<int>(p+0x3c)):0;
    }
    // The actor, compiled and placed when asked for and not there yet.
    Address ObjectivePlayersActor(bool create)
    {
        auto actors=ObjectivePlayersActors();
        if(!actors.empty() || !create)return actors.empty()?0:actors[0];
        CompileMapClass(ObjectivePlayers::ClassName,ObjectivePlayers::Script(),"Info",ObjectivePlayersCompiled);
        // ACTOR ADD selects the new actor; the objective the author right-clicked stays
        // selected, so its menu still offers what it did.
        const auto previous=SelectedIdentities();
        if(!Exec(std::string("ACTOR ADD CLASS=")+ObjectivePlayers::ClassName))throw std::runtime_error("The editor refused to place the objective player count actor.");
        Select(previous);
        actors=ObjectivePlayersActors();
        if(actors.empty())throw std::runtime_error("The editor did not place the objective player count actor.");
        // Named SObjectivePlayers0 rather than from ACTOR ADD's running count, as the
        // Character Skins actor is (UObject::Rename, thiscall name, new outer).
        for(int n=0;n<100;++n)
        {
            const auto name=std::string(ObjectivePlayers::ClassName)+std::to_string(n);
            if(NameOf(actors[0])==name)break;
            if(Find(LevelPath()+"."+name))continue;
            reinterpret_cast<void(__thiscall*)(void*,const char*,void*)>(0x10faef00)(reinterpret_cast<void*>(actors[0]),name.c_str(),nullptr);
            break;
        }
        return actors[0];
    }
    // A map with no rules and no Play Level count carries no actor.
    void RemoveObjectivePlayersIfUnused(Address actor)
    {
        if(!actor || !ReadPlayersTable(actor).empty() || PlayLevelPlayers(actor))return;
        auto previous=SelectedIdentities();
        Select(Json::array({Identity(actor)}));
        if(!Exec("ACTOR DELETE"))throw std::runtime_error("The editor refused to delete the objective player count actor.");
        Json remaining=Json::array();
        for(const auto& kept:previous)if(kept.at("path")!=Path(actor))remaining.push_back(kept);
        Select(remaining);
    }
}

Json ObjectivePlayerRules()
{
    auto actors=ObjectivePlayersActors();
    const Address actor=actors.empty()?0:actors[0];
    Json rules=Json::array();
    if(actor)
        for(const auto& entry:ReadPlayersTable(actor))
            rules.push_back({{"path",Path(entry.objective)},{"min",entry.minimum},{"max",entry.maximum}});
    return {{"placed",actor!=0},{"extra",actors.size()>1},{"compiled",ObjectivePlayersCompiled(ObjectivePlayersClass())},
            {"actor",actor?Json(Identity(actor)):Json()},{"playLevel",actor?PlayLevelPlayers(actor):0},{"rules",rules}};
}

Json SetObjectivePlayers(const Json& objective,int minimum,int maximum)
{
    ObjectivePlayers::Rule rule{minimum,maximum};
    ObjectivePlayers::Check(rule);
    auto target=ResolveIdentity(objective);
    if(!target || !IsA(target,"SBase.SObjective"))throw std::runtime_error("Choose an objective or a zone's mission first.");
    auto actor=ObjectivePlayersActor(!ObjectivePlayers::Empty(rule));
    if(!actor)return ObjectivePlayerRules();
    auto entries=ReadPlayersTable(actor);
    entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const PlayersEntry& e){return e.objective==target;}),entries.end());
    if(!ObjectivePlayers::Empty(rule))entries.push_back({target,minimum,maximum});
    {
        Transaction transaction("Objective player count");
        Modify(actor);
        WritePlayersTable(actor,entries);
        Call(actor,0x44);
        transaction.Commit();
    }
    RemoveObjectivePlayersIfUnused(actor);
    Redraw();
    return ObjectivePlayerRules();
}

Json SetObjectivePlayLevelPlayers(int players)
{
    if(players<0 || players>ObjectivePlayers::MostPlayers)throw std::runtime_error("Play Level can count 1 to "+std::to_string(ObjectivePlayers::MostPlayers)+" players, or every objective.");
    auto actor=ObjectivePlayersActor(players>0);
    if(!actor)return ObjectivePlayerRules();
    {
        Transaction transaction("Objective player count");
        Modify(actor);
        Write(Field(actor,"PlayLevelPlayers"),static_cast<unsigned char>(players));
        Call(actor,0x44);
        transaction.Commit();
    }
    RemoveObjectivePlayersIfUnused(actor);
    Redraw();
    return ObjectivePlayerRules();
}
