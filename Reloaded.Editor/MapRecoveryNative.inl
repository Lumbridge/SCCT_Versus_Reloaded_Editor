// File > Recover Compiled Map's report (MapRecoveryModel.h): selecting and
// framing the actors and BSP surfaces it lists. Included by WorkflowEditor.cpp.
//
// Recovered brushes keep their vertices in world space with the actor at the
// origin, so CAMERA ALIGN (which goes to actor locations) cannot frame them;
// they are framed by the built BSP surfaces they own instead.
namespace
{
    struct FrameBounds
    {
        std::array<double,3> low{},high{};
        bool any=false;
        void Add(const std::array<float,3>& point)
        {
            for(int axis=0;axis<3;++axis)
            {
                low[axis]=any?std::min(low[axis],double(point[axis])):point[axis];
                high[axis]=any?std::max(high[axis],double(point[axis])):point[axis];
            }
            any=true;
        }
    };

    // Adds the vertices of every built BSP node on one of the surfaces.
    void AddSurfaceNodes(const std::set<int>& surfaces,FrameBounds& bounds)
    {
        auto model=Read<Address>(Level()+0x13c);
        if(!model || surfaces.empty())return;
        const auto verts=Array(model+0x64,8);
        const auto points=Array(model+0x84,12);
        for(auto node:Array(model+0x54,0x5c))
        {
            const int count=Read<unsigned char>(node+0x5a),start=Read<int>(node+0x28);
            if(count<3 || !surfaces.count(Read<int>(node+0x2c)) || start<0 || size_t(start)+count>verts.size())continue;
            for(int i=0;i<count;++i)
            {
                const unsigned point=Read<unsigned short>(verts[size_t(start)+i]);
                if(point<points.size())bounds.Add(Read<std::array<float,3>>(points[point]));
            }
        }
    }

    // Built surfaces whose master polygon belongs to the actor (as
    // polyUpdateMaster, 0x110883b0, follows them).
    std::set<int> OwnedSurfaces(Address actor)
    {
        std::set<int> result;
        auto model=Read<Address>(Level()+0x13c);
        if(!model)return result;
        int index=0;
        for(auto surface:Array(model+0x94,0x2c))
        {
            auto poly=Read<Address>(surface+0x20);
            if(poly && Read<Address>(poly+0x14c)==actor)result.insert(index);
            ++index;
        }
        return result;
    }

    // Perspective viewports get the thumbnail framing at their own yaw; the
    // orthographic ones (13 top, 14 front, 15 side) are centred on the box
    // along the two axes they show.
    void FocusViewports(const FrameBounds& bounds)
    {
        if(!bounds.any)return;
        for(auto camera:Viewports())
        {
            if(!camera)continue;
            const int mode=Read<int>(camera+0x4fc);
            if(mode==13 || mode==14 || mode==15)
            {
                auto position=Position(camera);
                const int depth=mode==13?2:mode==14?1:0;
                for(int axis=0;axis<3;++axis)
                    if(axis!=depth)position[axis]=(bounds.low[axis]+bounds.high[axis])/2;
                SetPosition(camera,position);
                continue;
            }
            auto rotationField=Field(camera,"Rotation");
            float fov=90;
            if(auto p=Property(camera,"FovAngle"))fov=Read<float>(camera+Read<int>(p+0x3c));
            auto shot=Thumbnail::FrameBox(bounds.low,bounds.high,Read<Rotation>(rotationField)[1],fov,4.0/3.0);
            SetPosition(camera,shot.location);
            Write(rotationField,Rotation{shot.rotation[0],shot.rotation[1],shot.rotation[2]});
        }
    }

    void SetActorSelected(Address actor,bool on)
    {
        reinterpret_cast<void(__thiscall*)(void*,void*,void*,int,int)>(0x10eb9a20)(
            reinterpret_cast<void*>(Engine()),reinterpret_cast<void*>(Level()),reinterpret_cast<void*>(actor),on?1:0,0);
    }
}
size_t SelectActorNames(const std::vector<std::string>& names,bool focus)
{
    std::set<std::string> wanted;
    for(const auto& name:names)wanted.insert(Fold(name));
    Exec("POLY SELECT NONE");
    size_t selected=0;
    FrameBounds bounds;
    bool brushOnly=true;
    for(auto actor:LiveActors())
    {
        const bool on=wanted.count(Fold(NameOf(actor)))!=0;
        SetActorSelected(actor,on);
        if(!on)continue;
        ++selected;
        if(IsA(actor,"Brush"))AddSurfaceNodes(OwnedSurfaces(actor),bounds);
        else brushOnly=false;
    }
    if(focus && selected)
    {
        if(brushOnly && bounds.any)FocusViewports(bounds);
        else Exec("CAMERA ALIGN");
    }
    Call(Engine(),0xe4);
    Redraw();
    return selected;
}
size_t SelectSurfaces(const std::vector<int>& surfaces,bool focus)
{
    auto model=Read<Address>(Level()+0x13c);
    if(!model)throw std::runtime_error("The map has no built BSP.");
    for(auto actor:LiveActors())SetActorSelected(actor,false);
    Exec("POLY SELECT NONE");
    auto all=Array(model+0x94,0x2c);
    std::set<int> chosen;
    for(int index:surfaces)
        if(index>=0 && size_t(index)<all.size())
        {
            Write(all[size_t(index)]+0x14,Read<unsigned>(all[size_t(index)]+0x14)|0x02000000u);
            chosen.insert(index);
        }
    if(focus && !chosen.empty())
    {
        FrameBounds bounds;
        AddSurfaceNodes(chosen,bounds);
        FocusViewports(bounds);
    }
    Call(Engine(),0xe4);
    Redraw();
    return chosen.size();
}
