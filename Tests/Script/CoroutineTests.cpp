// Stage B of Milestone 4.4: spawn, coroutine references, cancellation, execution owners, native
// suspension on completion tokens and wait(operation). Everything here is generic: the test
// natives stand in for gameplay operations (walk_to, play_animation) without any adventure code.

#include "ScriptTestSupport.h"

#include "Script/Runtime/Completion.h"

#include <algorithm>

using namespace schuimm;
using namespace schuimm::script;
using namespace schuimm::script::test;

namespace {

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Test natives:
//   begin_op() -> operation      a new pending token (kept in `tokens`), like play_animation;
//   walk(id: int)                suspends until token `id` resolves, giving it up when the waiter
//                                is cancelled (abandonCancels), like walk_to;
//   finish_op(id: int)           completes token `id` (from script, during an update);
//   next_id() -> int             0, 1, 2, ... (counts its calls);
//   cancel_me()                  cancels the calling coroutine (a native that stops its own
//                                coroutine, e.g. by destroying the room it belongs to);
//   log(text: string)            records "<update>:<text>".
struct Fixture {
    NativeRegistry natives;
    ScriptScheduler scheduler;
    std::vector<std::string> output;
    std::vector<CompletionRef> tokens;
    std::vector<CoroutineEvent::Kind> events;
    std::vector<ScriptError> errors;
    int nextId = 0;
    int abandoned = 0;
    int walkCalls = 0;

    explicit Fixture(SchedulerSettings settings = {})
        : scheduler(settings)
    {
        REQUIRE(registerStandardNatives(natives, [this](std::string_view, std::string_view text) { log(text); }).has_value());
        add("begin_op", {}, ScriptType::Operation, [this](NativeContext&, std::span<const Value>) {
            tokens.push_back(CompletionToken::create());
            return NativeResult::returnValue(Value::fromOperation(tokens.back()));
        });
        add("walk", {ScriptType::Int}, ScriptType::None, [this](NativeContext&, std::span<const Value> arguments) {
            ++walkCalls;
            const CompletionRef& token = tokens.at(static_cast<std::size_t>(arguments[0].asInt()));
            token->setAbandonHandler([this] { ++abandoned; });
            return NativeResult::await(token, true);
        });
        add("finish_op", {ScriptType::Int}, ScriptType::None, [this](NativeContext&, std::span<const Value> arguments) {
            tokens.at(static_cast<std::size_t>(arguments[0].asInt()))->complete();
            return NativeResult::returnValue();
        });
        add("next_id", {}, ScriptType::Int,
            [this](NativeContext&, std::span<const Value>) { return NativeResult::returnValue(Value::fromInt(nextId++)); });
        add("cancel_me", {}, ScriptType::None, [](NativeContext& context, std::span<const Value>) {
            CHECK(context.scheduler->cancel(context.coroutine)); // A request: this coroutine is running.
            return NativeResult::returnValue();
        });
        add("log", {ScriptType::String}, ScriptType::None, [this](NativeContext&, std::span<const Value> arguments) {
            log(arguments[0].asString());
            return NativeResult::returnValue();
        });
        scheduler.setEventCallback([this](const CoroutineEvent& event) {
            events.push_back(event.kind);
            if (event.error != nullptr) errors.push_back(*event.error);
        });
    }

    void add(std::string name, std::vector<NativeType> parameters, NativeType result, NativeImplementation implementation)
    {
        REQUIRE(natives.add({std::move(name), std::move(parameters), std::move(result), std::move(implementation)}).has_value());
    }

    void log(std::string_view text) { output.push_back(std::to_string(scheduler.updateCount()) + ":" + std::string(text)); }

    void newToken() { tokens.push_back(CompletionToken::create()); }

