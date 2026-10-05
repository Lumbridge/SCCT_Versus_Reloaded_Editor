// Included inside Workflow::Editor; shares the verified native bridge helpers.
// Build > Generate Minimap: the level's built faces and meshes for the plan,
// and the LevelInfo settings the game's strategic map reads (see MinimapModel.h).
namespace
{
    // UModel arrays, as BspDiagnostics verified them against FilterEdPoly and
    // FindNearestVertex: Nodes +0x54 (0x5c each: iVertPool +0x28, iSurf
    // +0x2c, NumVertices byte +0x5a), Verts +0x64 (8 each, point index first),
    // Vectors +0x74, Points +0x84, Surfs +0x94 (0x2c each: PolyFlags +0x14,
    // vNormal word +0x1a).
    std::vector<Minimap::Vec3> MinimapVectors(Address at)
    {
        std::vector<Minimap::Vec3> out;
        for(auto item:Array(at,12)){auto v=Read<std::array<float,3>>(item);out.push_back({v[0],v[1],v[2]});}
        return out;
    }
    Minimap::Vec3 Newell(const std::vector<Minimap::Vec3>& p)
    {
        Minimap::Vec3 n{};
        for(size_t i=0;i<p.size();++i)
        {
            const auto& a=p[i];const auto& b=p[(i+1)%p.size()];
            n[0]+=(a[1]-b[1])*(a[2]+b[2]);n[1]+=(a[2]-b[2])*(a[0]+b[0]);n[2]+=(a[0]-b[0])*(a[1]+b[1]);
        }
        const double length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if(length>0)for(auto& v:n)v/=length;
        return n;
    }
    Address MinimapStructField(Address property,const char* name)
    {
        auto type=Read<Address>(property+0x64);auto field=StructField(type,name);
        if(!field)throw std::runtime_error(std::string("This editor's SnapCameraStruct has no ")+name+".");
        return static_cast<Address>(Read<int>(field+0x3c));
    }
}
Minimap::Scene MinimapScene()
{
    Minimap::Scene scene;
    auto level=Level(),model=Read<Address>(level+0x13c);
    scene.package=Path(Read<Address>(level+0x18));
    if(model)
    {
        const auto points=MinimapVectors(model+0x84),vectors=MinimapVectors(model+0x74);
        auto verts=Array(model+0x64,8);auto surfs=Array(model+0x94,0x2c);
        for(auto node:Array(model+0x54,0x5c))
        {
            const int count=Read<unsigned char>(node+0x5a),pool=Read<int>(node+0x28),surface=Read<int>(node+0x2c);
            if(count<3 || pool<0 || pool+count>static_cast<int>(verts.size()) || surface<0 || surface>=static_cast<int>(surfs.size()))continue;
            Minimap::Polygon face;
            bool sane=true;
            for(int i=0;i<count && sane;++i)
            {
                int index=Read<int>(verts[pool+i]);
                if(index<0 || index>=static_cast<int>(points.size()))index=Read<unsigned short>(verts[pool+i]);
                if(index<0 || index>=static_cast<int>(points.size())){sane=false;break;}
                const auto& p=points[index];
                for(double v:p)if(!std::isfinite(v) || std::abs(v)>1000000)sane=false;
                face.points.push_back(p);
            }
            if(!sane)continue;
            const auto surf=surfs[surface];
            face.flags=Read<unsigned>(surf+0x14);
            face.normal=Newell(face.points);
            // The surface's own normal says which way the face looks; the
            // winding alone does not.
            const int normal=Read<unsigned short>(surf+0x1a);
            if(normal<static_cast<int>(vectors.size()))
            {
                const auto& n=vectors[normal];
                const double dot=n[0]*face.normal[0]+n[1]*face.normal[1]+n[2]*face.normal[2];
                if(std::abs(dot)>0.5)face.normal=n;
            }
            scene.faces.push_back(std::move(face));
        }
    }
    // Static meshes as boxes: the mesh's local FBox (six floats at +0x28, as
    // SelectedMeshBounds reads it) through the actor's scale and pose.
    for(auto actor:LiveActors())
    {
        if(IsA(actor,"Mover") || IsA(actor,"Brush"))continue;
        auto property=Property(actor,"StaticMesh");
        auto mesh=property?Read<Address>(actor+Read<int>(property+0x3c)):0;
        if(!mesh || !IsA(mesh,"StaticMesh"))continue;
        if(auto hidden=Property(actor,"bHidden"))if(Read<unsigned>(actor+Read<int>(hidden+0x3c))&Read<unsigned>(hidden+0x64))continue;
        try
        {
            auto box=Read<std::array<float,6>>(mesh+0x28);
            auto scale=Read<float>(Field(actor,"DrawScale"));
            auto scale3=Read<std::array<float,3>>(Field(actor,"DrawScale3D"));
            auto pivot=Read<std::array<float,3>>(Field(actor,"PrePivot"));
            bool sane=std::isfinite(scale);
            for(int axis=0;axis<3;++axis)sane=sane && std::isfinite(box[axis]) && std::isfinite(box[axis+3]) && box[axis]<=box[axis+3] && std::isfinite(scale3[axis]) && std::isfinite(pivot[axis]);
            if(!sane)continue;
            Pose pose{Position(actor),RotationOf(actor)};
            Minimap::MeshBox item;
            for(int corner=0;corner<8;++corner)
            {
                Vector local{};
                for(int axis=0;axis<3;++axis)local[axis]=(static_cast<double>(box[axis+((corner&(1<<axis))?3:0)])-pivot[axis])*scale*scale3[axis];
                auto world=TransformPoint(local,pose);
                if(!std::isfinite(world[0]) || !std::isfinite(world[1]) || !std::isfinite(world[2]) || std::abs(world[0])>1000000 || std::abs(world[1])>1000000){item.corners.clear();break;}
                item.corners.push_back({world[0],world[1],world[2]});
            }
            if(!item.corners.empty())scene.meshes.push_back(std::move(item));
        }
        catch(const std::exception&) { /* An actor without the usual mesh fields is left off the plan. */ }
    }
    // What the LevelInfo holds now.
    auto info=LiveActors().at(0);
    if(auto camera=Property(info,"SnapshotCamera"))
    {
        const auto at=info+Read<int>(camera+0x3c);
        auto vector=[&](const char* name){auto v=Read<std::array<float,3>>(at+MinimapStructField(camera,name));return Minimap::Vec3{v[0],v[1],v[2]};};
        scene.camera.position=vector("Position");
        scene.camera.rows={vector("RotationRow0"),vector("RotationRow1"),vector("RotationRow2")};
        scene.camera.fov=Read<int>(at+MinimapStructField(camera,"FOV"));
        scene.camera.targetDistance=Read<float>(at+MinimapStructField(camera,"TargetDistance"));
    }
    if(auto floors=Property(info,"MapFloors"))
    {
        auto inner=Read<Address>(floors+0x64);const int stride=Read<unsigned short>(inner+0x32);
        const auto z=MinimapStructField(inner,"FloorZ"),texture=MinimapStructField(inner,"FloorMapTexture");
        if(stride>0)for(auto item:Array(info+Read<int>(floors+0x3c),stride))
            scene.floors.emplace_back(Read<float>(item+z),Path(Read<Address>(item+texture)));
    }
    return scene;
}
// Imports one picture per floor into the map's package (group Minimap, named
// Floor0, Floor1...) and points LevelInfo.MapFloors and SnapshotCamera at
// them in one Undo step. The textures themselves are not undone; generating
// again replaces them in place.
Json ApplyMinimap(const Minimap::Camera& camera,const std::vector<std::pair<double,std::filesystem::path>>& images)
{
    if(images.empty())throw std::runtime_error("A minimap needs at least one floor.");
    const auto cameraText=Minimap::CameraText(camera);
    auto info=LiveActors().at(0);
    auto cameraProperty=Property(info,"SnapshotCamera"),floorsProperty=Property(info,"MapFloors");
    if(!cameraProperty || !floorsProperty)throw std::runtime_error("This editor's LevelInfo has no SnapshotCamera or MapFloors.");
    const auto package=Path(Read<Address>(Level()+0x18));
    std::vector<std::pair<double,std::string>> floors;
    for(size_t i=0;i<images.size();++i)
    {
        const auto name=Minimap::FloorTextureName(i);
        const auto path=package+"."+Minimap::kTextureGroup+"."+name;
        if(!Exec("TEXTURE IMPORT FILE=\""+images[i].second.string()+"\" NAME=\""+name+"\" PACKAGE=\""+package+"\" GROUP=\""+Minimap::kTextureGroup+"\" MIPS=0 MASKED=0 ALPHATEXTURE=1"))
            throw std::runtime_error("The editor's texture importer refused "+images[i].second.string());
        auto texture=Find(path);
        if(!texture || !IsA(texture,"Texture"))throw std::runtime_error("The editor did not create "+path+".");
        floors.emplace_back(images[i].first,path);
    }
    const auto floorsText=Minimap::FloorsText(floors);
    Transaction transaction("Generate minimap");
    Modify(info);
    for(auto [property,text]:{std::pair<Address,const std::string*>{cameraProperty,&cameraText},{floorsProperty,&floorsText}})
    {
        auto end=Call<const char*>(property,0x94,text->c_str(),reinterpret_cast<void*>(info+Read<int>(property+0x3c)),0u);
        if(!end)throw std::runtime_error("The editor rejected the minimap's "+NameOf(property)+".");
    }
    Call(info,0x44);
    transaction.Commit();
    Redraw();
    // Pictures left from an earlier run with more floors are not deleted:
    // the Undo record still points at them. They are reported instead.
    Json result={{"floors",Json::array()},{"unused",Json::array()}};
    for(const auto& [z,path]:floors)result["floors"].push_back({{"z",z},{"texture",path}});
    for(size_t i=images.size();i<64;++i)
    {
        const auto path=package+"."+Minimap::kTextureGroup+"."+Minimap::FloorTextureName(i);
        if(!Find(path))break;
        result["unused"].push_back(path);
    }
    return result;
}
