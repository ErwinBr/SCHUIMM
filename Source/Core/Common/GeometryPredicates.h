#pragma once

// Geometric predicates for room geometry (polygons, navigation, movement), computed in double.
//
// Tolerance policy: one distance, geometryTolerance (1e-3 room pixels), decides whether two points
// are the same and whether a point lies on a segment or line. It is a distance, never a raw
// cross product: orientation() returns the signed distance of a point from a line, so the test
// means the same thing for long and short edges. Room coordinates are floats of at most 65536 in
// a room (float spacing there is 1/256 px), so 1e-3 px separates distinct stored coordinates while
// absorbing the rounding of double computations on them. Areas use their own threshold
// (Navigation/Polygon.h); angles need none (facing sectors are exact).

#include "Core/Common/Geometry.h"

#include <span>

namespace schuimm {

inline constexpr double geometryTolerance = 1e-3;

// Signed distance of c from the line through a and b: positive when c lies to the left of a->b in
// a y-up frame (cross(b - a, c - a) > 0). 0 if a and b coincide.
[[nodiscard]] double orientation(Vec2 a, Vec2 b, Vec2 c) noexcept;

// The point of segment ab closest to p, and its parameter t in [0, 1] (a + (b - a) t).
struct SegmentPoint {
    Vec2 point;
    double t = 0.0;
};
[[nodiscard]] SegmentPoint closestPointOnSegment(Vec2 p, Vec2 a, Vec2 b) noexcept;
[[nodiscard]] double distanceToSegment(Vec2 p, Vec2 a, Vec2 b) noexcept;
[[nodiscard]] bool pointOnSegment(Vec2 p, Vec2 a, Vec2 b, double tolerance = geometryTolerance) noexcept;

// How segments ab and cd meet, within `tolerance`:
//   None     no common point;
//   Point    one common point: a proper crossing or a touch (an endpoint on the other segment);
//            `point` and `t` (its parameter on ab) describe it;
//   Overlap  collinear with a common part longer than the tolerance: [t, tEnd] on ab.
struct SegmentIntersection {
    enum class Kind { None, Point, Overlap };
    Kind kind = Kind::None;
    Vec2 point;
    double t = 0.0;
    double tEnd = 0.0;
};
[[nodiscard]] SegmentIntersection intersectSegments(Vec2 a, Vec2 b, Vec2 c, Vec2 d,
                                                    double tolerance = geometryTolerance) noexcept;

enum class PointLocation { Outside, Boundary, Inside };

// Where p lies relative to a simple polygon (closed implicitly, either winding): Boundary within
// `tolerance` of an edge, otherwise Inside or Outside by the crossing rule.
[[nodiscard]] PointLocation locatePoint(Vec2 p, std::span<const Vec2> polygon,
                                        double tolerance = geometryTolerance) noexcept;

// Shoelace area: positive for counter-clockwise vertices in a y-up frame.
[[nodiscard]] double signedArea(std::span<const Vec2> polygon) noexcept;

} // namespace schuimm
