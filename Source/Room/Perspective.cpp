#include "Room/Perspective.h"

#include <algorithm>

namespace schuimm::room {

float evaluateScaleMap(const ScaleMap& map, float y) noexcept
{
    const auto& points = map.points;
    if (points.empty()) return 1.0f;
    if (!(y > points.front().y)) return points.front().scale; // Also NaN.
    if (!(y < points.back().y)) return points.back().scale;
    // The first point below y: points[i - 1].y <= y < points[i].y.
    const auto next = std::upper_bound(points.begin(), points.end(), y,
                                       [](float value, const ScalePoint& point) { return value < point.y; });
    const ScalePoint& b = *next;
    const ScalePoint& a = *(next - 1);
    const double t = (static_cast<double>(y) - static_cast<double>(a.y)) / (static_cast<double>(b.y) - static_cast<double>(a.y));
    return static_cast<float>(static_cast<double>(a.scale) + (static_cast<double>(b.scale) - static_cast<double>(a.scale)) * t);
}

} // namespace schuimm::room
