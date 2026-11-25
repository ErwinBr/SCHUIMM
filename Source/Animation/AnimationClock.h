#pragma once

// Converts engine time (double seconds, as Application and ScriptHost pass it) to whole animation
// ticks at a fixed rate, without losing the fraction of a tick between updates: playback speed
// does not depend on the frame rate, refresh rate, VSync or how often anything is rendered.
//
// Each update's seconds are rounded once, to a billionth of a tick; the remainder below one tick
// carries over exactly. So 60 updates of 1/60 s and 144 updates of 1/144 s both give 60 ticks, an
// update of 2/60 s gives 2, and splitting an interval into several updates gives the same total
// (up to that rounding).

#include <cstdint>

namespace schuimm::animation {

inline constexpr std::uint32_t animationTicksPerSecond = 60;

class AnimationClock {
public:
    // Longest interval one advance() accepts; longer ones count as this much. (Application
    // already clamps frame deltas to 0.25 s.)
    static constexpr double maxAdvanceSeconds = 1e6;

    // Whole ticks elapsed in `seconds`, plus the carried fraction. Negative and non-finite
    // values count as 0.
    [[nodiscard]] std::uint64_t advance(double seconds) noexcept;

    // The carried fraction of a tick, in billionths of a tick.
    [[nodiscard]] std::uint64_t remainder() const noexcept { return remainder_; }
    void reset() noexcept { remainder_ = 0; }

private:
    std::uint64_t remainder_ = 0; // < 1e9
};

} // namespace schuimm::animation
