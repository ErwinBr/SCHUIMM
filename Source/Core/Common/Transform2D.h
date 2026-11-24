#pragma once

#include "Core/Common/Geometry.h"

#include <cmath>
#include <optional>

namespace schuimm {

// 2D affine transformation (2x3 matrix), mapping a point p to
//
//   | a  c  tx |   | p.x |
//   | b  d  ty | * | p.y |
//                  |  1  |
//
// The engine's 2D coordinate systems have y pointing down, so a positive rotation angle turns
// clockwise on screen. Composition reads right to left, as with matrices:
// (outer * inner).apply(p) == outer.apply(inner.apply(p)).
struct Transform2D {
    float a = 1.0f;
    float b = 0.0f;
    float c = 0.0f;
    float d = 1.0f;
    float tx = 0.0f;
    float ty = 0.0f;

    [[nodiscard]] static constexpr Transform2D identity() noexcept { return {}; }
    [[nodiscard]] static constexpr Transform2D translation(Vec2 offset) noexcept
    {
        return {1.0f, 0.0f, 0.0f, 1.0f, offset.x, offset.y};
    }
    [[nodiscard]] static constexpr Transform2D scaling(float sx, float sy) noexcept
    {
        return {sx, 0.0f, 0.0f, sy, 0.0f, 0.0f};
    }
    [[nodiscard]] static Transform2D rotation(float radians) noexcept
    {
        const float cs = std::cos(radians);
        const float sn = std::sin(radians);
        return {cs, sn, -sn, cs, 0.0f, 0.0f};
    }

    // Transforms a point (translation applies).
    [[nodiscard]] constexpr Vec2 apply(Vec2 p) const noexcept { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    // Transforms a direction or size (translation does not apply).
    [[nodiscard]] constexpr Vec2 applyVector(Vec2 v) const noexcept { return {a * v.x + c * v.y, b * v.x + d * v.y}; }

    [[nodiscard]] constexpr float determinant() const noexcept { return a * d - b * c; }

    // The inverse transformation, or nullopt if the matrix is singular (e.g. zero scale).
    [[nodiscard]] std::optional<Transform2D> inverse() const noexcept
    {
        const float det = determinant();
        if (det == 0.0f || !std::isfinite(det)) {
            return std::nullopt;
        }
        const float inv = 1.0f / det;
        const float ia = d * inv;
        const float ib = -b * inv;
        const float ic = -c * inv;
        const float id = a * inv;
        return Transform2D{ia, ib, ic, id, -(ia * tx + ic * ty), -(ib * tx + id * ty)};
    }

    friend constexpr Transform2D operator*(const Transform2D& outer, const Transform2D& inner) noexcept
    {
        return {outer.a * inner.a + outer.c * inner.b,
                outer.b * inner.a + outer.d * inner.b,
                outer.a * inner.c + outer.c * inner.d,
                outer.b * inner.c + outer.d * inner.d,
                outer.a * inner.tx + outer.c * inner.ty + outer.tx,
                outer.b * inner.tx + outer.d * inner.ty + outer.ty};
    }

    friend constexpr bool operator==(const Transform2D&, const Transform2D&) noexcept = default;
};

[[nodiscard]] inline bool approxEqual(const Transform2D& x, const Transform2D& y, float tolerance = 1e-5f) noexcept
{
    return std::fabs(x.a - y.a) <= tolerance && std::fabs(x.b - y.b) <= tolerance && std::fabs(x.c - y.c) <= tolerance &&
           std::fabs(x.d - y.d) <= tolerance && std::fabs(x.tx - y.tx) <= tolerance && std::fabs(x.ty - y.ty) <= tolerance;
}

} // namespace schuimm
