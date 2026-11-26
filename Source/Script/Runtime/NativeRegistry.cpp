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

namespace {

bool isIdentifier(std::string_view name) noexcept
{
    return !name.empty() && !(name[0] >= '0' && name[0] <= '9') && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
}

std::string typeName(const NativeType& type)
{
    return type.kind == ScriptType::Enum ? type.enumName : std::string(toString(type.kind));
}

} // namespace

std::string formatSignature(const NativeFunction& function)
{
    std::string text = function.name + "(";
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
        text += concat(i == 0 ? "" : ", ", typeName(function.parameters[i]));
    }
    return concat(text, ") -> ", typeName(function.returnType));
}

Status NativeRegistry::add(NativeFunction function)
{
    const auto invalid = [&](const char* reason) {
        return makeError(ErrorCode::InvalidArgument, concat("Cannot register native function '", function.name, "': ", reason));
    };
    if (!isIdentifier(function.name)) {
        return invalid("the name is not a valid identifier");
    }
    if (!function.implementation) {
        return invalid("no implementation");
    }
    if (function.returnType.kind == ScriptType::Any || function.returnType.kind == ScriptType::Entity) {
        return invalid("the return type cannot be 'any' or 'entity'");
    }
    if (function.parameters.size() > 255) {
        return invalid("too many parameters");
    }
    const auto knownEnum = [&](const NativeType& type) {
        return type.kind != ScriptType::Enum ? type.enumName.empty() : enums_.count(type.enumName) != 0;
    };
    for (const NativeType& type : function.parameters) {
        if (type.kind == ScriptType::None) {
            return invalid("a parameter cannot have type None");
        }
        if (!knownEnum(type)) {
            return invalid("a parameter names an unregistered enum");
        }
    }
    if (!knownEnum(function.returnType)) {
        return invalid("the return type names an unregistered enum");
    }
    if (functions_.count(function.name) != 0) {
        return invalid("a function with this name is already registered");
    }
    auto shared = std::make_shared<const NativeFunction>(std::move(function));
    functions_.emplace(shared->name, std::move(shared));
    return {};
}

Status NativeRegistry::bind(const NativeInterface& interface,
                            std::unordered_map<std::string, NativeImplementation> implementations)
{
    const auto invalid = [&](const std::string& reason) {
        return makeError(ErrorCode::InvalidArgument, concat("Cannot bind native interface '", interface.name, "': ", reason));
    };
    if (auto problem = checkInterface(interface); !problem.empty()) {
        return invalid(problem);
    }
    // Check everything first, so a failed bind registers nothing.
    for (const EnumDeclaration& declaration : interface.enums) {
        const auto found = enums_.find(qualifiedEnumName(interface.name, declaration.name));
        if (found != enums_.end() && found->second != declaration.members) {
            return invalid(concat("enum '", found->first, "' is already registered with different members"));
        }
    }
    for (const NativeDeclaration& declaration : interface.functions) {
        const auto implementation = implementations.find(declaration.name);
        if (implementation == implementations.end() || !implementation->second) {
            return invalid(concat("no implementation for ", formatSignature(declaration)));
        }
        if (functions_.count(declaration.name) != 0) {
            return invalid(concat("a function named '", declaration.name, "' is already registered"));
        }
    }
    for (const auto& entry : implementations) {
        if (interface.findFunction(entry.first) == nullptr) {
            return invalid(concat("'", entry.first, "' is not declared by the interface"));
        }
    }
    for (const EnumDeclaration& declaration : interface.enums) {
        enums_.emplace(qualifiedEnumName(interface.name, declaration.name), declaration.members);
    }
    const auto qualify = [&](const NativeType& type) {
        return type.kind == ScriptType::Enum ? NativeType(ScriptType::Enum, qualifiedEnumName(interface.name, type.enumName))
                                             : type;
    };
    for (const NativeDeclaration& declaration : interface.functions) {
        NativeFunction function;
        function.name = declaration.name;
        for (const NativeType& type : declaration.parameters) {
            function.parameters.push_back(qualify(type));
        }
        function.returnType = qualify(declaration.returnType);
        function.implementation = std::move(implementations[declaration.name]);
        function.documentation = declaration.documentation;
        function.category = declaration.category;
        function.interfaceName = interface.name;
        auto shared = std::make_shared<const NativeFunction>(std::move(function));
        functions_.emplace(shared->name, std::move(shared));
    }
    return {};
}

const std::vector<std::string>* NativeRegistry::findEnum(std::string_view qualifiedName) const
{
    const auto found = enums_.find(std::string(qualifiedName));
    return found != enums_.end() ? &found->second : nullptr;
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
