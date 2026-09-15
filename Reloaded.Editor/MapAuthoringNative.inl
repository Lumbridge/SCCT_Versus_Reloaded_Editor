// Included after MagicEventNative.inl; all mutations run on the editor UI thread.
namespace
{
    std::string AuthoringMapKey()
    {
        auto key=MapKey();return key.empty()?"unsaved:"+std::to_string(LevelIdentity())+":"+std::to_string(MapGeneration()):key;
    }
    bool AuthoringSubclass(Address type,const std::string& wanted)
    {
        std::set<Address> seen;
        for(auto c=type;c && seen.insert(c).second;c=Read<Address>(c+0x28))
            if(Path(c)==wanted || NameOf(c)==wanted)return true;
        return false;
    }
    bool AuthoringAppearance(const std::string& name)
    {return name=="StaticMesh" || name=="DrawType" || name=="DrawScale" || name=="DrawScale3D";}
    Json AuthoringSchema(Address type)
    {
        Json result=Json::object();
        for(auto p:Properties(type))
        {
            const auto name=NameOf(p);
            if(!MagicEditable(p) && !(AuthoringSubclass(type,"Actor") && AuthoringAppearance(name)) && name!="Location" && name!="Rotation" && !(AuthoringSubclass(type,"Mover") && (name=="KeyPos" || name=="KeyRot" || name=="NumKeys")))continue;
            try{result[name]=MagicSchema(p);}catch(const std::exception&){}
        }
        return result;
    }
    struct AuthoringPlan
    {
        Json document,summary=Json::array();
        std::map<std::string,Address> types;
        std::map<std::string,Json> identities,schemas;
    };
    AuthoringPlan PlanAuthoring(const Json& document)
    {
        Authoring::Validate(document,AuthoringMapKey());
        AuthoringPlan plan;plan.document=document;
        const auto classes=EventClasses(),components=EventAssets("Engine.ParticleEmitter",true);
        for(const auto& actor:Actors())if(actor.value("authorable",false))
        {
            const auto path=actor.at("path").get<std::string>();
            plan.identities[path]=actor;plan.types[path]=Find(actor.at("class"));
        }
        for(const auto& op:document.at("operations"))
        {
            const auto kind=op.at("op").get<std::string>();
            if(kind=="create" || kind=="component")
            {
                const auto id=op.at("id").get<std::string>(),type=op.at("class").get<std::string>();
                const auto& choices=kind=="create"?classes:components;const char* key=kind=="create"?"class":"path";
                auto found=std::find_if(choices.begin(),choices.end(),[&](const Json& c){return c.at(key)==type;});
                if(found==choices.end() || type=="Engine.ParticleEmitter")throw std::runtime_error("Load a concrete supported class: "+type);
                if(kind=="create")
                {
                    for(const auto& live:Actors())if(Fold(live.at("name"))==Fold(id) || Fold(live.at("tag"))==Fold(id))throw std::runtime_error("A live actor already uses the new actor ID or Tag: "+id);
                    auto geometry=op.value("geometry",std::string("point"));
                    if(geometry!=(found->at("brush").get<bool>()?"box":"point"))throw std::runtime_error("Use box geometry for brush actors and point for other actors.");
                    if(!op.at("properties").contains("Location"))throw std::runtime_error("Every new actor requires an explicit Location.");
                }
                plan.types[id]=Find(type);plan.schemas[id]=AuthoringSchema(plan.types[id]);
                auto name=id;for(int suffix=2;Find(LevelPath()+"."+name);++suffix)name=id+"_"+std::to_string(suffix);
                plan.identities[id]={{"path",LevelPath()+"."+name},{"class",type}};
            }
            else if(kind=="update")
            {
                auto snapshot=InspectActor(op.at("actor"));
                if(snapshot.at("values")!=op.at("before"))throw std::runtime_error("Actor changed since export: "+op.at("actor").at("path").get<std::string>());
                auto path=op.at("actor").at("path").get<std::string>();
                plan.identities[path]=snapshot.at("actor");plan.types[path]=Find(snapshot.at("actor").at("class"));plan.schemas[path]=snapshot.at("schema");
            }
        }
        auto expect=document.value("expect",Json::object());
        for(const auto& op:document.at("operations"))if(op.at("op")=="update")expect[op.at("actor").at("path").get<std::string>()]=op.at("before");
        auto checkExisting=[&](const std::string& ref)
        {
            if(ref.find('.')==std::string::npos)return; // Creation IDs cannot contain dots.
            if(!expect.contains(ref))throw std::runtime_error("Include exported values in expect for existing object: "+ref);
            auto current=InspectActor(plan.identities.at(ref));
            if(current.at("values")!=expect.at(ref))throw std::runtime_error("Linked actor changed since export: "+ref);
        };
        for(auto& op:plan.document["operations"])
        {
            auto kind=op.at("op").get<std::string>();
            if(kind=="link")
            {
                auto event=op.at("event").get<std::string>(),target=op.at("target").get<std::string>();
                if(!plan.types.count(event) || !plan.types.count(target) || !AuthoringSubclass(plan.types.at(event),"SBase.SMagicEvent") || !AuthoringSubclass(plan.types.at(target),"Actor"))throw std::runtime_error("Link requires an SMagicEvent and an actor target.");
                checkExisting(event);checkExisting(target);
                plan.summary.push_back(kind+" "+event+(op.at("trigger").get<bool>()?" <- ":" -> ")+target+" (group "+std::to_string(op.value("group",0))+", delay "+op.value("delay",std::string("0"))+"s)");continue;
            }
            const auto id=kind=="update"?op.at("actor").at("path").get<std::string>():op.at("id").get<std::string>();
            if(kind=="component")
            {
                auto owner=op.at("owner").get<std::string>();
                if(!plan.types.count(owner) || !AuthoringSubclass(plan.types.at(owner),"Emitter"))throw std::runtime_error("Particle component owner must be an emitter actor.");
                checkExisting(owner);
                for(const auto& other:document.at("operations"))if(other.at("op")!="link" && other.at("properties").contains("Emitters") && ((other.at("op")=="update" && other.at("actor").at("path")==owner) || (other.at("op")=="create" && other.at("id")==owner)))throw std::runtime_error("Do not replace Emitters while adding components to the same owner.");
            }
            plan.summary.push_back(kind+" "+id+" ("+Path(plan.types.at(id))+")"+(kind=="component"?" attached to "+op.at("owner").get<std::string>():std::string{}));
            for(auto it=op["properties"].begin();it!=op["properties"].end();++it)
            {
                if(!plan.schemas.at(id).contains(it.key()))throw std::runtime_error(id+": unsupported property "+it.key());
                const auto& schema=plan.schemas.at(id).at(it.key());
                auto value=Authoring::Resolve(schema,it.value(),[&](const std::string& ref,const Json& field)->Json
                {
                    if(field.at("kind")=="ClassProperty" || !plan.types.count(ref) || !AuthoringSubclass(plan.types.at(ref),field.at("type")))throw std::runtime_error("Missing or incompatible object reference: "+ref);
                    return plan.identities.at(ref).at("class").get<std::string>()+"'"+plan.identities.at(ref).at("path").get<std::string>()+"'";
                });
                // Check external references now; symbolic new references are checked after creation.
                std::function<void(const Json&,const Json&,const Json&)> references=[&](const Json& s,const Json& original,const Json& resolved)
                {
                    if(original.is_object() && original.contains("$ref"))return;
                    if(resolved.is_array()){for(size_t i=0;i<resolved.size();++i)references(s.at("inner"),original[i],resolved[i]);}
                    else if(resolved.is_object()){for(auto f=resolved.begin();f!=resolved.end();++f)references(s.at("fields").at(f.key()),original.at(f.key()),f.value());}
                    else MagicReferences(s,resolved);
                };
                references(schema,it.value(),value);
                if(kind=="update")plan.summary.push_back("  "+it.key()+": "+Magic::Text(op.at("before").at(it.key()))+" -> "+Magic::Text(value));
                else plan.summary.push_back("  "+it.key()+" = "+Magic::Text(value));
            }
        }
        return plan;
    }
}
Json ExportMapAuthoring()
{
    Json result={{"format","scct.map-authoring"},{"version",1},{"map",AuthoringMapKey()},{"level",LevelPath()},
        {"actors",Json::array()},{"classes",Json::array()},{"assets",Json::object()},
        {"changes",{{"format","scct.map-changes"},{"version",1},{"map",AuthoringMapKey()},{"description",""},{"operations",Json::array()}}},
        {"instructions","Read docs/MapAuthoring.md. Coordinates are absolute Unreal units; rotation is pitch/yaw/roll, 65536 units per turn. Property leaves are strings. Copy complete values to update.before. Use {$ref: creationId} for references to new actors/components. The snapshot and geometry are reference information; import only a scct.map-changes document. Geometry, baked lighting and navigation are not rebuilt by import."}};
    Json members=Json::array();
    for(const auto& actor:Actors())
    {
        if(!actor.value("authorable",false))continue;
        members.push_back(actor);
        auto snapshot=InspectActor(actor);snapshot.erase("level");snapshot.erase("generation");
        snapshot["selected"]=actor.at("selected");snapshot["position"]=Position(MagicResolve(actor));
        snapshot["rotation"]=RotationOf(MagicResolve(actor));result["actors"].push_back(std::move(snapshot));
    }
    for(auto cls:EventClasses()){cls["schema"]=AuthoringSchema(Find(cls.at("class")));result["classes"].push_back(std::move(cls));}
    for(const auto& cls:EventAssets("Engine.ParticleEmitter",true))result["classes"].push_back({{"class",cls.at("path")},{"component",true},{"schema",AuthoringSchema(Find(cls.at("path")))}});
    for(const char* type:{"Sound","Material","StaticMesh"})result["assets"][type]=EventAssets(type);
    // Include owned particle settings, not just their object-reference strings.
    std::set<Address> visited;
    for(size_t i=0;i<result["actors"].size();++i)
    {
        auto actor=MagicResolve(result["actors"][i].at("actor"));if(!visited.insert(actor).second)continue;
        for(const auto& ref:ReferencesOf(actor,false))
            if(ref.target && Read<Address>(ref.target+0x18)==actor && IsA(ref.target,"ParticleEmitter") && !visited.count(ref.target))
            {auto component=InspectActor(Identity(ref.target));component.erase("level");component.erase("generation");result["actors"].push_back(std::move(component));}
    }
    auto selection=SelectedIdentities();
    struct Restore{Json selection;~Restore(){try{Select(selection);}catch(...){}}} restore{selection};
    try{Select(members);result["geometryT3d"]=NormalizeExportNames(CopySelectedText());}
    catch(const std::exception& e){result["geometryUnavailable"]=e.what();}
    return result;
}
Json PreviewMapAuthoring(const Json& document)
{
    auto plan=PlanAuthoring(document);
    return {{"changes",plan.summary},{"note","Applies in one Undo step. Save the map to retain changes. Lighting, geometry and navigation are not rebuilt; playtest the result."}};
}
Json ApplyMapAuthoring(const Json& document)
{
    auto plan=PlanAuthoring(document);if(!pasteHookReady || insertionText)throw std::runtime_error("Native actor insertion is unavailable or busy.");
    auto selection=SelectedIdentities();
    struct Restore{Json selection;~Restore(){try{Select(selection);Redraw();}catch(...){}}} restore{selection};
    Transaction transaction("Import map JSON changes");Json created=Json::array();
    std::string text="Begin Map\r\n";size_t count=0;
    for(const auto& op:document.at("operations"))if(op.at("op")=="create")
    {
        auto id=op.at("id").get<std::string>(),type=op.at("class").get<std::string>();
        auto path=plan.identities.at(id).at("path").get<std::string>();auto name=path.substr(path.find_last_of('.')+1);
        text+="Begin Actor Class="+type+" Name="+name+"\r\nTag="+id+"\r\nLocation="+Magic::Text(op.at("properties").at("Location"))+"\r\n";
        if(op.value("geometry",std::string("point"))=="box")text+="Begin Brush Name="+name+"Model\r\n"+Magic::BoxPolygons()+"End Brush\r\nBrush=Model'"+LevelPath()+"."+name+"Model'\r\n";
        text+="End Actor\r\n";++count;
    }
    text+="End Map\r\n";
    if(count)
    {
        struct PasteScope{~PasteScope(){insertionText=nullptr;}} paste;insertionText=text.c_str();
        Select(Json::array());Call(Engine(),0x26c,reinterpret_cast<void*>(Level()),0);
        if(SelectedIdentities().size()!=count)throw std::runtime_error("Native creation returned an unexpected actor count.");
        for(const auto& op:document.at("operations"))if(op.at("op")=="create")
        {
            auto id=op.at("id").get<std::string>();auto actor=MagicResolve(plan.identities.at(id));
            if(!actor)throw std::runtime_error("Native creation failed: "+id);created.push_back(Identity(actor));
        }
    }
    for(const auto& op:document.at("operations"))if(op.at("op")=="component")
    {
        const auto id=op.at("id").get<std::string>();auto owner=MagicResolve(plan.identities.at(op.at("owner").get<std::string>()));
        if(!owner)throw std::runtime_error("Component owner disappeared.");Modify(owner);
        using Construct=Address(__cdecl*)(Address,Address,int,unsigned,Address,Address,Address);
        auto object=reinterpret_cast<Construct>(0x10fadf80)(plan.types.at(id),owner,0,1,0,Read<Address>(0x115befb0),0);
        if(!object)throw std::runtime_error("Particle component creation failed.");Modify(object);plan.identities[id]=Identity(object);
        auto values=InspectActor(Identity(owner)).at("values").at("Emitters");values.push_back(Path(plan.types.at(id))+"'"+Path(object)+"'");MagicSet(owner,"Emitters",values);Call(owner,0x44);created.push_back(Identity(object));
    }
    for(const auto& op:document.at("operations"))if(op.at("op")!="link")
    {
        const auto id=op.at("op")=="update"?op.at("actor").at("path").get<std::string>():op.at("id").get<std::string>();
        auto identity=plan.identities.at(id);auto actor=MagicResolve(identity);Modify(actor);
        auto snapshot=InspectActor(identity);Json changes=Json::object();
        for(auto it=op.at("properties").begin();it!=op.at("properties").end();++it)
            changes[it.key()]=Authoring::Resolve(plan.schemas.at(id).at(it.key()),it.value(),[&](const std::string& ref,const Json&)->Json
            {const auto& target=plan.identities.at(ref);return target.at("class").get<std::string>()+"'"+target.at("path").get<std::string>()+"'";});
        auto ordinary=changes;
        for(auto it=changes.begin();it!=changes.end();++it)if(AuthoringAppearance(it.key()))
        {
            auto p=Property(actor,it.key());if(!p || !IsA(actor,"Actor"))throw std::runtime_error("Appearance needs an actor property.");
            Magic::Validate(MagicSchema(p),it.value());MagicReferences(MagicSchema(p),it.value());ordinary.erase(it.key());
        }
        ValidateActorChanges(snapshot,ordinary);
        for(auto it=changes.begin();it!=changes.end();++it)
            if(AuthoringAppearance(it.key())){auto p=Property(actor,it.key());MagicImport(p,actor+Read<int>(p+0x3c),it.value());}
            else MagicSet(actor,it.key(),it.value());
        if(!IsA(actor,"Actor")){auto owner=Read<Address>(actor+0x18);Modify(owner);Call(owner,0x44);}
        Call(actor,0x44);
    }
    for(const auto& op:document.at("operations"))if(op.at("op")=="link")
    {
        auto event=MagicResolve(plan.identities.at(op.at("event").get<std::string>())),target=MagicResolve(plan.identities.at(op.at("target").get<std::string>()));
        Modify(event);Modify(target);MagicLink(event,target,op.at("trigger"),op.value("group",0));
        if(op.contains("delay")){auto groups=InspectActor(Identity(event)).at("values").at("Groups");groups[op.value("group",0)]["EventGroup"].back()["Delay"]=op.at("delay");MagicSet(event,"Groups",groups);}
        Call(event,0x44);Call(target,0x44);
    }
    transaction.Commit();return {{"created",created},{"operations",document.at("operations").size()}};
}
