// Native reads for the Render Budget window (RenderBudgetModel.h).
// Included after LightingBudgetNative.inl, whose actor offsets and zone
// names it shares.
namespace
{
    constexpr size_t kLevelModel=0x13c;
    constexpr size_t kModelNodes=0x54,kModelVerts=0x64,kModelPoints=0x84,kModelSurfs=0x94,kModelNumZones=0x114,kModelZones=0x118;
    constexpr size_t kNodeStrideRB=0x5c,kNodeVertPool=0x28,kNodeSurf=0x2c,kNodeBack=0x30,kNodeFront=0x34,kNodeZones=0x58,kNodeVertexCount=0x5a;
    constexpr size_t kSurfStrideRB=0x2c,kSurfMaterial=0x10,kSurfFlags=0x14;
    // PF_Invisible and PF_Portal (Add Special's Portal box, 0x421).
    constexpr unsigned kSurfInvisible=0x1,kSurfPortal=0x04000000;
    // UStaticMesh: the render index buffer (FRawIndexBuffer, data +0xC4, count
    // +0xC8) and RawTriangles (TLazyArray, count +0x194), which is what the
    // Static Mesh browser's "Triangles : %d" (0x10EC0A2C) prints.
    constexpr size_t kMeshIndexCount=0xc8,kMeshRawTriangles=0x194;
    constexpr unsigned char kDrawStaticMesh=8;
    constexpr size_t kActorLastRenderTime=0x1c0;
    constexpr float kNotDrawn=-31337.0f;
    using RepaintFn=void(__thiscall*)(void*,int);

    struct ActorLayout
    {
        int drawType=-1,hidden=-1,staticMesh=-1,skins=-1;unsigned hiddenMask=0;
        size_t skinStride=4;
    };
    ActorLayout BudgetActorLayout(Address actor)
    {
        ActorLayout layout;
        for(auto p:Properties(Read<Address>(actor+0x24)))
        {
            const auto name=Fold(NameOf(p));const int offset=Read<int>(p+0x3c);
            if(name=="drawtype")layout.drawType=offset;
            else if(name=="bhidden"){layout.hidden=offset;layout.hiddenMask=Read<unsigned>(p+0x64);}
            else if(name=="staticmesh")layout.staticMesh=offset;
            else if(name=="skins"){layout.skins=offset;if(auto inner=Read<Address>(p+0x64))layout.skinStride=std::max<size_t>(4,Read<unsigned short>(inner+0x32));}
        }
        if(layout.drawType<0 || layout.hidden<0 || layout.staticMesh<0)throw std::runtime_error("This editor's Actor class has no DrawType, bHidden or StaticMesh.");
        return layout;
    }

    // Materials and the textures they reach, numbered as met.
    struct MaterialTable
    {
        std::map<Address,int> materials,textures;
        std::map<Address,std::vector<int>> reach;
        Json rows=Json::array(),textureNames=Json::array();
        int Texture(Address t)
        {
            auto [it,added]=textures.emplace(t,static_cast<int>(textures.size()));
            if(added)textureNames.push_back(Path(t));
            return it->second;
        }
        void Collect(Address object,std::set<int>& out,std::set<Address>& visited,int depth)
        {
            if(!object || depth>12 || !visited.insert(object).second || visited.size()>4096)return;
            if(IsA(object,"Texture")){out.insert(Texture(object));return;}
            if(!IsA(object,"Material"))return;
            for(const auto& ref:ReferencesOf(object,false))if(ref.target)Collect(ref.target,out,visited,depth+1);
        }
        int Index(Address material)
        {
            if(!material)return -1;
            if(auto found=materials.find(material);found!=materials.end())return found->second;
            if(!IsA(material,"Material"))return -1;
            const int index=static_cast<int>(materials.size());
            materials.emplace(material,index);
            std::set<int> reached;std::set<Address> visited;
            Collect(material,reached,visited,0);
            rows.push_back({{"name",Path(material)},{"textures",Json(std::vector<int>(reached.begin(),reached.end()))}});
            return index;
        }
    };

    int MeshTriangles(Address mesh)
    {
        const int indices=Read<int>(mesh+kMeshIndexCount),raw=Read<int>(mesh+kMeshRawTriangles);
        const int fromIndices=indices>0 && indices<30000000?indices/3:0;
        return std::max(fromIndices,raw>0 && raw<10000000?raw:0);
    }

