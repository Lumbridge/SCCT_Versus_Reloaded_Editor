// Native reads for Map Check (MapCheckModel.h): the BSP as last built, its
// zones and zone portals, the actors, the editor's own encroachment test and
// the stock CheckForErrors entries.
namespace
{
    // ULevel::EncroachingWorldGeometry(FCheckResult&, const FVector& Location,
    // const FVector& Extent, DWORD ExtraNodeFlags, ALevelInfo*): 1 when the box
    // overlaps the BSP or a world-geometry actor (MultiPointCheck with only world
    // geometry, a single result). FindSpot (0x110BE738) calls it the same way.
    // FCheckResult is 0x38 bytes; +4 is the actor hit (the LevelInfo for BSP).
    constexpr Address kEncroachingWorldGeometry=0x110be350;
    constexpr size_t kCheckResultSize=0x38,kCheckResultActor=4;
    // AActor::CheckForErrors, as the stock MAP CHECK loop (0x1100E872) calls it.
    constexpr size_t kCheckForErrorsSlot=0x13c;
    constexpr unsigned kPolyPortal=0x04000000u,kPolySelected=0x02000000u;
    constexpr size_t kNodeStride=0x5c,kSurfStride=0x2c;

    using EncroachFn=int(__thiscall*)(void*,void*,const float*,const float*,int,void*);
    int EncroachRaw(Address level,void* hit,const float* location,const float* extent,Address info)
    {
        __try{return reinterpret_cast<EncroachFn>(kEncroachingWorldGeometry)(reinterpret_cast<void*>(level),hit,location,extent,0,reinterpret_cast<void*>(info));}
        __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
    }
    bool CallMethodRaw(Address method,Address object)
    {
        __try{reinterpret_cast<void(__thiscall*)(void*)>(method)(reinterpret_cast<void*>(object));return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }

    struct StockCapture{int type;Address actor;std::string message;};
    std::vector<StockCapture>* stockCaptures=nullptr;
    void __cdecl CaptureStockEntry(int type,void* actor,const char* message)
    {
        if(!stockCaptures || stockCaptures->size()>=5000)return;
        try{stockCaptures->push_back({type,reinterpret_cast<Address>(actor),message?std::string(message,strnlen(message,1024)):std::string{}});}
        catch(...){}
    }

    // Reflected fields Map Check reads, per class.
    struct CheckFields
    {
        std::vector<std::string> classes;
        int radius=-1,height=-1,collide=-1,block=-1;
        unsigned collideMask=0,blockMask=0;
    };
    const CheckFields& FieldsOf(Address cls,std::map<Address,CheckFields>& cache)
    {
        if(auto found=cache.find(cls);found!=cache.end())return found->second;
        CheckFields fields;
        std::set<Address> seen;
        for(Address c=cls;c && seen.insert(c).second && seen.size()<64;c=Read<Address>(c+0x28))fields.classes.push_back(NameOf(c));
        for(auto p:Properties(cls))
        {
            const auto name=Fold(NameOf(p));
            const int offset=Read<int>(p+0x3c);
            if(name=="collisionradius")fields.radius=offset;
            else if(name=="collisionheight")fields.height=offset;
            else if(name=="bcollideactors"){fields.collide=offset;fields.collideMask=Read<unsigned>(p+0x64);}
            else if(name=="bblockactors"){fields.block=offset;fields.blockMask=Read<unsigned>(p+0x64);}
        }
        return cache.emplace(cls,std::move(fields)).first->second;
    }
    bool EncroachFaultLogged=false;
}
MapCheck::Scene MapCheckScene(const MapCheck::Settings& settings)
{
    auto level=Level();
    auto model=Read<Address>(level+0x13c);
    MapCheck::Scene scene;
    auto live=LiveActors();
    std::set<Address> liveSet(live.begin(),live.end());
    const Address levelInfo=live.empty()?0:Read<Address>(Read<Address>(level+0x2c));
    auto builder=reinterpret_cast<Address>(MapRecovery::ResolveBuilderBrushActor(reinterpret_cast<void*>(level)));

    std::vector<Address> surfaces;
    if(model)
    {
        auto& bsp=scene.bsp;
        bsp.rootOutside=Read<int>(model+0x104)!=0;
        bsp.numZones=Read<int>(model+0x114);
        if(bsp.numZones<0 || bsp.numZones>64)throw std::runtime_error("The BSP's zone table is invalid. Rebuild geometry and refresh.");
        auto nodes=Array(model+0x54,kNodeStride);
        bsp.nodes.reserve(nodes.size());
        for(auto at:nodes)
        {
            MapCheck::Node node;
            const auto plane=Read<std::array<float,4>>(at);
            node.normal={plane[0],plane[1],plane[2]};node.distance=plane[3];
            node.back=Read<int>(at+0x30);node.front=Read<int>(at+0x34);
            node.csg=Read<unsigned char>(at+0x5a)>0 && !(Read<unsigned char>(at+0x5b)&0x21);
            node.zone[0]=Read<unsigned char>(at+0x58);node.zone[1]=Read<unsigned char>(at+0x59);
            node.leaf[0]=Read<short>(at+0x4c);node.leaf[1]=Read<short>(at+0x4e);
            bsp.nodes.push_back(node);
        }
        auto points=Array(model+0x84,12);
        for(size_t i=0;i<points.size();++i)
        {
            const auto p=Read<std::array<float,3>>(points[i]);
            if(!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))continue;
            for(size_t k=0;k<3;++k)
            {
                if(!scene.boundsKnown){scene.boundsMin[k]=scene.boundsMax[k]=p[k];continue;}
                scene.boundsMin[k]=std::min(scene.boundsMin[k],double(p[k]));scene.boundsMax[k]=std::max(scene.boundsMax[k],double(p[k]));
            }
            scene.boundsKnown=true;
        }
        for(int zone=0;zone<std::min(bsp.numZones,64);++zone)
        {
            auto actor=Read<Address>(model+0x118+zone*0x10);
            scene.zoneActors.push_back(actor && liveSet.count(actor) && actor!=levelInfo?Path(actor):std::string{});
        }

        // Zone portal surfaces, the brush each came from and the polygons the BSP kept.
        surfaces=Array(model+0x94,kSurfStride);
        auto verts=Array(model+0x64,8);
        std::map<int,size_t> portalIndex;
        auto surfaceBrush=[&](Address surface)->Address{
            auto poly=Read<Address>(surface+0x20),actor=poly?Read<Address>(poly+0x14c):0;
            return actor && liveSet.count(actor)?actor:0;
        };
        for(size_t i=0;i<surfaces.size();++i)
        {
            if(!(Read<unsigned>(surfaces[i]+0x14)&kPolyPortal))continue;
            MapCheck::Portal portal;portal.surface=static_cast<int>(i);
            if(auto brush=surfaceBrush(surfaces[i]))portal.brush=Path(brush);
            portalIndex[portal.surface]=scene.portals.size();
            scene.portals.push_back(std::move(portal));
        }
        for(size_t n=0;n<nodes.size() && !portalIndex.empty();++n)
        {
            const int count=Read<unsigned char>(nodes[n]+0x5a),surf=Read<int>(nodes[n]+0x2c),pool=Read<int>(nodes[n]+0x28);
            auto found=portalIndex.find(surf);
            if(!count || found==portalIndex.end())continue;
            if(pool<0 || static_cast<size_t>(pool)+count>verts.size())continue;
            std::vector<MapCheck::Vec3> polygon;
            for(int k=0;k<count;++k)
            {
                // FVert.pVertex is 16 bits here (movsx word at 0x110D173E in
                // BuildRenderData); a rebuild leaves the next two bytes undefined.
                const int point=Read<short>(verts[static_cast<size_t>(pool+k)]);
                if(point<0 || static_cast<size_t>(point)>=points.size()){polygon.clear();break;}
                const auto p=Read<std::array<float,3>>(points[static_cast<size_t>(point)]);
                polygon.push_back({p[0],p[1],p[2]});
            }
            auto& portal=scene.portals[found->second];
            if(polygon.size()>=3)
            {
                portal.normal={bsp.nodes[n].normal[0],bsp.nodes[n].normal[1],bsp.nodes[n].normal[2]};
                portal.fragments.push_back(std::move(polygon));
            }
        }
    }

