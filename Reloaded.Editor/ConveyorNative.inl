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
        for(const char* name:{"Length","Width","Speed","ItemSpacing","FadeDistance","ItemScale","bCarryPlayers","bShowItems","bShowRollers","ItemMeshes","ItemOffsets"})
            if(!StructField(c,name))return false;
        return true;
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
        auto p=Property(actor,name);
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
        const auto scale=Conveyor::FrameScale(Read<float>(Field(actor,"Length")),Read<float>(Field(actor,"Width")));
        const auto at=Field(actor,"DrawScale3D");
        if(Read<std::array<float,3>>(at)!=scale){Write(at,scale);changed=true;}
        if(Read<float>(Field(actor,"DrawScale"))!=1.0f){Write(Field(actor,"DrawScale"),1.0f);changed=true;}
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

// Imports the texture and meshes and compiles the classes into the open map, unless
// they are there at this version already. A map's older versions are made again over
// themselves, so its belts keep their settings.
void PrepareConveyors()
{
    const auto package=LevelPath();
    const bool current=ConveyorCurrent(ConveyorBeltClass());
    if(!current || !Find(ConveyorPath(Conveyor::Texture)))ImportConveyorTexture(package);
    for(const auto& mesh:{Conveyor::Frame(),Conveyor::Roller()})
        if(!current || !Find(ConveyorPath(mesh.name.c_str())))ImportConveyorMesh(package,mesh);
    auto compiled=[](Address c){return ClassCompiled(c);};
    CompileMapClass(Conveyor::RollerClass,Conveyor::RollerScript(package),"Actor",compiled,!current);
    CompileMapClass(Conveyor::ItemClass,Conveyor::ItemScript(),"Actor",compiled,!current);
    CompileMapClass(Conveyor::BeltClass,Conveyor::BeltScript(package),"Actor",ConveyorCurrent,!current);
}

// Places a belt. Without a location it goes where the viewport was last clicked,
// standing on the floor when that was a floor. properties sets its own (Length,
// Speed, ItemMeshes, ...) as the property window would.
Json AddConveyorBelt(const Json& spec)
{
    PrepareConveyors();
    auto c=ConveyorBeltClass();
    std::set<Address> before;for(auto a:ActorsOf(c))before.insert(a);
    if(!Exec(std::string("ACTOR ADD CLASS=")+Conveyor::BeltClass))throw std::runtime_error("The editor refused to place a conveyor belt.");
    Address actor=0;
    for(auto a:ActorsOf(c))if(!before.count(a))actor=a;
    if(!actor)throw std::runtime_error("The editor did not place a conveyor belt.");
    Vector at{};
    if(spec.contains("location"))at=spec.at("location").get<Vector>();
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
    // The stock cases and boxes until the properties say otherwise.
    const auto meshes=BeltArray(actor,"ItemMeshes",4);
    int slot=0;
    for(const auto* path:Conveyor::DefaultItems)
        if(auto mesh=LoadStockMesh(path);mesh && slot<Conveyor::ItemSlots)Write(meshes+4*slot++,mesh);
    for(const auto& [key,value]:spec.value("properties",Json::object()).items())
    {
        auto p=Property(actor,key);
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
        try{MagicImport(p,actor+Read<int>(p+0x3c),value);}catch(const std::exception& e){throw std::runtime_error(key+": "+e.what());}
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
