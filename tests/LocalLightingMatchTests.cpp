#include "../Reloaded.Editor/LocalLightingMatch.h"
#include <cassert>
#include <cmath>
int main() {
    using namespace LocalLightingMatch;
    std::vector<BspNode> halfSpace{{{1,0,0},0,-1,-1,true}};
    assert(ClearSegment(halfSpace,true,{1,0,0},{2,0,0}));
    assert(!ClearSegment(halfSpace,true,{1,0,0},{-1,0,0}));
    assert(!ClearSegment(halfSpace,true,{-1,0,0},{-2,0,0}));
    std::vector<BspNode> wall{{{1,0,0},1,-1,1,true},{{-1,0,0},1,-1,-1,true}};
    assert(!ClearSegment(wall,true,{-2,0,0},{2,0,0}));
    assert(ClearSegment(wall,true,{-2,0,0},{-3,0,0}));
    assert(ClearSegment({},true,{0,0,0},{1,0,0}));
    assert(!ClearSegment({},false,{0,0,0},{1,0,0}));
    halfSpace[0].front=99;
    assert(!ClearSegment(halfSpace,true,{1,0,0},{2,0,0}));
    halfSpace[0].front=0;
    assert(!ClearSegment(halfSpace,true,{1,0,0},{2,0,0}));
    double b,c;
    assert(std::abs(Nearest({.25,.25,2},{0,0,0},{1,0,0},{0,1,0},b,c)-4)<1e-9);
    assert(b==.25 && c==.25);
    assert(std::abs(Nearest({1,1,0},{0,0,0},{1,0,0},{0,1,0},b,c)-.5)<1e-9);
    assert(b==.5 && c==.5);
    assert(Nearest({-1,-1,0},{0,0,0},{1,0,0},{0,1,0},b,c)==2 && b==0 && c==0);
    assert(Nearest({2,0,0},{0,0,0},{0,0,0},{0,0,0},b,c)==4);
    // Identity calibration, signed colour correction, saturation and alpha.
    assert(Correct(0x120a2030,0xff204060,0xaa204060)==0x120a2030);
    assert(Correct(0x120a2030,0xff204060,0xaa102030)==0x121a4060);
    assert(Correct(0x12fa1000,0xffff00ff,0xaa00ff00)==0x12ff00ff);
    // A reference's missing illumination offsets local shadow variation; it
    // must not flatten the difference between two native target samples.
    assert((Correct(30,70,20)&255)-(Correct(10,70,20)&255)==20);
}
