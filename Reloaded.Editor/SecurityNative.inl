// Native operations for security devices: creation with properties, wiring
// detectors to alarms, and alarm outputs. Included after MapDesignNative.inl
// inside Workflow::Editor; MagicSet, MagicTag and Transaction come from
// MagicEventNative.inl.
namespace
{
    std::set<std::string> UsedTags()
    {
        std::set<std::string> used;
        for(const auto& slot:TagSlots())used.insert(Fold(slot.value));
        return used;
    }
    // Every actor's Tag is a name; a None Tag is given one so it can be wired.
    std::string EnsureTag(Address actor,const std::string& stem)
    {
        auto tag=NameField(actor,"Tag");
        if(!tag.empty() && Fold(tag)!="none")return tag;
        tag=Security::NextTag(stem,UsedTags());
        MagicSet(actor,"Tag",tag);
        return tag;
    }
    Json NameList(Address actor,const char* property)
    {
        auto p=Property(actor,property);
        if(!p)return Json::array();
        auto value=MagicValue(p,actor+Read<int>(p+0x3c));
        return value.is_array()?value:Json::array();
    }
    double NumberProperty(Address actor,const char* property,double fallback)
    {
        auto p=Property(actor,property);
        if(!p)return fallback;
        if(IsA(p,"FloatProperty"))return Read<float>(actor+Read<int>(p+0x3c));
        if(IsA(p,"IntProperty"))return Read<int>(actor+Read<int>(p+0x3c));
        if(IsA(p,"ByteProperty"))return Read<unsigned char>(actor+Read<int>(p+0x3c));
        return fallback;
    }
}
// The security devices in the map with what the plan needs to draw and wire
// them: kind, pose, Tag and Event, reach, and an alarm's outputs.
Json SecurityActors()
{
    Json result=Json::array();
    for(auto actor:LiveActors())
    {
        const auto type=Path(Read<Address>(actor+0x24));
        auto kind=Security::KindOf(type);
        const bool mover=IsA(actor,"Mover") || IsA(actor,"SBase.SLift");
        const bool light=IsA(actor,"SBase.STriggerLight");
        const bool magic=IsA(actor,"SBase.SMagicEvent");
        if(kind.empty() && !mover && !light && !magic)continue;
        if(kind.empty())kind=mover?"Mover":light?"Light":"Magic event";
        Json item=Identity(actor);
        item["kind"]=kind;
        item["position"]=Position(actor);
        item["rotation"]=RotationOf(actor);
        item["locked"]=DesignBool(actor,"bLockLocation");
        item["hidden"]=(Read<unsigned>(actor+0x2f4)&0x10)!=0;
        item["tag"]=NameField(actor,"Tag");
        item["event"]=NameField(actor,"Event");
        item["selected"]=(Read<unsigned>(actor+0x2f4)&0x40)!=0;
        if(kind=="Laser")item["length"]=NumberProperty(actor,"LaserLength",300);
        if(kind=="Camera"){item["cone"]=NumberProperty(actor,"VisibilityConeAngle",40);item["reach"]=NumberProperty(actor,"VisibilityMaxDistance",2000);}
        if(kind=="Presence detector" || kind=="Mine")item["radius"]=NumberProperty(actor,kind=="Mine"?"fInteractionRadius":"CollisionRadius",90);
        if(kind=="Alarm")
        {
            item["events"]=NameList(actor,"Events");
            item["name"]=Json::parse(Property(actor,"AlarmName")?MagicValue(Property(actor,"AlarmName"),actor+Read<int>(Property(actor,"AlarmName")+0x3c)).get<std::string>():"\"\"",nullptr,false);
            item["duration"]=NumberProperty(actor,"Duration",60);
        }
        if(kind=="Motion sensor")
        {
            item["volumes"]=Json::array();
            for(const auto& reference:NameList(actor,"Volumes"))
            {
                auto refs=References(reference.get<std::string>());
                if(!refs.empty())item["volumes"].push_back(refs.front().path);
            }
        }
        result.push_back(item);
    }
    return result;
}
// Every light in the map with what the plan draws and edits. Reach is stored
// in steps of 25 units.
Json Lights()
{
    Json result=Json::array();
    for(auto actor:LiveActors())
    {
        if(!IsA(actor,"Light"))continue;
        Json item=Identity(actor);
        item["position"]=Position(actor);
        item["rotation"]=RotationOf(actor);
        item["locked"]=DesignBool(actor,"bLockLocation");
        item["hidden"]=(Read<unsigned>(actor+0x2f4)&0x10)!=0;
        item["radius"]=NumberProperty(actor,"LightRadius",64)*25.0;
        item["brightness"]=NumberProperty(actor,"LightBrightness",64);
        item["hue"]=NumberProperty(actor,"LightHue",0);
        item["saturation"]=NumberProperty(actor,"LightSaturation",255);
        for(const std::string name:{"LightType","LightEffect"})
        {
            std::string value;
            try
            {
                if(auto p=Property(actor,name.c_str()))
                {
                    auto v=MagicValue(p,actor+Read<int>(p+0x3c));
                    value=v.is_string()?v.get<std::string>():v.dump();
                }
            }
            catch(const std::exception&) { /* Unreadable enums are left blank. */ }
            item[name=="LightType"?"type":"effect"]=value;
        }
        item["switchable"]=IsA(actor,"SBase.STriggerLight");
        item["tag"]=NameField(actor,"Tag");
        item["selected"]=(Read<unsigned>(actor+0x2f4)&0x40)!=0;
        result.push_back(item);
    }
    return result;
}
// Player starts, the mission, its objectives and their devices, with what the
// plan draws and links.
Json ObjectiveActors()
{
    Json result=Json::array();
    for(auto actor:LiveActors())
    {
        std::string kind;
        if(IsA(actor,"SBase.SMission"))kind="Mission";
        else if(IsA(actor,"SBase.SObjective"))kind="Objective";
        else if(IsA(actor,"SBase.SComputerObjectiveTrigger"))kind="Computer terminal";
        else if(IsA(actor,"SBase.SBombTargetObjectiveTrigger"))kind="Bomb target";
        else if(IsA(actor,"SBase.SObjectiveTrigger"))kind="Objective trigger";
        else if(IsA(actor,"SBase.SFlagDropZone"))kind="Drop zone";
        else if(IsA(actor,"SBase.SFlag"))kind="Flag";
        else if(IsA(actor,"PlayerStart"))kind="Player start";
        else continue;
        Json item=Identity(actor);
        item["kind"]=kind;
        item["position"]=Position(actor);
        item["rotation"]=RotationOf(actor);
        item["locked"]=DesignBool(actor,"bLockLocation");
        item["hidden"]=(Read<unsigned>(actor+0x2f4)&0x10)!=0;
        item["tag"]=NameField(actor,"Tag");
        item["event"]=NameField(actor,"Event");
        item["selected"]=(Read<unsigned>(actor+0x2f4)&0x40)!=0;
        std::string name;
        try{if(auto p=Property(actor,"ObjectiveName"))name=Json::parse(MagicValue(p,actor+Read<int>(p+0x3c)).get<std::string>(),nullptr,false).get<std::string>();}
        catch(const std::exception&){}
        item["name"]=name;
        if(kind=="Player start")item["team"]=StartTeam(actor);
        if(kind=="Mission")item["objectives"]=NameList(actor,"Objectives");
        if(kind=="Objective")item["triggers"]=NameList(actor,"Triggers");
        result.push_back(item);
    }
    return result;
}
// What an alarm locks while it sounds: movers and objective triggers among the
// targets join MoversToLock and ObjectiveTriggersToLock.
Json AddAlarmLocks(const Json& alarm,const Json& targets)
{
    auto a=ResolveIdentity(alarm);
    if(!a || !IsA(a,"SBase.SAlarm"))throw std::runtime_error("Choose an SAlarm first.");
    std::vector<Address> movers,triggers;
    for(const auto& target:targets)
    {
        auto t=ResolveIdentity(target);
        if(!t)continue;
        if(IsA(t,"Mover"))movers.push_back(t);
        else if(IsA(t,"SBase.SObjectiveTrigger"))triggers.push_back(t);
    }
    if(movers.empty() && triggers.empty())throw std::runtime_error("Select the movers (doors, lifts) or objective triggers the alarm should lock.");
    auto reference=[&](Address actor){return NameOf(Read<Address>(actor+0x24))+"'"+Path(actor)+"'";};
    Transaction transaction("Alarm locks");
    Modify(a);
    auto moverList=NameList(a,"MoversToLock");
    for(auto m:movers)
    {
        bool present=false;
        for(const auto& entry:moverList)if(entry.is_object() && entry.value("MoverToLock",std::string()).find(Path(m))!=std::string::npos)present=true;
        if(!present)moverList.push_back({{"ActionBeforeLock","ActionBeforeLock_None"},{"MoverToLock",reference(m)}});
    }
    auto triggerList=NameList(a,"ObjectiveTriggersToLock");
    for(auto t:triggers)
    {
        bool present=false;
        for(const auto& entry:triggerList)if(entry.is_string() && entry.get<std::string>().find(Path(t))!=std::string::npos)present=true;
        if(!present)triggerList.push_back(reference(t));
    }
    if(!movers.empty())MagicSet(a,"MoversToLock",moverList);
    if(!triggers.empty())MagicSet(a,"ObjectiveTriggersToLock",triggerList);
    Call(a,0x44);
    transaction.Commit();Redraw();
    return {{"movers",movers.size()},{"triggers",triggers.size()}};
}
// A permanent player start for a team. TeamNumber is not an editable property,
// so it goes in through the pasted text, as the temporary playtest start does.
Json CreatePlayerStart(const std::string& type,const std::string& team,const Pose& pose)
{
    Design::CheckVector(pose.position);
    if(!std::regex_match(type,std::regex("[A-Za-z_][A-Za-z0-9_]{0,62}\\.[A-Za-z_][A-Za-z0-9_]{0,62}")))throw std::runtime_error("Choose a PlayerStart class.");
    if(!std::regex_match(team,std::regex("[0-9]{1,3}")))throw std::runtime_error("Invalid team number.");
    if(!pasteHookReady || insertionText)throw std::runtime_error("Native actor insertion is unavailable or busy.");
    const auto stem=type.substr(type.find_last_of('.')+1);
    std::string name=stem+"_Team"+team;
    for(int suffix=2;Find(LevelPath()+"."+name);++suffix)name=stem+"_Team"+team+"_"+std::to_string(suffix);
    auto text="Begin Map\r\nBegin Actor Class="+type+" Name="+name+"\r\nLocation="+VectorText(pose.position)+"\r\nRotation="+RotationText(pose.rotation)+"\r\nTeamNumber="+team+"\r\nEnd Actor\r\nEnd Map\r\n";
    auto before=SelectedIdentities();
    struct Scope{Scope(const char* value){if(insertionText)throw std::runtime_error("Actor insertion is busy.");insertionText=value;}~Scope(){insertionText=nullptr;}} scope(text.c_str());
    Address actor=0;
    try
    {
        Transaction transaction("Add player start");
        Select(Json::array());Call(Engine(),0x26c,reinterpret_cast<void*>(Level()),0);
        actor=Find(LevelPath()+"."+name,true);
        if(!actor || SelectedIdentities().size()!=1)throw std::runtime_error("Could not create the player start.");
        if(!IsA(actor,"PlayerStart"))throw std::runtime_error("That class is not a PlayerStart.");
        Modify(actor);SetPosition(actor,pose.position);Write(Field(actor,"Rotation"),pose.rotation);WriteTeamNumber(actor,team);Call(actor,0x44);
        transaction.Commit();
    }
    catch(...){Select(before);throw;}
    Select(Json::array({Identity(actor)}));Redraw();
    return Identity(actor);
}
// Sets properties on one actor in one Undo step.
Json SetActorProperties(const Json& identity,const Json& properties)
{
    auto actor=ResolveIdentity(identity);
    if(!actor)throw std::runtime_error("The actor is no longer in the map.");
    Transaction transaction("Edit actor");
    Modify(actor);
    if(properties.is_object())for(auto it=properties.begin();it!=properties.end();++it)MagicSet(actor,it.key(),it.value());
    Call(actor,0x44);
    transaction.Commit();
    Redraw();
    return Identity(actor);
}
// Creates a security actor at a pose with the given properties, in one Undo
// step. A Tag is always assigned so the actor can be wired straight away.
Json CreateSecurityActor(const std::string& type,const Pose& pose,const Json& properties)
{
    Design::CheckVector(pose.position);
    auto classes=EventClasses();
    auto cls=std::find_if(classes.begin(),classes.end(),[&](const Json& c){return c.at("class")==type;});
    if(cls==classes.end())throw std::runtime_error("This editor has not loaded "+type+".");
    if(cls->at("brush").get<bool>())throw std::runtime_error("Volume classes are placed through the motion sensor tool.");
    if(!pasteHookReady)throw std::runtime_error("Native actor insertion is unavailable.");
    const auto stem=type.substr(type.find_last_of('.')+1);
    auto tag=Security::NextTag(stem.substr(0,1)=="S"?stem.substr(1):stem,UsedTags());
    auto name=stem+"_"+tag;
    for(int suffix=2;Find(LevelPath()+"."+name);++suffix)name=stem+"_"+tag+"_"+std::to_string(suffix);
    auto text="Begin Map\r\nBegin Actor Class="+type+" Name="+name+"\r\nTag="+tag+"\r\nLocation="+VectorText(pose.position)+"\r\nRotation="+RotationText(pose.rotation)+"\r\nEnd Actor\r\nEnd Map\r\n";
    auto before=SelectedIdentities();
    struct Scope{Scope(const char* value){if(insertionText)throw std::runtime_error("Actor insertion is busy.");insertionText=value;}~Scope(){insertionText=nullptr;}} scope(text.c_str());
    Address actor=0;
    try
    {
        Transaction transaction("Add security device");
        Select(Json::array());Call(Engine(),0x26c,reinterpret_cast<void*>(Level()),0);
        actor=Find(LevelPath()+"."+name,true);
        if(!actor || SelectedIdentities().size()!=1)throw std::runtime_error("Native actor creation failed.");
        Modify(actor);SetPosition(actor,pose.position);Write(Field(actor,"Rotation"),pose.rotation);
        if(properties.is_object())for(auto it=properties.begin();it!=properties.end();++it)MagicSet(actor,it.key(),it.value());
        Call(actor,0x44);
        transaction.Commit();
    }
    catch(...){Select(before);throw;}
    Select(Json::array({Identity(actor)}));Redraw();
    return Identity(actor);
}
// A motion sensor and the box volume it watches, created and linked together.
Json CreateMotionSensor(const Vector& low,const Vector& high,const Json& properties)
{
    Design::CheckVector(low);Design::CheckVector(high);
    for(int axis=0;axis<3;++axis)if(high[axis]-low[axis]<1)throw std::runtime_error("The sensor volume needs positive size.");
    auto classes=EventClasses();
    auto loaded=[&](const std::string& type){return std::any_of(classes.begin(),classes.end(),[&](const Json& c){return c.at("class")==type;});};
    std::string volumeType="Engine.Volume";
    if(!loaded(volumeType))throw std::runtime_error("This editor has not loaded Engine.Volume.");
    if(!loaded("SBase.SVolumetricSensor"))throw std::runtime_error("This editor has not loaded SBase.SVolumetricSensor.");
    if(!pasteHookReady)throw std::runtime_error("Native actor insertion is unavailable.");
    // A box volume centred on the area, as the native Add Volume would make it.
    Vector centre{},half{};
    for(int axis=0;axis<3;++axis){centre[axis]=(low[axis]+high[axis])/2;half[axis]=(high[axis]-low[axis])/2;}
    auto box=Design::Box({-half[0],-half[1],-half[2]},{half[0],half[1],half[2]});
    std::ostringstream shape;
    for(auto& face:box.faces)
    {
        shape<<"Begin Polygon Flags=0\r\n";
        for(auto& v:face)shape<<"Vertex "<<v[0]<<","<<v[1]<<","<<v[2]<<"\r\n";
        shape<<"End Polygon\r\n";
    }
    auto used=UsedTags();
    auto sensorTag=Security::NextTag("Sensor",used);used.insert(Fold(sensorTag));
    auto volumeTag=Security::NextTag("SensorVolume",used);
    std::string sensorName="SVolumetricSensor_"+sensorTag,volumeName="Volume_"+volumeTag;
    for(int suffix=2;Find(LevelPath()+"."+sensorName);++suffix)sensorName="SVolumetricSensor_"+sensorTag+"_"+std::to_string(suffix);
    for(int suffix=2;Find(LevelPath()+"."+volumeName);++suffix)volumeName="Volume_"+volumeTag+"_"+std::to_string(suffix);
    Vector sensorAt=centre;sensorAt[2]=low[2];
    auto text="Begin Map\r\nBegin Actor Class="+volumeType+" Name="+volumeName+"\r\nTag="+volumeTag+"\r\nLocation="+VectorText(centre)+"\r\n"
        +"Begin Brush Name="+volumeName+"Model\r\nBegin PolyList\r\n"+shape.str()+"End PolyList\r\nEnd Brush\r\nBrush=Model'"+LevelPath()+"."+volumeName+"Model'\r\nEnd Actor\r\n"
        +"Begin Actor Class=SBase.SVolumetricSensor Name="+sensorName+"\r\nTag="+sensorTag+"\r\nLocation="+VectorText(sensorAt)+"\r\nEnd Actor\r\nEnd Map\r\n";
    auto before=SelectedIdentities();
    struct Scope{Scope(const char* value){if(insertionText)throw std::runtime_error("Actor insertion is busy.");insertionText=value;}~Scope(){insertionText=nullptr;}} scope(text.c_str());
    Address sensor=0,volume=0;
    try
    {
        Transaction transaction("Add motion sensor and its volume");
        Select(Json::array());Call(Engine(),0x26c,reinterpret_cast<void*>(Level()),0);
        sensor=Find(LevelPath()+"."+sensorName,true);volume=Find(LevelPath()+"."+volumeName,true);
        if(!sensor || !volume || SelectedIdentities().size()!=2)throw std::runtime_error("Native actor creation failed.");
        Modify(volume);SetPosition(volume,centre);Call(volume,0x44);
        Modify(sensor);SetPosition(sensor,sensorAt);
        MagicSet(sensor,"Volumes",Json::array({volumeType.substr(volumeType.find('.')+1)+"'"+Path(volume)+"'"}));
        if(properties.is_object())for(auto it=properties.begin();it!=properties.end();++it)MagicSet(sensor,it.key(),it.value());
        Call(sensor,0x44);
        transaction.Commit();
    }
    catch(...){Select(before);throw;}
    Select(Json::array({Identity(sensor)}));Redraw();
    return {{"sensor",Identity(sensor)},{"volume",Identity(volume)}};
}
// A detector fires its Event; wiring it to an alarm means giving it the
// alarm's Tag as that Event. The alarm gets a Tag if it has none.
// Moves and turns a security actor in one Undo step. A motion sensor's volumes
// travel with it, and extra properties (a laser's length) apply at the same time.
std::vector<Address> SensorVolumes(Address actor);
void FollowNow(const Json& members,const Vector& delta);
Json MoveSecurityActor(const Json& identity,const Pose& pose,const Json& properties,const Json& followers)
{
    Design::CheckVector(pose.position);
    auto actor=ResolveIdentity(identity);
    if(!actor)throw std::runtime_error("The device is no longer in the map.");
    const auto from=Position(actor);
    Vector delta{};
    for(int axis=0;axis<3;++axis)delta[axis]=pose.position[axis]-from[axis];
    const auto volumes=SensorVolumes(actor);
    Transaction transaction("Move security device");
    Modify(actor);
    SetPosition(actor,pose.position);
    Write(Field(actor,"Rotation"),pose.rotation);
    if(properties.is_object())for(auto it=properties.begin();it!=properties.end();++it)MagicSet(actor,it.key(),it.value());
    Call(actor,0x44);
    for(auto volume:volumes)
    {
        Modify(volume);
        auto at=Position(volume);
        for(int axis=0;axis<3;++axis)at[axis]+=delta[axis];
        SetPosition(volume,at);
        Call(volume,0x44);
    }
    // The rest of the device's group, in the same Undo step.
    FollowNow(followers,delta);
    transaction.Commit();
    Redraw();
    return Identity(actor);
}
// The volumes a motion sensor refers to, by actor address.
std::vector<Address> SensorVolumes(Address actor)
{
    std::vector<Address> volumes;
    if(!IsA(actor,"SBase.SVolumetricSensor"))return volumes;
    for(const auto& reference:NameList(actor,"Volumes"))
    {
        auto refs=References(reference.get<std::string>());
        if(refs.empty())continue;
        if(auto volume=Find(refs.front().path,true))volumes.push_back(volume);
    }
    return volumes;
}
// A detector that triggers nothing: its Event is cleared in one Undo step.
void UnwireDetector(const Json& detector)
{
    auto d=ResolveIdentity(detector);
    if(!d)throw std::runtime_error("The detector is no longer in the map.");
    if(!Property(d,"Event"))throw std::runtime_error("This actor has no Event to clear.");
    Transaction transaction("Unwire detector");
    Modify(d);
    MagicSet(d,"Event","None");
    Call(d,0x44);
    transaction.Commit();Redraw();
}
// Deletes a device (and a motion sensor's volumes) through the editor's own
// delete, so Undo brings everything back together.
void DeleteSecurityActor(const Json& identity)
{
    auto actor=ResolveIdentity(identity);
    if(!actor)throw std::runtime_error("The device is no longer in the map.");
    Json doomed=Json::array({Identity(actor)});
    for(auto volume:SensorVolumes(actor))doomed.push_back(Identity(volume));
    auto previous=SelectedIdentities();
    Select(doomed);
    if(!Exec("ACTOR DELETE"))throw std::runtime_error("The editor refused to delete "+identity.at("path").get<std::string>()+".");
    Json remaining=Json::array();
    for(const auto& kept:previous)if(std::none_of(doomed.begin(),doomed.end(),[&](const Json& d){return d.at("path")==kept.at("path");}))remaining.push_back(kept);
    Select(remaining);Redraw();
}
void LinkDetectorToAlarm(const Json& detector,const Json& alarm)
{
    auto d=ResolveIdentity(detector),a=ResolveIdentity(alarm);
    if(!d || !a)throw std::runtime_error("The detector or alarm is no longer in the map.");
    if(!IsA(a,"SBase.SAlarm"))throw std::runtime_error("Wire detectors to an SAlarm.");
    if(!Property(d,"Event"))throw std::runtime_error("This actor has no Event to wire.");
    Transaction transaction("Wire detector to alarm");
    Modify(a);Modify(d);
    auto tag=EnsureTag(a,"Alarm");
    MagicSet(d,"Event",tag);
    Call(d,0x44);Call(a,0x44);
    transaction.Commit();Redraw();
}
// What an alarm sets off: each target's Tag joins the alarm's Events list.
// Targets without a Tag are given one.
void AddAlarmOutputs(const Json& alarm,const Json& targets)
{
    auto a=ResolveIdentity(alarm);
    if(!a || !IsA(a,"SBase.SAlarm"))throw std::runtime_error("Choose an SAlarm first.");
    std::vector<Address> actors;
    for(const auto& target:targets)
    {
        auto t=ResolveIdentity(target);
        if(!t)throw std::runtime_error("A target is no longer in the map.");
        if(t==a)throw std::runtime_error("An alarm cannot trigger itself.");
        if(!Property(t,"Tag"))throw std::runtime_error("A target has no Tag to trigger.");
        actors.push_back(t);
    }
    if(actors.empty())throw std::runtime_error("Select the actors the alarm should trigger.");
    Transaction transaction("Add alarm outputs");
    Modify(a);
    auto events=NameList(a,"Events");
    for(auto t:actors)
    {
        Modify(t);
        auto tag=EnsureTag(t,NameOf(Read<Address>(t+0x24)));
        bool present=false;
        for(const auto& event:events)if(Fold(event.get<std::string>())==Fold(tag))present=true;
        if(!present)events.push_back(tag);
        Call(t,0x44);
    }
    MagicSet(a,"Events",events);
    Call(a,0x44);
    transaction.Commit();Redraw();
}
