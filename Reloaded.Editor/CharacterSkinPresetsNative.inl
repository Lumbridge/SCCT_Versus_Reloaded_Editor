// Character Skin presets (CharacterSkinPresetsModel.h): the library in
// skin_presets.json beside library.json, each user preset's pictures (and model files)
// in skin_presets\<id>\, and the built-ins compiled into the DLL, which are painted
// over the stock textures (or import a model the DLL carries) and never written. Each
// preset dresses one team. Applying goes through ImportCharacterSkin and
// ApplyCharacterSkins with the other team's values read back from the map, so a
// preset lands in the map exactly as the Character Skins window's own Import and
// Apply do and the other team keeps what it wears.
// Included by WorkflowEditor.cpp after CharacterSkinsNative.inl.
namespace
{
    namespace PresetModel=CharacterSkins::Presets;
    std::filesystem::path SkinPresetFile() { return Directory()/PresetModel::FileName; }
    std::filesystem::path SkinPresetFolder(const std::string& id)
    {
        if(!PresetModel::IsUserId(id))throw std::runtime_error("Only your own presets keep pictures.");
        return Directory()/PresetModel::ImageFolder/id;
    }
    CharacterSkins::Bytes ReadBytes(const std::filesystem::path& file,size_t limit);
    // A version 1 file (presets for both teams) is split into team presets the first time
    // it is read: the old file is kept as skin_presets.before-teams.json, and the merc
    // half of each split preset gets its pictures copied into its own folder.
    Json SkinPresetDocument()
    {
        Json raw;
        try
        {
            const auto file=SkinPresetFile();
            if(!std::filesystem::exists(file))raw=PresetModel::EmptyDocument();
            else{const auto bytes=ReadBytes(file,64u<<20);raw=Json::parse(bytes.begin(),bytes.end());}
        }
        catch(const std::exception&) { throw std::runtime_error(PresetModel::DamagedFile()); }
        if(raw.is_object() && raw.value("version",0)==1)
        {
            auto migration=PresetModel::Migrate(raw);
            std::error_code ignored;
            const auto backup=Directory()/"skin_presets.before-teams.json";
            if(!std::filesystem::exists(backup))std::filesystem::copy_file(SkinPresetFile(),backup,ignored);
            for(const auto& copy:migration.copies)
            {
                const auto from=SkinPresetFolder(copy.from)/PresetModel::ImageFile(copy.property);
                const auto to=SkinPresetFolder(copy.to)/PresetModel::ImageFile(copy.property);
                std::filesystem::create_directories(to.parent_path(),ignored);
                if(!std::filesystem::exists(to))std::filesystem::copy_file(from,to,ignored);
            }
            try{WriteDocument(SkinPresetFile(),migration.document);}catch(const std::exception&){} // still listed, migrated in memory
            raw=std::move(migration.document);
        }
        try { return PresetModel::Document(raw); }
        catch(const std::exception&) { throw std::runtime_error(PresetModel::DamagedFile()); }
    }
    CharacterSkins::Bytes ReadBytes(const std::filesystem::path& file,size_t limit)
    {
        std::error_code error;const auto size=std::filesystem::file_size(file,error);
        if(error)throw std::runtime_error("Cannot read "+PathText(file));
        if(size>limit)throw std::runtime_error(PathText(file)+" is too large.");
        CharacterSkins::Bytes bytes(static_cast<size_t>(size));
        std::ifstream input(file,std::ios::binary);
        if(!input || !input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))throw std::runtime_error("Cannot read "+PathText(file));
        return bytes;
    }
    // Written beside the target and moved over it, so a failed write leaves the old file.
    void WriteBytes(const std::filesystem::path& file,const void* data,size_t size)
    {
        std::error_code error;std::filesystem::create_directories(file.parent_path(),error);
        auto temporary=file;temporary+=".tmp";
        {
            std::ofstream out(temporary,std::ios::binary);
            out.write(static_cast<const char*>(data),static_cast<std::streamsize>(size));
            if(!out)throw std::runtime_error("Cannot write "+PathText(file));
        }
        std::filesystem::rename(temporary,file,error);
        if(error){std::filesystem::remove(temporary,error);throw std::runtime_error("Cannot write "+PathText(file));}
    }
    // The stock pictures of the four slots, read once from SPersoTextures.utx.
    const std::vector<CharacterSkins::Image>& StockSkinImages()
    {
        static std::vector<CharacterSkins::Image> images;
        if(images.empty())
        {
            const auto source=Directory().parent_path().parent_path()/"Packages"/"Textures"/"SPersoTextures.utx";
            const auto package=ReadBytes(source,256u<<20);
            std::vector<CharacterSkins::Image> read;
            for(const auto& slot:CharacterSkins::Slots)read.push_back(CharacterSkins::Decode(CharacterSkins::ReadMip(package,slot.texture)));
            images=std::move(read);
        }
        return images;
    }
    size_t SlotIndex(const std::string& property)
    {
        for(size_t i=0;i<CharacterSkins::Slots.size();++i)if(property==CharacterSkins::Slots[i].property)return i;
        throw std::runtime_error("Unknown Character Skins slot "+property);
    }
    // The top mip of a loaded texture (UTexture layout as in TextureBrowser.cpp's DDS export).
    CharacterSkins::Image LoadedTextureImage(Address texture)
    {
        if(!texture || !IsA(texture,"Texture"))throw std::runtime_error("not a texture");
        const int format=Read<unsigned char>(texture+0x5C),ubits=Read<unsigned char>(texture+0x5F),vbits=Read<unsigned char>(texture+0x60);
        const auto mips=Read<Address>(texture+0x70);const int count=Read<int>(texture+0x74);
        if(ubits>11 || vbits>11 || !mips || count<=0)throw std::runtime_error("the texture has no picture");
        reinterpret_cast<void(__fastcall*)(void*)>(0x10EAB600)(reinterpret_cast<void*>(mips+0x10));
        const auto data=Read<Address>(mips+0x1C);const int size=Read<int>(mips+0x20);
        if(!data || size<=0)throw std::runtime_error("the texture's picture is not loaded");
        CharacterSkins::Mip mip;mip.format=format;mip.width=1<<ubits;mip.height=1<<vbits;
        mip.data.assign(reinterpret_cast<const unsigned char*>(data),reinterpret_cast<const unsigned char*>(data)+size);
        return CharacterSkins::Decode(mip);
    }
    // Loads a material's package from Packages\Textures when it is not loaded yet.
    Address LoadMaterial(const std::string& path)
    {
        if(auto found=Find(path))return found;
        const auto package=path.substr(0,path.find('.'));
        const auto file=Directory().parent_path().parent_path()/"Packages"/"Textures"/(package+".utx");
        if(!package.empty() && std::filesystem::exists(file))Exec("OBJ LOAD FILE=\""+file.string()+"\"");
        return Find(path);
    }
    // A slot's picture as 32-bit TGA bytes: painted from its recipe or read from the
    // preset's own file.
    CharacterSkins::Bytes SkinPresetPicture(const Json& entry,const std::string& property)
    {
        const auto& value=entry.at("slots").at(property);
        if(value.contains("recipe"))
            return CharacterSkins::Tga(PresetModel::Recolour(StockSkinImages()[SlotIndex(property)],PresetModel::ReadRecipe(value.at("recipe"))));
        const auto file=SkinPresetFolder(entry.at("id").get<std::string>())/PresetModel::ImageFile(property);
        try{return CharacterSkins::Tga(PresetModel::ReadTga(ReadBytes(file,64u<<20)));}
        catch(const std::exception& e){throw std::runtime_error(std::string("The preset's picture for ")+property+" cannot be used: "+e.what());}
    }
    // An entry's image slots, with every recipe slot painted into an image too: what a
    // shared file or a new user entry holds.
    Json Painted(Json entry,std::map<std::string,CharacterSkins::Bytes>& images)
    {
        for(const auto& slot:CharacterSkins::Slots)
        {
            if(!entry.at("slots").contains(slot.property))continue;
            auto& value=entry["slots"][slot.property];
            if(value.contains("recipe") || value.contains("image"))
            {
                if(!images.count(slot.property))images[slot.property]=SkinPresetPicture(entry,slot.property);
                value={{"image",PresetModel::ImageFile(slot.property)}};
            }
        }
        return entry;
    }
    // Writes a saved user entry's pictures into its folder.
    void WritePictures(const std::string& id,const std::map<std::string,CharacterSkins::Bytes>& images)
    {
        for(const auto& [property,bytes]:images)WriteBytes(SkinPresetFolder(id)/PresetModel::ImageFile(property),bytes.data(),bytes.size());
    }
    Json SkinPresetEntry(const std::string& id) { return PresetModel::Find(SkinPresets()["entries"],id); }
    // The map's applied Character Skins values {slots, models, goggles}.
    Json MapSkinValues()
    {
        const auto settings=CharacterSkinSettings();
        return {{"slots",settings.at("slots")},{"models",settings.at("models")},{"goggles",settings.at("goggles")}};
    }
}

