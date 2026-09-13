#include "RecoveredPolygonImport.h"

#include <cmath>
#include <limits>

namespace RecoveredPolygonImport
{
    bool VertexPool::Resolve(const Vec3& point,Vec3& canonical,std::string& error)
    {
        const double tolerance=static_cast<double>(kCoincidentVertexTolerance);
        const double sourceCoordinates[]={point.x,point.y,point.z};
        for (const double coordinate:sourceCoordinates)
            if (!std::isfinite(coordinate)
                || std::fabs(coordinate)>(std::numeric_limits<float>::max)())
            {
                error="A recovered vertex cannot be represented by editor float coordinates.";
                return false;
            }
        const Vec3 rounded{static_cast<float>(point.x),static_cast<float>(point.y),
            static_cast<float>(point.z)};
        const double coordinates[]={rounded.x,rounded.y,rounded.z};
        std::int64_t keys[3]{};
        for (int axis=0;axis<3;++axis)
        {
            const double cell=std::floor(coordinates[axis]/tolerance);
            // Leave room for neighbour-cell lookup and avoid conversion at
            // the rounded int64 maximum. This exceeds native map dimensions.
            if (!std::isfinite(cell) || std::fabs(cell)>9e18)
            {
                error="A recovered vertex is outside the editor point-pool coordinate range.";
                return false;
            }
            keys[axis]=static_cast<std::int64_t>(cell);
        }
        std::size_t match=anchors_.size();
        for (int x=-1;x<=1;++x) for (int y=-1;y<=1;++y) for (int z=-1;z<=1;++z)
        {
            const auto found=cells_.find({keys[0]+x,keys[1]+y,keys[2]+z});
            if (found==cells_.end()) continue;
            for (const std::size_t index:found->second)
            {
                if (index>=match) continue;
                const Vec3& anchor=anchors_[index];
                if (std::fabs(anchor.x-rounded.x)<tolerance
                    && std::fabs(anchor.y-rounded.y)<tolerance
                    && std::fabs(anchor.z-rounded.z)<tolerance) match=index;
            }
        }
        if (match<anchors_.size()) canonical=anchors_[match];
        else
        {
            if (anchors_.size()>=maxPoints_)
            {
                error="Recovery exceeded its shared-vertex work budget.";
                return false;
            }
            cells_[{keys[0],keys[1],keys[2]}].push_back(anchors_.size());
            anchors_.push_back(rounded);
            canonical=rounded;
        }
        return true;
    }

    namespace
    {
        // Force the stores made by the native x87 implementation even when
        // this portable helper is compiled with excess intermediate precision.
        double FloatStore(double value)
        {
            const double largest = (std::numeric_limits<float>::max)();
            if (value>largest || value<-largest)
                return std::copysign(std::numeric_limits<double>::infinity(),value);
            volatile float stored = static_cast<float>(value);
            return stored;
        }

        bool Finite(const Vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y)
                && std::isfinite(value.z);
        }

        bool SamePoint(const Vec3& a,const Vec3& b)
        {
            // 10ED0010, called by FPoly::Fix at 110BFDFF. The comparisons
            // are strict and componentwise, not a Euclidean distance test.
            const double tolerance = static_cast<double>(kCoincidentVertexTolerance);
            return std::fabs(a.x-b.x)<tolerance
                && std::fabs(a.y-b.y)<tolerance
                && std::fabs(a.z-b.z)<tolerance;
        }

        double Squared(const Vec3& value)
        {
            return (value.x*value.x+value.y*value.y)+value.z*value.z;
        }

        bool Equal(const Vec3& a,const Vec3& b)
        {
            return a.x==b.x && a.y==b.y && a.z==b.z;
        }

        bool ExactDifference(double a,double b,double& difference)
        {
            difference = a-b;
            const double virtualB = a-difference;
            const double residual = (a-(difference+virtualB))+(virtualB-b);
            return residual==0;
        }

        bool EqualProducts(double a,double b,double c,double d)
        {
            const double first = a*b;
            const double second = c*d;
            // Products of differences of float32 values cannot overflow or
            // underflow double. FMA retains the exact multiplication residual,
            // preventing rounded cross-product cancellation from proving a
            // false collinearity at widely differing coordinate magnitudes.
            return first==second && std::fma(a,b,-first)==std::fma(c,d,-second);
        }

        bool OnSegment(const Vec3& point,const Vec3& start,const Vec3& end)
        {
            // An unchanged endpoint lies on its edge exactly, even when
            // subtracting widely separated float coordinates would round in
            // double (EDE64 has edges from roughly -5.7e-14 to 384).
            if (Equal(point,start) || Equal(point,end)) return true;
            Vec3 edge,offset;
            if (!ExactDifference(end.x,start.x,edge.x)
                || !ExactDifference(end.y,start.y,edge.y)
                || !ExactDifference(end.z,start.z,edge.z)
                || !ExactDifference(point.x,start.x,offset.x)
                || !ExactDifference(point.y,start.y,offset.y)
                || !ExactDifference(point.z,start.z,offset.z))
                return false;
            if ((point.x<start.x && point.x<end.x) || (point.x>start.x && point.x>end.x)
                || (point.y<start.y && point.y<end.y) || (point.y>start.y && point.y>end.y)
                || (point.z<start.z && point.z<end.z) || (point.z>start.z && point.z>end.z))
                return false;
            return EqualProducts(edge.y,offset.z,edge.z,offset.y)
                && EqualProducts(edge.z,offset.x,edge.x,offset.z)
                && EqualProducts(edge.x,offset.y,edge.y,offset.x);
        }

