// cl /nologo /std:c++17 /W4 /WX /EHsc tests\RecoveredPolygonImportTests.cpp Reloaded.Editor\RecoveredPolygonImport.cpp
#include "../Reloaded.Editor/RecoveredPolygonImport.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace RecoveredPolygonImport;

namespace
{
    bool Equal(const Vec3& a,const Vec3& b)
    {
        return a.x==b.x && a.y==b.y && a.z==b.z;
    }

    void TestNativePointComparison()
    {
        const double threshold = static_cast<double>(0.002f);
        const double below = std::nextafter(0.002f,0.0f);
        Result exact = Prepare({{0,0,0},{threshold,0,0},{1,1,0},{0,1,0}});
        assert(exact.accepted() && exact.vertices.size()==4);
        Result close = Prepare({{0,0,0},{below,below,0},{1,1,0},{0,1,0}});
        assert(close.accepted() && close.vertices.size()==3);
        assert(close.collapsedVertexCount==1);
        // sqrt(2)*below exceeds the threshold: comparison is componentwise.
        assert(std::sqrt(2.0)*below>threshold);

        Result wrapping = Prepare({{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0.001,0}});
        assert(wrapping.accepted() && wrapping.collapsedVertexCount==1);
        assert(Equal(wrapping.vertices.front(),{1,0,0}));
        // Removing a near point keeps comparison anchored at the last
        // accepted point rather than advancing through a chain of near points.
        Result chain = Prepare({{0,0,0},{0.0015,0,0},{0.003,0,0},{1,1,0},{0,1,0}});
        assert(chain.accepted() && chain.vertices.size()==4);
        assert(chain.vertices[1].x==static_cast<double>(static_cast<float>(0.003)));
    }

    void TestSharedVertices()
    {
        VertexPool pool;
        std::string error;
        Vec3 resolved;
        assert(pool.Resolve({-0.0015,0,0},resolved,error));
        const Vec3 first=resolved;
        // Search crosses a cell boundary and resolves independently supplied
        // brush corners to exactly the same float coordinate.
        assert(pool.Resolve({0,0.001,0.001},resolved,error) && Equal(resolved,first));
        assert(pool.Resolve({0.0015,0,0},resolved,error));
        const Vec3 second=resolved;
        assert(!Equal(first,second));
        // Both anchors match: retain the first, without chaining their ranges.
        assert(pool.Resolve({0,0,0},resolved,error) && Equal(resolved,first));
        assert(pool.Resolve({0.003,0,0},resolved,error) && Equal(resolved,second));
        VertexPool threshold;
        assert(threshold.Resolve({0,0,0},resolved,error));
        assert(threshold.Resolve({kCoincidentVertexTolerance,0,0},resolved,error));
        assert(resolved.x==static_cast<double>(kCoincidentVertexTolerance));
        VertexPool translated;
        assert(translated.Resolve({4096,4096,4096},resolved,error));
        assert(translated.Resolve({4096.001,4096,4096},resolved,error));
        assert(Equal(resolved,{4096,4096,4096}));
        assert(!translated.Resolve({std::numeric_limits<double>::infinity(),0,0},resolved,error));
        assert(!translated.Resolve({1e30,0,0},resolved,error));
        assert(!translated.Resolve({(std::numeric_limits<double>::max)(),0,0},resolved,error));
        VertexPool limited(1);
        assert(limited.Resolve({0,0,0},resolved,error));
        assert(limited.Resolve({0.001,0,0},resolved,error));
        assert(!limited.Resolve({1,0,0},resolved,error));
        assert(limited.Resolve({0,0,0},resolved,error));
    }