    std::shared_ptr<const Program> load(const std::string& source, bool optimize = true)
    {
        const std::string externs = "extern func begin_op() -> operation\nextern func walk(id: int)\n"
                                    "extern func finish_op(id: int)\nextern func next_id() -> int\nextern func cancel_me()\n"
                                    "extern func log(text: string)\n";
        const Compiled c = compileSource(externs + source, optimize);
        if (!c.result.success) std::fprintf(stderr, "%s", c.diagnostics().c_str());
        REQUIRE(c.result.success);
        auto bytes = writeBytecode(c.result.module);
        REQUIRE(bytes.has_value());
        auto program = Program::load(*bytes, natives);
        if (!program) std::fprintf(stderr, "%s\n", program.error().message.c_str());
        REQUIRE(program.has_value());
        return *program;
    }

    CoroutineHandle start(const std::shared_ptr<const Program>& program, const char* function = "main",
                          ExecutionOwner owner = noExecutionOwner)
    {
        auto handle = scheduler.start(program, function, {}, owner);
        REQUIRE(handle.has_value());
        return *handle;
    }

    void run(int updates, double dt = 0.5)
    {
        for (int i = 0; i < updates; ++i) scheduler.update(i == 0 ? 0.0 : dt);
    }

    std::string joined() const
    {
        std::string text;
        for (const std::string& line : output) text += line + "\n";
        return text;
    }

    [[nodiscard]] int count(CoroutineEvent::Kind kind) const
    {
        return static_cast<int>(std::count(events.begin(), events.end(), kind));
    }
};

} // namespace

// --- spawn ---------------------------------------------------------------------------------------

SCHUIMM_TEST(Coroutine_OrdinaryCallsStaySequential)
{
    Fixture f;
    const auto program = f.load("func step(name: string):\n\tlog(name)\n\twait(1.0)\n\tlog(name + \" done\")\n"
                                "func main():\n\tstep(\"a\")\n\tstep(\"b\")\n\tlog(\"end\")\n");
    f.start(program);
    f.run(6);
    // b starts only after a returned; one coroutine (calls never create coroutines).
    CHECK_EQ(f.joined(), std::string("1:a\n3:a done\n3:b\n5:b done\n5:end\n"));
    CHECK_EQ(f.scheduler.size(), std::size_t{1});
}

SCHUIMM_TEST(Coroutine_SpawnRunsIndependentlyInSchedulerOrder)
{
    Fixture f;
    const auto program = f.load("func worker(name: string, n: int):\n"
                                "\tfor i in range(n):\n\t\tlog(name + str(i))\n\t\tyield()\n"
                                "func main():\n"
                                "\tspawn worker(\"a\", 2)\n"
                                "\tvar b: coroutine = spawn worker(\"b\", 2)\n"
                                "\tlog(\"main\")\n"
                                "\tyield()\n"
                                "\tlog(\"main again\")\n");
    f.start(program);
    f.run(4);
    // The caller continues at once; spawned coroutines first run in the next update, in spawn
    // order, after the coroutines that existed before them.
    CHECK_EQ(f.joined(), std::string("1:main\n2:main again\n2:a0\n2:b0\n3:a1\n3:b1\n"));
    CHECK_EQ(f.count(CoroutineEvent::Kind::Finished), 3);
}

SCHUIMM_TEST(Coroutine_SpawnArgumentsAreEvaluatedOnceInTheCaller)
{
    Fixture f;
    const auto program = f.load("func show(id: int, label: string):\n\tlog(label + str(id))\n\tyield()\n\tlog(label + str(id))\n"
                                "func main():\n\tspawn show(next_id(), \"x\" + \"=\")\n\tspawn show(next_id(), \"y=\")\n");
    f.start(program);
    f.run(4);
    CHECK_EQ(f.nextId, 2); // Each argument once, in the spawning coroutine.
    CHECK_EQ(f.joined(), std::string("2:x=0\n2:y=1\n3:x=0\n3:y=1\n"));
}

SCHUIMM_TEST(Coroutine_SpawnedInvocationsHaveOwnStateAndIdentity)
{
    Fixture f;
    const auto program = f.load("func count(name: string, step: int):\n"
                                "\tvar total = 0\n"
                                "\tfor i in range(3):\n\t\ttotal = total + step\n\t\tyield()\n"
                                "\tlog(name + str(total))\n"
                                "func main():\n"
                                "\tvar a = spawn count(\"a\", 1)\n"
                                "\tvar b = spawn count(\"b\", 10)\n"
                                "\tprint(a)\n\tprint(b)\n");
    f.start(program);
    f.run(6);
    REQUIRE(f.output.size() == 4);
    CHECK(f.output[0] != f.output[1]); // Distinct identities for invocations of one function.
    CHECK(contains(f.output[0], "coroutine#"));
    CHECK_EQ(f.output[2], std::string("5:a3"));
    CHECK_EQ(f.output[3], std::string("5:b30"));
}