// Models a preset carries (CharacterSkinPresetsModel.h "mesh"): a built-in's files come
// from a zlib bundle in the DLL (tools/models/<model>/pack_bundle.py, RCDATA in
// Reloaded.Editor.rc), a user preset's from its folder. Applying imports them into the
// map: the textures into group Models (DXT1), the mesh stood up like the team's stock
// model, both named with the PSK's hash so the same model is imported once per map.
namespace
{
    struct BuiltinModel { const char* name; int resource; };
    constexpr BuiltinModel kBuiltinModels[]={{"HazmatMerc",IDR_HAZMAT_BUNDLE}};
    // Bundle: "RHZ1", uint32 count, then per file uint16 name length, name, uint32 size,
    // uint32 compressed size, zlib data.
    std::map<std::string,CharacterSkins::Bytes> BundleFiles(int id)
    {
        HMODULE module=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&BundleFiles),&module);
        auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);
        auto loaded=resource?LoadResource(module,resource):nullptr;
        auto data=loaded?static_cast<const unsigned char*>(LockResource(loaded)):nullptr;
        const size_t size=resource?SizeofResource(module,resource):0;
        if(!data || size<8 || memcmp(data,"RHZ1",4))throw std::runtime_error("This RE+ build does not carry the model.");
        auto take=[&](size_t& at,size_t n){if(at+n>size)throw std::runtime_error("The built-in model is damaged.");auto p=data+at;at+=n;return p;};
        size_t at=4;
        uint32_t count;memcpy(&count,take(at,4),4);
        std::map<std::string,CharacterSkins::Bytes> files;
        for(uint32_t i=0;i<count;++i)
        {
            uint16_t length;memcpy(&length,take(at,2),2);
            std::string name(reinterpret_cast<const char*>(take(at,length)),length);
            uint32_t raw,packed;memcpy(&raw,take(at,4),4);memcpy(&packed,take(at,4),4);
            auto source=take(at,packed);
            CharacterSkins::Bytes bytes(raw);
            uLongf written=raw;
            if(uncompress(bytes.data(),&written,source,packed)!=Z_OK || written!=raw)throw std::runtime_error("The built-in model is damaged ("+name+").");
            files[name]=std::move(bytes);
        }
        return files;
    }
    // Every file of an entry's model by name, checked: the PSK names the entry's materials.
    std::map<std::string,CharacterSkins::Bytes> PresetModelFiles(const Json& entry)
    {
        const auto id=entry.at("id").get<std::string>();
        const auto names=PresetModel::MeshFiles(entry);
        std::map<std::string,CharacterSkins::Bytes> files;
        if(PresetModel::IsBuiltinId(id))
        {
            const auto mesh=entry.at("mesh").at("name").get<std::string>();
            const BuiltinModel* builtin=nullptr;
            for(const auto& b:kBuiltinModels)if(Fold(b.name)==Fold(mesh))builtin=&b;
            if(!builtin)throw std::runtime_error("This RE+ build does not carry the model "+mesh+".");
            auto bundle=BundleFiles(builtin->resource);
            for(const auto& name:names)
            {
                auto found=bundle.find(name);
                if(found==bundle.end())throw std::runtime_error("The built-in model has no "+name+".");
                files[name]=std::move(found->second);
            }
        }
        else
            for(const auto& name:names)files[name]=ReadBytes(SkinPresetFolder(id)/name,256u<<20);
        const auto materials=PresetModel::PskMaterials(files.at(names[0]));
        const auto& wanted=entry.at("mesh").at("materials");
        bool same=materials.size()==wanted.size();
        for(size_t i=0;same && i<materials.size();++i)same=Fold(materials[i])==Fold(wanted[i].get<std::string>());
        if(!same)throw std::runtime_error("The preset's model file no longer names the materials it was saved with.");
        return files;
    }
    // Imports an entry's model into the map unless it is there already; returns the mesh's path.
    std::string ImportPresetModel(const Json& entry,const CharacterSkins::Presets::Team& team)
    {
        const auto package=Path(Read<Address>(Level()+0x18));
        auto files=PresetModelFiles(entry);
        const auto& psk=files.at(PresetModel::MeshFile(entry));
        const auto meshPath=package+"."+PresetModel::MeshImportName(entry.at("mesh").at("name").get<std::string>(),psk);
        if(auto existing=Find(meshPath))
        {
            if(NameOf(Read<Address>(existing+0x24))!="SkeletalMesh")throw std::runtime_error(meshPath+" already exists and is not a skeletal mesh.");
            return meshPath;
        }
        char temp[MAX_PATH]{};GetTempPathA(MAX_PATH,temp);
        const auto folder=std::filesystem::path(temp)/("RE+Model-"+std::to_string(GetCurrentProcessId()));
        std::error_code error;std::filesystem::create_directories(folder,error);
        struct Cleanup{std::filesystem::path folder;~Cleanup(){std::error_code e;std::filesystem::remove_all(folder,e);}} cleanup{folder};
        std::vector<Address> textures;
        for(const auto& material:entry.at("mesh").at("materials"))
        {
            const auto name=PresetModel::MaterialImportName(material.get<std::string>(),psk);
            const auto path=package+".Models."+name;
            auto texture=Find(path);
            if(!texture)
            {
                const auto file=folder/(name+".tga");
                const auto& bytes=files.at(PresetModel::MaterialFile(material.get<std::string>()));
                WriteBytes(file,bytes.data(),bytes.size());
                Exec("TEXTURE IMPORT FILE=\""+file.string()+"\" NAME=\""+name+"\" PACKAGE=\""+package+"\" GROUP=\"Models\" MIPS=1");
                texture=Find(path);
                if(!texture || !IsA(texture,"Texture"))throw std::runtime_error("The editor's texture importer refused the model's "+material.get<std::string>()+".");
                int chain=1;for(int side=(std::max)(AuthoringValue(texture,"USize"),AuthoringValue(texture,"VSize"));side>1;side>>=1)++chain;
                Exec("TEXTURE COMPRESS NAME="+path+" FORMAT=DXT1 MaxMips="+std::to_string(chain));
            }
            textures.push_back(texture);
        }
        const auto pskFile=folder/PresetModel::MeshFile(entry);
        WriteBytes(pskFile,psk.data(),psk.size());
        const auto meshName=PresetModel::ObjectName(meshPath);
        Exec("NEWANIM IMPORT FILE=\""+pskFile.string()+"\" PACKAGE=\""+package+"\" NAME=\""+meshName+"\" YAW=0 PITCH=0 ROLL=0");
        auto mesh=Find(meshPath);
        if(!mesh || NameOf(Read<Address>(mesh+0x24))!="SkeletalMesh")throw std::runtime_error("The editor did not import the preset's model. See the editor log.");
        // Stand it up the way the team's stock model stands: UMesh::RotOrigin (+0x8c).
        std::array<int,3> rotation{0,-16384,0};
        if(const auto* model=PresetModel::FindModel(team.model))if(auto stock=Find(model->stockMesh))rotation=Read<std::array<int,3>>(stock+0x8c);
        Write(mesh+0x8c,rotation);
        // Its materials, in the PSK's order, in the native UMesh::Material array (+0x68; the
        // Animation Browser probes the same offset). The textures' names carry the hash, so
        // the importer's link by name finds none of them.
        const auto data=Read<Address>(mesh+0x68);const int count=Read<int>(mesh+0x6c);
        if(!data || count<static_cast<int>(textures.size()))throw std::runtime_error("The editor imported the model without its materials. See the editor log.");
        for(size_t i=0;i<textures.size();++i)Write(data+i*4,textures[i]);
        Logger::log("Skin presets: imported the model "+meshPath);
        return meshPath;
    }
    // Writes a saved user entry's model files into its folder.
    void WriteModelFiles(const std::string& id,const std::map<std::string,CharacterSkins::Bytes>& files)
    {
        for(const auto& [name,bytes]:files)WriteBytes(SkinPresetFolder(id)/name,bytes.data(),bytes.size());
    }
}

