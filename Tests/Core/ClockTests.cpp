#include "TestFramework.h"

#include "Core/Timing/Clock.h"

using namespace schuimm;
using namespace std::chrono_literals;

namespace {
// Manually advanced time source.
struct FakeTime {
    std::chrono::nanoseconds now{1s};
    TimeFunction function()
    {
        return [this] { return now; };
    }
};
} // namespace

SCHUIMM_TEST(Clock_FirstTickReturnsZero)
{
    FakeTime time;
    Clock clock(time.function());
    time.now += 50ms;
    CHECK_EQ(clock.tick(), 0.0);
    CHECK_EQ(clock.statistics().frameCount, std::uint64_t{0});
}

SCHUIMM_TEST(Clock_MeasuresDeltaBetweenTicks)
{
    FakeTime time;
    Clock clock(time.function());
    clock.tick();
    time.now += 16'666'667ns;
    CHECK_NEAR(clock.tick(), 0.016666667, 1e-9);
    time.now += 33ms;
    CHECK_NEAR(clock.tick(), 0.033, 1e-9);
    CHECK_NEAR(clock.lastDeltaSeconds(), 0.033, 1e-9);
}

SCHUIMM_TEST(Clock_ClampsLargeDeltas)
{
    FakeTime time;
    Clock clock(time.function(), ClockSettings{.maximumDeltaSeconds = 0.1, .statisticsWindow = 10});
    clock.tick();
    time.now += 5s; // e.g. the window was dragged or the machine slept
    CHECK_NEAR(clock.tick(), 0.1, 1e-12);
    // Statistics keep the real, unclamped measurement.
    CHECK_NEAR(clock.statistics().lastDeltaSeconds, 5.0, 1e-9);
}

SCHUIMM_TEST(Clock_ResetDeltaDiscardsElapsedTime)
{
    FakeTime time;
    Clock clock(time.function());
    clock.tick();
    time.now += 10ms;
    clock.tick();
    time.now += 3s; // suspended
    clock.resetDelta();
    CHECK_EQ(clock.tick(), 0.0);
    time.now += 20ms;
    CHECK_NEAR(clock.tick(), 0.020, 1e-9);
}

SCHUIMM_TEST(Clock_NeverReturnsNegativeDelta)
{
    FakeTime time;
    Clock clock(time.function());
    clock.tick();
    time.now -= 5ms;
    CHECK_EQ(clock.tick(), 0.0);
}

SCHUIMM_TEST(Clock_RollingStatistics)
{
    FakeTime time;
    Clock clock(time.function(), ClockSettings{.maximumDeltaSeconds = 1.0, .statisticsWindow = 3});
    clock.tick();
    for (const auto step : {10ms, 20ms, 30ms}) {
        time.now += step;
        clock.tick();
    }
    const FrameStatistics& stats = clock.statistics();
    CHECK_EQ(stats.frameCount, std::uint64_t{3});
    CHECK_NEAR(stats.averageDeltaSeconds, 0.020, 1e-9);
    CHECK_NEAR(stats.minimumDeltaSeconds, 0.010, 1e-9);
    CHECK_NEAR(stats.maximumDeltaSeconds, 0.030, 1e-9);
    CHECK_NEAR(stats.framesPerSecond, 50.0, 1e-6);

    // The window slides: the 10 ms sample drops out.
    time.now += 40ms;
    clock.tick();
    CHECK_NEAR(clock.statistics().averageDeltaSeconds, 0.030, 1e-9);
    CHECK_NEAR(clock.statistics().minimumDeltaSeconds, 0.020, 1e-9);
    CHECK_NEAR(clock.statistics().maximumDeltaSeconds, 0.040, 1e-9);
}

SCHUIMM_TEST(Clock_SecondsSinceStart)
{
    FakeTime time;
    Clock clock(time.function());
    time.now += 2500ms;
    CHECK_NEAR(clock.secondsSinceStart(), 2.5, 1e-9);
}

SCHUIMM_TEST(Clock_SteadyTimeIsMonotonic)
{
    const auto a = steadyNow();
    const auto b = steadyNow();
    CHECK(b >= a);
}