    // Sheet brushes flagged as zone portals that left no surface at all.
    std::set<Address> brushesWithSurfaces;
    for(auto surface:surfaces){auto poly=Read<Address>(surface+0x20);if(auto actor=poly?Read<Address>(poly+0x14c):0)brushesWithSurfaces.insert(actor);}
    std::map<Address,CheckFields> cache;
    for(auto actor:live)
    {
        if(actor==builder || actor==levelInfo)continue;
        const auto& fields=FieldsOf(Read<Address>(actor+0x24),cache);
        const bool brush=std::find(fields.classes.begin(),fields.classes.end(),"Brush")!=fields.classes.end();
        if(brush && model && !brushesWithSurfaces.count(actor))
        {
            bool portal=(Read<unsigned>(actor+0x344)&kPolyPortal)!=0;
            auto brushModel=Read<Address>(actor+0x238);
            auto polys=brushModel?Read<Address>(brushModel+0x50):0;
            if(!portal && polys)for(auto poly:Array(polys+0x28,0x14c))if(Read<unsigned>(poly+0x140)&kPolyPortal){portal=true;break;}
            if(portal)scene.portalBrushesWithoutFaces.push_back(Path(actor));
        }
        MapCheck::Actor item;
        item.path=Path(actor);item.name=NameOf(actor);item.classes=fields.classes;
        const auto location=Position(actor);
        item.location={location[0],location[1],location[2]};
        if(fields.radius>=0)item.radius=Read<float>(actor+fields.radius);
        if(fields.height>=0)item.height=Read<float>(actor+fields.height);
        if(fields.collide>=0)item.collideActors=(Read<unsigned>(actor+fields.collide)&fields.collideMask)!=0;
        if(fields.block>=0)item.blockActors=(Read<unsigned>(actor+fields.block)&fields.blockMask)!=0;
        if(model && !scene.bsp.nodes.empty() && MapCheck::TestsGeometry(item))
        {
            // The editor's own spawn test, with the cylinder shrunk by the tolerance
            // so an actor resting on a floor does not count.
            const float at[3]={float(item.location[0]),float(item.location[1]),float(item.location[2])};
            const float r=float(std::max(1.0,item.radius-settings.geometryTolerance)),h=float(std::max(1.0,item.height-settings.geometryTolerance));
            const float extent[3]={r,r,h};
            alignas(8) unsigned char hit[kCheckResultSize]={};
            const int result=EncroachRaw(level,hit,at,extent,levelInfo);
            if(result<0)
            {
                if(!EncroachFaultLogged)Logger::log("MapCheck: the editor's encroachment test faulted; static mesh overlaps are not checked");
                EncroachFaultLogged=true;
            }
            else
            {
                item.encroachTested=true;
                Address other=0;memcpy(&other,hit+kCheckResultActor,sizeof(other));
                if(result && other==levelInfo)item.encroachesBsp=true;
                else if(result && other && other!=actor && liveSet.count(other))item.encroachingActor=Path(other);
            }
        }
        scene.actors.push_back(std::move(item));
    }