Json AddSkinPresetModel(const std::string& teamId,const std::filesystem::path& psk,const Json& details)
{
    const auto* team=PresetModel::FindTeam(teamId);
    if(!team)throw std::runtime_error("Choose spies or mercs first.");
    auto bytes=ReadBytes(psk,256u<<20);
    std::vector<std::string> materials;
    try{materials=PresetModel::PskMaterials(bytes);}catch(const std::exception& e){throw std::runtime_error(PathText(psk)+": "+e.what());}
    const auto name=PresetModel::ModelNameFrom(psk.stem().string());
    std::map<std::string,CharacterSkins::Bytes> files;
    files[name+".psk"]=std::move(bytes);
    // One picture per material, named after it, beside the PSK.
    const auto folder=psk.parent_path();
    for(const auto& material:materials)
    {
        if(!PresetModel::ValidModelName(material))throw std::runtime_error("The PSK's material '"+material+"' needs a name of letters, digits and underscores.");
        std::filesystem::path found;
        std::error_code error;
        for(const auto& file:std::filesystem::directory_iterator(folder,error))
            if(Fold(file.path().stem().string())==Fold(material) && Fold(file.path().extension().string())==".tga")found=file.path();
        if(found.empty())throw std::runtime_error("No "+material+".tga beside "+PathText(psk)+". Each material of the model needs a TGA picture named after it.");
        auto picture=ReadBytes(found,64u<<20);
        try{PresetModel::ReadTga(picture);}catch(const std::exception& e){throw std::runtime_error(PathText(found)+": "+e.what());}
        files[PresetModel::MaterialFile(material)]=std::move(picture);
    }
    // Goggle lights from a JSON beside it: "goggles": [x,y,z], or the hazmat build's
    // "goggle_offset_from_merc_front".
    Json goggles=Json::object();
    {
        std::error_code error;
        for(const auto& file:std::filesystem::directory_iterator(folder,error))
        {
            if(Fold(file.path().extension().string())!=".json")continue;
            try
            {
                const auto text=ReadBytes(file.path(),1u<<20);
                const auto json=Json::parse(text.begin(),text.end());
                for(const char* key:{"goggles","goggle_offset_from_merc_front"})
                    if(json.is_object() && json.contains(key) && json.at(key).is_array() && json.at(key).size()==3)
                    {
                        const auto& v=json.at(key);
                        const auto o=CharacterSkins::CheckOffset({v[0].get<double>(),v[1].get<double>(),v[2].get<double>()});
                        if(o.x || o.y || o.z)goggles[team->goggles]=Json::array({o.x,o.y,o.z});
                    }
            }
            catch(const std::exception&){}
            if(!goggles.empty())break;
        }
    }
    Json entry={{"team",team->id},{"name",name},{"category","Models"},{"description",""},{"slots",Json::object()},
                {"mesh",{{"name",name},{"materials",materials}}},{"goggles",goggles}};
    for(const char* key:{"name","category","description"})if(details.contains(key))entry[key]=details.at(key);
    auto file=SkinPresetDocument();
    auto saved=PresetModel::Save(file,entry);
    WriteModelFiles(saved.at("id"),files);
    WriteDocument(SkinPresetFile(),file);
    return saved;
}

