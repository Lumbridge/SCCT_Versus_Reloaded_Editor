// cl /nologo /std:c++17 /W4 /WX /EHsc tests\RecoveredSurfacePartitionTests.cpp Reloaded.Editor\RecoveredSurfacePartition.cpp
#include "../Reloaded.Editor/RecoveredSurfacePartition.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

using namespace RecoveredSurfacePartition;
using Face = RecoveredBspGeometry::Face;

namespace
{
    Vec3 Sub(const Vec3& a,const Vec3& b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
    double Dot(const Vec3& a,const Vec3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
    Vec3 Cross(const Vec3& a,const Vec3& b)
    {
        return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
    }
    std::vector<Vec3> Rectangle(double x,double y,double X,double Y)
    {
        return {{x,y,0},{X,y,0},{X,Y,0},{x,Y,0}};
    }
    double Area(const std::vector<Vec3>& vertices,const Vec3& normal)
    {
        double area = 0;
        for (std::size_t index=1; index+1<vertices.size(); ++index)
            area += Dot(Cross(Sub(vertices[index],vertices[0]),
                              Sub(vertices[index+1],vertices[0])),normal)*0.5;
        return area;
    }
    bool Contains(const std::vector<Vec3>& vertices,const Vec3& normal,const Vec3& point)
    {
        for (std::size_t index=0; index<vertices.size(); ++index)
            if (Dot(Cross(Sub(vertices[(index+1)%vertices.size()],vertices[index]),
                          Sub(point,vertices[index])),normal)<-1e-11)
                return false;
        return true;
    }
    std::vector<Piece> PartitionFace(const Face& face,const std::vector<Surface>& surfaces)
    {
        std::vector<Piece> pieces;
        std::string error;
        if (!Partition(face,surfaces,99,pieces,error))
        {
            std::fprintf(stderr,"Partition unexpectedly failed: %s\n",error.c_str());
            assert(false);
        }
        double total=0;
        for (const Piece& piece:pieces)
        {
            const double area=Area(piece.vertices,face.normal);
            // A positive clipping fragment can collapse while restoring its
            // double 3D coordinates. Editor preparation assesses these at
            // the whole-face level against the actual float rounding bound.
            double perimeter=0,maximumCoordinate=1;
            for (std::size_t index=0;index<piece.vertices.size();++index)
            {
                const Vec3& vertex=piece.vertices[index];
                const Vec3 edge=Sub(piece.vertices[(index+1)%piece.vertices.size()],vertex);
                perimeter+=std::hypot(edge.x,edge.y,edge.z);
                maximumCoordinate=(std::max)(maximumCoordinate,
                    (std::max)({std::fabs(vertex.x),std::fabs(vertex.y),std::fabs(vertex.z)}));
            }
            assert(area>=-maximumCoordinate*perimeter*std::numeric_limits<double>::epsilon()*16);
            total+=area;
            for (const Vec3& vertex:piece.vertices)
                assert(std::fabs(Dot(face.normal,Sub(vertex,face.vertices[0])))<1e-7);
        }
        assert(std::fabs(total-Area(face.vertices,face.normal))
               <(std::max)(1e-8,Area(face.vertices,face.normal)*1e-10));
        return pieces;
    }
    void CheckCoverage(const std::vector<Piece>& pieces,const Vec3& normal)
    {
        for (int x=0; x<30; ++x)
            for (int y=0; y<30; ++y)
            {
                const Vec3 point{(x+0.137)/3,(y+0.391)/3,0};
                int matches=0;
                for (const Piece& piece:pieces)
                    if (Contains(piece.vertices,normal,point)) ++matches;
                assert(matches==1);
            }
    }
    std::map<int,double> MaterialAreas(const std::vector<Piece>& pieces,const Vec3& normal)
    {
        std::map<int,double> areas;
        for (const Piece& piece:pieces) areas[piece.materialIndex]+=Area(piece.vertices,normal);
        return areas;
    }
    void MustFail(const Face& face,const std::vector<Surface>& surfaces,const char* expected,
                  const Limits& limits={})
    {
        std::vector<Piece> pieces{{{},123}};
        std::string error;
        assert(!Partition(face,surfaces,99,pieces,error,limits));
        assert(pieces.empty());
        if (error.find(expected)==std::string::npos)
        {
            std::fprintf(stderr,"Expected '%s', got '%s'\n",expected,error.c_str());
            assert(false);
        }
    }
    void CheckEditorPreparation(const Face& face,const std::vector<Piece>& pieces)
    {
        std::vector<Piece> prepared;
        std::string error;
        if (!PrepareForEditor(face,pieces,16,prepared,error))
        {
            std::fprintf(stderr,"Editor preparation unexpectedly failed: %s\n",error.c_str());
            assert(false);
        }
        assert(!prepared.empty());
        for (const Piece& piece:prepared)
        {
            assert(piece.vertices.size()>=3 && piece.vertices.size()<=16);
            assert(Area(piece.vertices,face.normal)>0);
            for (const Vec3& point:piece.vertices)
                assert(point.x==static_cast<float>(point.x) && point.y==static_cast<float>(point.y)
                    && point.z==static_cast<float>(point.z));
        }
    }
    std::vector<Surface> CheckSheetPreparation(const Surface& sheet)
    {
        std::vector<Surface> triangles;
        std::string error;
        if (!PrepareSheetForEditor(sheet,triangles,error))
        {
            std::fprintf(stderr,"Sheet preparation unexpectedly failed: %s\n",error.c_str());
            assert(false);
        }
        assert(!triangles.empty());
        for (const Surface& triangle:triangles)
        {
            assert(triangle.vertices.size()>=3 && triangle.vertices.size()<=16
                && triangle.materialIndex==sheet.materialIndex);
            assert(Area(triangle.vertices,triangle.normal)>0);
            for (const Vec3& point:triangle.vertices)
                assert(std::any_of(sheet.vertices.begin(),sheet.vertices.end(),[&](const Vec3& source)
                {
                    return point.x==static_cast<float>(source.x) && point.y==static_cast<float>(source.y)
                        && point.z==static_cast<float>(source.z);
                }));
        }
        return triangles;
    }
}

#include "RecoveredSurfacePartitionOffsDFixture.h"
#include "RecoveredSurfacePartitionLegacyD1Fixture.h"
#include "RecoveredSurfacePartitionFloatFixture.h"
#include "RecoveredSurfacePartitionBrushFixture.h"
#include "RecoveredSurfacePartitionEncodingFixture.h"
#include "RecoveredSurfacePartitionSheetFixture.h"

int main()
{
    VerifyNativeOffsDSurfaceCase();
    VerifyNativeOffsDSurfaceJoinCase();
    VerifyNativeLegacyD1SurfaceCase();
    VerifyOffsDFloatImportCase();
    VerifyLegacyD1FloatImportCase();
    VerifyNativeCollapsedBrushes();
    VerifyNativeFloatBoundaryCases();
    VerifyNativeSheetCases();
    const Vec3 up{0,0,1};
    Face face{Rectangle(0,0,10,10),up,0};
    std::vector<Surface> surfaces{
        {Rectangle(0,0,5,10),up,1},{Rectangle(5,0,10,10),up,2}};
    auto pieces=PartitionFace(face,surfaces);
    auto areas=MaterialAreas(pieces,up);
    assert(pieces.size()==2 && std::fabs(areas[1]-50)<1e-10 && std::fabs(areas[2]-50)<1e-10);
    CheckCoverage(pieces,up);
    CheckEditorPreparation(face,pieces);

    // Same visible materials on an oppositely wound subtractive brush face.
    Face reversed=face;
    reversed.normal={0,0,-1};
    std::reverse(reversed.vertices.begin(),reversed.vertices.end());
    pieces=PartitionFace(reversed,surfaces);
    areas=MaterialAreas(pieces,reversed.normal);
    assert(std::fabs(areas[1]-50)<1e-10 && std::fabs(areas[2]-50)<1e-10);
    CheckCoverage(pieces,reversed.normal);

    // The complementary split must not assign a tolerance strip twice.
    const double seam=5.00005;
    Face thin{Rectangle(5,0,5.0001,10),up,0};
    pieces=PartitionFace(thin,{{Rectangle(0,0,seam,10),up,1},
                              {Rectangle(seam,0,10,10),up,2}});
    areas=MaterialAreas(pieces,up);
    assert(std::fabs(areas[1]-0.0005)<1e-10 && std::fabs(areas[2]-0.0005)<1e-10);

    // Overlapping candidates consume only remaining space, in source order.
    pieces=PartitionFace(face,{{Rectangle(0,0,7,10),up,1},{Rectangle(3,0,10,10),up,2}});
    areas=MaterialAreas(pieces,up);
    assert(std::fabs(areas[1]-70)<1e-10 && std::fabs(areas[2]-30)<1e-10);
    CheckCoverage(pieces,up);

    // An inset material leaves a correctly partitioned fallback surround.
    pieces=PartitionFace(face,{{Rectangle(2,2,8,8),up,1}});
    areas=MaterialAreas(pieces,up);
    assert(std::fabs(areas[1]-36)<1e-10 && std::fabs(areas[99]-64)<1e-10);
    CheckCoverage(pieces,up);

    // A completely disjoint polygon must not fragment the original face.
    pieces=PartitionFace(face,{{Rectangle(12,2,18,8),up,1}});
    assert(pieces.size()==1 && pieces[0].materialIndex==99);

    // Touching boundaries have no positive-area overlap and no slivers.
    pieces=PartitionFace(face,{{Rectangle(10,0,20,10),up,1}});
    assert(pieces.size()==1 && pieces[0].materialIndex==99);

    // Different plane/normal candidates cannot paint this wall.
    Surface raised{Rectangle(0,0,10,10),up,1};
    for (Vec3& vertex:raised.vertices) vertex.z=1;
    pieces=PartitionFace(face,{raised});
    assert(pieces.size()==1 && pieces[0].materialIndex==99);
    pieces=PartitionFace(face,{{Rectangle(0,0,10,10),{1,0,0},1}});
    assert(pieces.size()==1 && pieces[0].materialIndex==99);

    // Rotate the face out of XY and translate far from zero. Material areas
    // and the reversed subtraction orientation must remain unchanged.
    auto transform=[](const Vec3& p) { return Vec3{100000+p.x,200000,p.y-300000}; };
    Face rotated{face.vertices,{0,-1,0},0};
    for (Vec3& vertex:rotated.vertices) vertex=transform(vertex);
    std::vector<Surface> rotatedSurfaces=surfaces;
    for (Surface& surface:rotatedSurfaces)
    {
        surface.normal={0,-1,0};
        for (Vec3& vertex:surface.vertices) vertex=transform(vertex);
    }
    pieces=PartitionFace(rotated,rotatedSurfaces);
    areas=MaterialAreas(pieces,rotated.normal);
    assert(std::fabs(areas[1]-50)<1e-8 && std::fabs(areas[2]-50)<1e-8);

    // A long convex perimeter with collinear edge points must fit FPoly's
    // 16 vertices without zero-area first-vertex fan triangles.
    std::vector<Vec3> longPolygon;
    for (int i=0; i<10; ++i) longPolygon.push_back({double(i),0,0});
    for (int i=0; i<10; ++i) longPolygon.push_back({10,double(i),0});
    for (int i=0; i<10; ++i) longPolygon.push_back({10-double(i),10,0});
    for (int i=0; i<10; ++i) longPolygon.push_back({0,10-double(i),0});
    std::vector<std::vector<Vec3>> split;
    std::string error;
    assert(SplitForVertexLimit(longPolygon,up,16,split,error));
    assert(split.size()==longPolygon.size());
    double splitArea=0;
    for (const auto& polygon:split)
    {
        assert(polygon.size()>=3 && polygon.size()<=16);
        assert(Area(polygon,up)>0);
        splitArea+=Area(polygon,up);
    }
    assert(std::fabs(splitArea-100)<1e-9);
    assert(SplitForVertexLimit(face.vertices,up,16,split,error));
    assert(split.size()==1 && split[0].size()==4);
    assert(!SplitForVertexLimit(face.vertices,up,2,split,error));
    assert(split.empty());

    // Whole-face encoding cannot hide genuinely missing coverage or a
    // collapsed structural face. An exactly zero float material strip is
    // harmless only when the rest still covers the encoded original face.
    std::vector<Piece> prepared;
    assert(!PrepareForEditor(face,{{Rectangle(0,0,5,10),1}},16,prepared,error));
    assert(prepared.empty() && error.find("coverage")!=std::string::npos);
    Face floatCollapsed{Rectangle(100000,0,100000.00001,10),up,0};
    assert(!PrepareForEditor(floatCollapsed,{{floatCollapsed.vertices,1}},16,prepared,error));
    assert(prepared.empty() && error.find("structural")!=std::string::npos);
    Face floatFace{Rectangle(100000,0,100010,10),up,0};
    auto floatPieces=PartitionFace(floatFace,{{Rectangle(100000,0,100000.00001,10),up,1}});
    assert(PrepareForEditor(floatFace,floatPieces,16,prepared,error));
    assert(prepared.size()==1 && prepared[0].materialIndex==99);

    RecoveredBspGeometry::Brush cube;
    cube.faces={
        {{{0,0,0},{0,1,0},{1,1,0},{1,0,0}},{0,0,-1},0},
        {{{0,0,1},{1,0,1},{1,1,1},{0,1,1}},{0,0,1},1},
        {{{0,0,0},{1,0,0},{1,0,1},{0,0,1}},{0,-1,0},2},
        {{{1,0,0},{1,1,0},{1,1,1},{1,0,1}},{1,0,0},3},
        {{{1,1,0},{0,1,0},{0,1,1},{1,1,1}},{0,1,0},4},
        {{{0,1,0},{0,0,0},{0,0,1},{0,1,1}},{-1,0,0},5}};
    std::vector<bool> collapsed;
    assert(ValidateEditorBrush(cube,collapsed,error));
    assert(std::count(collapsed.begin(),collapsed.end(),true)==0);
    // One face subdivides a shared edge: matching uses the same two directed
    // subedges even though the neighbouring face has a single long edge.
    cube.faces[0].vertices.insert(cube.faces[0].vertices.begin()+1,{0,0.5,0});
    assert(ValidateEditorBrush(cube,collapsed,error));
    auto invalidCube=cube;
    invalidCube.faces.push_back(invalidCube.faces.front());
    assert(!ValidateEditorBrush(invalidCube,collapsed,error));
    assert(collapsed.empty() && error.find("edge")!=std::string::npos);
    invalidCube=cube;
    invalidCube.faces.pop_back();
    assert(!ValidateEditorBrush(invalidCube,collapsed,error));
    invalidCube=cube;
    for (auto& f:invalidCube.faces) for (auto& p:f.vertices) p.z=100000+p.z*0.00001;
    assert(!ValidateEditorBrush(invalidCube,collapsed,error));
    assert(collapsed.empty());
    Limits noPolygons;
    noPolygons.maxPieces=0;
    assert(!SplitForVertexLimit(face.vertices,up,16,split,error,noPolygons));
    assert(split.empty());

    Limits limits;
    limits.maxPieces=1;
    MustFail(face,surfaces,"budget",limits);
    limits={}; limits.maxWork=1;
    MustFail(face,surfaces,"work limit",limits);
    Face concave{{{0,0,0},{10,0,0},{5,5,0},{10,10,0},{0,10,0}},up,0};
    MustFail(concave,{},"not convex");
    // Concave material masks preserve their notch; structural faces remain
    // convex. Crossing edges cannot become the hull of their endpoints.
    pieces=PartitionFace(face,{{concave.vertices,up,1}});
    areas=MaterialAreas(pieces,up);
    assert(std::fabs(areas[1]-75)<1e-10 && std::fabs(areas[99]-25)<1e-10);
    CheckCoverage(pieces,up);
    const std::vector<Vec3> crossing{{0,0,0},{10,0,0},{0,10,0},{10,10,0},{5,15,0}};
    MustFail(face,{{crossing,up,1}},"self-intersecting");
    // A sheet may be concave or non-planar: preserve its original outline and
    // original 3D vertices, with a plane normal for each emitted triangle.
    const Surface convexSheet{{{0,0,0},{5,0,0},{10,0,0},{10,10,0},{0,10,0}},up,19};
    auto preservedSheet=CheckSheetPreparation(convexSheet);
    assert(preservedSheet.size()==1 && preservedSheet.front().vertices.size()==convexSheet.vertices.size());
    assert(preservedSheet.front().materialIndex==19);
    assert(Dot(preservedSheet.front().normal,up)==1);
    for (std::size_t index=0;index<convexSheet.vertices.size();++index)
    {
        const auto& original=convexSheet.vertices[index];
        const auto& retained=preservedSheet.front().vertices[index];
        assert(original.x==retained.x && original.y==retained.y && original.z==retained.z);
    }
    auto reverseSheet=convexSheet;
    std::reverse(reverseSheet.vertices.begin(),reverseSheet.vertices.end());
    reverseSheet.normal={0,0,-1};
    preservedSheet=CheckSheetPreparation(reverseSheet);
    assert(preservedSheet.size()==1 && Dot(preservedSheet.front().normal,reverseSheet.normal)==1);
    const Surface tiltedSheet{{{0,0,0},{10,0,10},{10,10,10},{0,10,0}},{-std::sqrt(0.5),0,std::sqrt(0.5)},20};
    preservedSheet=CheckSheetPreparation(tiltedSheet);
    assert(preservedSheet.size()==1 && preservedSheet.front().vertices.size()==4);
    assert(std::fabs(Dot(preservedSheet.front().normal,tiltedSheet.normal)-1)<1e-12);
    // Separate convex sheets around an opening stay separate: the fast path
    // never unions boundaries or fills the centre of a ring.
    double ringArea=0;
    for (const auto& rectangle : {Rectangle(0,0,10,3),Rectangle(0,7,10,10),Rectangle(0,3,3,7),Rectangle(7,3,10,7)})
    {
        const auto ringPart=CheckSheetPreparation({rectangle,up,21});
        assert(ringPart.size()==1 && !Contains(ringPart.front().vertices,up,{5,5,0}));
        ringArea+=Area(ringPart.front().vertices,up);
    }
    assert(ringArea==84);
    auto sheetTriangles=CheckSheetPreparation({concave.vertices,up,7});
    assert(sheetTriangles.size()>1);
    double sheetArea=0;
    for (const Surface& triangle:sheetTriangles) sheetArea+=Area(triangle.vertices,up);
    assert(std::fabs(sheetArea-75)<1e-10);
    const Surface warped{{{0,0,0},{10,0,0},{10,10,2},{0,10,0}},up,8};
    sheetTriangles=CheckSheetPreparation(warped);
    assert(sheetTriangles.size()==2);
    assert(std::any_of(sheetTriangles.begin(),sheetTriangles.end(),[](const Surface& triangle)
    {
        return std::any_of(triangle.vertices.begin(),triangle.vertices.end(),[](const Vec3& vertex)
        {
            return vertex.z==2;
        });
    }));
    const Surface slightlyWarped{{{0,0,0},{10,0,0},{10,10,0.001},{0,10,0}},up,8};
    sheetTriangles=CheckSheetPreparation(slightlyWarped);
    assert(sheetTriangles.size()==2);
    assert(std::any_of(sheetTriangles.begin(),sheetTriangles.end(),[](const Surface& part)
    {
        return std::any_of(part.vertices.begin(),part.vertices.end(),[](const Vec3& vertex)
        {
            return vertex.z==static_cast<float>(0.001);
        });
    }));
    Surface largeConvexSheet{{},up,22};
    for (int vertex=0;vertex<17;++vertex)
    {
        const double angle=vertex*6.283185307179586/17;
        largeConvexSheet.vertices.push_back({std::cos(angle)*10,std::sin(angle)*10,0});
    }
    sheetTriangles=CheckSheetPreparation(largeConvexSheet);
    assert(sheetTriangles.size()>1);
    assert(!PrepareSheetForEditor({crossing,up,1},sheetTriangles,error));
    assert(sheetTriangles.empty() && error.find("self-intersecting")!=std::string::npos);
    assert(!PrepareSheetForEditor({{{0,0,0},{1,1,1},{2,2,2}},up,1},sheetTriangles,error));
    assert(sheetTriangles.empty());
    Limits sheetBudget;
    sheetBudget.maxPieces=1;
    assert(!PrepareSheetForEditor(warped,sheetTriangles,error,sheetBudget));
    assert(sheetTriangles.empty() && error.find("budget")!=std::string::npos);
    sheetBudget.maxPieces=0;
    assert(!PrepareSheetForEditor(convexSheet,sheetTriangles,error,sheetBudget));
    assert(sheetTriangles.empty() && error.find("budget")!=std::string::npos);
    Face invalid=face;
    invalid.vertices[0].x=std::numeric_limits<double>::quiet_NaN();
    MustFail(invalid,{},"invalid");
    invalid=face; invalid.vertices[0].z=1;
    MustFail(invalid,{},"non-planar");

    std::int64_t bucket=0, opposite=0;
    assert(PlaneBucket({1,0,0},{-345.75,0,0},bucket));
    assert(PlaneBucket({-1,0,0},{-345.75,0,0},opposite));
    assert(bucket==345 && bucket==opposite);
    assert(PlaneBucket({7,0,0},{345.75,0,0},bucket) && bucket==345);
    assert(!PlaneBucket(up,{0,0,std::numeric_limits<double>::infinity()},bucket));
    assert(!PlaneBucket(up,{0,0,1e20},bucket));
    assert(!PlaneBucket({0,0,0},{0,0,0},bucket));
    assert(!PlaneBucket(up,{0,0,0},bucket,0));

    std::puts("Recovered surface partition tests passed");
}
