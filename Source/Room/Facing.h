#pragma once

// The direction an actor faces: the eight compass directions, independent of which animation it
// plays. Resource files spell them north, north_east, east, south_east, south, south_west, west
// and north_west.

#include "Core/Common/Geometry.h"

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

// The facing of a movement direction in room coordinates (y points down, so (0, -1) is north).
// Eight 45-degree sectors centred on the compass directions; a direction exactly on a sector
// boundary (22.5 degrees from a compass direction) belongs to the sector counter-clockwise from it
// (on screen). nullopt for a zero (or non-finite) vector.
[[nodiscard]] std::optional<Facing> facingFromDirection(Vec2 direction) noexcept;

} // namespace schuimm::room