    // The Materials array of a static mesh: StaticMeshMaterial.Material is
    // its first member.
    std::vector<Address> MeshMaterials(Address mesh)
    {
        std::vector<Address> out;
        auto p=Property(mesh,"Materials");if(!p || !IsA(p,"ArrayProperty"))return out;
        auto inner=Read<Address>(p+0x64);const size_t stride=inner?Read<unsigned short>(inner+0x32):0;
        if(stride<4)return out;
        for(auto entry:Array(mesh+Read<int>(p+0x3c),stride))out.push_back(Read<Address>(entry));
        return out;
    }

    bool BudgetRepaint(Address viewport)
    {
        __try
        {
            const auto table=*reinterpret_cast<Address*>(viewport);
            reinterpret_cast<RepaintFn>(*reinterpret_cast<Address*>(table+0xc8))(reinterpret_cast<void*>(viewport),1);
            return true;
        }
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }

    Address BudgetModel()
    {
        auto model=Read<Address>(Level()+kLevelModel);
        if(!model)throw std::runtime_error("The map has no BSP model.");
        return model;
    }
}
Json RenderBudgetScene()
{
    auto model=BudgetModel();
    auto actors=LiveActors();
    if(actors.empty())throw std::runtime_error("The map has no actors.");
    const auto layout=BudgetActorLayout(actors[0]);
    MaterialTable table;
    Json items=Json::array(),paths=Json::array();
    std::map<Address,int> meshTriangles;
    for(size_t i=0;i<actors.size();++i)
    {
        const auto actor=actors[i];
        if(i==1 && IsA(actor,"Brush"))continue; // The builder brush.
        const int zone=Read<unsigned char>(actor+kActorRegionZoneNumber);
        const int drawType=Read<unsigned char>(actor+layout.drawType);
        const bool hidden=(Read<unsigned>(actor+layout.hidden)&layout.hiddenMask)!=0;
        int kind=0,triangles=0;
        std::vector<int> materials;
        auto addMaterial=[&](Address m){const int index=table.Index(m);if(index>=0 && std::find(materials.begin(),materials.end(),index)==materials.end())materials.push_back(index);};
        const auto mesh=Read<Address>(actor+layout.staticMesh);
        if(drawType==kDrawStaticMesh && mesh && IsA(mesh,"StaticMesh"))
        {
            kind=1;
            auto known=meshTriangles.find(mesh);
            if(known==meshTriangles.end())known=meshTriangles.emplace(mesh,MeshTriangles(mesh)).first;
            triangles=known->second;
        }
        else if(IsA(actor,"Emitter"))kind=2;
        const bool drawn=!hidden && drawType!=0;
        if(drawn)
        {
            // Skins override the mesh's own materials slot by slot.
            std::vector<Address> skins;
            if(layout.skins>=0)for(auto entry:Array(actor+layout.skins,layout.skinStride))skins.push_back(Read<Address>(entry));
            std::vector<Address> own;
            if(kind==1)own=MeshMaterials(mesh);
            for(size_t slot=0;slot<std::max(skins.size(),own.size());++slot)
                addMaterial(slot<skins.size() && skins[slot]?skins[slot]:slot<own.size()?own[slot]:0);
        }
        items.push_back({{"name",NameOf(actor)},{"zone",zone},{"kind",kind},{"drawn",drawn},{"triangles",triangles},{"materials",materials}});
        paths.push_back(Path(actor));
    }
    // BSP: nodes with a polygon by the zone in front of them.
    Json nodes=Json::array(),surfaces=Json::array();
    const auto surfs=Array(model+kModelSurfs,kSurfStrideRB);
    for(auto surf:surfs)
    {
        const unsigned flags=Read<unsigned>(surf+kSurfFlags);
        surfaces.push_back({{"material",table.Index(Read<Address>(surf+kSurfMaterial))},{"drawn",!(flags&(kSurfInvisible|kSurfPortal))}});
    }
    for(auto node:Array(model+kModelNodes,kNodeStrideRB))
    {
        const int count=Read<unsigned char>(node+kNodeVertexCount);
        if(count<3)continue;
        const int back=Read<unsigned char>(node+kNodeZones),front=Read<unsigned char>(node+kNodeZones+1);
        int surface=Read<int>(node+kNodeSurf);
        if(surface<0 || surface>=static_cast<int>(surfs.size()))surface=-1;
        nodes.push_back(Json::array({front,surface,count-2}));
    }
    Json zones=Json::object();
    const int numZones=std::clamp(Read<int>(model+kModelNumZones),0,64);
    for(int zone=0;zone<numZones;++zone)
        zones[std::to_string(zone)]=BudgetZoneName(Read<Address>(model+kModelZones+zone*0x10),zone);
    Logger::log("RenderBudget: "+std::to_string(items.size())+" actors, "+std::to_string(meshTriangles.size())+" meshes, "
                +std::to_string(nodes.size())+" BSP polygons, "+std::to_string(table.materials.size())+" materials, "
                +std::to_string(table.textures.size())+" textures in "+std::to_string(numZones)+" zones");
    return {{"items",items},{"paths",paths},{"nodes",nodes},{"surfaces",surfaces},{"materials",table.rows},{"textures",table.textureNames},
            {"zones",zones},{"map",MapKey()},{"generation",MapGeneration()}};
}
Json RenderBudgetView()
{
    auto model=BudgetModel();
    const auto perspective=LevelSnapshot::FindPerspective();
    const auto camera=perspective.camera;
    RECT client{};GetClientRect(perspective.window,&client);
    const double aspect=client.bottom>client.top?double(client.right-client.left)/double(client.bottom-client.top):4.0/3.0;
    const auto position=Position(camera);const auto rotation=RotationOf(camera);
    double fov=90;if(auto p=Property(camera,"FovAngle"))fov=Read<float>(camera+Read<int>(p+0x3c));

    // The engine's own verdict on actors: mark every actor not drawn, repaint
    // the perspective viewport now (UWindowsViewport::Repaint, vtable +0xC8,
    // as Level Snapshot does) and read back which ones the renderer stamped
    // with LastRenderTime. Unstamped actors get their old time back.
    auto actors=LiveActors();
    std::vector<float> saved;saved.reserve(actors.size());
    for(auto actor:actors){saved.push_back(Read<float>(actor+kActorLastRenderTime));Write(actor+kActorLastRenderTime,kNotDrawn);}
    bool repainted=false;
    {
        struct Restore
        {
            std::vector<Address>& actors;std::vector<float>& saved;
            ~Restore(){for(size_t i=0;i<actors.size();++i){float now=0;if(CopyMemorySafe(&now,reinterpret_cast<void*>(actors[i]+kActorLastRenderTime),4) && now==kNotDrawn)CopyMemorySafe(reinterpret_cast<void*>(actors[i]+kActorLastRenderTime),&saved[i],4);}}
        } restore{actors,saved};
        repainted=BudgetRepaint(perspective.viewport);
        Json drawn=Json::array();
        for(size_t i=0;i<actors.size();++i)if(Read<float>(actors[i]+kActorLastRenderTime)!=kNotDrawn)drawn.push_back(Path(actors[i]));

        // Geometry for the portal walk: every node's plane, children and
        // zones, and the polygons of the portal surfaces.
        const auto points=Array(model+kModelPoints,12);const auto verts=Array(model+kModelVerts,8);
        const auto surfs=Array(model+kModelSurfs,kSurfStrideRB);
        Json bsp=Json::array(),portals=Json::array();
        for(auto node:Array(model+kModelNodes,kNodeStrideRB))
        {
            const auto plane=Read<std::array<float,4>>(node);
            const int zoneBack=Read<unsigned char>(node+kNodeZones),zoneFront=Read<unsigned char>(node+kNodeZones+1);
            bsp.push_back(Json::array({plane[0],plane[1],plane[2],plane[3],Read<int>(node+kNodeBack),Read<int>(node+kNodeFront),zoneBack,zoneFront}));
            const int count=Read<unsigned char>(node+kNodeVertexCount),surface=Read<int>(node+kNodeSurf),pool=Read<int>(node+kNodeVertPool);
            if(count<3 || surface<0 || surface>=static_cast<int>(surfs.size()) || !(Read<unsigned>(surfs[surface]+kSurfFlags)&kSurfPortal))continue;
            if(pool<0 || pool+count>static_cast<int>(verts.size()))continue;
            Json polygon=Json::array();
            for(int v=0;v<count;++v)
            {
                const int point=Read<int>(verts[pool+v]);
                if(point<0 || point>=static_cast<int>(points.size()))break;
                const auto p=Read<std::array<float,3>>(points[point]);
                polygon.push_back(Json::array({p[0],p[1],p[2]}));
            }
            if(static_cast<int>(polygon.size())==count)portals.push_back({{"zones",Json::array({zoneBack,zoneFront})},{"polygon",polygon}});
        }
        return {{"camera",{{"position",position},{"rotation",rotation},{"fov",fov},{"aspect",aspect},{"name",NameOf(camera)}}},
                {"drawn",drawn},{"repainted",repainted},{"bsp",bsp},{"portals",portals},{"generation",MapGeneration()}};
    }
}