    // The stock Check Map for Errors: every actor's CheckForErrors, with its
    // entries taken from MapCheck_Add instead of the stock dialog.
    if(settings.stockChecks)
    {
        std::vector<StockCapture> captured;
        size_t faults=0;
        {
            struct Capturing
            {
                explicit Capturing(std::vector<StockCapture>& into){stockCaptures=&into;MapCheckLog::SetCapture(CaptureStockEntry);}
                ~Capturing(){MapCheckLog::SetCapture(nullptr);stockCaptures=nullptr;}
            } capturing(captured);
            for(auto actor:live)if(!CallMethodRaw(Read<Address>(Read<Address>(actor)+kCheckForErrorsSlot),actor))++faults;
        }
        scene.stockRan=true;
        for(const auto& entry:captured)
        {
            MapCheck::StockEntry item;item.type=entry.type;item.message=entry.message;
            if(entry.actor && liveSet.count(entry.actor))item.actor=Path(entry.actor);
            scene.stock.push_back(std::move(item));
        }
        if(faults)Logger::log("MapCheck: CheckForErrors faulted on "+std::to_string(faults)+" actor(s)");
    }
    return scene;
}
// Selects a Map Check row: its actors, and its BSP surfaces (cleared first).
size_t SelectMapCheckRow(const std::vector<std::string>& actors,const std::vector<int>& surfaceIndices,bool frame)
{
    Exec("POLY SELECT NONE");
    size_t found=SelectActorPaths(actors,frame);
    if(surfaceIndices.empty())return found;
    auto model=Read<Address>(Level()+0x13c);
    auto all=model?Array(model+0x94,kSurfStride):std::vector<Address>{};
    for(int index:surfaceIndices)
        if(index>=0 && static_cast<size_t>(index)<all.size()){Write(all[static_cast<size_t>(index)]+0x14,Read<unsigned>(all[static_cast<size_t>(index)]+0x14)|kPolySelected);++found;}
    Redraw();
    return found;
}
