// Included inside Workflow::Editor; shares the verified native bridge helpers.
namespace
{
    Address MagicResolve(const Json& identity)
    {
        auto object=Find(identity.at("path"),false);
        if(!object || Path(Read<Address>(object+0x24))!=identity.at("class").get<std::string>())return 0;
        auto owner=object;std::set<Address> seen;
        while(owner && !IsA(owner,"Actor") && seen.insert(owner).second)
        {
            auto parent=Read<Address>(owner+0x18);if(!parent)return 0;
            bool referenced=false;for(const auto& ref:ReferencesOf(parent,false))if(ref.target==owner)referenced=true;
            if(!referenced)return 0;owner=parent;
        }
        if(!owner)return 0;auto live=LiveActors();if(std::find(live.begin(),live.end(),owner)==live.end() || owner==live[0] || (live.size()>1 && owner==live[1]) || IsA(owner,"Camera"))return 0;
        return object;
    }
    std::vector<Address> MagicFields(Address type)
    {
        std::vector<Address> out;std::set<Address> seen;
        for(Address p=Read<Address>(type+0x30);p;p=Read<Address>(p+0x2c))
        {
            if(seen.size()>4096 || !seen.insert(p).second)throw std::runtime_error("Invalid field chain.");
            if(IsA(p,"Property"))out.push_back(p);
        }
        return out;
    }
    Json MagicSchema(Address p,int depth=0,bool dimension=true)
    {
        if(depth>12)throw std::runtime_error("Property nesting is too deep.");
        auto kind=NameOf(Read<Address>(p+0x24));
        Json result={{"kind",kind}};
        int dim=Read<unsigned short>(p+0x30);
        if(dimension && dim>1)return {{"kind","FixedArray"},{"dimension",dim},{"inner",MagicSchema(p,depth+1,false)}};
        if(kind=="ArrayProperty")result["inner"]=MagicSchema(Read<Address>(p+0x64),depth+1);
        else if(kind=="StructProperty")
        {
            result["fields"]=Json::object();
            for(auto f:MagicFields(Read<Address>(p+0x64)))result["fields"][NameOf(f)]=MagicSchema(f,depth+1);
        }
        else if(kind=="BoolProperty")result["choices"]=Json::array({"False","True"});
        else if(kind=="ByteProperty")
        {
            result["choices"]=Json::array();auto e=Read<Address>(p+0x64);
            if(e)for(auto at:Array(e+0x30))result["choices"].push_back(Name(Read<int>(at)));
        }
        else if(kind=="ObjectProperty" || kind=="ClassProperty")result["type"]=Path(Read<Address>(p+(kind=="ClassProperty"?0x68:0x64)));
        else if(kind!="FloatProperty" && kind!="IntProperty" && kind!="NameProperty" && kind!="StrProperty")throw std::runtime_error("Unsupported property type: "+kind);
        return result;
    }
    Json MagicValue(Address p,Address at,int depth=0,bool dimension=true)
    {
        static thread_local size_t remaining=0;if(depth==0)remaining=100000;if(!remaining--)throw std::runtime_error("Property exceeds the 100,000-value authoring limit.");
        if(depth>12)throw std::runtime_error("Property nesting is too deep.");
        int dim=Read<unsigned short>(p+0x30),stride=Read<unsigned short>(p+0x32);
        auto kind=NameOf(Read<Address>(p+0x24));
        if(dimension && dim>1)
        {
            if(dim>4096 || !stride)throw std::runtime_error("Invalid fixed array.");
            Json value=Json::array();for(int i=0;i<dim;++i)value.push_back(MagicValue(p,at+i*stride,depth+1,false));return value;
        }
        if(kind=="ArrayProperty")
        {
            auto inner=Read<Address>(p+0x64);int size=Read<unsigned short>(inner+0x32);
            if(!size || Read<int>(at+4)>4096)throw std::runtime_error("Array is too large for authoring.");
            Json value=Json::array();for(auto item:Array(at,size))value.push_back(MagicValue(inner,item,depth+1));return value;
        }
        if(kind=="StructProperty")
        {
            Json value=Json::object();for(auto f:MagicFields(Read<Address>(p+0x64)))value[NameOf(f)]=MagicValue(f,at+Read<int>(f+0x3c),depth+1);return value;
        }
        if(kind=="StrProperty")
        {
            if(Read<int>(at+4)>16384)throw std::runtime_error("String exceeds the editable text limit.");
            return Json(String(Read<Address>(at),16384)).dump(-1,' ',false,Json::error_handler_t::replace);
        }
        // Never export entire structs/arrays into the engine's unbounded char*
        // API. Leaves have a bounded size; strings have the limit above.
        std::vector<char> buffer(131072,0);
        Call(p,0x90,buffer.data(),reinterpret_cast<void*>(at),static_cast<void*>(nullptr),0u);
        return ConvertText(NormalizeExportNames(buffer.data()),CP_ACP,CP_UTF8);
    }
    bool MagicEditable(Address p,Address actor=0)
    {
        auto flags=Read<unsigned>(p+0x34);
        auto name=NameOf(p);
        if(actor && IsA(actor,"Actor") && (name=="Location" || name=="Rotation"))return true;
        if(actor && IsA(actor,"Mover") && (name=="KeyPos" || name=="KeyRot" || name=="NumKeys"))return true;
        return (flags&1) && !(flags&(2|0x2000|0x20000));
    }
    void MagicCheck(const Json& snapshot)
    {
        if(snapshot.at("level").get<uintptr_t>()!=LevelIdentity() || snapshot.at("generation").get<unsigned>()!=MapGeneration())throw std::runtime_error("The map changed. Refresh the workbench.");
        auto a=MagicResolve(snapshot.at("actor"));if(!a)throw std::runtime_error("This actor or component was deleted. Refresh the workbench.");
        // Value comparison also catches edits outside the transaction observer.
        auto fresh=InspectActor(snapshot.at("actor"));
        if(fresh.at("values")!=snapshot.at("values"))throw std::runtime_error("This actor changed outside the workbench. Refresh before editing.");
    }
    void MagicImport(Address p,Address at,const Json& value)
    {
        if(Read<unsigned short>(p+0x30)>1)
        {
            const int stride=Read<unsigned short>(p+0x32);
            for(size_t i=0;i<value.size();++i)
            {
                auto text=ConvertText(Magic::Text(value[i]),CP_UTF8,CP_ACP);
                auto end=Call<const char*>(p,0x94,text.c_str(),reinterpret_cast<void*>(at+i*stride),0u);
                if(!end || *end)throw std::runtime_error("The editor rejected the property value.");
            }
            return;
        }
        if(IsA(p,"StrProperty"))
        {
            // ImportText with flags=0 tokenizes whitespace. Assign the decoded
            // FString through the native assignment used by RestoreView instead.
            auto text=ConvertText(Json::parse(value.get<std::string>()).get<std::string>(),CP_UTF8,CP_ACP);
            reinterpret_cast<void*(__thiscall*)(void*,const char*)>(0x10e03770)(reinterpret_cast<void*>(at),text.c_str());
            return;
        }
        auto text=ConvertText(Magic::Text(value),CP_UTF8,CP_ACP);
        auto end=Call<const char*>(p,0x94,text.c_str(),reinterpret_cast<void*>(at),0u);
        if(!end || *end)throw std::runtime_error("The editor rejected the property value.");
    }
    std::string MagicTag()
    {
        std::set<std::string> used;for(const auto& slot:TagSlots())used.insert(Fold(slot.value));
        for(int i=1;i<1000000;++i){auto tag="Magic_"+std::to_string(i);if(!used.count(Fold(tag)))return tag;}
        throw std::runtime_error("Could not allocate an unused event Tag.");
    }
    void MagicSet(Address actor,const std::string& key,const Json& value)
    {
        auto p=Property(actor,key);if(!p || !MagicEditable(p,actor))throw std::runtime_error("Property is not editable: "+key);
        Magic::Validate(MagicSchema(p),value);
        try{MagicImport(p,actor+Read<int>(p+0x3c),value);}catch(const std::exception& e){throw std::runtime_error(key+": "+e.what());}
    }
    void MagicReferences(const Json& schema,const Json& value)
    {
        auto kind=schema.at("kind").get<std::string>();
        if(value.is_array()){for(const auto& v:value)MagicReferences(schema.at("inner"),v);return;}
        if(value.is_object()){for(auto it=value.begin();it!=value.end();++it)MagicReferences(schema.at("fields").at(it.key()),it.value());return;}
        if(kind!="ObjectProperty" && kind!="ClassProperty")return;auto text=value.get<std::string>();if(Fold(text)=="none")return;
        auto first=text.find('\''),last=text.rfind('\'');
        if(first==std::string::npos || last!=text.size()-1 || first==last || text.find('\'',first+1)!=last)throw std::runtime_error("Choose a valid object reference from the asset picker.");
        auto object=Find(text.substr(first+1,last-first-1));if(!object)throw std::runtime_error("Referenced asset or actor is not loaded.");
        auto wanted=schema.at("type").get<std::string>();bool compatible=false;
        if(kind=="ObjectProperty")compatible=IsA(object,wanted);
        else if(IsA(object,"Class")){std::set<Address> seen;for(auto c=object;c && seen.insert(c).second && seen.size()<256;c=Read<Address>(c+0x28))if(Path(c)==wanted)compatible=true;}
        if(!compatible)throw std::runtime_error("Reference has an incompatible class.");
    }
    void MagicLink(Address event,Address target,bool trigger,int group)
    {
        if(!IsA(event,"SBase.SMagicEvent"))throw std::runtime_error("Select an SMagicEvent first.");
        auto receiver=trigger?event:target;auto tag=NameField(receiver,"Tag");
        if(Fold(tag)=="none" || tag.empty()){tag=MagicTag();MagicSet(receiver,"Tag",tag);}
        if(trigger)MagicSet(target,"Event",tag);
        else
        {
            auto p=Property(event,"Groups");auto schema=MagicSchema(p);auto groups=MagicValue(p,event+Read<int>(p+0x3c));
            if(groups.empty())groups.push_back(Magic::Default(schema.at("inner")));
            if(group<0 || group>=static_cast<int>(groups.size()))throw std::runtime_error("Select a valid event group.");
            auto action=Magic::Default(schema.at("inner").at("fields").at("EventGroup").at("inner"));action["Event"]=tag;
            groups[group]["EventGroup"].push_back(action);MagicSet(event,"Groups",groups);
        }
    }
}
Json EventClasses()
{
    Engine();Json out=Json::array();
    for(auto at:Array(0x11697B70))
    {
        auto c=Read<Address>(at);if(!c || !IsA(c,"Class"))continue;
        bool actor=false,brush=false;std::set<Address> visited;
        for(auto base=c;base && visited.insert(base).second && visited.size()<256;base=Read<Address>(base+0x28))
        {auto name=NameOf(base);if(name=="Actor")actor=true;if(name=="Brush")brush=true;}
        // StaticAllocateObject at 0x10fada50 rejects CLASS_Abstract at +0x8c.
        if(actor && !(Read<unsigned>(c+0x8c)&1) && NameOf(c)!="Camera" && NameOf(c)!="LevelInfo")out.push_back({{"class",Path(c)},{"category",Magic::Category(Path(c))},{"brush",brush}});
    }
    std::sort(out.begin(),out.end(),[](const Json& a,const Json& b){return a.at("class")<b.at("class");});return out;
}
void PlayLevel()
{
    Engine();auto frame=Read<Address>(0x1165df84);auto window=frame?reinterpret_cast<HWND>(Read<Address>(frame+4)):nullptr;
    // This global owns the bottom bar; menu commands must reach its root frame.
    window=window?GetAncestor(window,GA_ROOT):nullptr;
    if(!window || !IsWindow(window))throw std::runtime_error("The main editor window is unavailable.");
    // Native menu resource: &Play Level (Ctrl+P), command 0x9c4f.
    SendMessage(window,WM_COMMAND,40015,0);
}
// Native menu resource: Build > Rebuild Geometry Only, command 40160.
void BuildGeometry()
{
    Engine();auto frame=Read<Address>(0x1165df84);auto window=frame?reinterpret_cast<HWND>(Read<Address>(frame+4)):nullptr;
    window=window?GetAncestor(window,GA_ROOT):nullptr;
    if(!window || !IsWindow(window))throw std::runtime_error("The main editor window is unavailable.");
    SendMessage(window,WM_COMMAND,40160,0);
}
unsigned long GeometryBuilds() { return BspDiagnostics::BuildCount(); }
Json EventAssets(const std::string& type,bool classes)
{
    Engine();Json out=Json::array();
    for(auto at:Array(0x11697B70))
    {
        auto object=Read<Address>(at);if(!object)continue;bool match=false;
        if(classes && IsA(object,"Class"))
        {
            std::set<Address> seen;for(auto c=object;c && seen.insert(c).second && seen.size()<256;c=Read<Address>(c+0x28))if(Path(c)==type || NameOf(c)==type)match=true;
        }
        else if(!classes && IsA(object,type))match=true;
        if(match && (!classes || !(Read<unsigned>(object+0x8c)&1)))
        {
            Json entry={{"path",Path(object)},{"class",Path(Read<Address>(object+0x24))}};
            if(!classes && IsA(object,"StaticMesh"))entry["bounds"]=Read<std::array<float,6>>(object+0x28);
            out.push_back(std::move(entry));
        }
        if(out.size()>50000)throw std::runtime_error("Too many matching assets. Use a more specific type.");
    }
    std::sort(out.begin(),out.end(),[](const Json& a,const Json& b){return a.at("path")<b.at("path");});return out;
}
Json CreateEventComponent(const Json& snapshot,const std::string& type)
{
    MagicCheck(snapshot);auto actor=MagicResolve(snapshot.at("actor"));
    if(!IsA(actor,"Emitter"))throw std::runtime_error("Select an emitter actor first.");
    auto classes=EventAssets("Engine.ParticleEmitter",true);
    if(std::none_of(classes.begin(),classes.end(),[&](const Json& c){return c.at("path")==type;}) || type=="Engine.ParticleEmitter")throw std::runtime_error("Choose a concrete particle emitter class.");
    auto cls=Find(type);Transaction transaction("Add particle emitter component");Modify(actor);
    using Construct=Address(__cdecl*)(Address,Address,int,unsigned,Address,Address,Address);
    auto object=reinterpret_cast<Construct>(0x10fadf80)(cls,actor,0,1,0,Read<Address>(0x115befb0),0);
    if(!object)throw std::runtime_error("Could not create particle emitter component.");Modify(object);
    auto values=snapshot.at("values").at("Emitters");values.push_back(type+"'"+Path(object)+"'");MagicSet(actor,"Emitters",values);Call(actor,0x44);transaction.Commit();Redraw();return Identity(object);
}
void CaptureMoverKey(const Json& snapshot,int key)
{
    MagicCheck(snapshot);auto actor=MagicResolve(snapshot.at("actor"));if(!IsA(actor,"Mover"))throw std::runtime_error("Select a mover first.");
    auto values=snapshot.at("values");auto positions=values.at("KeyPos"),rotations=values.at("KeyRot");
    if(key<=0 || key>=static_cast<int>(positions.size()))throw std::runtime_error("Select KeyPos[1] or a later key. Key 0 is the mover's base pose.");
    int current=std::stoi(values.at("KeyNum").get<std::string>());if(current<0 || current>=static_cast<int>(positions.size()))throw std::runtime_error("Invalid current mover key.");
    auto live=LiveActors();auto builder=live.at(1);auto target=Position(builder),base=Position(actor);auto targetRot=RotationOf(builder),baseRot=RotationOf(actor);
    const char* xyz[]={"X","Y","Z"};const char* angles[]={"Pitch","Yaw","Roll"};
    for(int i=0;i<3;++i)
    {
        base[i]-=Magic::Number(positions[current].at(xyz[i]));positions[key][xyz[i]]=std::to_string(target[i]-base[i]);
        baseRot[i]-=std::stoi(rotations[current].at(angles[i]).get<std::string>());rotations[key][angles[i]]=std::to_string(targetRot[i]-baseRot[i]);
    }
    EditActor(snapshot,{{"KeyPos",positions},{"KeyRot",rotations},{"NumKeys",std::to_string(std::max(key+1,std::stoi(values.at("NumKeys").get<std::string>())))}});
}
Json InspectActor(const Json& identity)
{
    auto actor=MagicResolve(identity);if(!actor)throw std::runtime_error("Actor or owned component is no longer available.");
    Json result={{"actor",Identity(actor)},{"level",LevelIdentity()},{"generation",MapGeneration()},{"values",Json::object()},{"schema",Json::object()},{"unavailable",Json::object()}};
    for(auto p:Properties(Read<Address>(actor+0x24)))
    {
        if(!MagicEditable(p,actor))continue;auto name=NameOf(p);
        try
        {
            auto schema=MagicSchema(p);schema["category"]=Name(Read<int>(p+0x38));
            result["values"][name]=MagicValue(p,actor+Read<int>(p+0x3c));result["schema"][name]=std::move(schema);
        }
        catch(const std::exception& e){result["unavailable"][name]=e.what();}
    }
    return result;
}
Json ExportEventJson(const Json& identity)
{
    auto actor=MagicResolve(identity);
    if(!actor || !IsA(actor,"SBase.SMagicEvent"))throw std::runtime_error("Open an SMagicEvent before exporting JSON.");
    return Magic::ExportEvent(InspectActor(identity),Actors(),LevelPath());
}
void ImportEventJson(const Json& snapshot,const Json& document)
{
    auto actor=MagicResolve(snapshot.at("actor"));
    if(!actor || !IsA(actor,"SBase.SMagicEvent"))throw std::runtime_error("Open an SMagicEvent before importing JSON.");
    // Use the live schema, never schema supplied by the JSON file.
    MagicCheck(snapshot);
    auto changes=Magic::ImportEventChanges(InspectActor(snapshot.at("actor")),document);
    EditActor(snapshot,changes);
}
void ValidateActorChanges(const Json& snapshot,const Json& changes)
{
    auto actor=MagicResolve(snapshot.at("actor"));
    if(!changes.is_object() || changes.empty())return;
    for(auto it=changes.begin();it!=changes.end();++it)
    {
        if(!snapshot.at("schema").contains(it.key()))throw std::runtime_error("Property is not exposed for editing.");
        Magic::Validate(snapshot.at("schema").at(it.key()),it.value());
        MagicReferences(snapshot.at("schema").at(it.key()),it.value());
        if(it.key()=="Groups")for(const auto& group:it.value())
        {
            if(Fold(group.at("Sequence"))=="true" && group.at("EventGroup").empty())throw std::runtime_error("A sequential group needs at least one action.");
            if(Magic::Number(group.at("Repeat"))<-1)throw std::runtime_error("Repeat: -1 disables, 0 allows unlimited activations, positive values limit cycles.");
            for(const auto& action:group.at("EventGroup"))Magic::Seconds(Magic::Number(action.at("Delay")));
        }
        if(it.key()=="NumKeys" && (Magic::Number(it.value())<1 || Magic::Number(it.value())>snapshot.at("values").at("KeyPos").size()))throw std::runtime_error("Mover key count is out of range.");
        if(it.key()=="KeyNum" && IsA(actor,"Mover") && Magic::Number(it.value())>=snapshot.at("values").at("KeyPos").size())throw std::runtime_error("The selected mover key is outside its key array.");
    }
}
void EditActor(const Json& snapshot,const Json& changes)
{
    MagicCheck(snapshot);ValidateActorChanges(snapshot,changes);
    if(!changes.is_object() || changes.empty())return;
    auto actor=MagicResolve(snapshot.at("actor"));
    Transaction transaction("Edit SMagicEvent workbench");Modify(actor);auto owner=actor;while(owner && !IsA(owner,"Actor"))owner=Read<Address>(owner+0x18);if(owner && owner!=actor)Modify(owner);
    for(auto it=changes.begin();it!=changes.end();++it)MagicSet(actor,it.key(),it.value());
    Call(actor,0x44);if(owner && owner!=actor)Call(owner,0x44);transaction.Commit();Redraw();
}
void LinkEventActor(const Json& event,const Json& target,bool trigger,int group)
{
    MagicCheck(event);MagicCheck(target);auto e=ResolveIdentity(event.at("actor")),t=ResolveIdentity(target.at("actor"));
    Transaction transaction(trigger?"Attach event trigger":"Add event action");Modify(e);if(t!=e)Modify(t);
    MagicLink(e,t,trigger,group);Call(e,0x44);if(t!=e)Call(t,0x44);transaction.Commit();Redraw();
}
Json CreateEventActor(const std::string& type,const Json& event,bool trigger,const std::string& geometry,int group)
{
    if(!event.is_null() && !event.empty())MagicCheck(event);
    auto classes=EventClasses();auto cls=std::find_if(classes.begin(),classes.end(),[&](const Json& c){return c.at("class")==type;});
    if(cls==classes.end())throw std::runtime_error("Choose a loaded actor class.");
    bool brush=cls->at("brush").get<bool>();std::string shape;
    if(brush)
    {
        if(geometry=="box")shape=Magic::BoxPolygons();
        else if(geometry=="builder")
        {
            auto live=LiveActors();if(live.size()<2)throw std::runtime_error("Builder brush is unavailable.");
            auto model=Read<Address>(live[1]+0x238),polys=model?Read<Address>(model+0x50):0;
            if(!polys || Read<int>(polys+0x2c)<=0)throw std::runtime_error("The builder brush is empty. Build a brush shape first.");
            TextOutput output;reinterpret_cast<int(__cdecl*)(void*,void*,void*,const char*,int)>(0x10ff6a40)(reinterpret_cast<void*>(polys),nullptr,&output,"t3d",0);
            if(output.overflow || output.text.find("Begin PolyList")==std::string::npos)throw std::runtime_error("Could not export builder brush polygons.");shape=NormalizeExportNames(output.text);
        }
        else throw std::runtime_error("Choose Current builder brush or Box volume for a volume class.");
    }
    else if(geometry!="point")throw std::runtime_error("Choose point placement for this class.");
    if(!pasteHookReady)throw std::runtime_error("Native actor insertion is unavailable.");
    auto before=SelectedIdentities();auto position=BuilderPose().position;auto tag=MagicTag();auto name=type.substr(type.find_last_of('.')+1)+"_"+tag;
    auto baseName=name;for(int suffix=2;Find(LevelPath()+"."+name);++suffix)name=baseName+"_"+std::to_string(suffix);
    auto text="Begin Map\r\nBegin Actor Class="+type+" Name="+name+"\r\nTag="+tag+"\r\nLocation="+VectorText(position)+"\r\n";
    if(type=="Engine.Mover")text+="InitialState=TriggerOpenTimed\r\n";
    if(geometry=="builder")
    {
        auto builder=LiveActors().at(1);
        for(const auto* key:{"Rotation","DrawScale","DrawScale3D","PrePivot"})if(auto p=Property(builder,key))text+=std::string(key)+"="+Magic::Text(MagicValue(p,builder+Read<int>(p+0x3c)))+"\r\n";
    }
    if(brush)text+="Begin Brush Name="+name+"Model\r\n"+shape+"End Brush\r\nBrush=Model'"+LevelPath()+"."+name+"Model'\r\n";
    text+="End Actor\r\nEnd Map\r\n";
    struct Scope{Scope(const char* text){if(insertionText)throw std::runtime_error("Actor insertion is busy.");insertionText=text;}~Scope(){insertionText=nullptr;}} scope(text.c_str());
    Address actor=0;
    try
    {
        Transaction transaction("Create event actor and connections");Select(Json::array());Call(Engine(),0x26c,reinterpret_cast<void*>(Level()),0);
        actor=Find(LevelPath()+"."+name,true);if(!actor || SelectedIdentities().size()!=1)throw std::runtime_error("Native actor creation failed.");
        Modify(actor);SetPosition(actor,position);
        if(!event.is_null() && !event.empty()){auto e=ResolveIdentity(event.at("actor"));Modify(e);MagicLink(e,actor,trigger,group);Call(e,0x44);}
        Call(actor,0x44);transaction.Commit();
    }
    catch(...){Select(before);throw;}
    Select(Json::array({Identity(actor)}));Redraw();return Identity(actor);
}
