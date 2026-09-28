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
    std::filesystem::path AuthoringRoot(){return Directory().parent_path().parent_path();}
    // Where the game keeps a package, by its name: scripts in System, assets in Packages.
    std::filesystem::path AuthoringPackageFile(const std::string& package)
    {
        const auto root=AuthoringRoot();std::error_code error;
        const std::pair<const char*,const char*> places[]={{"System",".u"},{"Packages/Textures",".utx"},{"Packages/StaticMeshes",".usx"},{"Packages/Animations",".ukx"},{"Packages/Sounds",".uax"},{"Packages/Sounds",".uas"}};
        for(const auto& [folder,extension]:places)
        {
            auto file=root/std::filesystem::path(folder)/(package+extension);
            if(std::filesystem::is_regular_file(file,error))return file;
        }
        return {};
    }
    bool AuthoringHas(const Json& document,const char* kind)
    {
        for(const auto& op:document.at("operations"))if(op.at("op")==kind)return true;
        return false;
    }
    // Packages and textures named by the file's asset operations.
    std::set<std::string> AuthoringProduced(const Json& document,bool textures)
    {
        std::set<std::string> out;
        for(const auto& op:document.at("operations"))
            if(op.at("op")==(textures?"texture":"load"))out.insert(Fold(textures?Authoring::TexturePath(op):op.at("package").get<std::string>()));
        return out;
    }
    int AuthoringValue(Address object,const char* name)
    {
        auto p=Property(object,name);if(!p)return -1;
        auto at=object+Read<int>(p+0x3c);
        return IsA(p,"ByteProperty")?Read<unsigned char>(at):Read<int>(at);
    }
    // The image a texture operation names, checked the way the editor's importer will (readable, powers of two).
    std::pair<uint32_t,uint32_t> AuthoringImage(const Json& op)
    {
        const std::filesystem::path file=Authoring::Utf8Path(op.at("file").get<std::string>());
        if(file.is_relative())throw std::runtime_error("Texture files need an absolute path, or a path relative to the change file: "+file.string());
        std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot read "+file.string());
        std::string header(64,'\0');input.read(header.data(),header.size());header.resize(static_cast<size_t>(input.gcount()));
        try{return Authoring::ImageSize(header,Authoring::Extension(file.string()));}
        catch(const std::exception& e){throw std::runtime_error(file.string()+": "+e.what());}
    }
    // Asset operations run first, in file order. They are not part of the map's Undo.
    Json RunAuthoringAssets(const Json& document)
    {
        Json done=Json::array();
        const char* formats[]={"P8","RGBA7","RGB16","DXT1","RGB8","RGBA8","NODATA","DXT3","DXT5","L8","G16","RRRGGGBBB"};
        auto formatName=[&](int f){return f>=0 && f<12?std::string(formats[f]):std::to_string(f);};
        for(const auto& op:document.at("operations"))
        {
            const auto kind=op.at("op").get<std::string>();
            if(kind=="load")
            {
                const auto package=op.at("package").get<std::string>();const auto file=AuthoringPackageFile(package);
                if(file.empty())throw std::runtime_error("No package file named "+package+" in System or Packages.");
                Exec("OBJ LOAD FILE=\""+file.string()+"\"");
                if(!Find(package))throw std::runtime_error("The editor did not load "+file.string());
                done.push_back("loaded "+package+" from "+file.string());
            }
            else if(kind=="texture")
            {
                const std::filesystem::path file=Authoring::Utf8Path(op.at("file").get<std::string>());
                const auto extension=Authoring::Extension(file.string());const auto size=AuthoringImage(op);
                const auto path=Authoring::TexturePath(op),package=op.at("package").get<std::string>();
                if(Find(path))throw std::runtime_error("A texture already exists at "+path+". Import under a new name.");
                std::string command="TEXTURE IMPORT FILE=\""+file.string()+"\" NAME=\""+op.at("name").get<std::string>()+"\" PACKAGE=\""+package+"\"";
                if(op.contains("group"))command+=" GROUP=\""+op.at("group").get<std::string>()+"\"";
                command+=" MIPS="+std::string(op.value("mips",true)?"1":"0")+" MASKED="+(op.value("masked",false)?"1":"0")+" ALPHATEXTURE="+(op.value("alphaTexture",false)?"1":"0");
                if(op.contains("lodSet"))command+=" LODSET="+std::to_string(op.at("lodSet").get<int>());
                Exec(command);
                auto texture=Find(path);
                if(!texture || !IsA(texture,"Texture"))throw std::runtime_error("The editor's texture importer refused "+file.string());
                const auto format=op.value("format",std::string(extension==".dds"?"":"DXT1"));
                if(!format.empty() && format!="RGBA8" && formatName(AuthoringValue(texture,"Format"))!=format)
                {
                    // Without MaxMips the compressor keeps only the top level; ask for the whole chain (or none).
                    int chain=1;for(auto side=(std::max)(size.first,size.second);side>1;side>>=1)++chain;
                    Exec("TEXTURE COMPRESS NAME="+path+" FORMAT="+format+" MaxMips="+std::to_string(op.value("mips",true)?chain:1));
                }
                const int w=AuthoringValue(texture,"USize"),h=AuthoringValue(texture,"VSize");const auto actual=formatName(AuthoringValue(texture,"Format"));
                if(w!=static_cast<int>(size.first) || h!=static_cast<int>(size.second))throw std::runtime_error(path+" imported at "+std::to_string(w)+"x"+std::to_string(h)+", not the file's "+std::to_string(size.first)+"x"+std::to_string(size.second));
                if(!format.empty() && actual!=format)throw std::runtime_error(path+" is "+actual+" after import, not "+format);
                done.push_back("texture "+path+" "+std::to_string(w)+"x"+std::to_string(h)+" "+actual+" from "+file.string());
            }
            else if(kind=="save")
            {
                const auto package=op.at("package").get<std::string>();
                if(!Find(package))throw std::runtime_error("Package "+package+" is not loaded; nothing to save.");
                const auto folder=AuthoringRoot()/"Packages"/"Textures";const auto file=folder/(package+".utx");std::error_code error;
                std::filesystem::create_directories(folder,error);
                if(std::filesystem::exists(file,error))
                {
                    if(!op.value("overwrite",false))throw std::runtime_error(file.string()+" exists. Set overwrite to replace it (the old file is backed up).");
                    const auto backups=Directory()/"map-json-backups";std::filesystem::create_directories(backups,error);
                    const auto backup=backups/(package+"."+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+".utx");
                    if(!std::filesystem::copy_file(file,backup,std::filesystem::copy_options::overwrite_existing,error))throw std::runtime_error("Cannot back up "+file.string());
                    done.push_back("backed up "+file.string()+" to "+backup.string());
                }
                const auto started=std::filesystem::file_time_type::clock::now()-std::chrono::seconds(2);
                if(!Exec("OBJ SAVEPACKAGE PACKAGE=\""+package+"\" FILE=\""+file.string()+"\"") || !std::filesystem::exists(file,error) || std::filesystem::last_write_time(file,error)<started)
                    throw std::runtime_error("The editor did not save "+file.string());
                done.push_back("saved "+package+" to "+file.string()+" ("+std::to_string(std::filesystem::file_size(file,error))+" bytes)");
            }
        }
        return done;
    }
    AuthoringPlan PlanAuthoring(const Json& document,bool assetsApplied=true)
    {
        Authoring::Validate(document,AuthoringMapKey());
        AuthoringPlan plan;plan.document=document;
        // Before a preview loads the file's packages, their classes and materials can't be checked yet.
        const bool deferred=!assetsApplied && (AuthoringHas(document,"load") || AuthoringHas(document,"texture"));
        const auto loadedPackages=AuthoringProduced(document,false),importedTextures=AuthoringProduced(document,true);
        auto materialReady=[&](const std::string& path)
        {
            if(auto m=Find(path))return IsA(m,"Material");
            if(!deferred)return false;
            return importedTextures.count(Fold(path))>0 || loadedPackages.count(Fold(path.substr(0,path.find('.'))))>0;
        };
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
                if(kind=="create")
                {
                    for(const auto& live:Actors())if(Fold(live.at("name"))==Fold(id) || Fold(live.at("tag"))==Fold(id))throw std::runtime_error("A live actor already uses the new actor ID or Tag: "+id);
                    if(!op.at("properties").contains("Location"))throw std::runtime_error("Every new actor requires an explicit Location.");
                    if(op.contains("polygons"))for(const auto& polygon:op.at("polygons"))
                        if(polygon.contains("texture") && !materialReady(polygon.at("texture").get<std::string>()))throw std::runtime_error(id+": load the material first (or import it in this file): "+polygon.at("texture").get<std::string>());
                }
                if(found==choices.end() && deferred)
                {
                    // Named without a schema yet; Apply plans again after the packages load.
                    plan.summary.push_back(kind+" "+id+" ("+type+", checked after the file's packages load)");continue;
                }
                if(found==choices.end() || type=="Engine.ParticleEmitter")throw std::runtime_error("Load a concrete supported class: "+type);
                if(kind=="create")
                {
                    auto geometry=op.value("geometry",std::string("point"));
                    if(found->at("brush").get<bool>()?geometry=="point":geometry!="point")throw std::runtime_error("Use box or polygons geometry for brush actors and point for other actors.");
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
            if(kind=="load"){plan.summary.push_back("load package "+op.at("package").get<std::string>());continue;}
            if(kind=="texture")
            {
                const auto size=AuthoringImage(op);
                plan.summary.push_back("import texture "+Authoring::TexturePath(op)+" "+std::to_string(size.first)+"x"+std::to_string(size.second)+" from "+op.at("file").get<std::string>()+" ("+op.value("format",std::string("file's own format"))+(op.value("mips",true)?", mips":", no mips")+")");continue;
            }
            if(kind=="save"){plan.summary.push_back("save package "+op.at("package").get<std::string>()+" to Packages\\Textures"+(op.value("overwrite",false)?" (replacing the file, backed up)":""));continue;}
            if(kind=="surface")
            {
                const auto texture=op.at("texture").get<std::string>();
                if(!materialReady(texture))throw std::runtime_error("Load the surface material first (or import it in this file): "+texture);
                const auto model=Read<Address>(Level()+0x13c);const auto count=model?Array(model+0x94,0x2c).size():0;
                for(const auto& surface:op.at("surfaces"))if(surface.get<size_t>()>=count)throw std::runtime_error("The map has "+std::to_string(count)+" BSP surfaces; surface "+std::to_string(surface.get<size_t>())+" does not exist. Rebuild geometry first.");
                plan.summary.push_back("texture "+std::to_string(op.at("surfaces").size())+" BSP surface(s) with "+texture);continue;
            }
            if(kind=="link")
            {
                auto event=op.at("event").get<std::string>(),target=op.at("target").get<std::string>();
                if(deferred && (!plan.types.count(event) || !plan.types.count(target))){plan.summary.push_back(kind+" "+event+" / "+target+" (checked after the file's packages load)");continue;}
                if(!plan.types.count(event) || !plan.types.count(target) || !AuthoringSubclass(plan.types.at(event),"SBase.SMagicEvent") || !AuthoringSubclass(plan.types.at(target),"Actor"))throw std::runtime_error("Link requires an SMagicEvent and an actor target.");
                checkExisting(event);checkExisting(target);
                plan.summary.push_back(kind+" "+event+(op.at("trigger").get<bool>()?" <- ":" -> ")+target+" (group "+std::to_string(op.value("group",0))+", delay "+op.value("delay",std::string("0"))+"s)");continue;
            }
            const auto id=kind=="update"?op.at("actor").at("path").get<std::string>():op.at("id").get<std::string>();
            if(deferred && !plan.types.count(id))continue;
            if(kind=="component")
            {
                auto owner=op.at("owner").get<std::string>();
                if(deferred && !plan.types.count(owner))continue;
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
    auto plan=PlanAuthoring(document,false);
    const bool assets=AuthoringHas(document,"load") || AuthoringHas(document,"texture") || AuthoringHas(document,"save");
    return {{"changes",plan.summary},{"note",std::string(assets?"Package loads, texture imports and package saves run first and are not undone by Undo. ":"")+"Map changes apply in one Undo step. Save the map to retain changes. Lighting, geometry and navigation are not rebuilt; playtest the result."}};
}
Json ApplyMapAuthoring(const Json& document)
{
    Authoring::Validate(document,AuthoringMapKey());
    if(!pasteHookReady || insertionText)throw std::runtime_error("Native actor insertion is unavailable or busy.");
    PlanAuthoring(document,false); // Reject what can be rejected before any package changes.
    auto assets=RunAuthoringAssets(document);
    auto plan=PlanAuthoring(document);
    auto selection=SelectedIdentities();
    struct Restore{Json selection;~Restore(){try{Select(selection);Redraw();}catch(...){}}} restore{selection};
    Transaction transaction("Import map JSON changes");Json created=Json::array();
    std::string text="Begin Map\r\n";size_t count=0;
    for(const auto& op:document.at("operations"))if(op.at("op")=="create")
    {
        auto id=op.at("id").get<std::string>(),type=op.at("class").get<std::string>();
        auto path=plan.identities.at(id).at("path").get<std::string>();auto name=path.substr(path.find_last_of('.')+1);
        text+="Begin Actor Class="+type+" Name="+name+"\r\nTag="+id+"\r\nLocation="+Magic::Text(op.at("properties").at("Location"))+"\r\n";
        const auto geometry=op.value("geometry",std::string("point"));
        // The paste turns a plain Brush left at its default CSG_Active into the builder brush and creates no actor,
        // so a CSG brush carries its operation in the pasted text (CSG_Add unless the file sets one).
        if(geometry!="point" && AuthoringSubclass(plan.types.at(id),"Brush") && !AuthoringSubclass(plan.types.at(id),"Volume"))
            text+="CsgOper="+(op.at("properties").contains("CsgOper")?op.at("properties").at("CsgOper").get<std::string>():std::string("CSG_Add"))+"\r\n";
        if(geometry!="point")text+="Begin Brush Name="+name+"Model\r\n"+(geometry=="box"?Magic::BoxPolygons():Authoring::BrushPolygons(op.at("polygons")))+"End Brush\r\nBrush=Model'"+LevelPath()+"."+name+"Model'\r\n";
        text+="End Actor\r\n";++count;
    }
    text+="End Map\r\n";
    if(count)
    {
        struct PasteScope{~PasteScope(){insertionText=nullptr;}} paste;insertionText=text.c_str();
        Select(Json::array());Call(Engine(),0x26c,reinterpret_cast<void*>(Level()),0);
        if(auto pasted=SelectedIdentities().size();pasted!=count)throw std::runtime_error("Native creation returned "+std::to_string(pasted)+" actors, not "+std::to_string(count)+".");
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
    for(const auto& op:document.at("operations"))if(op.at("op")=="create" || op.at("op")=="component" || op.at("op")=="update")
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
    // Per-surface textures: the stock Apply Texture command on exactly these surfaces.
    size_t textured=0;
    for(const auto& op:document.at("operations"))if(op.at("op")=="surface")
    {
        auto e=Engine(),model=Read<Address>(Level()+0x13c);if(!model)throw std::runtime_error("The map has no BSP surfaces. Rebuild geometry first.");
        auto all=Array(model+0x94,0x2c);auto material=Find(op.at("texture").get<std::string>());
        if(!material || !IsA(material,"Material"))throw std::runtime_error("Surface material is not loaded: "+op.at("texture").get<std::string>());
        std::set<size_t> wanted;for(const auto& s:op.at("surfaces")){auto i=s.get<size_t>();if(i>=all.size())throw std::runtime_error("BSP surface "+std::to_string(i)+" does not exist.");wanted.insert(i);}
        std::vector<unsigned> flags;for(auto s:all)flags.push_back(Read<unsigned>(s+0x14));
        struct Restore{Address e,previous;std::vector<Address>& surfaces;std::vector<unsigned>& flags;
            ~Restore(){Write(e+0x138,previous);for(size_t i=0;i<surfaces.size();++i)Write(surfaces[i]+0x14,(Read<unsigned>(surfaces[i]+0x14)&~0x02000000u)|(flags[i]&0x02000000u));}
        } restore{e,Read<Address>(e+0x138),all,flags};
        for(size_t i=0;i<all.size();++i)Write(all[i]+0x14,(flags[i]&~0x02000000u)|(wanted.count(i)?0x02000000u:0));
        Write(e+0x138,material);
        if(!Exec("POLY SETTEXTURE"))throw std::runtime_error("Native surface texturing failed.");
        textured+=wanted.size();
    }
    transaction.Commit();return {{"created",created},{"operations",document.at("operations").size()},{"assets",assets},{"surfaces",textured}};
}