        double DominantCoordinate(const Vec3& point,const Vec3& start,const Vec3& end)
        {
            const double x = std::fabs(end.x-start.x);
            const double y = std::fabs(end.y-start.y);
            const double z = std::fabs(end.z-start.z);
            return x>=y && x>=z ? point.x : y>=z ? point.y : point.z;
        }
    }

    Result Prepare(const std::vector<Vec3>& polygon)
    {
        Result result;
        if (polygon.size()<3)
            return result;
        if (polygon.size()>kMaximumVertices)
        {
            result.reason = Reason::TooManyVertices;
            return result;
        }

        std::vector<Vec3> encoded;
        encoded.reserve(polygon.size());
        const double largestFloat = (std::numeric_limits<float>::max)();
        for (const Vec3& point:polygon)
        {
            if (!Finite(point) || std::fabs(point.x)>largestFloat
                || std::fabs(point.y)>largestFloat || std::fabs(point.z)>largestFloat)
            {
                result.reason = Reason::NonFiniteCoordinate;
                return result;
            }
            encoded.push_back({FloatStore(point.x),FloatStore(point.y),FloatStore(point.z)});
        }

        // 110BFDA0: compare the first point with the last original point,
        // then each point with the most recently accepted point. There is
        // no separate collinear-point simplification or second wrap pass.
        result.vertices.reserve(encoded.size());
        Vec3 previous = encoded.back();
        for (const Vec3& point:encoded)
        {
            if (SamePoint(point,previous))
            {
                ++result.collapsedVertexCount;
                continue;
            }
            result.vertices.push_back(point);
            previous = point;
        }
        if (result.vertices.size()<3)
        {
            result.reason = Reason::CollapsedVertices;
            return result;
        }

        // CalcNormal (110C0970) uses an anchored triangle fan. Its x87 code
        // stores the previous edge's X/Y, cross product X/Y, and each normal
        // accumulator component as floats. Native UI-thread control word
        // 0x027F selects 53-bit arithmetic for the remaining intermediates.
        const Vec3& first = result.vertices.front();
        Vec3 accumulated;
        for (std::size_t index=2; index<result.vertices.size(); ++index)
        {
            const Vec3& prior = result.vertices[index-1];
            const Vec3& current = result.vertices[index];
            const Vec3 a{FloatStore(prior.x-first.x),FloatStore(prior.y-first.y),
                         prior.z-first.z};
            const Vec3 b{current.x-first.x,current.y-first.y,current.z-first.z};
            const Vec3 cross{FloatStore(a.y*b.z-a.z*b.y),
                             FloatStore(a.z*b.x-a.x*b.z),
                             a.x*b.y-a.y*b.x};
            accumulated = {FloatStore(accumulated.x+cross.x),
                           FloatStore(accumulated.y+cross.y),
                           FloatStore(accumulated.z+cross.z)};
            if (!Finite(accumulated))
            {
                result.reason = Reason::NonFiniteArithmetic;
                return result;
            }
        }
        result.normalSquared = Squared(accumulated);
        // 110C0A5C reads float constant 0x1147547C. Equality is accepted.
        if (result.normalSquared<static_cast<double>(kMinimumNormalSquared))
        {
            result.reason = Reason::NormalTooSmall;
            return result;
        }

        const double inverseLength = 1.0/std::sqrt(result.normalSquared);
        result.normal = {FloatStore(accumulated.x*inverseLength),
                         FloatStore(accumulated.y*inverseLength),
                         FloatStore(accumulated.z*inverseLength)};
        result.reason = Reason::Accepted;
        return result;
    }

    bool PreservesOutline(const std::vector<Vec3>& original,const Result& prepared)
    {
        if (!prepared.accepted() || original.size()<3
            || original.size()>kMaximumVertices || prepared.vertices.size()<3
            || prepared.vertices.size()>original.size())
            return false;
        const double largestFloat = (std::numeric_limits<float>::max)();
        for (const Vec3& point:original)
            if (!Finite(point) || std::fabs(point.x)>largestFloat
                || std::fabs(point.y)>largestFloat || std::fabs(point.z)>largestFloat
                || point.x!=FloatStore(point.x) || point.y!=FloatStore(point.y)
                || point.z!=FloatStore(point.z))
                return false;
        for (const Vec3& point:prepared.vertices)
            if (!Finite(point))
                return false;

        // Native Fix retains a cyclic subsequence. Check every possible first
        // occurrence so repeated exact vertices do not force a false failure.
        for (std::size_t first=0; first<original.size(); ++first)
        {
            if (!Equal(original[first],prepared.vertices.front()))
                continue;
            std::size_t edge=0;
            Vec3 previous=original[first];
            bool matches=true;
            for (std::size_t step=1; step<=original.size(); ++step)
            {
                const Vec3& point=original[(first+step)%original.size()];
                if (edge==prepared.vertices.size())
                {
                    if (!Equal(point,prepared.vertices.front())) matches=false;
                    continue;
                }
                const Vec3& start=prepared.vertices[edge];
                const Vec3& end=prepared.vertices[(edge+1)%prepared.vertices.size()];
                const double before=DominantCoordinate(previous,start,end);
                const double current=DominantCoordinate(point,start,end);
                const bool increasing=DominantCoordinate(end,start,end)
                    >DominantCoordinate(start,start,end);
                if (Equal(start,end) || !OnSegment(point,start,end)
                    || (increasing ? current<before : current>before))
                {
                    matches=false;
                    break;
                }
                previous=point;
                if (Equal(point,end)) ++edge;
            }
            if (matches && edge==prepared.vertices.size())
                return true;
        }
        return false;
    }
}