SCHUIMM_TEST(Coroutine_SpawningANative)
{
    Fixture f;
    f.newToken();
    const auto program = f.load("func main():\n\tvar w = spawn walk(0)\n\tlog(\"caller continues\")\n");
    f.start(program);
    f.run(2);
    CHECK_EQ(f.joined(), std::string("1:caller continues\n"));
    CHECK_EQ(f.scheduler.activeCount(), std::size_t{1}); // The spawned walk waits.
    CHECK_EQ(f.tokens[0]->waiterCount(), std::size_t{1});
    f.tokens[0]->complete();
    f.run(2);
    CHECK_EQ(f.scheduler.activeCount(), std::size_t{0});
    CHECK_EQ(f.count(CoroutineEvent::Kind::Finished), 2);
    CHECK_EQ(f.walkCalls, 1);
    // The wrapper is internal: it cannot be started by name.
    CHECK(!f.scheduler.start(program, "spawn:walk", {Value::fromInt(0)}).has_value());
}

SCHUIMM_TEST(Coroutine_ParentEndingDoesNotEndChildren)
{
    Fixture f;
    const auto program = f.load("func child(name: string):\n\twait(1.0)\n\tlog(name + \" still runs\")\n"
                                "func returns():\n\tspawn child(\"r\")\n"
                                "func waits():\n\tspawn child(\"w\")\n\twait(100.0)\n");
    f.start(program, "returns");
    const CoroutineHandle parent = f.start(program, "waits");
    f.run(2);
    CHECK(f.scheduler.cancel(parent)); // Cancelling a parent leaves what it spawned alone.
    f.run(4);
    CHECK_EQ(f.joined(), std::string("5:r still runs\n5:w still runs\n"));
}

// --- cancellation --------------------------------------------------------------------------------

SCHUIMM_TEST(Coroutine_CancelFromScript)
{
    Fixture f;
    const auto program = f.load("func worker():\n\twhile True:\n\t\tlog(\"tick\")\n\t\twait(1.0)\n"
                                "func main():\n"
                                "\tvar task = spawn worker()\n"
                                "\twait(1.5)\n"
                                "\tcancel(task)\n"
                                "\tcancel(task)\n" // Harmless: already cancelled.
                                "\tlog(\"cancelled\")\n"
                                "\twait(3.0)\n");
    f.start(program);
    f.run(10);
    // main runs before worker in update 4 (start order): the cancel comes before the second tick.
    CHECK_EQ(f.joined(), std::string("2:tick\n4:cancelled\n"));
    CHECK_EQ(f.count(CoroutineEvent::Kind::Cancelled), 1);
}

SCHUIMM_TEST(Coroutine_CancellingFinishedAndStaleCoroutinesIsHarmless)
{
    Fixture f;
    const auto program = f.load("func quick():\n\tpass\n"
                                "func main():\n\tvar q = spawn quick()\n\twait(2.0)\n\tcancel(q)\n\tlog(\"ok\")\n");
    const CoroutineHandle main = f.start(program);
    f.run(6);
    CHECK_EQ(f.joined(), std::string("5:ok\n"));
    CHECK_EQ(f.count(CoroutineEvent::Kind::Cancelled), 0);
    CHECK(!f.scheduler.cancel(main)); // Ended.
    // A stale reference never reaches the coroutine that reuses its slot.
    f.scheduler.releaseFinished();
    const CoroutineHandle fresh = f.start(program, "quick");
    CHECK_EQ(fresh.index, main.index);
    CHECK(!f.scheduler.cancel(main));
    CHECK(f.scheduler.state(fresh) == CoroutineState::Ready);
    CHECK(!f.scheduler.cancel(CoroutineHandle{})); // The null coroutine.
}

