#pragma once

#include <cstddef>
#include <limits>
#include <optional>

namespace schuimm {

// Overflow-checked std::size_t arithmetic for sizes computed from caller-supplied values
// (strides, dimensions, byte counts). Each returns std::nullopt instead of wrapping around.
// Written portably: MSVC has no __builtin_*_overflow.

[[nodiscard]] constexpr std::optional<std::size_t> checkedAdd(std::size_t a, std::size_t b) noexcept
{
    if (a > std::numeric_limits<std::size_t>::max() - b) {
        return std::nullopt;
    }
    return a + b;
}

[[nodiscard]] constexpr std::optional<std::size_t> checkedMul(std::size_t a, std::size_t b) noexcept
{
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
        return std::nullopt;
    }
    return a * b;
}

} // namespace schuimm
