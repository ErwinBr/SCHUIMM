#include "Animation/AnimationClock.h"

#include <cmath>

namespace schuimm::animation {

namespace {

constexpr std::uint64_t nanosecondsPerSecond = 1'000'000'000;

} // namespace

std::uint64_t AnimationClock::advance(double seconds) noexcept
{
    if (!std::isfinite(seconds) || seconds <= 0.0) return 0;
    if (seconds > maxAdvanceSeconds) seconds = maxAdvanceSeconds;
    // In units of 1e-9 ticks (1 / (rate * 1e9) seconds), rounded once: exact for whole ticks
    // (2/60 s is 2e9 units, not 1999999980). At most 1e6 * 6e10 + 1e9: fits easily in 64 bits.
    const auto units = static_cast<std::uint64_t>(std::llround(seconds * (1e9 * animationTicksPerSecond)));
    const std::uint64_t scaled = units + remainder_;
    remainder_ = scaled % nanosecondsPerSecond;
    return scaled / nanosecondsPerSecond;
}

} // namespace schuimm::animation
