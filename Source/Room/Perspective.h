#pragma once

// Room perspective: the scale map (RoomResource::scaleMap) gives a scale for a ground y. It scales
// actors (their effective scale is base scale x perspective scale) and, unless an actor opts out,
// their walking speed.

#include "Room/RoomResource.h"

namespace schuimm::room {

// Piecewise linear between the points, clamped beyond the first and the last (no extrapolation).
// Increasing and decreasing scales are both fine.
[[nodiscard]] float evaluateScaleMap(const ScaleMap& map, float y) noexcept;

// 1 without a map.
[[nodiscard]] inline float perspectiveScaleAt(const ScaleMap* map, float y) noexcept
{
    return map == nullptr ? 1.0f : evaluateScaleMap(*map, y);
}

} // namespace schuimm::room