SCHUIMM_TEST(Coroutine_SelfCancellationStopsAtTheNativeBoundary)
{
    Fixture f;
    const auto program = f.load("func inner():\n\tlog(\"inner\")\n\tcancel_me()\n\tlog(\"never after cancel\")\n"
                                "func main():\n\tlog(\"start\")\n\tinner()\n\tlog(\"never in caller\")\n");
    const std::int64_t strings = StringObject::liveCount();
    const CoroutineHandle main = f.start(program);
    f.run(3);
    CHECK_EQ(f.joined(), std::string("1:start\n1:inner\n"));
    CHECK(f.scheduler.state(main) == CoroutineState::Cancelled);
    CHECK_EQ(f.count(CoroutineEvent::Kind::Cancelled), 1);
    f.scheduler.releaseFinished();
    CHECK(StringObject::liveCount() <= strings); // Frames released.
}

SCHUIMM_TEST(Coroutine_CancellingAWaitingCoroutine)
{
    Fixture f;
    f.newToken();
    const auto program = f.load("func main():\n\tlog(\"walking\")\n\twalk(0)\n\tlog(\"never arrives\")\n"
                                "func sleeper():\n\twait(10.0)\n\tlog(\"never wakes\")\n");
    const CoroutineHandle walker = f.start(program);
    const CoroutineHandle sleeper = f.start(program, "sleeper");
    f.run(2);
    REQUIRE(f.scheduler.state(walker) == CoroutineState::Waiting);
    CHECK_EQ(f.tokens[0]->waiterCount(), std::size_t{1});
    CHECK(f.scheduler.cancel(walker));
    CHECK(f.scheduler.cancel(sleeper));
    // The subscription is gone and the operation it started was abandoned (once).
    CHECK_EQ(f.tokens[0]->waiterCount(), std::size_t{0});
    CHECK_EQ(f.abandoned, 1);
    f.tokens[0]->complete(); // Nobody to notify.
    f.run(30);
    CHECK_EQ(f.joined(), std::string("1:walking\n"));
    CHECK_EQ(f.abandoned, 1);
}

SCHUIMM_TEST(Coroutine_ExecutionOwnersCoverSpawnedCoroutines)
{
    Fixture f;
    const auto program = f.load("func child(name: string):\n\twait(1.0)\n\tlog(name)\n"
                                "func main():\n\tspawn child(\"owned child\")\n\twait(10.0)\n"
                                "func other():\n\tspawn child(\"other child\")\n\twait(10.0)\n");
    const ExecutionOwner room = f.scheduler.createOwner();
    const ExecutionOwner second = f.scheduler.createOwner();
    CHECK(room != second);
    const CoroutineHandle main = f.start(program, "main", room);
    f.start(program, "other", second);
    f.run(2); // Both have spawned their child.
    CHECK_EQ(f.scheduler.activeCount(room), std::size_t{2});
    CHECK(f.scheduler.owner(main) == room);
    CHECK_EQ(f.scheduler.cancelOwner(room), std::size_t{2});
    CHECK_EQ(f.scheduler.activeCount(room), std::size_t{0});
    CHECK_EQ(f.scheduler.cancelOwner(noExecutionOwner), std::size_t{0});
    f.run(4);
    CHECK_EQ(f.joined(), std::string("5:other child\n"));
}

SCHUIMM_TEST(Coroutine_OwnerCancellationOfTheRunningCoroutine)
{
    // A native that cancels its own coroutine's owner (e.g. it unloads the room): the running
    // coroutine is stopped at the call boundary, the others at once.
    Fixture f;
    ExecutionOwner owner = noExecutionOwner;
    f.add("unload", {}, ScriptType::None, [&](NativeContext& context, std::span<const Value>) {
        CHECK_EQ(context.scheduler->cancelOwner(owner), std::size_t{2});
        return NativeResult::returnValue();
    });
    const Compiled c = compileSource("extern func unload()\nextern func log(text: string)\n"
                                     "func helper():\n\twait(5.0)\n\tlog(\"helper\")\n"
                                     "func main():\n\tspawn helper()\n\tyield()\n\tunload()\n\tlog(\"never\")\n");
    REQUIRE(c.result.success);
    auto program = Program::link(c.result.module, f.natives);
    REQUIRE(program.has_value());
    owner = f.scheduler.createOwner();
    f.start(*program, "main", owner);
    f.run(10);
    CHECK(f.output.empty());
    CHECK_EQ(f.count(CoroutineEvent::Kind::Cancelled), 2);
}

