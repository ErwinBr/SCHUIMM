#include "Script/Compiler/Types.h"

namespace schuimm::script {

const char* toString(Type type) noexcept
{
    if (isEnum(type)) return "enum";
    switch (type) {
    case Type::Error: return "<error>";
    case Type::None: return "None";
    case Type::Bool: return "bool";
    case Type::Int: return "int";
    case Type::Float: return "float";
    case Type::String: return "string";
    case Type::Any: return "any";
    case Type::Object: return "object";
    case Type::Actor: return "actor";
    case Type::Entity: return "entity";
    case Type::Vec2: return "vec2";
    case Type::Coroutine: return "coroutine";
    case Type::Operation: return "operation";
    case Type::Null: return "None";
    case Type::Named: return "<unresolved>";
    case Type::EnumBase: return "enum";
    }
    return "?";
}

TypeRef toTypeRef(Type type) noexcept
{
    if (isEnum(type)) return TypeRef::enumType(static_cast<std::uint16_t>(enumIndexOf(type)));
    switch (type) {
    case Type::Bool: return ScriptType::Bool;
    case Type::Int: return ScriptType::Int;
    case Type::Float: return ScriptType::Float;
    case Type::String: return ScriptType::String;
    case Type::Any: return ScriptType::Any;
    case Type::Object: return ScriptType::Object;
    case Type::Actor: return ScriptType::Actor;
    case Type::Entity: return ScriptType::Entity;
    case Type::Vec2: return ScriptType::Vec2;
    case Type::Coroutine: return ScriptType::Coroutine;
    case Type::Operation: return ScriptType::Operation;
    default: return ScriptType::None;
    }
}

std::optional<Type> typeFromName(std::string_view name) noexcept
{
    if (name == "int") return Type::Int;
    if (name == "float") return Type::Float;
    if (name == "bool") return Type::Bool;
    if (name == "string") return Type::String;
    if (name == "entity") return Type::Entity;
    if (name == "actor") return Type::Actor;
    if (name == "vec2") return Type::Vec2;
    if (name == "coroutine") return Type::Coroutine;
    if (name == "operation") return Type::Operation;
    return std::nullopt;
}

ConstantValue defaultValue(Type type)
{
    ConstantValue value;
    value.type = type;
    return value; // Zero, False, "", vec2(0, 0), ordinal 0 or None.
}

bool isAssignable(Type from, Type to) noexcept
{
    if (from == Type::Error || to == Type::Error) return true;
    if (from == Type::Null) return isNullable(to);
    if (from == to) return from != Type::None;
    if (from == Type::Int && to == Type::Float) return true;
    if (to == Type::Entity) return from == Type::Actor || from == Type::Object;
    if (to == Type::Any) return from != Type::None;
    return false;
}

} // namespace schuimm::script
