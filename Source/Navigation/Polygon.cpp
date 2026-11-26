#include "Navigation/Polygon.h"

#include "Core/Common/GeometryPredicates.h"
#include "Core/Common/StringBuilder.h"

#include <cmath>

namespace schuimm::navigation {

std::optional<PolygonDefect> findPolygonDefect(std::span<const Vec2> polygon)
{
    using Kind = PolygonDefect::Kind;
    const std::size_t n = polygon.size();
    if (n < 3) return PolygonDefect{Kind::TooFewVertices, n, 0};
    if (n > maxPolygonVertices) return PolygonDefect{Kind::TooManyVertices, n, 0};
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(polygon[i].x) || !std::isfinite(polygon[i].y)) return PolygonDefect{Kind::NonFinite, i, 0};
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (distance(polygon[i], polygon[j]) <= geometryTolerance) return PolygonDefect{Kind::RepeatedVertex, i, j};
        }
    }
    if (std::fabs(signedArea(polygon)) < minPolygonArea) return PolygonDefect{Kind::ZeroArea, 0, 0};

    const auto start = [&](std::size_t edge) { return polygon[edge]; };
    const auto end = [&](std::size_t edge) { return polygon[(edge + 1) % n]; };
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            const SegmentIntersection meet = intersectSegments(start(i), end(i), start(j), end(j));
            if (meet.kind == SegmentIntersection::Kind::None) continue;
            const bool adjacent = j == i + 1 || (i == 0 && j == n - 1);
            if (!adjacent) return PolygonDefect{Kind::IntersectingEdges, i, j};
            // Adjacent edges share one vertex; anything more is backtracking (or, with an
            // in-between touch, a fold).
            if (meet.kind == SegmentIntersection::Kind::Overlap) return PolygonDefect{Kind::BacktrackingEdges, i, j};
            const Vec2 shared = j == i + 1 ? end(i) : start(i);
            if (distance(meet.point, shared) > geometryTolerance) return PolygonDefect{Kind::BacktrackingEdges, i, j};
        }
    }
    return std::nullopt;
}

std::string describe(const PolygonDefect& defect, std::size_t vertexCount)
{
    using Kind = PolygonDefect::Kind;
    const auto edge = [vertexCount](std::size_t i) {
        return concat(i, " (points[", i, "]-points[", vertexCount == 0 ? 0 : (i + 1) % vertexCount, "])");
    };
    switch (defect.kind) {
    case Kind::TooFewVertices: return concat("a polygon needs at least 3 points, found ", defect.first);
    case Kind::TooManyVertices:
        return concat("a polygon has at most ", maxPolygonVertices, " points, found ", defect.first);
    case Kind::NonFinite: return concat("points[", defect.first, "] is not finite");
    case Kind::RepeatedVertex:
        if (defect.first == 0 && defect.second + 1 == vertexCount) {
            return concat("points[", defect.second,
                          "] repeats the first point (polygons are closed implicitly; do not repeat the first point)");
        }
        return concat("points[", defect.second, "] repeats points[", defect.first, "]");
    case Kind::ZeroArea: return "polygon has zero area (all points lie on one line)";
    case Kind::BacktrackingEdges:
        return concat("edges ", edge(defect.first), " and ", edge(defect.second), " fold back over each other");
    case Kind::IntersectingEdges: return concat("edges ", edge(defect.first), " and ", edge(defect.second), " intersect");
    }
    return "invalid polygon";
}

} // namespace schuimm::navigation
