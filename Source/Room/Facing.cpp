#include "Room/Facing.h"

#include <cmath>

namespace schuimm::room {

namespace {

constexpr std::array<std::string_view, facingCount> names = {
    "north", "north_east", "east", "south_east", "south", "south_west", "west", "north_west",
};

} // namespace

std::string_view facingName(Facing facing) noexcept
{
    const std::size_t index = facingIndex(facing);
    return index < names.size() ? names[index] : std::string_view("unknown");
}

std::optional<Facing> facingFromDirection(Vec2 direction) noexcept
{
    const auto x = static_cast<double>(direction.x);
    const auto y = -static_cast<double>(direction.y); // y up: counter-clockwise angles.
    if (!std::isfinite(x) || !std::isfinite(y) || (x == 0.0 && y == 0.0)) return std::nullopt;
    constexpr double pi = 3.14159265358979323846;
    const double degrees = std::atan2(y, x) * 180.0 / pi; // (-180, 180], 0 = east, 90 = north.
    const auto sector = static_cast<int>(std::floor((degrees + 22.5) / 45.0));
    // Counter-clockwise from east.
    static constexpr std::array<Facing, 8> sectors = {Facing::East, Facing::NorthEast, Facing::North, Facing::NorthWest,
                                                      Facing::West, Facing::SouthWest, Facing::South, Facing::SouthEast};
    return sectors[static_cast<std::size_t>(((sector % 8) + 8) % 8)];
}

std::optional<Facing> parseFacing(std::string_view name) noexcept
{
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) return allFacings[i];
    }
    return std::nullopt;
}

} // namespace schuimm::room
