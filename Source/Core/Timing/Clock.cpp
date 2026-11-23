#include "Core/Timing/Clock.h"

#include "Core/Common/Assert.h"

#include <algorithm>

namespace schuimm {

std::chrono::nanoseconds steadyNow()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

Clock::Clock(TimeFunction timeFunction, ClockSettings settings)
    : timeFunction_(std::move(timeFunction))
    , settings_(settings)
{
    SCHUIMM_ASSERT(timeFunction_, "Clock requires a time function");
    SCHUIMM_ASSERT(settings_.maximumDeltaSeconds > 0.0, "maximum delta must be positive");
    settings_.statisticsWindow = std::max<std::size_t>(settings_.statisticsWindow, 1);
    samples_.reserve(settings_.statisticsWindow);
    startTime_ = timeFunction_();
}

double Clock::tick()
{
    const std::chrono::nanoseconds now = timeFunction_();
    if (!hasPreviousTick_) {
        hasPreviousTick_ = true;
        previousTick_ = now;
        lastClampedDelta_ = 0.0;
        return 0.0;
    }

    const std::chrono::duration<double> elapsed = now - previousTick_;
    previousTick_ = now;
    // A steady clock never goes backwards, but a custom time function might.
    const double delta = std::max(0.0, elapsed.count());
    recordSample(delta);
    lastClampedDelta_ = std::min(delta, settings_.maximumDeltaSeconds);
    return lastClampedDelta_;
}

void Clock::resetDelta()
{
    hasPreviousTick_ = false;
}

double Clock::secondsSinceStart() const
{
    return std::chrono::duration<double>(timeFunction_() - startTime_).count();
}

void Clock::recordSample(double deltaSeconds)
{
    if (samples_.size() < settings_.statisticsWindow) {
        samples_.push_back(deltaSeconds);
    } else {
        samples_[nextSample_] = deltaSeconds;
    }
    nextSample_ = (nextSample_ + 1) % settings_.statisticsWindow;

    double sum = 0.0;
    double minimum = samples_.front();
    double maximum = samples_.front();
    for (const double sample : samples_) {
        sum += sample;
        minimum = std::min(minimum, sample);
        maximum = std::max(maximum, sample);
    }

    statistics_.frameCount += 1;
    statistics_.lastDeltaSeconds = deltaSeconds;
    statistics_.averageDeltaSeconds = sum / static_cast<double>(samples_.size());
    statistics_.minimumDeltaSeconds = minimum;
    statistics_.maximumDeltaSeconds = maximum;
    statistics_.framesPerSecond = statistics_.averageDeltaSeconds > 0.0 ? 1.0 / statistics_.averageDeltaSeconds : 0.0;
}

} // namespace schuimm
