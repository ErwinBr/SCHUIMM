#include "Core/Common/GeometryPredicates.h"

#include <algorithm>
#include <cmath>

namespace schuimm {

namespace {

// A point or vector in double: every difference is taken here, never as a float Vec2.
struct D {
    double x;
    double y;
};

constexpr D toD(Vec2 v) noexcept
{
    return {static_cast<double>(v.x), static_cast<double>(v.y)};
}
constexpr D sub(D a, D b) noexcept
{
    return {a.x - b.x, a.y - b.y};
}
constexpr double dotD(D a, D b) noexcept
{
    return a.x * b.x + a.y * b.y;
}
constexpr double crossD(D a, D b) noexcept
{
    return a.x * b.y - a.y * b.x;
}
inline double lengthD(D v) noexcept
{
    return std::hypot(v.x, v.y);
}
inline Vec2 toVec(D v) noexcept
{
    return {static_cast<float>(v.x), static_cast<float>(v.y)};
}
inline D at(D a, D b, double t) noexcept
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

double orientationD(D a, D b, D c) noexcept
{
    const D ab = sub(b, a);
    const double l = lengthD(ab);
    if (!(l > 0.0)) return 0.0;
    return crossD(ab, sub(c, a)) / l;
}

// Parameter of the point of segment ab closest to p, in [0, 1].
double closestParameter(D p, D a, D b) noexcept
{
    const D ab = sub(b, a);
    const double l2 = dotD(ab, ab);
    if (!(l2 > 0.0)) return 0.0;
    return std::clamp(dotD(sub(p, a), ab) / l2, 0.0, 1.0);
}

double distanceToSegmentD(D p, D a, D b) noexcept
{
    return lengthD(sub(p, at(a, b, closestParameter(p, a, b))));
}

double parameterOn(D p, D a, D b) noexcept
{
    const D ab = sub(b, a);
    const double l2 = dotD(ab, ab);
    return l2 > 0.0 ? dotD(sub(p, a), ab) / l2 : 0.0;
}

} // namespace

double orientation(Vec2 a, Vec2 b, Vec2 c) noexcept
{
    return orientationD(toD(a), toD(b), toD(c));
}

SegmentPoint closestPointOnSegment(Vec2 p, Vec2 a, Vec2 b) noexcept
{
    const double t = closestParameter(toD(p), toD(a), toD(b));
    if (t <= 0.0) return {a, 0.0};
    if (t >= 1.0) return {b, 1.0};
    return {toVec(at(toD(a), toD(b), t)), t};
}

double distanceToSegment(Vec2 p, Vec2 a, Vec2 b) noexcept
{
    return distanceToSegmentD(toD(p), toD(a), toD(b));
}

bool pointOnSegment(Vec2 p, Vec2 a, Vec2 b, double tolerance) noexcept
{
    return distanceToSegment(p, a, b) <= tolerance;
}

SegmentIntersection intersectSegments(Vec2 a, Vec2 b, Vec2 c, Vec2 d, double tolerance) noexcept
{
    using Kind = SegmentIntersection::Kind;
    const D A = toD(a);
    const D B = toD(b);
    const D C = toD(c);
    const D E = toD(d);
    const double lengthAB = lengthD(sub(B, A));
    const auto point = [&](double t) { return toVec(at(A, B, t)); };

    // Collinear (both ends of cd on the line through ab): an overlap, a touch or nothing.
    if (lengthAB > tolerance && std::fabs(orientationD(A, B, C)) <= tolerance &&
        std::fabs(orientationD(A, B, E)) <= tolerance) {
        const double tc = parameterOn(C, A, B);
        const double td = parameterOn(E, A, B);
        const double from = std::max(0.0, std::min(tc, td));
        const double to = std::min(1.0, std::max(tc, td));
        const double slack = tolerance / lengthAB;
        if (to - from > slack) return {Kind::Overlap, point(from), from, to};
        if (to - from >= -slack) {
            const double t = std::clamp((from + to) / 2.0, 0.0, 1.0);
            return {Kind::Point, point(t), t, t};
        }
        return {};
    }

    // A proper crossing: each segment's ends strictly on opposite sides of the other's line.
    const double o1 = orientationD(A, B, C);
    const double o2 = orientationD(A, B, E);
    const double o3 = orientationD(C, E, A);
    const double o4 = orientationD(C, E, B);
    if (((o1 > tolerance && o2 < -tolerance) || (o1 < -tolerance && o2 > tolerance)) &&
        ((o3 > tolerance && o4 < -tolerance) || (o3 < -tolerance && o4 > tolerance))) {
        const double t = std::clamp(crossD(sub(C, A), sub(E, C)) / crossD(sub(B, A), sub(E, C)), 0.0, 1.0);
        return {Kind::Point, point(t), t, t};
    }

    // A touch: some endpoint within tolerance of the other segment (the closest such endpoint).
    struct Candidate {
        double distance;
        D point;
    };
    const Candidate candidates[] = {
        {distanceToSegmentD(C, A, B), C},
        {distanceToSegmentD(E, A, B), E},
        {distanceToSegmentD(A, C, E), A},
        {distanceToSegmentD(B, C, E), B},
    };
    const Candidate* best = nullptr;
    for (const Candidate& candidate : candidates) {
        if (candidate.distance <= tolerance && (best == nullptr || candidate.distance < best->distance)) best = &candidate;
    }
    if (best == nullptr) return {};
    const double t = std::clamp(parameterOn(best->point, A, B), 0.0, 1.0);
    return {Kind::Point, point(t), t, t};
}

PointLocation locatePoint(Vec2 p, std::span<const Vec2> polygon, double tolerance) noexcept
{
    const std::size_t n = polygon.size();
    if (n < 3) return PointLocation::Outside;
    const D P = toD(p);
    for (std::size_t i = 0; i < n; ++i) {
        if (distanceToSegmentD(P, toD(polygon[i]), toD(polygon[(i + 1) % n])) <= tolerance) return PointLocation::Boundary;
    }
    // Crossing rule with half-open edges (y in [min, max)), so a ray through a vertex counts once.
    bool inside = false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const D vi = toD(polygon[i]);
        const D vj = toD(polygon[j]);
        if ((vi.y > P.y) != (vj.y > P.y)) {
            const double x = vi.x + (P.y - vi.y) * (vj.x - vi.x) / (vj.y - vi.y);
            if (P.x < x) inside = !inside;
        }
    }
    return inside ? PointLocation::Inside : PointLocation::Outside;
}

double signedArea(std::span<const Vec2> polygon) noexcept
{
    double twice = 0.0;
    const std::size_t n = polygon.size();
    for (std::size_t i = 0; i < n; ++i) twice += crossD(toD(polygon[i]), toD(polygon[(i + 1) % n]));
    return twice / 2.0;
}

} // namespace schuimm
