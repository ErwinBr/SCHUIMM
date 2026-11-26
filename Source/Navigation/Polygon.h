#pragma once

// Validation of authored polygons (walkable areas, polygon regions). A valid polygon is simple:
//
//   * 3 to maxPolygonVertices vertices, all finite;
//   * no two vertices closer than geometryTolerance: no repeated vertices, no zero-length edges,
//     and the last vertex does not repeat the first (polygons close implicitly);
//   * an area of at least minPolygonArea;
//   * adjacent edges meet only at their shared vertex (they do not backtrack over each other);
//   * non-adjacent edges do not intersect, touch or overlap.
//
// Either winding is accepted, concave polygons are, and so are collinear vertices that do not
// backtrack. Checked on the coordinates as stored (float), pairwise: O(n^2) for n vertices.

#include "Core/Common/Geometry.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace schuimm::navigation {

inline constexpr std::size_t maxPolygonVertices = 128;
// Square room pixels.
inline constexpr double minPolygonArea = 1e-6;

struct PolygonDefect {
    enum class Kind {
        TooFewVertices,    // first: the vertex count
        TooManyVertices,   // first: the vertex count
        NonFinite,         // first: the vertex
        RepeatedVertex,    // first, second: the two vertices (second may be the next one)
        ZeroArea,
        BacktrackingEdges, // first, second: the adjacent edges (edge i runs from vertex i to i + 1)
        IntersectingEdges, // first, second: the edges
    };
    Kind kind;
    std::size_t first = 0;
    std::size_t second = 0;
};

// The first defect found, in the order listed above; nullopt for a valid polygon.
[[nodiscard]] std::optional<PolygonDefect> findPolygonDefect(std::span<const Vec2> polygon);

// "edges 1 (points[1]-points[2]) and 4 (points[4]-points[0]) intersect", for error messages.
// `vertexCount` is the polygon's size (edge i ends at vertex (i + 1) % vertexCount).
[[nodiscard]] std::string describe(const PolygonDefect& defect, std::size_t vertexCount);

} // namespace schuimm::navigation