Json SkinPresets()
{
    Json file;Json problems=Json::array();
    try{file=SkinPresetDocument();for(auto& p:PresetModel::Problems(file))problems.push_back(p);}
    catch(const std::exception& e){file=PresetModel::EmptyDocument();problems.push_back(e.what());}
    auto entries=PresetModel::Merge(PresetModel::Builtins(),file);
    Json wearing=Json::object();
    try
    {
        const auto values=MapSkinValues();
        for(const auto& team:PresetModel::Teams)wearing[team.id]=PresetModel::Wearing(team,values,entries,file.at("imports"));
    }
    catch(const std::exception&){for(const auto& team:PresetModel::Teams)wearing[team.id]="";}
    return {{"entries",entries},{"categories",PresetModel::Categories(entries)},{"problems",problems},{"wearing",wearing}};
}

std::vector<CharacterSkins::Image> MapSkinImages(int size)
{
    std::vector<CharacterSkins::Image> out;
    Json slots=Json::object();
    try{slots=CharacterSkinSettings().at("slots");}catch(const std::exception&){}
    for(const auto& slot:CharacterSkins::Slots)
    {
        CharacterSkins::Image image;
        try
        {
            const auto path=slots.value(slot.property,std::string{});
            if(path.empty())image=StockSkinImages()[SlotIndex(slot.property)];
            else if(const auto material=Find(path);material && IsA(material,"Texture"))image=LoadedTextureImage(material);
        }
        catch(const std::exception&){image={};}
        if(size>0 && image.width>0 && (image.width!=size || image.height!=size))image=PresetModel::Shrink(image,size,size);
        out.push_back(std::move(image));
    }
    return out;
}

