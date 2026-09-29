// Included inside Workflow::Editor. The Emitter Library: emitters saved as
// canonical assembly definitions (schema in EmitterLibraryModel.h) in
// emitter_library.json beside library.json, merged with the built-in entries
// compiled into the DLL (EmitterLibraryDefaults.h), which are never written.
namespace
{
    unsigned emitterLibraryRevision=0;
    std::filesystem::path EmitterLibraryFile() { return Directory()/"emitter_library.json"; }
    Json EmitterFile() { return Workflow::EmitterLibrary::Document(ReadDocument(EmitterLibraryFile(),Workflow::EmitterLibrary::EmptyDocument())); }
    void WriteEmitterFile(const Json& file) { WriteDocument(EmitterLibraryFile(),file);++emitterLibraryRevision; }
    const Json& BuiltinEmitters() { static const Json builtins=Workflow::EmitterLibrary::Builtins(Workflow::EmitterLibrary::DefaultsText()); return builtins; }
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
Json PlaceEmitterEntry(const Json& entry,const Pose& pose)
{
    // An unsaved draft places too; the id only names a saved entry.
    auto checked=entry; if(checked.is_object() && !checked.contains("id")) {checked["id"]=Id();checked["modified"]=Timestamp();}
    Workflow::EmitterLibrary::Validate(checked);
    return PlaceAssembly(checked,pose,{}).at("members");
}
Json EmitterLibrary() { return Workflow::EmitterLibrary::Merge(BuiltinEmitters(),EmitterFile()); }
Json EmitterLibraryProblems() { return Workflow::EmitterLibrary::Problems(EmitterFile()); }
Json EmitterCategories() { return Workflow::EmitterLibrary::Categories(EmitterLibrary()); }
unsigned EmitterLibraryRevision() { return emitterLibraryRevision; }
Json SaveEmitterEntry(const Json& entry)
{
    auto file=EmitterFile(); auto saved=Workflow::EmitterLibrary::Save(file,entry);
    WriteEmitterFile(file); saved["builtin"]=false; return saved;
}
Json UpdateEmitterEntry(const std::string& id,const Json& changes)
{
    auto file=EmitterFile(); auto updated=Workflow::EmitterLibrary::Update(file,id,changes);
    WriteEmitterFile(file); updated["builtin"]=false; return updated;
}
void DeleteEmitterEntry(const std::string& id)
{
    auto file=EmitterFile(); Workflow::EmitterLibrary::Delete(file,BuiltinEmitters(),id); WriteEmitterFile(file);
}
void RestoreBuiltinEmitters()
{
    auto file=EmitterFile(); Workflow::EmitterLibrary::RestoreBuiltins(file); WriteEmitterFile(file);
}
