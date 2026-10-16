// Conveyor belts (ConveyorModel.h): imports the belt's texture and meshes into the open
// map's package, compiles ConveyorRoller, ConveyorItem and ConveyorBelt into it, and
// places belts. Included by WorkflowEditor.cpp after CharacterSkinsNative.inl, whose
// map class compiler it uses.
namespace
{
    std::string ConveyorPath(const char* name) { return LevelPath()+"."+Conveyor::Group+"."+name; }
    Address ConveyorBeltClass() { return Find(LevelPath()+"."+Conveyor::BeltClass); }
    bool ClassCompiled(Address c) { return c && (Read<unsigned>(c+0x8c)&2); }
    // The version a compiled ConveyorBelt class was made from, or 0.
    int ConveyorClassVersion(Address c)
    {
        if(!ClassCompiled(c))return 0;
        auto p=StructField(c,"Version");auto defaults=Read<Address>(c+0xb0);
        if(!p || !defaults || Read<int>(p+0x3c)+4>Read<int>(c+0xb4))return 0;
        return Read<int>(defaults+Read<int>(p+0x3c));
    }
    bool ConveyorCurrent(Address c)
    {
        if(ConveyorClassVersion(c)!=Conveyor::Version)return false;
        for(const char* name:{"Length","Width","Speed","NextBelt","ItemSpacing","FadeDistance","ItemScale","bCarryPlayers","bShowItems","bShowRollers","bLeftWall","bRightWall","ItemMeshes","ItemOffsets"})
            if(!StructField(c,name))return false;
        return true;
    }
    // A property of a belt's class or its parents. A class compiled in this session
    // has its floats and bools only in its field list, not the editor's cached
    // property chain that Property reads, until the map is loaded again.
    Address BeltProperty(Address actor,const std::string& name)
    {
        std::set<Address> seen;
        for(Address c=Read<Address>(actor+0x24);c && seen.insert(c).second && seen.size()<64;c=Read<Address>(c+0x28))
            if(auto p=StructField(c,name))return p;
        return Property(actor,name);
    }
    Address BeltField(Address actor,const std::string& name)
    {
        auto p=BeltProperty(actor,name);
        if(!p)throw std::runtime_error("The map's ConveyorBelt class has no "+name+".");
        return actor+Read<int>(p+0x3c);
    }
    std::filesystem::path ConveyorTempFile(const std::string& name)
    {
        char folder[MAX_PATH]{};GetTempPathA(MAX_PATH,folder);
        return std::filesystem::path(folder)/("RE_"+name);
    }
    void WriteConveyorFile(const std::filesystem::path& file,const void* data,size_t size)
    {
        std::ofstream out(file,std::ios::binary);out.write(static_cast<const char*>(data),static_cast<std::streamsize>(size));
        if(!out)throw std::runtime_error("Cannot write "+file.string());
    }
    void ImportConveyorTexture(const std::string& package)
    {
        const auto tga=Conveyor::TextureTga();
        const auto file=ConveyorTempFile(std::string(Conveyor::Texture)+".tga");
        WriteConveyorFile(file,tga.data(),tga.size());
        Exec("TEXTURE IMPORT FILE=\""+file.string()+"\" NAME=\""+Conveyor::Texture+"\" PACKAGE=\""+package+"\" GROUP=\""+Conveyor::Group+"\" MIPS=1");
        std::error_code ignored;std::filesystem::remove(file,ignored);
        auto texture=Find(ConveyorPath(Conveyor::Texture));
        if(!texture || !IsA(texture,"Texture"))throw std::runtime_error("The editor did not import the conveyor texture.");
    }
    void ImportConveyorMesh(const std::string& package,const Conveyor::Mesh& mesh)
    {
        const auto text=Conveyor::Ase(mesh);
        const auto file=ConveyorTempFile(mesh.name+".ase");
        WriteConveyorFile(file,text.data(),text.size());
        Exec("STATICMESH IMPORT FILE=\""+file.string()+"\" NAME=\""+mesh.name+"\" PACKAGE=\""+package+"\" GROUP=\""+Conveyor::Group+"\"");
        std::error_code ignored;std::filesystem::remove(file,ignored);
        auto found=Find(ConveyorPath(mesh.name.c_str()));
        if(!found || !IsA(found,"StaticMesh"))throw std::runtime_error("The editor did not import the conveyor mesh "+mesh.name+".");
        const auto expected=Conveyor::Bounds(mesh);
        const auto box=Read<std::array<float,6>>(found+0x28);
        for(int i=0;i<6;++i)if(std::abs(box[i]-expected[i])>0.5)throw std::runtime_error("The conveyor mesh "+mesh.name+" imported with unexpected bounds.");
    }
    // A property value as the editor's text import takes it: numbers and booleans
    // become text, arrays and structs keep their shape.
    Json ImportValue(const Json& value)
    {
        if(value.is_boolean())return value.get<bool>()?"True":"False";
        if(value.is_number())return value.dump();
        if(value.is_array() || value.is_object())
        {
            Json out=value;
            for(auto it=out.begin();it!=out.end();++it)*it=ImportValue(*it);
            return out;
        }
        return value;
    }
    // Loads a stock static mesh by path from Packages\StaticMeshes if need be.
    Address LoadStockMesh(const std::string& path)
    {
        if(auto found=Find(path))return found;
        const auto package=path.substr(0,path.find('.'));
        const auto file=Directory().parent_path().parent_path()/"Packages"/"StaticMeshes"/(package+".usx");
        if(std::filesystem::exists(file))Exec("OBJ LOAD FILE=\""+file.string()+"\"");
        auto found=Find(path);
        return found && IsA(found,"StaticMesh")?found:0;
    }
    Address BeltArray(Address actor,const char* name,int stride)
    {
        auto p=BeltProperty(actor,name);
        if(!p)throw std::runtime_error(std::string("The map's ConveyorBelt class has no ")+name+".");
        const auto dims=Read<unsigned>(p+0x30);
        if(static_cast<int>(dims&0xffff)!=Conveyor::ItemSlots || static_cast<int>(dims>>16)!=stride)
            throw std::runtime_error(std::string("Unexpected ConveyorBelt.")+name+" layout.");
        return actor+Read<int>(p+0x3c);
    }
    // Brings a belt's frame and item offsets in line with its settings. Returns
    // whether anything changed.
    bool SyncBelt(Address actor)
    {
        bool changed=false;
        const auto scale=Conveyor::FrameScale(Read<float>(BeltField(actor,"Length")),Read<float>(BeltField(actor,"Width")));
        const auto at=Field(actor,"DrawScale3D");
        if(Read<std::array<float,3>>(at)!=scale){Write(at,scale);changed=true;}
        if(Read<float>(Field(actor,"DrawScale"))!=1.0f){Write(Field(actor,"DrawScale"),1.0f);changed=true;}
        // The frame with the channels its wall options keep.
        auto bit=[&](const char* name){auto p=BeltProperty(actor,name);return p && (Read<unsigned>(actor+Read<int>(p+0x3c))&Read<unsigned>(p+0x64))!=0;};
        if(auto frame=Find(ConveyorPath(Conveyor::FrameMeshName(bit("bLeftWall"),bit("bRightWall")))))
        {
            const auto slot=Field(actor,"StaticMesh");
            if(Read<Address>(slot)!=frame){Write(slot,frame);changed=true;}
        }
        const auto meshes=BeltArray(actor,"ItemMeshes",4),offsets=BeltArray(actor,"ItemOffsets",12);
        for(int i=0;i<Conveyor::ItemSlots;++i)
        {
            auto mesh=Read<Address>(meshes+i*4);
            std::array<float,3> offset{};
            if(mesh && IsA(mesh,"StaticMesh"))offset=Conveyor::ItemOffset(Read<std::array<float,6>>(mesh+0x28));
            if(Read<std::array<float,3>>(offsets+i*12)!=offset){Write(offsets+i*12,offset);changed=true;}
        }
        return changed;
    }
}

