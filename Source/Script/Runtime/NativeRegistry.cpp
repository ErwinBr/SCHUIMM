#include "Script/Runtime/NativeRegistry.h"

#include "Core/Common/StringBuilder.h"

#include <algorithm>

namespace schuimm::script {

const char* toString(CoroutineState state) noexcept
{
    switch (state) {
    case CoroutineState::Ready: return "Ready";
    case CoroutineState::Running: return "Running";
    case CoroutineState::Waiting: return "Waiting";
    case CoroutineState::Finished: return "Finished";
    case CoroutineState::Failed: return "Failed";
    case CoroutineState::Cancelled: return "Cancelled";
    }
    return "?";
}

const char* toString(ScriptErrorKind kind) noexcept
{
    switch (kind) {
    case ScriptErrorKind::Runtime: return "Runtime error";
    case ScriptErrorKind::Native: return "Native function error";
    case ScriptErrorKind::ExecutionLimit: return "Execution limit exceeded";
    case ScriptErrorKind::InvalidBytecode: return "Invalid bytecode";
    }
    return "Error";
}

std::string describe(const ScriptError& error)
{
    std::string text = concat(error.script.empty() ? "<script>" : error.script);
    if (error.position.line != 0) {
        text += concat(":", error.position.line, ":", error.position.column);
    }
    text += concat(": ", toString(error.kind));
    if (!error.function.empty()) {
        text += concat(" in '", error.function, "'");
    }
    text += concat(": ", error.message);
    for (std::size_t i = 1; i < error.stack.size(); ++i) {
        const StackFrameInfo& frame = error.stack[i];
        text += concat("\n    called from '", frame.function, "' (line ", frame.position.line, ")");
    }
    return text;
}

std::string formatSignature(std::string_view name, const std::vector<ScriptType>& parameters, ScriptType returnType)
{
    std::string text = std::string(name) + "(";
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        text += concat(i == 0 ? "" : ", ", toString(parameters[i]));
    }
    return concat(text, ") -> ", toString(returnType));
}

Status NativeRegistry::add(NativeFunction function)
{
    const auto invalid = [&](const char* reason) {
        return makeError(ErrorCode::InvalidArgument, concat("Cannot register native function '", function.name, "': ", reason));
    };
    const std::string& name = function.name;
    if (name.empty() || !std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        }) || (name[0] >= '0' && name[0] <= '9')) {
        return invalid("the name is not a valid identifier");
    }
    if (!function.implementation) {
        return invalid("no implementation");
    }
    if (function.returnType == ScriptType::Any) {
        return invalid("the return type cannot be 'any'");
    }
    if (function.parameters.size() > 255) {
        return invalid("too many parameters");
    }
    for (const ScriptType type : function.parameters) {
        if (type == ScriptType::None) {
            return invalid("a parameter cannot have type None");
        }
    }
    if (functions_.count(name) != 0) {
        return invalid("a function with this name is already registered");
    }
    auto shared = std::make_shared<const NativeFunction>(std::move(function));
    functions_.emplace(shared->name, std::move(shared));
    return {};
}

std::shared_ptr<const NativeFunction> NativeRegistry::find(std::string_view name) const
{
    const auto found = functions_.find(std::string(name));
    return found != functions_.end() ? found->second : nullptr;
}

std::vector<std::string> NativeRegistry::names() const
{
    std::vector<std::string> result;
    for (const auto& entry : functions_) {
        result.push_back(entry.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace schuimm::script
