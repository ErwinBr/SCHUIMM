#pragma once

namespace schuimm {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    friend constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
    friend constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
    friend constexpr Vec2 operator*(Vec2 a, float s) noexcept { return {a.x * s, a.y * s}; }
    constexpr Vec2& operator+=(Vec2 o) noexcept { x += o.x; y += o.y; return *this; }
    friend constexpr bool operator==(Vec2, Vec2) noexcept = default;
};

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
