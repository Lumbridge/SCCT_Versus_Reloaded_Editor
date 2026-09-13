#include "pch.h"
#include "BspCollisionFix.h"
#include "MemoryWriter.h"
#include "logger.h"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <string>
#include <vector>

namespace
{
    template<class T> T Read(const void* p, size_t offset)
    { T value; memcpy(&value, static_cast<const char*>(p)+offset, sizeof(value)); return value; }
    template<class T> struct Array { T* data; int count, capacity; };
    template<class T> bool Valid(const Array<T>& a, int limit=1000000)
    { return a.count>=0 && a.count<=limit && a.capacity>=a.count && (!a.count || a.data); }

    using Exec = int(__thiscall*)(void*, const char*, void*);
    using Build = void(__thiscall*)(void*, void*, int, int, int, int, int);
    Exec previousExec;
    Build previousBuild;
    bool retrying=false, recoveryOrdering=false;
    void* retryModel=nullptr;

    void* Level()
    {
        auto editor=*reinterpret_cast<void**>(0x1165DFA0);
        return editor ? Read<void*>(editor,0x130) : nullptr;
    }
    void* Model() { auto level=Level(); return level ? Read<void*>(level,0x13C) : nullptr; }

    bool Fits(void* model)
    {
        if (!model) return true;
        auto nodes=Read<Array<unsigned char>>(model,0x54);
        auto hulls=Read<Array<int>>(model,0xB0);
        if (!Valid(nodes) || !Valid(hulls) || hulls.count>65535) return false;
        // Both the PC game and editor sign-extend this 16-bit start index.
        // Keeping the array below 65536 alone does not prevent bad reads.
        for (int i=0;i<nodes.count;++i)
        {
            auto index=Read<uint16_t>(nodes.data+size_t(i)*0x5C,0x54);
            if (index!=0xFFFF && (index>=0x8000 || index>=hulls.count)) return false;
        }
        return true;
    }

    const char* Name(void* object)
    {
        if (!object) return "";
        auto names=*reinterpret_cast<unsigned char***>(0x1169CFBC);
        int count=*reinterpret_cast<int*>(0x1169CFC0),index=Read<int>(object,0x20);
        return names && index>=0 && index<count && names[index]
            ? reinterpret_cast<const char*>(names[index]+12) : "";
    }

