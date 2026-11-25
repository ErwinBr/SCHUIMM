#include "Room/Facing.h"

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

std::optional<Facing> parseFacing(std::string_view name) noexcept
{
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) return allFacings[i];
    }
    return std::nullopt;
}

} // namespace schuimm::room
