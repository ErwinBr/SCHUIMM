#include "Core/Common/GeometryPredicates.h"

#include <algorithm>
#include <cmath>

namespace schuimm {

double orientation(Vec2 a, Vec2 b, Vec2 c) noexcept
{
    const double l = distance(a, b);
    if (!(l > 0.0)) return 0.0;
    return cross(b - a, c - a) / l;
}

SegmentPoint closestPointOnSegment(Vec2 p, Vec2 a, Vec2 b) noexcept
{
    const double l2 = distanceSquared(a, b);
    if (!(l2 > 0.0)) return {a, 0.0};
    const double t = std::clamp(dot(p - a, b - a) / l2, 0.0, 1.0);
    if (t <= 0.0) return {a, 0.0};
    if (t >= 1.0) return {b, 1.0};
    return {lerp(a, b, t), t};
}

double distanceToSegment(Vec2 p, Vec2 a, Vec2 b) noexcept
{
    // Distance to the exact (double) closest point, not to its float rounding.
    const double l2 = distanceSquared(a, b);
    double t = l2 > 0.0 ? std::clamp(dot(p - a, b - a) / l2, 0.0, 1.0) : 0.0;
    const double x = static_cast<double>(a.x) + (static_cast<double>(b.x) - static_cast<double>(a.x)) * t;
    const double y = static_cast<double>(a.y) + (static_cast<double>(b.y) - static_cast<double>(a.y)) * t;
    return std::hypot(static_cast<double>(p.x) - x, static_cast<double>(p.y) - y);
}

bool pointOnSegment(Vec2 p, Vec2 a, Vec2 b, double tolerance) noexcept
{
    return distanceToSegment(p, a, b) <= tolerance;
}

namespace {

double parameterOn(Vec2 p, Vec2 a, Vec2 b) noexcept
{
    const double l2 = distanceSquared(a, b);
    return l2 > 0.0 ? dot(p - a, b - a) / l2 : 0.0;
}

} // namespace

SegmentIntersection intersectSegments(Vec2 a, Vec2 b, Vec2 c, Vec2 d, double tolerance) noexcept
{
    using Kind = SegmentIntersection::Kind;
    const double lengthAB = distance(a, b);

    // Collinear (both ends of cd on the line through ab): an overlap, a touch or nothing.
    if (lengthAB > tolerance && std::fabs(orientation(a, b, c)) <= tolerance &&
        std::fabs(orientation(a, b, d)) <= tolerance) {
        const double tc = parameterOn(c, a, b);
        const double td = parameterOn(d, a, b);
        const double from = std::max(0.0, std::min(tc, td));
        const double to = std::min(1.0, std::max(tc, td));
        const double slack = tolerance / lengthAB;
        if (to - from > slack) return {Kind::Overlap, lerp(a, b, from), from, to};
        if (to - from >= -slack) {
            const double t = std::clamp((from + to) / 2.0, 0.0, 1.0);
            return {Kind::Point, lerp(a, b, t), t, t};
        }
        return {};
    }

    // A proper crossing: each segment's ends strictly on opposite sides of the other's line.
    const double o1 = orientation(a, b, c);
    const double o2 = orientation(a, b, d);
    const double o3 = orientation(c, d, a);
    const double o4 = orientation(c, d, b);
    if (((o1 > tolerance && o2 < -tolerance) || (o1 < -tolerance && o2 > tolerance)) &&
        ((o3 > tolerance && o4 < -tolerance) || (o3 < -tolerance && o4 > tolerance))) {
        const double t = std::clamp(cross(c - a, d - c) / cross(b - a, d - c), 0.0, 1.0);
        return {Kind::Point, lerp(a, b, t), t, t};
    }

    // A touch: some endpoint within tolerance of the other segment (the closest such endpoint).
    struct Candidate {
        double distance;
        Vec2 point;
    };
    const Candidate candidates[] = {
        {distanceToSegment(c, a, b), c},
        {distanceToSegment(d, a, b), d},
        {distanceToSegment(a, c, d), a},
        {distanceToSegment(b, c, d), b},
    };
    const Candidate* best = nullptr;
    for (const Candidate& candidate : candidates) {
        if (candidate.distance <= tolerance && (best == nullptr || candidate.distance < best->distance)) best = &candidate;
    }
    if (best == nullptr) return {};
    const double t = std::clamp(parameterOn(best->point, a, b), 0.0, 1.0);
    return {Kind::Point, lerp(a, b, t), t, t};
}

PointLocation locatePoint(Vec2 p, std::span<const Vec2> polygon, double tolerance) noexcept
{
    const std::size_t n = polygon.size();
    if (n < 3) return PointLocation::Outside;
    for (std::size_t i = 0; i < n; ++i) {
        if (distanceToSegment(p, polygon[i], polygon[(i + 1) % n]) <= tolerance) return PointLocation::Boundary;
    }
    // Crossing rule with half-open edges (y in [min, max)), so a ray through a vertex counts once.
    bool inside = false;
    const double px = static_cast<double>(p.x);
    const double py = static_cast<double>(p.y);
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double yi = static_cast<double>(polygon[i].y);
        const double yj = static_cast<double>(polygon[j].y);
        if ((yi > py) != (yj > py)) {
            const double xi = static_cast<double>(polygon[i].x);
            const double xj = static_cast<double>(polygon[j].x);
            const double x = xi + (py - yi) * (xj - xi) / (yj - yi);
            if (px < x) inside = !inside;
        }
    }
    return inside ? PointLocation::Inside : PointLocation::Outside;
}

double signedArea(std::span<const Vec2> polygon) noexcept
{
    double twice = 0.0;
    const std::size_t n = polygon.size();
    for (std::size_t i = 0; i < n; ++i) twice += cross(polygon[i], polygon[(i + 1) % n]);
    return twice / 2.0;
}

} // namespace schuimm