Json ConveyorState()
{
    auto c=ConveyorBeltClass();
    Json belts=Json::array();
    if(c)for(auto a:ActorsOf(c))belts.push_back(Identity(a));
    return {{"compiled",ConveyorCurrent(c)},{"version",ConveyorClassVersion(c)},{"belts",belts}};
}

// Imports the texture and meshes and compiles the classes (the rollers', items' and
// motor's before the belt's, which spawns them) into the open map, unless
// they are there at this version already. A map's older versions are made again over
// themselves, so its belts keep their settings.
void PrepareConveyors()
{
    const auto package=LevelPath();
    const bool current=ConveyorCurrent(ConveyorBeltClass());
    if(!current || !Find(ConveyorPath(Conveyor::Texture)))ImportConveyorTexture(package);
    for(const auto& mesh:{Conveyor::Frame(),Conveyor::Frame(false,true),Conveyor::Frame(true,false),Conveyor::Frame(false,false),Conveyor::Roller()})
        if(!current || !Find(ConveyorPath(mesh.name.c_str())))ImportConveyorMesh(package,mesh);
    auto compiled=[](Address c){return ClassCompiled(c);};
    CompileMapClass(Conveyor::RollerClass,Conveyor::RollerScript(package),"Actor",compiled,!current);
    CompileMapClass(Conveyor::ItemClass,Conveyor::ItemScript(),"Actor",compiled,!current);
    CompileMapClass(Conveyor::MotorClass,Conveyor::MotorScript(),"Info",compiled,!current);
    CompileMapClass(Conveyor::BeltClass,Conveyor::BeltScript(package),"Actor",ConveyorCurrent,!current);
}