SCHUIMM_TEST(Coroutine_SpawnedCoroutinesKeepTheInstructionBudget)
{
    SchedulerSettings settings;
    settings.instructionBudget = 1000;
    Fixture f(settings);
    const auto program = f.load("func spin():\n\tvar n = 0\n\twhile True:\n\t\tn = n + 1\n"
                                "func main():\n\tspawn spin()\n\tlog(\"main finished\")\n");
    f.start(program);
    f.run(3);
    CHECK_EQ(f.joined(), std::string("1:main finished\n"));
    REQUIRE(f.errors.size() == 1);
    CHECK(f.errors[0].kind == ScriptErrorKind::ExecutionLimit);
    CHECK_EQ(f.errors[0].function, std::string("spin"));
}

// --- native suspension and completion tokens ------------------------------------------------------

SCHUIMM_TEST(Completion_SuspendedCallResumesAtTheNextInstructionOnce)
{
    Fixture f;
    f.newToken();
    const auto program = f.load("func deep(depth: int, label: string) -> int:\n"
                                "\tif depth > 0:\n\t\treturn deep(depth - 1, label) + 1\n"
                                "\tvar before = label + \"!\"\n"
                                "\twalk(0)\n"
                                "\tlog(before)\n"
                                "\treturn 100\n"
                                "func main():\n\tvar local = 7\n\tvar r = deep(3, \"resumed\")\n\tlog(str(r + local))\n");
    const CoroutineHandle main = f.start(program);
    f.run(5);
    CHECK(f.scheduler.state(main) == CoroutineState::Waiting);
    CHECK(f.output.empty());
    f.tokens[0]->complete();
    CHECK(f.scheduler.state(main) == CoroutineState::Ready); // Eligible; nothing ran yet.
    CHECK(f.output.empty());
    f.scheduler.update(0.5);
    // The nested frames, locals and the pending result destinations survived the suspension.
    CHECK_EQ(f.joined(), std::string("6:resumed!\n6:110\n"));
    CHECK_EQ(f.walkCalls, 1); // The suspended native was not called again.
    CHECK(f.scheduler.state(main) == CoroutineState::Finished);
}

SCHUIMM_TEST(Completion_TokenResolvedBeforeTheWait)
{
    Fixture f;
    f.newToken();
    f.newToken();
    f.tokens[0]->complete();
    f.tokens[1]->cancel("replaced");
    const auto program = f.load("func done():\n\twalk(0)\n\tlog(\"no suspension\")\n"
                                "func gone():\n\twalk(1)\n\tlog(\"never\")\n");
    f.start(program, "done");
    const CoroutineHandle gone = f.start(program, "gone");
    f.scheduler.update(0.0);
    // Completed: the call returns at once (same update). Cancelled: the waiter is cancelled.
    CHECK_EQ(f.joined(), std::string("1:no suspension\n"));
    CHECK(f.scheduler.state(gone) == CoroutineState::Cancelled);
    CHECK_EQ(f.abandoned, 0);
}

SCHUIMM_TEST(Completion_OutcomesAreDeliveredOnceToEveryWaiter)
{
    Fixture f;
    f.newToken();
    const auto program = f.load("func a():\n\twalk(0)\n\tlog(\"a\")\n\twait(10.0)\n"
                                "func b():\n\twalk(0)\n\tlog(\"b\")\n\twait(10.0)\n");
    f.start(program, "a");
    f.start(program, "b");
    f.run(2);
    CHECK_EQ(f.tokens[0]->waiterCount(), std::size_t{2});
    f.tokens[0]->complete();
    f.tokens[0]->complete();     // Resolving twice changes nothing.
    f.tokens[0]->cancel("late"); // Nor does a later cancellation.
    CHECK(f.tokens[0]->state() == CompletionToken::State::Completed);
    f.run(4);
    CHECK_EQ(f.joined(), std::string("3:a\n3:b\n"));
}

