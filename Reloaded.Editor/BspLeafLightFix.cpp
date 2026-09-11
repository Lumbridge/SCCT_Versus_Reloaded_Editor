#include "pch.h"
#include "BspLeafLightFix.h"
#include "BspLeafLightTable.h"
#include "Hooks.h"
#include "logger.h"

namespace
{
    struct Leaf { uint16_t zone, lightStart; };
    template<class T> struct Array { T* data; int count, capacity; };
    template<class T> bool Valid(const Array<T>& array, int limit)
    {
        return array.count >= 0 && array.count <= limit && array.capacity >= array.count
            && (!array.count || array.data);
    }
    auto& Leaves(void* model) { return *reinterpret_cast<Array<Leaf>*>(static_cast<char*>(model)+0xBC); }
    auto& Lights(void* model) { return *reinterpret_cast<Array<uint32_t>*>(static_cast<char*>(model)+0xC8); }

    void __cdecl CompactGeneratedLists(void* model)
    {
        try
        {
            if (!model) return;
            auto& leaves = Leaves(model);
            auto& lights = Lights(model);
            if (!Valid(leaves, 1000000) || !Valid(lights, BspLeafLightTable::None))
            {
                Logger::log("BspLeafLightFix: invalid generated table; left unchanged.");
                return;
            }
            bool needsPacking = false;
            for (int i=0; i<leaves.count; ++i)
                needsPacking |= leaves.data[i].lightStart != BspLeafLightTable::None
                    && leaves.data[i].lightStart >= BspLeafLightTable::MaxSlots;
            if (!needsPacking) return;
            std::vector<uint16_t> starts;
            starts.reserve(leaves.count);
            for (int i=0; i<leaves.count; ++i) starts.push_back(leaves.data[i].lightStart);
            BspLeafLightTable::Packed packed;
            std::string error;
            if (!BspLeafLightTable::Pack(starts, {lights.data, static_cast<size_t>(lights.count)}, packed, error))
            {
                Logger::log("BspLeafLightFix: " + error + " Original lists retained.");
                return;
            }
            const int previous = lights.count;
            // Reuse the engine-owned allocation; only the live count shrinks.
            if (!packed.lights.empty()) memcpy(lights.data, packed.lights.data(), packed.lights.size()*sizeof(uint32_t));
            lights.count = static_cast<int>(packed.lights.size());
            for (int i=0; i<leaves.count; ++i) leaves.data[i].lightStart = packed.starts[i];
            Logger::log("BspLeafLightFix: shared identical lists; " + std::to_string(previous)
                + " -> " + std::to_string(lights.count) + " slots, " + std::to_string(leaves.count) + " leaves.");
        }
        catch (const std::exception& exception)
        {
            Logger::log(std::string("BspLeafLightFix: ") + exception.what());
        }
    }

    __declspec(naked) void FinishLeafLights()
    {
        static const uintptr_t resume = 0x1101DF7F;
        __asm
        {
            pushfd
            pushad
            mov eax, [ebp-0x14]
            push dword ptr [eax+0x10]
            call CompactGeneratedLists
            add esp, 4
            popad
            popfd
            mov ecx, dword ptr ds:[0x11691D64]
            jmp dword ptr [resume]
        }
    }
}

bool BspLeafLightFix::Validate(void* model, std::string& error)
{
    if (!model || !Valid(Leaves(model), 1000000) || !Valid(Lights(model), BspLeafLightTable::None))
    {
        error = "The rebuilt BSP has an invalid leaf-light table.";
        return false;
    }
    const auto& leaves = Leaves(model);
    const auto& lights = Lights(model);
    // Every suffix can be checked once, rather than rescanning shared lists.
    int lastTerminator = -1;
    for (int i=lights.count-1; i>=0; --i)
        if (!lights.data[i]) { lastTerminator=i; break; }
    for (int i=0; i<leaves.count; ++i)
    {
        const auto index = leaves.data[i].lightStart;
        if (index != BspLeafLightTable::None
            && (index >= BspLeafLightTable::MaxSlots || index > lastTerminator))
        {
            error = "The rebuilt BSP exceeds the stock game's leaf-light index limit or has an invalid list.";
            return false;
        }
    }
    return true;
}

void BspLeafLightFix::Initialize()
{
    // The common visibility builder emits these lists during ordinary geometry
    // builds, lighting and Save. Compact only after all native light filtering
    // has finished and before any consumer can see a signed-overflow index.
    constexpr uintptr_t site = 0x1101DF79;
    const unsigned char expected[] = {0x8B,0x0D,0x64,0x1D,0x69,0x11};
    if (memcmp(reinterpret_cast<void*>(site), expected, sizeof(expected)))
    {
        Logger::log("BspLeafLightFix: visibility-builder byte mismatch; hook not installed.");
        return;
    }
    MemoryWriter::WriteJump(site, FinishLeafLights);
}