// The one selected actor when it is a conveyor belt, else null.
Json SelectedConveyorBelt()
{
    auto c=Find(LevelPath()+"."+Conveyor::BeltClass);
    if(!c)return nullptr;
    Address found=0;size_t selected=0;
    for(auto a:LiveActors())
        if(Read<unsigned>(a+0x2f4)&0x40)
        {
            if(++selected>1)return nullptr;
            if(Read<Address>(a+0x24)==c)found=a;
        }
    return found?Identity(found):Json(nullptr);
}

// Places a belt. Without a location it starts where the viewport was last clicked,
// standing on the floor when that was a floor. after (a belt's name or path) puts it
// at that belt's end, turned as it is, with its width, speed and options, and links
// it in: the belt carries its items on onto the new one (and the new one onto the
// belt that came next, if any). properties sets its own (Length, Speed,
// ItemMeshes, ...) as the property window would.
Json AddConveyorBelt(const Json& spec)
{
    PrepareConveyors();
    auto c=ConveyorBeltClass();
    Address previous=0;
    if(spec.contains("after"))
    {
        auto name=spec.at("after").get<std::string>();
        if(name.find('.')==std::string::npos)name=LevelPath()+"."+name;
        previous=Find(name,true);
        if(!previous || Read<Address>(previous+0x24)!=c)throw std::runtime_error(spec.at("after").get<std::string>()+" is not a conveyor belt in this map.");
    }
    const auto properties=spec.value("properties",Json::object());
    for(auto it=properties.begin();it!=properties.end();++it)
    {
        bool found=false;std::set<Address> seen;
        for(Address k=c;k && !found && seen.insert(k).second && seen.size()<64;k=Read<Address>(k+0x28))found=StructField(k,it.key())!=0;
        if(!found)throw std::runtime_error("ConveyorBelt has no property "+it.key()+".");
    }
    std::set<Address> before;for(auto a:ActorsOf(c))before.insert(a);
    if(!Exec(std::string("ACTOR ADD CLASS=")+Conveyor::BeltClass))throw std::runtime_error("The editor refused to place a conveyor belt.");
    Address actor=0;
    for(auto a:ActorsOf(c))if(!before.count(a))actor=a;
    if(!actor)throw std::runtime_error("The editor did not place a conveyor belt.");
    Vector at{};
    if(spec.contains("location"))at=spec.at("location").get<Vector>();
    else if(previous)at=TransformPoint({Read<float>(BeltField(previous,"Length")),0,0},{Position(previous),RotationOf(previous)});
    else
    {
        auto unrealEd=Read<Address>(0x117a59b0);if(!unrealEd)throw std::runtime_error("The editor is unavailable.");
        const auto click=Read<std::array<float,3>>(unrealEd+0x1bc);
        const auto plane=Read<std::array<float,3>>(unrealEd+0x1d4);
        for(int axis=0;axis<3;++axis)at[axis]=click[axis];
        if(spec.value("surface",true) && std::isfinite(plane[2]) && plane[2]>0.7f)at[2]+=Conveyor::FrameDepth;
    }
    Transaction transaction("Add conveyor belt");
    Modify(actor);
    SetPosition(actor,at);
    if(spec.contains("rotation"))Write(Field(actor,"Rotation"),spec.at("rotation").get<Rotation>());
    else if(previous)Write(Field(actor,"Rotation"),RotationOf(previous));
    if(previous)
    {
        // Its look and speed. bCarryPlayers shares its word with bShowItems and
        // bShowRollers, which come along.
        for(const char* name:{"Width","Speed","ItemScale","bCarryPlayers"})
            Write(BeltField(actor,name),Read<unsigned>(BeltField(previous,name)));
        Modify(previous);
        Write(BeltField(actor,"NextBelt"),Read<Address>(BeltField(previous,"NextBelt")));
        Write(BeltField(previous,"NextBelt"),actor);
        Call(previous,0x44);
    }
    // The stock cases and boxes until the properties say otherwise.
    const auto meshes=BeltArray(actor,"ItemMeshes",4);
    int slot=0;
    for(const auto* path:Conveyor::DefaultItems)
        if(auto mesh=LoadStockMesh(path);mesh && slot<Conveyor::ItemSlots)Write(meshes+4*slot++,mesh);
    for(auto it=properties.begin();it!=properties.end();++it)
    {
        const auto& key=it.key();const auto& value=it.value();
        auto p=BeltProperty(actor,key);
        if(!p)throw std::runtime_error("ConveyorBelt has no property "+key+".");
        if(key=="ItemMeshes" && value.is_array())
        {
            for(int i=0;i<Conveyor::ItemSlots;++i)
            {
                Address mesh=0;
                if(i<static_cast<int>(value.size()) && value[i].is_string() && !value[i].get<std::string>().empty() && Fold(value[i].get<std::string>())!="none")
                {
                    auto path=value[i].get<std::string>();
                    if(auto name=ReferencedName(path);!name.empty())path=name;
                    mesh=LoadStockMesh(path);
                    if(!mesh)throw std::runtime_error("ItemMeshes: "+path+" is not a static mesh the editor can load.");
                }
                Write(meshes+4*i,mesh);
            }
            continue;
        }
        try{MagicImport(p,actor+Read<int>(p+0x3c),ImportValue(value));}catch(const std::exception& e){throw std::runtime_error(key+": "+e.what());}
    }
    SyncBelt(actor);
    Call(actor,0x44);
    transaction.Commit();
    Select(Json::array({Identity(actor)}));
    Redraw();
    return Identity(actor);
}

// Keeps every belt's frame sized to its Length and Width and its item offsets to its
// meshes as they are edited. Cheap when the map has no belts.
size_t SyncConveyorBelts()
{
    auto c=ConveyorBeltClass();
    if(!ConveyorCurrent(c))return 0;
    size_t changed=0;
    for(auto a:ActorsOf(c))if(SyncBelt(a)){Call(a,0x44);++changed;}
    if(changed)Redraw();
    return changed;
}
