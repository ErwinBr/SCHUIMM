#pragma once

namespace schuimm {

// Linear RGBA colour with straight (non-premultiplied) alpha, components in [0, 1].
struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    static constexpr Color white() noexcept { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    static constexpr Color black() noexcept { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    friend constexpr bool operator==(const Color&, const Color&) noexcept = default;
};

} // namespace schuimm
