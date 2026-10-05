// Native reads for the Light and Shadow map (LightShadowModel.h): the walkable
// BSP floor triangles with their lightmap UVs, the lightmap atlases they use,
// the in-game lights and the BSP for line of sight. Included in
// WorkflowEditor.cpp after LightingBudgetNative.inl (its light-flag offsets).
// The model layout is the one RecoveredBspLighting reads.
namespace
{
    constexpr size_t kShadowNodeStride=0x5C,kShadowSectionStride=0x38,kShadowTextureStride=0x70,kShadowSurfStride=0x2C,kShadowVertexStride=0x28;
    // Invisible, not solid, fake backdrop, zone portal: no one stands on them.
    constexpr unsigned kShadowSkipFlags=0x00000001u|0x00000008u|0x00000080u|0x04000000u;
    constexpr Address kShadowLoadMip=0x111B2400;
    bool shadowFieldsLogged=false;

    float ShadowFloat(Address at)
    {
        const float v=Read<float>(at);
        if(!std::isfinite(v))throw std::runtime_error("The BSP holds a coordinate that is not a number. Rebuild the geometry.");
        return v;
    }
    LightShadow::Point ShadowPoint(Address at){return {ShadowFloat(at),ShadowFloat(at+4),ShadowFloat(at+8)};}
    // One lightmap atlas, copied, or an empty image when its format is not the
    // RGBA8 Build Lighting writes.
    LightShadow::Image ShadowAtlas(Address texture)
    {
        LightShadow::Image image;
        const Address compressed=texture+0x14;
        const int w=Read<int>(compressed+0x38),h=Read<int>(compressed+0x3C);
        if(Read<unsigned char>(compressed+0x34)!=5 || w<=0 || h<=0 || w>2048 || h>2048)return image;
        void* pixels=reinterpret_cast<void*(__thiscall*)(void*,int)>(kShadowLoadMip)(reinterpret_cast<void*>(compressed),0);
        const Address data=Read<Address>(compressed+0x10);
        const int bytes=Read<int>(compressed+0x14);
        if(!pixels || data!=reinterpret_cast<Address>(pixels) || bytes!=w*h*4)return image;
        image.width=w;image.height=h;image.pixels.resize(size_t(w)*h);
        if(!CopyMemorySafe(image.pixels.data(),pixels,size_t(bytes)))return {};
        return image;
    }
    // Once per session: the light properties the estimate reads, so a different
    // editor build shows up in the log rather than as a wrong map.
    void LogShadowFields(Address light)
    {
        if(shadowFieldsLogged)return;
        shadowFieldsLogged=true;
        std::string text="LightShadowMap: light fields";
        for(auto p:Properties(Read<Address>(light+0x24)))
        {
            const auto name=NameOf(p);
            const auto folded=Fold(name);
            if(folded.find("light")!=std::string::npos || folded.find("echelon")!=std::string::npos || folded.find("bright")!=std::string::npos)
                text+=" "+name+"@"+std::to_string(Read<int>(p+0x3c));
        }
        Logger::log(text);
    }
}
void LightShadowScene(LightShadow::Scene& scene)
{
    scene=LightShadow::Scene{};
    Engine();
    const auto level=Level();
    const auto model=Read<Address>(level+0x13c);
    if(!model)throw std::runtime_error("The map has no BSP. Build its geometry first.");
    const auto nodes=Array(model+0x54,kShadowNodeStride),sections=Array(model+0xD4,kShadowSectionStride);
    const auto textures=Array(model+0xE0,kShadowTextureStride),surfaces=Array(model+0x94,kShadowSurfStride);
    const auto verts=Array(model+0x64,8),points=Array(model+0x84,12);
    std::map<int,int> atlasOf;
    auto atlas=[&](int texture)->int
    {
        if(texture<0 || texture>=static_cast<int>(textures.size()))return -1;
        auto known=atlasOf.find(texture);
        if(known!=atlasOf.end())return known->second;
        auto image=ShadowAtlas(textures[texture]);
        int index=-1;
        if(image.width>0){index=static_cast<int>(scene.atlases.size());scene.atlases.push_back(std::move(image));}
        atlasOf[texture]=index;
        return index;
    };
    for(auto node:nodes)
    {
        const int count=Read<unsigned char>(node+0x5A);
        if(count<3)continue;
        // The plane faces the empty side: up for a floor, down for a ceiling.
        const auto normal=ShadowPoint(node);
        if(normal.z<LightShadow::kWalkable)continue;
        const int surface=Read<int>(node+0x2C);
        if(surface<0 || surface>=static_cast<int>(surfaces.size()))continue;
        if(Read<unsigned>(surfaces[surface]+0x14)&kShadowSkipFlags)continue;
        const int section=Read<short>(node+0x50),start=Read<short>(node+0x52);
        bool rendered=false;
        if(section>=0 && section<static_cast<int>(sections.size()) && start>=0)
        {
            const auto render=sections[section];
            const auto vertexData=Read<Address>(render+4);
            const int vertexCount=Read<int>(render+8);
            if(vertexData && vertexCount>=0 && vertexCount<=2000000 && start+count<=vertexCount)
            {
                // BuildRenderData's PC binding; the Xbox one is never used here.
                const int map=atlas(Read<int>(render+0x28));
                LightShadow::Point p[3];double u[3],v[3];
                auto vertex=[&](int i,int k)
                {
                    const Address at=vertexData+static_cast<Address>(start+i)*kShadowVertexStride;
                    p[k]=ShadowPoint(at);u[k]=Read<float>(at+0x20);v[k]=Read<float>(at+0x24);
                };
                vertex(0,0);
                for(int j=1;j+1<count;++j)
                {
                    vertex(j,1);vertex(j+1,2);
                    LightShadow::Floor f{p[0],p[1],p[2],{u[0],u[1],u[2]},{v[0],v[1],v[2]},map};
                    if(!std::isfinite(u[0]+u[1]+u[2]+v[0]+v[1]+v[2]))f.atlas=-1;
                    scene.floors.push_back(f);
                }
                rendered=true;
            }
        }
        if(rendered)continue;
        // No render data (geometry not built since a change): the polygon's
        // own points, without baked lighting.
        const int pool=Read<int>(node+0x28);
        if(pool<0 || pool+count>static_cast<int>(verts.size()))continue;
        auto corner=[&](int i)
        {
            const unsigned point=Read<unsigned short>(verts[pool+i]);
            if(point>=points.size())throw std::runtime_error("The BSP has an invalid floor point. Rebuild the geometry.");
            return ShadowPoint(points[point]);
        };
        const auto first=corner(0);
        for(int j=1;j+1<count;++j)scene.floors.push_back(LightShadow::Floor{first,corner(j),corner(j+1)});
    }
    // In-game and dynamic lights: the Lighting Budget's "in game" set, at the
    // render sphere the editor's own light checks use.
    for(auto actor:LiveActors())
    {
        if(!Read<unsigned char>(actor+kLightType))continue;
        LightingBudget::LightFlags flags;
        const auto a=Read<unsigned>(actor+kLightFlagsA),b=Read<unsigned>(actor+kLightFlagsB);
        flags.type=Read<unsigned char>(actor+kLightType);flags.effect=Read<unsigned char>(actor+kLightEffect);
        flags.staticFlag=(b&kStaticBit)!=0;flags.inGameFlag=(b&kInGameBit)!=0;
        flags.dynamicFlag=(a&kDynamicLightBit)!=0;flags.zoneLimited=(a&kZoneLimitedBit)!=0;
        flags.animA=Read<float>(actor+kLightAnimA);flags.animB=Read<float>(actor+kLightAnimB);
        if(!LightingBudget::CountsInGame(flags))continue;
        LogShadowFields(actor);
        auto render=reinterpret_cast<Address>(reinterpret_cast<void*(__thiscall*)(void*)>(kLightRenderData)(reinterpret_cast<void*>(actor)));
        if(!render)continue;
        LightShadow::Light light;
        light.at={Read<float>(render+0x1c),Read<float>(render+0x20),Read<float>(render+0x24)};
        light.radius=Read<float>(render+0x34);
        light.brightness=NumberProperty(actor,"LightBrightness",64)
            *LightShadow::ColourShare(static_cast<int>(NumberProperty(actor,"LightHue",0)),static_cast<int>(NumberProperty(actor,"LightSaturation",255)));
        if(!std::isfinite(light.at.x+light.at.y+light.at.z+light.radius+light.brightness))continue;
        scene.lights.push_back(light);
    }
    // The BSP's planes and solidity, for line of sight to each light.
    scene.rootOutside=Read<int>(model+0x104)!=0;
    for(auto node:nodes)
    {
        const auto normal=ShadowPoint(node);
        scene.bsp.push_back({normal,Read<float>(node+0xC),Read<int>(node+0x34),Read<int>(node+0x30),
            Read<unsigned char>(node+0x5A)!=0 && !(Read<unsigned char>(node+0x5B)&0x21)});
    }
}
