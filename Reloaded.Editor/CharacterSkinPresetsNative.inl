// Character Skin presets (CharacterSkinPresetsModel.h): the library in
// skin_presets.json beside library.json, each user preset's pictures in
// skin_presets\<id>\<Slot>.tga, and the built-ins compiled into the DLL, which are
// painted over the stock textures when shown or applied and never written.
// Applying goes through ImportCharacterSkin and ApplyCharacterSkins, so a preset
// lands in the map exactly as the Character Skins window's own Import and Apply do.
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
    Json SkinPresetDocument()
    {
        try { return PresetModel::Document(ReadDocument(SkinPresetFile(),PresetModel::EmptyDocument())); }
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
}

Json SkinPresets()
{
    Json file;Json problems=Json::array();
    try{file=SkinPresetDocument();for(auto& p:PresetModel::Problems(file))problems.push_back(p);}
    catch(const std::exception& e){file=PresetModel::EmptyDocument();problems.push_back(e.what());}
    auto entries=PresetModel::Merge(PresetModel::Builtins(),file);
    return {{"entries",entries},{"categories",PresetModel::Categories(entries)},{"problems",problems}};
}

std::vector<CharacterSkins::Image> SkinPresetImages(const Json& entry,int size)
{
    std::vector<CharacterSkins::Image> out;
    const auto slots=entry.value("slots",Json::object());
    for(const auto& slot:CharacterSkins::Slots)
    {
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
    const auto package=Path(Read<Address>(Level()+0x18));
    Json slots=Json::object(),models=Json::object(),goggles=Json::object();
    for(const auto& slot:CharacterSkins::Slots)
    {
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
            }
        }
        slots[slot.property]=path;
    }
    const auto presetModels=entry.value("models",Json::object()),presetGoggles=entry.value("goggles",Json::object());
    for(const auto& model:CharacterSkins::Models)
    {
        models[model.property]=presetModels.value(model.property,std::string{});
        goggles[model.goggles]=presetGoggles.contains(model.goggles)?presetGoggles.at(model.goggles):Json::array({0,0,0});
    }
    return ApplyCharacterSkins(slots,models,goggles);
}

Json SaveSkinPreset(const Json& slots,const Json& models,const Json& goggles,const Json& details)
{
    auto file=SkinPresetDocument();
    const auto package=Fold(Path(Read<Address>(Level()+0x18)));
    std::set<std::string> local;std::map<std::string,CharacterSkins::Bytes> images;
    for(const auto& slot:CharacterSkins::Slots)
    {
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
    auto entry=PresetModel::Capture(slots,models,goggles,local);
    for(const char* key:{"name","category","description"})if(details.contains(key))entry[key]=details.at(key);
    auto saved=PresetModel::Save(file,entry);
    WritePictures(saved.at("id"),images);
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
    const auto text=PresetModel::ShareDocument(entry,images).dump(1);
    WriteBytes(target,text.data(),text.size());
}

Json ImportSkinPreset(const std::filesystem::path& source)
{
    const auto bytes=ReadBytes(source,128u<<20);
    Json document;
    try{document=Json::parse(bytes.begin(),bytes.end());}catch(const std::exception&){throw std::runtime_error("This is not an RE+ skin preset file.");}
    auto shared=PresetModel::ReadShared(document);
    // A built-in shared as it is carries recipes: paint them, the user entry keeps pictures.
    shared.entry["id"]="builtin.shared";
    auto entry=Painted(shared.entry,shared.images);
    entry.erase("id");
    // A preset of the same name already listed (a built-in shared as it is) gets a number.
    const auto listed=SkinPresets()["entries"];
    const auto base=entry.value("name",std::string("Imported preset"));
    auto taken=[&](const std::string& name){return std::any_of(listed.begin(),listed.end(),[&](const Json& e){return Fold(e.value("name",std::string{}))==Fold(name);});};
    for(int n=2;taken(entry.value("name",std::string{}));++n)entry["name"]=base+" ("+std::to_string(n)+")";
    auto file=SkinPresetDocument();
    auto saved=PresetModel::Save(file,entry);
    WritePictures(saved.at("id"),shared.images);
    WriteDocument(SkinPresetFile(),file);
    return saved;
}
