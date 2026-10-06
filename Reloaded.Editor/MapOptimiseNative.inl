// Native side of Optimise Map Assets (MapOptimiseModel.h). All calls run on the
// UI thread.
//
// A release copy is made by moving the assets the map uses out of the chosen
// packs and into the map (or a package of the map's own) with the stock OBJ
// RENAME, saving the map into its release folder, and moving every asset back
// with UObject::Rename (0x10faef00): pointers to an object do not change when it
// moves, so the actors follow, and the working map and the packs end as they
// were. The saved copy's own import table is then the check: anything it still
// takes from a chosen pack (a native reference the scan cannot see) is moved
// too and the copy saved again.
namespace
{
    constexpr unsigned kObjectTransient=0x4000;
    // UPackage's changed flag: the editor asks to save every loaded package with
    // it set (the loop over the object list at 0x10e40e40).
    constexpr size_t kPackageDirty=0x30;
    Address OuterMost(Address o)
    {
        for(int i=0;i<64 && Read<Address>(o+0x18);++i)o=Read<Address>(o+0x18);
        return o;
    }
    // The asset an object belongs to: itself, or its owner, up to the object
    // that sits directly in a package or group.
    Address AssetOf(Address o)
    {
        for(int i=0;i<64;++i)
        {
            const auto outer=Read<Address>(o+0x18);
            if(!outer || IsA(outer,"Package"))return o;
            o=outer;
        }
        return o;
    }
    // Package files by name: the folders the game loads packages from, in the
    // order a name is most likely meant.
    std::map<std::string,std::filesystem::path> OptimisePackageFiles()
    {
        const auto root=Directory().parent_path().parent_path();
        std::map<std::string,std::filesystem::path> files;
        const char* extensions[]={".usx",".utx",".uax",".uas",".ukx",".utc",".u"};
        const std::filesystem::path folders[]={root/"Packages"/"StaticMeshes",root/"Packages"/"Textures",root/"Packages"/"Sounds",root/"Packages"/"Animations",root/"System"};
        for(const char* extension:extensions)
            for(const auto& folder:folders)
            {
                std::error_code error;
                for(std::filesystem::directory_iterator it(folder,error),end;!error && it!=end;it.increment(error))
                    if(it->is_regular_file() && Fold(it->path().extension().string())==extension)
                        files.emplace(Fold(it->path().stem().string()),it->path());
            }
        return files;
    }
    // Every asset outside the map and the game's code packages that the map
    // reaches: from its actors and its BSP surfaces, through every object
    // reference they hold and the ones those hold in turn.
    struct UsedObject { Address object; MapOptimise::Asset asset; };
    // inside, when given, collects the assets the map already carries itself.
    std::vector<UsedObject> OptimiseUsed(const std::map<std::string,std::filesystem::path>& files,std::vector<UsedObject>* inside=nullptr)
    {
        const auto mapPackage=OuterMost(Level());
        std::map<Address,int> classKinds; // 0 walk, 1 skip
        std::map<Address,bool> packageSkipped;
        auto skipped=[&](Address o)
        {
            const auto c=Read<Address>(o+0x24);
            auto it=classKinds.find(c);
            if(it==classKinds.end())it=classKinds.emplace(c,(IsA(o,"Field") || IsA(o,"Package") || IsA(o,"Model") || IsA(o,"Polys"))?1:0).first;
            return it->second==1;
        };
        auto codePackage=[&](Address root)
        {
            auto it=packageSkipped.find(root);
            if(it!=packageSkipped.end())return it->second;
            const auto name=NameOf(root);
            bool skip=MapPackage::RuntimePackage(name) || Fold(name)=="transient";
            if(auto file=files.find(Fold(name));file!=files.end() && Fold(file->second.extension().string())==".u")skip=true;
            return packageSkipped.emplace(root,skip).first->second;
        };
        std::set<Address> visited,assets,carried;
        std::vector<Address> stack;
        for(auto a:LiveActors())stack.push_back(a);
        {
            // BSP surfaces' materials are not object properties of anything.
            auto model=Read<Address>(Level()+0x13c);
            if(model)for(auto surface:Array(model+0x94,0x2c))if(auto m=Read<Address>(surface+0x10))stack.push_back(m);
        }
        while(!stack.empty())
        {
            const auto o=stack.back();stack.pop_back();
            if(!o || !visited.insert(o).second)continue;
            if(visited.size()>500000)throw std::runtime_error("The map reaches too many objects to scan.");
            if(skipped(o))continue;
            const auto root=OuterMost(o);
            if(root!=mapPackage)
            {
                if(codePackage(root))continue;
                assets.insert(AssetOf(o));
            }
            else if(inside)
            {
                // An actor's parts belong to the level, which is not an asset.
                const auto asset=AssetOf(o);
                if(!IsA(asset,"Level") && !IsA(asset,"Actor") && !IsA(asset,"LevelSummary"))carried.insert(asset);
            }
            // One object whose properties cannot be read does not stop the scan:
            // anything missed shows in the saved copy's imports and moves then.
            try{for(const auto& ref:ReferencesOf(o,false))if(ref.target && !visited.count(ref.target))stack.push_back(ref.target);}
            catch(const std::exception& e){Logger::log("Optimise Map Assets: skipped the references of "+Path(o)+": "+e.what());}
        }
        std::vector<UsedObject> out;
        for(auto a:assets)out.push_back({a,{Path(a),NameOf(Read<Address>(a+0x24))}});
        if(inside)for(auto a:carried)inside->push_back({a,{Path(a),NameOf(Read<Address>(a+0x24))}});
        return out;
    }
    // The loaded objects of some packages by folded path, in one pass.
    std::map<std::string,Address> OptimiseIndex(const std::vector<std::string>& packs)
    {
        std::set<std::string> wanted;for(const auto& p:packs)wanted.insert(Fold(p));
        std::map<Address,bool> roots;
        std::map<std::string,Address> index;
        for(auto at:Array(0x11697B70))
        {
            const auto o=Read<Address>(at);if(!o)continue;
            const auto root=OuterMost(o);
            auto it=roots.find(root);
            if(it==roots.end())it=roots.emplace(root,wanted.count(Fold(NameOf(root)))!=0).first;
            if(it->second)index.emplace(Fold(Path(o)),o);
        }
        return index;
    }
    void OptimiseRename(Address object,const std::string& name,Address outer)
    {
        reinterpret_cast<void(__thiscall*)(void*,const char*,void*)>(0x10faef00)(reinterpret_cast<void*>(object),name.c_str(),reinterpret_cast<void*>(outer));
    }
    void OptimiseTransient(Address object,bool transient)
    {
        const auto flags=Read<unsigned>(object+0x1c);
        Write(object+0x1c,transient?flags|kObjectTransient:flags&~kObjectTransient);
    }
    // The map's interface package (<Map>-i: map-selection picture, loading
    // screens, briefing and map settings) is found by the map's name, which the
    // release copy shares. The stock save brings the working package up to date
    // (the map save does not write it), then its files are copied beside the
    // release. Returns the files written.
    std::vector<std::filesystem::path> OptimiseInterface(const std::string& mapName,const std::filesystem::path& root,const std::filesystem::path& folder)
    {
        const auto name=mapName+"-i";
        if(Find(name))Exec("SAVEMAPPROP MAP=\""+mapName+"\"");
        std::vector<std::filesystem::path> written;
        for(const char* extension:{".utc",".utx"})
        {
            const auto from=root/"Packages"/"Textures"/(name+extension),to=folder/"Packages"/"Textures"/(name+extension);
            std::error_code error;
            if(!std::filesystem::exists(from))continue;
            std::filesystem::create_directories(to.parent_path(),error);
            if(!std::filesystem::copy_file(from,to,std::filesystem::copy_options::overwrite_existing,error))
                throw std::runtime_error("Cannot copy "+from.string()+" to "+to.string()+": "+error.message());
            written.push_back(to);
        }
        return written;
    }
    bool OptimiseSave(const std::filesystem::path& file)
    {
        const auto path=file.string();
        if(!reinterpret_cast<int(__thiscall*)(void*,const char*)>(0x10E0416B)(reinterpret_cast<void*>(Engine()),path.c_str()))return false;
        LightmapFix::RepairSavedMap(path.c_str());
        return true;
    }
}
Json OptimiseReport(const std::filesystem::path& base)
{
    Engine();
    const auto root=Directory().parent_path().parent_path();
    if(!base.empty())
    {
        std::error_code error;
        if(!std::filesystem::is_directory(base/"Packages",error))throw std::runtime_error("The base install has no Packages folder: "+base.string());
        if(std::filesystem::equivalent(base,root,error))throw std::runtime_error("Choose a separate base install, not the one the editor is running from.");
    }
    const auto files=OptimisePackageFiles();
    std::vector<MapOptimise::Asset> used;
    std::vector<UsedObject> carried;
    for(const auto& u:OptimiseUsed(files,&carried))used.push_back(u.asset);
    Json inside=Json::array();
    for(const auto& c:carried)inside.push_back({{"path",c.asset.path},{"class",c.asset.className}});
    std::vector<std::string> unreadable;
    auto packs=MapOptimise::Report(used,[&](const std::string& name)->std::optional<std::pair<std::string,MapPackage::Tables>>
    {
        auto file=files.find(Fold(name));
        if(file==files.end())return std::nullopt;
        try{return std::make_pair(file->second.string(),MapPackage::ReadTables(file->second));}
        catch(const std::exception& e){unreadable.push_back(name+": "+e.what());return std::nullopt;}
    });
    if(!base.empty())
        MapOptimise::MarkInstalled(packs,[&](const MapOptimise::Pack& p)
        {
            return MapPackage::IdenticalFiles(p.file,base/std::filesystem::path(p.file).lexically_relative(root));
        });
    Json rows=Json::array();
    for(const auto& p:packs)
    {
        Json assets=Json::array();
        for(const auto& a:p.assets)assets.push_back({{"path",a.path},{"class",a.className},{"size",a.size},{"found",a.found}});
        rows.push_back({{"name",p.name},{"file",p.file},{"fileSize",p.fileSize},{"usedSize",p.usedSize},{"suggested",p.suggested},{"installed",p.installed},{"assets",assets}});
    }
    auto text=MapOptimise::Text(packs);
    if(!carried.empty())
    {
        text+="\r\nAlready inside the map ("+std::to_string(carried.size())+"):\r\n";
        for(const auto& c:carried)text+="  "+c.asset.path+" ("+c.asset.className+")\r\n";
    }
    return {{"map",NameOf(OuterMost(Level()))},{"mapFile",MapFile()},{"packs",rows},{"inside",inside},{"text",text},{"unreadable",unreadable}};
}
std::filesystem::path ReleaseFolder(const std::string& map)
{
    return Directory().parent_path().parent_path()/"Releases"/map;
}
Json OptimiseRelease(const Json& options)
{
    Engine();
    const auto root=Directory().parent_path().parent_path();
    std::vector<std::string> packs;
    for(const auto& p:options.at("packs"))packs.push_back(p.get<std::string>());
    if(packs.empty())throw std::runtime_error("Tick at least one package to take assets from.");
    const bool intoMap=options.value("destination",std::string("map"))=="map";
    const auto assetPackage=options.value("assetPackage",std::string());
    const bool overwrite=options.value("overwrite",false);
    if(!intoMap && !MapOptimise::ValidName(assetPackage))throw std::runtime_error("Name the asset package with letters, digits and underscores only.");
    const auto working=MapFile();
    if(working.empty())throw std::runtime_error("Save the map first: the release copy is made from it.");
    const auto mapPackage=OuterMost(Level());
    const auto mapName=std::filesystem::path(working).stem().string();
    // The game needs the map's own name (four letters and the mode's), so the
    // release copy keeps it and goes into a folder laid out like the game's:
    // the editor writes the playable copy into the Maps folder beside MapsEd.
    const std::filesystem::path folder=options.value("folder",std::string()).empty()?ReleaseFolder(mapName):std::filesystem::path(options.at("folder").get<std::string>());
    {
        std::error_code error;
        if(std::filesystem::equivalent(folder,root,error))throw std::runtime_error("The release folder cannot be the game folder: your working map would be replaced.");
        // The engine reads "[" in a package path as a platform tag.
        if(folder.string().find('[')!=std::string::npos)throw std::runtime_error("The release folder's path cannot contain [: "+folder.string());
    }
    for(const auto& p:packs)if(!intoMap && Fold(p)==Fold(assetPackage))throw std::runtime_error("The asset package name is the name of a package the map uses: "+p);
    const auto files=OptimisePackageFiles();
    if(!intoMap && Fold(assetPackage)==Fold(mapName))throw std::runtime_error("Give the asset package a name of its own.");
    const auto releaseSource=folder/"Packages"/"MapsEd"/(mapName+".sdc"),releasePlayable=folder/"Packages"/"Maps"/(mapName+".sdc");
    const auto assetFile=folder/"Packages"/"StaticMeshes"/(assetPackage+".usx");
    std::vector<std::filesystem::path> existing;
    for(const auto& f:{releaseSource,releasePlayable,folder/"Packages"/"Textures"/(mapName+"-i.utc"),folder/"Packages"/"Textures"/(mapName+"-i.utx")})
        if(std::filesystem::exists(f))existing.push_back(f);
    if(!intoMap)
    {
        if(std::filesystem::exists(assetFile))existing.push_back(assetFile);
        if(auto other=files.find(Fold(assetPackage));other!=files.end())
            throw std::runtime_error("There is already a package called "+assetPackage+" ("+other->second.string()+"). Choose another name.");
    }
    if(!existing.empty() && !overwrite)
    {
        std::string list;for(const auto& f:existing)list+="\n"+f.string();
        throw std::runtime_error("These files exist already:"+list);
    }
    for(const auto& f:existing)
        if(std::error_code error;!std::filesystem::remove(f,error) && error)throw std::runtime_error("Cannot replace "+f.string()+": "+error.message());
    {
        std::error_code error;
        for(const char* sub:{"MapsEd","Maps"})std::filesystem::create_directories(folder/"Packages"/sub,error);
        if(!intoMap)std::filesystem::create_directories(assetFile.parent_path(),error);
        if(!std::filesystem::is_directory(folder/"Packages"/"Maps"))throw std::runtime_error("Cannot make the release folder "+folder.string()+": "+error.message());
    }
    // The working map is saved as it is: the release copy is made from it, and
    // the editor would otherwise count the copy's save as saving the map.
    if(!OptimiseSave(working))throw std::runtime_error("The editor could not save the map.");
    const auto interfaceFiles=OptimiseInterface(mapName,root,folder);
    const auto destinationName=intoMap?NameOf(mapPackage):assetPackage;
    struct Moved { Address object; std::string name; Address outer; MapOptimise::Move move; Address newOuter=0; };
    struct State
    {
        std::vector<Moved> moved;
        std::string working;
        Address mapPackage=0;
        std::string assetPackage;
        // The packs' changed flags before their assets moved: moving them out and
        // back changes nothing, so the editor should not ask to save them.
        std::vector<std::pair<Address,unsigned>> dirty;
        ~State()
        {
            try
            {
                for(auto it=moved.rbegin();it!=moved.rend();++it)OptimiseRename(it->object,it->name,it->outer);
                // Groups made for the move stay behind empty: inside the map they
                // are not saved with it, and the asset package is put out of the
                // way so loading the release copy reads its file.
                for(const auto& m:moved)
                    for(auto group=m.newOuter;group && group!=mapPackage;group=Read<Address>(group+0x18))OptimiseTransient(group,true);
                if(!assetPackage.empty())
                    if(auto package=Find(assetPackage))
                    {
                        OptimiseTransient(package,true);
                        OptimiseRename(package,assetPackage+"_Moved_"+Id().substr(0,8),0);
                        Write(package+kPackageDirty,0u);
                    }
                for(const auto& [package,flag]:dirty)Write(package+kPackageDirty,flag);
                SetMapFile(working);
            }
            catch(const std::exception& e){Logger::log(std::string("Optimise Map Assets: could not put every asset back: ")+e.what());}
        }
    } state;
    state.working=working;state.mapPackage=mapPackage;
    if(!intoMap)state.assetPackage=assetPackage;
    for(const auto& p:packs)if(auto package=Find(p))state.dirty.emplace_back(package,Read<unsigned>(package+kPackageDirty));

    std::vector<Address> pending;
    for(const auto& u:OptimiseUsed(files))
        if(std::any_of(packs.begin(),packs.end(),[&](const std::string& p){return Fold(p)==Fold(MapOptimise::RootPackage(u.asset.path));}))
            pending.push_back(u.object);
    // An asset inside another moved asset moves with it.
    std::sort(pending.begin(),pending.end());
    pending.erase(std::remove_if(pending.begin(),pending.end(),[&](Address o)
    {
        for(auto outer=Read<Address>(o+0x18);outer;outer=Read<Address>(outer+0x18))
            if(std::binary_search(pending.begin(),pending.end(),outer))return true;
        return false;
    }),pending.end());
    if(pending.empty())throw std::runtime_error("The map uses nothing from the ticked packages.");
    std::vector<std::string> left;
    MapPackage::Tables saved;
    int rounds=0;
    for(;rounds<6;++rounds)
    {
        for(auto o:pending)
        {
            if(std::any_of(state.moved.begin(),state.moved.end(),[&](const Moved& m){return m.object==o;}))continue;
            const auto move=MapOptimise::PlanMove(Path(o),destinationName);
            Moved record{o,NameOf(o),Read<Address>(o+0x18),move};
            Exec(MapOptimise::RenameCommand(move));
            if(Fold(Path(o))!=Fold(move.NewPath()))throw std::runtime_error("The editor did not move "+move.path+".");
            record.newOuter=Read<Address>(o+0x18);
            state.moved.push_back(record);
            // Groups left transient by an earlier run would not be saved.
            for(auto group=record.newOuter;group && group!=mapPackage;group=Read<Address>(group+0x18))OptimiseTransient(group,false);
        }
        pending.clear();
        if(!intoMap)
        {
            if(!Exec("OBJ SAVEPACKAGE PACKAGE=\""+assetPackage+"\" FILE=\""+assetFile.string()+"\"") || !std::filesystem::exists(assetFile))
                throw std::runtime_error("The editor could not save "+assetFile.string()+".");
        }
        if(!OptimiseSave(releaseSource))throw std::runtime_error("The editor could not save the release copy.");
        const auto check=std::filesystem::exists(releasePlayable)?releasePlayable:releaseSource;
        saved=MapPackage::ReadTables(check);
        left=MapOptimise::StillImported(saved,packs);
        if(!intoMap)for(const auto& p:MapOptimise::StillImported(MapPackage::ReadTables(assetFile),packs))left.push_back(p);
        if(left.empty())break;
        const auto index=OptimiseIndex(packs);
        for(const auto& path:left)
        {
            auto found=index.find(Fold(path));
            if(found==index.end())continue;
            const auto o=AssetOf(found->second);
            if(std::none_of(state.moved.begin(),state.moved.end(),[&](const Moved& m){return m.object==o;}) && std::find(pending.begin(),pending.end(),o)==pending.end())pending.push_back(o);
        }
        if(pending.empty())break;
    }
    Json moved=Json::array();
    for(const auto& m:state.moved)moved.push_back({{"from",m.move.path},{"to",m.move.NewPath()}});
    Json written=Json::array();
    auto note=[&](const std::filesystem::path& f){if(std::filesystem::exists(f))written.push_back({{"file",f.string()},{"size",std::filesystem::file_size(f)}});};
    note(releaseSource);note(releasePlayable);
    for(const auto& f:interfaceFiles)note(f);
    std::vector<std::string> needs=MapOptimise::ImportedPackages(saved);
    if(!intoMap)
    {
        note(assetFile);
        for(const auto& p:MapOptimise::ImportedPackages(MapPackage::ReadTables(assetFile)))
            if(Fold(p)!=Fold(assetPackage) && std::none_of(needs.begin(),needs.end(),[&](const std::string& n){return Fold(n)==Fold(p);}))needs.push_back(p);
    }
    // Looked up again: the asset package file is new.
    const auto now=OptimisePackageFiles();
    Json packages=Json::array();
    for(const auto& p:needs)
    {
        auto file=now.find(Fold(p));
        packages.push_back({{"name",p},{"file",file==now.end()?std::string():file->second.string()},{"size",file==now.end()?0:std::filesystem::file_size(file->second)}});
    }
    return {{"moved",moved},{"written",written},{"needs",packages},{"left",left},{"rounds",rounds+1},{"release",mapName},{"folder",folder.string()},{"playable",releasePlayable.string()},{"intoMap",intoMap}};
}
