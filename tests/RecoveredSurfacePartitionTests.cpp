// cl /nologo /std:c++17 /W4 /WX /EHsc tests\RecoveredSurfacePartitionTests.cpp Reloaded.Editor\RecoveredSurfacePartition.cpp
#include "../Reloaded.Editor/RecoveredSurfacePartition.h"
#include "../Reloaded.Editor/RecoveredPolygonImport.h"

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

    void CheckCoalescing()
    {
        const Vec3 up{0,0,1};
        const Face face{Rectangle(0,0,10,10),up,0};
        // A thin native-rejected subdivision with the same material does not
        // need to exist at all once the entire face's coverage is known.
        std::vector<Piece> output;
        std::string error;
        assert(Partition(face,{{Rectangle(0,0,0.0001,10),up,99}},99,output,error));
        assert(output.size()==1 && output[0].materialIndex==99);
        assert(output[0].vertices.size()==face.vertices.size());
        for (std::size_t i=0;i<face.vertices.size();++i)
            assert(output[0].vertices[i].x==face.vertices[i].x
                && output[0].vertices[i].y==face.vertices[i].y);

        // Three tiles share one material; a neighbouring material boundary
        // survives. This exercises partial-edge and subdivided-edge joins.
        std::vector<Piece> input{{Rectangle(0,0,5,5),1},{Rectangle(0,5,5,10),1},
            {Rectangle(5,0,8,10),1},{Rectangle(8,0,10,10),2}};
        input[2].vertices.insert(input[2].vertices.begin()+1,{6,0,0});
        assert(CoalesceCoplanarPieces(input,up,output,error));
        assert(output.size()==2);
        auto areas=MaterialAreas(output,up);
        assert(areas[1]==80 && areas[2]==20);
        CheckCoverage(output,up);
        for (const Piece& piece:output) for (const Vec3& point:piece.vertices)
            assert(std::any_of(input.begin(),input.end(),[&](const Piece& source)
            {
                return std::any_of(source.vertices.begin(),source.vertices.end(),[&](const Vec3& original)
                { return point.x==original.x && point.y==original.y && point.z==original.z; });
            }));
        std::vector<Piece> again;
        assert(CoalesceCoplanarPieces(output,up,again,error) && again.size()==output.size());

        // A real notch and an arbitrarily small gap are never filled.
        input={{Rectangle(0,0,10,4),1},{Rectangle(0,4,4,10),1}};
        assert(CoalesceCoplanarPieces(input,up,output,error));
        assert(output.size()==2 && MaterialAreas(output,up)[1]==64);
        input={{Rectangle(0,0,5,10),1},{Rectangle(5.0000000001,0,10,10),1}};
        assert(CoalesceCoplanarPieces(input,up,output,error) && output.size()==2);
        input={{Rectangle(0,0,10,2),1},{Rectangle(0,8,10,10),1},
            {Rectangle(0,2,2,8),1},{Rectangle(8,2,10,8),1}};
        assert(CoalesceCoplanarPieces(input,up,output,error));
        assert(MaterialAreas(output,up)[1]==64);
        for (const Piece& piece:output) assert(!Contains(piece.vertices,up,{5,5,0}));

        // Reversed, translated, non-axis-aligned 3D input retains its exact
        // original coordinates instead of restoring them from a 2D frame.
        input={{Rectangle(0,0,5,10),1},{Rectangle(5,0,10,10),1}};
        for (Piece& piece:input)
        {
            for (Vec3& point:piece.vertices) point={100000+point.x,200000+point.x,point.y-300000};
            std::reverse(piece.vertices.begin(),piece.vertices.end());
        }
        const Vec3 normal{-std::sqrt(0.5),std::sqrt(0.5),0};
        assert(CoalesceCoplanarPieces(input,normal,output,error) && output.size()==1);
        for (const Vec3& point:output[0].vertices)
            assert(std::any_of(input.begin(),input.end(),[&](const Piece& source)
            {
                return std::any_of(source.vertices.begin(),source.vertices.end(),[&](const Vec3& original)
                { return point.x==original.x && point.y==original.y && point.z==original.z; });
            }));
        auto invalid=input;
        for (Vec3& point:invalid[1].vertices) point.x+=1;
        assert(!CoalesceCoplanarPieces(invalid,normal,output,error) && output.empty());
        Limits limited; limited.maxWork=1;
        assert(!CoalesceCoplanarPieces(input,normal,output,error,limited) && output.empty());
        // Plane grouping may admit sub-epsilon 3D bends. Coalescing must not
        // flatten those bends or change their triangulation.
        input={{{{0,0,0},{10,0,0},{0,10,0}},1},
            {{{10,0,0},{10,10,0.0000001},{0,10,0}},1}};
        assert(CoalesceCoplanarPieces(input,up,output,error) && output.size()==2);
        assert(output[1].vertices[1].z==input[1].vertices[1].z);
    }
}

