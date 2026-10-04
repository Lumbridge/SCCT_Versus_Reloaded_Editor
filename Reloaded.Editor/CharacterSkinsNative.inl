// Character Skins (CharacterSkinsModel.h): compiles ReloadedCharacterSkins into the
// open map's package and edits the one actor of it. Included by WorkflowEditor.cpp.
//
// The stock compiler cannot build a map class unaided in this build:
// - CLASS LOAD's UClassFactoryUC accepts only the type UC_ (0x115e8620), so the
//   source is written as *.uc_.
// - The class it makes has no Defaults; MakeScripts backs every object up
//   (0x11023e20) and serializes the class against its parent's Defaults. Copy the
//   parent's Defaults (+0xb0) and PropertiesSize (+0x34).
// - Its replication block position (+0x4c, line +0x50) is 0, not INDEX_NONE, so the
//   second pass would parse a block that does not exist ("Missing 'Reliable'").
// - SCRIPT MAKE clears CLASS_Parsed|CLASS_Compiled (0x12 at +0x8c) on every class
//   and reparses all of them, and the shipped classes' sources are stripped. While
//   it runs, ParseScripts (0x11032960) re-marks every other class at its first call;
//   their ScriptText (+0x44) must stay, or the walk does not descend to the map class.
//   The flags are put back afterwards.
namespace
{
    constexpr Address kParseScripts=0x11032960, kWarnDevice=0x115BEFB0, kMalloc=0x115ac708;
    std::vector<Address> LoadedClasses()
    {
        std::vector<Address> out;
        for(auto at:Array(0x11697B70))
        {
            auto o=Read<Address>(at);if(!o)continue;
            auto c=Read<Address>(o+0x24);
            if(c && NameOf(c)=="Class")out.push_back(o);
        }
        return out;
    }
    Address SkinClass() { return Find(LevelPath()+"."+CharacterSkins::ClassName); }
    bool SkinClassCompiled(Address c)
    {
        if(!c || !(Read<unsigned>(c+0x8c)&2))return false;
        for(const auto& slot:CharacterSkins::Slots)if(!StructField(c,slot.property))return false;
        return true;
    }
    std::vector<Address> SkinActors()
    {
        std::vector<Address> out;auto c=SkinClass();
        if(c)for(auto a:LiveActors())if(Read<Address>(a+0x24)==c)out.push_back(a);
        return out;
    }

    struct SkinCompile
    {
        static inline Address target=0;
        static inline bool remarked=false;
        static inline std::vector<std::pair<Address,unsigned>> flags;
        static inline unsigned char* trampoline=nullptr;
        static inline void(__thiscall* serialize)(void*,const char*,int)=nullptr;
        static inline std::string errors;
        static int __cdecl Parse(void* stack,void* compiler,Address cls,int makeAll,int booting,int subclasses)
        {
            if(!remarked)
            {
                remarked=true;
                for(const auto& [c,f]:flags)if(c!=target)*reinterpret_cast<unsigned*>(c+0x8c)=f|0x12;
            }
            return reinterpret_cast<int(__cdecl*)(void*,void*,Address,int,int,int)>(trampoline)(stack,compiler,cls,0,booting,subclasses);
        }
        static void __fastcall Capture(void* self,void*,const char* text,int event)
        {
            if(text && (strstr(text,"Error") || strstr(text,"error")) && errors.size()<4000)errors+=std::string(text)+"\n";
            serialize(self,text,event);
        }
    };

