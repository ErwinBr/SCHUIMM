#pragma once

#include "Script/Bytecode/Module.h"

#include <cstdint>
#include <string>
#include <vector>

namespace schuimm::script {

// Identifies a coroutine in a ScriptScheduler. Generation-checked: a handle to a released
// coroutine never refers to a newer one that reuses the slot.
struct CoroutineHandle {
    std::uint32_t index = 0xFFFFFFFFu;
    std::uint32_t generation = 0;

    [[nodiscard]] bool isValid() const noexcept { return index != 0xFFFFFFFFu; }
    friend bool operator==(const CoroutineHandle&, const CoroutineHandle&) = default;
};

// Who a coroutine belongs to, e.g. a room instance: cancelling an owner cancels every coroutine
// it owns (ScriptScheduler::cancelOwner). Coroutines started by spawn inherit the owner of the
// coroutine that spawned them. Opaque to the scheduler; noExecutionOwner (0) is "no owner".
using ExecutionOwner = std::uint64_t;
inline constexpr ExecutionOwner noExecutionOwner = 0;

enum class CoroutineState : std::uint8_t {
    Ready,     // Runs at the next scheduler update.
    Running,   // Executing right now (only observable from natives).
    Waiting,   // Suspended by wait() until its deadline, or by an await until its token resolves.
    Finished,  // The entry function returned.
    Failed,    // A runtime error or the instruction budget ended it; see the error.
    Cancelled, // Stopped by cancel(), cancelOwner(), or a cancelled operation it waited for.
};

[[nodiscard]] const char* toString(CoroutineState state) noexcept;
[[nodiscard]] inline bool isTerminal(CoroutineState state) noexcept
{
    return state == CoroutineState::Finished || state == CoroutineState::Failed || state == CoroutineState::Cancelled;
}

enum class ScriptErrorKind : std::uint8_t {
    Runtime,        // Arithmetic, conversion, call depth, ... (Language.md, "Runtime errors").
    Native,         // A native function reported an error.
    ExecutionLimit, // The per-update instruction budget was exhausted.
    InvalidBytecode,// A register held a value of the wrong type (only possible with hand-made bytecode).
};

[[nodiscard]] const char* toString(ScriptErrorKind kind) noexcept;

struct StackFrameInfo {
    std::string function;
    std::string script; // The function's source file.
    SourcePosition position;
};

// A runtime error with the script location: the source file, the function and line/column of
// the failing instruction, and the call stack (innermost first). A frame of another source file
// than the failing one is shown with its file.
struct ScriptError {
    ScriptErrorKind kind = ScriptErrorKind::Runtime;
    std::string message;
    std::string script;
    std::string function;
    SourcePosition position;
    std::vector<StackFrameInfo> stack;
};

// "scripts/demo.sch:12:5: Runtime error in 'main': Division by zero" plus "  at ..." lines.
[[nodiscard]] std::string describe(const ScriptError& error);

} // namespace schuimm::script
