#include "RecoveredBspGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace RecoveredBspGeometry
{
    namespace
    {
        Vec3 Add(const Vec3& a, const Vec3& b)
        {
            return { a.x + b.x, a.y + b.y, a.z + b.z };
        }

        Vec3 Subtract(const Vec3& a, const Vec3& b)
        {
            return { a.x - b.x, a.y - b.y, a.z - b.z };
        }

        Vec3 Scale(const Vec3& p, double scale)
        {
            return { p.x * scale, p.y * scale, p.z * scale };
        }

        double Dot(const Vec3& a, const Vec3& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        Vec3 Cross(const Vec3& a, const Vec3& b)
        {
            return { a.y * b.z - a.z * b.y,
                     a.z * b.x - a.x * b.z,
                     a.x * b.y - a.y * b.x };
        }

        bool Finite(const Vec3& p)
        {
            return std::isfinite(p.x) && std::isfinite(p.y)
                && std::isfinite(p.z);
        }

        bool Near(const Vec3& a, const Vec3& b, double epsilon)
        {
            const Vec3 delta = Subtract(a, b);
            return Dot(delta, delta) <= epsilon * epsilon;
        }

        struct CellFace
        {
            Face face;
            bool artificialBoundary = false;
        };

        using Cell = std::vector<CellFace>;

        struct Work
        {
            const Limits& limits;
            std::size_t count = 0;
            std::string& error;
            bool exhausted = false;

            bool Spend(std::size_t amount)
            {
                if (amount > limits.maxClippingWork
                    || count > limits.maxClippingWork - amount)
                {
                    exhausted = true;
                    error = "BSP reconstruction exceeded its clipping-work limit. "
                            "This map needs a less fragmented source reconstruction.";
                    return false;
                }
                count += amount;
                return true;
            }
        };

        Cell MakeBox(const Bounds& bounds)
        {
            const double x = bounds.minimum.x, X = bounds.maximum.x;
            const double y = bounds.minimum.y, Y = bounds.maximum.y;
            const double z = bounds.minimum.z, Z = bounds.maximum.z;
            Cell cell;
            cell.push_back({ { { {x,y,z}, {x,y,Z}, {x,Y,Z}, {x,Y,z} }, {-1,0,0}, -1 }, true });
            cell.push_back({ { { {X,y,z}, {X,Y,z}, {X,Y,Z}, {X,y,Z} }, {1,0,0}, -1 }, true });
            cell.push_back({ { { {x,y,z}, {X,y,z}, {X,y,Z}, {x,y,Z} }, {0,-1,0}, -1 }, true });
            cell.push_back({ { { {x,Y,z}, {x,Y,Z}, {X,Y,Z}, {X,Y,z} }, {0,1,0}, -1 }, true });
            cell.push_back({ { { {x,y,z}, {x,Y,z}, {X,Y,z}, {X,y,z} }, {0,0,-1}, -1 }, true });
            cell.push_back({ { { {x,y,Z}, {X,y,Z}, {X,Y,Z}, {x,Y,Z} }, {0,0,1}, -1 }, true });
            return cell;
        }

        void AppendUnique(std::vector<Vec3>& points, const Vec3& point,
                          double epsilon)
        {
            if (points.empty() || !Near(points.back(), point, epsilon))
                points.push_back(point);
        }

        void CleanPolygon(std::vector<Vec3>& vertices, double epsilon)
        {
            if (vertices.size() > 1 && Near(vertices.front(), vertices.back(), epsilon))
                vertices.pop_back();
            // Keep collinear vertices: removing them independently on adjacent
            // faces introduces T-junctions. The caller may triangulate long
            // FPolys while retaining all edge points.
        }

        bool HasArea(const std::vector<Vec3>& points, const Vec3& normal,
                     double epsilon)
        {
            if (points.size() < 3)
                return false;
            Vec3 area{};
            for (std::size_t index = 1; index + 1 < points.size(); ++index)
                area = Add(area, Cross(Subtract(points[index], points[0]),
                                       Subtract(points[index + 1], points[0])));
            const double orientedArea = Dot(area, normal);
            return Finite(area) && std::isfinite(orientedArea)
                && orientedArea > epsilon * epsilon;
        }

        // Keep the negative half-space n.p <= d. Existing boundary polygons
        // retain their identity; the new cap receives the splitter's surface.
        bool Clip(const Cell& input, const Vec3& normal, double distance,
                  int surfaceIndex, Cell& output, Work& work)
        {
            const double epsilon = work.limits.epsilon;
            double minimum = std::numeric_limits<double>::infinity();
            double maximum = -minimum;
            for (const CellFace& face : input)
            {
                if (!work.Spend(face.face.vertices.size()))
                    return false;
                for (const Vec3& point : face.face.vertices)
                {
                    const double side = Dot(normal, point) - distance;
                    if (!std::isfinite(side))
                    {
                        work.error = "BSP clipping produced a non-finite plane distance.";
                        return false;
                    }
                    minimum = (std::min)(minimum, side);
                    maximum = (std::max)(maximum, side);
                }
            }
            output.clear();
            if (minimum >= -epsilon)
                return true; // Empty, or a cell collapsed to a plane.
            if (maximum <= epsilon)
            {
                output = input;
                return true;
            }

            std::vector<Vec3> cap;
            output.reserve(input.size() + 1);
            for (const CellFace& face : input)
            {
                if (!work.Spend(face.face.vertices.size()))
                    return false;
                CellFace clipped;
                clipped.face.normal = face.face.normal;
                clipped.face.surfaceIndex = face.face.surfaceIndex;
                clipped.artificialBoundary = face.artificialBoundary;
                const auto& vertices = face.face.vertices;
                for (std::size_t index = 0; index < vertices.size(); ++index)
                {
                    const Vec3& a = vertices[index];
                    const Vec3& b = vertices[(index + 1) % vertices.size()];
                    const double da = Dot(normal, a) - distance;
                    const double db = Dot(normal, b) - distance;
                    if (da <= 0)
                        AppendUnique(clipped.face.vertices, a, epsilon);
                    if ((da < 0 && db > 0) || (da > 0 && db < 0))
                    {
                        const double t = da / (da - db);
                        const Vec3 intersection = Add(a, Scale(Subtract(b, a), t));
                        if (!Finite(intersection))
                        {
                            work.error = "BSP clipping produced a non-finite vertex.";
                            return false;
                        }
                        AppendUnique(clipped.face.vertices, intersection, epsilon);
                        cap.push_back(intersection);
                    }
                    else if (da == 0)
                        cap.push_back(a);
                }
                CleanPolygon(clipped.face.vertices, epsilon);
                if (HasArea(clipped.face.vertices, clipped.face.normal, epsilon))
                    output.push_back(std::move(clipped));
            }

            std::vector<Vec3> uniqueCap;
            for (const Vec3& point : cap)
            {
                if (!work.Spend(uniqueCap.size()))
                    return false;
                if (std::none_of(uniqueCap.begin(), uniqueCap.end(),
                    [&](const Vec3& other) { return Near(point, other, epsilon); }))
                    uniqueCap.push_back(point);
            }
            if (uniqueCap.size() < 3)
            {
                work.error = "A BSP split could not produce a closed cap. "
                            "The cooked geometry is degenerate at the reconstruction tolerance.";
                return false;
            }

            Vec3 center = uniqueCap.front();
            for (std::size_t index = 1; index < uniqueCap.size(); ++index)
                center = Add(center, Scale(Subtract(uniqueCap[index], center),
                                          1.0 / static_cast<double>(index + 1)));
            const Vec3 reference = std::fabs(normal.x) < 0.8
                ? Vec3{1,0,0} : Vec3{0,1,0};
            Vec3 u = Cross(reference, normal);
            u = Scale(u, 1.0 / std::sqrt(Dot(u, u)));
            const Vec3 v = Cross(normal, u);
            std::sort(uniqueCap.begin(), uniqueCap.end(),
                [&](const Vec3& a, const Vec3& b)
                {
                    const Vec3 da = Subtract(a, center), db = Subtract(b, center);
                    return std::atan2(Dot(v, da), Dot(u, da))
                         < std::atan2(Dot(v, db), Dot(u, db));
                });
            if (!HasArea(uniqueCap, normal, epsilon))
            {
                work.error = "A BSP split produced a zero-area or reversed cap.";
                return false;
            }
            output.push_back({ { std::move(uniqueCap), normal, surfaceIndex }, false });
            if (output.size() > work.limits.maxFacesPerBrush)
            {
                work.error = "A reconstructed BSP cell exceeded the brush-face limit.";
                return false;
            }
            return true;
        }

        bool ValidateNodes(const std::vector<Node>& input, std::vector<Node>& nodes,
                           const Limits& limits, std::string& error)
        {
            if (input.size() > limits.maxNodes
                || input.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            {
                error = "The cooked BSP exceeds the reconstruction node limit.";
                return false;
            }
            nodes = input;
            for (Node& node : nodes)
            {
                const double length = std::hypot(node.normal.x, node.normal.y, node.normal.z);
                if (!Finite(node.normal) || !std::isfinite(node.distance)
                    || !std::isfinite(length) || length < 1e-12)
                {
                    error = "The cooked BSP contains an invalid splitting plane.";
                    return false;
                }
                node.normal = Scale(node.normal, 1.0 / length);
                node.distance /= length;
                if (!std::isfinite(node.distance))
                {
                    error = "A cooked BSP plane could not be normalized safely.";
                    return false;
                }
                if (node.front < -1 || node.back < -1
                    || node.front >= static_cast<int>(nodes.size())
                    || node.back >= static_cast<int>(nodes.size()))
                {
                    error = "The cooked BSP contains an invalid child index.";
                    return false;
                }
            }
            if (nodes.empty())
                return true;

            // A visited node in a front/back tree can never be encountered a
            // second time. This catches both cycles and shared-child graphs,
            // including paths whose geometry will later clip to nothing.
            std::vector<unsigned char> visited(nodes.size(), 0);
            std::vector<int> pending{0};
            while (!pending.empty())
            {
                const int index = pending.back();
                pending.pop_back();
                if (visited[static_cast<std::size_t>(index)] != 0)
                {
                    error = "The cooked BSP front/back tree contains a cycle or repeated node.";
                    return false;
                }
                visited[static_cast<std::size_t>(index)] = 1;
                const Node& node = nodes[static_cast<std::size_t>(index)];
                if (node.front >= 0)
                    pending.push_back(node.front);
                if (node.back >= 0)
                    pending.push_back(node.back);
            }
            return true;
        }

        bool ValidateClosedCell(const Cell& cell, Work& work)
        {
            const double epsilon = work.limits.epsilon;
            std::vector<Vec3> points;
            std::vector<std::pair<std::size_t, std::size_t>> edges;
            double volumeSix = 0;
            const Vec3 reference = cell.front().face.vertices.front();
            for (const CellFace& cellFace : cell)
            {
                const Face& face = cellFace.face;
                if (face.vertices.size() < 3 || !HasArea(face.vertices, face.normal, epsilon))
                {
                    work.error = "A reconstructed brush contains a degenerate face.";
                    return false;
                }
                std::vector<std::size_t> indices;
                for (const Vec3& point : face.vertices)
                {
                    if (!work.Spend(points.size() + 1))
                        return false;
                    if (!Finite(point) || std::fabs(Dot(face.normal,
                            Subtract(point, face.vertices.front()))) > epsilon * 8)
                    {
                        work.error = "A reconstructed brush contains a non-planar face.";
                        return false;
                    }
                    std::size_t index = 0;
                    while (index < points.size() && !Near(points[index], point, epsilon))
                        ++index;
                    if (index == points.size())
                        points.push_back(point);
                    indices.push_back(index);
                }
                for (std::size_t index = 0; index < indices.size(); ++index)
                    edges.emplace_back(indices[index], indices[(index + 1) % indices.size()]);
                for (std::size_t index = 1; index + 1 < face.vertices.size(); ++index)
                    volumeSix += Dot(Subtract(face.vertices[0], reference),
                        Cross(Subtract(face.vertices[index], reference),
                              Subtract(face.vertices[index + 1], reference)));
            }
            if (!std::isfinite(volumeSix) || volumeSix <= epsilon * epsilon * epsilon)
            {
                work.error = "A reconstructed brush has zero or reversed volume.";
                return false;
            }
            std::sort(edges.begin(), edges.end());
            for (std::size_t index = 0; index < edges.size(); ++index)
            {
                const auto edge = edges[index];
                if (edge.first == edge.second
                    || (index != 0 && edge == edges[index - 1])
                    || !std::binary_search(edges.begin(), edges.end(),
                                          std::make_pair(edge.second, edge.first)))
                {
                    work.error = "A reconstructed brush is not a closed two-sided edge manifold. "
                                 "The cooked BSP is degenerate at the reconstruction tolerance.";
                    return false;
                }
            }
            return true;
        }

        Bounds FaceBounds(const Face& face)
        {
            Bounds result{face.vertices.front(),face.vertices.front()};
            for (const Vec3& point:face.vertices)
            {
                result.minimum.x=(std::min)(result.minimum.x,point.x);
                result.minimum.y=(std::min)(result.minimum.y,point.y);
                result.minimum.z=(std::min)(result.minimum.z,point.z);
                result.maximum.x=(std::max)(result.maximum.x,point.x);
                result.maximum.y=(std::max)(result.maximum.y,point.y);
                result.maximum.z=(std::max)(result.maximum.z,point.z);
            }
            return result;
        }

        double FaceArea(const Face& face)
        {
            double twice=0;
            for (std::size_t index=1;index+1<face.vertices.size();++index)
                twice+=Dot(face.normal,Cross(Subtract(face.vertices[index],face.vertices.front()),
                    Subtract(face.vertices[index+1],face.vertices.front())));
            return twice*0.5;
        }

        bool ContainsFace(const Face& outer,const Face& inner,Work& work)
        {
            for (std::size_t index=0;index<outer.vertices.size();++index)
            {
                if (!work.Spend(inner.vertices.size())) return false;
                const Vec3& start=outer.vertices[index];
                const Vec3 edge=Subtract(outer.vertices[(index+1)%outer.vertices.size()],start);
                const double length=std::hypot(edge.x,edge.y,edge.z);
                if (length<=work.limits.epsilon) return false;
                for (const Vec3& point:inner.vertices)
                    if (Dot(outer.normal,Cross(edge,Subtract(point,start))) < -work.limits.epsilon*length)
                        return false;
            }
            return true;
        }

        bool BehindPlanes(const Brush& planes,const Brush& points,std::size_t excluded,Work& work)
        {
            for (std::size_t index=0;index<planes.faces.size();++index)
            {
                if (index==excluded) continue;
                const Face& plane=planes.faces[index];
                for (const Face& face:points.faces)
                {
                    if (!work.Spend(face.vertices.size())) return false;
                    for (const Vec3& point:face.vertices)
                        if (Dot(plane.normal,Subtract(point,plane.vertices.front()))>work.limits.epsilon)
                            return false;
                }
            }
            return true;
        }

        double BrushVolume(const Brush& brush,const Vec3& reference)
        {
            double six=0;
            for (const Face& face:brush.faces)
                for (std::size_t index=1;index+1<face.vertices.size();++index)
                    six+=Dot(Subtract(face.vertices[0],reference),Cross(Subtract(face.vertices[index],reference),
                        Subtract(face.vertices[index+1],reference)));
            return six/6.0;
        }

        bool TryMerge(const Brush& a,std::size_t aFace,const Brush& b,std::size_t bFace,
                       Brush& merged,Work& work)
        {
            const Face& first=a.faces[aFace];
            const Face& second=b.faces[bFace];
            if (a.subtractive!=b.subtractive || !Near(first.normal,Scale(second.normal,-1),1e-12)) return false;
            for (const Vec3& point:second.vertices)
                if (std::fabs(Dot(first.normal,Subtract(point,first.vertices.front())))>work.limits.epsilon)
                    return false;
            if (!ContainsFace(first,second,work) || !ContainsFace(second,first,work)
                || !BehindPlanes(a,b,aFace,work) || !BehindPlanes(b,a,bFace,work)) return false;

            Bounds bounds=FaceBounds(first);
            double area=0;
            for (const Brush* brush:{&a,&b}) for (const Face& face:brush->faces)
            {
                area+=FaceArea(face);
                const Bounds part=FaceBounds(face);
                bounds.minimum.x=(std::min)(bounds.minimum.x,part.minimum.x);
                bounds.minimum.y=(std::min)(bounds.minimum.y,part.minimum.y);
                bounds.minimum.z=(std::min)(bounds.minimum.z,part.minimum.z);
                bounds.maximum.x=(std::max)(bounds.maximum.x,part.maximum.x);
                bounds.maximum.y=(std::max)(bounds.maximum.y,part.maximum.y);
                bounds.maximum.z=(std::max)(bounds.maximum.z,part.maximum.z);
            }
            const Vec3 reference=bounds.minimum;
            bounds.minimum=Subtract(bounds.minimum,{1,1,1});
            bounds.maximum=Add(bounds.maximum,{1,1,1});
            Cell cell=MakeBox(bounds);
            for (const Brush* brush:{&a,&b}) for (std::size_t index=0;index<brush->faces.size();++index)
            {
                if (index==(brush==&a?aFace:bFace)) continue;
                const Face& face=brush->faces[index];
                Cell next;
                if (!Clip(cell,face.normal,Dot(face.normal,face.vertices.front()),face.surfaceIndex,next,work)) return false;
                cell=std::move(next);
                if (cell.empty() || cell.size()>work.limits.maxFacesPerBrush) return false;
            }
            if (std::any_of(cell.begin(),cell.end(),[](const CellFace& face){return face.artificialBoundary;})) return false;
            // A candidate that cannot retain a closed boundary is simply not
            // merged; the original independently valid cells remain intact.
            const std::string previousError=work.error;
            if (!ValidateClosedCell(cell,work))
            {
                if (!work.exhausted) work.error=previousError;
                return false;
            }
            merged.subtractive=a.subtractive;
            for (CellFace& face:cell) merged.faces.push_back(std::move(face.face));
            const double before=BrushVolume(a,reference)+BrushVolume(b,reference);
            const double after=BrushVolume(merged,reference);
            if (!std::isfinite(before) || !std::isfinite(after) || before<=0 || after<=0
                || std::fabs(after-before)>work.limits.epsilon*area+std::fabs(before)*1e-12)
            {
                merged.faces.clear();
                return false;
            }
            return true;
        }
    }

    bool MergeAdjacentConvexBrushes(Result& result,std::string& error,const Limits& limits)
    try
    {
        error.clear();
        if (!std::isfinite(limits.epsilon) || limits.epsilon<=0 || result.brushes.size()>limits.maxBrushes)
        {
            error="Convex brush merging requires valid geometry limits.";
            return false;
        }
        Work work{limits,0,error};
        std::size_t faceCount=0;
        for (const Brush& brush:result.brushes)
        {
            if (brush.faces.size()<4 || brush.faces.size()>limits.maxFacesPerBrush
                || brush.faces.size()>limits.maxTotalFaces-faceCount)
            {
                error="A brush exceeds the convex merge face limits.";
                return false;
            }
            faceCount+=brush.faces.size();
            Cell cell;
            for (const Face& face:brush.faces)
            {
                if (face.vertices.size()<3 || !Finite(face.normal) || std::fabs(Dot(face.normal,face.normal)-1)>1e-10)
                {
                    error="Convex brush merging requires finite unit face normals.";
                    return false;
                }
                cell.push_back({face,false});
            }
            if (!ValidateClosedCell(cell,work)) return false;
            if (!BehindPlanes(brush,brush,brush.faces.size(),work))
            {
                if (!work.exhausted) error="Convex brush merging received a non-convex brush.";
                return false;
            }
        }
        std::vector<Brush> brushes=result.brushes;
        using Key=std::tuple<double,double,double,long long>;
        struct Reference { std::size_t brush,face; Bounds bounds; };
        struct Candidate { std::size_t a,b,aFace,bFace; double area; };
        // Disjoint merges in each pass permit deterministic ordering without
        // invalidating queued face indices. The cap bounds even hostile data.
        for (unsigned pass=0;pass<64;++pass)
        {
            std::map<Key,std::vector<Reference>> groups;
            std::vector<Candidate> candidates;
            for (std::size_t bi=0;bi<brushes.size();++bi)
                for (std::size_t fi=0;fi<brushes[bi].faces.size();++fi)
                {
                    const Face& face=brushes[bi].faces[fi];
                    Vec3 normal=face.normal;
                    if (normal.x<0 || (normal.x==0 && normal.y<0)
                        || (normal.x==0 && normal.y==0 && normal.z<0)) normal=Scale(normal,-1);
                    const double distance=std::floor(Dot(normal,face.vertices.front()));
                    if (!std::isfinite(distance) || std::fabs(distance)>=9e18)
                    {
                        error="A brush plane exceeds convex merge indexing limits.";
                        return false;
                    }
                    const auto bucket=static_cast<long long>(distance);
                    const Bounds bounds=FaceBounds(face);
                    for (long long offset=-1;offset<=1;++offset)
                    {
                        const auto found=groups.find({normal.x,normal.y,normal.z,bucket+offset});
                        if (found==groups.end()) continue;
                        for (const Reference& other:found->second)
                        {
                            if (!work.Spend(1)) return false;
                            if (other.brush==bi || brushes[other.brush].subtractive!=brushes[bi].subtractive
                                || !Near(bounds.minimum,other.bounds.minimum,limits.epsilon)
                                || !Near(bounds.maximum,other.bounds.maximum,limits.epsilon)) continue;
                            const Face& old=brushes[other.brush].faces[other.face];
                            if (Dot(old.normal,face.normal)>0) continue;
                            if (candidates.size()>=500000)
                            {
                                error="Convex brush merging exceeded its candidate budget.";
                                return false;
                            }
                            candidates.push_back({other.brush,bi,other.face,fi,FaceArea(face)});
                        }
                    }
                    groups[{normal.x,normal.y,normal.z,bucket}].push_back({bi,fi,bounds});
                }
            std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b)
            {
                if (a.area!=b.area) return a.area>b.area;
                return std::tie(a.a,a.b,a.aFace,a.bFace)<std::tie(b.a,b.b,b.aFace,b.bFace);
            });
            std::vector<bool> changed(brushes.size(),false),removed(brushes.size(),false);
            std::size_t mergedCount=0;
            for (const Candidate& candidate:candidates)
            {
                if (changed[candidate.a] || changed[candidate.b]) continue;
                Brush merged;
                if (!TryMerge(brushes[candidate.a],candidate.aFace,brushes[candidate.b],candidate.bFace,merged,work))
                {
                    if (work.exhausted) return false;
                    error.clear();
                    continue;
                }
                brushes[candidate.a]=std::move(merged);
                removed[candidate.b]=true;
                changed[candidate.a]=changed[candidate.b]=true;
                ++mergedCount;
            }
            if (!mergedCount) break;
            std::vector<Brush> remaining;
            remaining.reserve(brushes.size()-mergedCount);
            for (std::size_t index=0;index<brushes.size();++index)
                if (!removed[index]) remaining.push_back(std::move(brushes[index]));
            brushes=std::move(remaining);
        }
        result.brushes=std::move(brushes);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        error="There is insufficient memory to merge recovered brushes."; return false;
    }
    catch (const std::length_error&)
    {
        error="Convex brush merging exceeds container capacity."; return false;
    }

    bool OrderForRebuild(Result& result,std::string& error)
    try
    {
        error.clear();
        std::vector<std::pair<double,std::size_t>> order;
        order.reserve(result.brushes.size());
        for (std::size_t index=0;index<result.brushes.size();++index)
        {
            const Brush& brush=result.brushes[index];
            if (brush.subtractive!=result.brushes.front().subtractive)
            {
                error="Mixed additive and subtractive brushes cannot be reordered safely.";
                return false;
            }
            if (brush.faces.empty() || brush.faces.front().vertices.empty())
            {
                error="A recovered brush has no geometry for rebuild ordering.";
                return false;
            }
            const double volume=BrushVolume(brush,brush.faces.front().vertices.front());
            if (!std::isfinite(volume) || volume<=0)
            {
                error="A recovered brush has invalid volume for rebuild ordering.";
                return false;
            }
            order.emplace_back(volume,index);
        }
        std::stable_sort(order.begin(),order.end(),[](const auto& a,const auto& b)
        {
            return a.first>b.first;
        });
        std::vector<Brush> ordered;
        ordered.reserve(result.brushes.size());
        for (const auto& entry:order)
            ordered.push_back(std::move(result.brushes[entry.second]));
        result.brushes=std::move(ordered);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        error="There is insufficient memory to order the recovered brushes."; return false;
    }
    catch (const std::length_error&)
    {
        error="Rebuild brush ordering exceeds container capacity."; return false;
    }

    bool Reconstruct(const std::vector<Node>& input, bool rootOutside,
                     const Bounds& extractionBounds, Result& result,
                     std::string& error, const Limits& limits)
    try
    {
        result = {};
        error.clear();
        if (!std::isfinite(limits.epsilon) || limits.epsilon <= 0
            || !Finite(extractionBounds.minimum) || !Finite(extractionBounds.maximum))
        {
            error = "BSP reconstruction requires finite bounds and a positive tolerance.";
            return false;
        }
        const Vec3 extent = Subtract(extractionBounds.maximum, extractionBounds.minimum);
        if (!Finite(extent) || extent.x <= limits.epsilon
            || extent.y <= limits.epsilon || extent.z <= limits.epsilon)
        {
            error = "BSP reconstruction bounds must enclose a nonzero three-dimensional region.";
            return false;
        }
        if (limits.maxFacesPerBrush < 6 || limits.maxPendingCells == 0)
        {
            error = "BSP reconstruction limits cannot accommodate the initial clipping volume.";
            return false;
        }

        std::vector<Node> nodes;
        if (!ValidateNodes(input, nodes, limits, error))
            return false;

        struct PendingCell
        {
            int nodeIndex;
            bool outside;
            Cell cell;
        };
        std::vector<PendingCell> pending;
        pending.push_back({ nodes.empty() ? -1 : 0, rootOutside, MakeBox(extractionBounds) });
        Work work{ limits, 0, error };
        Result reconstructed;
        std::size_t totalFaces = 0;
        while (!pending.empty())
        {
            PendingCell current = std::move(pending.back());
            pending.pop_back();
            if (!work.Spend(1))
                return false;
            if (current.cell.empty())
                continue;
            if (current.nodeIndex == -1)
            {
                if (current.outside)
                    ++reconstructed.emptyLeafCount;
                else
                    ++reconstructed.solidLeafCount;
                if (current.outside == rootOutside)
                    continue;
                if (std::any_of(current.cell.begin(), current.cell.end(),
                    [](const CellFace& face) { return face.artificialBoundary; }))
                {
                    error = "A recovered structural cell reaches the extraction bounds. "
                            "Its space is unbounded or the bounds do not enclose the map. "
                            "Expand the bounds or repair the source BSP; no artificial walls were created.";
                    return false;
                }
                if (!ValidateClosedCell(current.cell, work))
                    return false;
                if (reconstructed.brushes.size() >= limits.maxBrushes
                    || current.cell.size() > limits.maxTotalFaces
                    || totalFaces > limits.maxTotalFaces - current.cell.size())
                {
                    error = "The reconstructed map exceeds the brush or total-face limit. "
                            "This map needs a less fragmented source reconstruction.";
                    return false;
                }
                totalFaces += current.cell.size();
                Brush brush;
                brush.subtractive = !rootOutside;
                brush.faces.reserve(current.cell.size());
                for (CellFace& face : current.cell)
                    brush.faces.push_back(std::move(face.face));
                reconstructed.brushes.push_back(std::move(brush));
                continue;
            }

            const Node& node = nodes[static_cast<std::size_t>(current.nodeIndex)];
            Cell front, back;
            if (!Clip(current.cell, Scale(node.normal, -1), -node.distance,
                      node.surfaceIndex, front, work)
                || !Clip(current.cell, node.normal, node.distance,
                         node.surfaceIndex, back, work))
                return false;
            const std::size_t additional = (front.empty() ? 0u : 1u)
                                         + (back.empty() ? 0u : 1u);
            if (additional > limits.maxPendingCells
                || pending.size() > limits.maxPendingCells - additional)
            {
                error = "BSP reconstruction exceeded its pending-cell limit.";
                return false;
            }
            if (!back.empty())
                pending.push_back({ node.back, current.outside && !node.isCsg, std::move(back) });
            if (!front.empty())
                pending.push_back({ node.front, current.outside || node.isCsg, std::move(front) });
        }
        result = std::move(reconstructed);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        result = {};
        error = "The editor ran out of memory while reconstructing BSP cells. "
                "The original map has not been converted.";
        return false;
    }
    catch (const std::length_error&)
    {
        result = {};
        error = "The BSP reconstruction exceeds the container capacity on this platform.";
        return false;
    }
}