SCHUIMM_TEST(Completion_CancelledTokenCancelsItsWaiters)
{
    Fixture f;
    f.newToken();
    const auto program = f.load("func main():\n\twalk(0)\n\tlog(\"never\")\n");
    const CoroutineHandle main = f.start(program);
    f.run(2);
    f.tokens[0]->cancel("stopped");
    CHECK(f.scheduler.state(main) == CoroutineState::Cancelled);
    CHECK_EQ(f.abandoned, 0); // The operation ended by itself: nothing to abandon.
    CHECK_EQ(f.tokens[0]->cancelReason(), std::string("stopped"));
    f.run(2);
    CHECK(f.output.empty());
}

SCHUIMM_TEST(Completion_ResolvingFromAScriptDoesNotRunScriptsRecursively)
{
    Fixture f;
    f.newToken();
    const auto program = f.load("func waiter():\n\twalk(0)\n\tlog(\"waiter resumed\")\n"
                                "func early():\n\tyield()\n\tlog(\"early completes\")\n\tfinish_op(0)\n\tlog(\"early continues\")\n");
    f.start(program, "early"); // Runs before the waiter in every update.
    f.start(program, "waiter");
    f.run(3);
    // finish_op only makes the waiter Ready; it runs later, from the scheduler loop.
    CHECK_EQ(f.joined(), std::string("2:early completes\n2:early continues\n2:waiter resumed\n"));
}

SCHUIMM_TEST(Completion_NotAwaitableOperationsAreErrors)
{
    Fixture f;
    f.newToken();
    f.tokens[0]->setNotAwaitable("a looping animation never completes");
    const auto program = f.load("func main():\n\twalk(0)\n\tlog(\"never\")\n");
    f.start(program);
    f.run(2);
    REQUIRE(f.errors.size() == 1);
    CHECK(f.errors[0].kind == ScriptErrorKind::Native);
    CHECK(contains(f.errors[0].message, "a looping animation never completes"));
    CHECK_EQ(f.tokens[0]->waiterCount(), std::size_t{0});
}

SCHUIMM_TEST(Completion_WaitForAnOperation)
{
    Fixture f;
    const auto program = f.load("func main():\n"
                                "\tvar first = begin_op()\n"
                                "\twait(first)\n"
                                "\tlog(\"first done\")\n"
                                "\twait(begin_op())\n"
                                "\tlog(\"second done\")\n"
                                "\twait(first)\n" // Already completed: no suspension.
                                "\tlog(\"first again\")\n");
    f.start(program);
    f.run(2);
    REQUIRE(f.tokens.size() == 1);
    f.tokens[0]->complete();
    f.run(2);
    REQUIRE(f.tokens.size() == 2);
    CHECK_EQ(f.joined(), std::string("3:first done\n"));
    f.tokens[1]->complete();
    f.run(1);
    CHECK_EQ(f.joined(), std::string("3:first done\n5:second done\n5:first again\n"));
}

SCHUIMM_TEST(Completion_OutcomeIsRetainedWhileAnotherOperationStarts)
{
    // Request A completes, request B begins on the same "entity", then A's waiter resumes: it
    // sees A's completion, because the token is A's, not the entity's current state.
    Fixture f;
    const auto program = f.load("func main():\n\tvar a = begin_op()\n\twait(a)\n\tlog(\"A completed\")\n");
    f.start(program);
    f.run(2);
    f.tokens[0]->complete();
    f.newToken();                   // B begins before the waiter runs...
    f.tokens[1]->cancel("replaced"); // ...and is cancelled.
    f.run(1);
    CHECK_EQ(f.joined(), std::string("3:A completed\n"));
}

