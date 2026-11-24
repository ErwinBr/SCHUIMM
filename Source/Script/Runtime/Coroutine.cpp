#include "Script/Runtime/Coroutine.h"

#include "Core/Common/StringBuilder.h"

namespace schuimm::script {

Result<std::unique_ptr<Coroutine>> Coroutine::create(std::shared_ptr<const Program> program, std::string_view function,
                                                     std::vector<Value> arguments, CoroutineLimits limits)
{
    if (!program) {
        return ::schuimm::makeError(ErrorCode::InvalidArgument, "No program");
    }
    const auto index = program->findFunction(function);
    if (!index) {
        return ::schuimm::makeError(ErrorCode::NotFound,
                         concat("Script '", program->name(), "' has no function '", function, "'"));
    }
    const Function& entry = program->module().functions[*index];
    if (arguments.size() != entry.parameters.size()) {
        return ::schuimm::makeError(ErrorCode::InvalidArgument, concat("Function '", function, "' takes ", entry.parameters.size(),
                                                            " arguments, got ", arguments.size()));
    }
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (!arguments[i].matches(entry.parameters[i])) {
            return ::schuimm::makeError(ErrorCode::InvalidArgument, concat("Argument ", i + 1, " of '", function, "' must be ",
                                                                toString(entry.parameters[i]), ", got ",
                                                                toString(arguments[i].type())));
        }
    }
    if (entry.registerCount > limits.maxRegisters) {
        return ::schuimm::makeError(ErrorCode::InvalidArgument, concat("Function '", function, "' needs more registers than allowed"));
    }
    std::unique_ptr<Coroutine> coroutine(new Coroutine());
    coroutine->program_ = std::move(program);
    coroutine->entry_ = std::string(function);
    coroutine->limits_ = limits;
    coroutine->registers_.resize(entry.registerCount);
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        coroutine->registers_[i] = std::move(arguments[i]);
    }
    coroutine->frames_.push_back({*index, 0, 0, noReturnRegister});
    return coroutine;
}

void Coroutine::releaseFrames() noexcept
{
    frames_.clear();
    registers_.clear();
    registers_.shrink_to_fit();
}

ScriptError Coroutine::makeError(ScriptErrorKind kind, std::string message, std::uint32_t innermostIp) const
{
    ScriptError error;
    error.kind = kind;
    error.message = std::move(message);
    error.script = program_->name();
    const Module& module = program_->module();
    for (std::size_t i = frames_.size(); i-- > 0;) {
        const CallFrame& frame = frames_[i];
        const Function& function = module.functions[frame.function];
        const std::uint32_t ip = (i + 1 == frames_.size()) ? innermostIp : (frame.ip > 0 ? frame.ip - 1 : 0);
        StackFrameInfo info;
        info.function = module.string(function.nameIndex);
        if (ip < function.positions.size()) {
            info.position = function.positions[ip];
        }
        error.stack.push_back(std::move(info));
    }
    if (!error.stack.empty()) {
        error.function = error.stack.front().function;
        error.position = error.stack.front().position;
    }
    return error;
}

} // namespace schuimm::script
