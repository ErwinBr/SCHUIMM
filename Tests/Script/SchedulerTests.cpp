#include "ScriptTestSupport.h"

#include "Core/Logging/Logger.h"
#include "Script/Runtime/ScriptHost.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

// A scheduler with the standard natives whose print() output is recorded with the update index.
struct Fixture {
    NativeRegistry natives;
    std::vector<std::string> output;
    ScriptScheduler scheduler;
    std::vector<CoroutineEvent::Kind> events;
    std::vector<ScriptError> errors;

    explicit Fixture(SchedulerSettings settings = {})
        : scheduler(settings)
    {
        REQUIRE(registerStandardNatives(natives, [this](std::string_view, std::string_view text) {
                    output.push_back(std::to_string(scheduler.updateCount()) + ":" + std::string(text));
                }).has_value());
        scheduler.setEventCallback([this](const CoroutineEvent& event) {
            events.push_back(event.kind);
            if (event.error != nullptr) errors.push_back(*event.error);
        });
    }

    std::shared_ptr<const Program> load(const std::string& source)
    {
        const Compiled c = compileSource(source);
        if (!c.result.success) std::fprintf(stderr, "%s", c.diagnostics().c_str());
        REQUIRE(c.result.success);
        auto bytes = writeBytecode(c.result.module);
        REQUIRE(bytes.has_value());
        auto program = Program::load(*bytes, natives);
        REQUIRE(program.has_value());
        return *program;
    }

    CoroutineHandle start(const std::shared_ptr<const Program>& program, const char* function = "main")
    {
        auto handle = scheduler.start(program, function);
        REQUIRE(handle.has_value());
        return *handle;
    }

    std::string joined() const
    {
        std::string text;
        for (const std::string& line : output) text += line + "\n";
        return text;
    }
};

} // namespace

SCHUIMM_TEST(Scheduler_StartAndComplete)
{
    Fixture f;
    const auto handle = f.start(f.load("func main():\n\tprint(\"Start\")\n"));
    CHECK(f.scheduler.state(handle) == std::optional<CoroutineState>(CoroutineState::Ready));
    CHECK(f.output.empty()); // Nothing runs before the first update.
    f.scheduler.update(0.016);
    CHECK_EQ(f.joined(), std::string("1:Start\n"));
    CHECK(f.scheduler.state(handle) == std::optional<CoroutineState>(CoroutineState::Finished));
    CHECK_EQ(f.scheduler.activeCount(), std::size_t{0});
    CHECK(f.events == std::vector<CoroutineEvent::Kind>{CoroutineEvent::Kind::Finished});
    CHECK(f.scheduler.release(handle));
    CHECK(!f.scheduler.state(handle).has_value());
}

SCHUIMM_TEST(Scheduler_YieldResumesAtTheNextUpdate)
{
    Fixture f;
    f.start(f.load("func main():\n\tprint(\"Start\")\n\tyield()\n\tprint(\"Next\")\n\tyield()\n\tyield()\n\tprint(\"Last\")\n"));
    for (int i = 0; i < 6; ++i) f.scheduler.update(0.0);
    CHECK_EQ(f.joined(), std::string("1:Start\n2:Next\n4:Last\n"));
}