std::vector<CharacterSkins::Image> SkinPresetImages(const Json& entry,int size)
{
    std::vector<CharacterSkins::Image> out;
    if(PresetModel::HasMesh(entry))
    {
        // A model's own textures, the first two.
        std::map<std::string,CharacterSkins::Bytes> files;
        try{files=PresetModelFiles(entry);}catch(const std::exception&){}
        const auto& materials=entry.at("mesh").at("materials");
        for(size_t i=0;i<2 && i<materials.size();++i)
        {
            CharacterSkins::Image image;
            try{image=PresetModel::ReadTga(files.at(PresetModel::MaterialFile(materials[i].get<std::string>())));}catch(const std::exception&){image={};}
            if(size>0 && image.width>0 && (image.width!=size || image.height!=size))image=PresetModel::Shrink(image,size,size);
            out.push_back(std::move(image));
        }
        return out;
    }
    const auto slots=entry.value("slots",Json::object());
    const auto& team=PresetModel::TeamOf(entry);
    for(const auto& slot:CharacterSkins::Slots)
    {
        if(!PresetModel::Owns(team,slot.property))continue;
        CharacterSkins::Image image;
        try
        {
            const auto& stock=StockSkinImages()[SlotIndex(slot.property)];
            if(!slots.contains(slot.property))image=stock;
            else if(slots.at(slot.property).contains("path"))
            {
                const auto material=Find(slots.at(slot.property).at("path").get<std::string>());
                if(material && IsA(material,"Texture"))image=LoadedTextureImage(material);
            }
            else if(size>0 && slots.at(slot.property).contains("recipe"))
                image=PresetModel::Recolour(PresetModel::Shrink(stock,size,size),PresetModel::ReadRecipe(slots.at(slot.property).at("recipe")));
            else image=PresetModel::ReadTga(SkinPresetPicture(entry,slot.property));
        }
        catch(const std::exception&){image={};}
        if(size>0 && image.width>0 && (image.width!=size || image.height!=size))image=PresetModel::Shrink(image,size,size);
        out.push_back(std::move(image));
    }
    return out;
}

