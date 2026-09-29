// Included inside Workflow::Editor. The Emitter Library: emitters saved as
// canonical assembly definitions (schema in EmitterLibraryModel.h) in
// emitter_library.json beside library.json, merged with the built-in entries
// compiled into the DLL (EmitterLibraryDefaults.gen.h), which are never written,
// and the read-only effect packs in EmitterPacks\*.json.
namespace
{
    unsigned emitterLibraryWrites=0,emitterPackReads=0;
    std::filesystem::path EmitterLibraryFile() { return Directory()/"emitter_library.json"; }
    std::filesystem::path EmitterPacksDirectory() { return Directory()/"EmitterPacks"; }
    // The user file for editing. One that cannot be read or parsed refuses
    // with the model's sentence rather than the parser's message.
    Json EmitterFile()
    {
        try { return Workflow::EmitterLibrary::Document(ReadDocument(EmitterLibraryFile(),Workflow::EmitterLibrary::EmptyDocument())); }
        catch(const std::exception&) { throw std::runtime_error(Workflow::EmitterLibrary::DamagedFile()); }
    }
    // For listing, a damaged user file hides only its own entries: the
    // built-ins and packs stay listed and placeable, and EmitterLibraryProblems says why.
    Json ListedEmitterFile(bool& damaged)
    {
        damaged=false; try { return EmitterFile(); } catch(const std::exception&) { damaged=true; return Workflow::EmitterLibrary::EmptyDocument(); }
    }
    void WriteEmitterFile(const Json& file) { WriteDocument(EmitterLibraryFile(),file);++emitterLibraryWrites; }
    const Json& BuiltinEmitters() { static const Json builtins=Workflow::EmitterLibrary::Builtins(Workflow::EmitterLibrary::DefaultsText()); return builtins; }
    std::string PathText(const std::filesystem::path& path)
    {
        const auto wide=path.wstring(); if(wide.empty()) return {};
        const int count=WideCharToMultiByte(CP_UTF8,0,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr);
        std::string text(count,'\0'); WideCharToMultiByte(CP_UTF8,0,wide.data(),static_cast<int>(wide.size()),text.data(),count,nullptr,nullptr); return text;
    }
    // Size and modification time: a file that changes gets a new stamp.
    std::string Stamp(const std::filesystem::path& path)
    {
        std::error_code error; const auto size=std::filesystem::file_size(path,error); if(error) return "-";
        const auto time=std::filesystem::last_write_time(path,error); if(error) return "-";
        return std::to_string(size)+"@"+std::to_string(time.time_since_epoch().count());
    }
    // A pack file that could not be opened or read, which may succeed when tried again.
    struct PackReadError : std::runtime_error { using std::runtime_error::runtime_error; };
    Json ReadPackFile(const std::filesystem::path& path,const std::string& name)
    {
        ++emitterPackReads;
        std::error_code error; const auto size=std::filesystem::file_size(path,error);
        if(error) throw PackReadError("Effect pack "+name+" is not listed: it cannot be read.");
        if(size>64ull*1024*1024) throw std::runtime_error("Effect pack "+name+" is not listed: it is larger than 64 MB.");
        std::string text(static_cast<size_t>(size),'\0');
        {
            std::ifstream input(path,std::ios::binary);
            if(!input || !input.read(text.data(),static_cast<std::streamsize>(text.size()))) throw PackReadError("Effect pack "+name+" is not listed: it cannot be read (another program may be writing it).");
        }
        Json document;
        try { document=Json::parse(text); }
        catch(const Json::parse_error& e) { throw std::runtime_error("Effect pack "+name+" is not listed: it is not valid JSON (error at byte "+std::to_string(e.byte)+")."); }
        catch(const std::exception&) { throw std::runtime_error("Effect pack "+name+" is not listed: it is not valid JSON."); }
        std::string().swap(text);
        return Workflow::EmitterLibrary::ReadPack(std::move(document),name);
    }
    // One read of every pack file (kept while its stamp is unchanged) and one merged
    // list per revision: a revision starts when emitter_library.json or a file in
    // EmitterPacks changes, or this editor writes the user file. A file that could
    // not be read (another program writing it) is tried again on its own, at most
    // every 500 ms and 10 times; the merged list is rebuilt only once it reads. After
    // that it is tried again when its stamp changes or the next revision starts.
    struct PackFile { std::filesystem::path path; std::string name,stamp,error; Json pack; int retries=0; bool unreadable=false; };
    struct LibraryCache
    {
        std::string signature; std::vector<PackFile> files; std::vector<const Json*> packs;
        EmitterLibraryView view; unsigned revision=0; bool retry=false; unsigned long long retryAt=0;
    };
    // Reads a pack file into file; true unless it could not be read (which may pass when tried again).
    bool ReadInto(PackFile& file,int tried)
    {
        file.pack=Json(); file.error.clear(); file.retries=0; file.unreadable=false;
        try { file.pack=ReadPackFile(file.path,file.name); }
        catch(const PackReadError& e) { file.error=e.what(); file.unreadable=true; file.retries=tried<10?tried+1:0; return false; }
        catch(const std::exception& e) { file.error=e.what(); }
        return true;
    }
    LibraryCache& Cache() { static LibraryCache cache; return cache; }
    const LibraryCache& Library()
    {
        auto& cache=Cache();
        std::vector<std::pair<std::string,std::filesystem::path>> found;
        std::error_code error; const auto directory=EmitterPacksDirectory();
        if(std::filesystem::is_directory(directory,error))
            for(std::filesystem::directory_iterator it(directory,error),end;!error && it!=end;it.increment(error))
            {
                std::error_code kind; const auto& path=it->path();
                if(Fold(PathText(path.extension()))==".json" && it->is_regular_file(kind)) found.push_back({Fold(PathText(path.filename())),path});
            }
        std::sort(found.begin(),found.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        std::string signature=Stamp(EmitterLibraryFile())+"#"+std::to_string(emitterLibraryWrites);
        std::vector<std::string> stamps;
        for(const auto& [key,path]:found) {stamps.push_back(Stamp(path));signature+="|"+key+"="+stamps.back();}
        if(cache.view.entries && signature==cache.signature)
        {
            if(!cache.retry || GetTickCount64()<cache.retryAt) return cache;
            bool read=false;
            for(auto& file:cache.files) if(file.retries>0) read|=ReadInto(file,file.retries);
            cache.retry=std::any_of(cache.files.begin(),cache.files.end(),[](const PackFile& f){return f.retries>0;});
            cache.retryAt=GetTickCount64()+500;
            if(!read) return cache;
        }

        std::vector<PackFile> files;
        for(size_t i=0;i<found.size();++i)
        {
            PackFile file{found[i].second,PathText(found[i].second.filename()),stamps[i]};
            auto kept=std::find_if(cache.files.begin(),cache.files.end(),[&](const PackFile& f){return f.path==file.path && f.stamp==file.stamp && file.stamp!="-";});
            if(kept!=cache.files.end() && !kept->retries && !kept->unreadable) {file.error=std::move(kept->error);file.pack=std::move(kept->pack);}
            else ReadInto(file,kept!=cache.files.end()?kept->retries:0);
            files.push_back(std::move(file));
        }
        cache.files=std::move(files); cache.packs.clear();
        cache.retry=std::any_of(cache.files.begin(),cache.files.end(),[](const PackFile& f){return f.retries>0;});
        cache.retryAt=GetTickCount64()+500;
        // Two files with one pack id: the first by file name is listed.
        Json packProblems=Json::array(); std::map<std::string,std::string> owners; std::vector<const PackFile*> listed;
        for(const auto& file:cache.files)
        {
            if(file.pack.is_null()) {packProblems.push_back(file.error);continue;}
            const auto id=file.pack.at("id").get<std::string>();
            if(auto [owner,added]=owners.emplace(id,file.name);!added) {packProblems.push_back("Effect pack "+file.name+" is not listed: its pack id '"+id+"' is already used by "+owner->second+".");continue;}
            listed.push_back(&file);
            for(const auto& problem:file.pack.at("problems")) packProblems.push_back(problem);
        }
        std::stable_sort(listed.begin(),listed.end(),[](const PackFile* a,const PackFile* b)
        {
            const auto x=Fold(a->pack.at("name").get<std::string>()),y=Fold(b->pack.at("name").get<std::string>());
            return x!=y?x<y:a->pack.at("id").get<std::string>()<b->pack.at("id").get<std::string>();
        });
        for(const auto* file:listed) cache.packs.push_back(&file->pack);

        bool damaged; const auto userFile=ListedEmitterFile(damaged);
        auto entries=std::make_shared<Json>(Workflow::EmitterLibrary::Merge(BuiltinEmitters(),cache.packs,userFile));
        auto problems=std::make_shared<Json>(damaged?Json::array({Workflow::EmitterLibrary::DamagedFile()}):Workflow::EmitterLibrary::Problems(userFile));
        const size_t userProblems=problems->size();
        for(auto& problem:packProblems) problems->push_back(std::move(problem));
        std::set<std::string> hidden; for(const auto& id:userFile.at("hiddenPacks")) hidden.insert(id.get<std::string>());
        auto packs=std::make_shared<Json>(Json::array());
        for(const auto* file:listed)
        {
            auto summary=Workflow::EmitterLibrary::PackSummary(file->pack); size_t hiddenEntries=0;
            for(const auto& entry:file->pack.at("emitters")) hiddenEntries+=hidden.count(entry.at("id").get<std::string>());
            summary["file"]=PathText(file->path); summary["hidden"]=hiddenEntries; packs->push_back(std::move(summary));
        }
        cache.view.categories=std::make_shared<const Json>(Workflow::EmitterLibrary::Categories(*entries));
        cache.view.entries=std::move(entries); cache.view.problems=std::move(problems); cache.view.packs=std::move(packs);
        cache.view.userProblems=userProblems; cache.view.revision=++cache.revision; cache.signature=signature;
        return cache;
    }
    Json MissingPackageFiles(const Json& files)
    {
        Json missing=Json::array(); const auto packages=Directory().parent_path().parent_path()/"Packages";
        for(const auto& file:files)
        {
            // Requires lists hold plain ASCII file names (IsPackageFile).
            const auto name=file.get<std::string>(); std::error_code error;
            if(!std::filesystem::is_regular_file(packages/Workflow::EmitterLibrary::PackageFolder(name)/name,error)) missing.push_back(name);
        }
        return missing;
    }
    const Json* PackSummaryOf(const Json& entry)
    {
        if(!entry.is_object() || !entry.contains("pack") || !entry.at("pack").is_string()) return nullptr;
        for(const auto& pack:*Library().view.packs) if(pack.at("id")==entry.at("pack")) return &pack;
        return nullptr;
    }
    // The open map's file name for an entry's source. A raw MAP LOAD leaves
    // the frame's file name as it was, so callers that load maps that way
    // (the defaults generator) pass the name themselves.
    std::string MapLabel()
    {
        auto file=MapFile(); if(file.empty()) return "Untitled";
        auto name=file.substr(file.find_last_of("\\/")+1); return name.substr(0,name.find_last_of('.'));
    }
}
Json SelectedEmitters()
{
    Json result=Json::array(); auto live=LiveActors();
    for(size_t i=0;i<live.size();++i)
    {
        auto a=live[i];
        if(i>1 && (Read<unsigned>(a+0x2f4)&0x40) && IsA(a,"Emitter")) result.push_back(Identity(a));
    }
    return result;
}
Json CaptureEmitters(const Json& members,const std::string& map)
{
    if(!members.is_array() || members.empty()) throw std::runtime_error("Select one or more emitters first.");
    Vector low{},high{}; bool first=true,emitter=false;
    for(const auto& member:members)
    {
        auto a=ResolveIdentity(member); if(!a) throw std::runtime_error("A selected actor no longer exists. Select the emitters again.");
        emitter=emitter || IsA(a,"Emitter"); auto p=Position(a);
        if(first) {low=high=p;first=false;}
        else for(int i=0;i<3;++i) {low[i]=std::min(low[i],p[i]);high[i]=std::max(high[i],p[i]);}
    }
    if(!emitter) throw std::runtime_error("The selection has no emitter. Select one or more emitters first.");
    // Save Selection as Assembly's default pivot: the centre of the members.
    Pose frame; for(int i=0;i<3;++i) frame.position[i]=(low[i]+high[i])/2;
    return Workflow::EmitterLibrary::Draft(CaptureAssembly(members,frame),map.empty()?MapLabel():map);
}
Json MissingEmitterPackages(const Json& entry)
{
    const auto* pack=PackSummaryOf(entry);
    return pack?MissingPackageFiles(Workflow::EmitterLibrary::PackNeeds(entry,pack->at("requires"))):Json::array();
}
Json PlaceEmitterEntry(const Json& entry,const Pose& pose)
{
    // An unsaved draft places too; the id only names a saved entry.
    auto checked=entry; if(checked.is_object() && !checked.contains("id")) {checked["id"]=Id();checked["modified"]=Timestamp();}
    Workflow::EmitterLibrary::Validate(checked);
    // A pack entry whose packages are not installed says so, rather than which object is missing.
    if(const auto* pack=PackSummaryOf(checked))
    {
        const auto name=pack->at("name").get<std::string>(); const auto missing=MissingPackageFiles(Workflow::EmitterLibrary::PackNeeds(checked,pack->at("requires")));
        if(!missing.empty()) throw std::runtime_error(Workflow::EmitterLibrary::MissingPackagesMessage(name,missing));
    }
    return PlaceAssembly(checked,pose,{}).at("members");
}
EmitterLibraryView EmitterLibraryState() { return Library().view; }
Json EmitterLibrary() { return *Library().view.entries; }
Json EmitterLibraryProblems() { return *Library().view.problems; }
Json EmitterCategories() { return *Library().view.categories; }
unsigned EmitterLibraryRevision() { return Library().view.revision; }
Json EmitterLibraryCacheState()
{
    const auto& view=Library().view;
    return {{"revision",view.revision},{"packReads",emitterPackReads},{"entries",view.entries->size()},{"packs",view.packs->size()},{"problems",view.problems->size()}};
}
Json EmitterPacks()
{
    auto packs=*Library().view.packs;
    for(auto& pack:packs) pack["missing"]=MissingPackageFiles(pack.at("requires"));
    return packs;
}
Json SaveEmitterEntry(const Json& entry)
{
    auto file=EmitterFile(); auto saved=Workflow::EmitterLibrary::Save(file,entry);
    WriteEmitterFile(file); saved["builtin"]=false; saved["readonly"]=false; return saved;
}
Json UpdateEmitterEntry(const std::string& id,const Json& changes)
{
    auto file=EmitterFile(); auto updated=Workflow::EmitterLibrary::Update(file,id,changes);
    WriteEmitterFile(file); updated["builtin"]=false; updated["readonly"]=false; return updated;
}
void DeleteEmitterEntry(const std::string& id)
{
    auto file=EmitterFile(); Workflow::EmitterLibrary::Delete(file,BuiltinEmitters(),Library().packs,id); WriteEmitterFile(file);
}
void RestoreBuiltinEmitters()
{
    auto file=EmitterFile(); Workflow::EmitterLibrary::RestoreBuiltins(file); WriteEmitterFile(file);
}
