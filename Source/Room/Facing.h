#pragma once

// The direction an actor faces: the eight compass directions, independent of which animation it
// plays. Resource files spell them north, north_east, east, south_east, south, south_west, west
// and north_west.

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace schuimm::room {

enum class Facing {
    North,
    NorthEast,
    East,
    SouthEast,
    South,
    SouthWest,
    West,
    NorthWest,
};

inline constexpr std::size_t facingCount = 8;

inline constexpr std::array<Facing, facingCount> allFacings = {
    Facing::North, Facing::NorthEast, Facing::East, Facing::SouthEast,
    Facing::South, Facing::SouthWest, Facing::West, Facing::NorthWest,
};

[[nodiscard]] constexpr std::size_t facingIndex(Facing facing) noexcept
{
    return static_cast<std::size_t>(facing);
}

// The resource-file name ("north_east").
[[nodiscard]] std::string_view facingName(Facing facing) noexcept;
// nullopt for anything but the eight names.
[[nodiscard]] std::optional<Facing> parseFacing(std::string_view name) noexcept;

} // namespace schuimm::room
