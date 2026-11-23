#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace schuimm {

// Monotonic time source. The default uses std::chrono::steady_clock, which maps to
// QueryPerformanceCounter on Windows, mach_absolute_time on macOS and CLOCK_MONOTONIC on Linux.
using TimeFunction = std::function<std::chrono::nanoseconds()>;

[[nodiscard]] std::chrono::nanoseconds steadyNow();

struct ClockSettings {
    // Upper bound for the delta returned by tick(). Protects the simulation after
    // stalls such as window dragging, debugger breaks or system suspension.
    double maximumDeltaSeconds = 0.25;
    // Number of recent frames used for the rolling statistics.
    std::size_t statisticsWindow = 120;
};

struct FrameStatistics {
    std::uint64_t frameCount = 0;     // Number of measured frames (ticks after the first).
    double lastDeltaSeconds = 0.0;    // Unclamped duration of the last frame.
    double averageDeltaSeconds = 0.0; // Over the rolling window.
    double minimumDeltaSeconds = 0.0;
    double maximumDeltaSeconds = 0.0;
    double framesPerSecond = 0.0;     // 1 / averageDeltaSeconds.
};

class Clock {
public:
    explicit Clock(TimeFunction timeFunction = steadyNow, ClockSettings settings = {});

    // Marks the start of a new frame and returns the seconds elapsed since the previous tick,
    // clamped to ClockSettings::maximumDeltaSeconds. The first tick (and the first tick after
    // resetDelta()) returns 0.
    double tick();

    // Discards the time elapsed since the last tick, e.g. after the application was suspended.
    void resetDelta();

    [[nodiscard]] double lastDeltaSeconds() const noexcept { return lastClampedDelta_; }
    [[nodiscard]] double secondsSinceStart() const;
    [[nodiscard]] const FrameStatistics& statistics() const noexcept { return statistics_; }
    [[nodiscard]] const ClockSettings& settings() const noexcept { return settings_; }

private:
    void recordSample(double deltaSeconds);

    TimeFunction timeFunction_;
    ClockSettings settings_;
    std::chrono::nanoseconds startTime_{};
    std::chrono::nanoseconds previousTick_{};
    bool hasPreviousTick_ = false;
    double lastClampedDelta_ = 0.0;
    std::vector<double> samples_; // Ring buffer of unclamped deltas.
    std::size_t nextSample_ = 0;
    FrameStatistics statistics_;
};

} // namespace schuimm