#include "RecoveredSurfacePartitionOffsDFixture.h"
#include "RecoveredSurfacePartitionLegacyD1Fixture.h"
#include "RecoveredSurfacePartitionFloatFixture.h"
#include "RecoveredSurfacePartitionBrushFixture.h"
#include "RecoveredSurfacePartitionEncodingFixture.h"
#include "RecoveredSurfacePartitionSheetFixture.h"

void VerifyNativePrecisionMaterials()
{
    const Vec3 up{0,0,1};
    const Face face{Rectangle(0,0,10,10),up,0};
    const Surface strip{Rectangle(0,0,0.001,10),up,1};
    std::vector<Piece> pieces;
    std::string error;
    Limits native;
    native.matchEditorPrecision=true;
    assert(Partition(face,{strip},99,pieces,error,native));
    assert(pieces.size()==1 && pieces.front().materialIndex==99);
    assert(std::fabs(Area(pieces.front().vertices,up)-100)<1e-10);
    // This strip passes FPoly import, but is narrower than the ordinary BSP
    // splitter's distance band. Preserve its parent face as one polygon.
    const Surface buildSliver{Rectangle(0,0,0.1,10),up,1};
    assert(RecoveredPolygonImport::Prepare(buildSliver.vertices).accepted());
    assert(Partition(face,{buildSliver},99,pieces,error,native));
    assert(pieces.size()==1 && pieces.front().materialIndex==99);
    assert(std::fabs(Area(pieces.front().vertices,up)-100)<1e-10);
    // Exact mode continues to retain the authored narrow paint region.
    assert(Partition(face,{buildSliver},99,pieces,error));
    assert(std::fabs(MaterialAreas(pieces,up)[1]-1)<1e-10);
    // A narrow original face is geometry, and must never be widened or lost.
    const Face thinFace{buildSliver.vertices,up,0};
    assert(Partition(thinFace,{buildSliver},99,pieces,error,native));
    assert(pieces.size()==1 && pieces.front().materialIndex==1);
    assert(std::fabs(Area(pieces.front().vertices,up)-1)<1e-10);
    assert(Partition(face,{{Rectangle(0,0,5,10),up,1}},99,pieces,error,native));
    std::map<int,double> areas;
    for (const auto& piece:pieces) areas[piece.materialIndex]+=Area(piece.vertices,up);
    assert(std::fabs(areas[1]-50)<1e-10 && std::fabs(areas[99]-50)<1e-10);

    // Double-distinct cut endpoints can become native-coincident after
    // float encoding at larger world coordinates. Keep the entire face.
    const Face translated{Rectangle(4096,4096,4112,4112),up,0};
    const Surface angled{{{4096.0021,4096,0},{4112,4096,0},{4112,4112,0},{4104,4112,0}},up,1};
    assert(Partition(translated,{angled},99,pieces,error,native));
    std::vector<Piece> encoded;
    assert(PrepareForEditor(translated,pieces,16,encoded,error));
    double total=0;
    for (const auto& piece:encoded)
    {
        const auto imported=RecoveredPolygonImport::Prepare(piece.vertices);
        assert(imported.accepted() && RecoveredPolygonImport::PreservesOutline(piece.vertices,imported));
        total+=Area(piece.vertices,up);
    }
    assert(std::fabs(total-256)<1e-8);
}

