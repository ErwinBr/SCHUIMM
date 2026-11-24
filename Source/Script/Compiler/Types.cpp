#include "Script/Compiler/Types.h"

namespace schuimm::script {

const char* toString(Type type) noexcept
{
    switch (type) {
    case Type::Error: return "<error>";
    case Type::None: return "None";
    case Type::Bool: return "bool";
    case Type::Int: return "int";
    case Type::Float: return "float";
    case Type::String: return "string";
    case Type::Any: return "any";
    }
    return "?";
}

ScriptType toScriptType(Type type) noexcept
{
    switch (type) {
    case Type::Bool: return ScriptType::Bool;
    case Type::Int: return ScriptType::Int;
    case Type::Float: return ScriptType::Float;
    case Type::String: return ScriptType::String;
    case Type::Any: return ScriptType::Any;
    case Type::Error:
    case Type::None: return ScriptType::None;
    }
    return ScriptType::None;
}

Type fromScriptType(ScriptType type) noexcept
{
    switch (type) {
    case ScriptType::None: return Type::None;
    case ScriptType::Bool: return Type::Bool;
    case ScriptType::Int: return Type::Int;
    case ScriptType::Float: return Type::Float;
    case ScriptType::String: return Type::String;
    case ScriptType::Any: return Type::Any;
    }
    return Type::Error;
}

std::optional<Type> typeFromName(std::string_view name) noexcept
{
    if (name == "int") return Type::Int;
    if (name == "float") return Type::Float;
    if (name == "bool") return Type::Bool;
    if (name == "string") return Type::String;
    return std::nullopt;
}

bool isAssignable(Type from, Type to) noexcept
{
    if (from == Type::Error || to == Type::Error) return true;
    if (from == to) return from != Type::None;
    if (from == Type::Int && to == Type::Float) return true;
    if (to == Type::Any) return from != Type::None;
    return false;
}

} // namespace schuimm::script
