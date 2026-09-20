// Native operations for stages: the inventory the stage model reads, the plan
// a map already carries, and applying a plan as one map-changes batch in one
// Undo step. Included after SecurityNative.inl inside Workflow::Editor;
// UsedTags, NameList and AuthoringMapKey come from the earlier includes.
namespace
{
    // A quoted text property as plain text, or empty.
    std::string StageText(Address actor,const char* property)
    {
        auto p=Property(actor,property);
        if(!p)return "";
        try{return Stages::Unquoted(MagicValue(p,actor+Read<int>(p+0x3c)).get<std::string>());}
        catch(const std::exception&){return "";}
    }
    // Any other property as the text the workbench shows for it.
    std::string StageValue(Address actor,const char* property)
    {
        auto p=Property(actor,property);
        if(!p)return "";
        try{auto v=MagicValue(p,actor+Read<int>(p+0x3c));return v.is_string()?v.get<std::string>():v.dump();}
        catch(const std::exception&){return "";}
    }
    Json StagePaths(Address actor,const char* property)
    {
        Json paths=Json::array();
        for(const auto& reference:NameList(actor,property))
        {
            if(!reference.is_string())continue;
            auto refs=References(reference.get<std::string>());
            if(!refs.empty())paths.push_back(refs.front().path);
        }
        return paths;
    }
}
// The actors a stage plan can name: the mission, objectives and their
// terminals, movers, switchable lights, sound triggers, alarms, events and
// other triggerable actors, each with what the model needs to wire it.
Json StageActors()
{
    Json result=Json::array();
    std::map<std::string,int> tagCount;
    for(auto actor:LiveActors())if(Property(actor,"Tag"))++tagCount[Fold(NameField(actor,"Tag"))];
    for(auto actor:LiveActors())
    {
        std::string kind;
        if(IsA(actor,"SBase.SMission"))kind="Mission";
        else if(IsA(actor,"SBase.SObjective"))kind="Objective";
        else if(IsA(actor,"SBase.SObjectiveTrigger"))kind="Objective trigger";
        else if(IsA(actor,"Mover"))kind="Mover";
        else if(IsA(actor,"SBase.STriggerLight"))kind="Light";
        else if(IsA(actor,"SoundTrigger") || IsA(actor,"SBase.SAmbientSoundTrigger") || IsA(actor,"MusicTrigger"))kind="Sound";
        else if(IsA(actor,"SBase.SAlarm"))kind="Alarm";
        else if(IsA(actor,"SBase.SMagicEvent"))kind="Magic event";
        else if((IsA(actor,"Triggers") || IsA(actor,"Emitter")) && Property(actor,"Tag"))kind="Other";
        else continue;
        Json item=Identity(actor);
        item["kind"]=kind;
        item["position"]=Position(actor);
        item["tag"]=NameField(actor,"Tag");
        item["event"]=NameField(actor,"Event");
        item["selected"]=(Read<unsigned>(actor+0x2f4)&0x40)!=0;
        item["shared"]=tagCount[Fold(NameField(actor,"Tag"))]>1;
        std::string name;
        if(kind=="Mission" || kind=="Objective")name=StageText(actor,"ObjectiveName");
        else if(kind=="Objective trigger"){name=StageText(actor,"TriggerName");if(name.empty())name=StageText(actor,"sDescriptionText");}
        else if(kind=="Alarm")name=StageText(actor,"AlarmName");
        item["name"]=name;
        if(kind=="Mission")item["objectives"]=StagePaths(actor,"Objectives");
        if(kind=="Objective")item["triggers"]=StagePaths(actor,"Triggers");
        if(kind=="Objective trigger"){item["usable"]=DesignBool(actor,"bInitialyUsable");item["method"]=StageValue(actor,"TriggerMethode");}
        if(kind=="Mover")item["state"]=NameField(actor,"InitialState");
        if(kind=="Magic event")
        {
            auto p=Property(actor,"Groups");
            Json groups=Json::array();
            try{if(p)groups=MagicValue(p,actor+Read<int>(p+0x3c));}catch(const std::exception&){}
            item["groups"]=groups.is_array()?groups:Json::array();
        }
        if(kind=="Sound")item["sound"]=StageValue(actor,"Sound");
        if(kind=="Alarm")
        {
            item["title"]=name;item["merc"]=StageText(actor,"AlarmDescription");item["spy"]=StageText(actor,"AlarmDescriptionSpy");
            item["duration"]=StageValue(actor,"Duration");
        }
        result.push_back(item);
    }
    return result;
}
// The plan the map already carries, derived from its actors.
Json StagePlan(){return Stages::Read(StageActors());}
// What applying a plan would do, without touching the map.
Json PreviewStages(const Json& plan)
{
    auto build=Stages::Plan(StageActors(),UsedTags(),plan);
    return {{"notes",build.notes},{"creates",build.creates},{"updates",build.updates}};
}
// Applies a plan as one map-changes batch: new events, sound triggers and
// announcements are created, objectives and terminals rewired, all in one
// Undo step. Update operations check the live values first, so a map that
// changed since the plan was read is refused rather than half-applied.
Json ApplyStages(const Json& plan)
{
    auto build=Stages::Plan(StageActors(),UsedTags(),plan);
    for(auto& op:build.operations)if(op.at("op")=="update")op["before"]=InspectActor(op.at("actor")).at("values");
    Json result={{"notes",build.notes},{"creates",build.creates},{"updates",build.updates},{"created",Json::array()}};
    if(build.operations.empty())return result;
    auto applied=ApplyMapAuthoring({{"format","scct.map-changes"},{"version",1},{"map",AuthoringMapKey()},{"description","Apply stages"},{"operations",build.operations}});
    result["created"]=applied.at("created");
    Redraw();
    return result;
}
