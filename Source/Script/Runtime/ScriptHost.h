#pragma once

#include "Core/Common/Error.h"
#include "Core/Logging/Logger.h"
#include "Script/Runtime/NativeRegistry.h"
#include "Script/Runtime/Program.h"
#include "Script/Runtime/ScriptScheduler.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::script {

struct ScriptHostSettings {
    SchedulerSettings scheduler;
    bool logPrintOutput = true; // print() output also goes to the log (category "Script").
};

// The engine's scripting service: native registry (with the standard natives), program
// loading (read + validate + link of precompiled bytecode; no compiler involved) and the
// coroutine scheduler, with diagnostics through the engine Logger.
//
// The Application updates it once per frame right after ApplicationListener::onUpdate with the
// frame delta (see Lifecycle.md), so waits use game time and stop while the application is
// suspended. Ended coroutines are reported (log + event callback) and released after each
// update, so handles of ended coroutines become invalid then.
//
// Destroy the host before anything that registered natives capture.
class ScriptHost {
public:
    using OutputCallback = std::function<void(std::string_view program, std::string_view text)>;

    explicit ScriptHost(Logger& logger, ScriptHostSettings settings = {});
    ~ScriptHost();

    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // Register game natives here before loading the programs that use them.
    [[nodiscard]] NativeRegistry& natives() noexcept { return natives_; }

    [[nodiscard]] Result<std::shared_ptr<const Program>> load(std::span<const std::byte> bytecode, std::string name);

    [[nodiscard]] Result<CoroutineHandle> start(std::shared_ptr<const Program> program, std::string_view function = "main",
                                                std::vector<Value> arguments = {},
                                                ExecutionOwner owner = noExecutionOwner);
    // Starts a global function of a game session's program with its global state.
    [[nodiscard]] Result<CoroutineHandle> start(std::shared_ptr<GlobalState> globals, std::string_view function,
                                                std::vector<Value> arguments = {},
                                                ExecutionOwner owner = noExecutionOwner);
    // Invokes a handler of a behaviour instance (ScriptScheduler::dispatch): the way a game
    // dispatches, with variables that persist between events.
    [[nodiscard]] Result<DispatchResult> dispatch(std::shared_ptr<BehaviourInstance> instance, std::string_view event,
                                                  std::vector<Value> arguments = {},
                                                  ExecutionOwner owner = noExecutionOwner);
    // Invokes an object handler (see ScriptScheduler::dispatch). This is the integration point
    // for the adventure runtime: `object` is the object's identifier in the room resource,
    // `event` an arbitrary event name, `self` the engine's handle of that object. A missing
    // handler is {handled = false} and is not logged as an error.
    [[nodiscard]] Result<DispatchResult> dispatch(std::shared_ptr<const Program> program, std::string_view object,
                                                  std::string_view event, ObjectRef self,
                                                  std::vector<Value> arguments = {},
                                                  ExecutionOwner owner = noExecutionOwner);
    bool cancel(CoroutineHandle handle) { return scheduler_.cancel(handle); }
    // Execution owners (ScriptScheduler::createOwner / cancelOwner): e.g. a room instance owns
    // the coroutines it starts and those they spawn, and cancels them when it is destroyed.
    [[nodiscard]] ExecutionOwner createOwner() noexcept { return scheduler_.createOwner(); }
    std::size_t cancelOwner(ExecutionOwner owner) { return scheduler_.cancelOwner(owner); }
    [[nodiscard]] std::optional<CoroutineState> state(CoroutineHandle handle) const { return scheduler_.state(handle); }

    // One scheduler step; never blocks.
    void update(double deltaSeconds);
    // Cancels every coroutine.
    void stopAll();

    // Receives print() output (in addition to the log).
    void setOutputCallback(OutputCallback callback) { output_ = std::move(callback); }
    // Receives completion, failure, cancellation and budget warnings, after they are logged.
    void setEventCallback(ScriptScheduler::EventCallback callback) { events_ = std::move(callback); }

    [[nodiscard]] ScriptScheduler& scheduler() noexcept { return scheduler_; }
    [[nodiscard]] std::size_t activeCount() const noexcept { return scheduler_.activeCount(); }
    [[nodiscard]] std::size_t activeCount(ExecutionOwner owner) const noexcept { return scheduler_.activeCount(owner); }
    [[nodiscard]] bool isUpdating() const noexcept { return scheduler_.isUpdating(); }

    struct Statistics {
        std::uint64_t started = 0;
        std::uint64_t finished = 0;
        std::uint64_t failed = 0;
        std::uint64_t cancelled = 0;
    };
    [[nodiscard]] const Statistics& statistics() const noexcept { return statistics_; }

private:
    void onEvent(const CoroutineEvent& event);

    Logger& logger_;
    ScriptHostSettings settings_;
    NativeRegistry natives_;
    ScriptScheduler scheduler_;
    OutputCallback output_;
    ScriptScheduler::EventCallback events_;
    Statistics statistics_;
};

} // namespace schuimm::script