SCHUIMM_TEST(Scheduler_WaitUsesSchedulerTime)
{
    Fixture f;
    const auto handle = f.start(f.load("func main():\n\tprint(\"Start\")\n\twait(2.0)\n\tprint(\"Two seconds later\")\n"));
    f.scheduler.update(0.0); // t = 0: prints, starts waiting
    CHECK(f.scheduler.state(handle) == std::optional<CoroutineState>(CoroutineState::Waiting));
    for (int i = 0; i < 3; ++i) f.scheduler.update(0.5); // t = 1.5
    CHECK_EQ(f.output.size(), std::size_t{1});
    f.scheduler.update(0.5); // t = 2.0: the deadline is reached
    CHECK_EQ(f.joined(), std::string("1:Start\n5:Two seconds later\n"));
    CHECK_EQ(f.scheduler.timeNanoseconds(), std::int64_t{2'000'000'000});
}

SCHUIMM_TEST(Scheduler_WaitNeverResumesInTheSameUpdate)
{
    Fixture f;
    f.start(f.load("func main():\n\tprint(\"a\")\n\twait(0.0)\n\tprint(\"b\")\n\twait(0.001)\n\tprint(\"c\")\n"));
    f.scheduler.update(100.0);
    CHECK_EQ(f.joined(), std::string("1:a\n"));
    f.scheduler.update(0.0);
    CHECK_EQ(f.joined(), std::string("1:a\n2:b\n"));
    f.scheduler.update(0.0); // deadline not reached
    f.scheduler.update(0.001);
    CHECK_EQ(f.joined(), std::string("1:a\n2:b\n4:c\n"));
}

SCHUIMM_TEST(Scheduler_IntegerTimeDoesNotDrift)
{
    Fixture f;
    f.start(f.load("func main():\n\twait(10.0)\n\tprint(\"done\")\n"));
    f.scheduler.update(0.0);
    for (int i = 0; i < 999; ++i) f.scheduler.update(0.01); // 9.99 s
    CHECK(f.output.empty());
    f.scheduler.update(0.01); // exactly 10 s in integer nanoseconds
    CHECK_EQ(f.joined(), std::string("1001:done\n"));
    CHECK_EQ(f.scheduler.timeNanoseconds(), std::int64_t{10'000'000'000});
    f.scheduler.update(-5.0); // ignored
    f.scheduler.update(std::numeric_limits<double>::quiet_NaN());
    CHECK_EQ(f.scheduler.timeNanoseconds(), std::int64_t{10'000'000'000});
}

SCHUIMM_TEST(Scheduler_TimeScale)
{
    SchedulerSettings settings;
    settings.timeScale = 2.0;
    Fixture f(settings);
    f.start(f.load("func main():\n\twait(1.0)\n\tprint(\"done\")\n"));
    f.scheduler.update(0.0);
    f.scheduler.update(0.25);
    f.scheduler.update(0.25);
    CHECK_EQ(f.joined(), std::string("3:done\n"));
}

SCHUIMM_TEST(Scheduler_MultipleCoroutinesInterleaveDeterministically)
{
    Fixture f;
    const auto program = f.load("func a():\n\tfor i in range(3):\n\t\tprint(\"a\" + str(i))\n\t\tyield()\n"
                                "func b():\n\tprint(\"b0\")\n\twait(0.25)\n\tprint(\"b1\")\n"
                                "func c():\n\tprint(\"c0\")\n");
    f.start(program, "a");
    f.start(program, "b");
    f.start(program, "c");
    f.start(program, "a"); // The same function twice: independent state.
    CHECK_EQ(f.scheduler.activeCount(), std::size_t{4});
    f.scheduler.update(0.0);
    f.scheduler.update(0.25);
    f.scheduler.update(0.25);
    f.scheduler.update(0.25);
    CHECK_EQ(f.joined(), std::string("1:a0\n1:b0\n1:c0\n1:a0\n2:a1\n2:b1\n2:a1\n3:a2\n3:a2\n"));
    CHECK_EQ(f.scheduler.activeCount(), std::size_t{0});
}

SCHUIMM_TEST(Scheduler_Cancellation)
{
    Fixture f;
    const std::int64_t stringsBefore = StringObject::liveCount();
    {
        const auto program = f.load("func main():\n\tlabel = \"x\" + str(5)\n\twait(10.0)\n\tprint(label)\n");
        const auto handle = f.start(program);
        f.scheduler.update(0.0);
        CHECK(f.scheduler.state(handle) == std::optional<CoroutineState>(CoroutineState::Waiting));
        CHECK(f.scheduler.cancel(handle));
        CHECK(f.scheduler.state(handle) == std::optional<CoroutineState>(CoroutineState::Cancelled));
        CHECK(!f.scheduler.cancel(handle)); // already ended
        f.scheduler.update(20.0);
        CHECK(f.output.empty());
        CHECK(f.events == std::vector<CoroutineEvent::Kind>{CoroutineEvent::Kind::Cancelled});
        f.scheduler.releaseFinished();
        CHECK(!f.scheduler.state(handle).has_value());
        CHECK(!f.scheduler.cancel(handle));
    }
    CHECK_EQ(StringObject::liveCount(), stringsBefore); // Frames, values and the program are released.
}

SCHUIMM_TEST(Scheduler_RuntimeErrorsFailOnlyTheirCoroutine)
{
    Fixture f;
    const auto program = f.load("func bad():\n\tzero = 0\n\tprint(1 // zero)\nfunc good():\n\tyield()\n\tprint(\"still running\")\n");
    const auto bad = f.start(program, "bad");
    const auto good = f.start(program, "good");
    f.scheduler.update(0.0);
    f.scheduler.update(0.0);
    CHECK(f.scheduler.state(bad) == std::optional<CoroutineState>(CoroutineState::Failed));
    REQUIRE(f.scheduler.error(bad) != nullptr);
    CHECK_EQ(f.scheduler.error(bad)->function, std::string("bad"));
    CHECK_EQ(f.scheduler.error(bad)->position.line, std::uint32_t{3});
    CHECK(f.scheduler.state(good) == std::optional<CoroutineState>(CoroutineState::Finished));
    CHECK_EQ(f.joined(), std::string("2:still running\n"));
}

SCHUIMM_TEST(Scheduler_InstructionBudgetStopsRunawayScripts)
{
    SchedulerSettings settings;
    settings.instructionBudget = 1000;
    Fixture f(settings);
    const auto program = f.load("func spin():\n\tx = 0\n\twhile True:\n\t\tx = x + 1\n"
                                "func calls():\n\twhile True:\n\t\thelper()\nfunc helper():\n\tpass\n"
                                "func fine():\n\tprint(\"fine\")\n");
    const auto spin = f.start(program, "spin");
    const auto calls = f.start(program, "calls");
    const auto fine = f.start(program, "fine");
    f.scheduler.update(0.0); // Returns: the game loop is not frozen.
    CHECK(f.scheduler.state(spin) == std::optional<CoroutineState>(CoroutineState::Failed));
    CHECK(f.scheduler.state(calls) == std::optional<CoroutineState>(CoroutineState::Failed));
    CHECK(f.scheduler.state(fine) == std::optional<CoroutineState>(CoroutineState::Finished));
    const ScriptError* error = f.scheduler.error(spin);
    REQUIRE(error != nullptr);
    CHECK(error->kind == ScriptErrorKind::ExecutionLimit);
    CHECK(error->message.find("Instruction budget of 1000") != std::string::npos);
    CHECK_EQ(error->script, std::string("test.sch"));
    CHECK_EQ(error->function, std::string("spin"));
    CHECK(error->position.line >= 3 && error->position.line <= 4);
    // Script calls cannot evade the budget: it counts across frames.
    REQUIRE(f.scheduler.error(calls) != nullptr);
    CHECK(f.scheduler.error(calls)->kind == ScriptErrorKind::ExecutionLimit);
    CHECK(describe(*error).find("test.sch:") == 0);
}

SCHUIMM_TEST(Scheduler_PreemptPolicyContinuesNextUpdate)
{
    SchedulerSettings settings;
    settings.instructionBudget = 500;
    settings.budgetPolicy = BudgetPolicy::Preempt;
    Fixture f(settings);
    const auto program = f.load("func busy():\n\ttotal = 0\n\tfor i in range(1000):\n\t\ttotal = total + i\n\tprint(total)\n"
                                "func other():\n\tfor i in range(3):\n\t\tprint(\"other\")\n\t\tyield()\n");
    const auto busy = f.start(program, "busy");
    f.start(program, "other");
    int updates = 0;
    while (f.scheduler.activeCount() > 0 && updates < 100) {
        f.scheduler.update(0.0);
        ++updates;
    }
    CHECK(f.scheduler.state(busy) == std::optional<CoroutineState>(CoroutineState::Finished));
    CHECK(updates > 3); // Spread over several updates.
    CHECK(f.joined().find(":499500\n") != std::string::npos);
    CHECK(f.joined().find("1:other\n2:other\n3:other\n") != std::string::npos); // Not starved.
    // One warning, no failure.
    CHECK_EQ(std::count(f.events.begin(), f.events.end(), CoroutineEvent::Kind::BudgetWarning), std::ptrdiff_t{1});
    CHECK_EQ(std::count(f.events.begin(), f.events.end(), CoroutineEvent::Kind::Failed), std::ptrdiff_t{0});
}

SCHUIMM_TEST(Scheduler_WaitingCoroutinesCostNothing)
{
    Fixture f;
    f.start(f.load("func main():\n\twait(100.0)\n"));
    std::uint64_t instructions = 0;
    f.scheduler.setTraceCallback([&](const TraceEvent&) { ++instructions; });
    f.scheduler.update(0.0);
    const std::uint64_t first = instructions;
    CHECK(first > 0);
    for (int i = 0; i < 1000; ++i) f.scheduler.update(0.016);
    CHECK_EQ(instructions, first); // Waiting coroutines are not executed (no busy polling).
}

SCHUIMM_TEST(Scheduler_CoroutinesStartedDuringAnUpdateRunNextUpdate)
{
    Fixture f;
    const auto program = f.load("func first():\n\tprint(\"first\")\nfunc second():\n\tprint(\"second\")\n");
    bool started = false;
    f.scheduler.setEventCallback([&](const CoroutineEvent& event) {
        if (event.kind == CoroutineEvent::Kind::Finished && !started) {
            started = true;
            REQUIRE(f.scheduler.start(program, "second").has_value());
        }
    });
    f.start(program, "first");
    f.scheduler.update(0.0);
    CHECK_EQ(f.joined(), std::string("1:first\n"));
    f.scheduler.update(0.0);
    CHECK_EQ(f.joined(), std::string("1:first\n2:second\n"));
}

SCHUIMM_TEST(Scheduler_HandlesAreGenerationChecked)
{
    Fixture f;
    const auto program = f.load("func main():\n\tpass\n");
    const auto old = f.start(program);
    f.scheduler.update(0.0);
    CHECK(f.scheduler.release(old));
    const auto fresh = f.start(program); // reuses the slot
    CHECK_EQ(fresh.index, old.index);
    CHECK(!(fresh == old));
    CHECK(!f.scheduler.state(old).has_value());
    CHECK(f.scheduler.state(fresh).has_value());
    CHECK(!f.scheduler.release(fresh)); // still Ready
    CHECK(!f.scheduler.state(CoroutineHandle{}).has_value());
    CHECK(!f.scheduler.start(program, "missing").has_value());
    CHECK(!f.scheduler.start(nullptr, "main").has_value());
    f.scheduler.clear();
    CHECK_EQ(f.scheduler.size(), std::size_t{0});
}

SCHUIMM_TEST(Scheduler_ScriptHostLogsAndReleases)
{
    Logger logger(LogLevel::Off);
    ScriptHost host(logger);
    std::vector<std::string> output;
    host.setOutputCallback([&](std::string_view program, std::string_view text) {
        output.push_back(std::string(program) + ": " + std::string(text));
    });
    const Compiled c = compileSource("func main():\n\tprint(\"hi\")\n\twait(1.0)\nfunc broken():\n\tzero = 0\n\tprint(1 // zero)\n");
    REQUIRE(c.result.success);
    auto bytes = writeBytecode(c.result.module);
    REQUIRE(bytes.has_value());
    CHECK(!host.load(std::span<const std::byte>(bytes->data(), 10), "short.schb").has_value());
    auto program = host.load(*bytes, "scripts/test.schb");
    REQUIRE(program.has_value());
    REQUIRE(host.start(*program).has_value());
    REQUIRE(host.start(*program, "broken").has_value());
    CHECK(!host.start(*program, "nope").has_value());
    host.update(0.0);
    CHECK(output == std::vector<std::string>({"scripts/test.schb: hi"}));
    CHECK_EQ(host.statistics().failed, std::uint64_t{1});
    CHECK_EQ(host.activeCount(), std::size_t{1});
    CHECK_EQ(host.scheduler().size(), std::size_t{1}); // the failed coroutine was released
    host.update(1.0);
    CHECK_EQ(host.statistics().finished, std::uint64_t{1});
    CHECK_EQ(host.activeCount(), std::size_t{0});
}

SCHUIMM_TEST(Scheduler_RecoversWhenACallbackThrows)
{
    Fixture f;
    const auto program = f.load("func main():\n\tprint(\"x\")\n\tyield()\n\tprint(\"y\")\nfunc other():\n\tyield()\n\tprint(\"other\")\n");

    // 1. The trace callback throws while a coroutine is running.
    const auto traced = f.start(program);
    const auto bystander = f.start(program, "other");
    f.scheduler.setTraceCallback([](const TraceEvent&) { throw std::runtime_error("trace failed"); });
    bool thrown = false;
    try {
        f.scheduler.update(0.0);
    } catch (const std::runtime_error&) {
        thrown = true;
    }
    CHECK(thrown); // Host-code exceptions are not swallowed...
    CHECK(f.scheduler.state(traced) == std::optional<CoroutineState>(CoroutineState::Failed)); // ...nor left Running.
    REQUIRE(f.scheduler.error(traced) != nullptr);
    CHECK(f.scheduler.error(traced)->message.find("Aborted by an exception") == 0);
    f.scheduler.setTraceCallback({});
    f.scheduler.update(0.0); // The scheduler is usable again (no "re-entrant update" abort).
    f.scheduler.update(0.0);
    CHECK(f.scheduler.state(bystander) == std::optional<CoroutineState>(CoroutineState::Finished));

    // 2. The event callback throws after a coroutine finished.
    const auto finishing = f.start(program, "other");
    f.scheduler.setEventCallback([](const CoroutineEvent&) { throw std::logic_error("event failed"); });
    thrown = false;
    for (int i = 0; i < 3 && !thrown; ++i) {
        try {
            f.scheduler.update(0.0);
        } catch (const std::logic_error&) {
            thrown = true;
        }
    }
    CHECK(thrown);
    CHECK(f.scheduler.state(finishing) == std::optional<CoroutineState>(CoroutineState::Finished));
    f.scheduler.setEventCallback({});
    f.scheduler.releaseFinished();
    f.scheduler.clear();
    CHECK_EQ(f.scheduler.size(), std::size_t{0});
}
