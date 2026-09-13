#include "pch.h"
#include "RecoveredBspLighting.h"
#include "logger.h"
#include "LocalLightingMatch.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <array>

namespace RecoveredBspLighting
{
namespace
{
    template<class T> T Read(const void* p, size_t offset)
    {
        T v{}; memcpy(&v, static_cast<const unsigned char*>(p) + offset, sizeof(v)); return v;
    }
    struct Array
    {
        unsigned char* data;
        int count, capacity;
    };
    Array GetArray(const void* p, size_t offset, int limit = 1000000)
    {
        auto a = Read<Array>(p, offset);
        if (a.count < 0 || a.count > limit || (a.count && !a.data))
            throw std::runtime_error("Invalid BSP lighting array");
        return a;
    }
    struct V
    {
        double x{}, y{}, z{};
        V operator+(V b) const { return {x+b.x,y+b.y,z+b.z}; }
        V operator-(V b) const { return {x-b.x,y-b.y,z-b.z}; }
        V operator*(double s) const { return {x*s,y*s,z*s}; }
    };
    double Dot(V a,V b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
    V Cross(V a,V b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
    V Position(const void* p)
    {
        V v{Read<float>(p,0),Read<float>(p,4),Read<float>(p,8)};
        if (!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))
            throw std::runtime_error("Nonfinite BSP lighting coordinate");
        return v;
    }
    struct Image { int w{},h{}; std::vector<uint32_t> pixels; };
    struct Triangle
    {
        V a,b,c,normal,minimum,maximum;
        double u[3]{},v[3]{},distance{},denominator{};
        int atlas[2]{-1,-1};
        bool Weights(V p,double& bWeight,double& cWeight) const
        {
            V e=b-a,f=c-a,q=p-a;
            const double ee=Dot(e,e),ef=Dot(e,f),ff=Dot(f,f);
            bWeight=(Dot(q,e)*ff-Dot(q,f)*ef)/denominator;
            cWeight=(Dot(q,f)*ee-Dot(q,e)*ef)/denominator;
            return bWeight>=-0.0001 && cWeight>=-0.0001 && bWeight+cWeight<=1.0001;
        }
    };
    // UE's row-vector FMatrix convention, as used by BuildRenderData.
    V Transform(const double m[4][4],V p)
    {
        return {p.x*m[0][0]+p.y*m[1][0]+p.z*m[2][0]+m[3][0],
                p.x*m[0][1]+p.y*m[1][1]+p.z*m[2][1]+m[3][1],
                p.x*m[0][2]+p.y*m[1][2]+p.z*m[2][2]+m[3][2]};
    }
    bool Invert(const double m[4][4],double inverse[4][4])
    {
        double a[4][8]{};
        for(int r=0;r<4;++r) for(int c=0;c<4;++c) { a[r][c]=m[r][c];a[r][c+4]=r==c; }
        for(int c=0;c<4;++c)
        {
            int pivot=c;
            for(int r=c+1;r<4;++r) if(std::abs(a[r][c])>std::abs(a[pivot][c])) pivot=r;
            if(std::abs(a[pivot][c])<1e-14) return false;
            for(int k=0;k<8;++k) std::swap(a[c][k],a[pivot][k]);
            const double divisor=a[c][c];for(double& v:a[c]) v/=divisor;
            for(int r=0;r<4;++r) if(r!=c)
            {
                const double f=a[r][c];for(int k=0;k<8;++k) a[r][k]-=f*a[c][k];
            }
        }
        for(int r=0;r<4;++r) for(int c=0;c<4;++c) inverse[r][c]=a[r][c+4];
        return true;
    }
    uint32_t Sample(const Image& image,double u,double v)
    {
        const double x=std::clamp(u*image.w-0.5,0.0,double(image.w-1));
        const double y=std::clamp(v*image.h-0.5,0.0,double(image.h-1));
        const int x0=int(x),y0=int(y),x1=(std::min)(x0+1,image.w-1),y1=(std::min)(y0+1,image.h-1);
        uint32_t result=0;
        for(int channel=0;channel<4;++channel)
        {
            auto component=[&](int px,int py) {return double((image.pixels[py*image.w+px]>>(channel*8))&255);};
            const double top=component(x0,y0)*(1-(x-x0))+component(x1,y0)*(x-x0);
            const double bottom=component(x0,y1)*(1-(x-x0))+component(x1,y1)*(x-x0);
            result|=uint32_t(std::clamp(top*(1-(y-y0))+bottom*(y-y0)+0.5,0.0,255.0))<<(channel*8);
        }
        return result;
    }
}
class Snapshot
{
public:
    AssetPath path{};
    std::vector<Image> images;
    std::unordered_map<std::string,std::vector<Triangle>> triangles;
    std::string error;
    std::vector<uint64_t> savedAtlases;
    size_t mapped{},unmapped{},atlases{};
    bool selective = false;
    std::vector<std::array<int,6>> chartLayouts;
    std::unordered_set<int> selectedCharts;
    std::vector<int> nodeCharts;
    bool recordNative = false;
    bool localMatch = false;
    std::vector<Image> nativeImages;
    size_t matchedTexels{}, unmatchedTexels{};
    std::vector<LocalLightingMatch::BspNode> visibility;
    bool rootOutside = false;
};
static std::shared_ptr<Snapshot> active;

std::shared_ptr<Snapshot> Capture(void* model,AssetPath path,const char* package,std::string& error,
    const std::unordered_set<int>* excludedSurfaces)
{
    try
    {
        auto snapshot=std::make_shared<Snapshot>();snapshot->path=path;
        const auto textures=GetArray(model,0xE0,4096);
        for(int i=0;i<textures.count;++i)
        {
            auto* compressed=textures.data+i*0x70+0x14;
            Image image;image.w=Read<int>(compressed,0x38);image.h=Read<int>(compressed,0x3C);
            // Native CompressLightmaps writes TEXF_RGBA8 (5). Do not guess
            // the layout of another format or read beyond its mip allocation.
            if(Read<unsigned char>(compressed,0x34)!=5 || image.w<=0 || image.h<=0
                || image.w>2048 || image.h>2048)
                throw std::runtime_error("Unsupported original BSP lightmap texture format or size");
            using LoadMip=void*(__thiscall*)(void*,int);
            void* pixels=reinterpret_cast<LoadMip>(0x111B2400)(compressed,0);
            auto mip=GetArray(compressed,0x10,2048*2048*4);
            const size_t bytes=size_t(image.w)*image.h*4;
            if(!pixels || mip.data!=pixels || mip.count!=int(bytes))
                throw std::runtime_error("Original BSP lightmap mip size does not match its dimensions");
            image.pixels.resize(bytes/4);memcpy(image.pixels.data(),pixels,bytes);
            snapshot->images.push_back(std::move(image));
        }
        const auto nodes=GetArray(model,0x54),sections=GetArray(model,0xD4),surfaces=GetArray(model,0x94);
        if(excludedSurfaces) {
            snapshot->selective=true;
            for(int n=0;n<nodes.count;++n) snapshot->nodeCharts.push_back(Read<int>(nodes.data+n*0x5C,0x3C));
            const auto charts=GetArray(model,0xEC);
            for(int i=0;i<charts.count;++i) {
                const auto chart=charts.data+i*0xA4;
                snapshot->chartLayouts.push_back({Read<int>(chart,8),Read<int>(chart,0x10),
                    Read<int>(chart,0x14),Read<int>(chart,0x18),Read<int>(chart,0x1C),Read<int>(chart,0x20)});
                bool selected=false,unselected=false;
                for(int n=0;n<nodes.count;++n) {
                    auto node=nodes.data+n*0x5C;
                    if(node[0x5A]<3 || Read<int>(node,0x3C)!=i) continue;
                    if(excludedSurfaces->count(Read<int>(node,0x2C))) selected=true;else unselected=true;
                }
                if(selected && unselected) throw std::runtime_error("Selected and unselected faces share a lighting chart. Select the adjoining faces too.");
                if(selected) snapshot->selectedCharts.insert(i);
            }
        }
        for(int i=0;i<nodes.count;++i)
        {
            const auto* node=nodes.data+i*0x5C;
            const int count=node[0x5A],section=Read<int16_t>(node,0x50),start=Read<int16_t>(node,0x52);
            if(count<3 || section<0) continue;
            if(section>=sections.count || start<0) throw std::runtime_error("Invalid original BSP render section");
            const auto* render=sections.data+section*0x38;
            // BuildRenderData (110D1500) uses the secondary binding only on
            // Xbox. PC source files may retain an out-of-range Xbox index.
            const auto context=*reinterpret_cast<unsigned char**>(0x11691D7C);
            const bool xbox=context && Read<int>(context,0x78)==1;
            const int primary=Read<int>(render,0x28),secondary=xbox ? Read<int>(render,0x2C) : -1;
            if(primary<0 && secondary<0) continue;
            if(primary>=textures.count || secondary>=textures.count) throw std::runtime_error("Invalid original BSP lightmap binding");
            const auto vertices=GetArray(render,4);
            const int surface=Read<int>(node,0x2C);
            if(excludedSurfaces && excludedSurfaces->count(surface)) continue;
            if(count>vertices.count || start>vertices.count-count || surface<0 || surface>=surfaces.count)
                throw std::runtime_error("Invalid original BSP lighting vertices");
            char material[1024]{};
            if(!path(Read<void*>(surfaces.data+surface*0x2C,0x10),material,sizeof(material),package)) continue;
            auto& triangles=snapshot->triangles[material];
            for(int j=1;j<count-1;++j)
            {
                Triangle triangle;int indices[]={start,start+j,start+j+1};
                V positions[3];
                for(int k=0;k<3;++k)
                {
                    const auto* vertex=vertices.data+indices[k]*0x28;
                    positions[k]=Position(vertex);triangle.u[k]=Read<float>(vertex,0x20);triangle.v[k]=Read<float>(vertex,0x24);
                    if(!std::isfinite(triangle.u[k])||!std::isfinite(triangle.v[k])) throw std::runtime_error("Invalid original BSP lightmap UV");
                }
                triangle.a=positions[0];triangle.b=positions[1];triangle.c=positions[2];
                V e=triangle.b-triangle.a,f=triangle.c-triangle.a;
                triangle.normal=Cross(e,f);const double length=std::sqrt(Dot(triangle.normal,triangle.normal));
                triangle.denominator=Dot(e,e)*Dot(f,f)-Dot(e,f)*Dot(e,f);
                if(length<1e-6 || triangle.denominator<1e-12) continue;
                triangle.normal=Position(node);
                const double normalLength=std::sqrt(Dot(triangle.normal,triangle.normal));
                if(normalLength<0.99 || normalLength>1.01) throw std::runtime_error("Invalid original BSP plane normal");
                triangle.normal=triangle.normal*(1/normalLength);triangle.distance=Dot(triangle.normal,triangle.a);
                triangle.minimum={(std::min)({triangle.a.x,triangle.b.x,triangle.c.x}),
                    (std::min)({triangle.a.y,triangle.b.y,triangle.c.y}),(std::min)({triangle.a.z,triangle.b.z,triangle.c.z})};
                triangle.maximum={(std::max)({triangle.a.x,triangle.b.x,triangle.c.x}),
                    (std::max)({triangle.a.y,triangle.b.y,triangle.c.y}),(std::max)({triangle.a.z,triangle.b.z,triangle.c.z})};
                triangle.atlas[0]=primary;triangle.atlas[1]=secondary;
                triangles.push_back(triangle);
            }
        }
        Logger::log("MapRecovery: captured original BSP lighting from "+std::to_string(textures.count)+" atlas textures");
        return snapshot;
    }
    catch(const std::exception& exception) { error=exception.what();return {}; }
}
void Activate(const std::shared_ptr<Snapshot>& snapshot)
{
    active=snapshot;
    if(active) { active->error.clear();active->mapped=active->unmapped=active->atlases=0; }
}
void Deactivate() { active.reset(); }

void RecordNativeBake(const std::shared_ptr<Snapshot>& snapshot) {
    if(snapshot->triangles.empty())
        throw std::runtime_error("Leave original BSP surfaces unselected to provide lighting references. No lighting was changed.");
    snapshot->recordNative=true;
}

std::shared_ptr<Snapshot> PrepareLocalMatch(void* model, const std::shared_ptr<Snapshot>& snapshot,
    const std::unordered_set<int>& selectedSurfaces, std::string& error)
{
    // Capture after the first bake: donor UVs now address the freshly packed
    // atlases, whose unselected charts have already been restored exactly.
    auto result=Capture(model,snapshot->path,"MyLevel",error,&selectedSurfaces);
    if(!result) return {};
    if(result->images.size()!=snapshot->nativeImages.size()) {
        error="The native reference bake did not produce every lighting atlas."; return {};
    }
    for(size_t i=0;i<result->images.size();++i) {
        if(result->images[i].w!=snapshot->nativeImages[i].w || result->images[i].h!=snapshot->nativeImages[i].h) {
            error="The native reference atlas dimensions do not match."; return {};
        }
    }
    result->nativeImages=std::move(snapshot->nativeImages);
    result->localMatch=true;
    result->rootOutside=Read<int>(model,0x104)!=0;
    const auto nodes=GetArray(model,0x54);
    for(int n=0;n<nodes.count;++n) {
        const auto node=nodes.data+n*0x5C;
        const auto normal=Position(node);
        result->visibility.push_back({{normal.x,normal.y,normal.z},Read<float>(node,0xC),
            Read<int>(node,0x34),Read<int>(node,0x30),node[0x5A]!=0 && !(node[0x5B]&0x21)});
    }
    return result;
}

std::string LocalMatchReport(const std::shared_ptr<Snapshot>& snapshot) {
    return "Matched "+std::to_string(snapshot->matchedTexels)+" lighting texels to nearby original surfaces.\n"
        +std::to_string(snapshot->unmatchedTexels)+" texels had no suitable reference and kept the native bake.\n\n"
        "This is an approximate colour and brightness match. Check the result in game, especially shadows. "
        "It does not reconstruct missing original lights or automatically select shadow receivers.";
}

namespace {
void MatchChart(Snapshot& snapshot, void* pixels, const void* model, const unsigned char* chart,
    int chartIndex, int textureIndex)
{
    const auto nodes=GetArray(model,0x54),surfaces=GetArray(model,0x94),verts=GetArray(model,0x64),points=GetArray(model,0x84);
    const int x=Read<int>(chart,0x14),y=Read<int>(chart,0x18),w=Read<int>(chart,0x1C),h=Read<int>(chart,0x20);
    const int layer=Read<int>(chart,8)==textureIndex ? 0 : 1;
    const unsigned char* node=nullptr;
    for(int n=0;n<nodes.count;++n)
        if(nodes.data[n*0x5C+0x5A]>=3 && Read<int>(nodes.data+n*0x5C,0x3C)==chartIndex) {node=nodes.data+n*0x5C;break;}
    if(!node) { snapshot.unmatchedTexels+=size_t(w)*h; return; }
    const int surface=Read<int>(node,0x2C),start=Read<int>(node,0x28);
    if(surface<0 || surface>=surfaces.count || start<0 || start>=verts.count) throw std::runtime_error("Invalid matching surface.");
    const unsigned point=Read<uint16_t>(verts.data,start*8);
    if(point>=unsigned(points.count)) throw std::runtime_error("Invalid matching surface point.");
    const V anchor=Position(points.data+point*12),normal=Position(node);
    char material[1024]{};
    if(!snapshot.path(Read<void*>(surfaces.data+surface*0x2C,0x10),material,sizeof(material),"MyLevel")) {
        snapshot.unmatchedTexels+=size_t(w)*h; return;
    }
    auto found=snapshot.triangles.find(material);
    std::vector<const Triangle*> candidates;
    if(found!=snapshot.triangles.end()) for(const auto& t:found->second) {
        // Allow a small wall-thickness offset, but require the same facing and
        // material. The segment check below rejects intervening solid BSP.
        if(Dot(t.normal,normal)>0.9999 && std::abs(Dot(t.normal,anchor)-t.distance)<=16.0
            && t.atlas[layer]>=0) candidates.push_back(&t);
    }
    if(candidates.empty()) { snapshot.unmatchedTexels+=size_t(w)*h; return; }
    double matrix[4][4],inverse[4][4];
    for(int r=0;r<4;++r) for(int c=0;c<4;++c) {
        matrix[r][c]=Read<float>(chart,0x28+(r*4+c)*4);
        if(!std::isfinite(matrix[r][c])) throw std::runtime_error("Invalid matching chart transform.");
    }
    if(!Invert(matrix,inverse)) throw std::runtime_error("Singular matching chart transform.");
    const auto local=Transform(matrix,anchor);
    auto convert=[](V v) {return LocalLightingMatch::Point{v.x,v.y,v.z};};
    for(int py=0;py<h;++py) for(int px=0;px<w;++px) {
        const auto world=Transform(inverse,{px+0.5,py+0.5,local.z});
        const Triangle* nearest=nullptr;
        double distance=512.0*512.0,wb=0,wc=0;
        for(const auto* t:candidates) {
            // Cheap AABB rejection before the closest-triangle calculation.
            if(world.x<t->minimum.x-512 || world.x>t->maximum.x+512
                || world.y<t->minimum.y-512 || world.y>t->maximum.y+512
                || world.z<t->minimum.z-512 || world.z>t->maximum.z+512) continue;
            double b,c;
            const double d=LocalLightingMatch::Nearest(convert(world),convert(t->a),convert(t->b),convert(t->c),b,c);
            const auto reference=t->a*(1-b-c)+t->b*b+t->c*c;
            // Move the connecting segment outside both almost coplanar faces.
            // This checks BSP only: mesh occlusion remains the native baker's.
            if(d<distance && LocalLightingMatch::ClearSegment(snapshot.visibility,snapshot.rootOutside,
                convert(world+normal*18),convert(reference+normal*18))) {distance=d;nearest=t;wb=b;wc=c;}
        }
        if(!nearest) {++snapshot.unmatchedTexels;continue;}
        const double wa=1-wb-wc;
        const double u=nearest->u[0]*wa+nearest->u[1]*wb+nearest->u[2]*wc;
        const double v=nearest->v[0]*wa+nearest->v[1]*wb+nearest->v[2]*wc;
        const int atlas=nearest->atlas[layer];
        auto& target=static_cast<uint32_t*>(pixels)[(y+py)*512+x+px];
        target=LocalLightingMatch::Correct(target,Sample(snapshot.images[atlas],u,v),Sample(snapshot.nativeImages[atlas],u,v));
        ++snapshot.matchedTexels;
    }
}
}

void RestoreAtlas(void* pixels,const void* model,const void* texture) noexcept
{
    if(!active || !active->error.empty()) return;
    try
    {
        auto& snapshot=*active;
        const auto lightmaps=GetArray(model,0xEC),indices=GetArray(texture,8),nodes=GetArray(model,0x54);
        const auto surfaces=GetArray(model,0x94),verts=GetArray(model,0x64),points=GetArray(model,0x84),textures=GetArray(model,0xE0);
        const auto textureOffset=static_cast<const unsigned char*>(texture)-textures.data;
        if(textureOffset<0 || textureOffset%0x70 || textureOffset/0x70>=textures.count) throw std::runtime_error("Invalid target BSP atlas");
        const int textureIndex=int(textureOffset/0x70);
        ++snapshot.atlases;
        if(snapshot.recordNative) {
            if(snapshot.nativeImages.size()!=size_t(textures.count)) snapshot.nativeImages.resize(textures.count);
            auto& image=snapshot.nativeImages[textureIndex];image.w=image.h=512;
            const auto* begin=static_cast<const uint32_t*>(pixels);
            image.pixels.assign(begin,begin+512*512);
        }
        if(snapshot.selective) {
            // LIGHT APPLY repacks atlases. Match unchanged chart identities and
            // dimensions, then copy their texels to their new atlas rectangles.
            if(nodes.count!=int(snapshot.nodeCharts.size()))
                throw std::runtime_error("Lighting chart layout changed during selective recalculation.");
            std::vector<int> sourceCharts(lightmaps.count,-1);
            for(int n=0;n<nodes.count;++n) {
                const auto node=nodes.data+n*0x5C;
                if(node[0x5A]<3) continue;
                const int target=Read<int>(node,0x3C),source=snapshot.nodeCharts[n];
                if(target<0 || target>=lightmaps.count) continue;
                if(source<0 || source>=int(snapshot.chartLayouts.size()))
                    throw std::runtime_error("A surface has no original lighting chart. Build geometry before selective recalculation.");
                if(sourceCharts[target]>=0 && sourceCharts[target]!=source)
                    throw std::runtime_error("Lighting charts merged during selective recalculation. Reopen the saved map before retrying.");
                sourceCharts[target]=source;
            }
            for(int entry=0;entry<indices.count;++entry) {
                const int i=Read<int>(indices.data,entry*4);
                if(i<0 || i>=lightmaps.count) throw std::runtime_error("Invalid selective lighting chart index.");
                auto chart=lightmaps.data+i*0xA4;
                const std::array<int,6> layout={Read<int>(chart,8),Read<int>(chart,0x10),Read<int>(chart,0x14),
                    Read<int>(chart,0x18),Read<int>(chart,0x1C),Read<int>(chart,0x20)};
                if(sourceCharts[i]<0) continue;
                const auto& old=snapshot.chartLayouts[sourceCharts[i]];
                if(layout[4]!=old[4] || layout[5]!=old[5]) throw std::runtime_error("Lighting chart dimensions changed. Reopen the saved map before retrying.");
                if(layout[0]!=textureIndex && layout[1]!=textureIndex) continue;
                if(layout[2]<0 || layout[3]<0 || layout[4]<=0 || layout[5]<=0
                    || layout[4]>512 || layout[5]>512 || layout[2]>512-layout[4] || layout[3]>512-layout[5])
                    throw std::runtime_error("Selective lighting chart exceeds its atlas.");
                if(snapshot.selectedCharts.count(sourceCharts[i])) {
                    if(snapshot.localMatch) MatchChart(snapshot,pixels,model,chart,i,textureIndex);
                    continue;
                }
                const int source=old[layout[0]==textureIndex ? 0 : 1];
                if(source<0 || source>=int(snapshot.images.size())) throw std::runtime_error("Missing original selective lighting atlas.");
                const auto& original=snapshot.images[source];
                if(old[2]<0 || old[3]<0 || old[2]>original.w-old[4] || old[3]>original.h-old[5])
                    throw std::runtime_error("Original selective lighting chart exceeds its atlas.");
                for(int row=0;row<layout[5];++row) {
                    const int offset=(layout[3]+row)*512+layout[2];
                    const int sourceOffset=(old[3]+row)*original.w+old[2];
                    memcpy(static_cast<uint32_t*>(pixels)+offset,original.pixels.data()+sourceOffset,layout[4]*sizeof(uint32_t));
                }
                snapshot.mapped+=size_t(layout[4])*layout[5];
            }
            return;
        }
        for(int entry=0;entry<indices.count;++entry)
        {
            const int index=Read<int>(indices.data,entry*4);
            if(index<0 || index>=lightmaps.count) throw std::runtime_error("Invalid target BSP lightmap index");
            const auto* lightmap=lightmaps.data+index*0xA4;
            const int x=Read<int>(lightmap,0x14),y=Read<int>(lightmap,0x18),w=Read<int>(lightmap,0x1C),h=Read<int>(lightmap,0x20);
            if(x<0 || y<0 || w<=0 || h<=0 || w>512 || h>512 || x>512-w || y>512-h) throw std::runtime_error("Target BSP chart exceeds the atlas");
            const int layer=Read<int>(lightmap,8)==textureIndex ? 0 : 1;
            if(layer==1 && Read<int>(lightmap,0x10)!=textureIndex) throw std::runtime_error("Target BSP atlas binding mismatch");
            const unsigned char* node=nullptr;
            for(int n=0;n<nodes.count;++n) if(nodes.data[n*0x5C+0x5A]>=3 && Read<int>(nodes.data+n*0x5C,0x3C)==index) {node=nodes.data+n*0x5C;break;}
            if(!node) continue;
            const int surface=Read<int>(node,0x2C),start=Read<int>(node,0x28);
            if(surface<0 || surface>=surfaces.count || start<0 || start>=verts.count) throw std::runtime_error("Invalid target BSP chart surface");
            const unsigned point=Read<uint16_t>(verts.data,start*8);
            if(point>=unsigned(points.count)) throw std::runtime_error("Invalid target BSP chart point");
            const V anchor=Position(points.data+point*12),normal=Position(node);
            char material[1024]{};
            if(!snapshot.path(Read<void*>(surfaces.data+surface*0x2C,0x10),material,sizeof(material),nullptr)) continue;
            const auto found=snapshot.triangles.find(material);
            if(found==snapshot.triangles.end()) continue;
            double matrix[4][4],inverse[4][4];
            for(int r=0;r<4;++r) for(int c=0;c<4;++c)
            {
                matrix[r][c]=Read<float>(lightmap,0x28+(r*4+c)*4);
                if(!std::isfinite(matrix[r][c])) throw std::runtime_error("Nonfinite BSP chart transform");
            }
            if(!Invert(matrix,inverse)) throw std::runtime_error("Singular BSP chart transform");
            const V local=Transform(matrix,anchor),roundTrip=Transform(inverse,local);
            if(Dot(roundTrip-anchor,roundTrip-anchor)>0.01) throw std::runtime_error("BSP chart transform does not round trip");
            V minimum{1e100,1e100,1e100},maximum{-1e100,-1e100,-1e100};
            for(int cy:{0,h}) for(int cx:{0,w})
            {
                const V corner=Transform(inverse,{double(cx),double(cy),local.z});
                if(std::abs(Dot(corner-anchor,normal))>0.1)
                    throw std::runtime_error("BSP chart coordinates leave their surface plane");
                minimum={(std::min)(minimum.x,corner.x),(std::min)(minimum.y,corner.y),(std::min)(minimum.z,corner.z)};
                maximum={(std::max)(maximum.x,corner.x),(std::max)(maximum.y,corner.y),(std::max)(maximum.z,corner.z)};
            }
            std::vector<const Triangle*> candidates;
            for(const auto& t:found->second)
                if(Dot(t.normal,normal)>0.9999 && std::abs(Dot(t.normal,anchor)-t.distance)<0.1 && t.atlas[layer]>=0
                    && t.minimum.x<=maximum.x+0.1 && t.maximum.x>=minimum.x-0.1
                    && t.minimum.y<=maximum.y+0.1 && t.maximum.y>=minimum.y-0.1
                    && t.minimum.z<=maximum.z+0.1 && t.maximum.z>=minimum.z-0.1) candidates.push_back(&t);
            if(candidates.empty()) continue;
            const Triangle* previous=nullptr;
            for(int py=0;py<h;++py) for(int px=0;px<w;++px)
            {
                const V world=Transform(inverse,{double(px)+0.5,double(py)+0.5,local.z});
                double b=0,c=0;const Triangle* selected=previous;
                if(!selected || !selected->Weights(world,b,c))
                {
                    selected=nullptr;
                    for(const auto* candidate:candidates)
                        if(world.x>=candidate->minimum.x-0.1 && world.x<=candidate->maximum.x+0.1
                            && world.y>=candidate->minimum.y-0.1 && world.y<=candidate->maximum.y+0.1
                            && world.z>=candidate->minimum.z-0.1 && world.z<=candidate->maximum.z+0.1
                            && candidate->Weights(world,b,c)) {selected=candidate;break;}
                }
                if(!selected) {++snapshot.unmapped;continue;}
                previous=selected;
                const double a=1-b-c;
                static_cast<uint32_t*>(pixels)[(y+py)*512+x+px]=Sample(snapshot.images[selected->atlas[layer]],
                    selected->u[0]*a+selected->u[1]*b+selected->u[2]*c,selected->v[0]*a+selected->v[1]*b+selected->v[2]*c);
                ++snapshot.mapped;
            }
        }
    }
    catch(const std::exception& exception) { active->error=exception.what(); }
    catch(...) { active->error="Unknown failure transferring BSP lighting"; }
}
bool Result(const std::shared_ptr<Snapshot>& snapshot,std::string& error,bool allowUnmatched)
{
    if(!snapshot) {error="No original BSP lighting snapshot";return false;}
    if(!snapshot->error.empty()) {error=snapshot->error;return false;}
    if(!allowUnmatched && !snapshot->triangles.empty() && !snapshot->mapped) {error="No original BSP lighting could be mapped onto the rebuilt surfaces";return false;}
    Logger::log("MapRecovery: transferred original BSP lighting to "+std::to_string(snapshot->mapped)
        +" texels across "+std::to_string(snapshot->atlases)+" atlases; unmatched chart texels="+std::to_string(snapshot->unmapped));
    return true;
}
bool CheckSavedAtlases(void* model,const std::shared_ptr<Snapshot>& snapshot,bool remember,std::string& error)
{
    if(!snapshot || !snapshot->error.empty())
    {
        error=snapshot ? snapshot->error : "Missing BSP lighting snapshot";
        return false;
    }
    try
    {
        std::vector<uint64_t> hashes;
        const auto textures=GetArray(model,0xE0,4096);
        for(int i=0;i<textures.count;++i)
        {
            auto* compressed=textures.data+i*0x70+0x14;
            const int w=Read<int>(compressed,0x38),h=Read<int>(compressed,0x3C);
            if(Read<unsigned char>(compressed,0x34)!=5 || w<=0 || h<=0 || w>2048 || h>2048)
                throw std::runtime_error("Cannot verify saved BSP atlas format");
            using LoadMip=void*(__thiscall*)(void*,int);
            reinterpret_cast<LoadMip>(0x111B2400)(compressed,0);
            const auto mip=GetArray(compressed,0x10,2048*2048*4);
            if(mip.count!=w*h*4) throw std::runtime_error("Cannot verify saved BSP atlas size");
            uint64_t hash=14695981039346656037ull;
            for(int k=0;k<mip.count;++k) {hash^=mip.data[k];hash*=1099511628211ull;}
            hashes.push_back(hash);hashes.push_back((uint64_t(w)<<32)|uint32_t(h));
        }
        if(remember) snapshot->savedAtlases=std::move(hashes);
        else if(hashes!=snapshot->savedAtlases)
            throw std::runtime_error("The ordinary save changed preserved BSP lighting. Do not use the saved outputs");
        return true;
    }
    catch(const std::exception& exception) {error=exception.what();return false;}
}
}
