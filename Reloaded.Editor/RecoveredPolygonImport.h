#pragma once

#include "RecoveredBspGeometry.h"

#include <cstdint>
#include <map>
#include <tuple>

// The native T3D polygon importer ignores supplied Normal lines and runs
// FPoly::Finalize(1). This models its float conversion, Fix and CalcNormal.
// It does not establish convexity, planarity, or coverage of a brush face.
namespace RecoveredPolygonImport
{
    using Vec3 = RecoveredBspGeometry::Vec3;

    constexpr std::size_t kMaximumVertices = 16;
    constexpr float kCoincidentVertexTolerance = 0.002f;
    constexpr float kMinimumNormalSquared = 0.0001f;

    // Resolve native-coincident float coordinates across neighbouring brushes.
    // Seed with structural corners before resolving material subdivisions, so
    // paint boundaries cannot choose a different corner for shared geometry.
    // Always choose the first matching anchor; proximity is not transitive.
    // Callers must validate polygons after resolving their vertices.
    class VertexPool
    {
    public:
        explicit VertexPool(std::size_t maxPoints=200000) : maxPoints_(maxPoints) {}
        bool Resolve(const Vec3& point,Vec3& canonical,std::string& error);

    private:
        using Cell=std::tuple<std::int64_t,std::int64_t,std::int64_t>;
        std::size_t maxPoints_;
        std::vector<Vec3> anchors_;
        std::map<Cell,std::vector<std::size_t>> cells_;
    };

    enum class Reason
    {
        Accepted,
        TooFewVertices,
        TooManyVertices,
        NonFiniteCoordinate,
        CollapsedVertices,
        NormalTooSmall,
        NonFiniteArithmetic
    };

    struct Result
    {
        Reason reason = Reason::TooFewVertices;
        // Float32 coordinates after the native consecutive-point cleanup.
        // Retained even when that cleanup leaves fewer than three points or
        // normal calculation fails, so callers can inspect lost coverage.
        std::vector<Vec3> vertices;
        Vec3 normal;
        double normalSquared = 0;
        std::size_t collapsedVertexCount = 0;

        bool accepted() const noexcept { return reason == Reason::Accepted; }
    };

    // Input limits/non-finite values are rejected before native cleanup.
    // In particular, we reject >16 vertices instead of reproducing the
    // native parser's silent truncation. An accepted result can still have
    // a changed outline: callers must check coverage before using it.
    Result Prepare(const std::vector<Vec3>& polygon);

    // Conservative check that an accepted result retains the same directed
    // outline. Input must already be exactly float32-representable. Deleted
    // points are allowed only on the exact forward segment between retained
    // vertices; no distance or area tolerance is applied. This checks outline
    // preservation, not whether the input itself is simple, convex or planar.
    bool PreservesOutline(const std::vector<Vec3>& original,const Result& prepared);
}