Json ApplySkinPreset(const std::string& id)
{
    const auto entry=SkinPresetEntry(id);
    const auto& team=PresetModel::TeamOf(entry);
    const auto package=Path(Read<Address>(Level()+0x18));
    std::map<std::string,std::string> paths,imported; // slot -> material; texture name -> preset id
    for(const auto& slot:CharacterSkins::Slots)
    {
        if(!PresetModel::Owns(team,slot.property))continue;
        std::string path;
        if(entry.at("slots").contains(slot.property))
        {
            const auto& value=entry.at("slots").at(slot.property);
            if(value.contains("path"))
            {
                path=value.at("path").get<std::string>();
                if(!LoadMaterial(path))throw std::runtime_error(std::string(slot.label)+": "+path+" is not installed. This preset needs that package in Packages\\Textures.");
            }
            else
            {
                // Named after the picture's hash: applying the same preset again reuses the texture.
                const auto bytes=SkinPresetPicture(entry,slot.property);
                const auto name=PresetModel::ImportName(slot.property,bytes);
                path=package+".CharacterSkins."+name;
                if(!Find(path))
                {
                    char folder[MAX_PATH]{};GetTempPathA(MAX_PATH,folder);
                    const auto file=std::filesystem::path(folder)/(name+".tga");
                    WriteBytes(file,bytes.data(),bytes.size());
                    try{path=ImportCharacterSkin(slot.property,file,name);}
                    catch(...){std::error_code ignored;std::filesystem::remove(file,ignored);throw;}
                    std::error_code ignored;std::filesystem::remove(file,ignored);
                }
                imported[PresetModel::ObjectName(path)]=id;
            }
        }
        if(!path.empty())paths[slot.property]=path;
    }
    std::string mesh;
    if(PresetModel::HasMesh(entry))
    {
        mesh=ImportPresetModel(entry,team);
        imported[PresetModel::ObjectName(mesh)]=id;
    }
    // The other team's values exactly as the map has them.
    const auto values=PresetModel::Dress(entry,MapSkinValues(),paths,mesh);
    auto settings=ApplyCharacterSkins(values.at("slots"),values.at("models"),values.at("goggles"));
    // Remembered so the browser can name what each team wears; never fails the apply.
    if(!imported.empty())
        try{auto file=SkinPresetDocument();PresetModel::RecordImports(file,imported);WriteDocument(SkinPresetFile(),file);}catch(const std::exception&){}
    return settings;
}