    void TestNormalAndWinding()
    {
        std::vector<Vec3> square{{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
        const Result forward = Prepare(square);
        assert(forward.accepted() && forward.normalSquared==4);
        assert(Equal(forward.normal,{0,0,1}));
        std::reverse(square.begin(),square.end());
        const Result backward = Prepare(square);
        assert(backward.accepted() && Equal(backward.normal,{0,0,-1}));
        // A representable large translation does not change anchored fan area.
        for (Vec3& p:square) { p.x+=65536; p.y-=32768; p.z+=1024; }
        const Result shifted = Prepare(square);
        assert(shifted.accepted() && shifted.normalSquared==backward.normalSquared);
        assert(Equal(shifted.normal,backward.normal));

        const Result collinear = Prepare({{0,0,0},{1,0,0},{2,0,0}});
        assert(collinear.reason==Reason::NormalTooSmall && collinear.vertices.size()==3);
        const Result boundary = Prepare({{0,0,0},{1,0,0},{0,0.01f,0}});
        // The native squared comparison is made after the cross product's
        // float store: float(0.01)^2 is just below float(0.0001).
        assert(boundary.reason==Reason::NormalTooSmall);
        const Result above = Prepare({{0,0,0},{1,0,0},{0,std::nextafter(0.01f,1.0f),0}});
        assert(above.accepted());
        assert(Prepare({{0,0,0},{1,0,0},{0,0.009,0}}).reason==Reason::NormalTooSmall);

        const Result collinearBoundary = Prepare({{0,0,0},{1,0,0},{2,0,0},{2,1,0},{0,1,0}});
        assert(collinearBoundary.accepted() && collinearBoundary.vertices.size()==5);
        // Native cleanup does not establish planarity or convexity.
        assert(Prepare({{0,0,0},{1,0,0},{1,1,0.1},{0,1,0}}).accepted());
    }

    void TestObservedNativeOutlines()
    {
        // Captured native imports: the first loses its short edge and drops;
        // the second keeps three points but its resulting fan area is too small.
        const Result collapsed = Prepare({{-2922.96143,12416,-449},
            {-2922.96143,12416,-448},{-2922.96094,12416,-448}});
        assert(collapsed.reason==Reason::CollapsedVertices);
        assert(collapsed.vertices.size()==2 && collapsed.collapsedVertexCount==1);
        const Result tiny = Prepare({{1507.07739,-1510,-448},{1507.07727,-1510,-4288},
            {1507.07764,-1510,-468},{1507.07764,-1510,-448}});
        assert(tiny.reason==Reason::NormalTooSmall && tiny.vertices.size()==3);
        assert(tiny.normalSquared==5.36441802978515625e-5);
        const Result changed = Prepare({{1943.8833,9415.92188,-4288},
            {930.392151,10429.4131,-4288},{-1056.19458,12416,-4288},
            {-1056.19446,12416,-4288}});
        assert(changed.accepted() && changed.vertices.size()==3);
        assert(changed.collapsedVertexCount==1);
    }

    void TestInputLimits()
    {
        assert(Prepare({}).reason==Reason::TooFewVertices);
        assert(Prepare({{0,0,0},{1,0,0}}).reason==Reason::TooFewVertices);
        std::vector<Vec3> polygon;
        for (int i=0; i<17; ++i)
        {
            const double angle=i*6.283185307179586/17;
            polygon.push_back({std::cos(angle),std::sin(angle),0});
        }
        assert(Prepare(polygon).reason==Reason::TooManyVertices);
        polygon.pop_back();
        assert(Prepare(polygon).accepted());
        assert(Prepare({{0,0,0},{1,0,0},{0,std::numeric_limits<double>::infinity(),0}})
            .reason==Reason::NonFiniteCoordinate);
        assert(Prepare({{0,0,0},{1,0,0},{0,std::numeric_limits<double>::quiet_NaN(),0}})
            .reason==Reason::NonFiniteCoordinate);
        assert(Prepare({{0,0,0},{1,0,0},{0,1e100,0}}).reason==Reason::NonFiniteCoordinate);
        assert(Prepare({{0,0,0},{1e30,0,0},{0,1e30,0}}).reason==Reason::NonFiniteArithmetic);
    }

    void TestOutlinePreservation()
    {
        const std::vector<Vec3> square{{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
        assert(PreservesOutline(square,Prepare(square)));
        const std::vector<Vec3> straight{{0,0,0},{0.001f,0,0},{1,0,0},{1,1,0},{0,1,0}};
        assert(Prepare(straight).collapsedVertexCount==1);
        assert(PreservesOutline(straight,Prepare(straight)));
        const std::vector<Vec3> diagonal{{0,0,0},{0.001f,0.001f,0.001f},
            {1,1,1},{0,2,2},{-1,1,1}};
        assert(PreservesOutline(diagonal,Prepare(diagonal)));
        const std::vector<Vec3> wrapping{{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0.001f,0}};
        // Fix deletes the first corner here; it was not redundant.
        assert(!PreservesOutline(wrapping,Prepare(wrapping)));
        const std::vector<Vec3> wrappingStraight{{0.001f,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,0}};
        assert(PreservesOutline(wrappingStraight,Prepare(wrappingStraight)));
        const std::vector<Vec3> notch{{0,0,0},{0.001f,-0.001f,0},{1,0,0},{1,1,0},{0,1,0}};
        assert(Prepare(notch).accepted());
        assert(!PreservesOutline(notch,Prepare(notch)));
        const std::vector<Vec3> reverse{{0,0,0},{-0.001f,0,0},{1,0,0},{1,1,0},{0,1,0}};
        assert(!PreservesOutline(reverse,Prepare(reverse)));
        const std::vector<Vec3> rounding{{0.1,0,0},{1,0,0},{1,1,0},{0,1,0}};
        assert(!PreservesOutline(rounding,Prepare(rounding)));
        assert(!PreservesOutline(square,Prepare({{0,0,0},{1,0,0},{2,0,0}})));
        Result changed=Prepare(square);
        changed.vertices[1].z=0.000001;
        assert(!PreservesOutline(square,changed));
        std::reverse(changed.vertices.begin(),changed.vertices.end());
        assert(!PreservesOutline(square,changed));
    }
}

int main()
{
    TestNativePointComparison();
    TestSharedVertices();
    TestNormalAndWinding();
    TestObservedNativeOutlines();
    TestInputLimits();
    TestOutlinePreservation();
    std::puts("RecoveredPolygonImport tests passed");
}