    void CompileSkinClass()
    {
        auto c=SkinClass();
        if(!c)
        {
            char folder[MAX_PATH]{};GetTempPathA(MAX_PATH,folder);
            auto file=std::filesystem::path(folder)/(std::string(CharacterSkins::ClassName)+".uc_");
            {std::ofstream out(file,std::ios::binary);out<<CharacterSkins::Script();if(!out)throw std::runtime_error("Cannot write the Character Skins script to "+file.string());}
            Exec("CLASS LOAD FILE=\""+file.string()+"\" PACKAGE="+Path(Read<Address>(Level()+0x18))+" NAME="+CharacterSkins::ClassName);
            std::error_code ignored;std::filesystem::remove(file,ignored);
            c=SkinClass();
            if(!c)throw std::runtime_error("The editor did not import the Character Skins script.");
        }
        if(SkinClassCompiled(c))return;
        auto parent=Read<Address>(c+0x28);
        if(!parent || NameOf(parent)!="Info")throw std::runtime_error("The map's ReloadedCharacterSkins class is not an Info. Remove it from the map package first.");
        if(!Read<Address>(c+0xb0))
        {
            auto data=Read<Address>(parent+0xb0);int count=Read<int>(parent+0xb4);
            if(!data || count<=0 || count>0x10000 || count!=Read<int>(parent+0x34))throw std::runtime_error("Unexpected Info class layout.");
            auto malloc=Read<Address>(kMalloc);
            auto copy=reinterpret_cast<void*(__thiscall*)(void*,unsigned,const char*)>(Read<Address>(Read<Address>(malloc)))(reinterpret_cast<void*>(malloc),count,"CharacterSkins");
            if(!copy)throw std::runtime_error("Out of memory.");
            memcpy(copy,reinterpret_cast<void*>(data),count);
            Write(c+0xb0,reinterpret_cast<Address>(copy));Write(c+0xb4,count);Write(c+0xb8,count);Write(c+0x34,count);
        }
        Write(c+0x4c,-1);Write(c+0x50,-1);

        static const unsigned char prologue[5]={0x55,0x8b,0xec,0x6a,0xff};
        if(memcmp(reinterpret_cast<void*>(kParseScripts),prologue,5))throw std::runtime_error("Unsupported editor build: the script compiler differs.");
        if(!SkinCompile::trampoline)
        {
            auto t=static_cast<unsigned char*>(VirtualAlloc(nullptr,16,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
            if(!t)throw std::runtime_error("Out of memory.");
            memcpy(t,prologue,5);t[5]=0xe9;
            const int back=static_cast<int>(kParseScripts+5-reinterpret_cast<Address>(t+10));memcpy(t+6,&back,4);
            SkinCompile::trampoline=t;
        }
        SkinCompile::target=c;SkinCompile::remarked=false;SkinCompile::errors.clear();SkinCompile::flags.clear();
        for(auto k:LoadedClasses())SkinCompile::flags.push_back({k,Read<unsigned>(k+0x8c)});
        unsigned char jump[5]={0xe9};
        const int to=static_cast<int>(reinterpret_cast<Address>(&SkinCompile::Parse)-kParseScripts-5);memcpy(jump+1,&to,4);
        auto warn=Read<Address>(kWarnDevice);auto slot=Read<Address>(warn);
        SkinCompile::serialize=Read<decltype(SkinCompile::serialize)>(slot);
        auto capture=&SkinCompile::Capture;
        if(!MemoryWriter::WriteBytes(kParseScripts,jump,5))throw std::runtime_error("Cannot prepare the script compiler.");
        const bool captured=MemoryWriter::WriteBytes(slot,&capture,sizeof(capture));
        struct Restore
        {
            Address slot;bool captured;
            ~Restore()
            {
                MemoryWriter::WriteBytes(kParseScripts,prologue,5);
                if(captured)MemoryWriter::WriteBytes(slot,&SkinCompile::serialize,sizeof(SkinCompile::serialize));
                for(const auto& [k,f]:SkinCompile::flags)if(k!=SkinCompile::target)*reinterpret_cast<unsigned*>(k+0x8c)=f;
            }
        } restore{slot,captured};
        Exec("SCRIPT MAKE");
        if(!SkinClassCompiled(c))
            throw std::runtime_error("The Character Skins script did not compile."+(SkinCompile::errors.empty()?std::string(" See the editor log."):"\n"+SkinCompile::errors));
    }
}

Json CharacterSkinSettings()
{
    Json slots=Json::object();
    auto actors=SkinActors();
    for(const auto& slot:CharacterSkins::Slots)
    {
        std::string path;
        if(!actors.empty())
        {
            auto p=Property(actors[0],slot.property);
            if(p)path=Path(Read<Address>(actors[0]+Read<int>(p+0x3c)));
        }
        slots[slot.property]=path;
    }
    return {{"placed",!actors.empty()},{"extra",actors.size()>1},{"compiled",SkinClassCompiled(SkinClass())},{"slots",slots}};
}

Json ApplyCharacterSkins(const Json& slots)
{
    std::vector<std::pair<const CharacterSkins::Slot*,std::string>> values;
    for(const auto& slot:CharacterSkins::Slots)
    {
        auto path=slots.value(slot.property,std::string{});
        if(!CharacterSkins::ValidPath(path))throw std::runtime_error(std::string(slot.label)+": not a material path: "+path);
        std::string text="None";
        if(!path.empty())
        {
            auto material=Find(path);
            if(!material)throw std::runtime_error(std::string(slot.label)+": "+path+" is not loaded. Open its package in the Texture Browser first.");
            if(!IsA(material,"Material"))throw std::runtime_error(std::string(slot.label)+": "+path+" is not a material.");
            text=CharacterSkins::PropertyText(NameOf(Read<Address>(material+0x24)),Path(material));
        }
        values.push_back({&slot,text});
    }
    CompileSkinClass();
    auto actors=SkinActors();
    if(actors.empty())
    {
        if(!Exec(std::string("ACTOR ADD CLASS=")+CharacterSkins::ClassName))throw std::runtime_error("The editor refused to place the Character Skins actor.");
        actors=SkinActors();
        if(actors.empty())throw std::runtime_error("The editor did not place the Character Skins actor.");
    }
    auto actor=actors[0];
    Transaction transaction("Character skins");
    Modify(actor);
    for(const auto& [slot,text]:values)
    {
        auto p=Property(actor,slot->property);
        if(!p)throw std::runtime_error(std::string("The map's Character Skins class has no ")+slot->property+".");
        MagicImport(p,actor+Read<int>(p+0x3c),Json(text));
    }
    Call(actor,0x44);
    transaction.Commit();
    Redraw();
    return CharacterSkinSettings();
}

void RemoveCharacterSkins()
{
    auto actors=SkinActors();
    if(actors.empty())return;
    Json doomed=Json::array();for(auto a:actors)doomed.push_back(Identity(a));
    auto previous=SelectedIdentities();
    Select(doomed);
    if(!Exec("ACTOR DELETE"))throw std::runtime_error("The editor refused to delete the Character Skins actor.");
    Json remaining=Json::array();
    for(const auto& kept:previous)if(std::none_of(doomed.begin(),doomed.end(),[&](const Json& d){return d.at("path")==kept.at("path");}))remaining.push_back(kept);
    Select(remaining);Redraw();
}

// The stock textures behind the four slots as 32-bit TGA files in folder, named after
// their slots (SpyBody.tga, ...). Read from SPersoTextures.utx; the editor is not used.
Json ExportDefaultCharacterSkins(const std::filesystem::path& folder)
{
    const auto source=Directory().parent_path().parent_path()/"Packages"/"Textures"/"SPersoTextures.utx";
    std::ifstream input(source,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read "+source.string());
    CharacterSkins::Bytes package((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    std::error_code error;std::filesystem::create_directories(folder,error);
    Json written=Json::array();
    for(const auto& slot:CharacterSkins::Slots)
    {
        const auto image=CharacterSkins::Decode(CharacterSkins::ReadMip(package,slot.texture));
        const auto tga=CharacterSkins::Tga(image);
        const auto file=folder/(std::string(slot.property)+".tga");
        std::ofstream out(file,std::ios::binary);
        out.write(reinterpret_cast<const char*>(tga.data()),static_cast<std::streamsize>(tga.size()));
        if(!out)throw std::runtime_error("Cannot write "+file.string());
        written.push_back({{"slot",slot.property},{"file",file.u8string()},{"width",image.width},{"height",image.height}});
    }
    return written;
}

// Imports an edited image into the map package (group CharacterSkins, named after the
// slot) and compresses it the way the stock texture is. Returns its object path.
std::string ImportCharacterSkin(const std::string& property,const std::filesystem::path& file)
{
    auto slot=std::find_if(CharacterSkins::Slots.begin(),CharacterSkins::Slots.end(),[&](const CharacterSkins::Slot& s){return property==s.property;});
    if(slot==CharacterSkins::Slots.end())throw std::runtime_error("Unknown Character Skins slot "+property);
    const auto extension=Authoring::Extension(file.string());
    if(!CharacterSkins::Importable(extension))throw std::runtime_error("Import a .tga, .bmp, .pcx or .dds image.");
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot read "+file.string());
    std::string header(64,'\0');input.read(header.data(),header.size());header.resize(static_cast<size_t>(input.gcount()));
    std::pair<uint32_t,uint32_t> size;
    try{size=Authoring::ImageSize(header,extension);}catch(const std::exception& e){throw std::runtime_error(file.string()+": "+e.what());}
    const auto package=Path(Read<Address>(Level()+0x18));
    std::string name=slot->property;
    for(int suffix=2;Find(package+".CharacterSkins."+name);++suffix)name=std::string(slot->property)+std::to_string(suffix);
    const auto path=package+".CharacterSkins."+name;
    Exec("TEXTURE IMPORT FILE=\""+file.string()+"\" NAME=\""+name+"\" PACKAGE=\""+package+"\" GROUP=\"CharacterSkins\" MIPS=1");
    auto texture=Find(path);
    if(!texture || !IsA(texture,"Texture"))throw std::runtime_error("The editor's texture importer refused "+file.string());
    if(extension!=".dds")
    {
        int chain=1;for(auto side=(std::max)(size.first,size.second);side>1;side>>=1)++chain;
        Exec("TEXTURE COMPRESS NAME="+path+" FORMAT="+slot->format+" MaxMips="+std::to_string(chain));
    }
    if(AuthoringValue(texture,"USize")!=static_cast<int>(size.first) || AuthoringValue(texture,"VSize")!=static_cast<int>(size.second))
        throw std::runtime_error(path+" did not import at the file's "+std::to_string(size.first)+"x"+std::to_string(size.second)+".");
    return path;
}
