#pragma once

#include "Core/Common/Error.h"
#include "Script/Runtime/Coroutine.h"
#include "Script/Runtime/Interpreter.h"
#include "Script/Runtime/RuntimeTypes.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace schuimm::script {

// What happens when a coroutine executes more instructions in one update than its budget.
enum class BudgetPolicy : std::uint8_t {
    Fail,    // Default: the coroutine fails with an ExecutionLimit error (script, function, line).
    Preempt, // The coroutine is paused and continues at the next update (a warning is reported once).
};

struct SchedulerSettings {
    std::uint64_t instructionBudget = 100'000; // Per coroutine per update.
    BudgetPolicy budgetPolicy = BudgetPolicy::Fail;
    // Scheduler time advances by deltaSeconds * timeScale per update. With the engine's frame
    // delta this is scaled game time; pass unscaled deltas (or a second scheduler) for UI scripts.
    double timeScale = 1.0;
    CoroutineLimits limits;
};

// Reported once per coroutine when it ends (and once when a Preempt budget warning occurs).
struct CoroutineEvent {
    enum class Kind : std::uint8_t { Finished, Failed, Cancelled, BudgetWarning };
    Kind kind = Kind::Finished;
    CoroutineHandle handle;
    std::string_view program;
    std::string_view function;
    const ScriptError* error = nullptr; // Failed and BudgetWarning.
};

// Result of ScriptScheduler::dispatch.
struct DispatchResult {
    // False: the script declares no handler for this object and event. That is a normal
    // condition (the event simply has no script behaviour), not an error.
    bool handled = false;
    // handled: the coroutine running the handler. It is Ready and runs at the next update (like
    // start()); its outcome is observed as for any coroutine: state(), result(), error() and
    // the event callback (a runtime error in the handler is a Failed coroutine).
    CoroutineHandle handle;
};

// Cooperative scheduler for script coroutines, driven by the game loop (no threads).
//
//   * update(dt) runs, in start order, every coroutine that is Ready or whose wait deadline has
//     been reached, each until it waits, yields, finishes, fails or exhausts its budget. A
//     coroutine runs at most once per update; one started during an update first runs in the
//     next one. update() never blocks.
//   * Time is an int64 nanosecond counter advanced by llround(dt * timeScale * 1e9) per update;
//     a wait deadline is now + llround(seconds * 1e9). Integer time does not accumulate
//     floating-point error. Negative or non-finite deltas count as 0.
//   * A coroutine that waits resumes in the first update whose time has reached the deadline,
//     never in the update in which it started waiting. wait(0) behaves like yield().
//   * Exceptions: natives that throw fail their coroutine (see runCoroutine). An exception from
//     the event or trace callback propagates out of update(), but the scheduler stays usable:
//     the coroutine that was running is marked Failed and update() may be called again.
//   * Finished, failed and cancelled coroutines release their frames immediately but keep their
//     state and error until release()/releaseFinished(), so callers can inspect the outcome.
class ScriptScheduler {
public:
    using EventCallback = std::function<void(const CoroutineEvent&)>;

    explicit ScriptScheduler(SchedulerSettings settings = {});
    ~ScriptScheduler();

    ScriptScheduler(const ScriptScheduler&) = delete;
    ScriptScheduler& operator=(const ScriptScheduler&) = delete;

    // Creates a Ready coroutine running `function(arguments...)` of `program`.
    [[nodiscard]] Result<CoroutineHandle> start(std::shared_ptr<const Program> program, std::string_view function,
                                                std::vector<Value> arguments = {});

    // Invokes the handler `event` of the object declaration `object` ("object blender:" ...
    // "func look_at():") as a new coroutine, with `self` as the handler's object context and
    // `arguments` for its declared parameters. Event names are arbitrary; nothing is hardcoded.
    //   * a null `self` (id 0): InvalidArgument, checked first (a host error, never a no-op);
    //   * no such object or handler: {handled = false}, no coroutine, no error;
    //   * arguments that do not match the handler's parameters: InvalidArgument;
    //   * otherwise {handled = true, handle}.
    [[nodiscard]] Result<DispatchResult> dispatch(std::shared_ptr<const Program> program, std::string_view object,
                                                  std::string_view event, ObjectRef self,
                                                  std::vector<Value> arguments = {});

    // Stops a coroutine that has not ended and releases its frames. Returns false if the handle
    // is unknown or the coroutine has already ended. Not allowed on the running coroutine.
    bool cancel(CoroutineHandle handle);

    void update(double deltaSeconds);

    [[nodiscard]] std::optional<CoroutineState> state(CoroutineHandle handle) const;
    [[nodiscard]] const ScriptError* error(CoroutineHandle handle) const;      // Failed only.
    [[nodiscard]] const Value* result(CoroutineHandle handle) const;           // Finished only.

    // Forgets an ended coroutine (its handle becomes invalid). False if it is still running.
    bool release(CoroutineHandle handle);
    void releaseFinished();
    // Cancels and forgets every coroutine.
    void clear();

    [[nodiscard]] std::size_t activeCount() const noexcept; // Not yet ended.
    [[nodiscard]] std::size_t size() const noexcept;        // Including ended, unreleased ones.
    // True while update() runs (i.e. inside a native or a callback called from it).
    [[nodiscard]] bool isUpdating() const noexcept { return updating_; }
    [[nodiscard]] std::int64_t timeNanoseconds() const noexcept { return now_; }
    [[nodiscard]] std::uint64_t updateCount() const noexcept { return updates_; }
    [[nodiscard]] const SchedulerSettings& settings() const noexcept { return settings_; }

    void setEventCallback(EventCallback callback) { callback_ = std::move(callback); }
    // Debug: called before every instruction of every coroutine (expensive; off by default).
    void setTraceCallback(TraceCallback callback) { trace_ = std::move(callback); }

private:
    struct Slot {
        std::uint32_t generation = 1;
        bool used = false;
        CoroutineState state = CoroutineState::Ready;
        std::unique_ptr<Coroutine> coroutine;
        std::int64_t wakeTime = 0;
        std::uint64_t startedInUpdate = 0;
        std::uint64_t sequence = 0; // Start order.
        bool budgetWarned = false;
        std::optional<ScriptError> error;
        std::string program;
        std::string function;
    };

    // Restores updating_/running_ when update() exits, also by an exception from a callback.
    class UpdateGuard;
    friend class UpdateGuard;

    CoroutineHandle add(std::unique_ptr<Coroutine> coroutine);
    Slot* find(CoroutineHandle handle);
    const Slot* find(CoroutineHandle handle) const;
    void finish(Slot& slot, CoroutineHandle handle, CoroutineState state);
    void emit(CoroutineEvent::Kind kind, const Slot& slot, CoroutineHandle handle);

    SchedulerSettings settings_;
    std::deque<Slot> slots_; // Stable element addresses: natives and callbacks may start coroutines.
    std::vector<std::uint32_t> order_; // Slot indices in start order.
    std::int64_t now_ = 0;
    std::uint64_t updates_ = 0;
    std::uint64_t nextSequence_ = 0;
    bool updating_ = false;
    std::uint32_t running_ = 0xFFFFFFFFu;
    EventCallback callback_;
    TraceCallback trace_;
};

// Converts seconds to scheduler nanoseconds: nullopt for negative, non-finite or absurdly
// large (> 1e9 s) durations.
[[nodiscard]] std::optional<std::int64_t> secondsToNanoseconds(double seconds) noexcept;

} // namespace schuimm::script
