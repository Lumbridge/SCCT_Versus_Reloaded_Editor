#include "../Reloaded.Editor/BspLeafLightTable.h"
#include <cassert>
#include <iostream>
#include <numeric>
#include <random>

using namespace BspLeafLightTable;

void Equivalent(const std::vector<uint16_t>& starts, const std::vector<uint32_t>& lights, const Packed& packed)
{
    assert(starts.size()==packed.starts.size());
    assert(packed.lights.size()<=lights.size());
    for (size_t leaf=0; leaf<starts.size(); ++leaf)
    {
        if (starts[leaf]==None) { assert(packed.starts[leaf]==None); continue; }
        assert(packed.starts[leaf]<MaxSlots);
        size_t old=starts[leaf], now=packed.starts[leaf];
        do
        {
            assert(old<lights.size() && now<packed.lights.size());
            assert(lights[old]==packed.lights[now]);
            ++now;
        } while (lights[old++]);
    }
}

int main()
{
    Packed packed;
    std::string error;
    std::vector<uint16_t> starts{0,3,4,6,8,None};
    std::vector<uint32_t> lights{9,8,0,0,8,0,9,0,9,8,0};
    assert(Pack(starts,lights,packed,error));
    Equivalent(starts,lights,packed);
    assert(packed.lights.size()==5 && packed.starts[0]==packed.starts[4]);
    assert(packed.starts[2]==packed.starts[0]+1);
    assert(packed.starts[1]!=None); // Preserve an explicit empty list.
    Packed twice;
    assert(Pack(packed.starts,packed.lights,twice,error));
    assert(packed.starts==twice.starts && packed.lights==twice.lights);

    // Native overflow: starts are unsigned while packing, then made safe for
    // the stock signed reader. No light, duplicate or order may be dropped.
    lights.clear(); starts.clear();
    for (int i=0;i<5000;++i)
    {
        starts.push_back(static_cast<uint16_t>(lights.size()));
        lights.insert(lights.end(),{7,8,7,9,10,11,0});
    }
    assert(starts.back()>32767 && Pack(starts,lights,packed,error));
    assert(packed.lights.size()==7);
    Equivalent(starts,lights,packed);

    std::mt19937 rng(9137);
    for (int trial=0;trial<200;++trial)
    {
        lights.clear(); starts.clear();
        for (int list=0;list<100;++list)
        {
            const auto begin=static_cast<uint16_t>(lights.size());
            const unsigned length=rng()%30;
            for(unsigned i=0;i<length;++i) lights.push_back(1+rng()%20);
            lights.push_back(0);
            starts.push_back(begin);
            starts.push_back(static_cast<uint16_t>(begin+length/2));
            starts.push_back(begin);
            starts.push_back(None);
        }
        assert(Pack(starts,lights,packed,error));
        Equivalent(starts,lights,packed);
    }

    lights.resize(MaxSlots);
    std::iota(lights.begin(),lights.end(),1u);
    lights.back()=0; starts={0};
    assert(Pack(starts,lights,packed,error));
    Equivalent(starts,lights,packed);
    const auto previous=packed;
    lights.insert(lights.end(),{999999,0}); starts.push_back(static_cast<uint16_t>(MaxSlots));
    assert(!Pack(starts,lights,packed,error));
    assert(packed.starts==previous.starts && packed.lights==previous.lights);
    assert(!Pack(std::vector<uint16_t>{1},std::vector<uint32_t>{0},packed,error));
    assert(!Pack(std::vector<uint16_t>{0},std::vector<uint32_t>{123},packed,error));
    assert(!Pack(std::vector<uint16_t>{0},std::vector<uint32_t>(65536),packed,error));
    assert(Pack({}, {}, packed,error));
    assert(packed.starts.empty() && packed.lights.empty());
    std::cout << "BspLeafLightTable tests passed\n";
}
