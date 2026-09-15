#include "../Reloaded.Editor/BrushGridSnapModel.h"
#include <cassert>
#include <limits>
#include <iostream>
int main()
{
    using V=std::array<double,3>;
    using BrushGridSnap::Translation;
    assert((Translation(V{3,-13,19},V{12,-4,27},V{16,16,16},1)==V{-3,0,0}));
    assert((Translation(V{7,-13,19},V{15,-4,27},V{16,16,16},7)==V{1,-3,-3}));
    assert((Translation(V{0,16,-32},V{7,23,-21},V{16,16,16},7)==V{0,0,0}));
    assert((Translation(V{-8,-8,-8},V{8,8,8},V{16,16,16},7)==V{-8,-8,-8}));
    auto fractional=Translation(V{.3,9,29},V{.8,15,31},V{.25,8,32},7);
    assert(std::abs(fractional[0]+.05)<1e-12 && fractional[1]==-1 && fractional[2]==1);
    for(unsigned mask: {0u,8u}){bool rejected=false;try{Translation(V{},V{},V{1,1,1},mask);}catch(const std::exception&){rejected=true;}assert(rejected);}
    for(double grid: {0.,-1.,std::numeric_limits<double>::infinity()}){bool rejected=false;try{Translation(V{},V{},V{grid,1,1},1);}catch(const std::exception&){rejected=true;}assert(rejected);}
    std::cout<<"Brush grid snap tests passed\n";
}