SCHUIMM_TEST(Completion_TokensAreFreedWithTheirLastReference)
{
    const std::int64_t tokensBefore = CompletionToken::liveCount();
    const std::int64_t stringsBefore = StringObject::liveCount();
    {
        Fixture f;
        const auto program = f.load("func hold(op: operation):\n\twait(op)\n\tlog(\"held\")\n"
                                    "func main():\n\tvar op = begin_op()\n\tvar copy = op\n\tspawn hold(copy)\n\twait(op)\n"
                                    "\tlog(\"done\")\n"
                                    "func fails():\n\tvar op = begin_op()\n\tvar zero = 0\n\tlog(str(1 // zero))\n");
        f.start(program);
        f.start(program, "fails");
        f.run(3);
        CHECK_EQ(f.tokens.size(), std::size_t{2});
        f.tokens[0]->complete();
        f.run(3);
        CHECK_EQ(f.joined(), std::string("4:done\n4:held\n"));
        CHECK_EQ(f.errors.size(), std::size_t{1}); // The failed coroutine released its operation.
        f.scheduler.releaseFinished();
        CHECK_EQ(f.tokens[0]->referenceCount(), std::uint32_t{1}); // Only the fixture's.
        CHECK_EQ(f.tokens[1]->referenceCount(), std::uint32_t{1});
    }
    CHECK_EQ(CompletionToken::liveCount(), tokensBefore);
    CHECK_EQ(StringObject::liveCount(), stringsBefore);
}

SCHUIMM_TEST(Completion_ATokenMayOutliveItsScheduler)
{
    CompletionRef token;
    {
        Fixture f;
        f.newToken();
        token = f.tokens[0];
        const auto program = f.load("func main():\n\twalk(0)\n");
        f.start(program);
        f.run(2);
        CHECK_EQ(token->waiterCount(), std::size_t{1});
    }
    CHECK_EQ(token->waiterCount(), std::size_t{0}); // Unsubscribed by the scheduler's destructor.
    token->complete();                               // Notifies nobody.
}

// --- language ------------------------------------------------------------------------------------

SCHUIMM_TEST(Coroutine_SpawnAndWaitAreTypeChecked)
{
    const auto errors = [](const std::string& text) { return compileSource(text); };
    CHECK(errors("func f():\n\tspawn 1 + 2\n").has("'spawn' must be followed by a function call"));
    CHECK(errors("func f():\n\tspawn missing()\n").has("Undefined function 'missing'"));
    CHECK(errors("func f():\n\tspawn print(1)\n").has("cannot be spawned"));
    CHECK(errors("func f():\n\tspawn int(1.5)\n").has("built into the language and cannot be spawned"));
    CHECK(errors("func g(n: int):\n\tpass\nfunc f():\n\tspawn g(\"x\")\n").has("must be of type 'int'"));
    CHECK(errors("func g():\n\tpass\nfunc f():\n\tvar c: int = spawn g()\n").has("'coroutine' to variable 'c' of type 'int'"));
    CHECK(errors("func f():\n\twait(\"soon\")\n").has("Argument 1 of 'wait' must be of type 'float'"));
    CHECK(errors("func f(o: operation):\n\tawait_operation(o)\n").has("Undefined function 'await_operation'"));
    CHECK(errors("extern func await_operation(o: operation)\n").has("built-in function"));
    CHECK(errors("func f(c: coroutine) -> bool:\n\treturn c == c\n").has("Operator '=='"));
    CHECK(errors("func f():\n\tcancel(1)\n").has("must be of type 'coroutine'"));
    CHECK(errors("object door:\n\tfunc use():\n\t\tspawn open()\n\tfunc open():\n\t\tpass\n").result.success); // Its own handler.
    CHECK(errors("object door:\n\tfunc open():\n\t\tpass\nfunc f():\n\tspawn open()\n").has("Undefined function 'open'"));
    // spawn as a statement and as a value; a function returning a coroutine.
    CHECK(errors("func g():\n\tpass\nfunc h() -> coroutine:\n\treturn spawn g()\nfunc f():\n\tspawn g()\n\tcancel(h())\n")
              .result.success);
}

