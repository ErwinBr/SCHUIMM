#pragma once

// Geometric predicates for room geometry (polygons, navigation, movement), computed in double.
//
// All arithmetic, differences of coordinates included, is done in double: the inputs are floats
// (room coordinates), but a float difference would round before any double computation began.
//
// Tolerance policy: one distance, geometryTolerance (1/64 room pixel), decides whether two points
// are the same and whether a point lies on a segment or line. It is a distance, never a raw cross
// product: orientation() returns the signed distance of a point from a line, so the test means the
// same thing for long and short edges. It must cover float rounding: points computed in double
// (edge intersections, closest points, positions along a path) are stored as float, and in a room
// (coordinates up to 65536) neighbouring floats are up to 1/128 px apart, so a stored point can be
// 1/256 px off. 1/64 px (exact in binary) is four times that, and still far below a visible pixel.
// Areas use their own threshold (Navigation/Polygon.h); angles need none (facing sectors are exact).

#include "Core/Common/Geometry.h"

#include <span>

namespace schuimm {

inline constexpr double geometryTolerance = 1.0 / 64.0;

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
