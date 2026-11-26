#pragma once

#include "Script/Runtime/Coroutine.h"
#include "Script/Runtime/NativeRegistry.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace schuimm::script {

enum class RunOutcome : std::uint8_t {
    Finished,        // The entry function returned; Coroutine::result() holds its value.
    Waiting,         // wait(): suspended for RunResult::waitNanoseconds.
    Yielded,         // yield(): suspended until the next update.
    Failed,          // Runtime error: RunResult::error.
    BudgetExhausted, // The instruction budget ran out; the coroutine can be resumed as is.
    Awaiting,        // NativeResult::await on a pending token: RunResult::token.
    Cancelled,       // The coroutine was cancelled while it ran (self-cancellation, or an
                     // awaited token that was already cancelled), at a native call boundary.
};

struct RunResult {
    RunOutcome outcome = RunOutcome::Finished;
    std::uint64_t instructions = 0; // Instructions executed in this run.
    std::int64_t waitNanoseconds = 0;
    ScriptError error;              // Failed, and the location for BudgetExhausted.
    CompletionRef token;            // Awaiting.
    bool abandonCancels = false;    // Awaiting.
};

// Called before each instruction when tracing is enabled (debugging only).
struct TraceEvent {
    const Program& program;
    std::uint32_t function;
    std::uint32_t ip;
    const Instruction& instruction;
    std::span<const Value> registers; // The frame's registers.
    std::size_t callDepth;
};
using TraceCallback = std::function<void(const TraceEvent&)>;

// "main:7 (line 4)  AddInt r2, r0, r1    r0=1 r1=2"
[[nodiscard]] std::string formatTrace(const TraceEvent& event);

// Executes a coroutine until it finishes, suspends, fails or has executed `budget`
// instructions. Every instruction counts (including calls and returns), and script calls run
// in this same loop (no native recursion), so no call pattern can exceed the budget. Dispatch is
// a plain switch; register types are checked by each instruction, so malformed bytecode yields
// an InvalidBytecode error rather than undefined behaviour.
[[nodiscard]] RunResult runCoroutine(Coroutine& coroutine, std::uint64_t budget, NativeContext& context,
                                     const TraceCallback* trace = nullptr);

} // namespace schuimm::script