    struct SourceOrder
    {
        void* level=nullptr;
        std::vector<int> slots;
        std::vector<void*> original;
        bool committed=false;
        ~SourceOrder()
        {
            if (committed || !level || level!=Level()) return;
            auto actors=Read<Array<void*>>(level,0x2C);
            if (!Valid(actors)) return;
            for (size_t i=0;i<slots.size();++i)
                if (slots[i]<actors.count) actors.data[slots[i]]=original[i];
        }
        bool LongestFirst()
        {
            level=Level();
            if (!level) return false;
            auto actors=Read<Array<void*>>(level,0x2C);
            if (!Valid(actors)) return false;
            std::vector<std::pair<double,void*>> order;
            slots.reserve(actors.count);
            original.reserve(actors.count);
            order.reserve(actors.count);
            int operation=0;
            bool sheetsStarted=false;
            for (int i=2;i<actors.count;++i)
            {
                auto actor=actors.data[i];
                if (!actor || strcmp(Name(Read<void*>(actor,0x24)),"Brush")) continue;
                int op=Read<unsigned char>(actor,0x34C);
                if (!op) continue;
                unsigned flags=Read<unsigned>(actor,0x344);
                if (flags&8) { sheetsStarted=true; continue; }
                // Recovery's structural cells have disjoint interiors. Never
                // apply this reorder to authored or mixed-operation brushes.
                if (sheetsStarted || flags || (op!=1 && op!=2)
                    || (operation && operation!=op) || strncmp(Name(actor),"RecoveredVolume",15)) return false;
                operation=op;
                auto brush=Read<void*>(actor,0x238);
                auto polys=brush ? Read<void*>(brush,0x50) : nullptr;
                if (!polys) return false;
                auto faces=Read<Array<unsigned char>>(polys,0x28);
                if (!Valid(faces) || !faces.count) return false;
                double lo[3]={DBL_MAX,DBL_MAX,DBL_MAX},hi[3]={-DBL_MAX,-DBL_MAX,-DBL_MAX};
                for(int f=0;f<faces.count;++f)
                {
                    auto face=faces.data+size_t(f)*0x14C;
                    auto count=Read<uint16_t>(face,0x148);
                    if(count<3 || count>19) return false;
                    for(unsigned v=0;v<count;++v) for(unsigned axis=0;axis<3;++axis)
                    {
                        double value=Read<float>(face,0x18+v*12+axis*4);
                        if(!std::isfinite(value)) return false;
                        lo[axis]=(std::min)(lo[axis],value);hi[axis]=(std::max)(hi[axis],value);
                    }
                }
                double extent=(std::max)({hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2]});
                slots.push_back(i);original.push_back(actor);order.emplace_back(extent,actor);
            }
            if(order.size()<2) return false;
            std::stable_sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.first>b.first;});
            for(size_t i=0;i<order.size();++i) actors.data[slots[i]]=order[i].second;
            return true;
        }
    };

    void __fastcall BuildWithFewerSplits(void* editor,void*,void* model,int quality,
                                       int balance,int portal,int simple,int root)
    {
        if(retrying && model==retryModel) { quality=2;balance=0; }
        previousBuild(editor,model,quality,balance,portal,simple,root);
    }

    bool RebuildCommand(const char* command)
    {
        if(!command) return false;
        while(*command==' ' || *command=='\t') ++command;
        for(const char* prefix:{"MAP REBUILD","BSP REBUILD"})
        {
            size_t size=strlen(prefix);
            if(!_strnicmp(command,prefix,size) && (!command[size] || command[size]==' ' || command[size]=='\t')) return true;
        }
        return false;
    }

    int __fastcall CheckBuild(void* self,void*,const char* command,void* output)
    {
        if(retrying || !RebuildCommand(command)) return previousExec(self,command,output);
        int result;
        auto model=Model();
        // A saved map close to the limit can overflow during the native build,
        // before Exec returns. Start with fewer splits for these rebuilds.
        if(model && Read<int>(model,0xB4)>=0x7800)
        {
            retryModel=model;retrying=true;
            struct Reset { ~Reset(){retrying=false;retryModel=nullptr;} } reset;
            result=previousExec(self,command,output);
        }
        else result=previousExec(self,command,output);
        if(!result || Fits(Model())) return result;
        try
        {
            SourceOrder order;
            if(recoveryOrdering) order.LongestFirst();
            retryModel=Model();retrying=true;
            struct Reset { ~Reset(){retrying=false;retryModel=nullptr;} } reset;
            auto before=Read<int>(retryModel,0xB4);
            Logger::log("BspCollisionFix: collision table has "+std::to_string(before)
                +" slots; rebuilding geometry/BSP with fewer splits.");
            result=previousExec(self,"MAP REBUILD",output) && previousExec(self,"BSP REBUILD",output);
            if(result && Model()==retryModel && Fits(retryModel))
            {
                order.committed=true;
                Logger::log("BspCollisionFix: collision table fits: "+std::to_string(before)
                    +" -> "+std::to_string(Read<int>(retryModel,0xB4))+" slots.");
                return result;
            }
            Logger::log("BspCollisionFix: collision table still exceeds the PC index limit; build rejected.");
        }
        catch(const std::exception& e) { Logger::log(std::string("BspCollisionFix: ")+e.what()); }
        return 0;
    }
}

BspCollisionFix::RecoveryScope::RecoveryScope():previous(recoveryOrdering) { recoveryOrdering=true; }
BspCollisionFix::RecoveryScope::~RecoveryScope() { recoveryOrdering=previous; }

void BspCollisionFix::Initialize()
{
    // The supported editor's FExec and bspBuild vtable entries. Other wrappers
    // installed later retain these callbacks through their normal chaining.
    constexpr uintptr_t execSlot=0x1147B9B4,buildSlot=0x1147BB7C;
    if(*reinterpret_cast<uintptr_t*>(execSlot)!=0x10E040EE
        || *reinterpret_cast<uintptr_t*>(buildSlot)!=0x11082530)
    { Logger::log("BspCollisionFix: editor vtable mismatch; hooks not installed.");return; }
    previousExec=*reinterpret_cast<Exec*>(execSlot);
    previousBuild=*reinterpret_cast<Build*>(buildSlot);
    auto build=&BuildWithFewerSplits;auto exec=&CheckBuild;
    if(!MemoryWriter::WriteBytes(buildSlot,&build,sizeof(build))) return;
    if(!MemoryWriter::WriteBytes(execSlot,&exec,sizeof(exec)))
        MemoryWriter::WriteBytes(buildSlot,&previousBuild,sizeof(previousBuild));
}