SCHUIMM_TEST(Coroutine_SpawnBytecodeIsValidated)
{
    const Compiled c = compileSource("func g(n: int):\n\tpass\nfunc f():\n\tvar t = spawn g(1)\n\tcancel(t)\n"
                                     "object door:\n\tfunc use():\n\t\tpass\n",
                                     false);
    REQUIRE(c.result.success);
    Module m = c.result.module;
    Function& f = m.functions[1];
    std::size_t spawn = f.code.size();
    for (std::size_t i = 0; i < f.code.size(); ++i) {
        if (f.code[i].opcode == Opcode::Spawn) spawn = i;
    }
    REQUIRE(spawn < f.code.size());
    REQUIRE(validateModule(m).has_value());
    Module bad = m;
    bad.functions[1].code[spawn].b = 2; // The handler door.use.
    const Status handler = validateModule(bad);
    REQUIRE(!handler.has_value());
    CHECK(contains(handler.error().message, "spawns handler 'door.use'"));
    bad = m;
    bad.functions[1].registerTypes[bad.functions[1].code[spawn].a] = ScriptType::Int;
    const Status typed = validateModule(bad);
    REQUIRE(!typed.has_value());
    CHECK(contains(typed.error().message, "spawn stores its coroutine in register"));
}

SCHUIMM_TEST(Completion_RecordingAndDeliveringAreSeparate)
{
    Fixture f;
    f.newToken();
    f.newToken();
    const auto program = f.load("func waiter():\n\twalk(0)\n\tlog(\"resumed\")\n"
                                "func late():\n\twalk(1)\n\tlog(\"never\")\n");
    const CoroutineHandle waiter = f.start(program, "waiter");
    f.run(2);
    REQUIRE(f.scheduler.state(waiter) == CoroutineState::Waiting);
    // Recorded: the outcome is fixed and visible, but nobody is notified yet.
    f.tokens[0]->recordCompleted();
    f.tokens[0]->recordCancelled("too late"); // First record wins.
    CHECK(f.tokens[0]->state() == CompletionToken::State::Completed);
    CHECK(!f.tokens[0]->isDelivered());
    f.run(2);
    CHECK(f.scheduler.state(waiter) == CoroutineState::Waiting);
    CHECK(f.output.empty());
    f.tokens[0]->deliver();
    f.tokens[0]->deliver(); // Once.
    CHECK(f.scheduler.state(waiter) == CoroutineState::Ready);
    f.run(1);
    CHECK_EQ(f.joined(), std::string("5:resumed\n"));
    // A new wait on a recorded (undelivered) cancellation is cancelled at once.
    f.tokens[1]->recordCancelled("stopped");
    const CoroutineHandle late = f.start(program, "late");
    f.run(1);
    CHECK(f.scheduler.state(late) == CoroutineState::Cancelled);
    CHECK_EQ(f.tokens[1]->waiterCount(), std::size_t{0});
    // Delivering before anything is recorded does nothing.
    f.newToken();
    f.tokens[2]->deliver();
    CHECK(!f.tokens[2]->isDelivered());
}

SCHUIMM_TEST(Coroutine_ABareSchedulerReclaimsWhatItsOwnerReleases)
{
    // The policy (ScriptScheduler.h): ended records stay until released. Spawning short-lived
    // coroutines therefore grows a bare scheduler unless its owner calls releaseFinished();
    // with it, freed slots are reused.
    const auto spawner = [](Fixture& f) {
        return f.load("func blink():\n\tpass\n"
                      "func main():\n\tfor i in range(2000):\n\t\tspawn blink()\n\t\tif i % 50 == 49:\n\t\t\tyield()\n");
    };
    {
        Fixture f;
        f.start(spawner(f));
        f.run(60);
        CHECK(f.scheduler.size() > std::size_t{2000}); // Every ended record kept.
    }
    {
        Fixture f;
        f.start(spawner(f));
        std::size_t largest = 0;
        for (int i = 0; i < 60; ++i) {
            f.scheduler.update(0.0);
            f.scheduler.releaseFinished();
            largest = std::max(largest, f.scheduler.slotCapacity());
        }
        CHECK_EQ(f.scheduler.size(), std::size_t{0});
        CHECK(largest <= std::size_t{128});
    }
}
