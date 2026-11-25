#pragma once

#include "Core/Common/Error.h"
#include "Script/Runtime/Program.h"
#include "Script/Runtime/RuntimeTypes.h"
#include "Script/Runtime/Value.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace schuimm::script {

inline constexpr std::uint32_t noReturnRegister = 0xFFFFFFFFu;

// One activation of a script function. Registers of all frames live in one stack owned by the
// coroutine; a frame addresses its registers by base index (never by pointer, because the
// stack grows), so a suspended coroutine holds everything it needs to resume.
struct CallFrame {
    std::uint32_t function = 0;
    std::uint32_t ip = 0;   // Next instruction.
    std::uint32_t base = 0; // First register of this frame in the register stack.
    std::uint32_t returnRegister = noReturnRegister; // Absolute index in the caller's frame.
};

struct CoroutineLimits {
    std::uint32_t maxCallDepth = 200;          // Nested script calls (recursion depth).
    std::uint32_t maxRegisters = 1u << 20;     // Total register stack (16 MiB of values).
    std::uint32_t maxStringLength = 16u << 20; // Bytes per string created at run time.
};

// Execution state of one script coroutine: the program it runs (shared ownership keeps the
// bytecode, constants and natives alive), its register stack and call frames. Executed by
// runCoroutine(); scheduled by ScriptScheduler. Destroying it releases all values.
class Coroutine {
public:
    // Prepares a call of `function` with `arguments` (checked against its parameter types).
    // Errors: NotFound (no such function), InvalidArgument (argument count or types).
    [[nodiscard]] static Result<std::unique_ptr<Coroutine>> create(std::shared_ptr<const Program> program,
                                                                   std::string_view function, std::vector<Value> arguments,
                                                                   CoroutineLimits limits = {});

    // Prepares a call of object handler `object.event` for the object `self`, with the declared
    // (visible) `arguments`; "self" becomes the hidden parameter 0. The context is a register of
    // the entry frame, so it lives exactly as long as the coroutine, also while it is suspended.
    // Errors: NotFound (no such handler), InvalidArgument (argument count or types).
    [[nodiscard]] static Result<std::unique_ptr<Coroutine>> createHandler(std::shared_ptr<const Program> program,
                                                                          std::string_view object, std::string_view event,
                                                                          ObjectRef self, std::vector<Value> arguments,
                                                                          CoroutineLimits limits = {});

    [[nodiscard]] const Program& program() const noexcept { return *program_; }
    [[nodiscard]] const std::shared_ptr<const Program>& sharedProgram() const noexcept { return program_; }
    // The entry function's name; "object.event" for handlers.
    [[nodiscard]] const std::string& entryFunction() const noexcept { return entry_; }
    [[nodiscard]] const CoroutineLimits& limits() const noexcept { return limits_; }

    [[nodiscard]] bool hasFrames() const noexcept { return !frames_.empty(); }
    [[nodiscard]] std::size_t callDepth() const noexcept { return frames_.size(); }
    // The return value of the entry function once it has finished.
    [[nodiscard]] const Value& result() const noexcept { return result_; }

    // Releases frames and registers (after completion, failure or cancellation).
    void releaseFrames() noexcept;

    // Builds an error located at the innermost frame's instruction `ip` (callers at their call).
    [[nodiscard]] ScriptError makeError(ScriptErrorKind kind, std::string message, std::uint32_t innermostIp) const;

private:
    friend struct Interpreter;
    Coroutine() = default;

    // `hidden`: leading arguments the caller supplies implicitly (self), not counted in messages.
    [[nodiscard]] static Result<std::unique_ptr<Coroutine>> createAt(std::shared_ptr<const Program> program,
                                                                     std::uint32_t index, std::vector<Value> arguments,
                                                                     std::size_t hidden, CoroutineLimits limits);

    std::shared_ptr<const Program> program_;
    std::string entry_;
    CoroutineLimits limits_;
    std::vector<Value> registers_;
    std::vector<CallFrame> frames_;
    Value result_;
};

} // namespace schuimm::script