void VerifyNativeBrushBevel()
{
    using Node=RecoveredBspGeometry::Node;
    std::vector<Node> nodes;
    const Vec3 normals[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{1,1,1}};
    for (int i=0;i<7;++i)
        nodes.push_back({{-normals[i].x,-normals[i].y,-normals[i].z},
            i==6 ? -2.999 : -1.0,i==6 ? -1 : i+1,-1,true,i});
    RecoveredBspGeometry::Result geometry;
    std::string error;
    assert(RecoveredBspGeometry::Reconstruct(nodes,false,{{-4,-4,-4},{4,4,4}},geometry,error));
    auto& brush=geometry.brushes.front();
    assert(brush.faces.size()==7);
    assert(CanonicalizeBrushForEditor(brush,error));
    assert(brush.faces.size()==6);
    std::vector<bool> collapsed;
    assert(ValidateEditorBrush(brush,collapsed,error));
    for (const auto& face:brush.faces)
    {
        std::vector<Piece> encoded;
        assert(PrepareForEditor(face,{{face.vertices,0}},16,encoded,error));
        for (const auto& piece:encoded)
        {
            const auto imported=RecoveredPolygonImport::Prepare(piece.vertices);
            assert(imported.accepted() && RecoveredPolygonImport::PreservesOutline(piece.vertices,imported));
        }
    }
    RecoveredBspGeometry::Brush empty;
    assert(!CanonicalizeBrushForEditor(empty,error) && empty.faces.empty());
}

int main()
{
    VerifyNativePrecisionMaterials();
    VerifyNativeBrushBevel();
    CheckCoalescing();
    VerifyOffsDNativeCoincidentSpike();
    VerifyNarrowParentMaterialCut();
    VerifyOffsDFloatSpike();
    VerifyOffsDBoundarySpike();
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
    // Native BSP cuts can leave almost-collinear vertices on a warped sheet
    // edge. Preserve that exact boundary while choosing importable diagonals.
    Surface thinEar{{{2087.425537109375,823.50244140625,-1044},
        {2083.425537109375,816.57440185546875,-1044},
        {2083.425537109375,816.57440185546875,-1332},
        {2092,831.42559814453125,-1332},{2092,831.42559814453125,-1044}},
        {0.8660253286361694,-0.5000001192092896,0},23};
    for (std::size_t rotation=0;rotation<thinEar.vertices.size();++rotation)
    {
        const auto parts=CheckSheetPreparation(thinEar);
        std::map<std::pair<std::size_t,std::size_t>,int> edges;
        for (const auto& part:parts)
        {
            const auto native=RecoveredPolygonImport::Prepare(part.vertices);
            assert(native.accepted() && RecoveredPolygonImport::PreservesOutline(part.vertices,native));
            std::vector<std::size_t> indices;
            for (const auto& vertex:part.vertices)
            {
                const auto match=std::find_if(thinEar.vertices.begin(),thinEar.vertices.end(),[&](const auto& original)
                { return original.x==vertex.x && original.y==vertex.y && original.z==vertex.z; });
                assert(match!=thinEar.vertices.end());
                indices.push_back(static_cast<std::size_t>(match-thinEar.vertices.begin()));
            }
            for (std::size_t i=0;i<indices.size();++i)
            {
                const auto a=indices[i],b=indices[(i+1)%indices.size()];
                ++edges[{a,b}]; --edges[{b,a}];
            }
        }
        for (std::size_t i=0;i<thinEar.vertices.size();++i)
        {
            const auto next=(i+1)%thinEar.vertices.size();
            --edges[{i,next}]; ++edges[{next,i}];
        }
        for (const auto& edge:edges) assert(edge.second==0);
        std::rotate(thinEar.vertices.begin(),thinEar.vertices.begin()+1,thinEar.vertices.end());
    }
    Surface largeConvexSheet{{},up,22};
    // A concave float boundary can also contain very shallow ears. A failed
    // convex fast-path probe must not poison successful triangulation/retry.
    const Surface shallowConcave{{{400.01385498046875,2179.999755859375,-448},
        {8,2180,-448},{-496,2180.00048828125,-448},{-623.99822998046875,2180.00048828125,-448},
        {-1284.001220703125,2180.001220703125,-448},{-1392,2180.001220703125,-448},
        {-2298.92431640625,2180.001220703125,-448},{-2922.96142578125,2804.038330078125,-448},
        {-2927.994140625,2809.071044921875,-448},{-3030.923095703125,2912,-448},
        {-2547,2912,-448},{432,2912,-448},{432,2264,-448},{432,2179.999755859375,-448}},
        {0,0,-1},24};
    sheetTriangles=CheckSheetPreparation(shallowConcave);
    assert(sheetTriangles.size()>1);
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