Json SaveSkinPreset(const std::string& teamId,const Json& slots,const Json& models,const Json& goggles,const Json& details)
{
    const auto* team=PresetModel::FindTeam(teamId);
    if(!team)throw std::runtime_error("Choose spies or mercs to save.");
    auto file=SkinPresetDocument();
    const auto package=Fold(Path(Read<Address>(Level()+0x18)));
    std::set<std::string> local;std::map<std::string,CharacterSkins::Bytes> images;
    for(const auto& slot:CharacterSkins::Slots)
    {
        if(!PresetModel::Owns(*team,slot.property))continue;
        const auto path=slots.is_object()?slots.value(slot.property,std::string{}):std::string{};
        const auto folded=Fold(path);
        if(path.empty() || (folded.rfind(package+".",0)!=0 && folded.rfind("mylevel.",0)!=0))continue;
        // A texture stored in this map travels with the preset as a picture.
        const auto material=Find(path);
        if(!material)throw std::runtime_error(std::string(slot.label)+": "+path+" is not loaded.");
        try{images[slot.property]=CharacterSkins::Tga(LoadedTextureImage(material));}
        catch(const std::exception& e){throw std::runtime_error(std::string(slot.label)+": "+path+" cannot be saved in a preset ("+e.what()+"). Only textures stored in the map can be kept as pictures.");}
        local.insert(slot.property);
    }
    auto entry=PresetModel::Capture(*team,slots,models,goggles,local);
    for(const char* key:{"name","category","description"})if(details.contains(key))entry[key]=details.at(key);
    // A model a preset imported into this map travels as that preset's model files.
    std::map<std::string,CharacterSkins::Bytes> modelFiles;
    const auto model=entry.at("models").value(team->model,std::string{});
    const auto& imports=file.at("imports");
    if(const auto name=PresetModel::ObjectName(model);!model.empty() && imports.contains(name))
        try
        {
            const auto source=SkinPresetEntry(imports.at(name).get<std::string>());
            if(PresetModel::HasMesh(source))
            {
                modelFiles=PresetModelFiles(source);
                entry["models"].erase(team->model);
                entry["mesh"]=source.at("mesh");
            }
        }
        catch(const std::exception&){} // the preset is gone: the path is kept as it is
    auto saved=PresetModel::Save(file,entry);
    WritePictures(saved.at("id"),images);
    WriteModelFiles(saved.at("id"),modelFiles);
    WriteDocument(SkinPresetFile(),file);
    return saved;
}

