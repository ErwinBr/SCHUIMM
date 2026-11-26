#pragma once

// Basic geometry types and vector operations. Predicates with tolerances (orientation, segment
// intersection, point-in-polygon) are in GeometryPredicates.h.

#include <cmath>

namespace schuimm {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    friend constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
    friend constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
    friend constexpr Vec2 operator*(Vec2 a, float s) noexcept { return {a.x * s, a.y * s}; }
    friend constexpr Vec2 operator/(Vec2 a, float s) noexcept { return {a.x / s, a.y / s}; }
    friend constexpr Vec2 operator-(Vec2 a) noexcept { return {-a.x, -a.y}; }
    constexpr Vec2& operator+=(Vec2 o) noexcept { x += o.x; y += o.y; return *this; }
    friend constexpr bool operator==(Vec2, Vec2) noexcept = default;
};

// Products and lengths are computed in double: room coordinates are floats up to 2^20, and their
// products need more precision than a float has.
[[nodiscard]] constexpr double dot(Vec2 a, Vec2 b) noexcept
{
    return static_cast<double>(a.x) * static_cast<double>(b.x) + static_cast<double>(a.y) * static_cast<double>(b.y);
}
// The z component of the 3D cross product: positive when b is counter-clockwise from a in a
// y-up frame (clockwise on screen, where y points down).
[[nodiscard]] constexpr double cross(Vec2 a, Vec2 b) noexcept
{
    return static_cast<double>(a.x) * static_cast<double>(b.y) - static_cast<double>(a.y) * static_cast<double>(b.x);
}
[[nodiscard]] constexpr double lengthSquared(Vec2 v) noexcept { return dot(v, v); }
[[nodiscard]] inline double length(Vec2 v) noexcept { return std::sqrt(lengthSquared(v)); }
// The difference is taken in double too (b - a as floats would round first).
[[nodiscard]] constexpr double distanceSquared(Vec2 a, Vec2 b) noexcept
{
    const double dx = static_cast<double>(b.x) - static_cast<double>(a.x);
    const double dy = static_cast<double>(b.y) - static_cast<double>(a.y);
    return dx * dx + dy * dy;
}
[[nodiscard]] inline double distance(Vec2 a, Vec2 b) noexcept { return std::sqrt(distanceSquared(a, b)); }
// The unit vector in v's direction; {0, 0} for a zero vector (callers that must distinguish test
// the length first).
[[nodiscard]] inline Vec2 normalized(Vec2 v) noexcept
{
    const double l = length(v);
    if (!(l > 0.0)) return {};
    return {static_cast<float>(static_cast<double>(v.x) / l), static_cast<float>(static_cast<double>(v.y) / l)};
}
// a + (b - a) * t, in double.
[[nodiscard]] constexpr Vec2 lerp(Vec2 a, Vec2 b, double t) noexcept
{
    return {static_cast<float>(static_cast<double>(a.x) + (static_cast<double>(b.x) - static_cast<double>(a.x)) * t),
            static_cast<float>(static_cast<double>(a.y) + (static_cast<double>(b.y) - static_cast<double>(a.y)) * t)};
}

// Integer size, used for window and framebuffer dimensions.
struct Extent2D {
    int width = 0;
    int height = 0;

    [[nodiscard]] constexpr bool isEmpty() const noexcept { return width <= 0 || height <= 0; }
    friend constexpr bool operator==(Extent2D, Extent2D) noexcept = default;
};

struct RectF {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    friend constexpr bool operator==(const RectF&, const RectF&) noexcept = default;
};

// Integer rectangle, e.g. a region of an image in pixels (origin top-left).
struct RectI {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    friend constexpr bool operator==(const RectI&, const RectI&) noexcept = default;
};

} // namespace schuimm