Json UpdateSkinPreset(const std::string& id,const Json& changes)
{
    auto file=SkinPresetDocument();
    auto updated=PresetModel::Update(file,id,changes);
    WriteDocument(SkinPresetFile(),file);
    return updated;
}

void DeleteSkinPreset(const std::string& id)
{
    auto file=SkinPresetDocument();
    const bool removed=PresetModel::Delete(file,PresetModel::Builtins(),id);
    WriteDocument(SkinPresetFile(),file);
    if(removed){std::error_code ignored;std::filesystem::remove_all(SkinPresetFolder(id),ignored);}
}

void RestoreSkinPresets()
{
    auto file=SkinPresetDocument();
    PresetModel::RestoreBuiltins(file);
    WriteDocument(SkinPresetFile(),file);
}

void ExportSkinPreset(const std::string& id,const std::filesystem::path& target)
{
    std::map<std::string,CharacterSkins::Bytes> images;
    const auto entry=Painted(SkinPresetEntry(id),images);
    std::map<std::string,CharacterSkins::Bytes> files;
    if(PresetModel::HasMesh(entry))files=PresetModelFiles(SkinPresetEntry(id));
    const auto text=PresetModel::ShareDocument(entry,images,files).dump(1);
    WriteBytes(target,text.data(),text.size());
}

Json ImportSkinPreset(const std::filesystem::path& source)
{
    const auto bytes=ReadBytes(source,128u<<20);
    Json document;
    try{document=Json::parse(bytes.begin(),bytes.end());}catch(const std::exception&){throw std::runtime_error("This is not an RE+ skin preset file.");}
    auto parts=PresetModel::ReadShared(document); // an old two-team file gives a spy and a merc preset
    const auto listed=SkinPresets()["entries"];
    struct Ready { Json entry; std::map<std::string,CharacterSkins::Bytes> images,files; };
    std::vector<Ready> ready;
    for(auto& shared:parts)
    {
        // A built-in shared as it is carries recipes: paint them, the user entry keeps pictures.
        shared.entry["id"]=std::string("builtin.")+shared.entry.value("team",std::string("spy"))+".shared";
        auto entry=Painted(shared.entry,shared.images);
        entry.erase("id");
        // A preset of the same name and team already listed (a built-in shared as it is) gets a number.
        const auto base=entry.value("name",std::string("Imported preset"));
        auto taken=[&](const std::string& name){return std::any_of(listed.begin(),listed.end(),[&](const Json& e){
            return Fold(e.value("name",std::string{}))==Fold(name) && e.value("team",std::string{})==entry.value("team",std::string{});});};
        for(int n=2;taken(entry.value("name",std::string{}));++n)entry["name"]=base+" ("+std::to_string(n)+")";
        ready.push_back({std::move(entry),std::move(shared.images),std::move(shared.files)});
    }
    auto file=SkinPresetDocument();
    Json saved=Json::array();
    for(auto& [entry,images,files]:ready)
    {
        auto stored=PresetModel::Save(file,entry);
        WritePictures(stored.at("id"),images);
        WriteModelFiles(stored.at("id"),files);
        saved.push_back(stored);
    }
    WriteDocument(SkinPresetFile(),file);
    return saved;
}
